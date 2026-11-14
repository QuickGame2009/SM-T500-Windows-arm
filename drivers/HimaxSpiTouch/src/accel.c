/*
 * accel.c — hilo del acelerómetro: arranque por etapas con protección contra
 * reinicios, y bucle de muestreo que alimenta el sensor HID.
 * Las etapas y los valores del registro están descritos en accel.h.
 */
#include <ntifs.h>
#include "accel.h"
#include "util.h"

static KSTART_ROUTINE AccelThread;

#define ACCEL_STAGE_DEFAULT    6
#define ACCEL_AXISMAP_DEFAULT  0xA10   /* Xw = +x, Yw = +y, Zw = -z (ver MapAxes) */
#define ACCEL_START_DELAY_MS   15000   /* empezar con el escritorio ya cargado */

static ULONG ParamGet(PCWSTR Name, ULONG Default)
{
    ULONG value = Default;
    WDFKEY key = NULL;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, Name);
    if (NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_READ,
                                                      WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        ULONG v = 0;
        if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &v))) value = v;
        WdfRegistryClose(key);
    }
    return value;
}

/* Escribe el valor y fuerza el volcado de la colmena a disco, para que sobreviva
 * a un reinicio brusco justo después. */
static VOID ParamSet(PCWSTR Name, ULONG Value)
{
    WDFKEY key = NULL;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, Name);
    NTSTATUS s = WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_SET_VALUE,
                                                    WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (NT_SUCCESS(s)) {
        s = WdfRegistryAssignULong(key, &name, Value);
        if (NT_SUCCESS(s)) (VOID)ZwFlushKey(WdfRegistryWdmGetHandle(key));
        WdfRegistryClose(key);
    }
    if (!NT_SUCCESS(s)) HxLog("ACC: no se pudo escribir %ws en el registro (%08X)", Name, (ULONG)s);
}

static VOID Guard(PDEVICE_CONTEXT c, ULONG Step)
{
    ParamSet(L"AccelGuard", 10 * c->Accel.Bus + Step);
}

static VOID UnmapSe(PDEVICE_CONTEXT c)
{
    if (c->Accel.Se) { MmUnmapIoSpace(c->Accel.Se, ACCEL_SE_LEN); c->Accel.Se = NULL; }
}

/*
 * Ejes. El chip da +1 g en el eje que apunta hacia arriba; Windows quiere lo contrario
 * (tablet boca arriba sobre la mesa: Z = -1 g) y sus ejes referidos a la orientación
 * nativa de la pantalla (vertical): X hacia la derecha, Y hacia arriba, Z hacia quien mira.
 * La configuración de sensores de Samsung para esta placa (P85943AA2_bmi26x_0.json)
 * dice que los ejes de Android son (-x, -y, +z) del chip, y Windows = -Android:
 * Xw = +x, Yw = +y, Zw = -z  ->  AccelAxisMap = 0xA10.
 */
static VOID MapAxes(ULONG Map, const SHORT Raw[3], SHORT Mg[3])
{
    for (ULONG i = 0; i < 3; i++) {
        ULONG n = (Map >> (4 * i)) & 0xF;
        ULONG src = n & 3;
        if (src > 2) src = i;
        LONG mg = ((LONG)Raw[src] * 125) / 1024;      /* 8192 LSB por g -> milésimas de g */
        if (n & 8) mg = -mg;
        if (mg > 32767) mg = 32767;
        if (mg < -32767) mg = -32767;
        Mg[i] = (SHORT)mg;
    }
}

