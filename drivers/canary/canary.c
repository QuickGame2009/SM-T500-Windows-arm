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
__declspec(dllimport) LONG  ObReferenceObjectByName(PUNICODE_STRING Name, ULONG Attr, PVOID AccessState, ULONG Access, PVOID Type, UCHAR Mode, PVOID Ctx, PVOID *Obj);
__declspec(dllimport) ULONGLONG ObfDereferenceObject(PVOID Obj);
__declspec(dllimport) extern PVOID *IoDriverObjectType;
typedef struct _OBJ_ATTR { ULONG Length; PVOID Root; PUNICODE_STRING Name; ULONG Attr; PVOID Sd; PVOID Sqos; } OBJ_ATTR;
typedef struct _IOSB { ULONGLONG St; ULONGLONG Info; } IOSB;
__declspec(dllimport) LONG ZwOpenDirectoryObject(PVOID *Handle, ULONG Access, OBJ_ATTR *Oa);
__declspec(dllimport) LONG ZwQueryDirectoryObject(PVOID H, PVOID Buf, ULONG Len, UCHAR Single, UCHAR Restart, ULONG *Ctx, ULONG *Ret);
__declspec(dllimport) LONG ZwOpenKey(PVOID *Handle, ULONG Access, OBJ_ATTR *Oa);
__declspec(dllimport) LONG ZwEnumerateKey(PVOID H, ULONG Idx, ULONG Cls, PVOID Buf, ULONG Len, ULONG *Ret);
__declspec(dllimport) LONG ZwQueryValueKey(PVOID H, PUNICODE_STRING Name, ULONG Cls, PVOID Buf, ULONG Len, ULONG *Ret);
__declspec(dllimport) LONG ZwOpenFile(PVOID *Handle, ULONG Access, OBJ_ATTR *Oa, IOSB *Io, ULONG Share, ULONG Opts);
__declspec(dllimport) LONG ZwOpenSymbolicLinkObject(PVOID *Handle, ULONG Access, OBJ_ATTR *Oa);
__declspec(dllimport) LONG ZwQuerySymbolicLinkObject(PVOID Handle, PUNICODE_STRING Target, ULONG *Ret);

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
__declspec(dllimport) extern UCHAR MmIsAddressValid(PVOID p);
/* Busca KiBugCheckData en ntoskrnl: {0x7B, arg1, 0xC0000034, 0, 1} y devuelve arg1 (o 0). */
static ULONGLONG FindArg1(void)
{
    PVOID *head = (PVOID *)&PsLoadedModuleList;
    UCHAR *ent = (UCHAR *)head[0];
    ULONGLONG base, size, a, ok = 0;
    if (!MmIsAddressValid(ent + 0x48)) return 0;
    base = *(ULONGLONG *)(ent + 0x30); size = *(ULONG *)(ent + 0x40);
    for (a = base; a < base + size; a += 0x1000) {
        ULONGLONG *q = (ULONGLONG *)a; ULONG i;
        if (!MmIsAddressValid(q) || !MmIsAddressValid((UCHAR *)q + 0xFFF)) continue;
        for (i = 0; i + 5 <= 512; i++)
            if ((ULONG)q[i] == 0x7B && ((q[i+2] == 0xC0000034ULL) || (q[i+2] == 0xFFFFFFFFC0000034ULL)) && q[i+3] == 0 && q[i+4] == 1 && q[i+1] > 0xFFFF000000000000ULL) {
                ok = q[i+1]; return ok;
            }
    }
    return 0;
}
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
    {
        ULONGLONG a1 = FindArg1();
        char ln[96]; ULONG p = 0, k; UCHAR *u;
        DrawAddr(1280, "ARG1", a1, 0x00FFFFFF);
        if (a1 && MmIsAddressValid((PVOID)a1) && MmIsAddressValid((PVOID)(a1 + 15))) {
            ULONGLONG bp = *(ULONGLONG *)(a1 + 8); USHORT len = *(USHORT *)a1;
            DrawAddr(1310, "BUF", bp, 0x00FFFFFF);
            if (bp > 0xFFFF000000000000ULL && MmIsAddressValid((PVOID)bp) && MmIsAddressValid((PVOID)(bp + 127))) {
                USHORT *w = (USHORT *)bp; ULONG n = len / 2; if (n > 90) n = 90;
                for (k = 0; k < n; k++) ln[p++] = (w[k] >= 32 && w[k] < 127) ? (char)w[k] : '?';
                ln[p] = 0; DrawStr(20, 1345, ln, 0x0000FF00);
            }
            u = (UCHAR *)a1; p = 0;
            for (k = 0; k < 64 && MmIsAddressValid(u + k); k++) ln[p++] = (u[k] >= 32 && u[k] < 127) ? (char)u[k] : '.';
            ln[p] = 0; DrawStr(20, 1385, ln, 0x0000FFFF);
        }
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

static volatile ULONG *g_gicd, *g_gicr, *g_sd;
#define KUSER_SYSTEM_TIME_LOW    (*(volatile ULONG *)0xFFFFF78000000014ULL)
static inline ULONGLONG rd_cntp_ctl(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v)); return v; }

