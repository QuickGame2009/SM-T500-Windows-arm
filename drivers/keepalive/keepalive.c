/*
 * keepalive.sys: driver de arranque minimo (ARM64, sin WDK) que reproduce lo que hacian los
 * canarios y que parece necesario para que el arranque no se congele:
 *   1) un temporizador periodico del kernel (cada 500 ms) con una DPC vacia;
 *   2) un hilo de prioridad 1 que nunca se duerme (la CPU no entra en reposo).
 * No dibuja nada en pantalla.  Opcion "Modo" (DWORD en la clave del servicio), mascara de bits:
 *   1 = temporizador periodico 500 ms
 *   2 = hilo de prioridad 1 que no duerme
 *   4 = lectura del volumen de Windows (\Device\HarddiskVolume80) cada 3 s, como el FsThread del canario
 *   8 = lectura de registros del controlador eMMC (SDHCI 0x04744000) cada 100 ms, como el MonThread
 *  16 = linea de estado abajo del todo en pantalla cada 500 ms (necesita 2): segundos, pulsos del temporizador,
 *       lecturas de disco y su estado, y registros SDHCI PRESENT_STATE / INT_STATUS / INT_EN
 *  32 = acariciar el watchdog hardware APSS (0x0F017000, WDT_RST) cada 500 ms
 *  64 = desactivar el watchdog hardware (WDT_EN = 0) al cargar
 * 128 = encender el USB antes que Windows: GDSC USB30_PRIM y relojes (GCC 0x01400000)
 *   La linea de estado (16) muestra ademas U = GDSCR del USB y mascara de relojes USB apagados (bit31 CLK_OFF).
 *   La linea de estado (16) muestra ademas W = WDT_EN, WDT_STS, BARK y BITE.
 *   Modo = 0 equivale a 3 (temporizador + hilo).
 */
