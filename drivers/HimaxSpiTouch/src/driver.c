/*
 * driver.c — entrada del driver, ciclo PnP/energía y arranque del táctil.
 */
#include <ntifs.h> /* ZwWaitForSingleObject: prototipo WDK */
#include "device.h"
#include "clocks.h"
#include "geni_spi.h"
#include "tlmm.h"
#include "himax.h"
#include "fwload.h"
#include "fileio.h"
#include "report.h"
#include "vhfdev.h"
#include "accel.h"
#include "util.h"

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD        EvtDeviceAdd;
static EVT_WDF_DEVICE_PREPARE_HARDWARE  EvtPrepareHardware;
static EVT_WDF_DEVICE_RELEASE_HARDWARE  EvtReleaseHardware;
static EVT_WDF_DEVICE_D0_ENTRY          EvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT           EvtD0Exit;
static KSTART_ROUTINE                   WorkerThread;

/* ---- mapeo de MMIO ---- */

/* Direcciones físicas de cada bloque. Se busca por dirección y no por índice:
 * el SSDT de la UEFI puede traer 4 o 5 recursos (con TLMM oeste y este por
 * separado) y el índice cambia según la versión. */
#define PHYS_SE0      0x04A80000ULL
#define PHYS_TLMM     0x00500000ULL
#define PHYS_GCC      0x01400000ULL
#define PHYS_QUPWRAP  0x04AC0000ULL

static VOID LogResources(WDFCMRESLIST Translated)
{
    ULONG n = 0;
    for (ULONG i = 0; i < WdfCmResourceListGetCount(Translated); i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(Translated, i);
        if (d->Type != CmResourceTypeMemory) continue;
        HxLog("recurso MMIO %lu: base=%llX longitud=%lX", n++,
              (ULONGLONG)d->u.Memory.Start.QuadPart, d->u.Memory.Length);
    }
}

static PUCHAR MapByPhys(WDFCMRESLIST Translated, ULONGLONG Phys, SIZE_T MinLen, SIZE_T *OutLen)
{
    for (ULONG i = 0; i < WdfCmResourceListGetCount(Translated); i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(Translated, i);
        if (d->Type != CmResourceTypeMemory) continue;
        if ((ULONGLONG)d->u.Memory.Start.QuadPart != Phys) continue;
        if (d->u.Memory.Length < MinLen) {
            HxLog("recurso %llX demasiado pequeño: %lX < %Iu", Phys, d->u.Memory.Length, MinLen);
            break;
        }
        *OutLen = d->u.Memory.Length;
        return (PUCHAR)MmMapIoSpaceEx(d->u.Memory.Start, d->u.Memory.Length,
                                      PAGE_READWRITE | PAGE_NOCACHE);
    }
    *OutLen = 0;
    return NULL;
}

static VOID UnmapOne(PUCHAR *p, SIZE_T len);

static NTSTATUS EvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Translated)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Raw);

    HxLog("PrepareHardware: mapeando MMIO");
    LogResources(Translated);
    c->GeniSe0 = MapByPhys(Translated, PHYS_SE0,     0x4000,   &c->GeniSe0Len);
    c->Tlmm    = MapByPhys(Translated, PHYS_TLMM,    0x80000,  &c->TlmmLen);
    c->Gcc     = MapByPhys(Translated, PHYS_GCC,     0x1F0000, &c->GccLen);
    c->QupWrap = MapByPhys(Translated, PHYS_QUPWRAP, 0x2000,   &c->QupWrapLen);

    if (!c->GeniSe0 || !c->Tlmm || !c->Gcc || !c->QupWrap) {
        HxLog("recursos MMIO ausentes o demasiado pequeños en el SSDT");
        UnmapOne(&c->GeniSe0, c->GeniSe0Len);
        UnmapOne(&c->Tlmm, c->TlmmLen);
        UnmapOne(&c->Gcc, c->GccLen);
        UnmapOne(&c->QupWrap, c->QupWrapLen);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    HxLog("PrepareHardware OK: SE0=%p(%llu) TLMM=%p(%llu) GCC=%p(%llu) WRAP=%p(%llu)",
          c->GeniSe0, (ULONGLONG)c->GeniSe0Len, c->Tlmm, (ULONGLONG)c->TlmmLen, c->Gcc, (ULONGLONG)c->GccLen, c->QupWrap, (ULONGLONG)c->QupWrapLen);
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
    if (c->Orphaned) { HxLog("ReleaseHardware: mapeos conservados (hilo abandonado)"); return STATUS_SUCCESS; }
    UnmapOne(&c->GeniSe0, c->GeniSe0Len);
    UnmapOne(&c->Tlmm, c->TlmmLen);
    UnmapOne(&c->Gcc, c->GccLen);
    UnmapOne(&c->QupWrap, c->QupWrapLen);
    return STATUS_SUCCESS;
}

