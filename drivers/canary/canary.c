/*
 * Driver "canario" de arranque para ARM64 (sin WDK: prototipos minimos propios).
 *
 * Dibuja en el framebuffer del UEFI (region "Display Reserved", 0x5C000000,
 * 1200x2000, 32 bpp BGRX) para saber hasta donde llega el kernel de Windows.
 * SLOT 1/2/3 elige columna y color (verde / amarillo / magenta).
 *
 * Por columna:
 *   - Cuadrado de color 100x100: DriverEntry se ha ejecutado.
 *   - 4 digitos: pulsos de un temporizador del kernel (+1 cada 500 ms).
 *   - Barra bajo los digitos: prueba del reloj del sistema. En DriverEntry se
 *     espera ~200 ms (medidos con el contador de la CPU) y se mira si
 *     KUSER_SHARED_DATA.InterruptTime ha avanzado.  VERDE = el reloj funciona;
 *     ROJA = no hay interrupcion de reloj.
 * Solo SLOT 3: tabla de registros (ver REGS abajo) en 3 filas de 6 valores
 * hexadecimales de 8 cifras.
 */
typedef unsigned char      UCHAR;
typedef unsigned short     USHORT;
typedef unsigned int       ULONG;
typedef long               LONG;
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long SIZE_T;
typedef long               NTSTATUS;
typedef void              *PVOID;

