/*
 * accel_bus.c — relojes (GCC), pines (TLMM) e I2C (GENI, FIFO por sondeo) del SE
 * al que está conectado el acelerómetro.
 *
 * Fuentes: kernel de la tablet (LineageOS android_kernel_samsung_sm6115):
 *   drivers/clk/qcom/gcc-bengal.c, drivers/pinctrl/qcom/pinctrl-bengal.c,
 *   drivers/i2c/busses/i2c-qcom-geni.c, drivers/platform/msm/qcom-geni-se.c.
 */
#include "accel.h"
#include "geni_spi.h"     /* registros GENI comunes */
#include "tlmm.h"
#include "util.h"

/* ---------- GCC ---------- */
#define GCC_VOTE                 0x7900C
#define GCC_VOTE_COMMON          ((1u << 6) | (1u << 7) | (1u << 8) | (1u << 9))  /* m_ahb, s_ahb, core, core_2x */
#define GCC_VOTE_SE(n)           (1u << (10 + (n)))
#define GCC_M_AHB_CBCR           0x1F004
#define GCC_S_AHB_CBCR           0x1F008
#define GCC_CORE_CBCR            0x1F00C
#define GCC_CORE_2X_CBCR         0x1F014
#define GCC_SE_CBCR(n)           (0x1F144 + 0x130 * (n))   /* s0 1F144, s1 1F274, s2 1F3A4 */
#define GCC_SE_RCGR(n)           (0x1F148 + 0x130 * (n))   /* s0 1F148, s1 1F278, s2 1F3A8 */
#define GCC_CLK_OFF              (1u << 31)
#define GCC_RCG_UPDATE           (1u << 0)
#define GCC_RCG_CFG              0x04
#define GCC_RCG_DFSR             0x14
#define GCC_RCG_PERF_DFSR(l)     (0x1C + 4 * (l))

static ULONG GccRd(PDEVICE_CONTEXT c, ULONG o) { return READ_REGISTER_ULONG((volatile ULONG *)(c->Gcc + o)); }
static VOID  GccWr(PDEVICE_CONTEXT c, ULONG o, ULONG v) { WRITE_REGISTER_ULONG((volatile ULONG *)(c->Gcc + o), v); }

static NTSTATUS GccWaitOn(PDEVICE_CONTEXT c, ULONG Cbcr, const char *Name)
{
    for (ULONG i = 0; i < 400; i++) {
        if ((GccRd(c, Cbcr) & GCC_CLK_OFF) == 0) return STATUS_SUCCESS;
        KeStallExecutionProcessor(5);
    }
    HxLog("ACC: reloj %s sigue apagado (CBCR=%08X)", Name, GccRd(c, Cbcr));
    return STATUS_IO_TIMEOUT;
}

