#pragma once
#include <ntddk.h>
#include <vhf.h>

/*
 * Estado del acelerómetro (Bosch BMI260/BMI270 por I2C en un SE QUPv3).
 * Vive dentro de DEVICE_CONTEXT: comparte dispositivo, GCC y TLMM con el táctil.
 */
typedef struct _ACCEL_STATE {
    /* Bus */
    PUCHAR      Se;             /* SE GENI mapeado (0x4000 bytes) */
    ULONG       Bus;            /* nº de SE: 1 (0x04A84000) o 2 (0x04A88000) */
    ULONG       ClkSel;         /* nivel DFS de 19,2 MHz */
    ULONG       FifoDepth;      /* en palabras de 32 bits */
    UCHAR       Addr;           /* dirección I2C (0x68 o 0x69) */
    UCHAR       ChipId;         /* 0x24 = BMI270, 0x27 = BMI260 */

    /* Hilo */
    PETHREAD    Thread;
    ULONG       Stage;          /* Parameters\AccelStage */
    ULONG       AxisMap;        /* Parameters\AccelAxisMap */

    /* HID (sensor) */
    VHFHANDLE   Vhf;
    BOOLEAN     VhfStarted;
    KSPIN_LOCK  Lock;           /* protege lo de abajo */
    UCHAR       ReportingState; /* enumeración HID, base 1 */
    UCHAR       PowerState;
    ULONG       ReportIntervalMs;
    USHORT      Sensitivity;    /* en mg */
    SHORT       Last[3];        /* última muestra enviada, en mg (ejes de Windows) */
    BOOLEAN     HaveSample;
} ACCEL_STATE;
