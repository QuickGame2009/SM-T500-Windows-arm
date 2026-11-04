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

__declspec(align(64)) static UCHAR g_timer[256];
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
    /* Bucle en vivo 60 s: demuestra si el codigo normal y los DPC siguen corriendo y que
       interrupciones (SPI) estan pendientes/activas.  Fila 3 (blanco): ms transcurridos,
       InterruptTime, ticks del DPC, 1a palabra ISPENDR no nula (indice, valor, bits).
       Fila 4 (naranja): idem ISACTIVER, ICC_RPR, ICC_HPPIR1, mayor hueco entre vueltas (ms). */
    {
        volatile ULONG *gd;
        ULONGLONG tl = rd_cntvct(), per = freq / 1000;
        ULONG ms, lastms = 0, lastdraw = 0, maxgap = 0, w, b, p, a;
        ULONG pi, pv, pc, ai, av, ac;
        pa.QuadPart = (LONGLONG)GICD_PHYS;
        gd = (volatile ULONG *)MmMapIoSpace(pa, 0x1000, MmNonCached);
        for (;;) {
            ms = (ULONG)((rd_cntvct() - tl) / per);
            if (ms > 60000) break;
            if (ms - lastms > maxgap) maxgap = ms - lastms;
            lastms = ms;
            if (ms - lastdraw < 50) continue;
            lastdraw = ms;
            pi = 0xFF; pv = 0; pc = 0; ai = 0xFF; av = 0; ac = 0;
            if (gd) for (w = 1; w < 16; w++) {
                p = gd[(0x200 + 4 * w) / 4]; a = gd[(0x300 + 4 * w) / 4];
                if (p) { if (pi == 0xFF) { pi = w; pv = p; } for (b = 0; b < 32; b++) pc += (p >> b) & 1; }
                if (a) { if (ai == 0xFF) { ai = w; av = a; } for (b = 0; b < 32; b++) ac += (a >> b) & 1; }
            }
            DrawDec(0, 3, ms,                    0x00FFFFFF);
            DrawHex(1, 3, KUSER_INTERRUPT_TIME_LOW, 0x00FFFFFF);
            DrawDec(2, 3, g_tick,                0x00FFFFFF);
            DrawHex(3, 3, pi,                    0x00FFFFFF);
            DrawHex(4, 3, pv,                    0x00FFFFFF);
            DrawHex(5, 3, pc,                    0x00FFFFFF);
            DrawHex(0, 4, ai,                    0x00FF8000);
            DrawHex(1, 4, av,                    0x00FF8000);
            DrawHex(2, 4, ac,                    0x00FF8000);
            DrawHex(3, 4, (ULONG)rd_icc_rpr(),   0x00FF8000);
            DrawHex(4, 4, (ULONG)rd_icc_hppir1(),0x00FF8000);
            DrawDec(5, 4, maxgap,                0x00FF8000);
        }
    }
#endif
    return 0;
}
