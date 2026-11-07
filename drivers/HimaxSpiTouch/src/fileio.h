#pragma once
#include "device.h"

/* Lee el .bin del firmware a memoria no paginada (ruta en el registro del
 * dispositivo, valor "FirmwarePath" que pone el INF). PASSIVE_LEVEL.
 * Deja el puntero y el tamaño en Ctx->Firmware / Ctx->FirmwareSize. */
NTSTATUS FwReadFileFromRegistry(PDEVICE_CONTEXT Ctx);
VOID     FwFreeFile(PDEVICE_CONTEXT Ctx);
