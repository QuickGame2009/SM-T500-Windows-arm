/*
 * driver.c — entrada del driver, ciclo PnP/energía y arranque del táctil.
 */
#include "device.h"
#include "clocks.h"
#include "geni_spi.h"
#include "tlmm.h"
#include "himax.h"
#include "fwload.h"
#include "fileio.h"
#include "report.h"
#include "vhfdev.h"
#include "util.h"

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD        EvtDeviceAdd;
static EVT_WDF_DEVICE_PREPARE_HARDWARE  EvtPrepareHardware;
static EVT_WDF_DEVICE_RELEASE_HARDWARE  EvtReleaseHardware;
static EVT_WDF_DEVICE_D0_ENTRY          EvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT           EvtD0Exit;
static KSTART_ROUTINE                   WorkerThread;

/* ---- mapeo de MMIO ---- */

static PUCHAR MapResource(WDFCMRESLIST Translated, ULONG Index, SIZE_T *OutLen)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG seen = 0;
    for (ULONG i = 0; i < WdfCmResourceListGetCount(Translated); i++) {
        d = WdfCmResourceListGetDescriptor(Translated, i);
        if (d->Type != CmResourceTypeMemory) continue;
        if (seen == Index) {
            *OutLen = d->u.Memory.Length;
            return (PUCHAR)MmMapIoSpaceEx(d->u.Memory.Start, d->u.Memory.Length,
                                          PAGE_READWRITE | PAGE_NOCACHE);
        }
        seen++;
    }
    *OutLen = 0;
    return NULL;
}

static NTSTATUS EvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Raw);

    c->GeniSe0  = MapResource(Translated, 0, &c->GeniSe0Len);
    c->TlmmWest = MapResource(Translated, 1, &c->TlmmWestLen);
    c->TlmmEast = MapResource(Translated, 2, &c->TlmmEastLen);
    c->Gcc      = MapResource(Translated, 3, &c->GccLen);
    c->QupWrap  = MapResource(Translated, 4, &c->QupWrapLen);

    if (!c->GeniSe0 || !c->TlmmWest || !c->TlmmEast || !c->Gcc || !c->QupWrap) {
        HxLog("faltan recursos de memoria en el SSDT");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static VOID UnmapOne(PUCHAR *p, SIZE_T len)
{
    if (*p) { MmUnmapIoSpace(*p, len); *p = NULL; }
}

static NTSTATUS EvtReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Translated)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Translated);
    UnmapOne(&c->GeniSe0, c->GeniSe0Len);
    UnmapOne(&c->TlmmWest, c->TlmmWestLen);
    UnmapOne(&c->TlmmEast, c->TlmmEastLen);
    UnmapOne(&c->Gcc, c->GccLen);
    UnmapOne(&c->QupWrap, c->QupWrapLen);
    return STATUS_SUCCESS;
}

/* ---- arranque del táctil y bucle de sondeo, en un hilo propio ---- */

static NTSTATUS BringUpTouch(PDEVICE_CONTEXT c)
{
    NTSTATUS s;
    ULONG seHz = 0, clkSel = 0;

    s = ClocksEnableQup0Se0(c, &seHz, &clkSel);
    if (!NT_SUCCESS(s)) return s;
    c->SeClkHz = seHz; c->ClkSel = clkSel;

    s = GeniSpiInit(c);                 if (!NT_SUCCESS(s)) return s;
    s = TlmmConfigure(c);               if (!NT_SUCCESS(s)) return s;
    s = HimaxDetect(c);                 if (!NT_SUCCESS(s)) return s;

    s = FwReadFileFromRegistry(c);      if (!NT_SUCCESS(s)) return s;
    s = FwDownload(c);                  if (!NT_SUCCESS(s)) return s;
    s = HimaxSenseOn(c);                if (!NT_SUCCESS(s)) return s;

    s = VhfDeviceStart(c);              return s;
}