/* Cuenta los DEVICE_OBJECT de un driver (por nombre); 0xFFFFFFFF si el driver no existe. */
#define DRV(var, txt) static USHORT var[] = txt
DRV(nm_sdbus,   L"\\Driver\\sdbus");
DRV(nm_sdstor,  L"\\Driver\\sdstor");
DRV(nm_acpi,    L"\\Driver\\ACPI");
DRV(nm_disk,    L"\\Driver\\disk");
DRV(nm_partmgr, L"\\Driver\\partmgr");
DRV(nm_volmgr,  L"\\Driver\\volmgr");

static ULONG CountDevs(USHORT *name, USHORT nchars)
{
    UNICODE_STRING us;
    PVOID obj = 0, d;
    ULONG n = 0;
    us.Length = (USHORT)(nchars * 2); us.MaximumLength = us.Length; us.Buffer = name;
    if (ObReferenceObjectByName(&us, 0x40, 0, 0, *IoDriverObjectType, 0, 0, &obj) < 0 || !obj) return 0xFFFFFFFF;
    d = *(PVOID *)((UCHAR *)obj + 0x08);
    while (d && n < 64) { n++; d = *(PVOID *)((UCHAR *)d + 0x10); }
    ObfDereferenceObject(obj);
    return n;
}

/* Comprueba si existe un objeto por nombre (sin comprobar el tipo). */
static USHORT g_nm[128];
static ULONG g_nn;
static void NmClear(void) { g_nn = 0; }
static void NmA(const char *t) { while (*t && g_nn < 120) g_nm[g_nn++] = (USHORT)(UCHAR)*t++; }
static void NmDec(ULONG v)
{
    char b[12]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) { if (g_nn < 120) g_nm[g_nn++] = (USHORT)(UCHAR)b[--i]; }
}
static int OpenLink(PVOID *h)
{
    UNICODE_STRING us;
    OBJ_ATTR oa;
    us.Length = (USHORT)(g_nn * 2); us.MaximumLength = us.Length; us.Buffer = g_nm;
    oa.Length = sizeof(oa); oa.Root = 0; oa.Name = &us; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
    return ZwOpenSymbolicLinkObject(h, 1, &oa) >= 0;
}
static int NmExists(void)
{
    PVOID h = 0;
    if (!OpenLink(&h)) return 0;
    ZwClose(h);
    return 1;
}
static char g_tgt[80];
/* Lee el destino del enlace simbolico g_nm en g_tgt; devuelve 0 si no existe. */
static int NmTarget(void)
{
    PVOID h = 0;
    USHORT tb[80];
    UNICODE_STRING t;
    ULONG rl = 0, i, n;
    g_tgt[0] = 0;
    if (!OpenLink(&h)) return 0;
    t.Length = 0; t.MaximumLength = sizeof(tb); t.Buffer = tb;
    if (ZwQuerySymbolicLinkObject(h, &t, &rl) >= 0) {
        n = t.Length / 2; if (n > 70) n = 70;
        for (i = 0; i < n; i++) g_tgt[i] = (char)tb[i];
        g_tgt[n] = 0;
    }
    ZwClose(h);
    return 1;
}
static void PutDec(char *o, ULONG *p, ULONG v)
{
    char b[12]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) o[(*p)++] = b[--i];
}

