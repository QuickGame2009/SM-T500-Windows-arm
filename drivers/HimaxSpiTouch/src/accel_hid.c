/*
 * accel_hid.c — sensor HID (página 0x20, acelerómetro 3D) con VHF.
 * Windows lo maneja con su driver de clase de sensores HID (SensorsHIDClassDriver),
 * y su servicio de sensores gira la pantalla a partir de estos datos.
 *
 * Descriptor según "HID Sensors Usages" de Microsoft. Las enumeraciones (campos de
 * tipo array) usan mínimo lógico 1: valor = posición del selector empezando en 1.
 * La aceleración va en g con exponente -3 (milésimas de g).
 */
#include "accel.h"
#include "util.h"

#define REPORTID_ACCEL  1

/* Enumeraciones (base 1) */
#define CONN_PC_INTEGRATED        1
#define REPORTING_NO_EVENTS       1
#define REPORTING_ALL_EVENTS      2
#define REPORTING_NO_EVENTS_WAKE  4
#define POWER_D0_FULL             2
#define STATE_READY               2
#define EVENT_DATA_UPDATED        4
#define EVENT_POLL_RESPONSE       5

static const UCHAR g_AccelReportDescriptor[] = {
    0x05, 0x20,                         /* Usage Page (Sensor) */
    0x09, 0x01,                         /* Usage (Sensor Type Collection) */
    0xA1, 0x01,                         /* Collection (Application) */
    0x85, REPORTID_ACCEL,               /*   Report ID */
    0x05, 0x20,                         /*   Usage Page (Sensor) */
    0x09, 0x73,                         /*   Usage (Motion: Accelerometer 3D) */
    0xA1, 0x00,                         /*   Collection (Physical) */

    /* ---- Feature ---- */
    0x0A, 0x09, 0x03,                   /* Property: Sensor Connection Type */
    0x15, 0x01, 0x25, 0x03, 0x75, 0x08, 0x95, 0x01,
    0xA1, 0x02,
      0x0A, 0x30, 0x08,                 /*   PC Integrated */
      0x0A, 0x31, 0x08,                 /*   PC Attached */
      0x0A, 0x32, 0x08,                 /*   PC External */
      0xB1, 0x00,                       /*   Feature (Data,Arr,Abs) */
    0xC0,
    0x0A, 0x16, 0x03,                   /* Property: Reporting State */
    0x15, 0x01, 0x25, 0x06, 0x75, 0x08, 0x95, 0x01,
    0xA1, 0x02,
      0x0A, 0x40, 0x08,                 /*   No Events */
      0x0A, 0x41, 0x08,                 /*   All Events */
      0x0A, 0x42, 0x08,                 /*   Threshold Events */
      0x0A, 0x43, 0x08,                 /*   No Events Wake */
      0x0A, 0x44, 0x08,                 /*   All Events Wake */
      0x0A, 0x45, 0x08,                 /*   Threshold Events Wake */
      0xB1, 0x00,
    0xC0,
    0x0A, 0x19, 0x03,                   /* Property: Power State */
    0x15, 0x01, 0x25, 0x06, 0x75, 0x08, 0x95, 0x01,
    0xA1, 0x02,
      0x0A, 0x50, 0x08,                 /*   Undefined */
      0x0A, 0x51, 0x08,                 /*   D0 Full Power */
      0x0A, 0x52, 0x08,                 /*   D1 Low Power */
      0x0A, 0x53, 0x08,                 /*   D2 Standby with Wake */
      0x0A, 0x54, 0x08,                 /*   D3 Sleep with Wake */
      0x0A, 0x55, 0x08,                 /*   D4 Power Off */
      0xB1, 0x00,
    0xC0,
    0x0A, 0x01, 0x02,                   /* Sensor State */
    0x15, 0x01, 0x25, 0x07, 0x75, 0x08, 0x95, 0x01,
    0xA1, 0x02,
      0x0A, 0x00, 0x08,                 /*   Unknown */
      0x0A, 0x01, 0x08,                 /*   Ready */
      0x0A, 0x02, 0x08,                 /*   Not Available */
      0x0A, 0x03, 0x08,                 /*   No Data */
      0x0A, 0x04, 0x08,                 /*   Initializing */
      0x0A, 0x05, 0x08,                 /*   Access Denied */
      0x0A, 0x06, 0x08,                 /*   Error */
      0xB1, 0x00,
    0xC0,
    0x0A, 0x0E, 0x03,                   /* Property: Report Interval (ms) */
    0x15, 0x00, 0x27, 0xFF, 0xFF, 0xFF, 0x7F, 0x75, 0x20, 0x95, 0x01, 0x55, 0x00,
    0xB1, 0x02,                         /* Feature (Data,Var,Abs) */
    0x0A, 0x52, 0x14,                   /* Acceleration | Change Sensitivity Abs */
    0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x55, 0x0D,
    0xB1, 0x02,
    0x0A, 0x52, 0x24,                   /* Acceleration | Maximum */
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x01, 0x55, 0x0D,
    0xB1, 0x02,
    0x0A, 0x52, 0x34,                   /* Acceleration | Minimum */
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x01, 0x55, 0x0D,
    0xB1, 0x02,

    /* ---- Input ---- */
    0x0A, 0x01, 0x02,                   /* Sensor State */
    0x15, 0x01, 0x25, 0x07, 0x75, 0x08, 0x95, 0x01,
    0xA1, 0x02,
      0x0A, 0x00, 0x08, 0x0A, 0x01, 0x08, 0x0A, 0x02, 0x08, 0x0A, 0x03, 0x08,
      0x0A, 0x04, 0x08, 0x0A, 0x05, 0x08, 0x0A, 0x06, 0x08,
      0x81, 0x00,                       /*   Input (Data,Arr,Abs) */
    0xC0,
    0x0A, 0x02, 0x02,                   /* Sensor Event */
    0x15, 0x01, 0x25, 0x06, 0x75, 0x08, 0x95, 0x01,
    0xA1, 0x02,
      0x0A, 0x10, 0x08,                 /*   Unknown */
      0x0A, 0x11, 0x08,                 /*   State Changed */
      0x0A, 0x12, 0x08,                 /*   Property Changed */
      0x0A, 0x13, 0x08,                 /*   Data Updated */
      0x0A, 0x14, 0x08,                 /*   Poll Response */
      0x0A, 0x15, 0x08,                 /*   Change Sensitivity */
      0x81, 0x00,
    0xC0,
    0x0A, 0x53, 0x04,                   /* Acceleration X */
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x01, 0x55, 0x0D,
    0x81, 0x02,                         /* Input (Data,Var,Abs) */
    0x0A, 0x54, 0x04,                   /* Acceleration Y */
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x01, 0x55, 0x0D,
    0x81, 0x02,
    0x0A, 0x55, 0x04,                   /* Acceleration Z */
    0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x01, 0x55, 0x0D,
    0x81, 0x02,

    0xC0,                               /*   End Collection (Physical) */
    0xC0                                /* End Collection (Application) */
};

