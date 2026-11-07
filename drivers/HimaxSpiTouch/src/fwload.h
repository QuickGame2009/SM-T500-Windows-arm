#pragma once
#include "device.h"

/*
 * fwload — firmware sin flash (himax_mcu_firmware_update_0f / himax_zf_part_info).
 * Fichero: Himax_firmware_lide_hsd.bin (128 KiB, de /vendor/firmware de la tablet).
 */
#define HX_FW_CFG_TABLE_OFFSET  0x10000   /* tabla de particiones dentro del .bin */
#define HX_FW_PART_ENTRY_SIZE   16
#define HX_FW_PART_COUNT_BYTE   12        /* byte 12 de la 1.ª entrada = nº de particiones */
#define HX_FW_MAX_PARTS         64
#define HX_DSRAM_SIZE           (32 * 1024)  /* DSRAM_SIZE = HX_32K_SZ */
#define HX_FW_CRC_POLY          0x82F63B78u  /* CRC-32C invertido (himax_mcu_Calculate_CRC_with_AP) */

typedef struct _HX_FW_PART {
    ULONG SramAddr;     /* dirección destino en SRAM */
    ULONG Size;         /* tamaño en bytes */
    ULONG FileOffset;   /* offset dentro del .bin */
} HX_FW_PART;

/* Guardar el .bin ya leído (lo lee driver.c desde el registro). */
VOID     FwSetImage(PDEVICE_CONTEXT Ctx, PUCHAR Data, ULONG Size);

/* CRC32 por software, igual que el del chip (para comparar). */
ULONG    FwCrc32(const UCHAR *Data, ULONG Len);

/* Analizar la tabla de particiones. */
NTSTATUS FwParsePartitions(PDEVICE_CONTEXT Ctx, HX_FW_PART *Parts, ULONG MaxParts, PULONG Count);

/* Secuencia completa: reset, sense_off, escribir SRAM + CRC, deshabilitar recarga. */
NTSTATUS FwDownload(PDEVICE_CONTEXT Ctx);
