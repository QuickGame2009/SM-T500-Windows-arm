#pragma once
#include "device.h"

/*
 * geni_spi — QUPv3 GENI SE0 (0x04A80000) como maestro SPI en modo FIFO, por sondeo.
 * Port de linux drivers/spi/spi-geni-qcom.c y drivers/soc/qcom/qcom-geni-se.c.
 */
#define SE0_PHYS_BASE               0x04A80000
#define SE0_LENGTH                  0x4000
#define QUP0_WRAP_PHYS_BASE         0x04AC0000
#define QUP0_WRAP_LENGTH            0x2000
#define QUPV3_HW_VER_REG            0x4         /* en el wrapper */

/* Registros comunes GENI */
#define GENI_FORCE_DEFAULT_REG      0x020
#define GENI_OUTPUT_CTRL            0x024
#define SE_GENI_CGC_CTRL            0x028
#define SE_GENI_STATUS              0x040
#define GENI_SER_M_CLK_CFG          0x048
#define GENI_IF_DISABLE_RO          0x064
#define GENI_FW_REVISION_RO         0x068
#define SE_GENI_CLK_SEL             0x07C
#define SE_GENI_DMA_MODE_EN         0x258
#define SE_GENI_BYTE_GRAN           0x254
#define SE_GENI_TX_PACKING_CFG0     0x260
#define SE_GENI_TX_PACKING_CFG1     0x264
#define SE_GENI_RX_PACKING_CFG0     0x284
#define SE_GENI_RX_PACKING_CFG1     0x288
#define SE_GENI_M_CMD0              0x600
#define SE_GENI_M_CMD_CTRL_REG      0x604
#define SE_GENI_M_IRQ_STATUS        0x610
#define SE_GENI_M_IRQ_EN            0x614
#define SE_GENI_M_IRQ_CLEAR         0x618
#define SE_GENI_S_IRQ_CLEAR         0x648
#define SE_GENI_TX_FIFOn            0x700
#define SE_GENI_RX_FIFOn            0x780
#define SE_GENI_TX_FIFO_STATUS      0x800
#define SE_GENI_RX_FIFO_STATUS      0x804
#define SE_GENI_TX_WATERMARK_REG    0x80C
#define SE_GENI_RX_WATERMARK_REG    0x810
#define SE_GENI_RX_RFR_WATERMARK_REG 0x814
#define SE_DMA_TX_IRQ_CLR           0xC44
#define SE_DMA_RX_IRQ_CLR           0xD44
#define SE_GSI_EVENT_EN             0xE18
#define SE_IRQ_EN                   0xE1C
#define SE_HW_PARAM_0               0xE24
#define SE_DMA_GENERAL_CFG          0xE30

/* Registros SPI */
#define SE_SPI_CPHA                 0x224
#define   SPI_CPHA_BIT              (1u << 0)
#define SE_SPI_LOOPBACK             0x22C
#define SE_SPI_CPOL                 0x230
#define   SPI_CPOL_BIT              (1u << 2)
#define SE_SPI_DEMUX_OUTPUT_INV     0x24C
#define SE_SPI_DEMUX_SEL            0x250
#define SE_SPI_TRANS_CFG            0x25C
#define   SPI_CS_TOGGLE             (1u << 1)
#define SE_SPI_WORD_LEN             0x268
#define SE_SPI_TX_TRANS_LEN         0x26C
#define SE_SPI_RX_TRANS_LEN         0x270

/* Órdenes */
#define M_OPCODE_SHFT               27
#define SPI_TX_ONLY                 1
#define SPI_RX_ONLY                 2
#define SPI_FRAGMENTATION           (1u << 2)   /* mantener CS al acabar */
#define M_GENI_CMD_ABORT            (1u << 1)
#define M_GENI_CMD_CANCEL           (1u << 2)

