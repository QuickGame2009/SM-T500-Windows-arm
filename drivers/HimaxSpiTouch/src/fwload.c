#include "fwload.h"
#include "himax.h"
#include "util.h"

VOID FwSetImage(PDEVICE_CONTEXT Ctx, PUCHAR Data, ULONG Size)
{
    Ctx->Firmware = Data;
    Ctx->FirmwareSize = Size;
}

/* himax_mcu_Calculate_CRC_with_AP: CRC-32C, acumulador 0xFFFFFFFF, por palabras LE. */
ULONG FwCrc32(const UCHAR *Data, ULONG Len)
{
    ULONG crc = 0xFFFFFFFF;
    ULONG words = Len / 4;

    for (ULONG i = 0; i < words; i++) {
        ULONG v = HxGetLe32(Data + i * 4);
        crc ^= v;
        for (ULONG j = 0; j < 32; j++) {
            if (crc & 1) crc = (crc >> 1) ^ HX_FW_CRC_POLY;
            else         crc = crc >> 1;
        }
    }
    return crc;
}

NTSTATUS FwParsePartitions(PDEVICE_CONTEXT Ctx, HX_FW_PART *Parts, ULONG MaxParts, PULONG Count)
{
    if (!Ctx->Firmware || Ctx->FirmwareSize < HX_FW_CFG_TABLE_OFFSET + HX_FW_PART_ENTRY_SIZE)
        return STATUS_INVALID_PARAMETER;

    ULONG base = HX_FW_CFG_TABLE_OFFSET;
    if (Ctx->FirmwareSize <= base + HX_FW_PART_COUNT_BYTE)
        return STATUS_INVALID_PARAMETER;

    ULONG n = Ctx->Firmware[base + HX_FW_PART_COUNT_BYTE];
    if (n < 1 || n > MaxParts || n > HX_FW_MAX_PARTS) {
        HxLog("firmware: nº de particiones fuera de rango (%u)", n);
        return STATUS_INVALID_PARAMETER;
    }

    /* Validar la tabla completa antes de calcular punteros a sus entradas. */
    ULONGLONG tableEnd = (ULONGLONG)base + HX_FW_PART_COUNT_BYTE + 1 +
                         (ULONGLONG)n * HX_FW_PART_ENTRY_SIZE;
    if (tableEnd > Ctx->FirmwareSize) {
        HxLog("firmware: tabla de particiones truncada");
        return STATUS_INVALID_PARAMETER;
    }

    for (ULONG i = 0; i < n; i++) {
        const UCHAR *e = Ctx->Firmware + base + i * HX_FW_PART_ENTRY_SIZE;
        Parts[i].SramAddr   = HxGetLe32(e + 0);
        Parts[i].Size       = HxGetLe32(e + 4);
        Parts[i].FileOffset = HxGetLe32(e + 8);
        HxLog("part %u: sram=%08X size=%u fileoff=%08X",
              i, Parts[i].SramAddr, Parts[i].Size, Parts[i].FileOffset);
        if ((ULONGLONG)Parts[i].FileOffset + Parts[i].Size > Ctx->FirmwareSize) {
            HxLog("part %u se sale del fichero", i);
            return STATUS_INVALID_PARAMETER;
        }
    }
    *Count = n;
    return STATUS_SUCCESS;
}

/* Escribe una partición en SRAM y comprueba el CRC por hardware (3 intentos). */
static NTSTATUS FwWritePartCrc(PDEVICE_CONTEXT Ctx, const HX_FW_PART *p)
{
    for (ULONG retry = 0; retry < 3; retry++) {
        NTSTATUS status = HimaxRegWrite(Ctx, p->SramAddr,
                                        Ctx->Firmware + p->FileOffset, p->Size);
        if (!NT_SUCCESS(status)) {
            HxLog("fallo escribiendo partición SRAM %08X: %08X",
                  p->SramAddr, status);
            return status;
        }
        ULONG crc = HimaxCheckCrcHw(Ctx, p->SramAddr, p->Size);
        if (crc == 0) return STATUS_SUCCESS;
        HxLog("CRC de sram=%08X ha fallado (%08X), reintento %u", p->SramAddr, crc, retry + 1);
    }
    return STATUS_CRC_ERROR;
}

NTSTATUS FwDownload(PDEVICE_CONTEXT Ctx)
{
    HX_FW_PART parts[HX_FW_MAX_PARTS];
    ULONG count = 0;
    NTSTATUS s;

    s = FwParsePartitions(Ctx, parts, HX_FW_MAX_PARTS, &count);
    if (!NT_SUCCESS(s)) return s;

    s = HimaxRegWrite32(Ctx, HX_REG_SYSTEM_RESET, HX_DATA_SYSTEM_RESET);
    if (!NT_SUCCESS(s)) return s;
    s = HimaxSenseOff(Ctx, FALSE);
    if (!NT_SUCCESS(s)) return s;

    /* La partición 0 es la entidad de FW; el resto, configuración/ADC/tabla.
     * El driver Android junta las de configuración en un bloque contiguo; aquí
     * se escribe partición por partición, que es funcionalmente equivalente y
     * más simple. TODO: si el CRC de alguna parte de config falla, pasar al
     * esquema de bloque contiguo de himax_zf_part_info (ordenar por cfg_addr). */
    for (ULONG i = 0; i < count; i++) {
        s = FwWritePartCrc(Ctx, &parts[i]);
        if (!NT_SUCCESS(s)) return s;
    }

    s = HimaxRegWrite32(Ctx, HX_REG_SET_FRAME, 0x00000001); /* n frame = 1 */
    if (!NT_SUCCESS(s)) return s;
    s = HimaxRegWrite32(Ctx, HX_REG_FLASH_RELOAD, HX_DATA_DIS_RELOAD);
    if (!NT_SUCCESS(s)) return s;

    Ctx->FirmwareLoaded = TRUE;
    HxLog("firmware cargado (%u particiones)", count);
    return STATUS_SUCCESS;
}