NTSTATUS AccelBusClocks(PDEVICE_CONTEXT c)
{
    ULONG n = c->Accel.Bus;
    ULONG rcgr = GCC_SE_RCGR(n);
    NTSTATUS s;

    ULONG vote = GccRd(c, GCC_VOTE);
    HxLog("ACC: GCC voto=%08X CBCR_S%u=%08X RCGR=%08X CFG=%08X DFSR=%08X", vote, n,
          GccRd(c, GCC_SE_CBCR(n)), GccRd(c, rcgr), GccRd(c, rcgr + GCC_RCG_CFG),
          GccRd(c, rcgr + GCC_RCG_DFSR));

    /* Ramas comunes del wrapper (el táctil ya las tiene votadas; votar otra vez no cambia nada). */
    GccWr(c, GCC_VOTE, vote | GCC_VOTE_COMMON);
    if (!NT_SUCCESS(s = GccWaitOn(c, GCC_M_AHB_CBCR, "m_ahb")))     return s;
    if (!NT_SUCCESS(s = GccWaitOn(c, GCC_S_AHB_CBCR, "s_ahb")))     return s;
    if (!NT_SUCCESS(s = GccWaitOn(c, GCC_CORE_CBCR, "core")))       return s;
    if (!NT_SUCCESS(s = GccWaitOn(c, GCC_CORE_2X_CBCR, "core_2x"))) return s;

    /* Frecuencia del SE: 19,2 MHz (TCXO), que es lo que supone la tabla de tiempos I2C. */
    ULONG dfsr = GccRd(c, rcgr + GCC_RCG_DFSR);
    c->Accel.ClkSel = 0;
    if (dfsr & 1) {
        BOOLEAN found = FALSE;
        for (ULONG l = 0; l < 8; l++) {
            ULONG cfg = GccRd(c, rcgr + GCC_RCG_PERF_DFSR(l));
            ULONG src = (cfg >> 8) & 7, div = cfg & 0x1F, mode = (cfg >> 12) & 3;
            if (src == 0 && div <= 1 && mode == 0) {
                HxLog("ACC: DFS nivel %u = TCXO 19,2 MHz (CFG=%08X)", l, cfg);
                c->Accel.ClkSel = l; found = TRUE;
                break;
            }
        }
        if (!found) {
            HxLog("ACC: DFS activo pero sin nivel de 19,2 MHz");
            return STATUS_NOT_FOUND;
        }
    } else {
        HxLog("ACC: sin DFS; se programa el RCG a TCXO/1");
        GccWr(c, rcgr + GCC_RCG_CFG, 1u);   /* src 0 (TCXO), div 1, sin MND */
        GccWr(c, rcgr, GccRd(c, rcgr) | GCC_RCG_UPDATE);
        BOOLEAN done = FALSE;
        for (ULONG i = 0; i < 1000; i++) {
            if ((GccRd(c, rcgr) & GCC_RCG_UPDATE) == 0) { done = TRUE; break; }
            KeStallExecutionProcessor(2);
        }
        if (!done) { HxLog("ACC: el RCG del SE%u no se actualiza", n); return STATUS_IO_TIMEOUT; }
    }

    GccWr(c, GCC_VOTE, GccRd(c, GCC_VOTE) | GCC_VOTE_SE(n));
    if (!NT_SUCCESS(s = GccWaitOn(c, GCC_SE_CBCR(n), "wrap0_sN"))) return s;
    HxLog("ACC: relojes del SE%u encendidos (voto=%08X, CLK_SEL=%u)", n, GccRd(c, GCC_VOTE), c->Accel.ClkSel);
    return STATUS_SUCCESS;
}

/* ---------- GENI ---------- */
#define SE_RD(c, o)     READ_REGISTER_ULONG((volatile ULONG *)((c)->Accel.Se + (o)))
#define SE_WR(c, o, v)  WRITE_REGISTER_ULONG((volatile ULONG *)((c)->Accel.Se + (o)), (v))

#define SE_I2C_TX_TRANS_LEN   0x26C
#define SE_I2C_RX_TRANS_LEN   0x270
#define SE_I2C_SCL_COUNTERS   0x278
#define SE_GENI_IOS           0x908        /* bit0 y bit1: niveles de SDA y SCL */
#define I2C_OP_WRITE          1u
#define I2C_OP_READ           2u
#define I2C_STOP_STRETCH      (1u << 2)    /* no enviar STOP: lo siguiente es un inicio repetido */
#define I2C_SLV_ADDR_SHFT     9
#define GENI_PROTO_I2C        3
#define M_GP_IRQ_0            (1u << 9)
#define M_GP_IRQ_1            (1u << 10)   /* NACK */
#define M_GP_IRQ_3            (1u << 12)   /* error de protocolo */
#define M_GP_IRQ_4            (1u << 13)   /* arbitraje perdido */
#define I2C_ERR_MASK          (M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN | \
                               M_GP_IRQ_0 | M_GP_IRQ_1 | M_GP_IRQ_3 | M_GP_IRQ_4)
#define I2C_SPINS             100000u      /* x 2 us = 200 ms por orden */