/* Prueba un bus hasta donde permita Stage. STATUS_SUCCESS = chip listo (Stage >= 5). */
static NTSTATUS TryBus(PDEVICE_CONTEXT c, ULONG Bus)
{
    ACCEL_STATE *a = &c->Accel;
    NTSTATUS s;
    PHYSICAL_ADDRESS pa;

    a->Bus = Bus;
    HxLog("ACC: --- bus SE%u (GPIO%u/%u) ---", Bus, (Bus == 1) ? 4 : 6, (Bus == 1) ? 5 : 7);

    Guard(c, 1);
    HxLog("ACC: paso 1: relojes");
    if (!NT_SUCCESS(s = AccelBusClocks(c))) return s;
    if (a->Stage < 2) return STATUS_NOT_FOUND;

    pa.QuadPart = (LONGLONG)ACCEL_SE_PHYS(Bus);
    a->Se = (PUCHAR)MmMapIoSpaceEx(pa, ACCEL_SE_LEN, PAGE_READWRITE | PAGE_NOCACHE);
    if (!a->Se) return STATUS_INSUFFICIENT_RESOURCES;

    Guard(c, 2);
    HxLog("ACC: paso 2: leer el SE");
    if (!NT_SUCCESS(s = AccelBusIdent(c))) goto fail;
    if (a->Stage < 3) { s = STATUS_NOT_FOUND; goto fail; }

    Guard(c, 3);
    HxLog("ACC: paso 3: pines");
    if (!NT_SUCCESS(s = AccelBusPins(c, ParamGet(L"AccelPullUp", 0) != 0))) goto fail;
    if (a->Stage < 4) { s = STATUS_NOT_FOUND; goto fail; }

    Guard(c, 4);
    HxLog("ACC: paso 4: I2C y detección");
    if (!NT_SUCCESS(s = AccelBusInit(c, ParamGet(L"AccelKhz", 400)))) goto fail;
    AccelBusScan(c);
    if (!NT_SUCCESS(s = AccelBusInit(c, ParamGet(L"AccelKhz", 400)))) goto fail;
    if (!NT_SUCCESS(s = Bmi2Probe(c))) goto fail;
    HxLog("ACC: paso 4 OK: chip %02X en SE%u, dirección %02X", a->ChipId, Bus, a->Addr);
    if (a->Stage < 5) { s = STATUS_NOT_FOUND; goto fail; }

    Guard(c, 5);
    HxLog("ACC: paso 5: configurar el BMI");
    if (!NT_SUCCESS(s = Bmi2Init(c))) goto fail;
    for (ULONG i = 0; i < 3; i++) {
        SHORT raw[3], mg[3];
        if (NT_SUCCESS(Bmi2ReadAccel(c, raw))) {
            MapAxes(a->AxisMap, raw, mg);
            HxLog("ACC: muestra %u: chip x=%d y=%d z=%d  ->  Windows X=%d Y=%d Z=%d mg",
                  i, raw[0], raw[1], raw[2], mg[0], mg[1], mg[2]);
        }
        HxSleepMs(100);
    }
    HxLog("ACC: paso 5 OK");
    return STATUS_SUCCESS;

fail:
    UnmapSe(c);
    return s;
}

static NTSTATUS BringUp(PDEVICE_CONTEXT c)
{
    ACCEL_STATE *a = &c->Accel;
    ULONG busSel  = ParamGet(L"AccelBus", 0);
    ULONG guard   = ParamGet(L"AccelGuard", 0);
    ULONG bad     = ParamGet(L"AccelBadBus", 0);
    NTSTATUS s = STATUS_NOT_FOUND;

    a->Stage   = ParamGet(L"AccelStage", ACCEL_STAGE_DEFAULT);
    a->AxisMap = ParamGet(L"AccelAxisMap", ACCEL_AXISMAP_DEFAULT);
    HxLog("ACC: === acelerómetro: AccelStage=%u AccelBus=%u AxisMap=%03X Guard=%u BadBus=%u ===",
          a->Stage, busSel, a->AxisMap, guard, bad);
    if (a->Stage == 0) { HxLog("ACC: desactivado (AccelStage=0)"); return STATUS_NOT_FOUND; }

    if (guard != 0) {
        /* La vez anterior no se llegó a borrar: ese paso tumbó la tablet. */
        ULONG gb = guard / 10;
        HxLog("ACC: el arranque anterior se cortó en el bus SE%u, paso %u: no se vuelve a usar ese bus",
              gb, guard % 10);
        if (gb < 31) bad |= (1u << gb);
        ParamSet(L"AccelBadBus", bad);
        ParamSet(L"AccelGuard", 0);
    }

    {
        ULONG list[2] = { 2, 1 };
        ULONG count = 2;
        if (busSel == 1 || busSel == 2) { list[0] = busSel; count = 1; }
        for (ULONG i = 0; i < count; i++) {
            if (bad & (1u << list[i])) {
                HxLog("ACC: bus SE%u descartado (AccelBadBus); borra ese valor para reintentar", list[i]);
                continue;
            }
            s = TryBus(c, list[i]);
            ParamSet(L"AccelGuard", 0);
            if (NT_SUCCESS(s)) break;
            HxLog("ACC: bus SE%u: no listo (%08X)", list[i], (ULONG)s);
        }
    }
    if (!NT_SUCCESS(s)) return s;
    if (a->Stage < 6) { HxLog("ACC: AccelStage < 6: no se crea el sensor HID"); return STATUS_NOT_FOUND; }

    HxLog("ACC: paso 6: sensor HID");
    return AccelHidCreate(c);
}

