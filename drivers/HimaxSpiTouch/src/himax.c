#include "himax.h"
#include "geni_spi.h"
#include "tlmm.h"
#include "util.h"

/* himax_bus_write: F2 cmd datos... */
NTSTATUS HimaxBusWrite(PDEVICE_CONTEXT Ctx, UCHAR Cmd, const UCHAR *Data, ULONG Len)
{
    if (Len > HX_TX_BUFFER_SIZE - 2) return STATUS_BUFFER_TOO_SMALL;
    if (Len != 0 && Data == NULL) return STATUS_INVALID_PARAMETER;
    Ctx->TxBuffer[0] = HX_SPI_WRITE;
    Ctx->TxBuffer[1] = Cmd;
    if (Len) RtlCopyMemory(Ctx->TxBuffer + 2, Data, Len);
    return GeniSpiTransfer(Ctx, Ctx->TxBuffer, Len + 2, NULL, 0);
}

/* himax_bus_read: enviar F3 cmd 00, luego recibir Len bytes (mismo CS) */
NTSTATUS HimaxBusRead(PDEVICE_CONTEXT Ctx, UCHAR Cmd, UCHAR *Data, ULONG Len)
{
    if (Len == 0 || Data == NULL) return STATUS_INVALID_PARAMETER;
    UCHAR hdr[3] = { HX_SPI_READ, Cmd, 0x00 };
    return GeniSpiTransfer(Ctx, hdr, sizeof(hdr), Data, Len);
}

/* hx83102_burst_enable */
NTSTATUS HimaxBurstEnable(PDEVICE_CONTEXT Ctx, BOOLEAN AutoAdd4)
{
    UCHAR b = HX_DATA_CONTI;
    NTSTATUS s = HimaxBusWrite(Ctx, HX_CMD_CONTI, &b, 1);
    if (!NT_SUCCESS(s)) return s;
    b = (UCHAR)(HX_DATA_INCR4 | (AutoAdd4 ? 1 : 0));
    return HimaxBusWrite(Ctx, HX_CMD_INCR4, &b, 1);
}

/* himax_mcu_register_read */
NTSTATUS HimaxRegRead(PDEVICE_CONTEXT Ctx, ULONG Addr, UCHAR *Data, ULONG Len)
{
    NTSTATUS s;
    UCHAR a[HX_ADDR_LEN], dir = HX_CMD_DIR_READ;

    if (!Data || Len == 0 || Len > HX_FLASH_RW_MAX_LEN)
        return STATUS_INVALID_PARAMETER;

    s = HimaxBurstEnable(Ctx, Len > HX_ADDR_LEN);
    if (!NT_SUCCESS(s)) return s;

    HxPutLe32(a, Addr);
    s = HimaxBusWrite(Ctx, HX_CMD_AHB_ADDR, a, HX_ADDR_LEN);
    if (!NT_SUCCESS(s)) goto restore;
    s = HimaxBusWrite(Ctx, HX_CMD_AHB_DIR, &dir, 1);
    if (!NT_SUCCESS(s)) goto restore;
    s = HimaxBusRead(Ctx, HX_CMD_AHB_RDATA, Data, Len);
    if (!NT_SUCCESS(s)) goto restore;

    if (Len > HX_ADDR_LEN) {
        s = HimaxBurstEnable(Ctx, FALSE);
        if (!NT_SUCCESS(s)) return s;
    }
    return STATUS_SUCCESS;
restore:
    if (Len > HX_ADDR_LEN) (VOID)HimaxBurstEnable(Ctx, FALSE);
    return s;
}

/* himax_mcu_register_write / flash_write_burst_lenth, troceado a HX_MAX_WRITE_CHUNK */
NTSTATUS HimaxRegWrite(PDEVICE_CONTEXT Ctx, ULONG Addr, const UCHAR *Data, ULONG Len)
{
    NTSTATUS s;
    ULONG off = 0;

    if ((!Data && Len != 0) || Addr > MAXULONG - Len)
        return STATUS_INVALID_PARAMETER;

    s = HimaxBurstEnable(Ctx, Len > HX_ADDR_LEN);
    if (!NT_SUCCESS(s)) return s;

    while (off < Len) {
        ULONG chunk = min(Len - off, HX_MAX_WRITE_CHUNK);
        /* El búfer lleva [F2][cmd][addr 4][datos]; dirección por bloque */
        Ctx->TxBuffer[0] = HX_SPI_WRITE;
        Ctx->TxBuffer[1] = HX_CMD_AHB_ADDR;
        HxPutLe32(Ctx->TxBuffer + 2, Addr + off);
        RtlCopyMemory(Ctx->TxBuffer + 6, Data + off, chunk);
        s = GeniSpiTransfer(Ctx, Ctx->TxBuffer, chunk + 6, NULL, 0);
        if (!NT_SUCCESS(s)) {
            if (Len > HX_ADDR_LEN) (VOID)HimaxBurstEnable(Ctx, FALSE);
            return s;
        }
        off += chunk;
    }
    if (Len > HX_ADDR_LEN) {
        s = HimaxBurstEnable(Ctx, FALSE);
        if (!NT_SUCCESS(s)) return s;
    }
    return STATUS_SUCCESS;
}

