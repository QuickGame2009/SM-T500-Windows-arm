#pragma once
#include "device.h"
#include "report.h"

/*
 * vhfdev — dispositivo HID virtual con VHF (vhf.h, VhfKm.lib).
 * El INF añade "vhf" como filtro inferior.
 */
NTSTATUS VhfDeviceCreate(PDEVICE_CONTEXT Ctx);
NTSTATUS VhfDeviceStart (PDEVICE_CONTEXT Ctx);
NTSTATUS VhfDeviceSubmit(PDEVICE_CONTEXT Ctx, HID_TOUCH_REPORT *Report);
VOID     VhfDeviceDelete(PDEVICE_CONTEXT Ctx);
