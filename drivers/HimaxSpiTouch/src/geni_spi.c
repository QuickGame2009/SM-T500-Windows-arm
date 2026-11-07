#include "geni_spi.h"
#include "util.h"

/*
 * Controlador SPI QUPv3 GENI (SE0) en modo FIFO, por sondeo. Versión defensiva:
 *  - Empaquetado de 1 byte por palabra de FIFO (sin manejo de bits; menos errores).
 *  - Todos los bucles de espera están acotados por tiempo.
 *  - Si una orden no termina, se aborta antes de devolver el error.
 *  - Medio dúplex: TX y luego RX, que es justo lo que pide el Himax
 *    (F3 cmd 00, después lectura), con el CS mantenido entre fases.
 * Port de linux drivers/spi/spi-geni-qcom.c y drivers/soc/qcom/qcom-geni-se.c.
 */

#define SE_RD(c, o)     READ_REGISTER_ULONG((volatile ULONG *)((c)->GeniSe0 + (o)))
#define SE_WR(c, o, v)  WRITE_REGISTER_ULONG((volatile ULONG *)((c)->GeniSe0 + (o)), (v))

/* Tope de espera de una orden: ~200 ms en pasos de 1 µs. */
#define GENI_CMD_SPINS   200000u
/* Tope de un lote de FIFO sin progreso. */
#define GENI_FIFO_SPINS  100000u

/* Empaquetado de 1 palabra SPI de 8 bits por palabra de FIFO, MSB primero.
 * (geni_se_config_packing con pack_words = 1.) */
static VOID ConfigPacking1Byte(PDEVICE_CONTEXT Ctx)
{
    /* Un solo vector: inicio bit 7, dirección MSB, longitud 8 bits, bit de parada. */
    ULONG cfg0 = (7u << 5) | (1u << 4) | (7u << 1) | 1u;
    SE_WR(Ctx, SE_GENI_TX_PACKING_CFG0, cfg0);
    SE_WR(Ctx, SE_GENI_TX_PACKING_CFG1, 0);
    SE_WR(Ctx, SE_GENI_RX_PACKING_CFG0, cfg0);
    SE_WR(Ctx, SE_GENI_RX_PACKING_CFG1, 0);
    SE_WR(Ctx, SE_GENI_BYTE_GRAN, 0);   /* 8/16 = 0 */
}

/* Aborta la orden actual y espera (acotado) a que el hardware lo confirme. */
static VOID AbortCmd(PDEVICE_CONTEXT Ctx)
{
    SE_WR(Ctx, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
    for (ULONG i = 0; i < GENI_FIFO_SPINS; i++) {
        ULONG irq = SE_RD(Ctx, SE_GENI_M_IRQ_STATUS);
        if (irq & M_CMD_ABORT_EN) {
            SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, irq);
            return;
        }
        KeStallExecutionProcessor(1);
    }
    HxLog("GENI: el aborto no se ha confirmado");
}