/* Deben coincidir byte a byte con el descriptor. */
#pragma pack(push, 1)
typedef struct _ACCEL_FEATURE_REPORT {
    UCHAR  ReportId;
    UCHAR  ConnectionType;
    UCHAR  ReportingState;
    UCHAR  PowerState;
    UCHAR  SensorState;
    ULONG  ReportInterval;
    USHORT Sensitivity;
    SHORT  Maximum;
    SHORT  Minimum;
} ACCEL_FEATURE_REPORT;

typedef struct _ACCEL_INPUT_REPORT {
    UCHAR  ReportId;
    UCHAR  SensorState;
    UCHAR  Event;
    SHORT  X;
    SHORT  Y;
    SHORT  Z;
} ACCEL_INPUT_REPORT;
#pragma pack(pop)

static EVT_VHF_ASYNC_OPERATION EvtAccelGetFeature;
static EVT_VHF_ASYNC_OPERATION EvtAccelSetFeature;
static EVT_VHF_ASYNC_OPERATION EvtAccelGetInputReport;

static VOID EvtAccelGetFeature(PVOID ClientContext, VHFOPERATIONHANDLE Op,
                               PVOID OpContext, PHID_XFER_PACKET Packet)
{
    PDEVICE_CONTEXT c = (PDEVICE_CONTEXT)ClientContext;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    UNREFERENCED_PARAMETER(OpContext);

    if (Packet->reportId == REPORTID_ACCEL && Packet->reportBufferLen >= sizeof(ACCEL_FEATURE_REPORT)) {
        ACCEL_FEATURE_REPORT *r = (ACCEL_FEATURE_REPORT *)Packet->reportBuffer;
        KIRQL irql;
        KeAcquireSpinLock(&c->Accel.Lock, &irql);
        r->ReportId       = REPORTID_ACCEL;
        r->ConnectionType = CONN_PC_INTEGRATED;
        r->ReportingState = c->Accel.ReportingState;
        r->PowerState     = c->Accel.PowerState;
        r->SensorState    = STATE_READY;
        r->ReportInterval = c->Accel.ReportIntervalMs;
        r->Sensitivity    = c->Accel.Sensitivity;
        r->Maximum        = 4000;       /* ±4 g */
        r->Minimum        = -4000;
        KeReleaseSpinLock(&c->Accel.Lock, irql);
        status = STATUS_SUCCESS;
    }
    VhfAsyncOperationComplete(Op, status);
}