static LONG OpenSt(void)
{
    PVOID h = 0; UNICODE_STRING us; OBJ_ATTR oa; IOSB io; LONG st;
    us.Length = (USHORT)(g_nn * 2); us.MaximumLength = us.Length; us.Buffer = g_nm;
    oa.Length = sizeof(oa); oa.Root = 0; oa.Name = &us; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
    io.St = 0; io.Info = 0;
    st = ZwOpenFile(&h, 0x100080, &oa, &io, 7, 0x20);
    if (st >= 0 && h) ZwClose(h);
    return st;
}
/* Replica la apertura del kernel: ZwOpenFile del nombre ARC y sigue la cadena de enlaces. */
static void DrawChain(void)
{
    ULONG lvl, y = 950, i, p; char ln[100], h[17], cur[80];
    NmClear(); NmA("\\ArcName\\multi(0)disk(0)rdisk(0)partition(80)");
    for (lvl = 0; lvl < 5; lvl++) {
        LONG st; int lk;
        for (i = 0; i < g_nn && i < 79; i++) cur[i] = (char)g_nm[i];
        cur[i] = 0;
        st = OpenSt();
        lk = NmTarget();
        Hex(h, (ULONGLONG)(ULONG)st, 8);
        p = 0; for (i = 0; i < 8; i++) ln[p++] = h[i];
        ln[p++] = lk ? 'L' : 'D'; ln[p++] = ' ';
        for (i = 0; cur[i] && p < 97; i++) ln[p++] = cur[i];
        ln[p] = 0;
        DrawStr(20, y, ln, st >= 0 ? 0x0000FF00 : 0x00FFFF00);
        y += 32;
        if (!lk) break;
        NmClear(); NmA(g_tgt);
    }
}

static int StExists(LONG st) { return st >= 0 || (ULONG)st != 0xC0000034UL; }
static void DrawDevGrid(void)
{
    ULONG k, g, y = 1150, p, i, cnt[2], mn[2], mx[2]; char ln[100], h[17];
    static const char *pre[2] = { "\\Device\\Harddisk0\\Partition", "\\Device\\HarddiskVolume" };
    for (g = 0; g < 2; g++) {
        cnt[g] = mn[g] = mx[g] = 0;
        for (k = 1; k <= 130; k++) {
            int e;
            NmClear(); NmA(pre[g]); NmDec(k);
            e = StExists(OpenSt());
            FillRect(20 + (k - 1) * 8, y + g * 14, 7, 7, e ? 0x0000C000 : 0x00C00000);
            if (e) { cnt[g]++; if (!mn[g]) mn[g] = k; mx[g] = k; }
        }
    }
    p = 0; ln[p++]='P'; ln[p++]='='; PutDec(ln, &p, cnt[0]); ln[p++]=' '; PutDec(ln, &p, mn[0]); ln[p++]='-'; PutDec(ln, &p, mx[0]);
    ln[p++]=' '; ln[p++]='V'; ln[p++]='='; PutDec(ln, &p, cnt[1]); ln[p++]=' '; PutDec(ln, &p, mn[1]); ln[p++]='-'; PutDec(ln, &p, mx[1]);
    ln[p] = 0; DrawStr(20, y + 34, ln, 0x00FFFFFF);
    {
        static const char *nm[4] = { "\\Device\\Harddisk0\\DR0", "\\Device\\Harddisk0\\Partition0", "\\Device\\Harddisk0\\Partition76", "\\Device\\Harddisk0\\Partition80" };
        for (g = 0; g < 4; g++) {
            LONG st; NmClear(); NmA(nm[g]); st = OpenSt();
            Hex(h, (ULONGLONG)(ULONG)st, 8);
            p = 0; for (i = 0; i < 8; i++) ln[p++] = h[i];
            ln[p++] = ' '; for (i = 0; nm[g][i] && p < 97; i++) ln[p++] = nm[g][i];
            ln[p] = 0; DrawStr(20, y + 70 + g * 32, ln, StExists(st) ? 0x0000FF00 : 0x00FFFF00);
        }
    }
}