static VOID WorkerThread(PVOID Context)
{
    PDEVICE_CONTEXT c = (PDEVICE_CONTEXT)Context;
    UCHAR buf[HX_TOUCH_INFO_SIZE];
    TOUCH_DATA td;
    HID_TOUCH_REPORT rep;

    NTSTATUS s = BringUpTouch(c);
    if (!NT_SUCCESS(s)) {
        HxLog("arranque del táctil fallido: %08X; el hilo termina", s);
        PsTerminateSystemThread(s);
        return;
    }

    HxLog("táctil listo; entrando en el bucle de sondeo");
    for (;;) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)6 * 10000;   /* ~6 ms */
        if (KeWaitForSingleObject(&c->StopEvent, Executive, KernelMode, FALSE, &due)
            == STATUS_SUCCESS)
            break;   /* StopEvent señalado */

        /* Flanco pendiente, o nivel bajo = línea de interrupción activa (activo bajo). */
        BOOLEAN pending = TlmmTouchEdgePending(c);
        if (!pending && TlmmReadInput(c, GPIO_TOUCH_IRQ) != 0)
            continue;

        KeWaitForSingleObject(&c->SpiLock, Executive, KernelMode, FALSE, NULL);
        s = HimaxReadEventStack(c, buf, HX_TOUCH_INFO_SIZE);
        KeReleaseMutex(&c->SpiLock, FALSE);
        if (!NT_SUCCESS(s)) continue;

        if (!NT_SUCCESS(ReportParse(buf, &td))) continue;
        ReportBuildHid(c, &td, &rep);
        VhfDeviceSubmit(c, &rep);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static NTSTATUS EvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Prev)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Prev);

    KeClearEvent(&c->StopEvent);

    HANDLE h;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    NTSTATUS s = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, &oa, NULL, NULL,
                                      WorkerThread, c);
    if (!NT_SUCCESS(s)) return s;
    ObReferenceObjectByHandle(h, THREAD_ALL_ACCESS, *PsThreadType, KernelMode,
                              (PVOID *)&c->WorkerThread, NULL);
    ZwClose(h);
    return STATUS_SUCCESS;
}

static NTSTATUS EvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Target);

    KeSetEvent(&c->StopEvent, IO_NO_INCREMENT, FALSE);
    if (c->WorkerThread) {
        KeWaitForSingleObject(c->WorkerThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(c->WorkerThread);
        c->WorkerThread = NULL;
    }
    if (c->ChipDetected) HimaxSenseOff(c, FALSE);
    TlmmWriteOutput(c, GPIO_TOUCH_RST, 0);   /* reset en bajo */
    FwFreeFile(c);
    return STATUS_SUCCESS;
}

static VOID EvtDeviceCleanup(WDFOBJECT Device)
{
    PDEVICE_CONTEXT c = GetDeviceContext((WDFDEVICE)Device);
    VhfDeviceDelete(c);
    if (c->TxBuffer) { ExFreePoolWithTag(c->TxBuffer, 'xmiH'); c->TxBuffer = NULL; }
}

static NTSTATUS EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);
    NTSTATUS status;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = EvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware = EvtReleaseHardware;
    pnp.EvtDeviceD0Entry = EvtD0Entry;
    pnp.EvtDeviceD0Exit = EvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, DEVICE_CONTEXT);
    attr.EvtCleanupCallback = EvtDeviceCleanup;

    WDFDEVICE device;
    status = WdfDeviceCreate(&DeviceInit, &attr, &device);
    if (!NT_SUCCESS(status)) return status;

    PDEVICE_CONTEXT c = GetDeviceContext(device);
    RtlZeroMemory(c, sizeof(*c));
    c->Device = device;
    KeInitializeMutex(&c->SpiLock, 0);
    KeInitializeEvent(&c->StopEvent, NotificationEvent, FALSE);
    KeQueryPerformanceCounter(&c->QpcFreq);

    c->TxBuffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, HX_TX_BUFFER_SIZE, 'xmiH');
    if (!c->TxBuffer) return STATUS_INSUFFICIENT_RESOURCES;

    status = VhfDeviceCreate(c);        /* crea el HID virtual (se inicia en D0Entry) */
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);
}
