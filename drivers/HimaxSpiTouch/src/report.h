#pragma once
#include "device.h"

/*
 * report — event stack → contactos → informe HID.
 * Formato (himax_common.c, HX_MAX_PT = 10, sin lápiz):
 *   coord_data_size = 4 * 10 = 40  →  bytes 0..39: {X alto, X bajo, Y alto, Y bajo} por dedo
 *   area_data_size  = (10/4 redondeado) * 4 = 12  →  w de cada dedo + relleno
 *   coordInfoSize   = 40 + 12 + 4 = 56
 *   byte coordInfoSize-4 (52), bits 0..3 = nº de dedos
 *   touch_info_size = (10 + ceil(10/4) + 1) * 4 = 56
 *   La suma de los touch_info_size bytes debe ser múltiplo de 0x100.
 *   X/Y son big-endian; fuera de 0..max = sin dedo.
 */
#define HX_TOUCH_INFO_SIZE   56
#define HX_COORD_SIZE        40
#define HX_AREA_OFFSET       40
#define HX_FINGER_NUM_OFFSET 52

typedef struct _TOUCH_CONTACT {
    BOOLEAN Down;
    USHORT  X;
    USHORT  Y;
    UCHAR   Width;
} TOUCH_CONTACT;

typedef struct _TOUCH_DATA {
    TOUCH_CONTACT Contacts[TOUCH_MAX_CONTACTS];
    UCHAR         Count;
} TOUCH_DATA;

/* Informe HID. Debe coincidir byte a byte con hid_desc.h. */
#pragma pack(push, 1)
typedef struct _HID_TOUCH_CONTACT {
    UCHAR  Status;       /* bit0 Tip Switch, bit1 Confidence */
    UCHAR  ContactId;
    USHORT X;
    USHORT Y;
} HID_TOUCH_CONTACT;

typedef struct _HID_TOUCH_REPORT {
    UCHAR             ReportId;      /* REPORTID_TOUCH */
    HID_TOUCH_CONTACT Contacts[TOUCH_MAX_CONTACTS];
    USHORT            ScanTime;      /* unidades de 100 µs */
    UCHAR             ContactCount;
} HID_TOUCH_REPORT;
#pragma pack(pop)

BOOLEAN  ReportChecksumOk(const UCHAR *Buf, ULONG Len);
NTSTATUS ReportParse(const UCHAR *Buf, TOUCH_DATA *Out);
VOID     ReportBuildHid(PDEVICE_CONTEXT Ctx, const TOUCH_DATA *In, HID_TOUCH_REPORT *Out);
