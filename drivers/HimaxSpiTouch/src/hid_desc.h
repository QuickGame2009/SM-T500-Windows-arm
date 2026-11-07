#pragma once
#include <ntddk.h>

/*
 * hid_desc — descriptor HID de pantalla táctil (modo paralelo, 10 dedos).
 * Basado en la muestra de Microsoft "touchscreen-sample-report-descriptors".
 * El orden de campos coincide con HID_TOUCH_REPORT de report.h:
 *   ReportId(1) + 10 × {Status(1), ContactId(1), X(2), Y(2)} + ScanTime(2) + ContactCount(1)
 *   = 1 + 60 + 2 + 1 = 64 bytes.
 * Logical Max de X/Y = 1200 / 2000 (resolución del panel).
 * (0,0) = esquina superior izquierda de la orientación nativa.
 */
#define REPORTID_TOUCH      0x01
#define REPORTID_MAX_COUNT  0x02

/* Una colección lógica de dedo (Finger): Tip, Confidence, Contact Id, X, Y. */
#define HX_HID_FINGER_BLOCK \
    0x05, 0x0D,        /*   USAGE_PAGE (Digitizer)        */ \
    0x09, 0x22,        /*   USAGE (Finger)                */ \
    0xA1, 0x02,        /*   COLLECTION (Logical)          */ \
    0x09, 0x42,        /*     USAGE (Tip Switch)          */ \
    0x09, 0x47,        /*     USAGE (Confidence)          */ \
    0x15, 0x00,        /*     LOGICAL_MINIMUM (0)         */ \
    0x25, 0x01,        /*     LOGICAL_MAXIMUM (1)         */ \
    0x75, 0x01,        /*     REPORT_SIZE (1)             */ \
    0x95, 0x02,        /*     REPORT_COUNT (2)            */ \
    0x81, 0x02,        /*     INPUT (Data,Var,Abs)        */ \
    0x95, 0x06,        /*     REPORT_COUNT (6) relleno    */ \
    0x81, 0x03,        /*     INPUT (Cnst,Var,Abs)        */ \
    0x75, 0x08,        /*     REPORT_SIZE (8)             */ \
    0x09, 0x51,        /*     USAGE (Contact Identifier)  */ \
    0x95, 0x01,        /*     REPORT_COUNT (1)            */ \
    0x25, 0x1F,        /*     LOGICAL_MAXIMUM (31)        */ \
    0x81, 0x02,        /*     INPUT (Data,Var,Abs)        */ \
    0x05, 0x01,        /*     USAGE_PAGE (Generic Desktop)*/ \
    0x75, 0x10,        /*     REPORT_SIZE (16)            */ \
    0x55, 0x0E,        /*     UNIT_EXPONENT (-2)          */ \
    0x65, 0x11,        /*     UNIT (cm)                   */ \
    0x09, 0x30,        /*     USAGE (X)                   */ \
    0x26, 0xB0, 0x04,  /*     LOGICAL_MAXIMUM (1200)      */ \
    0x46, 0xB0, 0x04,  /*     PHYSICAL_MAXIMUM (1200)     */ \
    0x81, 0x02,        /*     INPUT (Data,Var,Abs)        */ \
    0x09, 0x31,        /*     USAGE (Y)                   */ \
    0x26, 0xD0, 0x07,  /*     LOGICAL_MAXIMUM (2000)      */ \
    0x46, 0xD0, 0x07,  /*     PHYSICAL_MAXIMUM (2000)     */ \
    0x81, 0x02,        /*     INPUT (Data,Var,Abs)        */ \
    0xC0               /*   END_COLLECTION                */

static const UCHAR g_TouchReportDescriptor[] = {
    0x05, 0x0D,        /* USAGE_PAGE (Digitizer)          */
    0x09, 0x04,        /* USAGE (Touch Screen)            */
    0xA1, 0x01,        /* COLLECTION (Application)        */
    0x85, REPORTID_TOUCH, /* REPORT_ID (1)                */

    /* 10 colecciones de dedo */
    HX_HID_FINGER_BLOCK, HX_HID_FINGER_BLOCK, HX_HID_FINGER_BLOCK,
    HX_HID_FINGER_BLOCK, HX_HID_FINGER_BLOCK, HX_HID_FINGER_BLOCK,
    HX_HID_FINGER_BLOCK, HX_HID_FINGER_BLOCK, HX_HID_FINGER_BLOCK,
    HX_HID_FINGER_BLOCK,

    /* Scan Time (a nivel de informe) */
    0x05, 0x0D,        /*   USAGE_PAGE (Digitizer)        */
    0x55, 0x0C,        /*   UNIT_EXPONENT (-4)            */
    0x66, 0x01, 0x10,  /*   UNIT (seconds)                */
    0x47, 0xFF, 0xFF, 0x00, 0x00, /* PHYSICAL_MAXIMUM (65535) */
    0x27, 0xFF, 0xFF, 0x00, 0x00, /* LOGICAL_MAXIMUM (65535)  */
    0x75, 0x10,        /*   REPORT_SIZE (16)              */
    0x95, 0x01,        /*   REPORT_COUNT (1)              */
    0x09, 0x56,        /*   USAGE (Scan Time)             */
    0x81, 0x02,        /*   INPUT (Data,Var,Abs)          */

    /* Contact Count */
    0x09, 0x54,        /*   USAGE (Contact Count)         */
    0x25, 0x7F,        /*   LOGICAL_MAXIMUM (127)         */
    0x75, 0x08,        /*   REPORT_SIZE (8)               */
    0x81, 0x02,        /*   INPUT (Data,Var,Abs)          */

    /* Contact Count Maximum (feature) */
    0x85, REPORTID_MAX_COUNT, /* REPORT_ID (2)            */
    0x09, 0x55,        /*   USAGE (Contact Count Maximum) */
    0x25, 0x0A,        /*   LOGICAL_MAXIMUM (10)          */
    0x75, 0x08,        /*   REPORT_SIZE (8)               */
    0x95, 0x01,        /*   REPORT_COUNT (1)              */
    0xB1, 0x02,        /*   FEATURE (Data,Var,Abs)        */

    0xC0               /* END_COLLECTION                  */
};

/* Respuesta al GET_FEATURE de Contact Count Maximum. */
#pragma pack(push, 1)
typedef struct _HID_MAX_COUNT_REPORT {
    UCHAR ReportId;        /* REPORTID_MAX_COUNT */
    UCHAR ContactCountMax; /* 10 */
} HID_MAX_COUNT_REPORT;
#pragma pack(pop)