typedef unsigned char UCHAR; typedef unsigned short USHORT; typedef unsigned int ULONG;
typedef long LONG; typedef long long LONGLONG; typedef unsigned long long ULONGLONG; typedef void *PVOID;
typedef long NTSTATUS;
typedef union _LARGE_INTEGER { struct { ULONG LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct _UNICODE_STRING { USHORT Length; USHORT MaximumLength; USHORT *Buffer; } UNICODE_STRING, *PUNICODE_STRING;
typedef struct _IOSB { ULONGLONG St; ULONGLONG Info; } IOSB;
typedef struct _OBJ_ATTR { ULONG Length; PVOID Root; PUNICODE_STRING Name; ULONG Attr; PVOID Sd; PVOID Sqos; } OBJ_ATTR;
__declspec(dllimport) void  KeInitializeTimerEx(PVOID Timer, int Type);
__declspec(dllimport) void  KeInitializeDpc(PVOID Dpc, PVOID Routine, PVOID Ctx);
__declspec(dllimport) UCHAR KeSetTimerEx(PVOID Timer, LARGE_INTEGER Due, LONG Period, PVOID Dpc);
__declspec(dllimport) LONG  PsCreateSystemThread(PVOID *Handle, ULONG Access, PVOID Oa, PVOID Proc, PVOID Cid, PVOID Start, PVOID Ctx);
__declspec(dllimport) LONG  ZwClose(PVOID Handle);
__declspec(dllimport) LONG  KeSetPriorityThread(PVOID Thread, LONG Prio);
__declspec(dllimport) PVOID KeGetCurrentThread(void);
__declspec(dllimport) LONG  ZwCreateFile(PVOID *H, ULONG Acc, OBJ_ATTR *Oa, IOSB *Io, PVOID Alloc, ULONG Attr, ULONG Share, ULONG Disp, ULONG Opts, PVOID Ea, ULONG EaLen);
__declspec(dllimport) LONG  ZwReadFile(PVOID H, PVOID Ev, PVOID Apc, PVOID Ctx, IOSB *Io, PVOID Buf, ULONG Len, LARGE_INTEGER *Off, PVOID Key);
__declspec(dllimport) PVOID MmMapIoSpace(LARGE_INTEGER Pa, ULONGLONG Bytes, int CacheType);
__declspec(dllimport) LONG  ZwOpenKey(PVOID *Handle, ULONG Access, OBJ_ATTR *Oa);
__declspec(dllimport) LONG  ZwQueryValueKey(PVOID H, PUNICODE_STRING Name, int Class, PVOID Buf, ULONG Len, ULONG *Ret);

static ULONGLONG g_timer[8], g_dpc[8];
static volatile ULONG g_ticks;
static void TimerDpc(PVOID Dpc, PVOID Ctx, PVOID A1, PVOID A2) { (void)Dpc; (void)Ctx; (void)A1; (void)A2; g_ticks++; }
static ULONG g_modo;
#include "font8x14.h"
#define FB_W 1200
#define FB_ROWS 32
static volatile ULONG *g_fb;   /* franja inferior de 32 filas del framebuffer de la UEFI */
static void Fill(ULONG x, ULONG y, ULONG w, ULONG h, ULONG c) { ULONG yy, xx; for (yy = y; yy < y + h && yy < FB_ROWS; yy++) for (xx = x; xx < x + w && xx < FB_W; xx++) g_fb[yy * FB_W + xx] = c; }
static void Str(ULONG x, const char *t, ULONG c)
{
    ULONG gy, bit;
    for (; *t; t++, x += 16) {
        const UCHAR *g = &kFont[((UCHAR)*t - 32) * 14];
        Fill(x, 2, 16, 28, 0);
        for (gy = 0; gy < 14; gy++) for (bit = 0; bit < 8; bit++) if (g[gy] & (0x80 >> bit)) Fill(x + bit * 2, 2 + gy * 2, 2, 2, c);
    }
}
static char *Hx(char *o, ULONG v, int d) { int i; for (i = 0; i < d; i++) { ULONG n = (v >> (4 * (d - 1 - i))) & 15; *o++ = (char)(n < 10 ? '0' + n : 'A' + n - 10); } return o; }
static volatile ULONG *g_sd, *g_wd, *g_gcc;
static const ULONG kUsbCbcr[7] = { 0x1a010, 0x1a014, 0x1a018, 0x1a080, 0x1a084, 0x1a054, 0x9f000 };
static volatile ULONG g_sdv, g_rd, g_rdst;
static inline ULONGLONG cntvct(void) { ULONGLONG v; __asm__ volatile("isb; mrs %0, cntvct_EL0" : "=r"(v)); return v; }
static inline ULONGLONG cntfrq(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntfrq_EL0" : "=r"(v)); return v ? v : 19200000; }
static void SpinThread(PVOID ctx)
{
    ULONGLONG per = cntfrq() / 1000, t0 = cntvct(), last = 0, ms;
    (void)ctx;
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    for (;;) {
        __asm__ volatile("yield");
        if ((g_modo & 8) && g_sd) {
            ms = (cntvct() - t0) / per;
            if (ms - last >= 100) { last = ms; g_sdv = g_sd[0x24 / 4]; }
        }
        if ((g_modo & 32) && g_wd) {
            static ULONGLONG lastp; ULONGLONG m3 = (cntvct() - t0) / per;
            if (m3 - lastp >= 500) { lastp = m3; g_wd[0x04 / 4] = 1; }
        }
        if ((g_modo & 16) && g_fb) {
            static ULONGLONG lastd; ULONGLONG m2 = (cntvct() - t0) / per;
            if (m2 - lastd >= 500) {
                char ln[80], *q = ln; lastd = m2;
                *q++ = 'T'; q = Hx(q, (ULONG)(m2 / 1000), 4); *q++ = ' ';
                *q++ = 'K'; q = Hx(q, g_ticks, 4); *q++ = ' ';
                *q++ = 'R'; q = Hx(q, g_rd, 4); *q++ = ' ';
                *q++ = 'S'; q = Hx(q, g_rdst, 8); *q++ = ' ';
                if (g_sd) { *q++ = 'P'; q = Hx(q, g_sd[0x24 / 4], 8); *q++ = ' ';
                            *q++ = 'I'; q = Hx(q, g_sd[0x30 / 4], 8); *q++ = ' '; }
                if (g_gcc) { ULONG k, m = 0; *q++ = 'U'; q = Hx(q, g_gcc[0x1a004 / 4], 8); *q++ = '/';
                             for (k = 0; k < 7; k++) if (g_gcc[kUsbCbcr[k] / 4] & 0x80000000u) m |= 1u << k;
                             q = Hx(q, m, 2); *q++ = ' '; }
                if (g_wd) { *q++ = 'W'; q = Hx(q, g_wd[0x08 / 4], 1); q = Hx(q, g_wd[0x0C / 4], 2); *q++ = ' ';
                            q = Hx(q, g_wd[0x10 / 4], 5); *q++ = '/'; q = Hx(q, g_wd[0x14 / 4], 5); }
                *q = 0;
                Str(8, ln, 0x0000FF00);
            }
        }
    }
}
static UCHAR g_sec[4096];
static void DiskThread(PVOID ctx)
{
    static USHORT nm[] = { '\\','D','e','v','i','c','e','\\','H','a','r','d','d','i','s','k','V','o','l','u','m','e','8','0', 0 };
    ULONGLONG per = cntfrq() / 1000, t0 = cntvct(); ULONG n = 0;
    (void)ctx;
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    for (;;) {
        UNICODE_STRING us; OBJ_ATTR oa; IOSB io; PVOID h = 0; LARGE_INTEGER lo; LONG st;
        while ((cntvct() - t0) / per < 6000ULL + 3000ULL * n) __asm__ volatile("yield");
        n++;
        us.Length = 24 * 2; us.MaximumLength = us.Length; us.Buffer = nm;
        oa.Length = sizeof(oa); oa.Root = 0; oa.Name = &us; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
        io.St = 0; io.Info = 0;
        st = ZwCreateFile(&h, 0x120089, &oa, &io, 0, 0x80, 7, 1, 0x20, 0, 0);
        if (st >= 0 && h) {
            io.St = 0; io.Info = 0; lo.QuadPart = 0;
            st = ZwReadFile(h, 0, 0, 0, &io, g_sec, 4096, &lo, 0);
            ZwClose(h);
        }
        g_rd = n; g_rdst = (ULONG)st;
    }
}
static ULONG ReadModo(PUNICODE_STRING reg)
{
    static USHORT nm[] = { 'M','o','d','o', 0 }; UNICODE_STRING us; OBJ_ATTR oa; PVOID h = 0; ULONG buf[8], ret = 0, v = 0;
    oa.Length = sizeof(oa); oa.Root = 0; oa.Name = reg; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
    if (ZwOpenKey(&h, 0x20019, &oa) < 0 || !h) return 0;
    us.Length = 8; us.MaximumLength = 10; us.Buffer = nm;
    /* KeyValuePartialInformation (2): TitleIndex, Type, DataLength, Data */
    if (ZwQueryValueKey(h, &us, 2, buf, sizeof(buf), &ret) >= 0 && buf[1] == 4) v = buf[3];
    ZwClose(h);
    return v;
}
NTSTATUS DriverEntry(PVOID DriverObject, PUNICODE_STRING RegistryPath)
{
    PVOID th;
    (void)DriverObject;
    g_modo = ReadModo(RegistryPath);
    if (g_modo == 0) g_modo = 3;
    if (g_modo & 16) { LARGE_INTEGER pf; pf.QuadPart = 0x5C000000LL + (LONGLONG)(2000 - FB_ROWS) * FB_W * 4; g_fb = (volatile ULONG *)MmMapIoSpace(pf, FB_W * FB_ROWS * 4, 2); g_modo |= 2; }
    { LARGE_INTEGER pw; pw.QuadPart = 0x0F017000LL; g_wd = (volatile ULONG *)MmMapIoSpace(pw, 0x1000, 0);
      if (g_wd && (g_modo & 64)) g_wd[0x08 / 4] = 0;
      if (g_wd && (g_modo & 32)) g_wd[0x04 / 4] = 1; }
    if (g_modo & 32) g_modo |= 2;
    { LARGE_INTEGER pg; pg.QuadPart = 0x01400000LL; g_gcc = (volatile ULONG *)MmMapIoSpace(pg, 0xA0000, 0); }
    if (g_gcc && (g_modo & 128)) {
        ULONG k, n;
        g_gcc[0x1a004 / 4] &= ~1u;                                   /* GDSC: SW_COLLAPSE = 0 (encender) */
        for (n = 0; n < 1000000 && !(g_gcc[0x1a004 / 4] & 0x80000000u); n++) __asm__ volatile("yield");
        for (k = 0; k < 7; k++) g_gcc[kUsbCbcr[k] / 4] |= 1u;        /* CLK_ENABLE */
        for (n = 0; n < 100000; n++) __asm__ volatile("yield");
    }
    if (g_modo & 24) { LARGE_INTEGER pa; pa.QuadPart = 0x04744000LL; g_sd = (volatile ULONG *)MmMapIoSpace(pa, 0x1000, 0); }
    if (g_modo & 1) {
        LARGE_INTEGER due; due.QuadPart = -5000000LL;
        KeInitializeTimerEx(g_timer, 1);
        KeInitializeDpc(g_dpc, (PVOID)TimerDpc, 0);
        KeSetTimerEx(g_timer, due, 500, g_dpc);
    }
    if (g_modo & 58) { th = 0; if (PsCreateSystemThread(&th, 0x1FFFFF, 0, 0, 0, (PVOID)SpinThread, 0) >= 0 && th) ZwClose(th); }
    if (g_modo & 4)  { th = 0; if (PsCreateSystemThread(&th, 0x1FFFFF, 0, 0, 0, (PVOID)DiskThread, 0) >= 0 && th) ZwClose(th); }
    return 0;
}
