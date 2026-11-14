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

/*
 * Posición de la tabla de particiones (TP_CONFIG_TABLE). Réplica de
 * hx_mcu_bin_desc_get / hx_bin_desc_data_get del driver de Himax:
 * si el .bin empieza con una tabla de descripción (8 primeros bytes a 0 y byte
 * 0x0E == 0x87), se recorren entradas de 16 bytes; las que suman 0 (mod 256)
 * traen [0..3] = código de mapeo y [4..7] = dirección. Código 0x0A00 =
 * TP_CONFIG_TABLE. Una entrada toda a cero termina la tabla. Si no hay tabla de
 * descripción o no trae esa entrada, se usa el valor por defecto 0x10000.
 */
#define HX_MAP_TP_CONFIG_TABLE  0x00000A00u

static ULONG FwFindCfgTable(PDEVICE_CONTEXT Ctx)
{
    const UCHAR *fw = Ctx->Firmware;
    ULONG size = Ctx->FirmwareSize;
    ULONG found = HX_FW_CFG_TABLE_OFFSET;

    if (size < 128) return found;
    for (ULONG i = 0; i < 8; i++) if (fw[i] != 0) return found;
    if (fw[0x0E] != 0x87) return found;

    for (ULONG off = 0; off + 16 <= size; off += 16) {
        ULONG orAll = 0, sum = 0;
        for (ULONG j = 0; j < 16; j++) { orAll |= fw[off + j]; sum += fw[off + j]; }
        if (orAll == 0) break;                 /* fin de la tabla */
        if ((sum & 0xFF) != 0) continue;       /* suma de control incorrecta */
        ULONG code = HxGetLe32(fw + off);
        ULONG addr = HxGetLe32(fw + off + 4);
        if (code == HX_MAP_TP_CONFIG_TABLE) {
            HxLog("tabla de descripción: TP_CONFIG_TABLE en %05X (entrada %05X)", addr, off);
            found = addr;
        }
    }
    return found;
}

NTSTATUS FwParsePartitions(PDEVICE_CONTEXT Ctx, HX_FW_PART *Parts, ULONG MaxParts, PULONG Count)
{
    if (!Ctx || !Parts || !Count || MaxParts == 0 || !Ctx->Firmware)
        return STATUS_INVALID_PARAMETER;

    ULONG base = FwFindCfgTable(Ctx);
    if ((ULONGLONG)base + HX_FW_PART_ENTRY_SIZE > Ctx->FirmwareSize ||
        Ctx->FirmwareSize <= base + HX_FW_PART_COUNT_BYTE) {
        HxLog("firmware: la tabla (%05X) se sale del fichero (%u bytes)", base, Ctx->FirmwareSize);
        return STATUS_INVALID_PARAMETER;
    }
    HxLog("firmware: tabla de particiones en %05X", base);

    ULONG n = Ctx->Firmware[base + HX_FW_PART_COUNT_BYTE];
    if (n < 1 || n > MaxParts || n > HX_FW_MAX_PARTS) {
        HxLog("firmware: nº de particiones fuera de rango (%u)", n);
        /* Volcado para ver el formato real del .bin. */
        for (ULONG off = 0; off < 0x40 && off + 16 <= Ctx->FirmwareSize; off += 16) {
            const UCHAR *p = Ctx->Firmware + off;
            HxLog("fw[%05X]: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                  off, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
        }
        for (ULONG off = base; off < base + 0x80 && off + 16 <= Ctx->FirmwareSize; off += 16) {
            const UCHAR *p = Ctx->Firmware + off;
            HxLog("fw[%05X]: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                  off, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
        }
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

/* La entidad principal del firmware se valida con CRC hardware == 0 (kernel Himax). */
static NTSTATUS FwWriteMainPart(PDEVICE_CONTEXT Ctx, const HX_FW_PART *p)
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
        HxLog("CRC de entidad principal no es cero (%08X), intento %u", crc, retry + 1);
    }
    return STATUS_CRC_ERROR;
}

/* Configuración DSRAM agrupada como himax_zf_part_info(); HX_CODE_OVERLAY
 * está desactivado en himax_common.h para este kernel. */
static NTSTATUS FwWriteConfigBlock(PDEVICE_CONTEXT Ctx,
                                   const HX_FW_PART *Parts, ULONG Count)
{
    ULONG base = MAXULONG;
    ULONGLONG end = 0;
    PUCHAR image = NULL;
    ULONG length, expected, actual;
    NTSTATUS status = STATUS_SUCCESS;

    if (Count <= 1) return STATUS_SUCCESS;

    for (ULONG i = 1; i < Count; i++) {
        ULONG addr = Parts[i].SramAddr & ~3u;
        ULONGLONG partEnd = (ULONGLONG)addr + Parts[i].Size;
        if (Parts[i].Size == 0 || partEnd > 0x100000000ull)
            return STATUS_INVALID_PARAMETER;
        if (addr < base) base = addr;
        if (partEnd > end) end = partEnd;
    }
    if (base == MAXULONG || end <= base) return STATUS_INVALID_PARAMETER;

    ULONGLONG span = (end - base + 15u) & ~15ull;
    if (span == 0 || span > HX_DSRAM_SIZE || span > MAXULONG)
        return STATUS_INVALID_BUFFER_SIZE;
    length = (ULONG)span;

    image = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, length, 'xmiH');
    if (!image) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(image, length);

    for (ULONG i = 1; i < Count; i++) {
        ULONG addr = Parts[i].SramAddr & ~3u;
        ULONG offset = addr - base;
        if ((ULONGLONG)offset + Parts[i].Size > length) {
            status = STATUS_INVALID_PARAMETER;
            goto done;
        }
        RtlCopyMemory(image + offset, Ctx->Firmware + Parts[i].FileOffset,
                      Parts[i].Size);
    }

    expected = FwCrc32(image, length);
    for (ULONG retry = 0; retry < 4; retry++) {
        status = HimaxRegWrite(Ctx, base, image, length);
        if (!NT_SUCCESS(status)) goto done;
        actual = HimaxCheckCrcHw(Ctx, base, length);
        if (actual == expected) {
            HxLog("CRC de configuración correcto: %08X (%u bytes)", actual, length);
            status = STATUS_SUCCESS;
            goto done;
        }
        HxLog("CRC de configuración distinto: HW=%08X SW=%08X, intento %u",
              actual, expected, retry + 1);
    }
    status = STATUS_CRC_ERROR;
done:
    ExFreePoolWithTag(image, 'xmiH');
    return status;
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

    /* Partición 0 = entidad principal; el resto = bloque de configuración. */
    s = FwWriteMainPart(Ctx, &parts[0]);
    if (!NT_SUCCESS(s)) return s;
    s = FwWriteConfigBlock(Ctx, parts, count);
    if (!NT_SUCCESS(s)) return s;

    s = HimaxRegWrite32(Ctx, HX_REG_SET_FRAME, 0x00000001); /* n frame = 1 */
    if (!NT_SUCCESS(s)) return s;
    /* Como himax_mcu_0f_operation: reload_disable(0) = escribir 0 (reload habilitado),
     * NO 0x9AA9. Con 0x9AA9 el firmware no arrancaba el sensado. */
    s = HimaxRegWrite32(Ctx, HX_REG_FLASH_RELOAD, HX_DATA_RELOAD_EN);
    if (!NT_SUCCESS(s)) return s;
    HxLog("reload_disable(0) escrito en %08X", HX_REG_FLASH_RELOAD);

    Ctx->FirmwareLoaded = TRUE;
    HxLog("firmware cargado (%u particiones)", count);
    return STATUS_SUCCESS;
}