static ULONG g_dy;           /* siguiente y libre de DirList */
static ULONG g_dtot, g_ddig, g_dhd;
/* Lista un directorio del Object Manager. filt=1: solo nombres Harddisk, Volume o Part; cuenta totales. */
static void DirList(const char *path, ULONG filt, ULONG maxlines)
{
    PVOID h = 0; UNICODE_STRING us; OBJ_ATTR oa; static UCHAR buf[512]; ULONG ctx = 0, rl, n = 0, lines = 0, p = 0, i;
    char ln[100]; USHORT pl = 0;
    static USHORT pw[64];
    for (; path[pl]; pl++) pw[pl] = (USHORT)(UCHAR)path[pl];
    us.Length = (USHORT)(pl * 2); us.MaximumLength = us.Length; us.Buffer = pw;
    oa.Length = sizeof(oa); oa.Root = 0; oa.Name = &us; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
    g_dtot = g_ddig = g_dhd = 0;
    if (ZwOpenDirectoryObject(&h, 1, &oa) < 0) { DrawStr(20, g_dy, "(sin dir)", 0x00FFFF00); g_dy += 32; return; }
    for (;;) {
        LONG st = ZwQueryDirectoryObject(h, buf, sizeof(buf), 1, n == 0, &ctx, &rl);
        USHORT nl, tl; USHORT *nb, *tb; char tok[48]; ULONG t = 0, show = 1;
        if (st < 0) break;
        nl = *(USHORT *)buf / 2; nb = *(USHORT **)(buf + 8);
        tl = *(USHORT *)(buf + 16) / 2; tb = *(USHORT **)(buf + 24);
        if (!nl) break;
        n++; g_dtot++;
        { int dig = 1; for (i = 0; i < nl; i++) if (nb[i] < '0' || nb[i] > '9') dig = 0; if (dig) g_ddig++; }
        if (nl >= 8 && nb[0]=='H' && nb[1]=='a' && nb[2]=='r' && nb[3]=='d' && nb[4]=='d') g_dhd++;
        if (filt) {
            show = (nl >= 4 && ((nb[0]=='H' && nb[1]=='a' && nb[2]=='r' && nb[3]=='d') ||
                                (nb[0]=='V' && nb[1]=='o' && nb[2]=='l' && nb[3]=='u') ||
                                (nb[0]=='P' && nb[1]=='a' && nb[2]=='r' && nb[3]=='t')));
        }
        if (!show) continue;
        for (i = 0; i < nl && t < 36; i++) tok[t++] = (nb[i] >= 32 && nb[i] < 127) ? (char)nb[i] : '?';
        tok[t++] = '('; for (i = 0; i < 3 && i < tl; i++) tok[t++] = (char)tb[i]; tok[t++] = ')'; tok[t++] = ' '; tok[t] = 0;
        if (p + t > 72) {
            ln[p] = 0; DrawStr(20, g_dy, ln, 0x0000FFFF); g_dy += 32; p = 0;
            if (++lines >= maxlines) { ZwClose(h); return; }
        }
        for (i = 0; i < t; i++) ln[p++] = tok[i];
    }
    if (p && lines < maxlines) { ln[p] = 0; DrawStr(20, g_dy, ln, 0x0000FFFF); g_dy += 32; }
    ZwClose(h);
}
static void DrawDirs(void)
{
    char ln[100]; ULONG p;
    g_dy = 1400;
    DirList("\\Device\\Harddisk0", 0, 3);
    p = 0; ln[p++]='D'; ln[p++]='e'; ln[p++]='v'; ln[p++]=' '; ln[p++]='t'; ln[p++]='='; PutDec(ln, &p, g_dtot);
    ln[p++]=' '; ln[p++]='d'; ln[p++]='='; PutDec(ln, &p, g_ddig); ln[p] = 0;
    DrawStr(20, g_dy, ln, 0x00FFFFFF); g_dy += 32;
    DirList("\\Device", 1, 14);
    p = 0; ln[p++]='D'; ln[p++]='e'; ln[p++]='v'; ln[p++]=' '; ln[p++]='t'; ln[p++]='='; PutDec(ln, &p, g_dtot);
    ln[p++]=' '; ln[p++]='d'; ln[p++]='='; PutDec(ln, &p, g_ddig); ln[p++]=' '; ln[p++]='h'; ln[p++]='='; PutDec(ln, &p, g_dhd); ln[p] = 0;
    DrawStr(20, 1860, ln, 0x00FFFFFF);
}

