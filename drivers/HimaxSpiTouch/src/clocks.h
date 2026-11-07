#pragma once
#include "device.h"

/*
 * clocks — relojes del QUPv3 wrapper 0 / SE0 en el GCC de SM6115.
 * Datos: linux drivers/clk/qcom/gcc-sm6115.c y clk-rcg2.c.
 *
 * Todos los relojes del QUP son "votados": se encienden poniendo un bit en
 * GCC_APCS_CLOCK_BRANCH_ENA_VOTE (0x7900C) y se comprueba en su CBCR que
 * CLK_OFF (bit 31) pase a 0.
 *
 * El RCG del SE0 usa DFS (tabla de 8 niveles programada por el bootloader):
 * el SE elige el nivel con SE_GENI_CLK_SEL. Si DFS no está activo, se programa
 * el RCG a mano a 19,2 MHz (TCXO, divisor 1).
 */
#define GCC_PHYS_BASE   0x01400000
#define GCC_LENGTH      0x001F0000

#define GCC_APCS_CLOCK_BRANCH_ENA_VOTE  0x7900C
#define   VOTE_QUPV3_WRAP_0_M_AHB       (1u << 6)
#define   VOTE_QUPV3_WRAP_0_S_AHB       (1u << 7)
#define   VOTE_QUPV3_WRAP0_CORE         (1u << 8)
#define   VOTE_QUPV3_WRAP0_CORE_2X      (1u << 9)
#define   VOTE_QUPV3_WRAP0_S0           (1u << 10)

#define GCC_QUPV3_WRAP_0_M_AHB_CBCR     0x1F004
#define GCC_QUPV3_WRAP_0_S_AHB_CBCR     0x1F008
#define GCC_QUPV3_WRAP0_CORE_CBCR       0x1F00C
#define GCC_QUPV3_WRAP0_CORE_2X_CBCR    0x1F014
#define GCC_QUPV3_WRAP0_S0_CBCR         0x1F144
#define   CBCR_CLK_OFF                  (1u << 31)

#define GCC_QUPV3_WRAP0_S0_CMD_RCGR     0x1F148
#define   RCG_CMD_UPDATE                (1u << 0)
#define   RCG_CMD_ROOT_OFF              (1u << 31)
#define   RCG_CFG                       0x04       /* SRC_DIV [4:0], SRC_SEL [10:8], MODE [13:12] */
#define   RCG_DFSR                      0x14       /* bit0 = DFS activo */
#define   RCG_PERF_DFSR(l)              (0x1C + 4 * (l))
#define   RCG_PERF_M_DFSR(l)            (0x5C + 4 * (l))
#define   RCG_PERF_N_DFSR(l)            (0x9C + 4 * (l))
#define   RCG_MAX_PERF_LEVEL            8

#define TCXO_HZ                         19200000u

/* Enciende los relojes. Devuelve en *SeClkHz la frecuencia del SE y en
 * *ClkSel el valor que hay que escribir en SE_GENI_CLK_SEL. */
NTSTATUS ClocksEnableQup0Se0(PDEVICE_CONTEXT Ctx, PULONG SeClkHz, PULONG ClkSel);
