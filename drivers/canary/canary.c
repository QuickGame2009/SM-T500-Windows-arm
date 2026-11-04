/*
 * Driver "canario" de arranque para ARM64 (sin WDK: prototipos minimos propios).
 *
 * Al cargarse, dibuja directamente en el framebuffer del UEFI (region "Display
 * Reserved", 0x5C000000, 1200x2000, 32 bpp BGRX) para saber hasta donde llega
 * el kernel de Windows:
 *   - Un cuadrado de color de 100x100 al ejecutarse DriverEntry (color segun SLOT).
 *   - Debajo, 4 digitos grandes con la cuenta de un temporizador del kernel
 *     (+1 cada 500 ms; 2 pulsos = 1 segundo). Si cuentan, el kernel esta vivo y las interrupciones
 *     del temporizador funcionan. Si se paran, el sistema se ha colgado.
 * SLOT 1/2/3 elige columna y color (verde / amarillo / magenta).
 */
typedef unsigned char      UCHAR;
typedef unsigned short     USHORT;
typedef unsigned int       ULONG;
typedef long               LONG;
typedef long long          LONGLONG;
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
#define MmNonCached 0
#define MmWriteCombined 2

__declspec(dllimport) PVOID MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, int CacheType);
__declspec(dllimport) void  KeInitializeTimerEx(PVOID Timer, int Type);
__declspec(dllimport) void  KeInitializeDpc(PVOID Dpc, PVOID Routine, PVOID Context);
__declspec(dllimport) UCHAR KeSetTimerEx(PVOID Timer, LARGE_INTEGER DueTime, LONG Period, PVOID Dpc);

/* Almacenamiento no paginado, alineado y de sobra grande para KTIMER y KDPC */
__declspec(align(64)) static UCHAR g_timer[256];
__declspec(align(64)) static UCHAR g_dpc[256];
static volatile ULONG *g_fb;
static ULONG g_tick;

static void FillRect(ULONG x, ULONG y, ULONG w, ULONG h, ULONG color)
{
    ULONG yy, xx;
    if (!g_fb) return;
    for (yy = y; yy < y + h && yy < FB_H; yy++)
        for (xx = x; xx < x + w && xx < FB_W; xx++)
            g_fb[(SIZE_T)yy * FB_W + xx] = color;
}

#define COL_X (20 + (SLOT - 1) * 180)

static ULONG SlotColor(void)
{
    if (SLOT == 1) return 0x0000FF00;   /* verde */
    if (SLOT == 2) return 0x00FFFF00;   /* amarillo */
    return 0x00FF00FF;                  /* magenta */
}

/* Dígitos de 7 segmentos grandes (36x64): legibles aunque la foto salga borrosa. */
static const UCHAR k7seg[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static void DrawDigit(ULONG x, ULONG y, ULONG d)
{
    UCHAR m = k7seg[d % 10];
    const ULONG W = 36, H = 64, t = 8, on = 0x00FFFFFF, off = 0x00202020;
    FillRect(x, y, W, H, 0x00000000);
    FillRect(x + t, y,               W - 2 * t, t, (m & 0x01) ? on : off);            /* a  arriba */
    FillRect(x + W - t, y + t,       t, H / 2 - t - t / 2, (m & 0x02) ? on : off);    /* b  arriba der. */
    FillRect(x + W - t, y + H / 2 + t / 2, t, H / 2 - t - t / 2, (m & 0x04) ? on : off); /* c abajo der. */
    FillRect(x + t, y + H - t,       W - 2 * t, t, (m & 0x08) ? on : off);            /* d  abajo */
    FillRect(x, y + H / 2 + t / 2,   t, H / 2 - t - t / 2, (m & 0x10) ? on : off);    /* e  abajo izq. */
    FillRect(x, y + t,               t, H / 2 - t - t / 2, (m & 0x20) ? on : off);    /* f  arriba izq. */
    FillRect(x + t, y + H / 2 - t / 2, W - 2 * t, t, (m & 0x40) ? on : off);          /* g  medio */
}

/* Cuenta de pulsos del temporizador (cada 500 ms), 4 digitos, debajo del cuadrado de color. */
static void DrawCount(ULONG v)
{
    DrawDigit(COL_X,       140, (v / 1000) % 10);
    DrawDigit(COL_X + 42,  140, (v / 100) % 10);
    DrawDigit(COL_X + 84,  140, (v / 10) % 10);
    DrawDigit(COL_X + 126, 140, v % 10);
}

static void TimerDpc(PVOID Dpc, PVOID Ctx, PVOID A1, PVOID A2)
{
    (void)Dpc; (void)Ctx; (void)A1; (void)A2;
    g_tick++;
    DrawCount(g_tick);
}

NTSTATUS DriverEntry(PVOID DriverObject, PUNICODE_STRING RegistryPath)
{
    PHYSICAL_ADDRESS pa;
    LARGE_INTEGER due;
    (void)DriverObject; (void)RegistryPath;

    pa.QuadPart = (LONGLONG)FB_PHYS;
    g_fb = (volatile ULONG *)MmMapIoSpace(pa, FB_BYTES, MmWriteCombined);
    if (!g_fb) return 0;                 /* STATUS_SUCCESS: que no moleste */

    FillRect(COL_X, 20, 100, 100, SlotColor());
    DrawCount(0);

    KeInitializeTimerEx(g_timer, 1 /* SynchronizationTimer */);
    KeInitializeDpc(g_dpc, (PVOID)TimerDpc, 0);
    due.QuadPart = -5000000LL;           /* 500 ms, relativo */
    KeSetTimerEx(g_timer, due, 500, g_dpc);
    return 0;
}
