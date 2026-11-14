#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>
#include "accel_types.h"

#define TOUCH_MAX_X        1200   /* FIX_HX_X_RES del driver Android */
#define TOUCH_MAX_Y        2000   /* FIX_HX_Y_RES */
#define TOUCH_MAX_CONTACTS 10     /* FIX_HX_MAX_PT */

/* Bloque máximo que se escribe de una vez (HX_MAX_WRITE_SZ - 4 en Android) */
#define HX_MAX_WRITE_CHUNK (64 * 1024)
/* F2 + cmd + 4 bytes de dirección + datos */
#define HX_TX_BUFFER_SIZE  (HX_MAX_WRITE_CHUNK + 8)

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE   Device;

    /* MMIO (orden de recursos en el SSDT) */
    PUCHAR      GeniSe0;   SIZE_T GeniSe0Len;     /* 0: 0x04A80000 */
    PUCHAR      Tlmm;      SIZE_T TlmmLen;        /* 0x00500000, mínimo 0x80000 */
    PUCHAR      Gcc;       SIZE_T GccLen;         /* 2: 0x01400000 */
    PUCHAR      QupWrap;   SIZE_T QupWrapLen;     /* 3: 0x04AC0000 */

    /* SPI GENI */
    ULONG       SeClkHz;
    ULONG       ClkSel;
    ULONG       TxFifoDepth;      /* en palabras */
    ULONG       FifoWidthBits;
    ULONG       Oversampling;
    KMUTEX      SpiLock;          /* una transacción Himax a la vez */
    PUCHAR      TxBuffer;         /* HX_TX_BUFFER_SIZE, no paginado */

    /* Hilo de trabajo (arranque del táctil + sondeo) */
    PETHREAD    WorkerThread;
    KEVENT      StopEvent;
    KEVENT      InitEvent;
    NTSTATUS    InitStatus;

    /* Firmware */
    PUCHAR      Firmware;
    ULONG       FirmwareSize;

    /* HID */
    VHFHANDLE   VhfHandle;
    BOOLEAN     VhfStarted;

    /* Estado */
    BOOLEAN     ChipDetected;
    volatile LONG Orphaned;   /* el hilo de arranque no terminó a tiempo */
    ULONG       Stage;
    ULONG       ResetGpio;    /* Parameters\ResetGpio (por defecto 31) */        /* Parameters\Stage: hasta qué paso llega el arranque */
    BOOLEAN     FirmwareLoaded;
    BOOLEAN     PrevDown[TOUCH_MAX_CONTACTS];
    USHORT      PrevX[TOUCH_MAX_CONTACTS];
    USHORT      PrevY[TOUCH_MAX_CONTACTS];
    LARGE_INTEGER QpcFreq;

    /* Acelerómetro (módulo accel*.c) */
    ACCEL_STATE Accel;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)