NTSTATUS AccelBusIdent(PDEVICE_CONTEXT c)
{
    HxLog("ACC: ANTES de leer el SE%u (%llX)", c->Accel.Bus, ACCEL_SE_PHYS(c->Accel.Bus));
    ULONG fw = SE_RD(c, GENI_FW_REVISION_RO);
    ULONG proto = (fw >> 8) & 0xFF;
    ULONG hwp0 = SE_RD(c, SE_HW_PARAM_0);
    ULONG depth = (hwp0 & TX_FIFO_DEPTH_MSK) >> TX_FIFO_DEPTH_SHFT;
    ULONG width = (hwp0 & TX_FIFO_WIDTH_MSK) >> TX_FIFO_WIDTH_SHFT;
    HxLog("ACC: SE%u FW_REVISION=%08X protocolo=%u (1 SPI, 2 UART, 3 I2C) fifo=%ux%u STATUS=%08X IOS=%08X",
          c->Accel.Bus, fw, proto, depth, width, SE_RD(c, SE_GENI_STATUS), SE_RD(c, SE_GENI_IOS));
    if (proto != GENI_PROTO_I2C) {
        HxLog("ACC: el SE%u no tiene cargado el firmware I2C", c->Accel.Bus);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (width != 32 || depth < 4 || depth > 64) {
        HxLog("ACC: FIFO no válida");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    c->Accel.FifoDepth = depth;
    return STATUS_SUCCESS;
}

/* Pines: SE1 = GPIO4 (SDA) y GPIO5 (SCL); SE2 = GPIO6 y GPIO7. Todos en el bloque oeste
 * del TLMM (0x00500000 + 0x1000 * n), función 1 = qupN, 2 mA, sin pull (bengal-pinctrl.dtsi). */
NTSTATUS AccelBusPins(PDEVICE_CONTEXT c, BOOLEAN PullUp)
{
    ULONG first = (c->Accel.Bus == 1) ? 4 : 6;
    for (ULONG g = first; g < first + 2; g++) {
        ULONG ctl = TlmmReadRaw(c, g, 0), io = TlmmReadRaw(c, g, 4);
        HxLog("ACC: GPIO%u antes: CTL=%08X (func=%u pull=%u oe=%u) IO=%08X", g, ctl,
              (ctl & TLMM_CTL_FUNC_MASK) >> TLMM_CTL_FUNC_SHIFT, ctl & TLMM_CTL_PULL_MASK,
              (ctl & TLMM_CTL_OE) ? 1 : 0, io);
        ULONG want = (ctl & ~(TLMM_CTL_FUNC_MASK | TLMM_CTL_OE | TLMM_CTL_PULL_MASK | TLMM_CTL_DRV_MASK))
                     | (1u << TLMM_CTL_FUNC_SHIFT) | (PullUp ? TLMM_CTL_PULL_UP : 0);
        if (want == ctl) {
            HxLog("ACC: GPIO%u ya está en función qup%u; no se toca", g, c->Accel.Bus);
            continue;
        }
        HxLog("ACC: ANTES de escribir CTL de GPIO%u = %08X", g, want);
        WRITE_REGISTER_ULONG((volatile ULONG *)(c->Tlmm + TLMM_CTL(g)), want);
        HxLog("ACC: GPIO%u ahora CTL=%08X", g, TlmmReadRaw(c, g, 0));
    }
    return STATUS_SUCCESS;
}

NTSTATUS AccelBusInit(PDEVICE_CONTEXT c, ULONG Khz)
{
    ULONG depth = c->Accel.FifoDepth;

    HxLog("ACC: ANTES de configurar el SE%u como I2C", c->Accel.Bus);
    /* geni_se_irq_clear */
    SE_WR(c, SE_GSI_EVENT_EN, 0);
    SE_WR(c, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFF);
    SE_WR(c, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFF);
    SE_WR(c, SE_DMA_TX_IRQ_CLR, 0xFFFFFFFF);
    SE_WR(c, SE_DMA_RX_IRQ_CLR, 0xFFFFFFFF);
    SE_WR(c, SE_IRQ_EN, 0xFFFFFFFF);
    /* geni_se_io_init */
    SE_WR(c, SE_GENI_CGC_CTRL, SE_RD(c, SE_GENI_CGC_CTRL) | DEFAULT_CGC_EN);
    SE_WR(c, SE_DMA_GENERAL_CFG, SE_RD(c, SE_DMA_GENERAL_CFG) |
          AHB_SEC_SLV_CLK_CGC_ON | DMA_AHB_SLV_CLK_CGC_ON | DMA_TX_CLK_CGC_ON | DMA_RX_CLK_CGC_ON);
    SE_WR(c, GENI_OUTPUT_CTRL, DEFAULT_IO_OUTPUT_CTRL_MSK);
    SE_WR(c, GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
    /* modo FIFO */
    SE_WR(c, SE_IRQ_EN, SE_RD(c, SE_IRQ_EN) | IRQ_GENI_M_EN | IRQ_GENI_S_EN | IRQ_DMA_TX_EN | IRQ_DMA_RX_EN);
    SE_WR(c, SE_GENI_DMA_MODE_EN, SE_RD(c, SE_GENI_DMA_MODE_EN) & ~GENI_DMA_MODE_EN_BIT);
    SE_WR(c, SE_GSI_EVENT_EN, 0);
    SE_WR(c, SE_GENI_RX_WATERMARK_REG, depth - 1);
    SE_WR(c, SE_GENI_RX_RFR_WATERMARK_REG, depth - 2);
    SE_WR(c, SE_GENI_TX_WATERMARK_REG, 0);
    SE_WR(c, SE_GENI_M_IRQ_EN, SE_RD(c, SE_GENI_M_IRQ_EN) | M_COMMON_GENI_M_IRQ_EN |
          M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN | M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);

    /* Empaquetado: 4 bytes de 8 bits por palabra, el byte 0 en los bits 7..0
     * (geni_se_config_packing(8, 4, msb_to_lsb = true)). */
    {
        ULONG v[4];
        for (ULONG i = 0; i < 4; i++) v[i] = (((i * 8) + 7) << 5) | (1u << 4) | (7u << 1);
        v[3] |= 1u;
        ULONG cfg0 = v[0] | (v[1] << 10), cfg1 = v[2] | (v[3] << 10);
        SE_WR(c, SE_GENI_TX_PACKING_CFG0, cfg0); SE_WR(c, SE_GENI_TX_PACKING_CFG1, cfg1);
        SE_WR(c, SE_GENI_RX_PACKING_CFG0, cfg0); SE_WR(c, SE_GENI_RX_PACKING_CFG1, cfg1);
    }

    /* Tiempos (geni_i2c_clk_map, reloj de 19,2 MHz): {divisor, t_high, t_low, t_cycle}. */
    ULONG div, th, tl, tc;
    if (Khz == 100) { div = 7; th = 10; tl = 11; tc = 26; }
    else            { div = 2; th = 7;  tl = 10; tc = 24; Khz = 400; }
    SE_WR(c, SE_GENI_CLK_SEL, c->Accel.ClkSel);
    SE_WR(c, GENI_SER_M_CLK_CFG, (div << CLK_DIV_SHFT) | SER_CLK_EN);
    SE_WR(c, SE_I2C_SCL_COUNTERS, (th << 20) | (tl << 10) | tc);
    KeStallExecutionProcessor(200);

    ULONG ios = SE_RD(c, SE_GENI_IOS);
    HxLog("ACC: I2C a %u kHz configurado; IOS=%08X (los 2 bits bajos deben ser 3: SDA y SCL en alto)", Khz, ios);
    if ((ios & 3) != 3) {
        /* Igual que el kernel: "IO lines in bad state, Power the slave". */
        HxLog("ACC: SDA/SCL en bajo: el sensor no tiene alimentación o faltan los pull-up");
        return STATUS_DEVICE_POWERED_OFF;
    }
    return STATUS_SUCCESS;
}

/* Cancela la orden en curso; si no se confirma, la aborta. */
static VOID I2cStop(PDEVICE_CONTEXT c)
{
    SE_WR(c, SE_GENI_TX_WATERMARK_REG, 0);
    SE_WR(c, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL);
    for (ULONG i = 0; i < 5000; i++) {
        ULONG irq = SE_RD(c, SE_GENI_M_IRQ_STATUS);
        if (irq & (M_CMD_CANCEL_EN | M_CMD_DONE_EN)) { SE_WR(c, SE_GENI_M_IRQ_CLEAR, irq); goto drain; }
        KeStallExecutionProcessor(2);
    }
    SE_WR(c, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
    for (ULONG i = 0; i < 5000; i++) {
        ULONG irq = SE_RD(c, SE_GENI_M_IRQ_STATUS);
        if (irq & M_CMD_ABORT_EN) { SE_WR(c, SE_GENI_M_IRQ_CLEAR, irq); break; }
        KeStallExecutionProcessor(2);
    }
drain:
    /* Vaciar lo que quedara en la FIFO de RX. */
    for (ULONG w = SE_RD(c, SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK; w != 0 && w <= 64; w--)
        (VOID)SE_RD(c, SE_GENI_RX_FIFOn);
    SE_WR(c, SE_GENI_M_IRQ_CLEAR, SE_RD(c, SE_GENI_M_IRQ_STATUS));
}

static NTSTATUS I2cError(PDEVICE_CONTEXT c, ULONG Irq)
{
    SE_WR(c, SE_GENI_M_IRQ_CLEAR, Irq);
    I2cStop(c);
    /* NACK = no hay nadie en esa dirección: no es un fallo del bus. */
    return (Irq & M_GP_IRQ_1) ? STATUS_NO_SUCH_DEVICE : STATUS_IO_DEVICE_ERROR;
}

static ULONG I2cFill(PDEVICE_CONTEXT c, const UCHAR *Buf, ULONG Remaining)
{
    ULONG limit = min(Remaining, (c->Accel.FifoDepth - 1) * 4);
    ULONG off = 0;
    while (off < limit) {
        ULONG word = 0, n = min(4u, limit - off);
        for (ULONG j = 0; j < n; j++) word |= ((ULONG)Buf[off + j]) << (j * 8);
        SE_WR(c, SE_GENI_TX_FIFOn, word);
        off += n;
    }
    return off;
}

static NTSTATUS I2cWrite(PDEVICE_CONTEXT c, const UCHAR *Tx, ULONG Len, BOOLEAN NoStop, PULONG IrqOut)
{
    ULONG off, irq = 0;
    SE_WR(c, SE_GENI_M_IRQ_CLEAR, SE_RD(c, SE_GENI_M_IRQ_STATUS));
    SE_WR(c, SE_I2C_TX_TRANS_LEN, Len);
    SE_WR(c, SE_GENI_M_CMD0, (I2C_OP_WRITE << M_OPCODE_SHFT) |
          ((ULONG)c->Accel.Addr << I2C_SLV_ADDR_SHFT) | (NoStop ? I2C_STOP_STRETCH : 0));
    off = I2cFill(c, Tx, Len);
    SE_WR(c, SE_GENI_TX_WATERMARK_REG, (off < Len) ? 1 : 0);

    for (ULONG spin = 0; spin < I2C_SPINS; spin++) {
        irq = SE_RD(c, SE_GENI_M_IRQ_STATUS);
        if (irq & I2C_ERR_MASK) { *IrqOut = irq; return I2cError(c, irq); }
        if ((irq & M_TX_FIFO_WATERMARK_EN) && off < Len) {
            off += I2cFill(c, Tx + off, Len - off);
            if (off >= Len) SE_WR(c, SE_GENI_TX_WATERMARK_REG, 0);
        }
        if (irq) SE_WR(c, SE_GENI_M_IRQ_CLEAR, irq);
        if (irq & M_CMD_DONE_EN) {
            SE_WR(c, SE_GENI_TX_WATERMARK_REG, 0);
            return (off == Len) ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
        }
        KeStallExecutionProcessor(2);
    }
    *IrqOut = irq;
    I2cStop(c);
    return STATUS_IO_TIMEOUT;
}

static ULONG I2cDrain(PDEVICE_CONTEXT c, UCHAR *Rx, ULONG Off, ULONG Len)
{
    ULONG words = SE_RD(c, SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK;
    if (words > 64) words = 64;
    while (words--) {
        ULONG w = SE_RD(c, SE_GENI_RX_FIFOn);
        for (ULONG j = 0; j < 4 && Off < Len; j++) Rx[Off++] = (UCHAR)(w >> (j * 8));
    }
    return Off;
}

static NTSTATUS I2cRead(PDEVICE_CONTEXT c, UCHAR *Rx, ULONG Len, PULONG IrqOut)
{
    ULONG off = 0, irq = 0;
    SE_WR(c, SE_GENI_M_IRQ_CLEAR, SE_RD(c, SE_GENI_M_IRQ_STATUS));
    SE_WR(c, SE_I2C_RX_TRANS_LEN, Len);
    SE_WR(c, SE_GENI_M_CMD0, (I2C_OP_READ << M_OPCODE_SHFT) |
          ((ULONG)c->Accel.Addr << I2C_SLV_ADDR_SHFT));

    for (ULONG spin = 0; spin < I2C_SPINS; spin++) {
        irq = SE_RD(c, SE_GENI_M_IRQ_STATUS);
        if (irq & I2C_ERR_MASK) { *IrqOut = irq; return I2cError(c, irq); }
        off = I2cDrain(c, Rx, off, Len);
        if (irq) SE_WR(c, SE_GENI_M_IRQ_CLEAR, irq);
        if (irq & M_CMD_DONE_EN) {
            off = I2cDrain(c, Rx, off, Len);
            return (off == Len) ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
        }
        KeStallExecutionProcessor(2);
    }
    *IrqOut = irq;
    I2cStop(c);
    return STATUS_IO_TIMEOUT;
}

/* Los primeros errores se anotan con el M_IRQ; después solo se cuentan. */
static ULONG g_I2cErrLogged;
static VOID I2cLogErr(PDEVICE_CONTEXT c, const char *Op, UCHAR Reg, NTSTATUS s, ULONG Irq)
{
    if (g_I2cErrLogged < 12) {
        g_I2cErrLogged++;
        HxLog("ACC: I2C %s dir=%02X reg=%02X -> %08X (M_IRQ=%08X IOS=%08X)", Op, c->Accel.Addr, Reg,
              (ULONG)s, Irq, SE_RD(c, SE_GENI_IOS));
    }
}

NTSTATUS AccelRegWrite(PDEVICE_CONTEXT c, UCHAR Reg, const UCHAR *Data, ULONG Len)
{
    UCHAR tx[61];
    ULONG irq = 0;
    if (Len > sizeof(tx) - 1) return STATUS_INVALID_PARAMETER;
    tx[0] = Reg;
    RtlCopyMemory(tx + 1, Data, Len);
    NTSTATUS s = I2cWrite(c, tx, Len + 1, FALSE, &irq);
    if (!NT_SUCCESS(s)) I2cLogErr(c, "escribir", Reg, s, irq);
    return s;
}

NTSTATUS AccelRegRead(PDEVICE_CONTEXT c, UCHAR Reg, UCHAR *Data, ULONG Len)
{
    ULONG irq = 0;
    NTSTATUS s = I2cWrite(c, &Reg, 1, TRUE, &irq);
    if (NT_SUCCESS(s)) s = I2cRead(c, Data, Len, &irq);
    if (!NT_SUCCESS(s)) I2cLogErr(c, "leer", Reg, s, irq);
    return s;
}