NTSTATUS GeniSpiInit(PDEVICE_CONTEXT Ctx)
{
    ULONG fwRev = SE_RD(Ctx, GENI_FW_REVISION_RO);
    ULONG proto = (fwRev >> 8) & 0xFF;
    HxLog("SE0 FW_REVISION=%08X protocolo=%u (1 = SPI)", fwRev, proto);
    if (proto != 1) {
        HxLog("el SE0 no tiene cargado el firmware SPI; abortando");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Versión del wrapper → sobremuestreo (oversampling). */
    ULONG ver = READ_REGISTER_ULONG((volatile ULONG *)(Ctx->QupWrap + QUPV3_HW_VER_REG));
    ULONG major = (ver >> 28) & 0xF, minor = (ver >> 16) & 0xFFF;
    Ctx->Oversampling = (major == 1 && minor == 0) ? 2 : 1;

    /* Profundidad y anchura de la FIFO. */
    ULONG hwp0 = SE_RD(Ctx, SE_HW_PARAM_0);
    Ctx->TxFifoDepth   = (hwp0 & TX_FIFO_DEPTH_MSK) >> TX_FIFO_DEPTH_SHFT;
    Ctx->FifoWidthBits = (hwp0 & TX_FIFO_WIDTH_MSK) >> TX_FIFO_WIDTH_SHFT;
    if (Ctx->TxFifoDepth == 0 || Ctx->TxFifoDepth > 256) {
        HxLog("profundidad de FIFO sospechosa (%u); uso 16", Ctx->TxFifoDepth);
        Ctx->TxFifoDepth = 16;
    }
    HxLog("GENI ver=%u.%u ovs=%u fifo_depth=%u width=%u",
          major, minor, Ctx->Oversampling, Ctx->TxFifoDepth, Ctx->FifoWidthBits);

    /* --- geni_se_irq_clear --- */
    SE_WR(Ctx, SE_GSI_EVENT_EN, 0);
    SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFF);
    SE_WR(Ctx, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFF);
    SE_WR(Ctx, SE_DMA_TX_IRQ_CLR, 0xFFFFFFFF);
    SE_WR(Ctx, SE_DMA_RX_IRQ_CLR, 0xFFFFFFFF);
    SE_WR(Ctx, SE_IRQ_EN, 0xFFFFFFFF);

    /* --- geni_se_io_init --- */
    SE_WR(Ctx, SE_GENI_CGC_CTRL, SE_RD(Ctx, SE_GENI_CGC_CTRL) | DEFAULT_CGC_EN);
    SE_WR(Ctx, SE_DMA_GENERAL_CFG, SE_RD(Ctx, SE_DMA_GENERAL_CFG) |
          AHB_SEC_SLV_CLK_CGC_ON | DMA_AHB_SLV_CLK_CGC_ON |
          DMA_TX_CLK_CGC_ON | DMA_RX_CLK_CGC_ON);
    SE_WR(Ctx, GENI_OUTPUT_CTRL, DEFAULT_IO_OUTPUT_CTRL_MSK);
    SE_WR(Ctx, GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);

    /* --- geni_se_io_set_mode (FIFO) --- */
    SE_WR(Ctx, SE_IRQ_EN, SE_RD(Ctx, SE_IRQ_EN) |
          IRQ_GENI_M_EN | IRQ_GENI_S_EN | IRQ_DMA_TX_EN | IRQ_DMA_RX_EN);
    SE_WR(Ctx, SE_GENI_DMA_MODE_EN, SE_RD(Ctx, SE_GENI_DMA_MODE_EN) & ~GENI_DMA_MODE_EN_BIT);
    SE_WR(Ctx, SE_GSI_EVENT_EN, 0);

    /* Marcas de agua de RX (profundidad-2, como recomienda el manual). */
    SE_WR(Ctx, SE_GENI_RX_WATERMARK_REG, 0);
    SE_WR(Ctx, SE_GENI_RX_RFR_WATERMARK_REG, Ctx->TxFifoDepth - 2);

    /* Habilitar las IRQ comunes (aunque sondeamos, el estado se lee en M_IRQ_STATUS). */
    SE_WR(Ctx, SE_GENI_M_IRQ_EN, SE_RD(Ctx, SE_GENI_M_IRQ_EN) | M_COMMON_GENI_M_IRQ_EN);

    /* --- parámetros SPI: modo 3 (CPOL=1, CPHA=1), 8 bits, CS0 --- */
    SE_WR(Ctx, SE_SPI_LOOPBACK, 0);
    SE_WR(Ctx, SE_SPI_DEMUX_OUTPUT_INV, 0);
    SE_WR(Ctx, SE_SPI_DEMUX_SEL, 0);                     /* CS0 */
    SE_WR(Ctx, SE_SPI_CPHA, HIMAX_SPI_CPHA ? SPI_CPHA_BIT : 0);
    SE_WR(Ctx, SE_SPI_CPOL, HIMAX_SPI_CPOL ? SPI_CPOL_BIT : 0);
    SE_WR(Ctx, SE_SPI_WORD_LEN, SPI_WORD_LEN_8BIT);
    SE_WR(Ctx, SE_SPI_TRANS_CFG, SE_RD(Ctx, SE_SPI_TRANS_CFG) & ~SPI_CS_TOGGLE);
    ConfigPacking1Byte(Ctx);

    /* --- reloj: ~10 MHz máximo --- */
    ULONG div = (Ctx->SeClkHz + (Ctx->Oversampling * HIMAX_SPI_HZ) - 1) /
                (Ctx->Oversampling * HIMAX_SPI_HZ);   /* DIV_ROUND_UP */
    if (div == 0) div = 1;
    SE_WR(Ctx, SE_GENI_CLK_SEL, Ctx->ClkSel);
    SE_WR(Ctx, GENI_SER_M_CLK_CFG, (div << CLK_DIV_SHFT) | SER_CLK_EN);
    HxLog("SPI: modo 3, div=%u, SE=%u Hz → ~%u Hz", div, Ctx->SeClkHz,
          Ctx->SeClkHz / (Ctx->Oversampling * div));

    return STATUS_SUCCESS;
}

/* Rellena la FIFO de TX con hasta (profundidad) bytes sueltos. Devuelve bytes escritos. */
static ULONG FillTxFifo(PDEVICE_CONTEXT Ctx, const UCHAR *Buf, ULONG Remaining)
{
    /* 1 byte por palabra de FIFO; se deja un hueco para no desbordar. */
    ULONG space = (Ctx->TxFifoDepth > 1) ? (Ctx->TxFifoDepth - 1) : 1;
    ULONG n = min(space, Remaining);
    for (ULONG i = 0; i < n; i++)
        SE_WR(Ctx, SE_GENI_TX_FIFOn, Buf[i]);
    return n;
}