typedef union _LARGE_INTEGER {
    struct { ULONG LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
typedef struct _UNICODE_STRING { USHORT Length; USHORT MaximumLength; USHORT *Buffer; } UNICODE_STRING, *PUNICODE_STRING;

#ifndef SLOT
#define SLOT 1
#endif

#define FB_PHYS   0x5C000000ULL
#define FB_W      1200
#define FB_H      2000
#define FB_BYTES  ((SIZE_T)FB_W * FB_H * 4)
#define MmNonCached     0
#define MmWriteCombined 2

#define GICD_PHYS 0x0F200000ULL     /* distribuidor */
#define GICR_PHYS 0x0F300000ULL     /* redistribuidor, CPU0: RD en +0, SGI/PPI en +0x10000 */

__declspec(dllimport) PVOID MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, int CacheType);
__declspec(dllimport) void  KeInitializeTimerEx(PVOID Timer, int Type);
__declspec(dllimport) void  KeInitializeDpc(PVOID Dpc, PVOID Routine, PVOID Context);
__declspec(dllimport) UCHAR KeSetTimerEx(PVOID Timer, LARGE_INTEGER DueTime, LONG Period, PVOID Dpc);

typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY;
__declspec(dllimport) UCHAR KeRegisterBugCheckCallback(PVOID Record, PVOID Routine, PVOID Buffer, ULONG Length, UCHAR *Component);
__declspec(dllimport) extern LIST_ENTRY PsLoadedModuleList;
__declspec(dllimport) LONG  PsCreateSystemThread(PVOID *Handle, ULONG Access, PVOID Oa, PVOID Proc, PVOID Cid, PVOID Start, PVOID Ctx);
__declspec(dllimport) LONG  ZwClose(PVOID Handle);
__declspec(dllimport) LONG  KeSetPriorityThread(PVOID Thread, LONG Prio);
__declspec(dllimport) PVOID KeGetCurrentThread(void);

__declspec(align(64)) static UCHAR g_timer[256];
__declspec(align(64)) static UCHAR g_bcrec[128];
__declspec(align(64)) static UCHAR g_bcbuf[64];
__declspec(align(64)) static UCHAR g_dpc[256];
static volatile ULONG *g_fb;
static ULONG g_tick;

#define COL_X (20 + (SLOT - 1) * 180)

static void FillRect(ULONG x, ULONG y, ULONG w, ULONG h, ULONG color)
{
    ULONG yy, xx;
    if (!g_fb) return;
    for (yy = y; yy < y + h && yy < FB_H; yy++)
        for (xx = x; xx < x + w && xx < FB_W; xx++)
            g_fb[(SIZE_T)yy * FB_W + xx] = color;
}

static ULONG SlotColor(void)
{
    if (SLOT == 1) return 0x0000FF00;   /* verde */
    if (SLOT == 2) return 0x00FFFF00;   /* amarillo */
    return 0x00FF00FF;                  /* magenta */
}

/* 7 segmentos: bit0=a(arriba) 1=b 2=c 3=d(abajo) 4=e 5=f 6=g(medio); 0-9 y A-F */
static const UCHAR k7seg[16] = { 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F, 0x77,0x7C,0x39,0x5E,0x79,0x71 };

static void DrawDigitSz(ULONG x, ULONG y, ULONG d, ULONG W, ULONG H, ULONG t, ULONG on)
{
    UCHAR m = k7seg[d & 15];
    const ULONG off = 0x00202020;
    FillRect(x, y, W, H, 0x00000000);
    FillRect(x + t, y,                     W - 2 * t, t, (m & 0x01) ? on : off);
    FillRect(x + W - t, y + t,             t, H / 2 - t - t / 2, (m & 0x02) ? on : off);
    FillRect(x + W - t, y + H / 2 + t / 2, t, H / 2 - t - t / 2, (m & 0x04) ? on : off);
    FillRect(x + t, y + H - t,             W - 2 * t, t, (m & 0x08) ? on : off);
    FillRect(x, y + H / 2 + t / 2,         t, H / 2 - t - t / 2, (m & 0x10) ? on : off);
    FillRect(x, y + t,                     t, H / 2 - t - t / 2, (m & 0x20) ? on : off);
    FillRect(x + t, y + H / 2 - t / 2,     W - 2 * t, t, (m & 0x40) ? on : off);
}

static void DrawCount(ULONG v)
{
    DrawDigitSz(COL_X,       140, (v / 1000) % 10, 36, 64, 8, 0x00FFFFFF);
    DrawDigitSz(COL_X + 42,  140, (v / 100) % 10,  36, 64, 8, 0x00FFFFFF);
    DrawDigitSz(COL_X + 84,  140, (v / 10) % 10,   36, 64, 8, 0x00FFFFFF);
    DrawDigitSz(COL_X + 126, 140, v % 10,          36, 64, 8, 0x00FFFFFF);
}

/* Valor hexadecimal de 8 cifras, digitos de 18x32; 'row' y 'col' en la tabla (6 columnas) */
static void DrawHex(ULONG col, ULONG row, ULONG v, ULONG color)
{
    ULONG i, x = 20 + col * 196, y = 270 + row * 52;
    for (i = 0; i < 8; i++)
        DrawDigitSz(x + i * 22, y, (v >> (28 - 4 * i)) & 15, 18, 32, 4, color);
}

static void DrawDec(ULONG col, ULONG row, ULONG v, ULONG color)
{
    ULONG i, p = 10000000, x = 20 + col * 196, y = 270 + row * 52;
    for (i = 0; i < 8; i++, p /= 10)
        DrawDigitSz(x + i * 22, y, (v / p) % 10, 18, 32, 4, color);
}

#include "font8x14.h"

static void DrawStr(ULONG x, ULONG y, const char *t, ULONG color)
{
    ULONG gx, gy, bit;
    for (; *t; t++, x += 16) {
        const UCHAR *g = &kFont[((UCHAR)*t - 32) * 14];
        FillRect(x, y, 16, 28, 0);
        for (gy = 0; gy < 14; gy++)
            for (bit = 0; bit < 8; bit++)
                if (g[gy] & (0x80 >> bit)) FillRect(x + bit * 2, y + gy * 2, 2, 2, color);
    }
}

static void Hex(char *o, ULONGLONG v, int digits)
{
    int i;
    for (i = 0; i < digits; i++) { ULONG d = (ULONG)(v >> (4 * (digits - 1 - i))) & 15; o[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10); }
    o[digits] = 0;
}

/* Localiza el modulo del kernel que contiene la direccion 'a'; name = 12 chars + 0 */
static int FindMod(ULONGLONG a, ULONGLONG *off, char *name)
{
    LIST_ENTRY *h = &PsLoadedModuleList, *e = h->Flink;
    int n = 0, i;
    while (e && e != h && n++ < 600) {
        UCHAR *m = (UCHAR *)e;
        ULONGLONG base = *(ULONGLONG *)(m + 0x30);
        ULONG sz = *(ULONG *)(m + 0x40);
        if (a >= base && a < base + sz) {
            UNICODE_STRING *u = (UNICODE_STRING *)(m + 0x58);
            for (i = 0; i < 12; i++) name[i] = ' ';
            name[12] = 0;
            if (u->Buffer) for (i = 0; i < 12 && i < (int)(u->Length / 2); i++) name[i] = (char)u->Buffer[i];
            *off = a - base;
            return 1;
        }
        e = e->Flink;
    }
    return 0;
}

static void DrawAddr(ULONG y, const char *tag, ULONGLONG a, ULONG color)
{
    char line[40], h[17], name[13];
    ULONGLONG off = 0;
    int i, p = 0;
    for (i = 0; tag[i] && p < 4; i++) line[p++] = tag[i];
    while (p < 4) line[p++] = ' ';
    Hex(h, a, 16);
    for (i = 0; i < 16; i++) line[p++] = h[i];
    line[p++] = ' ';
    if (FindMod(a, &off, name)) {
        for (i = 0; i < 12; i++) line[p++] = name[i];
        line[p++] = '+';
        Hex(h, off, 8);
        for (i = 0; i < 8; i++) line[p++] = h[i];
    }
    line[p] = 0;
    DrawStr(8, y, line, color);
}

static inline ULONGLONG rd_esr(void) { ULONGLONG v; __asm__ volatile("mrs %0, esr_el1" : "=r"(v)); return v; }
static inline ULONGLONG rd_elr(void) { ULONGLONG v; __asm__ volatile("mrs %0, elr_el1" : "=r"(v)); return v; }
static inline ULONGLONG rd_far(void) { ULONGLONG v; __asm__ volatile("mrs %0, far_el1" : "=r"(v)); return v; }

/* Se ejecuta durante un bugcheck: dibuja ESR/ELR/FAR y la pila, y se queda aqui (evita el reinicio). */
static void BugCheckCb(PVOID Buffer, ULONG Length)
{
    ULONGLONG *fp = (ULONGLONG *)__builtin_frame_address(0), *nx;
    ULONG y = 760, i;
    (void)Buffer; (void)Length;
    FillRect(0, 700, FB_W, 56, 0x00C00000);
    DrawStr(20, 714, "BUGCHECK!", 0x00FFFFFF);
    DrawAddr(y, "ESR", rd_esr(), 0x00FFFFFF);  y += 30;
    DrawAddr(y, "ELR", rd_elr(), 0x0000FFFF);  y += 30;
    DrawAddr(y, "FAR", rd_far(), 0x0000FFFF);  y += 30;
    for (i = 0; i < 12; i++) {
        if ((ULONGLONG)fp < 0xFFFF000000000000ULL || ((ULONGLONG)fp & 15)) break;
        DrawAddr(y, "LR", fp[1], 0x0000FF00);  y += 30;
        nx = (ULONGLONG *)fp[0];
        if ((ULONGLONG)nx <= (ULONGLONG)fp || (ULONGLONG)nx - (ULONGLONG)fp > 0x4000) break;
        fp = nx;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    for (;;) { }
}

static void TimerDpc(PVOID Dpc, PVOID Ctx, PVOID A1, PVOID A2)
{
    (void)Dpc; (void)Ctx; (void)A1; (void)A2;
    g_tick++;
    DrawCount(g_tick);
}

static inline ULONGLONG rd_cntvct(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline ULONGLONG rd_cntfrq(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
static inline ULONGLONG rd_cntv_ctl(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v)); return v; }
static inline ULONGLONG rd_cntv_cval(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntv_cval_el0" : "=r"(v)); return v; }
static inline ULONGLONG rd_daif(void) { ULONGLONG v; __asm__ volatile("mrs %0, daif" : "=r"(v)); return v; }
static inline ULONGLONG rd_icc_sre(void) { ULONGLONG v; __asm__ volatile("mrs %0, S3_0_C12_C12_5" : "=r"(v)); return v; }
static inline ULONGLONG rd_icc_pmr(void) { ULONGLONG v; __asm__ volatile("mrs %0, S3_0_C4_C6_0" : "=r"(v)); return v; }
static inline ULONGLONG rd_icc_igrpen1(void) { ULONGLONG v; __asm__ volatile("mrs %0, S3_0_C12_C12_7" : "=r"(v)); return v; }

static inline ULONGLONG rd_icc_rpr(void) { ULONGLONG v; __asm__ volatile("mrs %0, S3_0_C12_C11_3" : "=r"(v)); return v; }
static inline ULONGLONG rd_icc_hppir1(void) { ULONGLONG v; __asm__ volatile("mrs %0, S3_0_C12_C12_2" : "=r"(v)); return v; }

#define KUSER_INTERRUPT_TIME_LOW (*(volatile ULONG *)0xFFFFF78000000008ULL)

static volatile ULONG *g_gicd, *g_gicr;
#define KUSER_SYSTEM_TIME_LOW    (*(volatile ULONG *)0xFFFFF78000000014ULL)
static inline ULONGLONG rd_cntp_ctl(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v)); return v; }

/* Hilo monitor.  Fila 3 (blanco): ms (dec), InterruptTime, ticks DPC (dec), CNTV_CTL, CNTV_CVAL-CNTVCT, CNTP_CTL.
   Fila 4 (naranja): ICC_RPR, ICC_HPPIR1, ICC_PMR, DAIF, ISENABLER0, ISPENDR0.
   Fila 5 (cian): SystemTime, 1a palabra SPI pendiente (indice, valor), numero de SPI pendientes, ISACTIVER0, CurrentEL. */
static void MonThread(PVOID ctx)
{
    ULONGLONG t0 = rd_cntvct(), per, freq = rd_cntfrq();
    ULONG ms, lastdraw = 0, w, b, p, pi, pv, pc;
    (void)ctx;
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    if (freq == 0) freq = 19200000;
    per = freq / 1000;
    for (;;) {
        ms = (ULONG)((rd_cntvct() - t0) / per);
        if (ms - lastdraw < 100) continue;
        lastdraw = ms;
        pi = 0xFF; pv = 0; pc = 0;
        if (g_gicd) for (w = 1; w < 16; w++) {
            p = g_gicd[(0x200 + 4 * w) / 4];
            if (p) { if (pi == 0xFF) { pi = w; pv = p; } for (b = 0; b < 32; b++) pc += (p >> b) & 1; }
        }
        DrawDec(0, 3, ms,                                   0x00FFFFFF);
        DrawHex(1, 3, KUSER_INTERRUPT_TIME_LOW,             0x00FFFFFF);
        DrawDec(2, 3, g_tick,                               0x00FFFFFF);
        DrawHex(3, 3, (ULONG)rd_cntv_ctl(),                 0x00FFFFFF);
        DrawHex(4, 3, (ULONG)(rd_cntv_cval() - rd_cntvct()),0x00FFFFFF);
        DrawHex(5, 3, (ULONG)rd_cntp_ctl(),                 0x00FFFFFF);
        DrawHex(0, 4, (ULONG)rd_icc_rpr(),                  0x00FF8000);
        DrawHex(1, 4, (ULONG)rd_icc_hppir1(),               0x00FF8000);
        DrawHex(2, 4, (ULONG)rd_icc_pmr(),                  0x00FF8000);
        DrawHex(3, 4, (ULONG)rd_daif(),                     0x00FF8000);
        if (g_gicr) DrawHex(4, 4, g_gicr[(0x10000 + 0x100) / 4], 0x00FF8000);
        if (g_gicr) DrawHex(5, 4, g_gicr[(0x10000 + 0x200) / 4], 0x00FF8000);
        DrawHex(0, 5, KUSER_SYSTEM_TIME_LOW,                0x0000FFFF);
        DrawHex(1, 5, pi,                                   0x0000FFFF);
        DrawHex(2, 5, pv,                                   0x0000FFFF);
        DrawHex(3, 5, pc,                                   0x0000FFFF);
        if (g_gicr) DrawHex(4, 5, g_gicr[(0x10000 + 0x300) / 4], 0x0000FFFF);
        { ULONGLONG el; __asm__ volatile("mrs %0, CurrentEL" : "=r"(el)); DrawHex(5, 5, (ULONG)el, 0x0000FFFF); }
    }
}

NTSTATUS DriverEntry(PVOID DriverObject, PUNICODE_STRING RegistryPath)
{
    PHYSICAL_ADDRESS pa;
    LARGE_INTEGER due;
    ULONGLONG t0, freq, wait;
    ULONG k0, k1, i;
    (void)DriverObject; (void)RegistryPath;

    pa.QuadPart = (LONGLONG)FB_PHYS;
    g_fb = (volatile ULONG *)MmMapIoSpace(pa, FB_BYTES, MmWriteCombined);
    if (!g_fb) return 0;

    FillRect(COL_X, 20, 100, 100, SlotColor());
    DrawCount(0);

    /* --- Prueba del reloj del sistema: espera ~200 ms medidos por el contador de la CPU --- */
    freq = rd_cntfrq(); if (freq == 0) freq = 19200000;
    wait = freq / 5;
    k0 = KUSER_INTERRUPT_TIME_LOW;
    t0 = rd_cntvct();
    for (i = 0; i < 400000000u && (rd_cntvct() - t0) < wait; i++) { }
    k1 = KUSER_INTERRUPT_TIME_LOW;
    FillRect(COL_X, 215, 160, 14, (k1 != k0) ? 0x0000C000 : 0x00FF0000);   /* verde / roja */

#if SLOT == 3
    {
        volatile ULONG *gicd, *gicr;
        ULONG v_ctl, v_cval, delta;
        pa.QuadPart = (LONGLONG)GICD_PHYS;
        gicd = (volatile ULONG *)MmMapIoSpace(pa, 0x1000, MmNonCached);
        pa.QuadPart = (LONGLONG)GICR_PHYS;
        gicr = (volatile ULONG *)MmMapIoSpace(pa, 0x20000, MmNonCached);
        v_ctl = (ULONG)rd_cntv_ctl(); v_cval = (ULONG)rd_cntv_cval(); delta = (ULONG)(rd_cntvct() - t0);
        /* REGS  fila 0 */
        DrawHex(0, 0, (ULONG)freq,            0x00FFFF00);   /* CNTFRQ_EL0 (esperado 0x012C0300)         */
        DrawHex(1, 0, (ULONG)rd_cntvct(),     0x00FFFF00);   /* CNTVCT_EL0 (parte baja)                   */
        DrawHex(2, 0, v_ctl,                  0x00FFFF00);   /* CNTV_CTL_EL0: bit0 ENABLE, bit1 IMASK, bit2 ISTATUS */
        DrawHex(3, 0, v_cval,                 0x00FFFF00);   /* CNTV_CVAL_EL0 (parte baja)                */
        DrawHex(4, 0, (ULONG)rd_daif(),       0x00FFFF00);   /* DAIF: bit7 I, bit6 F (0x3C0 = todo enmascarado) */
        DrawHex(5, 0, (ULONG)rd_icc_sre(),    0x00FFFF00);   /* ICC_SRE_EL1                               */
        /* fila 1 */
        DrawHex(0, 1, (ULONG)rd_icc_pmr(),    0x0000FFFF);   /* ICC_PMR_EL1                               */
        DrawHex(1, 1, (ULONG)rd_icc_igrpen1(),0x0000FFFF);   /* ICC_IGRPEN1_EL1: bit0 = grupo 1 habilitado */
        if (gicd) DrawHex(2, 1, gicd[0],      0x0000FFFF);   /* GICD_CTLR                                 */
        if (gicr) DrawHex(3, 1, gicr[0x14/4], 0x0000FFFF);   /* GICR_WAKER                                */
        if (gicr) DrawHex(4, 1, gicr[(0x10000 + 0x80)/4],  0x0000FFFF);   /* GICR_IGROUPR0 (bit19 = PPI 19) */
        if (gicr) DrawHex(5, 1, gicr[(0x10000 + 0x100)/4], 0x0000FFFF);   /* GICR_ISENABLER0               */
        /* fila 2 */
        if (gicr) DrawHex(0, 2, gicr[(0x10000 + 0x200)/4], 0x0000FF00);   /* GICR_ISPENDR0                 */
        if (gicr) DrawHex(1, 2, gicr[(0x10000 + 0x300)/4], 0x0000FF00);   /* GICR_ISACTIVER0               */
        if (gicr) DrawHex(2, 2, gicr[(0x10000 + 0x410)/4], 0x0000FF00);   /* GICR_IPRIORITYR4 (PPI 16-19)  */
        DrawHex(3, 2, k0,                     0x0000FF00);   /* InterruptTime antes                       */
        DrawHex(4, 2, k1,                     0x0000FF00);   /* InterruptTime despues                     */
        DrawHex(5, 2, delta,                  0x0000FF00);   /* ciclos de CNTVCT esperados                */
    }
#endif

    KeInitializeTimerEx(g_timer, 1 /* SynchronizationTimer */);
    KeInitializeDpc(g_dpc, (PVOID)TimerDpc, 0);
    due.QuadPart = -5000000LL;           /* 500 ms, relativo */
    KeSetTimerEx(g_timer, due, 500, g_dpc);

#if SLOT == 3
    g_bcbuf[0] = 0;
    KeRegisterBugCheckCallback(g_bcrec, (PVOID)BugCheckCb, g_bcbuf, sizeof(g_bcbuf), (UCHAR *)"CANARY");
    DrawAddr(700, "SELF", (ULONGLONG)(PVOID)DriverEntry, 0x00FFFFFF);   /* autotest: debe mostrar canary3.sys */
    /* Hilo monitor (prioridad minima): sigue dibujando aunque DriverEntry ya haya vuelto.
       Si la pantalla se sigue actualizando tras el cuelgue, el kernel NO esta girando en un
       bucle a DISPATCH_LEVEL: esta en espera (p. ej. con el reloj muerto).  Si se congela, si. */
    {
        PVOID th = 0;
        LONG st;
        pa.QuadPart = (LONGLONG)GICD_PHYS;
        g_gicd = (volatile ULONG *)MmMapIoSpace(pa, 0x1000, MmNonCached);
        pa.QuadPart = (LONGLONG)GICR_PHYS;
        g_gicr = (volatile ULONG *)MmMapIoSpace(pa, 0x20000, MmNonCached);
        st = PsCreateSystemThread(&th, 0x1FFFFF, 0, 0, 0, (PVOID)MonThread, 0);
        if (st >= 0 && th) ZwClose(th);
        else DrawHex(0, 3, (ULONG)st, 0x00FF0000);
    }
#endif
    return 0;
}