NTSTATUS HimaxRegWrite32(PDEVICE_CONTEXT Ctx, ULONG Addr, ULONG Value)
{
    UCHAR d[HX_ADDR_LEN];
    HxPutLe32(d, Value);
    return HimaxRegWrite(Ctx, Addr, d, HX_ADDR_LEN);
}

/* hx83102_pin_reset */
VOID HimaxPinReset(PDEVICE_CONTEXT Ctx)
{
    TlmmWriteOutput(Ctx, GPIO_TOUCH_RST, 0);
    HxLog("pin reset GPIO%u bajo: IO=%08X", GPIO_TOUCH_RST, TlmmReadRaw(Ctx, GPIO_TOUCH_RST, 4));
    HxSleepMs(20);
    TlmmWriteOutput(Ctx, GPIO_TOUCH_RST, 1);
    HxLog("pin reset GPIO%u alto: IO=%08X", GPIO_TOUCH_RST, TlmmReadRaw(Ctx, GPIO_TOUCH_RST, 4));
    HxSleepMs(50);
}

/* himax_mcu_interface_on: despierta el bus y fija los modos conti/incr4 */
NTSTATUS HimaxInterfaceOn(PDEVICE_CONTEXT Ctx)
{
    UCHAR dummy[HX_ADDR_LEN], rb = 0, conti = HX_DATA_CONTI, incr4 = HX_DATA_INCR4;

    NTSTATUS s = HimaxBusRead(Ctx, HX_CMD_AHB_RDATA, dummy, HX_ADDR_LEN); /* knock */
    if (!NT_SUCCESS(s)) return s;

    for (ULONG cnt = 0; cnt < 10; cnt++) {
        s = HimaxBusWrite(Ctx, HX_CMD_CONTI, &conti, 1);
        if (!NT_SUCCESS(s)) return s;
        s = HimaxBusWrite(Ctx, HX_CMD_INCR4, &incr4, 1);
        if (!NT_SUCCESS(s)) return s;
        s = HimaxBusRead(Ctx, HX_CMD_CONTI, &rb, 1);
        if (!NT_SUCCESS(s)) return s;
        UCHAR rb2 = 0;
        s = HimaxBusRead(Ctx, HX_CMD_INCR4, &rb2, 1);
        if (!NT_SUCCESS(s)) return s;
        if (rb == conti && rb2 == incr4) return STATUS_SUCCESS;
        HxSleepMs(1);
    }
    return STATUS_IO_TIMEOUT;
}

/* hx83102e_sense_off */
NTSTATUS HimaxSenseOff(PDEVICE_CONTEXT Ctx, BOOLEAN CheckEn)
{
    UCHAR tmp[HX_ADDR_LEN] = {0};
    UCHAR pw;
    NTSTATUS s;

    for (ULONG cnt = 0; cnt < 10; cnt++) {
        if (cnt == 0 || (tmp[0] != 0xA5 && tmp[0] != 0x00 && tmp[0] != 0x87)) {
            s = HimaxRegWrite32(Ctx, HX_REG_CTRL_FW, HX_DATA_FW_STOP);
            if (!NT_SUCCESS(s)) return s;
        }
        HxSleepMs(1);

        s = HimaxRegRead(Ctx, HX_REG_CENTRAL_STATE, tmp, HX_ADDR_LEN);
        if (!NT_SUCCESS(s)) return s;
        if (tmp[0] != 0x05) break;    /* no hay que esperar al FW */
        if (!CheckEn) break;

        HxSleepMs(10);
        s = HimaxRegRead(Ctx, HX_REG_CTRL_FW, tmp, HX_ADDR_LEN);
        if (!NT_SUCCESS(s)) return s;
        if (tmp[0] == 0x87) break;
    }

    for (ULONG cnt = 0; cnt < 5; cnt++) {
        pw = HX_PSW_LB;
        s = HimaxBusWrite(Ctx, HX_CMD_PSW_LB, &pw, 1);
        if (!NT_SUCCESS(s)) return s;
        pw = HX_PSW_UB;
        s = HimaxBusWrite(Ctx, HX_CMD_PSW_UB, &pw, 1);
        if (!NT_SUCCESS(s)) return s;

        s = HimaxRegRead(Ctx, HX_REG_CENTRAL_STATE, tmp, HX_ADDR_LEN);
        if (!NT_SUCCESS(s)) return s;
        HxLog("sense_off: central_state=%02X (0x0C = modo seguro)", tmp[0]);
        if (tmp[0] == 0x0C) {
            s = HimaxRegWrite32(Ctx, HX_REG_TCON_RST, 0); /* reset TCON */
            if (!NT_SUCCESS(s)) return s;
            HxSleepMs(1);
            return STATUS_SUCCESS;
        }
        HxSleepMs(5);
        HimaxPinReset(Ctx);
    }
    return STATUS_IO_TIMEOUT;
}