static VOID AccelThread(PVOID Context)
{
    PDEVICE_CONTEXT c = (PDEVICE_CONTEXT)Context;
    ACCEL_STATE *a = &c->Accel;
    LARGE_INTEGER due;

    due.QuadPart = -(LONGLONG)ACCEL_START_DELAY_MS * 10000;
    if (KeWaitForSingleObject(&c->StopEvent, Executive, KernelMode, FALSE, &due) == STATUS_SUCCESS)
        PsTerminateSystemThread(STATUS_SUCCESS);

    NTSTATUS s = BringUp(c);
    if (!NT_SUCCESS(s)) {
        HxLog("ACC: el acelerómetro no queda activo (%08X); el táctil no se ve afectado", (ULONG)s);
        PsTerminateSystemThread(STATUS_SUCCESS);
    }
    HxLog("ACC: acelerómetro listo; enviando muestras a Windows");

    ULONG sent = 0, errs = 0, sinceSend = 0, sinceLog = 0;
    for (;;) {
        KIRQL irql;
        ULONG interval;
        UCHAR reporting, power;
        USHORT sens;
        SHORT raw[3], mg[3], last[3];
        BOOLEAN have;

        KeAcquireSpinLock(&a->Lock, &irql);
        interval = a->ReportIntervalMs; reporting = a->ReportingState; power = a->PowerState;
        sens = a->Sensitivity; have = a->HaveSample;
        last[0] = a->Last[0]; last[1] = a->Last[1]; last[2] = a->Last[2];
        KeReleaseSpinLock(&a->Lock, irql);

        if (interval < 20) interval = 20;
        if (interval > 1000) interval = 1000;
        due.QuadPart = -(LONGLONG)interval * 10000;
        if (KeWaitForSingleObject(&c->StopEvent, Executive, KernelMode, FALSE, &due) == STATUS_SUCCESS)
            break;
        sinceSend += interval; sinceLog += interval;

        /* Windows ha pedido silencio (sin eventos o sensor apagado): no muestrear. */
        if (power != 2 || reporting == 1 || reporting == 4) continue;

        if (!NT_SUCCESS(Bmi2ReadAccel(c, raw))) {
            if (++errs == 20) HxLog("ACC: 20 lecturas seguidas con error");
            if (errs > 20) HxSleepMs(500);
            continue;
        }
        errs = 0;
        MapAxes(a->AxisMap, raw, mg);

        BOOLEAN changed = !have;
        for (ULONG i = 0; i < 3 && !changed; i++) {
            LONG d = (LONG)mg[i] - (LONG)last[i];
            if (d < 0) d = -d;
            if (d >= (LONG)sens && d != 0) changed = TRUE;
        }
        if (!changed && sinceSend < 1000) continue;

        KeAcquireSpinLock(&a->Lock, &irql);
        a->Last[0] = mg[0]; a->Last[1] = mg[1]; a->Last[2] = mg[2];
        a->HaveSample = TRUE;
        KeReleaseSpinLock(&a->Lock, irql);
        (VOID)AccelHidSubmit(c, mg);
        sinceSend = 0;
        sent++;

        /* Diario: las 5 primeras y luego una cada 5 minutos. */
        if (sent <= 5 || sinceLog >= 300000) {
            sinceLog = 0;
            HxLog("ACC: enviadas=%u X=%d Y=%d Z=%d mg (intervalo=%u ms, estado=%u, sensibilidad=%u)",
                  sent, mg[0], mg[1], mg[2], interval, reporting, sens);
        }
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS AccelStart(PDEVICE_CONTEXT c)
{
    ACCEL_STATE *a = &c->Accel;
    HANDLE h = NULL;
    OBJECT_ATTRIBUTES oa;

    KeInitializeSpinLock(&a->Lock);
    a->ReportingState   = 2;      /* todos los eventos */
    a->PowerState       = 2;      /* D0 */
    a->ReportIntervalMs = 100;
    a->Sensitivity      = 20;     /* 0,02 g */
    a->HaveSample       = FALSE;
    a->Thread           = NULL;

    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    NTSTATUS s = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, &oa, NULL, NULL, AccelThread, c);
    if (!NT_SUCCESS(s)) { HxLog("ACC: no se pudo crear el hilo (%08X)", (ULONG)s); return s; }
    s = ObReferenceObjectByHandle(h, THREAD_ALL_ACCESS, *PsThreadType, KernelMode,
                                  (PVOID *)&a->Thread, NULL);
    ZwClose(h);
    if (!NT_SUCCESS(s)) a->Thread = NULL;   /* el hilo sigue y saldrá con StopEvent */
    return s;
}

VOID AccelStop(PDEVICE_CONTEXT c)
{
    ACCEL_STATE *a = &c->Accel;
    if (a->Thread) {
        KeWaitForSingleObject(a->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(a->Thread);
        a->Thread = NULL;
    }
    AccelHidDelete(c);
    UnmapSe(c);
}
