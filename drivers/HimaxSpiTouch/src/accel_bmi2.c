/*
 * accel_bmi2.c — Bosch BMI260 / BMI270 por I2C (solo el acelerómetro).
 * Secuencias de BoschSensortec/BMI270_SensorAPI (bmi2.c: bmi2_sec_init, write_config_file).
 */
#include "accel.h"
#include "accel_blobs.h"
#include "util.h"

#define BMI2_REG_CHIP_ID          0x00
#define BMI2_REG_ACC_X_LSB        0x0C
#define BMI2_REG_INTERNAL_STATUS  0x21
#define BMI2_REG_ACC_CONF         0x40
#define BMI2_REG_ACC_RANGE        0x41
#define BMI2_REG_INIT_CTRL        0x59
#define BMI2_REG_INIT_ADDR_0      0x5B
#define BMI2_REG_INIT_DATA        0x5E
#define BMI2_REG_PWR_CONF         0x7C
#define BMI2_REG_PWR_CTRL         0x7D
#define BMI2_REG_CMD              0x7E
#define BMI2_CMD_SOFT_RESET       0xB6
#define BMI2_PWR_CTRL_ACC_EN      0x04
#define BMI2_ACC_CONF_100HZ       0xA8     /* ODR 100 Hz, media de 4, modo de rendimiento */
#define BMI2_ACC_RANGE_4G         0x01     /* 8192 LSB por g */
#define BMI2_CHUNK                32       /* bytes de configuración por escritura (par) */

static NTSTATUS Wr1(PDEVICE_CONTEXT c, UCHAR Reg, UCHAR Val)
{
    return AccelRegWrite(c, Reg, &Val, 1);
}

NTSTATUS Bmi2Probe(PDEVICE_CONTEXT c)
{
    static const UCHAR addrs[2] = { 0x68, 0x69 };
    for (ULONG i = 0; i < 2; i++) {
        UCHAR id = 0;
        c->Accel.Addr = addrs[i];
        NTSTATUS s = AccelRegRead(c, BMI2_REG_CHIP_ID, &id, 1);
        if (!NT_SUCCESS(s)) {
            HxLog("ACC: dirección %02X: sin respuesta (%08X)", addrs[i], (ULONG)s);
            continue;
        }
        HxLog("ACC: dirección %02X: CHIP_ID=%02X (24 = BMI270, 27 = BMI260)", addrs[i], id);
        if (id == BMI270_CHIP_ID || id == BMI260_CHIP_ID) {
            c->Accel.ChipId = id;
            return STATUS_SUCCESS;
        }
        /* Otro chip: anotar también el WHO_AM_I de InvenSense (0x75) para identificarlo. */
        {
            UCHAR who = 0;
            if (NT_SUCCESS(AccelRegRead(c, 0x75, &who, 1)))
                HxLog("ACC: dirección %02X: reg 0x75=%02X (un ICM-4x6xx daría 0x4x/0x6x)", addrs[i], who);
        }
    }
    return STATUS_NO_SUCH_DEVICE;
}

NTSTATUS Bmi2Init(PDEVICE_CONTEXT c)
{
    const UCHAR *cfg = (c->Accel.ChipId == BMI270_CHIP_ID) ? g_Bmi270Config : g_Bmi260Config;
    NTSTATUS s;
    UCHAR v = 0;

    /* Reinicio por software: deja el chip como recién encendido (suspendido). El chip
     * se reinicia en cuanto recibe la orden, así que el resultado de esta escritura no cuenta. */
    (VOID)Wr1(c, BMI2_REG_CMD, BMI2_CMD_SOFT_RESET);
    HxSleepMs(10);
    s = AccelRegRead(c, BMI2_REG_CHIP_ID, &v, 1);
    HxLog("ACC: tras el reinicio CHIP_ID=%02X (%08X)", v, (ULONG)s);
    if (!NT_SUCCESS(s)) return s;

    /* Quitar el ahorro avanzado de energía y esperar >= 450 us antes de cargar. */
    if (!NT_SUCCESS(s = Wr1(c, BMI2_REG_PWR_CONF, 0x00))) return s;
    HxSleepMs(2);
    if (!NT_SUCCESS(s = Wr1(c, BMI2_REG_INIT_CTRL, 0x00))) return s;

    HxLog("ACC: cargando la configuración del %s (%u bytes)",
          (c->Accel.ChipId == BMI270_CHIP_ID) ? "BMI270" : "BMI260", (ULONG)BMI2_CONFIG_SIZE);
    for (ULONG idx = 0; idx < BMI2_CONFIG_SIZE; idx += BMI2_CHUNK) {
        UCHAR a[2];
        a[0] = (UCHAR)((idx / 2) & 0x0F);
        a[1] = (UCHAR)((idx / 2) >> 4);
        if (!NT_SUCCESS(s = AccelRegWrite(c, BMI2_REG_INIT_ADDR_0, a, 2)) ||
            !NT_SUCCESS(s = AccelRegWrite(c, BMI2_REG_INIT_DATA, cfg + idx, BMI2_CHUNK))) {
            HxLog("ACC: la carga falla en el byte %u (%08X)", idx, (ULONG)s);
            return s;
        }
    }
    if (!NT_SUCCESS(s = Wr1(c, BMI2_REG_INIT_CTRL, 0x01))) return s;

    /* El chip tarda hasta ~150 ms en validar la configuración: INTERNAL_STATUS[3:0] = 1. */
    v = 0;
    for (ULONG i = 0; i < 30; i++) {
        HxSleepMs(20);
        s = AccelRegRead(c, BMI2_REG_INTERNAL_STATUS, &v, 1);
        if (NT_SUCCESS(s) && (v & 0x0F) == 0x01) break;
    }
    HxLog("ACC: INTERNAL_STATUS=%02X (1 = configuración aceptada)", v);
    if (!NT_SUCCESS(s)) return s;
    if ((v & 0x0F) != 0x01) return STATUS_DEVICE_NOT_READY;

    if (!NT_SUCCESS(s = Wr1(c, BMI2_REG_ACC_CONF, BMI2_ACC_CONF_100HZ))) return s;
    if (!NT_SUCCESS(s = Wr1(c, BMI2_REG_ACC_RANGE, BMI2_ACC_RANGE_4G))) return s;
    if (!NT_SUCCESS(s = Wr1(c, BMI2_REG_PWR_CTRL, BMI2_PWR_CTRL_ACC_EN))) return s;
    HxSleepMs(60);    /* primeras muestras válidas */
    return STATUS_SUCCESS;
}

NTSTATUS Bmi2ReadAccel(PDEVICE_CONTEXT c, SHORT Raw[3])
{
    UCHAR b[6];
    NTSTATUS s = AccelRegRead(c, BMI2_REG_ACC_X_LSB, b, 6);
    if (!NT_SUCCESS(s)) return s;
    Raw[0] = (SHORT)((USHORT)b[0] | ((USHORT)b[1] << 8));
    Raw[1] = (SHORT)((USHORT)b[2] | ((USHORT)b[3] << 8));
    Raw[2] = (SHORT)((USHORT)b[4] | ((USHORT)b[5] << 8));
    return STATUS_SUCCESS;
}
