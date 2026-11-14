#pragma once
#include "device.h"

/*
 * accel — acelerómetro Bosch BMI260/BMI270 de la tablet, expuesto a Windows como
 * sensor HID (acelerómetro 3D) para que el propio Windows gire la pantalla.
 *
 * En Android el sensor lo maneja el ADSP (SSC). En Windows el ADSP no arranca,
 * así que el bus I2C queda libre y lo llevamos desde aquí con el SE GENI en
 * modo FIFO por sondeo, igual que el SPI del táctil.
 *
 * Valores en HKLM\SYSTEM\CurrentControlSet\Services\HimaxSpiTouch\Parameters:
 *   AccelStage   (DWORD, por defecto 6)
 *      0 = desactivado            1 = relojes del SE
 *      2 = + leer el SE (versión del firmware GENI, FIFO, estado de las líneas)
 *      3 = + pines I2C (TLMM)     4 = + iniciar I2C y detectar el chip
 *      5 = + configurar el BMI y leer muestras (solo al log)
 *      6 = + sensor HID (funcionamiento completo)
 *   AccelBus     (DWORD) 0 = automático (SE2 y luego SE1), 1 = SE1 (GPIO4/5), 2 = SE2 (GPIO6/7)
 *   AccelAxisMap (DWORD, por defecto 0xA10) un nibble por eje de Windows (X = bits 0-3,
 *                Y = 4-7, Z = 8-11): bits 0-1 = eje del chip (0 x, 1 y, 2 z), bit 3 = invertir.
 *   AccelKhz     (DWORD) 400 (por defecto) o 100.
 *   AccelPullUp  (DWORD) 1 = activar los pull-up internos de los pines I2C (prueba).
 *   AccelGuard   lo escribe el driver antes de cada paso de riesgo (10*bus + paso) y lo borra
 *                al acabar. Si al arrancar sigue puesto, ese paso reinició la tablet: el bus
 *                se apunta en AccelBadBus (máscara de bits) y no se vuelve a tocar.
 *                Para reintentar: borrar AccelBadBus.
 */

/* accel.c */
NTSTATUS AccelStart(PDEVICE_CONTEXT Ctx);   /* crea el hilo (D0Entry, tras el táctil) */
VOID     AccelStop (PDEVICE_CONTEXT Ctx);   /* StopEvent ya señalado; espera y libera */

/* accel_bus.c — relojes, pines e I2C GENI del SE elegido (Ctx->Accel.Bus) */
#define ACCEL_SE_PHYS(bus)   (0x04A80000ULL + 0x4000ULL * (bus))
#define ACCEL_SE_LEN         0x4000
NTSTATUS AccelBusClocks(PDEVICE_CONTEXT Ctx);
NTSTATUS AccelBusIdent (PDEVICE_CONTEXT Ctx);                 /* solo lectura */
NTSTATUS AccelBusPins  (PDEVICE_CONTEXT Ctx, BOOLEAN PullUp);
NTSTATUS AccelBusInit  (PDEVICE_CONTEXT Ctx, ULONG Khz);
VOID     AccelBusScan  (PDEVICE_CONTEXT Ctx);                 /* solo lecturas; al log */
/* Escribe Reg seguido de Len bytes (Len <= 60). */
NTSTATUS AccelRegWrite(PDEVICE_CONTEXT Ctx, UCHAR Reg, const UCHAR *Data, ULONG Len);
/* Escribe Reg y, con inicio repetido, lee Len bytes. */
NTSTATUS AccelRegRead (PDEVICE_CONTEXT Ctx, UCHAR Reg, UCHAR *Data, ULONG Len);

/* accel_bmi2.c */
#define BMI270_CHIP_ID  0x24
#define BMI260_CHIP_ID  0x27
NTSTATUS Bmi2Probe(PDEVICE_CONTEXT Ctx);                      /* fija Addr y ChipId */
NTSTATUS Bmi2Init (PDEVICE_CONTEXT Ctx);                      /* carga la configuración, ±4 g, 100 Hz */
NTSTATUS Bmi2ReadAccel(PDEVICE_CONTEXT Ctx, SHORT Raw[3]);    /* 8192 LSB por g */

/* accel_hid.c */
NTSTATUS AccelHidCreate(PDEVICE_CONTEXT Ctx);
VOID     AccelHidDelete(PDEVICE_CONTEXT Ctx);
/* Event: 4 = datos nuevos. Mg = ejes de Windows en milésimas de g. */
NTSTATUS AccelHidSubmit(PDEVICE_CONTEXT Ctx, const SHORT Mg[3]);