static USHORT g_rp[320];
static ULONG g_rn;
static void RpClear(void) { g_rn = 0; }
static void RpA(const char *t) { while (*t && g_rn < 310) g_rp[g_rn++] = (USHORT)(UCHAR)*t++; }
static void RpW(const USHORT *w, ULONG n) { ULONG i; for (i = 0; i < n && g_rn < 310; i++) g_rp[g_rn++] = w[i]; }
static LONG RpOpen(PVOID *h)
{
    UNICODE_STRING us; OBJ_ATTR oa;
    us.Length = (USHORT)(g_rn * 2); us.MaximumLength = us.Length; us.Buffer = g_rp;
    oa.Length = sizeof(oa); oa.Root = 0; oa.Name = &us; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
    return ZwOpenKey(h, 0x20019, &oa);
}
/* Lee el valor 'nm' de la clave abierta; devuelve tipo (1=SZ,4=DWORD) o 0; copia a out (ASCII) o *dw. */
static ULONG RegVal(PVOID k, const char *nm, char *out, ULONG omax, ULONG *dw)
{
    static UCHAR vb[256]; static USHORT nw[24]; UNICODE_STRING us; ULONG rl = 0, i, n = 0, ty, dl;
    while (nm[n] && n < 23) { nw[n] = (USHORT)(UCHAR)nm[n]; n++; }
    us.Length = (USHORT)(n * 2); us.MaximumLength = us.Length; us.Buffer = nw;
    if (ZwQueryValueKey(k, &us, 2, vb, sizeof(vb), &rl) < 0) return 0;
    ty = *(ULONG *)(vb + 4); dl = *(ULONG *)(vb + 8);
    if (ty == 4 && dw) { *dw = *(ULONG *)(vb + 12); return 4; }
    if ((ty == 1 || ty == 2) && out) {
        USHORT *w = (USHORT *)(vb + 12); n = dl / 2; if (n > omax - 1) n = omax - 1;
        for (i = 0; i < n && w[i]; i++) out[i] = (w[i] >= 32 && w[i] < 127) ? (char)w[i] : '?';
        out[i] = 0; return 1;
    }
    return ty;
}
/* Cuenta las instancias bajo Enum\STORAGE\<cls> y cuantas tienen valor Service. */
static void EnumClass(const char *cls, ULONG y)
{
    static UCHAR kb[512]; PVOID h = 0, k = 0; ULONG idx, nInst = 0, nSvc = 0, cf = 0, hasCf = 0, rl; char svc[40], ln[100], first[40], h8[17];
    ULONG p, i; svc[0] = 0; first[0] = 0;
    RpClear(); RpA("\\Registry\\Machine\\SYSTEM\\ControlSet001\\Enum\\STORAGE\\"); RpA(cls);
    if (RpOpen(&h) < 0) { p = 0; for (i = 0; cls[i]; i++) ln[p++] = cls[i]; ln[p++]=' '; ln[p++]='-'; ln[p++]='-'; ln[p] = 0; DrawStr(20, y, ln, 0x00FFFF00); return; }
    for (idx = 0; idx < 300; idx++) {
        ULONG nl; USHORT *nw; char tmp[40];
        if (ZwEnumerateKey(h, idx, 0, kb, sizeof(kb), &rl) < 0) break;
        nl = *(ULONG *)(kb + 12) / 2; nw = (USHORT *)(kb + 16);
        nInst++;
        RpClear(); RpA("\\Registry\\Machine\\SYSTEM\\ControlSet001\\Enum\\STORAGE\\"); RpA(cls); RpA("\\"); RpW(nw, nl);
        if (RpOpen(&k) >= 0) {
            ULONG d2 = 0;
            if (RegVal(k, "Service", tmp, sizeof(tmp), 0) == 1 && tmp[0]) { nSvc++; if (!svc[0]) { for (i = 0; tmp[i] && i < 39; i++) svc[i] = tmp[i]; svc[i] = 0; } }
            if (!hasCf && RegVal(k, "ConfigFlags", 0, 0, &d2) == 4) { cf = d2; hasCf = 1; }
            ZwClose(k);
        }
        if (idx == 0) { for (i = 0; i < nl && i < 39; i++) first[i] = (char)nw[i]; first[i] = 0; }
    }
    ZwClose(h);
    p = 0; for (i = 0; cls[i] && p < 20; i++) ln[p++] = cls[i];
    ln[p++]=' '; ln[p++]='n'; ln[p++]='='; PutDec(ln, &p, nInst);
    ln[p++]=' '; ln[p++]='s'; ln[p++]='v'; ln[p++]='c'; ln[p++]='='; PutDec(ln, &p, nSvc);
    ln[p++]=' '; for (i = 0; svc[i] && p < 60; i++) ln[p++] = svc[i];
    ln[p++]=' '; ln[p++]='c'; ln[p++]='f'; ln[p++]='='; Hex(h8, cf, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    ln[p] = 0; DrawStr(20, y, ln, 0x0000FF00);
    p = 0; for (i = 0; first[i] && p < 72; i++) ln[p++] = first[i]; ln[p] = 0; DrawStr(20, y + 28, ln, 0x00AAAAAA);
}
/* Lista los nombres de subclaves de Enum\\<sub> (sub vacio = Enum) */
static void EnumNames(const char *sub, ULONG y)
{
    static UCHAR kb[512]; PVOID h = 0; ULONG idx, rl, p = 0, i; char ln[100];
    RpClear(); RpA("\\Registry\\Machine\\SYSTEM\\ControlSet001\\Enum");
    if (sub[0]) { RpA("\\"); RpA(sub); }
    for (i = 0; sub[i] && p < 12; i++) ln[p++] = sub[i];
    if (!sub[0]) { ln[p++]='E'; ln[p++]='n'; ln[p++]='u'; ln[p++]='m'; }
    ln[p++] = ':'; ln[p++] = ' ';
    if (RpOpen(&h) < 0) { ln[p++]='-'; ln[p++]='-'; ln[p]=0; DrawStr(20, y, ln, 0x00FFFF00); return; }
    for (idx = 0; idx < 40; idx++) {
        ULONG nl; USHORT *nw;
        if (ZwEnumerateKey(h, idx, 0, kb, sizeof(kb), &rl) < 0) break;
        nl = *(ULONG *)(kb + 12) / 2; nw = (USHORT *)(kb + 16);
        if (p + nl + 1 > 72) break;
        for (i = 0; i < nl; i++) ln[p++] = (char)nw[i];
        ln[p++] = ' ';
    }
    ZwClose(h); ln[p] = 0; DrawStr(20, y, ln, 0x0000FFFF);
}
static void DrawEnum(void)
{
    EnumNames("", 1570);
    EnumNames("STORAGE", 1600);
    EnumNames("SCSI", 1630);
    EnumNames("SD", 1660);
    EnumClass("Volume", 1700);
    EnumClass("Partition", 1760);
}

/* Rejilla ARC partition(1..130) (verde existe / rojo no) y resumen con destino de los enlaces. */
static void DrawArc(void)
{
    ULONG k, cnt = 0, kmin = 0, kmax = 0, p;
    char line[96];
    for (k = 1; k <= 130; k++) {
        int e;
        NmClear(); NmA("\\ArcName\\multi(0)disk(0)rdisk(0)partition("); NmDec(k); NmA(")");
        e = NmExists();
        FillRect(20 + (k - 1) * 9, 800, 8, 8, e ? 0x0000C000 : 0x00C00000);
        if (k % 10 == 0) FillRect(20 + (k - 1) * 9, 796, 8, 2, 0x00FFFFFF);
        if (e) { cnt++; if (!kmin) kmin = k; kmax = k; }
    }
    p = 0; line[p++]='N'; line[p++]='='; PutDec(line, &p, cnt);
    line[p++]=' '; line[p++]='M'; line[p++]='I'; line[p++]='N'; line[p++]='='; PutDec(line, &p, kmin);
    line[p++]=' '; line[p++]='M'; line[p++]='A'; line[p++]='X'; line[p++]='='; PutDec(line, &p, kmax);
    line[p] = 0;
    DrawStr(20, 820, line, 0x00FFFFFF);
    NmClear(); NmA("\\ArcName\\multi(0)disk(0)rdisk(0)partition(80)");
    p = 0; line[p++]='8'; line[p++]='0'; line[p++]=' ';
    if (NmTarget()) { ULONG i; for (i = 0; g_tgt[i] && p < 90; i++) line[p++] = g_tgt[i]; } else { line[p++]='-'; line[p++]='-'; }
    line[p] = 0;
    DrawStr(20, 850, line, 0x0000FFFF);
    if (kmin) {
        NmClear(); NmA("\\ArcName\\multi(0)disk(0)rdisk(0)partition("); NmDec(kmin); NmA(")");
        p = 0; PutDec(line, &p, kmin); line[p++]=' ';
        if (NmTarget()) { ULONG i; for (i = 0; g_tgt[i] && p < 90; i++) line[p++] = g_tgt[i]; }
        line[p] = 0;
        DrawStr(20, 880, line, 0x0000FF00);
    }
    NmClear(); NmA("\\ArcName\\multi(0)disk(0)rdisk(0)");
    FillRect(20, 910, 20, 8, NmExists() ? 0x0000C000 : 0x00C00000);
    DrawChain();
    DrawDevGrid();
    DrawDirs();
    DrawEnum();
}

/* Hilo monitor.  Fila 3 (blanco): ms (dec), InterruptTime, ticks DPC (dec), CNTV_CTL, CNTV_CVAL-CNTVCT, CNTP_CTL.
   Fila 4 (naranja): ICC_RPR, ICC_HPPIR1, ICC_PMR, DAIF, ISENABLER0, ISPENDR0.
   Fila 5 (cian): SystemTime, 1a palabra SPI pendiente (indice, valor), numero de SPI pendientes, ISACTIVER0, CurrentEL. */
static void MonThread(PVOID ctx)
{
    ULONGLONG t0 = rd_cntvct(), per, freq = rd_cntfrq();
    ULONG ms, lastdraw = 0, lastobj = 0, lastgrid = 0, w, b, p, pi, pv, pc;
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
        if (ms > 3000 && ms - lastgrid >= 3000) {
            lastgrid = ms;
            DrawArc();
        }
        if (ms - lastobj >= 500) {
            lastobj = ms;
            DrawHex(0, 8, CountDevs(nm_sdbus, (USHORT)(sizeof(nm_sdbus) / 2 - 1)),   0x0000C0FF);
            DrawHex(1, 8, CountDevs(nm_sdstor, (USHORT)(sizeof(nm_sdstor) / 2 - 1)),  0x0000C0FF);
            DrawHex(2, 8, CountDevs(nm_acpi, (USHORT)(sizeof(nm_acpi) / 2 - 1)),    0x0000C0FF);
            DrawHex(3, 8, CountDevs(nm_disk, (USHORT)(sizeof(nm_disk) / 2 - 1)),    0x0000C0FF);
            DrawHex(4, 8, CountDevs(nm_partmgr, (USHORT)(sizeof(nm_partmgr) / 2 - 1)), 0x0000C0FF);
            DrawHex(5, 8, CountDevs(nm_volmgr, (USHORT)(sizeof(nm_volmgr) / 2 - 1)),  0x0000C0FF);
        }
        /* SDCC1 (eMMC) registros SDHCI.  Fila 6: PRESENT_STATE(24) HOSTCTL/POWER(28) CLOCK(2C) INT_STATUS(30) INT_EN(34) HC2(3C).
           Fila 7: CAPS(40) CAPS2(44) VERSION(FC) ADMA_ERR(54) CMD(0C) RESP0(10) */
        if (g_sd) {
            DrawHex(0, 6, g_sd[0x24 / 4], 0x00FF00FF);
            DrawHex(1, 6, g_sd[0x28 / 4], 0x00FF00FF);
            DrawHex(2, 6, g_sd[0x2C / 4], 0x00FF00FF);
            DrawHex(3, 6, g_sd[0x30 / 4], 0x00FF00FF);
            DrawHex(4, 6, g_sd[0x34 / 4], 0x00FF00FF);
            DrawHex(5, 6, g_sd[0x3C / 4], 0x00FF00FF);
            DrawHex(0, 7, g_sd[0x40 / 4], 0x0080FF80);
            DrawHex(1, 7, g_sd[0x44 / 4], 0x0080FF80);
            DrawHex(2, 7, g_sd[0xFC / 4], 0x0080FF80);
            DrawHex(3, 7, g_sd[0x54 / 4], 0x0080FF80);
            DrawHex(4, 7, g_sd[0x0C / 4], 0x0080FF80);
            DrawHex(5, 7, g_sd[0x10 / 4], 0x0080FF80);
        }
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
    DrawAddr(740, "SELF", (ULONGLONG)(PVOID)DriverEntry, 0x00FFFFFF);   /* autotest: debe mostrar canary3.sys */
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
        pa.QuadPart = 0x04744000LL;
        g_sd = (volatile ULONG *)MmMapIoSpace(pa, 0x1000, MmNonCached);
        st = PsCreateSystemThread(&th, 0x1FFFFF, 0, 0, 0, (PVOID)MonThread, 0);
        if (st >= 0 && th) ZwClose(th);
        else DrawHex(0, 3, (ULONG)st, 0x00FF0000);
    }
#endif
    return 0;
}