/* Vacía lo que haya en la FIFO de RX hacia Buf (tope Remaining). Devuelve bytes leídos. */
static ULONG DrainRxFifo(PDEVICE_CONTEXT Ctx, UCHAR *Buf, ULONG Remaining)
{
    ULONG wc = SE_RD(Ctx, SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK;
    ULONG n = min(wc, Remaining);
    for (ULONG i = 0; i < n; i++)
        Buf[i] = (UCHAR)(SE_RD(Ctx, SE_GENI_RX_FIFOn) & 0xFF);
    /* Descarta palabras sobrantes (no debería haber con medio dúplex). */
    for (ULONG i = n; i < wc; i++)
        (void)SE_RD(Ctx, SE_GENI_RX_FIFOn);
    return n;
}

/* Fase de transmisión de TxLen bytes. KeepCs = mantener CS al terminar. */
static NTSTATUS DoTx(PDEVICE_CONTEXT Ctx, const UCHAR *Tx, ULONG TxLen, BOOLEAN KeepCs)
{
    ULONG rem = TxLen, off = 0;

    SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, SE_RD(Ctx, SE_GENI_M_IRQ_STATUS));
    SE_WR(Ctx, SE_SPI_TX_TRANS_LEN, TxLen);
    SE_WR(Ctx, SE_GENI_M_CMD0,
          (SPI_TX_ONLY << M_OPCODE_SHFT) | (KeepCs ? SPI_FRAGMENTATION : 0));

    /* Primer llenado y arranque de la marca de agua. */
    off += FillTxFifo(Ctx, Tx + off, rem);
    rem = TxLen - off;
    SE_WR(Ctx, SE_GENI_TX_WATERMARK_REG, 1);

    for (ULONG spin = 0; spin < GENI_CMD_SPINS; spin++) {
        ULONG irq = SE_RD(Ctx, SE_GENI_M_IRQ_STATUS);

        if (irq & M_ERROR_MASK) {
            HxLog("TX: error de GENI (M_IRQ=%08X)", irq);
            SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, irq);
            AbortCmd(Ctx);
            return STATUS_IO_DEVICE_ERROR;
        }
        if ((irq & M_TX_FIFO_WATERMARK_EN) && rem) {
            ULONG wrote = FillTxFifo(Ctx, Tx + off, rem);
            off += wrote; rem -= wrote;
            if (rem == 0) SE_WR(Ctx, SE_GENI_TX_WATERMARK_REG, 0);
        }
        SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, irq);
        if (irq & M_CMD_DONE_EN) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1);
    }

    HxLog("TX: tiempo agotado (rem=%u)", rem);
    SE_WR(Ctx, SE_GENI_TX_WATERMARK_REG, 0);
    AbortCmd(Ctx);
    return STATUS_IO_TIMEOUT;
}

/* Fase de recepción de RxLen bytes (suelta el CS al terminar). */
static NTSTATUS DoRx(PDEVICE_CONTEXT Ctx, UCHAR *Rx, ULONG RxLen)
{
    ULONG rem = RxLen, off = 0;

    SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, SE_RD(Ctx, SE_GENI_M_IRQ_STATUS));
    SE_WR(Ctx, SE_SPI_RX_TRANS_LEN, RxLen);
    SE_WR(Ctx, SE_GENI_RX_WATERMARK_REG, 1);
    SE_WR(Ctx, SE_GENI_M_CMD0, (SPI_RX_ONLY << M_OPCODE_SHFT));

    for (ULONG spin = 0; spin < GENI_CMD_SPINS; spin++) {
        ULONG irq = SE_RD(Ctx, SE_GENI_M_IRQ_STATUS);

        if (irq & M_ERROR_MASK) {
            HxLog("RX: error de GENI (M_IRQ=%08X)", irq);
            SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, irq);
            AbortCmd(Ctx);
            return STATUS_IO_DEVICE_ERROR;
        }
        if (irq & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN)) {
            ULONG got = DrainRxFifo(Ctx, Rx + off, rem);
            off += got; rem -= got;
        }
        SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, irq);
        if (irq & M_CMD_DONE_EN) {
            /* Apura lo que quede en la FIFO. */
            if (rem) { ULONG got = DrainRxFifo(Ctx, Rx + off, rem); off += got; rem -= got; }
            SE_WR(Ctx, SE_GENI_RX_WATERMARK_REG, 0);
            if (rem) { HxLog("RX: faltan %u bytes", rem); return STATUS_IO_DEVICE_ERROR; }
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1);
    }

    HxLog("RX: tiempo agotado (rem=%u)", rem);
    SE_WR(Ctx, SE_GENI_RX_WATERMARK_REG, 0);
    AbortCmd(Ctx);
    return STATUS_IO_TIMEOUT;
}

NTSTATUS GeniSpiTransfer(PDEVICE_CONTEXT Ctx,
                         const UCHAR *Tx, ULONG TxLen,
                         UCHAR *Rx, ULONG RxLen)
{
    NTSTATUS s;

    if (TxLen == 0 && RxLen == 0) return STATUS_SUCCESS;

    if (TxLen) {
        /* Si hay lectura después, mantener el CS entre fases. */
        s = DoTx(Ctx, Tx, TxLen, RxLen != 0);
        if (!NT_SUCCESS(s)) return s;
    }
    if (RxLen) {
        s = DoRx(Ctx, Rx, RxLen);
        if (!NT_SUCCESS(s)) return s;
    }
    return STATUS_SUCCESS;
}
