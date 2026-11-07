#pragma once
#include "device.h"

/*
 * himax — protocolo HX83102E. Port de drivers/input/touchscreen/hxchipset/
 * (himax_platform.c, himax_ic_incell_core.c, himax_ic_HX83102.c) del kernel
 * LineageOS android_kernel_samsung_sm6115.
 */

/* Prefijos SPI (himax_platform.c) */
#define HX_SPI_WRITE      0xF2   /* F2 cmd datos...          */
#define HX_SPI_READ       0xF3   /* F3 cmd 00, luego lectura */

/* Comandos del bus */
#define HX_CMD_AHB_ADDR   0x00
#define HX_CMD_AHB_RDATA  0x08
#define HX_CMD_AHB_DIR    0x0C
#define HX_CMD_DIR_READ   0x00
#define HX_CMD_INCR4      0x0D
#define HX_CMD_CONTI      0x13
#define HX_DATA_CONTI     0x31
#define HX_DATA_INCR4     0x10
#define HX_CMD_EVENT      0x30
#define HX_CMD_PSW_LB     0x31
#define HX_CMD_PSW_UB     0x32
#define HX_PSW_LB         0x27
#define HX_PSW_UB         0x95

/* Registros AHB (himax_ic_core.h) */
#define HX_REG_CTRL_FW        0x9000005C
#define HX_DATA_FW_STOP       0x000000A5
#define HX_REG_CENTRAL_STATE  0x900000A8
#define HX_REG_IC_ID          0x900000D0
#define HX_REG_SYSTEM_RESET   0x90000018
#define HX_DATA_SYSTEM_RESET  0x00000055
#define HX_REG_TCON_RST       0x80020004   /* hx83102e_ic_adr_tcon_rst */
#define HX_REG_RELOAD         0x90000048
#define HX_DATA_RELOAD_IN     0x000000EC
#define HX_REG_SET_FRAME      0x10007294
#define HX_REG_FLASH_RELOAD   0x10007F00   /* driver_addr_fw_define_flash_reload */
#define HX_DATA_DIS_RELOAD    0x00009AA9   /* zf_data_dis_flash_reload */

/* CRC por hardware (himax_mcu_check_CRC) */
#define HX_REG_RELOAD_STATUS      0x80050000
#define HX_REG_RELOAD_CRC32       0x80050018
#define HX_REG_RELOAD_ADDR_FROM   0x80050020
#define HX_REG_RELOAD_ADDR_CMD    0x80050028

#define HX_ADDR_LEN   4
#define HX_FLASH_RW_MAX_LEN  256   /* tope de una lectura AHB */

/* Bajo nivel */
NTSTATUS HimaxBusWrite(PDEVICE_CONTEXT Ctx, UCHAR Cmd, const UCHAR *Data, ULONG Len);
NTSTATUS HimaxBusRead (PDEVICE_CONTEXT Ctx, UCHAR Cmd, UCHAR *Data, ULONG Len);
NTSTATUS HimaxBurstEnable(PDEVICE_CONTEXT Ctx, BOOLEAN AutoAdd4);
NTSTATUS HimaxRegRead (PDEVICE_CONTEXT Ctx, ULONG Addr, UCHAR *Data, ULONG Len);
NTSTATUS HimaxRegWrite(PDEVICE_CONTEXT Ctx, ULONG Addr, const UCHAR *Data, ULONG Len);
NTSTATUS HimaxRegWrite32(PDEVICE_CONTEXT Ctx, ULONG Addr, ULONG Value);

/* Secuencias */
VOID     HimaxPinReset(PDEVICE_CONTEXT Ctx);
NTSTATUS HimaxInterfaceOn(PDEVICE_CONTEXT Ctx);
NTSTATUS HimaxSenseOff(PDEVICE_CONTEXT Ctx, BOOLEAN CheckEn);
NTSTATUS HimaxDetect(PDEVICE_CONTEXT Ctx);
NTSTATUS HimaxReloadToActive(PDEVICE_CONTEXT Ctx);
NTSTATUS HimaxSenseOn(PDEVICE_CONTEXT Ctx);
ULONG    HimaxCheckCrcHw(PDEVICE_CONTEXT Ctx, ULONG SramAddr, ULONG Len);
NTSTATUS HimaxReadEventStack(PDEVICE_CONTEXT Ctx, UCHAR *Buf, ULONG Len);