/* Bits de M_IRQ */
#define M_CMD_DONE_EN               (1u << 0)
#define M_CMD_OVERRUN_EN            (1u << 1)
#define M_ILLEGAL_CMD_EN            (1u << 2)
#define M_CMD_FAILURE_EN            (1u << 3)
#define M_CMD_CANCEL_EN             (1u << 4)
#define M_CMD_ABORT_EN              (1u << 5)
#define M_IO_DATA_DEASSERT_EN       (1u << 22)
#define M_IO_DATA_ASSERT_EN         (1u << 23)
#define M_RX_FIFO_RD_ERR_EN         (1u << 24)
#define M_RX_FIFO_WR_ERR_EN         (1u << 25)
#define M_RX_FIFO_WATERMARK_EN      (1u << 26)
#define M_RX_FIFO_LAST_EN           (1u << 27)
#define M_TX_FIFO_RD_ERR_EN         (1u << 28)
#define M_TX_FIFO_WR_ERR_EN         (1u << 29)
#define M_TX_FIFO_WATERMARK_EN      (1u << 30)
#define M_COMMON_GENI_M_IRQ_EN      (0x7Eu | M_IO_DATA_DEASSERT_EN | M_IO_DATA_ASSERT_EN | \
                                     M_RX_FIFO_RD_ERR_EN | M_RX_FIFO_WR_ERR_EN | \
                                     M_TX_FIFO_RD_ERR_EN | M_TX_FIFO_WR_ERR_EN)
#define M_ERROR_MASK                (M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN | \
                                     M_RX_FIFO_RD_ERR_EN | M_RX_FIFO_WR_ERR_EN | \
                                     M_TX_FIFO_RD_ERR_EN | M_TX_FIFO_WR_ERR_EN)

#define TX_FIFO_WC_MSK              0x0FFFFFFFu
#define RX_LAST                     (1u << 31)
#define RX_LAST_BYTE_VALID_MSK      (7u << 28)
#define RX_LAST_BYTE_VALID_SHFT     28
#define RX_FIFO_WC_MSK              0x01FFFFFFu

/* Bits extra para la inicialización (qcom-geni-se.c) */
#define SE_GENI_S_IRQ_CLEAR         0x648
#define DEFAULT_CGC_EN              0x7Fu        /* GENMASK(6,0) */
#define DEFAULT_IO_OUTPUT_CTRL_MSK  0x7Fu        /* GENMASK(6,0) */
#define FORCE_DEFAULT               (1u << 0)
#define SER_CLK_EN                  (1u << 0)
#define CLK_DIV_SHFT                4
#define GENI_DMA_MODE_EN_BIT        (1u << 0)
/* SE_IRQ_EN */
#define IRQ_DMA_RX_EN               (1u << 0)
#define IRQ_DMA_TX_EN               (1u << 1)
#define IRQ_GENI_M_EN               (1u << 2)
#define IRQ_GENI_S_EN               (1u << 3)
/* SE_DMA_GENERAL_CFG */
#define DMA_RX_CLK_CGC_ON           (1u << 0)
#define DMA_TX_CLK_CGC_ON           (1u << 1)
#define DMA_AHB_SLV_CLK_CGC_ON      (1u << 2)
#define AHB_SEC_SLV_CLK_CGC_ON      (1u << 3)
/* SE_HW_PARAM_0: profundidad y anchura de la FIFO de TX */
#define TX_FIFO_DEPTH_MSK           0x003F0000u  /* GENMASK(21,16) */
#define TX_FIFO_DEPTH_SHFT          16
#define TX_FIFO_WIDTH_MSK           0x3F000000u  /* GENMASK(29,24) */
#define TX_FIFO_WIDTH_SHFT          24
/* WORD_LEN: valor del registro = bits_por_palabra - 4 (Linux MIN_WORD_LEN) */
#define SPI_WORD_LEN_8BIT           (8 - 4)

/* Modo del driver Android (himax_platform.c: spi->mode = SPI_MODE_3) */
#define HIMAX_SPI_CPOL              1
#define HIMAX_SPI_CPHA              1
#define HIMAX_SPI_HZ                10000000u

NTSTATUS GeniSpiInit(PDEVICE_CONTEXT Ctx);

/* TX de TxLen bytes y, sin soltar CS, RX de RxLen bytes (RxLen puede ser 0).
 * PASSIVE_LEVEL; el llamante serializa con Ctx->SpiLock. */
NTSTATUS GeniSpiTransfer(PDEVICE_CONTEXT Ctx,
                         const UCHAR *Tx, ULONG TxLen,
                         UCHAR *Rx, ULONG RxLen);
