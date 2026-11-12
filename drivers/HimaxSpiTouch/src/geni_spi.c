#include "geni_spi.h"
#include "util.h"

/*
 * QUPv3 GENI SE0 FIFO SPI. El empaquetado sigue spi-geni-qcom.c:
 * cada palabra FIFO transporta FifoWidthBits/8 bytes, en orden little-endian.
 * TX/RX son fases separadas y la fragmentación mantiene CS entre ambas.
 */

#define SE_RD(c, o)     READ_REGISTER_ULONG((volatile ULONG *)((c)->GeniSe0 + (o)))
#define SE_WR(c, o, v)  WRITE_REGISTER_ULONG((volatile ULONG *)((c)->GeniSe0 + (o)), (v))

/* Tope de espera de una orden: ~200 ms en pasos de 1 µs. */
#define GENI_CMD_SPINS   200000u
/* Tope de un lote de FIFO sin progreso. */
#define GENI_FIFO_SPINS  100000u

/* Replica geni_se_config_packing(bpw=8, pack_words=FIFO_width/8, MSB first). */
static NTSTATUS ConfigPacking(PDEVICE_CONTEXT Ctx)
{
    ULONG bytesPerWord = Ctx->FifoWidthBits / 8;
    ULONG vectors[4] = {0, 0, 0, 0};
    ULONG cfg0, cfg1;
    if (bytesPerWord == 0 || bytesPerWord > RTL_NUMBER_OF(vectors))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (ULONG i = 0; i < bytesPerWord; i++) {
        ULONG startBit = (i * 8) + 7;
        vectors[i] = (startBit << 5) | (1u << 4) | (7u << 1);
    }
    vectors[bytesPerWord - 1] |= 1u;
    cfg0 = vectors[0] | (vectors[1] << 10);
    cfg1 = vectors[2] | (vectors[3] << 10);
    SE_WR(Ctx, SE_GENI_TX_PACKING_CFG0, cfg0);
    SE_WR(Ctx, SE_GENI_TX_PACKING_CFG1, cfg1);
    SE_WR(Ctx, SE_GENI_RX_PACKING_CFG0, cfg0);
    SE_WR(Ctx, SE_GENI_RX_PACKING_CFG1, cfg1);
    return STATUS_SUCCESS;
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
    HxLog("ANTES: leer FW_REVISION del SE0");
    ULONG fwRev = SE_RD(Ctx, GENI_FW_REVISION_RO);
    ULONG proto = (fwRev >> 8) & 0xFF;
    HxLog("SE0 FW_REVISION=%08X protocolo=%u (1 = SPI)", fwRev, proto);
    if (proto != 1) {
        HxLog("el SE0 no tiene cargado el firmware SPI; abortando");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Versión del wrapper → sobremuestreo (oversampling). */
    HxLog("ANTES: leer versión del wrapper QUP");
    ULONG ver = READ_REGISTER_ULONG((volatile ULONG *)(Ctx->QupWrap + QUPV3_HW_VER_REG));
    ULONG major = (ver >> 28) & 0xF, minor = (ver >> 16) & 0xFFF;
    Ctx->Oversampling = (major == 1 && minor == 0) ? 2 : 1;

    /* Profundidad y anchura de la FIFO. */
    ULONG hwp0 = SE_RD(Ctx, SE_HW_PARAM_0);
    Ctx->TxFifoDepth   = (hwp0 & TX_FIFO_DEPTH_MSK) >> TX_FIFO_DEPTH_SHFT;
    Ctx->FifoWidthBits = (hwp0 & TX_FIFO_WIDTH_MSK) >> TX_FIFO_WIDTH_SHFT;
    if (Ctx->TxFifoDepth < 2 || Ctx->TxFifoDepth > 256 ||
        Ctx->FifoWidthBits < 8 || Ctx->FifoWidthBits > 32 ||
        (Ctx->FifoWidthBits & 7) != 0) {
        HxLog("parámetros FIFO GENI no válidos: depth=%u width=%u",
              Ctx->TxFifoDepth, Ctx->FifoWidthBits);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    HxLog("GENI ver=%u.%u ovs=%u fifo_depth=%u width=%u",
          major, minor, Ctx->Oversampling, Ctx->TxFifoDepth, Ctx->FifoWidthBits);

    /* --- geni_se_irq_clear --- */
    HxLog("ANTES: escribir registros de IRQ del SE0");
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
    SE_WR(Ctx, SE_GENI_M_IRQ_EN,
          SE_RD(Ctx, SE_GENI_M_IRQ_EN) | M_COMMON_GENI_M_IRQ_EN |
          M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN |
          M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);

    /* --- parámetros SPI: modo 3 (CPOL=1, CPHA=1), 8 bits, CS0 --- */
    SE_WR(Ctx, SE_SPI_LOOPBACK, 0);
    SE_WR(Ctx, SE_SPI_DEMUX_OUTPUT_INV, 0);
    SE_WR(Ctx, SE_SPI_DEMUX_SEL, 0);                     /* CS0 */
    SE_WR(Ctx, SE_SPI_CPHA, HIMAX_SPI_CPHA ? SPI_CPHA_BIT : 0);
    SE_WR(Ctx, SE_SPI_CPOL, HIMAX_SPI_CPOL ? SPI_CPOL_BIT : 0);
    SE_WR(Ctx, SE_SPI_WORD_LEN, SPI_WORD_LEN_8BIT);
    SE_WR(Ctx, SE_SPI_TRANS_CFG, SE_RD(Ctx, SE_SPI_TRANS_CFG) & ~SPI_CS_TOGGLE);
    {
        NTSTATUS packStatus = ConfigPacking(Ctx);
        if (!NT_SUCCESS(packStatus)) return packStatus;
    }

    HxLog("configuración SPI escrita");
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

/* Llena FIFO agrupando bytes en palabras del ancho real del hardware. */
static ULONG FillTxFifo(PDEVICE_CONTEXT Ctx, const UCHAR *Buf, ULONG Remaining)
{
    ULONG bytesPerWord = Ctx->FifoWidthBits / 8;
    ULONG words = (Ctx->TxFifoDepth > 1) ? (Ctx->TxFifoDepth - 1) : 1;
    ULONG limit = min(Remaining, words * bytesPerWord);
    ULONG off = 0;
    while (off < limit) {
        ULONG word = 0;
        ULONG n = min(bytesPerWord, limit - off);
        for (ULONG j = 0; j < n; j++)
            word |= ((ULONG)Buf[off + j]) << (j * 8);
        SE_WR(Ctx, SE_GENI_TX_FIFOn, word);
        off += n;
    }
    return off;
}

/* Vacía FIFO RX, respetando RX_LAST y RX_LAST_BYTE_VALID como el kernel Qualcomm. */
static ULONG DrainRxFifo(PDEVICE_CONTEXT Ctx, UCHAR *Buf, ULONG Remaining)
{
    ULONG status = SE_RD(Ctx, SE_GENI_RX_FIFO_STATUS);
    ULONG words = status & RX_FIFO_WC_MSK;
    ULONG bytesPerWord = Ctx->FifoWidthBits / 8;
    ULONG tailBytes = 0;
    if ((status & RX_LAST) != 0) {
        ULONG valid = (status & RX_LAST_BYTE_VALID_MSK) >> RX_LAST_BYTE_VALID_SHFT;
        if (valid != 0 && valid < bytesPerWord) {
            if (words == 0) return 0;
            words--;
            tailBytes = valid;
        }
    }
    ULONG available = words * bytesPerWord + tailBytes;
    ULONG copied = min(available, Remaining);
    ULONG wordsToRead = (copied + bytesPerWord - 1) / bytesPerWord;
    ULONG off = 0;
    for (ULONG i = 0; i < wordsToRead; i++) {
        ULONG word = SE_RD(Ctx, SE_GENI_RX_FIFOn);
        ULONG n = min(bytesPerWord, copied - off);
        for (ULONG j = 0; j < n; j++)
            Buf[off + j] = (UCHAR)(word >> (j * 8));
        off += n;
    }
    return copied;
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
            SE_WR(Ctx, SE_GENI_TX_WATERMARK_REG, 0);
            AbortCmd(Ctx);
            return STATUS_IO_DEVICE_ERROR;
        }
        if ((irq & M_TX_FIFO_WATERMARK_EN) && rem) {
            ULONG wrote = FillTxFifo(Ctx, Tx + off, rem);
            off += wrote; rem -= wrote;
            if (rem == 0) SE_WR(Ctx, SE_GENI_TX_WATERMARK_REG, 0);
        }
        SE_WR(Ctx, SE_GENI_M_IRQ_CLEAR, irq);
        if (irq & M_CMD_DONE_EN) {
            SE_WR(Ctx, SE_GENI_TX_WATERMARK_REG, 0);
            if (rem != 0) {
                HxLog("TX: CMD_DONE prematuro, faltan %u bytes", rem);
                return STATUS_IO_DEVICE_ERROR;
            }
            return STATUS_SUCCESS;
        }
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
            SE_WR(Ctx, SE_GENI_RX_WATERMARK_REG, 0);
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
