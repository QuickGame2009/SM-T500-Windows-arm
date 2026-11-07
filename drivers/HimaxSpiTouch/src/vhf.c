#include "vhfdev.h"
#include "hid_desc.h"
#include "util.h"

/* GET_FEATURE: responder al Contact Count Maximum. */
static EVT_VHF_ASYNC_OPERATION EvtVhfGetFeature;

static VOID EvtVhfGetFeature(PVOID VhfClientContext,
                             VHFOPERATIONHANDLE VhfOperationHandle,
                             PVOID VhfOperationContext, PHID_XFER_PACKET HidTransferPacket)
{
    UNREFERENCED_PARAMETER(VhfClientContext);
    UNREFERENCED_PARAMETER(VhfOperationContext);

    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (HidTransferPacket->reportId == REPORTID_MAX_COUNT &&
        HidTransferPacket->reportBufferLen >= sizeof(HID_MAX_COUNT_REPORT)) {
        HID_MAX_COUNT_REPORT *r = (HID_MAX_COUNT_REPORT *)HidTransferPacket->reportBuffer;
        r->ReportId = REPORTID_MAX_COUNT;
        r->ContactCountMax = TOUCH_MAX_CONTACTS;
        status = STATUS_SUCCESS;
    }
    VhfAsyncOperationComplete(VhfOperationHandle, status);
}

NTSTATUS VhfDeviceCreate(PDEVICE_CONTEXT Ctx)
{
    VHF_CONFIG cfg;
    VHF_CONFIG_INIT(&cfg,
                    WdfDeviceWdmGetDeviceObject(Ctx->Device),
                    sizeof(g_TouchReportDescriptor),
                    (PUCHAR)g_TouchReportDescriptor);

    cfg.VhfClientContext = Ctx;
    cfg.EvtVhfAsyncOperationGetFeature = EvtVhfGetFeature;
    /* IDs inventados (no hay USB-IF para esto). */
    cfg.VendorID  = 0x0C62;   /* Himax */
    cfg.ProductID = 0x83E0;
    cfg.VersionNumber = 0x0001;

    NTSTATUS status = VhfCreate(&cfg, &Ctx->VhfHandle);
    if (!NT_SUCCESS(status))
        HxLog("VhfCreate ha fallado: %08X", status);
    return status;
}

NTSTATUS VhfDeviceStart(PDEVICE_CONTEXT Ctx)
{
    NTSTATUS status = VhfStart(Ctx->VhfHandle);
    if (NT_SUCCESS(status)) Ctx->VhfStarted = TRUE;
    else HxLog("VhfStart ha fallado: %08X", status);
    return status;
}

NTSTATUS VhfDeviceSubmit(PDEVICE_CONTEXT Ctx, HID_TOUCH_REPORT *Report)
{
    HID_XFER_PACKET p;
    p.reportId = Report->ReportId;
    p.reportBuffer = (PUCHAR)Report;
    p.reportBufferLen = sizeof(*Report);
    return VhfReadReportSubmit(Ctx->VhfHandle, &p);
}

VOID VhfDeviceDelete(PDEVICE_CONTEXT Ctx)
{
    if (Ctx->VhfHandle) {
        VhfDelete(Ctx->VhfHandle, TRUE);
        Ctx->VhfHandle = NULL;
    }
}
