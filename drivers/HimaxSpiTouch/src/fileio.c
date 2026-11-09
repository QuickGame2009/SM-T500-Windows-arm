#include "fileio.h"
#include "fwload.h"
#include "util.h"

/* Lee "FirmwarePath" de la clave de hardware del dispositivo y carga el fichero. */
NTSTATUS FwReadFileFromRegistry(PDEVICE_CONTEXT Ctx)
{
    NTSTATUS status;
    WDFKEY key = NULL;
    WDFSTRING wstr = NULL;
    DECLARE_CONST_UNICODE_STRING(valName, L"FirmwarePath");
    UNICODE_STRING path;

    status = WdfDeviceOpenRegistryKey(Ctx->Device, PLUGPLAY_REGKEY_DEVICE,
                                      KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (!NT_SUCCESS(status)) { HxLog("no se abre la clave del dispositivo: %08X", status); return status; }

    status = WdfStringCreate(NULL, WDF_NO_OBJECT_ATTRIBUTES, &wstr);
    if (!NT_SUCCESS(status)) goto done;

    status = WdfRegistryQueryString(key, &valName, wstr);
    if (!NT_SUCCESS(status)) { HxLog("FirmwarePath no encontrado: %08X", status); goto done; }
    WdfStringGetUnicodeString(wstr, &path);

    /* Abrir y leer el fichero con Zw*. Debe ocurrir a PASSIVE_LEVEL y con el
     * sistema de ficheros montado (lo llamamos desde el hilo de trabajo). */
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE h = NULL;
    InitializeObjectAttributes(&oa, &path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = ZwCreateFile(&h, GENERIC_READ | SYNCHRONIZE, &oa, &iosb, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(status)) { HxLog("no se abre el firmware: %08X", status); goto done; }

    FILE_STANDARD_INFORMATION fi;
    status = ZwQueryInformationFile(h, &iosb, &fi, sizeof(fi), FileStandardInformation);
    if (!NT_SUCCESS(status)) { ZwClose(h); goto done; }

    /* Validar el tamaño de 64 bits antes de convertirlo a ULONG. */
    if (fi.EndOfFile.QuadPart <= 0 || fi.EndOfFile.QuadPart > (1LL << 20) ||
        fi.EndOfFile.QuadPart > MAXULONG) {
        status = STATUS_FILE_INVALID;
        ZwClose(h);
        goto done;
    }
    ULONG size = (ULONG)fi.EndOfFile.QuadPart;

    PUCHAR buf = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, size, 'xmiH');
    if (!buf) { status = STATUS_INSUFFICIENT_RESOURCES; ZwClose(h); goto done; }

    LARGE_INTEGER off; off.QuadPart = 0;
    status = ZwReadFile(h, NULL, NULL, NULL, &iosb, buf, size, &off, NULL);
    ZwClose(h);
    if (!NT_SUCCESS(status) || iosb.Information != size) {
        ExFreePoolWithTag(buf, 'xmiH');
        status = NT_SUCCESS(status) ? STATUS_END_OF_FILE : status;
        goto done;
    }

    FwSetImage(Ctx, buf, size);
    HxLog("firmware leído: %u bytes", size);
    status = STATUS_SUCCESS;

done:
    if (wstr) WdfObjectDelete(wstr);
    if (key)  WdfRegistryClose(key);
    return status;
}

VOID FwFreeFile(PDEVICE_CONTEXT Ctx)
{
    if (Ctx->Firmware) {
        ExFreePoolWithTag(Ctx->Firmware, 'xmiH');
        Ctx->Firmware = NULL;
        Ctx->FirmwareSize = 0;
    }
}