/* ---- arranque del táctil y bucle de sondeo, en un hilo propio ---- */

/*
 * Etapas de arranque (valor DWORD "Stage" en
 * HKLM\SYSTEM\CurrentControlSet\Services\HimaxSpiTouch\Parameters).
 * Por defecto 6 (funcionamiento completo). 0 = cargar sin tocar el hardware.
 *   1 = relojes QUP0/SE0 (GCC)      2 = + init GENI SE0
 *   3 = + configuración TLMM        4 = + detectar el chip Himax
 *   5 = + leer el firmware y validar su tabla de particiones (SIN escribir al chip)
 *   6 = + descargar el firmware, sense on y HID virtual (funcionamiento completo)
 *   7 = como el kernel al arrancar: SIN cargar firmware (power_on_init + sense_on + HID)
 */
static ULONG ReadParam(PCWSTR Name, ULONG Default)
{
    ULONG value = Default;
    WDFKEY key = NULL;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, Name);
    if (NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_READ,
                                                      WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        ULONG v = 0;
        if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &v))) value = v;
        WdfRegistryClose(key);
    }
    return value;
}
#define ReadStage() ReadParam(L"Stage", 6)   /* por defecto: funcionamiento completo */

static NTSTATUS BringUpTouch(PDEVICE_CONTEXT c)
{
    NTSTATUS s;
    ULONG seHz = 0, clkSel = 0;

    c->Stage = ReadStage();
    c->ResetGpio = ReadParam(L"ResetGpio", GPIO_TOUCH_RST_DEFAULT);
    if (c->ResetGpio > 126) c->ResetGpio = GPIO_TOUCH_RST_DEFAULT;
    if (c->ResetGpio == 0) HxLog("ResetGpio=0: reset del chip por software (sin pin)");
    HxLog("=== arranque del táctil, Stage=%u ResetGpio=%u ===", c->Stage, c->ResetGpio);
    if (c->Stage < 1) { HxLog("Stage 0: no se toca el hardware"); return STATUS_SUCCESS; }

    HxLog("paso 1: relojes");
    s = ClocksEnableQup0Se0(c, &seHz, &clkSel);
    if (!NT_SUCCESS(s)) return s;
    c->SeClkHz = seHz; c->ClkSel = clkSel;
    HxLog("paso 1 OK");
    if (c->Stage < 2) return STATUS_SUCCESS;

    HxLog("paso 2: GENI SE0");
    s = GeniSpiInit(c);                 if (!NT_SUCCESS(s)) return s;
    HxLog("paso 2 OK");
    if (c->Stage < 3) return STATUS_SUCCESS;

    HxLog("paso 3: TLMM");
    s = TlmmConfigure(c);               if (!NT_SUCCESS(s)) return s;
    HxLog("paso 3 OK");
    if (c->Stage < 4) return STATUS_SUCCESS;

    HxLog("paso 4: detectar Himax");
    s = HimaxDetect(c);                 if (!NT_SUCCESS(s)) return s;
    HxLog("paso 4 OK");
    if (c->Stage < 5) return STATUS_SUCCESS;

    HxLog("paso 5: leer firmware y validar tabla");
    s = FwReadFileFromRegistry(c);      if (!NT_SUCCESS(s)) return s;
    {
        HX_FW_PART parts[HX_FW_MAX_PARTS];
        ULONG count = 0;
        s = FwParsePartitions(c, parts, HX_FW_MAX_PARTS, &count);
        if (!NT_SUCCESS(s)) return s;
        HxLog("paso 5 OK: %u particiones válidas (no se escribe al chip)", count);
    }
    if (c->Stage < 6) return STATUS_SUCCESS;

    if (c->Stage == 7) {
        HxLog("paso 6 (Stage 7): se omite la carga del firmware, como el kernel al arrancar");
    } else {
        HxLog("paso 6: descargar firmware");
        s = FwDownload(c);              if (!NT_SUCCESS(s)) return s;
    }
    /* Como himax_mcu_power_on_init: RawOut select = 0 y sorting mode = clear
     * antes de sense_on. Un firmware en modo raw/sorting no reporta toques. */
    {
        UCHAR d[4];
        s = HimaxRegWrite32(c, HX_REG_RAW_OUT_SEL, 0);
        HxLog("raw_out_sel=0 -> %08X", s);
        s = HimaxRegWrite32(c, HX_REG_SORTING_MODE, 0);
        HxLog("sorting_mode=0 -> %08X", s);
        /* touch_information: lo que el firmware cree que es el panel */
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_RXTX_MAXPT, d, 4)))
            HxLog("touch_info RX=%u TX=%u MAXPT=%u (100070F4=%02X%02X%02X%02X)", d[2], d[3], d[0], d[3],d[2],d[1],d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_XY_RES_ENABLE, d, 4)))
            HxLog("touch_info xy_reverse=%02X (100070F8=%02X%02X%02X%02X)", d[1], d[3],d[2],d[1],d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_X_Y_RES, d, 4)))
            HxLog("touch_info Y_RES=%u X_RES=%u (100070FC=%02X%02X%02X%02X)",
                  (ULONG)d[0] << 8 | d[1], (ULONG)d[2] << 8 | d[3], d[3],d[2],d[1],d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_INT_IS_EDGE, d, 4)))
            HxLog("touch_info int_is_edge=%u (10007088=%02X%02X%02X%02X)", d[0] & 1, d[3],d[2],d[1],d[0]);
    }
    HxLog("paso 6: sense on");
    s = HimaxSenseOn(c);
    HxLog("sense_on = %08X", s);
    if (!NT_SUCCESS(s)) return s;
    {
        UCHAR d[4];
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_FW_VER, d, 4)))
            HxLog("FW_VER (10007004) = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_FW_STATE, d, 4)))
            HxLog("FW_STATE (900000F8) = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_FW_DBG_MSG, d, 4)))
            HxLog("FW_DBG_MSG (10007F40) = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_RELOAD, d, 4)))
            HxLog("RELOAD (90000048) = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_CHK_FW_STATUS, d, 4)))
            HxLog("CHK_FW_STATUS (900000A8) = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_FLAG_RESET_EVT, d, 4)))
            HxLog("FLAG_RESET_EVENT (900000E4) = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
        HxSleepMs(200);
        if (NT_SUCCESS(HimaxRegRead(c, HX_REG_FW_DBG_MSG, d, 4)))
            HxLog("FW_DBG_MSG +200ms = %02X%02X%02X%02X", d[3], d[2], d[1], d[0]);
    }

    HxLog("paso 6: HID virtual");
    s = VhfDeviceStart(c);
    HxLog("paso 6 terminado: %08X", s);
    return s;
}

static VOID WorkerThread(PVOID Context)
{
    PDEVICE_CONTEXT c = (PDEVICE_CONTEXT)Context;
    UCHAR buf[HX_TOUCH_INFO_SIZE];
    TOUCH_DATA td;
    HID_TOUCH_REPORT rep;

    NTSTATUS s = BringUpTouch(c);
    c->InitStatus = s;
    KeSetEvent(&c->InitEvent, IO_NO_INCREMENT, FALSE);
    if (c->Orphaned) {
        HxLog("el hilo terminó tras ser abandonado (%08X)", s);
        PsTerminateSystemThread(s);
        return;
    }
    if (!NT_SUCCESS(s)) {
        HxLog("arranque del táctil fallido: %08X; el hilo termina", s);
        PsTerminateSystemThread(s);
        return;
    }

    if (c->Stage < 6) {
        /* Etapa parcial: esperar a que pare el dispositivo sin sondear. */
        KeWaitForSingleObject(&c->StopEvent, Executive, KernelMode, FALSE, NULL);
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    HxLog("táctil listo; entrando en el bucle de sondeo");
    {
        /* Estado inicial de la línea IRQ y del chip tras sense_on. */
        HxLog("GPIO80 tras sense_on: CTL=%08X IO=%08X INTR_CFG=%08X INTR_STATUS=%08X",
              TlmmReadRaw(c, GPIO_TOUCH_IRQ, 0), TlmmReadRaw(c, GPIO_TOUCH_IRQ, 4),
              TlmmReadRaw(c, GPIO_TOUCH_IRQ, 8), TlmmReadRaw(c, GPIO_TOUCH_IRQ, 0xC));
    }
    ULONG iter = 0, edges = 0, lows = 0, reads = 0, readErr = 0, badSum = 0, good = 0;
    ULONG edReloads = 0, excp = 0;
    ULONG lastIo = 0xFFFFFFFF;
    for (;;) {
        iter++;
        /* Cada ~6 s, un resumen; en las dos primeras lecturas, el paquete en bruto. */
        if ((iter % 10000) == 0) {
            HxLog("sondeo: iter=%u flancos=%u nivel_bajo=%u lecturas=%u err=%u badsum=%u ok=%u ED=%u excp=%u IO=%08X",
                  iter, edges, lows, reads, readErr, badSum, good, edReloads, excp,
                  TlmmReadRaw(c, GPIO_TOUCH_IRQ, 4));
        }
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)6 * 10000;   /* ~6 ms */
        if (KeWaitForSingleObject(&c->StopEvent, Executive, KernelMode, FALSE, &due)
            == STATUS_SUCCESS)
            break;   /* StopEvent señalado */

        /* Flanco pendiente, o nivel bajo = línea de interrupción activa (activo bajo). */
        BOOLEAN pending = TlmmTouchEdgePending(c);
        ULONG io = TlmmReadRaw(c, GPIO_TOUCH_IRQ, 4);
        lastIo = io;
        if (pending) edges++;
        BOOLEAN low = (io & TLMM_IO_IN) == 0;
        if (low) lows++;
        if (!pending && !low)
            continue;

        KeWaitForSingleObject(&c->SpiLock, Executive, KernelMode, FALSE, NULL);
        s = HimaxReadEventStack(c, buf, HX_TOUCH_INFO_SIZE);
        KeReleaseMutex(&c->SpiLock, FALSE);
        reads++;
        if (!NT_SUCCESS(s)) { readErr++; if (readErr <= 3) HxLog("event stack: error %08X", s); continue; }

        if (reads <= 2) {
            HxLog("stack[0..15]:  %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                  buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9],buf[10],buf[11],buf[12],buf[13],buf[14],buf[15]);
            HxLog("stack[40..55]: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                  buf[40],buf[41],buf[42],buf[43],buf[44],buf[45],buf[46],buf[47],buf[48],buf[49],buf[50],buf[51],buf[52],buf[53],buf[54],buf[55]);
        }
        /* himax_err_ctrl: el HX83102E devuelve todo 0xED tras un reset hasta que
         * se le manda reload_to_active; 0xEB/0xEC/0xEE todo igual = excepción. */
        {
            UCHAR first = buf[0]; ULONG same = 1;
            while (same < HX_TOUCH_INFO_SIZE && buf[same] == first) same++;
            if (same == HX_TOUCH_INFO_SIZE && first == 0xED) {
                edReloads++;
                if (edReloads <= 5 || (edReloads % 100) == 0)
                    HxLog("event stack todo 0xED (%u): reload_to_active", edReloads);
                KeWaitForSingleObject(&c->SpiLock, Executive, KernelMode, FALSE, NULL);
                s = HimaxReloadToActive(c);
                KeReleaseMutex(&c->SpiLock, FALSE);
                if (edReloads <= 5) HxLog("reload_to_active -> %08X", s);
                continue;
            }
            if (same == HX_TOUCH_INFO_SIZE && (first == 0xEB || first == 0xEC || first == 0xEE)) {
                excp++;
                if (excp <= 3 || (excp % 500) == 0) HxLog("event stack: excepción todo %02X (%u)", first, excp);
                continue;
            }
        }
        if (!NT_SUCCESS(ReportParse(buf, &td))) { badSum++; continue; }
        good++;
        if (good <= 3)
            HxLog("toque: n=%u x0=%u y0=%u", td.Count, td.Contacts[0].X, td.Contacts[0].Y);
        ReportBuildHid(c, &td, &rep);
        VhfDeviceSubmit(c, &rep);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static NTSTATUS EvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Prev)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Prev);
    HxLog("D0Entry");

    KeClearEvent(&c->StopEvent);
    KeClearEvent(&c->InitEvent);
    c->InitStatus = STATUS_PENDING;

    HANDLE h = NULL;
    OBJECT_ATTRIBUTES oa;
    PETHREAD thread = NULL;
    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    c->WorkerThread = NULL;
    NTSTATUS s = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, &oa, NULL, NULL,
                                      WorkerThread, c);
    if (!NT_SUCCESS(s)) return s;

    s = ObReferenceObjectByHandle(h, THREAD_ALL_ACCESS, *PsThreadType,
                                  KernelMode, (PVOID *)&thread, NULL);
    if (!NT_SUCCESS(s)) {
        /* No dejar un hilo huérfano si no podemos conservar su objeto. */
        KeSetEvent(&c->StopEvent, IO_NO_INCREMENT, FALSE);
        ZwWaitForSingleObject(h, FALSE, NULL);
        ZwClose(h);
        return s;
    }

    c->WorkerThread = thread;
    ZwClose(h);

    /* No marcar D0 iniciado hasta completar la inicialización del controlador. */
    {
        LARGE_INTEGER limit; limit.QuadPart = -(LONGLONG)15000 * 10000;   /* 15 s */
        if (KeWaitForSingleObject(&c->InitEvent, Executive, KernelMode, FALSE, &limit)
            == STATUS_TIMEOUT) {
            /* El hilo sigue ocupado (hardware sin respuesta). No bloquear a PnP:
             * se abandona el hilo, se mantienen vivos contexto y mapeos MMIO
             * y se falla D0Entry. */
            HxLog("D0Entry: el arranque no termina en 15 s; se abandona el hilo");
            InterlockedExchange(&c->Orphaned, 1);
            WdfObjectReference(c->Device);      /* el contexto no se libera nunca */
            KeSetEvent(&c->StopEvent, IO_NO_INCREMENT, FALSE);
            return STATUS_IO_TIMEOUT;
        }
    }
    s = c->InitStatus;
    if (!NT_SUCCESS(s)) {
        KeSetEvent(&c->StopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(c->WorkerThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(c->WorkerThread);
        c->WorkerThread = NULL;
        if (c->ChipDetected) (VOID)HimaxSenseOff(c, FALSE);
        /* No se fuerza el reset en bajo: en un TDDI apaga también la pantalla. */
        FwFreeFile(c);
        return s;
    }
    /* El acelerómetro va en su propio hilo y nunca hace fallar al táctil. */
    (VOID)AccelStart(c);
    return STATUS_SUCCESS;
}

static NTSTATUS EvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
    PDEVICE_CONTEXT c = GetDeviceContext(Device);
    UNREFERENCED_PARAMETER(Target);
    HxLog("D0Exit");

    KeSetEvent(&c->StopEvent, IO_NO_INCREMENT, FALSE);
    if (c->Orphaned) return STATUS_SUCCESS;     /* hilo abandonado: no tocar hardware */
    if (c->WorkerThread) {
        KeWaitForSingleObject(c->WorkerThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(c->WorkerThread);
        c->WorkerThread = NULL;
    }
    AccelStop(c);
    if (c->ChipDetected) HimaxSenseOff(c, FALSE);
    /* No se fuerza el reset en bajo: en un TDDI apaga también la pantalla. */
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
    HxLog("EvtDeviceAdd: inicio");
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
    HxLog("EvtDeviceAdd: antes de WdfDeviceCreate");
    status = WdfDeviceCreate(&DeviceInit, &attr, &device);
    HxLog("EvtDeviceAdd: WdfDeviceCreate = %08X", status);
    if (!NT_SUCCESS(status)) return status;

    PDEVICE_CONTEXT c = GetDeviceContext(device);
    RtlZeroMemory(c, sizeof(*c));
    c->Device = device;
    KeInitializeMutex(&c->SpiLock, 0);
    KeInitializeEvent(&c->StopEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&c->InitEvent, NotificationEvent, FALSE);
    c->InitStatus = STATUS_PENDING;
    KeQueryPerformanceCounter(&c->QpcFreq);

    c->TxBuffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, HX_TX_BUFFER_SIZE, 'xmiH');
    if (!c->TxBuffer) return STATUS_INSUFFICIENT_RESOURCES;

    HxLog("EvtDeviceAdd: antes de VhfCreate");
    status = VhfDeviceCreate(c);        /* crea el HID virtual (se inicia en D0Entry) */
    HxLog("EvtDeviceAdd: VhfCreate = %08X", status);
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
    HxLogInit();
    HxLog("DriverEntry (build con registro a fichero)");
    NTSTATUS st = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
                                  &config, WDF_NO_HANDLE);
    HxLog("DriverEntry: WdfDriverCreate = %08X", st);
    return st;
}
