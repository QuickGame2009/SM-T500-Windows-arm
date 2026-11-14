#pragma once
#include "device.h"

/*
 * tlmm — GPIO del SM6115 por acceso directo (linux pinctrl-sm6115.c, pinctrl-msm.c).
 * Registros de cada GPIO n dentro de su bloque (base + 0x1000 * n):
 *   CTL       +0x0  pull [1:0] (3 = pull-up), función [5:2], fuerza [8:6], OE bit 9
 *   IO        +0x4  bit 0 = entrada, bit 1 = salida
 *   INTR_CFG  +0x8  enable bit 0, polaridad bit 1, detección [3:2], raw status bit 4, destino [7:5]
 *   INTR_STAT +0xC  bit 0 = evento pendiente (se borra escribiendo 0)
 */
#define TLMM_PHYS_BASE   0x00500000
#define TLMM_REGION_LEN  0x00300000

#define TLMM_CTL(n)         (0x1000u * (n))
#define TLMM_IO(n)          (0x1000u * (n) + 0x4)
#define TLMM_INTR_CFG(n)    (0x1000u * (n) + 0x8)
#define TLMM_INTR_STATUS(n) (0x1000u * (n) + 0xC)

#define TLMM_CTL_PULL_MASK      0x3u
#define TLMM_CTL_PULL_UP        0x3u
#define TLMM_CTL_FUNC_SHIFT     2
#define TLMM_CTL_FUNC_MASK      (0xFu << 2)
#define TLMM_CTL_DRV_SHIFT      6
#define TLMM_CTL_DRV_MASK       (0x7u << 6)
#define TLMM_CTL_OE             (1u << 9)

#define TLMM_IO_IN              (1u << 0)
#define TLMM_IO_OUT             (1u << 1)

#define TLMM_INTR_ENABLE        (1u << 0)
#define TLMM_INTR_POL_HIGH      (1u << 1)
#define TLMM_INTR_DET_SHIFT     2
#define TLMM_INTR_DET_MASK      (0x3u << 2)
#define TLMM_INTR_DET_FALLING   (0x2u << 2)
#define TLMM_INTR_RAW_STATUS    (1u << 4)

#define GPIO_TOUCH_IRQ   80   /* pmx_ts_int_active en scuba.dts */
#define GPIO_TOUCH_RST   71   /* pmx_ts_reset_active en scuba.dts */

NTSTATUS TlmmConfigure(PDEVICE_CONTEXT Ctx);
ULONG    TlmmReadRaw(PDEVICE_CONTEXT Ctx, ULONG Gpio, ULONG Off); /* 0=CTL 4=IO 8=INTR_CFG 12=INTR_STATUS */
ULONG    TlmmReadInput(PDEVICE_CONTEXT Ctx, ULONG Gpio);
VOID     TlmmWriteOutput(PDEVICE_CONTEXT Ctx, ULONG Gpio, ULONG Value);

/* TRUE si el TLMM ha registrado un flanco de bajada en el GPIO 80 (y lo borra). */
BOOLEAN  TlmmTouchEdgePending(PDEVICE_CONTEXT Ctx);