static VOID EvtAccelSetFeature(PVOID ClientContext, VHFOPERATIONHANDLE Op,
                               PVOID OpContext, PHID_XFER_PACKET Packet)
{
    PDEVICE_CONTEXT c = (PDEVICE_CONTEXT)ClientContext;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    UNREFERENCED_PARAMETER(OpContext);

    if (Packet->reportId == REPORTID_ACCEL && Packet->reportBufferLen >= sizeof(ACCEL_FEATURE_REPORT)) {
        const ACCEL_FEATURE_REPORT *r = (const ACCEL_FEATURE_REPORT *)Packet->reportBuffer;
        KIRQL irql;
        KeAcquireSpinLock(&c->Accel.Lock, &irql);
        if (r->ReportingState >= 1 && r->ReportingState <= 6) c->Accel.ReportingState = r->ReportingState;
        if (r->PowerState >= 1 && r->PowerState <= 6)         c->Accel.PowerState = r->PowerState;
        if (r->ReportInterval != 0)                           c->Accel.ReportIntervalMs = r->ReportInterval;
        c->Accel.Sensitivity = r->Sensitivity;
        KeReleaseSpinLock(&c->Accel.Lock, irql);
        status = STATUS_SUCCESS;
    }
    VhfAsyncOperationComplete(Op, status);
}

static VOID EvtAccelGetInputReport(PVOID ClientContext, VHFOPERATIONHANDLE Op,
                                   PVOID OpContext, PHID_XFER_PACKET Packet)
{
    PDEVICE_CONTEXT c = (PDEVICE_CONTEXT)ClientContext;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    UNREFERENCED_PARAMETER(OpContext);

    if (Packet->reportId == REPORTID_ACCEL && Packet->reportBufferLen >= sizeof(ACCEL_INPUT_REPORT)) {
        ACCEL_INPUT_REPORT *r = (ACCEL_INPUT_REPORT *)Packet->reportBuffer;
        KIRQL irql;
        KeAcquireSpinLock(&c->Accel.Lock, &irql);
        r->ReportId    = REPORTID_ACCEL;
        r->SensorState = STATE_READY;
        r->Event       = EVENT_POLL_RESPONSE;
        r->X = c->Accel.Last[0]; r->Y = c->Accel.Last[1]; r->Z = c->Accel.Last[2];
        KeReleaseSpinLock(&c->Accel.Lock, irql);
        status = STATUS_SUCCESS;
    }
    VhfAsyncOperationComplete(Op, status);
}

NTSTATUS AccelHidCreate(PDEVICE_CONTEXT c)
{
    VHF_CONFIG cfg;
    VHF_CONFIG_INIT(&cfg, WdfDeviceWdmGetDeviceObject(c->Device),
                    sizeof(g_AccelReportDescriptor), (PUCHAR)g_AccelReportDescriptor);
    cfg.VhfClientContext = c;
    cfg.EvtVhfAsyncOperationGetFeature     = EvtAccelGetFeature;
    cfg.EvtVhfAsyncOperationSetFeature     = EvtAccelSetFeature;
    cfg.EvtVhfAsyncOperationGetInputReport = EvtAccelGetInputReport;
    cfg.VendorID  = 0x0C62;   /* inventados, como los del táctil */
    cfg.ProductID = 0x0270;
    cfg.VersionNumber = 0x0001;

    NTSTATUS s = VhfCreate(&cfg, &c->Accel.Vhf);
    HxLog("ACC: VhfCreate (sensor HID, %u bytes de descriptor) = %08X",
          (ULONG)sizeof(g_AccelReportDescriptor), (ULONG)s);
    if (!NT_SUCCESS(s)) { c->Accel.Vhf = NULL; return s; }
    s = VhfStart(c->Accel.Vhf);
    HxLog("ACC: VhfStart = %08X", (ULONG)s);
    if (NT_SUCCESS(s)) c->Accel.VhfStarted = TRUE;
    return s;
}

VOID AccelHidDelete(PDEVICE_CONTEXT c)
{
    if (c->Accel.Vhf) {
        VhfDelete(c->Accel.Vhf, TRUE);
        c->Accel.Vhf = NULL;
        c->Accel.VhfStarted = FALSE;
    }
}

NTSTATUS AccelHidSubmit(PDEVICE_CONTEXT c, const SHORT Mg[3])
{
    ACCEL_INPUT_REPORT r;
    HID_XFER_PACKET p;
    if (!c->Accel.VhfStarted) return STATUS_DEVICE_NOT_READY;
    r.ReportId = REPORTID_ACCEL;
    r.SensorState = STATE_READY;
    r.Event = EVENT_DATA_UPDATED;
    r.X = Mg[0]; r.Y = Mg[1]; r.Z = Mg[2];
    p.reportId = REPORTID_ACCEL;
    p.reportBuffer = (PUCHAR)&r;
    p.reportBufferLen = sizeof(r);
    return VhfReadReportSubmit(c->Accel.Vhf, &p);
}