/* hx83102_chip_detect (parte HX83102E) */
NTSTATUS HimaxDetect(PDEVICE_CONTEXT Ctx)
{
    UCHAR id[HX_ADDR_LEN], dummy = 0;
    NTSTATUS s;

    HimaxPinReset(Ctx);
    s = HimaxBusRead(Ctx, HX_CMD_CONTI, &dummy, 1);
    if (!NT_SUCCESS(s)) return s;
    {
        UCHAR st[HX_ADDR_LEN] = {0};
        if (NT_SUCCESS(HimaxRegRead(Ctx, HX_REG_CENTRAL_STATE, st, HX_ADDR_LEN)))
            HxLog("detect: central_state tras reset = %02X (04/01 = FW propio corriendo, 0C = modo seguro)", st[0]);
    }

    s = HimaxSenseOff(Ctx, FALSE);
    if (!NT_SUCCESS(s)) { HxLog("detect: sense_off ha fallado"); return s; }

    for (ULONG i = 0; i < 5; i++) {
        s = HimaxRegRead(Ctx, HX_REG_IC_ID, id, HX_ADDR_LEN);
        if (!NT_SUCCESS(s)) return s;
        HxLog("detect: IC ID = %02X %02X %02X", id[3], id[2], id[1]);
        if (id[3] == 0x83 && id[2] == 0x10 && id[1] == 0x2E) {
            Ctx->ChipDetected = TRUE;
            HxLog("HX83102E detectado");
            return STATUS_SUCCESS;
        }
    }
    return STATUS_DEVICE_DOES_NOT_EXIST;
}

/* hx83102e_reload_to_active */
NTSTATUS HimaxReloadToActive(PDEVICE_CONTEXT Ctx)
{
    UCHAR d[HX_ADDR_LEN];
    NTSTATUS s;
    for (ULONG i = 0; i < 30; i++) {
        s = HimaxRegWrite32(Ctx, HX_REG_RELOAD, HX_DATA_RELOAD_IN);
        if (!NT_SUCCESS(s)) return s;
        HxSleepMs(1);
        s = HimaxRegRead(Ctx, HX_REG_RELOAD, d, HX_ADDR_LEN);
        if (!NT_SUCCESS(s)) return s;
        if (d[1] == 0x01 && d[0] == 0xEC) return STATUS_SUCCESS;
    }
    return STATUS_IO_TIMEOUT;
}

/* hx83102e_sense_on (FlashMode = 0) */
NTSTATUS HimaxSenseOn(PDEVICE_CONTEXT Ctx)
{
    NTSTATUS s = HimaxInterfaceOn(Ctx);
    if (!NT_SUCCESS(s)) return s;
    s = HimaxRegWrite32(Ctx, HX_REG_CTRL_FW, 0);
    if (!NT_SUCCESS(s)) return s;
    HxSleepMs(10);
    HimaxPinReset(Ctx);
    return HimaxReloadToActive(Ctx);
}

/* himax_mcu_check_CRC: CRC32 por hardware de Len bytes desde SramAddr. 0 = OK. */
ULONG HimaxCheckCrcHw(PDEVICE_CONTEXT Ctx, ULONG SramAddr, ULONG Len)
{
    UCHAR a[HX_ADDR_LEN], d[HX_ADDR_LEN];
    ULONG words = Len / HX_ADDR_LEN;

    HxPutLe32(a, SramAddr);
    if (!NT_SUCCESS(HimaxRegWrite(Ctx, HX_REG_RELOAD_ADDR_FROM, a, HX_ADDR_LEN)))
        return 0xFFFFFFFF;

    d[3] = 0x00; d[2] = 0x99; d[1] = (UCHAR)(words >> 8); d[0] = (UCHAR)words;
    if (!NT_SUCCESS(HimaxRegWrite(Ctx, HX_REG_RELOAD_ADDR_CMD, d, HX_ADDR_LEN)))
        return 0xFFFFFFFF;

    for (ULONG cnt = 0; cnt < 100; cnt++) {
        if (!NT_SUCCESS(HimaxRegRead(Ctx, HX_REG_RELOAD_STATUS, d, HX_ADDR_LEN)))
            return 0xFFFFFFFF;
        if ((d[0] & 0x01) != 0x01) {
            if (!NT_SUCCESS(HimaxRegRead(Ctx, HX_REG_RELOAD_CRC32, d, HX_ADDR_LEN)))
                return 0xFFFFFFFF;
            return HxGetLe32(d);
        }
        HxSleepMs(1);
    }
    return 0xFFFFFFFF;   /* HW_CRC_FAIL */
}

/* hx83102e_read_event_stack */
NTSTATUS HimaxReadEventStack(PDEVICE_CONTEXT Ctx, UCHAR *Buf, ULONG Len)
{
    return HimaxBusRead(Ctx, HX_CMD_EVENT, Buf, Len);
}
