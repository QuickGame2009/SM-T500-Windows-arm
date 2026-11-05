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
__declspec(dllimport) extern PVOID PsInitialSystemProcess;
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
__declspec(dllimport) LONG IoGetDeviceObjectPointer(PUNICODE_STRING Name, ULONG Access, PVOID *File, PVOID *Dev);
__declspec(dllimport) PVOID IoGetDeviceAttachmentBaseRef(PVOID Dev);
__declspec(dllimport) PVOID IoGetAttachedDeviceReference(PVOID Dev);
__declspec(dllimport) LONG ZwCreateFile(PVOID *H, ULONG Acc, OBJ_ATTR *Oa, IOSB *Io, PVOID Alloc, ULONG Attr, ULONG Share, ULONG Disp, ULONG Opts, PVOID Ea, ULONG EaLen);
__declspec(dllimport) LONG ZwWriteFile(PVOID H, PVOID Ev, PVOID Apc, PVOID Ctx, IOSB *Io, PVOID Buf, ULONG Len, LARGE_INTEGER *Off, PVOID Key);
__declspec(dllimport) LONG ZwReadFile(PVOID H, PVOID Ev, PVOID Apc, PVOID Ctx, IOSB *Io, PVOID Buf, ULONG Len, LARGE_INTEGER *Off, PVOID Key);
__declspec(dllimport) LONG ZwDeviceIoControlFile(PVOID H, PVOID Ev, PVOID Apc, PVOID Ctx, IOSB *Io, ULONG Code, PVOID In, ULONG InLen, PVOID Out, ULONG OutLen);
__declspec(dllimport) void IoInvalidateDeviceRelations(PVOID Pdo, int Type);
static volatile ULONG g_kn, g_kst, g_kms, g_fsStarted;
static volatile ULONGLONG g_kdo, g_kpdo;
static void KickPnp(void)
{
    static USHORT nm[40]; UNICODE_STRING us; PVOID fo = 0, dev = 0; const char *a = "\\Device\\Harddisk0\\DR0"; ULONG n = 0; LONG st;
    while (a[n]) { nm[n] = (USHORT)(UCHAR)a[n]; n++; }
    us.Length = (USHORT)(n * 2); us.MaximumLength = us.Length; us.Buffer = nm;
    g_kn++;
    st = IoGetDeviceObjectPointer(&us, 0x80, &fo, &dev);
    g_kst = (ULONG)st; g_kdo = (ULONGLONG)dev; g_kpdo = 0;
    if (st >= 0 && dev) {
        PVOID pdo = IoGetDeviceAttachmentBaseRef(dev);
        g_kpdo = (ULONGLONG)pdo;
        if (pdo) { IoInvalidateDeviceRelations(pdo, 0); ObfDereferenceObject(pdo); }
        if (fo) ObfDereferenceObject(fo);
    }
}
static volatile ULONGLONG g_t0v, g_perv;
static volatile ULONG g_lastPm, g_thdone, g_tPm1, g_tPm82, g_tIo, g_nIo, g_tIo0;
static volatile ULONG g_lastMs, g_lastP, g_lastV, g_lastStor, g_lastDirT;
static inline ULONGLONG rd_cntvct0(void) { ULONGLONG v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
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
static void PutDec(char *o, ULONG *p, ULONG v);
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
    DrawAddr(y, "G1", 1, 0x0000FF00); y += 30;
    DrawAddr(y, "T", g_perv ? (rd_cntvct0() - g_t0v) / g_perv : 0, 0x00FFFFFF); y += 30;
    DrawAddr(y, "L", g_lastMs, 0x00FFFFFF); y += 30;
    DrawAddr(y, "P", g_lastP, 0x00FFFFFF); y += 30;
    DrawAddr(y, "V", g_lastV, 0x00FFFFFF); y += 30;
    DrawAddr(y, "S", g_lastStor, 0x00FFFFFF); y += 30;
    DrawAddr(y, "PM", g_lastPm, 0x00FFFFFF); y += 30;
    DrawAddr(y, "PM1", g_tPm1, 0x0000FFFF); y += 30;
    DrawAddr(y, "P82", g_tPm82, 0x0000FFFF); y += 30;
    DrawAddr(y, "IO0", g_tIo0, 0x0000FFFF); y += 30;
    DrawAddr(y, "IOL", g_tIo, 0x0000FFFF); y += 30;
    DrawAddr(y, "NIO", g_nIo, 0x0000FFFF); y += 30;
    (void)fp; (void)nx; (void)i;
    {
        ULONGLONG a1 = FindArg1();
        DrawAddr(1250, "G2", 2, 0x0000FF00);
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
DRV(nm_pnpm,    L"\\Driver\\PnpManager");
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
    while (d && n < 500) { n++; d = *(PVOID *)((UCHAR *)d + 0x10); }
    ObfDereferenceObject(obj);
    if (name == nm_partmgr) g_lastPm = n;
    return n;
}


__declspec(dllimport) PVOID IoAllocateIrp(signed char StackSize, UCHAR ChargeQuota);
__declspec(dllimport) void  IoFreeIrp(PVOID Irp);
__declspec(dllimport) LONG  IofCallDriver(PVOID Dev, PVOID Irp);
__declspec(dllimport) void  KeInitializeEvent(PVOID Ev, int Type, UCHAR State);
__declspec(dllimport) LONG  KeSetEvent(PVOID Ev, LONG Inc, UCHAR Wait);
__declspec(dllimport) LONG  KeWaitForSingleObject(PVOID Obj, int Reason, signed char Mode, UCHAR Alert, LARGE_INTEGER *Tmo);
__declspec(dllimport) void  ExFreePool(PVOID P);
__declspec(dllimport) LONG IoGetDevicePropertyData(PVOID Pdo, const void *Key, ULONG Lcid, ULONG Flags, ULONG Size, PVOID Data, ULONG *Req, ULONG *Type);
static const UCHAR g_dnkey[2][20] = {
 {0xC5,0xA6,0x40,0x43,0xFA,0x93,0x06,0x47,0x97,0x2C,0x7B,0x64,0x80,0x08,0xA5,0xA7, 2,0,0,0},
 {0xC5,0xA6,0x40,0x43,0xFA,0x93,0x06,0x47,0x97,0x2C,0x7B,0x64,0x80,0x08,0xA5,0xA7, 3,0,0,0} };
static ULONGLONG g_pev[8];
static LONG PrCompl(PVOID Dev, PVOID Irp, PVOID Ctx) { (void)Dev; (void)Irp; KeSetEvent(Ctx, 0, 0); return (LONG)0xC0000016; }
/* Envia IRP_MN_QUERY_ID al dispositivo y devuelve estado; copia la 1a cadena a out. */
static LONG QueryId(PVOID dev, ULONG type, char *out, ULONG omax, ULONG *tmo)
{
    UCHAR *irp, *sl; LARGE_INTEGER to; LONG st; ULONG i;
    out[0] = 0; *tmo = 0;
    irp = (UCHAR *)IoAllocateIrp(8, 0);
    if (!irp) return (LONG)0xC000009A;
    KeInitializeEvent(g_pev, 0, 0);
    *(LONG *)(irp + 0x30) = (LONG)0xC00000BB; *(ULONGLONG *)(irp + 0x38) = 0;
    sl = *(UCHAR **)(irp + 0xB8) - 0x48;
    sl[0] = 0x1B; sl[1] = 0x13; sl[3] = 0xE0;
    *(ULONG *)(sl + 8) = type;
    *(PVOID *)(sl + 0x38) = (PVOID)PrCompl; *(PVOID *)(sl + 0x40) = g_pev;
    IofCallDriver(dev, irp);
    to.QuadPart = -30000000LL;
    if (KeWaitForSingleObject(g_pev, 0, 0, 0, &to) != 0) { *tmo = 1; return (LONG)0x102; }
    st = *(LONG *)(irp + 0x30);
    if (st >= 0) {
        USHORT *w = (USHORT *)*(ULONGLONG *)(irp + 0x38);
        if (w) { for (i = 0; i < omax - 1 && w[i]; i++) out[i] = (w[i] >= 32 && w[i] < 127) ? (char)w[i] : '?'; out[i] = 0; ExFreePool(w); }
    }
    IoFreeIrp(irp);
    return st;
}
/* Envia IRP_MN_QUERY_DEVICE_RELATIONS(BusRelations) al tope de la pila de DR0; guarda estado, n y 1er objeto. */
static void RpClear(void);
static void RpA(const char *t);
static LONG RpOpen(PVOID *h);
static ULONG RegVal(PVOID k, const char *nm, char *out, ULONG omax, ULONG *dw);
static UCHAR g_lay[0x8000];
static void DiskIoctls(void)
{
    static USHORT nm[40]; UNICODE_STRING us; OBJ_ATTR oa; IOSB io; PVOID h = 0; const char *a = "\\Device\\Harddisk0\\DR0"; ULONG n = 0, p, i, san = 0xFFFFFFFF, hv = 0;
    LONG st, s1 = 0, s2 = 0, s3 = 0, s4 = 0; ULONG att2 = 0xFFFFFFFF; ULONG att_lo = 0, att_hi = 0, pc = 0xFFFFFFFF; char ln[100], h8[17]; PVOID k = 0;
    while (a[n]) { nm[n] = (USHORT)(UCHAR)a[n]; n++; }
    us.Length = (USHORT)(n * 2); us.MaximumLength = us.Length; us.Buffer = nm;
    oa.Length = sizeof(oa); oa.Root = 0; oa.Name = &us; oa.Attr = 0x240; oa.Sd = 0; oa.Sqos = 0;
    st = ZwOpenFile(&h, 0x100081, &oa, &io, 7, 0x20);
    if (st >= 0 && h) {
        for (i = 0; i < 4; i++) ((ULONG *)g_lay)[i] = 0;
        io.St = 0; io.Info = 0;
        s1 = ZwDeviceIoControlFile(h, 0, 0, 0, &io, 0x700F0, 0, 0, g_lay, 16);
        att_lo = ((ULONG *)g_lay)[2]; att_hi = ((ULONG *)g_lay)[3];
        ZwClose(h); h = 0;
        /* intento: poner el disco ONLINE (quitar atributo OFFLINE) con IOCTL_DISK_SET_DISK_ATTRIBUTES */
        {
            PVOID h2 = 0; IOSB io2;
            if (ZwOpenFile(&h2, 0x100003, &oa, &io2, 7, 0x20) >= 0 && h2) {
                ULONG *b = (ULONG *)g_lay; ULONG q;
                for (q = 0; q < 16; q++) b[q] = 0;
                b[0] = 0x28; b[4] = 1;   /* Version, Attributes=0 (b[2..3]), Mask=1 (b[4..5]), Owner=0 */
                io2.St = 0; io2.Info = 0;
                s3 = ZwDeviceIoControlFile(h2, 0, 0, 0, &io2, 0x7C0F4, b, 0x28, 0, 0);
                for (q = 0; q < 16; q++) b[q] = 0;
                io2.St = 0; io2.Info = 0;
                s4 = ZwDeviceIoControlFile(h2, 0, 0, 0, &io2, 0x700F0, 0, 0, b, 16);
                att2 = b[2];
                ZwClose(h2);
            } else s3 = (LONG)0xDEAD0003;
        }
        st = ZwOpenFile(&h, 0x100081, &oa, &io, 7, 0x20);
        if (st < 0) h = 0;
        io.St = 0; io.Info = 0;
        io.St = 0; io.Info = 0;
        for (i = 0; i < 16; i++) ((ULONG *)g_lay)[i] = 0xFFFFFFFF;
        if (h) { s2 = ZwDeviceIoControlFile(h, 0, 0, 0, &io, 0x70050, 0, 0, g_lay, sizeof(g_lay));
        if (s2 >= 0) pc = ((ULONG *)g_lay)[1];
        ZwClose(h); }
    }
    RpClear(); RpA("\\Registry\\Machine\\SYSTEM\\ControlSet001\\Services\\partmgr\\Parameters");
    if (RpOpen(&k) >= 0) { if (RegVal(k, "SanPolicy", 0, 0, &hv) == 4) san = hv; ZwClose(k); }
    p = 0; ln[p++]='A'; ln[p++]='T'; ln[p++]='T'; ln[p++]=' ';
    if (st < 0) { ln[p++]='o'; Hex(h8, (ULONG)st, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; }
    else { Hex(h8, (ULONG)s1, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; ln[p++]=' '; Hex(h8, att_lo, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; }
    ln[p] = 0; DrawStr(640, 1830, ln, 0x00FF80FF);
    (void)att_hi;
    p = 0; ln[p++]='L'; ln[p++]='A'; ln[p++]='Y'; ln[p++]=' ';
    Hex(h8, (ULONG)s2, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; ln[p++]=' '; Hex(h8, pc, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    ln[p] = 0; DrawStr(640, 1860, ln, 0x00FF80FF);
    p = 0; ln[p++]='S'; ln[p++]='E'; ln[p++]='T'; ln[p++]=' '; Hex(h8, (ULONG)s3, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; ln[p++]=' '; Hex(h8, att2, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    ln[p] = 0; FillRect(640, 1770, 480, 26, 0x00000000); DrawStr(640, 1770, ln, 0x00FF80FF);
    p = 0; ln[p++]='S'; ln[p++]='4'; ln[p++]=' '; Hex(h8, (ULONG)s4, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    ln[p] = 0; FillRect(640, 1800, 480, 26, 0x00000000); DrawStr(640, 1800, ln, 0x00FF80FF);
    {   /* PRUEBA: forzar ONLINE a mano en memoria (atributo OFFLINE, flag 8 y bit 0x80 de las particiones) */
        static USHORT nmx[40]; UNICODE_STRING ux; PVOID fox = 0, dvx = 0, objx = 0, dd; const char *ax = "\\Device\\Harddisk0\\DR0"; ULONG qx = 0, nclr = 0, b210 = 0xFFFFFFFF, b200 = 0xFFFFFFFF, a210 = 0xFFFFFFFF, a200 = 0xFFFFFFFF;
        while (ax[qx]) { nmx[qx] = (USHORT)(UCHAR)ax[qx]; qx++; }
        ux.Length = (USHORT)(qx * 2); ux.MaximumLength = ux.Length; ux.Buffer = nmx;
        if (IoGetDeviceObjectPointer(&ux, 0x80, &fox, &dvx) >= 0 && dvx) {
            UCHAR *ex = *(UCHAR **)((UCHAR *)dvx + 0x40);
            if (ex && MmIsAddressValid(ex + 0x460)) {
                b210 = *(ULONG *)(ex + 0x210); b200 = *(ULONG *)(ex + 0x200);
                *(ULONG *)(ex + 0x210) &= ~1u; *(ULONG *)(ex + 0x200) &= ~8u;
                a210 = *(ULONG *)(ex + 0x210); a200 = *(ULONG *)(ex + 0x200);
            }
            if (fox) ObfDereferenceObject(fox);
        }
        us.Length = (USHORT)(sizeof(nm_partmgr) - 2); us.MaximumLength = us.Length; us.Buffer = nm_partmgr;
        if (ObReferenceObjectByName(&us, 0x40, 0, 0, *IoDriverObjectType, 0, 0, &objx) >= 0 && objx) {
            dd = *(PVOID *)((UCHAR *)objx + 0x08);
            while (dd && nclr < 400) {
                ULONG *fl = (ULONG *)((UCHAR *)dd + 0x30);
                if (*fl & 0x80) { *fl &= ~0x80u; }
                nclr++; dd = *(PVOID *)((UCHAR *)dd + 0x10);
            }
            ObfDereferenceObject(objx);
        }
        p = 0; ln[p++]='P'; ln[p++]='K'; ln[p++]=' '; Hex(h8, b210, 4); for (i = 0; i < 4; i++) ln[p++] = h8[i]; ln[p++]='>'; Hex(h8, a210, 4); for (i = 0; i < 4; i++) ln[p++] = h8[i];
        ln[p++]=' '; Hex(h8, b200, 4); for (i = 0; i < 4; i++) ln[p++] = h8[i]; ln[p++]='>'; Hex(h8, a200, 4); for (i = 0; i < 4; i++) ln[p++] = h8[i];
        ln[p++]=' '; PutDec(ln, &p, nclr);
        ln[p] = 0; FillRect(640, 1740, 480, 26, 0x00000000); DrawStr(640, 1740, ln, 0x0000FF00);
    }
    (void)san;
    KickPnp();
}
/* Envia IRP_MN_QUERY_DEVICE_RELATIONS(BusRelations) directamente a 'target'; devuelve el estado y el numero de objetos. */
static LONG SendQdr(PVOID target, ULONG *cnt)
{
    UCHAR *irp, *sl; LARGE_INTEGER to; LONG st;
    *cnt = 0xFFFFFFFF;
    irp = (UCHAR *)IoAllocateIrp(12, 0);
    if (!irp || !target) return (LONG)0xDEAD0001;
    KeInitializeEvent(g_pev, 0, 0);
    *(LONG *)(irp + 0x30) = (LONG)0xC00000BB; *(ULONGLONG *)(irp + 0x38) = 0;
    sl = *(UCHAR **)(irp + 0xB8) - 0x48;
    sl[0] = 0x1B; sl[1] = 0x07; sl[3] = 0xE0;
    *(ULONG *)(sl + 8) = 0;
    *(PVOID *)(sl + 0x38) = (PVOID)PrCompl; *(PVOID *)(sl + 0x40) = g_pev;
    IofCallDriver(target, irp);
    to.QuadPart = -30000000LL;
    if (KeWaitForSingleObject(g_pev, 0, 0, 0, &to) != 0) return (LONG)0xDEAD0002;
    st = *(LONG *)(irp + 0x30);
    if (st >= 0) { UCHAR *r = (UCHAR *)*(ULONGLONG *)(irp + 0x38); *cnt = r ? *(ULONG *)r : 0; }
    return st;
}
static void DrawQ(ULONG x, ULONG y, const char *tag, LONG st, ULONG cnt)
{
    char ln[40], h8[17]; ULONG p = 0, i;
    for (i = 0; tag[i]; i++) ln[p++] = tag[i];
    ln[p++] = ' '; Hex(h8, (ULONG)st, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    ln[p++] = ' '; ln[p++] = 'n'; ln[p++] = '='; if (cnt == 0xFFFFFFFF) ln[p++] = '-'; else PutDec(ln, &p, cnt);
    ln[p] = 0; DrawStr(x, y, ln, 0x00FFA040);
}
static void DiskRel(void)
{
    DiskIoctls();
    static USHORT nm[40]; UNICODE_STRING us; PVOID fo = 0, dev = 0, top; const char *a = "\\Device\\Harddisk0\\DR0"; ULONG n = 0; LONG st;
    UCHAR *irp, *sl; LARGE_INTEGER to;
    while (a[n]) { nm[n] = (USHORT)(UCHAR)a[n]; n++; }
    us.Length = (USHORT)(n * 2); us.MaximumLength = us.Length; us.Buffer = nm;
    st = IoGetDeviceObjectPointer(&us, 0x80, &fo, &dev);
    g_kst = (ULONG)st; g_kdo = 0; g_kpdo = 0;
    if (st < 0 || !dev) return;
    top = IoGetAttachedDeviceReference(dev);
    {   /* pila de dispositivos, de abajo (PDO) arriba */
        PVOID bs2 = IoGetDeviceAttachmentBaseRef(dev), cur, chain[6] = {0,0,0,0,0,0}; char ln[100]; ULONG p = 0, k, c = 0, qc;
        ln[p++]='S'; ln[p++]='T'; ln[p++]='K'; ln[p++]=' ';
        for (cur = bs2; cur && c < 6; c++) {
            chain[c] = cur;
            PVOID drv = *(PVOID *)((UCHAR *)cur + 8);
            if (drv && MmIsAddressValid((PVOID)((UCHAR *)drv + 0x48))) {
                USHORT len = *(USHORT *)((UCHAR *)drv + 0x38); USHORT *w = *(USHORT **)((UCHAR *)drv + 0x40);
                ULONG o = 0;
                if (w && MmIsAddressValid(w)) { if (len / 2 > 8) o = len / 2 - 8; for (k = o; k < len / 2 && p < 90; k++) ln[p++] = (char)w[k]; }
            }
            if (cur == dev) { ln[p++]='*'; }
            ln[p++] = ' '; ln[p++] = '<'; ln[p++] = ' ';
            cur = *(PVOID *)((UCHAR *)cur + 0x18);
        }
        ln[p] = 0; DrawStr(20, 1830, ln, 0x0000FFFF);
        if (chain[1]) { LONG qs = SendQdr(chain[1], &qc); DrawQ(700, 1360, "QF", qs, qc); }
        if (chain[0]) { LONG qs = SendQdr(chain[0], &qc); DrawQ(700, 1390, "QP", qs, qc); }
        if (bs2) ObfDereferenceObject(bs2);
    }
    irp = (UCHAR *)IoAllocateIrp(12, 0);
    if (!irp || !top) { g_kst = 0xDEAD0001; return; }
    KeInitializeEvent(g_pev, 0, 0);
    *(LONG *)(irp + 0x30) = (LONG)0xC00000BB; *(ULONGLONG *)(irp + 0x38) = 0;
    sl = *(UCHAR **)(irp + 0xB8) - 0x48;
    sl[0] = 0x1B; sl[1] = 0x07; sl[3] = 0xE0;
    *(ULONG *)(sl + 8) = 0;
    *(PVOID *)(sl + 0x38) = (PVOID)PrCompl; *(PVOID *)(sl + 0x40) = g_pev;
    IofCallDriver(top, irp);
    to.QuadPart = -30000000LL;
    if (KeWaitForSingleObject(g_pev, 0, 0, 0, &to) != 0) { g_kst = 0xDEAD0002; return; }
    g_kst = (ULONG)*(LONG *)(irp + 0x30);
    if ((LONG)g_kst >= 0) {
        UCHAR *r = (UCHAR *)*(ULONGLONG *)(irp + 0x38);
        if (r) { g_kdo = *(ULONG *)r; g_kpdo = (g_kdo > 0) ? *(ULONGLONG *)(r + 8) : 0; }
    }
    g_kn = 7;
    (void)fo;
}
/* Nombre (ultimos 8 car.) del driver del dispositivo enganchado encima de 'dev'; '-' si no hay. */
static void AttName(PVOID dev, char *o)
{
    PVOID a = *(PVOID *)((UCHAR *)dev + 0x18); ULONG k, n = 0;
    o[0] = '-'; o[1] = 0;
    if (!a || !MmIsAddressValid(a)) return;
    {
        PVOID drv = *(PVOID *)((UCHAR *)a + 8);
        if (drv && MmIsAddressValid((PVOID)((UCHAR *)drv + 0x48))) {
            USHORT len = *(USHORT *)((UCHAR *)drv + 0x38); USHORT *w = *(USHORT **)((UCHAR *)drv + 0x40); ULONG b = 0;
            if (w && MmIsAddressValid(w)) { if (len / 2 > 9) b = len / 2 - 9; for (k = b; k < len / 2 && n < 9; k++) o[n++] = (char)w[k]; o[n] = 0; }
        }
    }
}
/* Histograma de DEVICE_OBJECT.Flags de los objetos de partmgr, con driver enganchado y un ejemplo de ID. */
static void PartHist(void)
{
    PVOID obj = 0, d, ex[8]; UNICODE_STRING us; ULONG fv[8], fc[8], nv = 0, n = 0, i, j, y = 1000, att[8];
    us.Length = (USHORT)(sizeof(nm_partmgr) - 2); us.MaximumLength = us.Length; us.Buffer = nm_partmgr;
    if (ObReferenceObjectByName(&us, 0x40, 0, 0, *IoDriverObjectType, 0, 0, &obj) < 0 || !obj) { DrawStr(700, y, "pm no drv", 0x00FFFF00); return; }
    d = *(PVOID *)((UCHAR *)obj + 0x08);
    while (d && n < 200) {
        ULONG f = *(ULONG *)((UCHAR *)d + 0x30);
        for (i = 0; i < nv && fv[i] != f; i++) ;
        if (i == nv && nv < 8) { fv[nv] = f; fc[nv] = 0; ex[nv] = d; att[nv] = (*(PVOID *)((UCHAR *)d + 0x18) != 0); nv++; }
        if (i < nv) fc[i]++;
        n++; d = *(PVOID *)((UCHAR *)d + 0x10);
    }
    for (i = 0; i < nv; i++) {
        char ln[80], nm[48], h8[17]; ULONG p = 0, tmo; LONG st;
        ln[p++]='f'; Hex(h8, fv[i], 8); for (j = 0; j < 8; j++) ln[p++] = h8[j];
        ln[p++]=' '; ln[p++]='n'; ln[p++]='='; PutDec(ln, &p, fc[i]);
        ln[p++]=' '; ln[p++]='a'; ln[p++]='='; ln[p++] = att[i] ? '1' : '0';
        ln[p] = 0;
        FillRect(700, y, 480, 26, 0x00000000); DrawStr(700, y, ln, 0x00FFFF00); y += 28;
        st = QueryId(ex[i], 0, nm, sizeof(nm), &tmo);
        p = 0; ln[p++]=' ';
        if (tmo) { ln[p++]='T'; ln[p++]='M'; ln[p++]='O'; }
        else if (st < 0) { ln[p++]='s'; ln[p++]='t'; ln[p++]='='; Hex(h8, (ULONG)st, 8); for (j = 0; j < 8; j++) ln[p++] = h8[j]; }
        else { for (j = 0; nm[j] && p < 60; j++) ln[p++] = nm[j]; }
        ln[p] = 0;
        FillRect(700, y, 480, 26, 0x00000000); DrawStr(700, y, ln, 0x0000FF00); y += 28;
    }
    ObfDereferenceObject(obj);
    {   /* extension del filtro de disco (DR0) de partmgr */
        static USHORT nm2[40]; UNICODE_STRING u2; PVOID fo2 = 0, dv = 0; const char *a2 = "\\Device\\Harddisk0\\DR0"; ULONG q = 0;
        while (a2[q]) { nm2[q] = (USHORT)(UCHAR)a2[q]; q++; }
        u2.Length = (USHORT)(q * 2); u2.MaximumLength = u2.Length; u2.Buffer = nm2;
        if (IoGetDeviceObjectPointer(&u2, 0x80, &fo2, &dv) >= 0 && dv) {
            UCHAR *ex2 = *(UCHAR **)((UCHAR *)dv + 0x40); char ln[80], h8[17]; ULONG p = 0, jj;
            if (ex2 && MmIsAddressValid(ex2 + 0x460)) {
                ln[p++]='X'; ln[p++]='2'; ln[p++]='0'; ln[p++]='0'; ln[p++]=' '; Hex(h8, *(ULONG *)(ex2 + 0x200), 8); for (jj = 0; jj < 8; jj++) ln[p++] = h8[jj];
                ln[p++]=' '; ln[p++]='c'; Hex(h8, ex2[0x20C], 2); ln[p++]=h8[0]; ln[p++]=h8[1];
                ln[p++]=' '; ln[p++]='d'; Hex(h8, ex2[0x25C], 2); ln[p++]=h8[0]; ln[p++]=h8[1];
                ln[p] = 0; FillRect(700, 1300, 480, 26, 0x00000000); DrawStr(700, 1300, ln, 0x00FF80FF);
                p = 0; ln[p++]='A'; ln[p++]='T'; ln[p++]='T'; ln[p++]=' '; Hex(h8, *(ULONGLONG *)(ex2 + 0x210), 16); for (jj = 0; jj < 16; jj++) ln[p++] = h8[jj];
                ln[p++]=' '; ln[p++]='r'; Hex(h8, *(ULONG *)(ex2 + 0x458), 8); for (jj = 0; jj < 8; jj++) ln[p++] = h8[jj];
                ln[p] = 0; FillRect(700, 1330, 480, 26, 0x00000000); DrawStr(700, 1330, ln, 0x00FF80FF);
            } else DrawStr(700, 1300, "X ext?", 0x00FFFF00);
            if (fo2) ObfDereferenceObject(fo2);
        } else DrawStr(700, 1300, "X DR0 no", 0x00FFFF00);
    }
}
static UCHAR *g_dr0ext;
static ULONG g_lstn[2], g_lstown[2], g_lstg[2][4], g_lstgl[2][4];
static void WalkLst(int w)
{
    UCHAR *h, *e; ULONG n = 0, own = 0;
    if (!g_dr0ext) return;
    h = g_dr0ext + 0x278; e = *(UCHAR **)h;
    g_lstg[w][0] = g_lstg[w][1] = g_lstg[w][2] = g_lstg[w][3] = 0xEEEEEEEE;
    while (e && e != h && n < 200 && MmIsAddressValid(e) && MmIsAddressValid(e + 0x28)) {
        if (n == 0) { ULONG j; for (j = 0; j < 4; j++) g_lstg[w][j] = ((ULONG *)(e + 0x18))[j]; }
        { ULONG j; for (j = 0; j < 4; j++) g_lstgl[w][j] = ((ULONG *)(e + 0x18))[j]; }
        if (*(UCHAR **)(e + 0x10) == g_dr0ext) own++;
        n++; e = *(UCHAR **)e;
    }
    g_lstn[w] = n; g_lstown[w] = own;
}
static void LayChk(void)
{
    static USHORT nml[40]; UNICODE_STRING ul; OBJ_ATTR ol; IOSB iol; PVOID hl = 0; const char *al = "\\Device\\Harddisk0\\DR0"; ULONG q = 0, i, j, k;
    LONG sl = 0; ULONG cnt = 0, sty = 0xFFFFFFFF, zero = 0, eqd = 0, dup = 0, fi = 0xFFFF, fj = 0xFFFF, dz = 0, ntab = 0;
    char ln[100], h8[17]; ULONG p;
    while (al[q]) { nml[q] = (USHORT)(UCHAR)al[q]; q++; }
    ul.Length = (USHORT)(q * 2); ul.MaximumLength = ul.Length; ul.Buffer = nml;
    ol.Length = sizeof(ol); ol.Root = 0; ol.Name = &ul; ol.Attr = 0x240; ol.Sd = 0; ol.Sqos = 0;
    sl = ZwOpenFile(&hl, 0x100081, &ol, &iol, 7, 0x20);
    if (sl >= 0 && hl) {
        for (i = 0; i < 16; i++) ((ULONG *)g_lay)[i] = 0;
        iol.St = 0; iol.Info = 0;
        sl = ZwDeviceIoControlFile(hl, 0, 0, 0, &iol, 0x70050, 0, 0, g_lay, sizeof(g_lay));
        ZwClose(hl);
        if (sl >= 0) {
            ULONG *L = (ULONG *)g_lay; sty = L[0]; cnt = L[1];
            if (L[2] == 0 && L[3] == 0 && L[4] == 0 && L[5] == 0) dz = 1;
            if (cnt > 200) cnt = 200;
            for (i = 0; i < cnt; i++) {
                ULONG *pi_ = (ULONG *)(g_lay + 0x30 + 0x90 * i + 0x30);
                int z = (pi_[0] == 0 && pi_[1] == 0 && pi_[2] == 0 && pi_[3] == 0);
                if (z) { zero++; continue; }
                ntab++;
                if (pi_[0] == L[2] && pi_[1] == L[3] && pi_[2] == L[4] && pi_[3] == L[5]) eqd++;
                for (j = i + 1; j < cnt; j++) {
                    ULONG *pj = (ULONG *)(g_lay + 0x30 + 0x90 * j + 0x30);
                    if (pi_[0] == pj[0] && pi_[1] == pj[1] && pi_[2] == pj[2] && pi_[3] == pj[3]) { if (!dup) { fi = i; fj = j; } dup++; }
                }
            }
        }
    }
    {   /* lectura directa del disco (LBA1 cabecera y LBA2.. tabla) tal como la ve la tablet */
        static UCHAR rb[16384]; PVOID hr = 0; IOSB ior; LONG sr, sr2 = 0; LARGE_INTEGER lo; ULONG v0 = 0xEEEEEEEE, v1 = 0xEEEEEEEE, v2 = 0xEEEEEEEE, t2 = 0xEEEEEEEE, el = 0xEEEEEEEE, ne = 0xEEEEEEEE;
        ULONG used = 0, rdup = 0, da = 0xFFFF, db = 0xFFFF, a1, b1;
        ol.Attr = 0x240;
        sr = ZwOpenFile(&hr, 0x100081, &ol, &ior, 7, 0x20);
        if (sr >= 0 && hr) {
            ior.St = 0; ior.Info = 0; lo.QuadPart = 512;
            sr = ZwReadFile(hr, 0, 0, 0, &ior, rb, 4096, &lo, 0);
            if (sr >= 0) { el = *(ULONG *)(rb + 72); ne = *(ULONG *)(rb + 80); }
            ior.St = 0; ior.Info = 0; lo.QuadPart = 1024;
            sr2 = ZwReadFile(hr, 0, 0, 0, &ior, rb, 10752, &lo, 0);
            if (sr2 >= 0) {
                v0 = *(ULONG *)(rb + 16); v1 = *(ULONG *)(rb + 128 + 16); v2 = *(ULONG *)(rb + 256 + 16); t2 = *(ULONG *)(rb + 256);
                for (a1 = 0; a1 < 84; a1++) {
                    ULONG *ea = (ULONG *)(rb + 128 * a1);
                    if (!(ea[0] | ea[1] | ea[2] | ea[3])) continue;
                    used++;
                    for (b1 = a1 + 1; b1 < 84; b1++) {
                        ULONG *eb = (ULONG *)(rb + 128 * b1);
                        if (!(eb[0] | eb[1] | eb[2] | eb[3])) continue;
                        if (ea[4] == eb[4] && ea[5] == eb[5] && ea[6] == eb[6] && ea[7] == eb[7]) { if (!rdup) { da = a1; db = b1; } rdup++; }
                    }
                }
            }
            ZwClose(hr);
        }
        p = 0; ln[p++]='R'; ln[p++]=' '; Hex(h8, (ULONG)sr, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k]; ln[p++]=' '; ln[p++]='e'; PutDec(ln, &p, el); ln[p++]=' '; ln[p++]='n'; PutDec(ln, &p, ne);
        ln[p] = 0; FillRect(260, 1700, 380, 22, 0x00000000); DrawStr(260, 1700, ln, 0x0000FF80);
        p = 0; ln[p++]='R'; ln[p++]='D'; ln[p++]=' '; Hex(h8, (ULONG)sr2, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k]; ln[p++]=' '; ln[p++]='u'; PutDec(ln, &p, used); ln[p++]=' '; ln[p++]='d'; PutDec(ln, &p, rdup);
        ln[p] = 0; FillRect(260, 1724, 380, 22, 0x00000000); DrawStr(260, 1724, ln, 0x0000FF80);
        p = 0; ln[p++]='D'; ln[p++]=' '; PutDec(ln, &p, da); ln[p++]=','; PutDec(ln, &p, db);
        ln[p] = 0; FillRect(260, 1748, 380, 22, 0x00000000); DrawStr(260, 1748, ln, 0x0000FF80);
        p = 0; ln[p++]='V'; ln[p++]=' '; Hex(h8, v0, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k]; ln[p++]=' '; Hex(h8, v1, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k];
        ln[p] = 0; FillRect(260, 1772, 380, 22, 0x00000000); DrawStr(260, 1772, ln, 0x0000FF80);
        p = 0; ln[p++]='W'; ln[p++]=' '; Hex(h8, v2, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k]; ln[p++]=' '; Hex(h8, t2, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k];
        ln[p] = 0; FillRect(260, 1796, 380, 22, 0x00000000); DrawStr(260, 1796, ln, 0x0000FF80);
    }
    if (sl >= 0) {
        ULONG r;
        for (r = 0; r < 4; r++) {
            ULONG c; p = 0; ln[p++]='E'; PutDec(ln, &p, r * 3); ln[p++]=' ';
            for (c = 0; c < 3; c++) {
                ULONG idx = r * 3 + c; ULONG *pq = (ULONG *)(g_lay + 0x30 + 0x90 * idx + 0x30);
                Hex(h8, pq[0], 8); for (k = 0; k < 8; k++) ln[p++] = h8[k]; ln[p++]=' ';
            }
            ln[p] = 0; FillRect(640, 1690 + 24 * r, 540, 22, 0x00000000); DrawStr(640, 1690 + 24 * r, ln, 0x0000FFFF);
        }
    }
    p = 0; ln[p++]='Y'; ln[p++]=' '; Hex(h8, (ULONG)sl, 8); for (k = 0; k < 8; k++) ln[p++] = h8[k];
    ln[p++]=' '; ln[p++]='s'; PutDec(ln, &p, sty); ln[p++]=' '; ln[p++]='c'; PutDec(ln, &p, cnt);
    ln[p] = 0; FillRect(640, 1790, 480, 22, 0x00000000); DrawStr(640, 1790, ln, 0x0000FFFF);
    p = 0; ln[p++]='Y'; ln[p++]='z'; PutDec(ln, &p, zero); ln[p++]=' '; ln[p++]='e'; PutDec(ln, &p, eqd); ln[p++]=' '; ln[p++]='d'; PutDec(ln, &p, dup);
    ln[p++]=' '; PutDec(ln, &p, fi); ln[p++]=','; PutDec(ln, &p, fj); ln[p++]=' '; ln[p++]='D'; PutDec(ln, &p, dz);
    ln[p] = 0; FillRect(640, 1814, 480, 22, 0x00000000); DrawStr(640, 1814, ln, 0x0000FFFF);
}
static volatile ULONG g_plms[16], g_pla[16], g_plf[16], g_plr[16], g_pln;
static void ProbeThread(PVOID ctx)
{
    PVOID obj = 0, d; UNICODE_STRING us; ULONG n, y, pass;
    (void)ctx;
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    if (0) DiskRel();   /* v45: sin tocar nada, solo observar */
    {   /* volcado del registro de cambios (atributo, flags, motivo) de partmgr DR0 */
        ULONG k;
        while ((rd_cntvct() - g_t0v) / g_perv < 20000) ;
        for (k = 0; k < g_pln && k < 10; k++) {
            char ln[60], h8[17]; ULONG p = 0, j;
            ln[p++]='L'; ln[p++]=' '; PutDec(ln, &p, g_plms[k]); ln[p++]=' '; ln[p++]='a'; Hex(h8, g_pla[k], 4); for (j = 0; j < 4; j++) ln[p++] = h8[j];
            ln[p++]=' '; ln[p++]='f'; Hex(h8, g_plf[k], 4); for (j = 0; j < 4; j++) ln[p++] = h8[j];
            ln[p++]=' '; ln[p++]='r'; Hex(h8, g_plr[k], 4); for (j = 0; j < 4; j++) ln[p++] = h8[j];
            ln[p] = 0; FillRect(640, 1420 + 24 * k, 480, 22, 0x00000000); DrawStr(640, 1420 + 24 * k, ln, 0x0000FFFF);
        }
        if (!g_pln) DrawStr(640, 1420, "L sin DR0", 0x00FFFF00);
        WalkLst(1);
        LayChk();
    }
    us.Length = (USHORT)(sizeof(nm_acpi) - 2); us.MaximumLength = us.Length; us.Buffer = nm_acpi;
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) { while ((rd_cntvct() - g_t0v) / g_perv < 22000) ; PartHist(); return; }
        if (pass == 1) { us.Length = (USHORT)(sizeof(nm_pnpm) - 2); us.MaximumLength = us.Length; us.Buffer = nm_pnpm; }
        if (ObReferenceObjectByName(&us, 0x40, 0, 0, *IoDriverObjectType, 0, 0, &obj) < 0 || !obj) { DrawStr(700, 1000, "drv no", 0x00FFFF00); return; }
        d = *(PVOID *)((UCHAR *)obj + 0x08); n = 0; y = 1000;
        while (d && n < 14) {
            char nm[48], ln[80]; ULONG p = 0, tmo, i; LONG st; char h8[17];
            if (pass == 0) {
                ln[p++] = (char)('A' + n); ln[p++] = ' '; ln[p++] = '.'; ln[p++] = '.'; ln[p] = 0;
                FillRect(700, y, 480, 26, 0x00000000);
                DrawStr(700, y, ln, 0x00FFFF00);
                st = QueryId(d, 1, nm, sizeof(nm), &tmo);
                p = 0; ln[p++] = (char)('A' + n); ln[p++] = ' ';
                if (tmo) { ln[p++]='T'; ln[p++]='M'; ln[p++]='O'; }
                else if (st < 0) { ln[p++]='s'; ln[p++]='t'; ln[p++]='='; Hex(h8, (ULONG)st, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; }
                else { for (i = 0; nm[i] && p < 60; i++) ln[p++] = nm[i]; }
                ln[p] = 0;
                FillRect(700, y, 480, 26, 0x00000000);
                DrawStr(700, y, ln, tmo ? 0x00FF0000 : 0x0000FF00);
            } else {
                st = QueryId(d, 0, nm, sizeof(nm), &tmo);
                ln[p++] = (char)('a' + n); ln[p++] = ' ';
                if (tmo) { ln[p++]='T'; ln[p++]='M'; ln[p++]='O'; }
                else if (st < 0) { ln[p++]='s'; ln[p++]='t'; ln[p++]='='; Hex(h8, (ULONG)st, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; }
                else { for (i = 0; nm[i] && p < 60; i++) ln[p++] = nm[i]; }
                { char an[16]; ln[p++] = ' '; ln[p++] = '>'; AttName(d, an); for (i = 0; an[i] && p < 76; i++) ln[p++] = an[i]; }
                ln[p] = 0;
                FillRect(700, y, 480, 26, 0x00000000);
                DrawStr(700, y, ln, 0x0000FFFF);
            }
            y += 28; n++;
            d = *(PVOID *)((UCHAR *)d + 0x10);
        }
        ObfDereferenceObject(obj);
    }
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
    g_lastP = cnt[0]; g_lastV = cnt[1];
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
static void EnumNamesFrom(const char *sub, ULONG y, ULONG skip)
{
    static UCHAR kb[512]; PVOID h = 0; ULONG idx, rl, p = 0, i; char ln[100];
    RpClear(); RpA("\\Registry\\Machine\\SYSTEM\\ControlSet001\\Enum");
    if (sub[0]) { RpA("\\"); RpA(sub); }
    for (i = 0; sub[i] && p < 12; i++) ln[p++] = sub[i];
    if (!sub[0]) { ln[p++]='E'; ln[p++]='n'; ln[p++]='u'; ln[p++]='m'; }
    ln[p++] = ':'; ln[p++] = ' ';
    if (RpOpen(&h) < 0) { if (sub[0]=='S' && sub[1]=='T') g_lastStor = 0; ln[p++]='-'; ln[p++]='-'; ln[p]=0; DrawStr(20, y, ln, 0x00FFFF00); return; }
    if (sub[0]=='S' && sub[1]=='T') g_lastStor = 1;
    for (idx = skip; idx < 40; idx++) {
        ULONG nl; USHORT *nw;
        if (ZwEnumerateKey(h, idx, 0, kb, sizeof(kb), &rl) < 0) break;
        nl = *(ULONG *)(kb + 12) / 2; nw = (USHORT *)(kb + 16);
        if (p + nl + 1 > 72) break;
        for (i = 0; i < nl; i++) ln[p++] = (char)nw[i];
        ln[p++] = ' ';
    }
    ZwClose(h); ln[p] = 0; DrawStr(20, y, ln, 0x0000FFFF);
}
static void EnumNames(const char *sub, ULONG y) { EnumNamesFrom(sub, y, 0); }
static void DiskInst(ULONG y)
{
    static UCHAR kb[512]; PVOID h = 0, k = 0; ULONG rl, p = 0, i, cf = 0; char ln[100], svc[30], cl[10], h8[17];
    svc[0] = 0; cl[0] = 0;
    RpClear(); RpA("\\Registry\\Machine\\SYSTEM\\ControlSet001\\Enum\\SD\\DISK&Samsung&DP68MB&0.2");
    if (RpOpen(&h) < 0) { DrawStr(20, y, "DI --", 0x00FFFF00); return; }
    if (ZwEnumerateKey(h, 0, 0, kb, sizeof(kb), &rl) >= 0) {
        ULONG nl = *(ULONG *)(kb + 12) / 2; USHORT *nw = (USHORT *)(kb + 16);
        RpA("\\"); RpW(nw, nl);
        if (RpOpen(&k) >= 0) {
            ULONG d2 = 0; char t2[30];
            if (RegVal(k, "Service", t2, 30, 0) == 1) for (i = 0; t2[i] && i < 29; i++) svc[i] = t2[i];
            svc[i] = 0;
            if (RegVal(k, "ConfigFlags", 0, 0, &d2) == 4) cf = d2;
            if (RegVal(k, "Class", t2, 30, 0) == 1) for (i = 0; t2[i] && i < 9; i++) cl[i] = t2[i];
            ZwClose(k);
        }
    }
    ZwClose(h);
    ln[p++]='D'; ln[p++]='I'; ln[p++]=' '; ln[p++]='s'; ln[p++]='v'; ln[p++]='c'; ln[p++]='=';
    for (i = 0; svc[i]; i++) ln[p++] = svc[i];
    ln[p++]=' '; ln[p++]='c'; ln[p++]='l'; ln[p++]='='; for (i = 0; cl[i]; i++) ln[p++] = cl[i];
    ln[p++]=' '; ln[p++]='c'; ln[p++]='f'; ln[p++]='='; Hex(h8, cf, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    ln[p] = 0; DrawStr(20, y, ln, 0x0000FF00);
}
static void DrawEnum(void)
{
    DrawAddr(1000, "KN", g_kn, 0x0000FF00);
    DrawAddr(1030, "KS", g_kst, 0x00FFFF00);
    DrawAddr(1060, "KDO", g_kdo, 0x00FFFFFF);
    DrawAddr(1090, "KPD", g_kpdo, 0x00FFFFFF);
    EnumNames("ROOT", 1900);
    EnumNamesFrom("ROOT", 1930, 6);
    EnumNamesFrom("ROOT\\VOLMGR", 1960, 0);
    EnumNames("", 1570);
    EnumNames("STORAGE", 1600);
    EnumNames("SCSI", 1630);
    EnumNames("SD", 1660);
    EnumClass("Volume", 1700);
    EnumClass("Partition", 1760);
}

typedef struct { ULONGLONG base; ULONG size; char nm[10]; } MODE;
static MODE g_md[300]; static ULONG g_nmd;
static void LoadMods(void)
{
    LIST_ENTRY *h = &PsLoadedModuleList, *e = h->Flink; ULONG n = 0, i;
    g_nmd = 0;
    while (e && e != h && n++ < 299) {
        UCHAR *m = (UCHAR *)e; UNICODE_STRING *u = (UNICODE_STRING *)(m + 0x58); MODE *d = &g_md[g_nmd++];
        d->base = *(ULONGLONG *)(m + 0x30); d->size = *(ULONG *)(m + 0x40);
        for (i = 0; i < 9; i++) d->nm[i] = ' ';
        d->nm[9] = 0;
        if (u->Buffer) for (i = 0; i < 9 && i < (ULONG)(u->Length / 2); i++) d->nm[i] = (char)u->Buffer[i];
        e = e->Flink;
    }
}
static int ModOf(ULONGLONG v, ULONG *idx)
{
    ULONG i;
    for (i = 0; i < g_nmd; i++) if (v >= g_md[i].base && v < g_md[i].base + g_md[i].size) { *idx = i; return 1; }
    return 0;
}
static int Interesting(const char *n)
{
    static const char *k[] = { "sdbus", "sdstor", "storport", "disk", "partmgr", "volmgr", "mountmgr", "ACPI", "CLASSPNP", "fvevol", "volsnap", "fltmgr", "Ntfs", "ntfs", 0 };
    ULONG i, j;
    for (i = 0; k[i]; i++) {
        for (j = 0; k[i][j] && n[j] == k[i][j]; j++) ;
        if (!k[i][j]) return 1;
    }
    return 0;
}

/* Prueba del sistema de archivos: abre ntoskrnl por \SystemRoot y por el volumen 80, intenta crear/escribir
   \SystemRoot\canary-w.txt y lee el sector 0 del volumen (firma NTFS).  Resultados en la esquina superior derecha. */
static LONG FsOpen(const char *path, ULONG acc, ULONG disp, PVOID *outh)
{
    static USHORT nmf[96]; UNICODE_STRING uf; OBJ_ATTR of; IOSB iof; ULONG q = 0; PVOID h = 0; LONG s;
    while (path[q] && q < 94) { nmf[q] = (USHORT)(UCHAR)path[q]; q++; }
    uf.Length = (USHORT)(q * 2); uf.MaximumLength = uf.Length; uf.Buffer = nmf;
    of.Length = sizeof(of); of.Root = 0; of.Name = &uf; of.Attr = 0x240; of.Sd = 0; of.Sqos = 0;
    iof.St = 0; iof.Info = 0;
    s = ZwCreateFile(&h, acc, &of, &iof, 0, 0x80, 7, disp, 0x20, 0, 0);
    if (s >= 0 && outh) *outh = h; else if (s >= 0 && h) ZwClose(h);
    return s;
}
static void FsLine(ULONG row, const char *tag, LONG s1, LONG s2, ULONG extra, int hasextra)
{
    char ln[40], h8[17]; ULONG p = 0, i;
    for (i = 0; tag[i] && p < 6; i++) ln[p++] = tag[i];
    Hex(h8, (ULONG)s1, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i];
    if (s2 != 0x7FFFFFFF) { ln[p++] = ' '; Hex(h8, (ULONG)s2, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; }
    if (hasextra) { ln[p++] = ' '; Hex(h8, extra, 8); for (i = 0; i < 8; i++) ln[p++] = h8[i]; }
    ln[p] = 0;
    DrawStr(640, 110 + 30 * row, ln, 0x0000FFFF);
}

/* Lista de procesos: cuenta y los 4 ultimos nombres (smss/csrss/wininit... indican que el modo usuario arranco). */
static void ProcScan(void)
{
    ULONGLONG ep = (ULONGLONG)PsInitialSystemProcess, ho, lh = 0, e, nmo = 0, o;
    ULONG cnt = 0, i, k, p; char names[4][8], ln[48];
    if (!ep || !MmIsAddressValid((PVOID)(ep + 0x700))) { DrawStr(640, 230, "PR noep", 0x00FFFF00); return; }
    for (o = 0x100; o < 0x700; o++) {
        UCHAR *s = (UCHAR *)(ep + o);
        if (s[0]=='S' && s[1]=='y' && s[2]=='s' && s[3]=='t' && s[4]=='e' && s[5]=='m' && s[6]==0) { nmo = o; break; }
    }
    if (!nmo) { DrawStr(640, 230, "PR noname", 0x00FFFF00); return; }
    /* ActiveProcessLinks: lista circular cuyo primer elemento es un proceso con nombre ASCII (no la lista de hilos, que esta mas arriba) */
    for (ho = 0x100; ho <= nmo && !lh; ho += 8) {
        ULONGLONG hd = ep + ho, f, b2; UCHAR *nm;
        f = *(ULONGLONG *)hd; b2 = *(ULONGLONG *)(hd + 8);
        if (f == hd || f < 0xFFFF000000000000ULL || b2 < 0xFFFF000000000000ULL) continue;
        if (!MmIsAddressValid((PVOID)(f + 8)) || !MmIsAddressValid((PVOID)b2)) continue;
        if (*(ULONGLONG *)(f + 8) != hd || *(ULONGLONG *)b2 != hd) continue;
        if (!MmIsAddressValid((PVOID)(f - ho + nmo + 16))) continue;
        nm = (UCHAR *)(f - ho + nmo);
        if (nm[0] < 'A' || nm[0] > 'z' || nm[1] < 32 || nm[1] > 126) continue;
        lh = hd;
    }
    if (!lh) { DrawStr(640, 230, "PR nolist", 0x00FFFF00); return; }
    ho = lh - ep;
    for (k = 0; k < 4; k++) for (i = 0; i < 7; i++) names[k][i] = ' ';
    e = *(ULONGLONG *)lh;
    while (e != lh && cnt < 300 && e > 0xFFFF000000000000ULL && MmIsAddressValid((PVOID)e) && MmIsAddressValid((PVOID)(e - ho + nmo + 16))) {
        UCHAR *pr = (UCHAR *)(e - ho + nmo); int z = 0;
        for (k = 0; k < 3; k++) for (i = 0; i < 7; i++) names[k][i] = names[k + 1][i];
        for (i = 0; i < 7; i++) { if (pr[i] == 0) z = 1; names[3][i] = (!z && pr[i] >= 32 && pr[i] < 127) ? (char)pr[i] : ' '; }
        cnt++; e = *(ULONGLONG *)e;
    }
    p = 0; ln[p++] = 'P'; ln[p++] = 'R'; PutDec(ln, &p, cnt); ln[p++] = ' ';
    for (k = 0; k < 4; k++) { for (i = 0; i < 7; i++) ln[p++] = names[k][i]; ln[p++] = ' '; }
    ln[p] = 0;
    DrawStr(640, 230, ln, 0x0000FF80);
}


/* ---- Registro en fichero: \SystemRoot\canary-log.txt (modulos cargados y, por cada hilo del proceso System, las direcciones de
   la pila que caen dentro de un modulo: modulo+RVA).  Sirve para ver donde esta parada la inicializacion del kernel. ---- */
static char g_lg[90000]; static ULONG g_lgn; static ULONGLONG g_kso = 0x58;
static void LgC(char c) { if (g_lgn < sizeof(g_lg) - 1) g_lg[g_lgn++] = c; }
static void LgS(const char *s) { while (*s) LgC(*s++); }
static void LgH(ULONGLONG v, int d) { char h[17]; ULONG i; Hex(h, v, d); for (i = 0; h[i]; i++) LgC(h[i]); }
static void WriteLog(ULONG n)
{
    ULONGLONG ep = (ULONGLONG)PsInitialSystemProcess, lh = 0, first, e, off = 0, o, ho;
    ULONG i, tot = 0; PVOID h = 0; LONG c; IOSB io; LARGE_INTEGER lo;
    g_lgn = 0;
    LgS("canary log n="); LgH(n, 4); LgS("\r\n");
    if (!ep || !MmIsAddressValid((PVOID)(ep + 0x700))) { LgS("sin proceso System\r\n"); goto fin; }
    LoadMods();
    for (i = 0; i < g_nmd; i++) { LgS("M "); { ULONG k; for (k = 0; k < 9; k++) LgC(g_md[i].nm[k]); } LgC(' '); LgH(g_md[i].base, 16); LgC(' '); LgH(g_md[i].size, 8); LgS("\r\n"); }
    for (ho = 0x18; ho <= 0x80 && !lh; ho += 8) {
        ULONGLONG hd = ep + ho, f, b;
        f = *(ULONGLONG *)hd; b = *(ULONGLONG *)(hd + 8);
        if (f == hd || f < 0xFFFF000000000000ULL || b < 0xFFFF000000000000ULL) continue;
        if (!MmIsAddressValid((PVOID)(f + 8)) || !MmIsAddressValid((PVOID)b)) continue;
        if (*(ULONGLONG *)(f + 8) == hd && *(ULONGLONG *)b == hd) lh = hd;
    }
    if (!lh) { LgS("sin lista de hilos\r\n"); goto fin; }
    first = *(ULONGLONG *)lh;
    for (o = 0x100; o <= 0x600 && !off; o += 8) {
        ULONGLONG th = first - o, ini, lim;
        if (!MmIsAddressValid((PVOID)(th + 0x60))) continue;
        ini = *(ULONGLONG *)(th + 0x28); lim = *(ULONGLONG *)(th + 0x30);
        if (ini > 0xFFFF000000000000ULL && lim > 0xFFFF000000000000ULL && ini > lim && ini - lim >= 0x1000 && ini - lim <= 0x80000) off = o;
    }
    if (!off) { LgS("sin offset de hilo\r\n"); goto fin; }
    LgS("thoff "); LgH(off, 4); LgS("\r\n");
    {   /* KernelStack (SP guardado) no esta en +0x58 en ARM64: se busca el desplazamiento cuyo valor cae dentro de la pila en mas hilos */
        ULONG cn[32], bi = 0, bc = 0, nt = 0, oi; ULONGLONG ee = first;
        for (oi = 0; oi < 32; oi++) cn[oi] = 0;
        while (ee != lh && nt < 80 && MmIsAddressValid((PVOID)ee)) {
            ULONGLONG th = ee - off, ini = *(ULONGLONG *)(th + 0x28), lim = *(ULONGLONG *)(th + 0x30);
            if (!MmIsAddressValid((PVOID)(th + 0x120))) break;
            nt++;
            for (oi = 0; oi < 32; oi++) {
                ULONGLONG v = *(ULONGLONG *)(th + 0x38 + 8 * oi);
                if (v > lim + 0x100 && v < ini - 0x40 && !(v & 7) && ini - v < 0x6000) cn[oi]++;
            }
            ee = *(ULONGLONG *)ee;
        }
        for (oi = 0; oi < 32; oi++) { if (cn[oi] > bc) { bc = cn[oi]; bi = oi; } }
        g_kso = 0x38 + 8 * bi;
        LgS("kso "); LgH(g_kso, 4); LgS(" n="); LgH(bc, 4); LgS("/"); LgH(nt, 4); LgS(" cand:");
        for (oi = 0; oi < 32; oi++) if (cn[oi]) { LgC(' '); LgH(0x38 + 8 * oi, 3); LgC('='); LgH(cn[oi], 2); }
        LgS("\r\n");
    }
    e = first;
    while (e != lh && tot < 300 && MmIsAddressValid((PVOID)e)) {
        ULONGLONG th = e - off, ks, ini, lim, q, end; ULONG nf = 0, idx;
        if (!MmIsAddressValid((PVOID)(th + 0x60))) break;
        tot++;
        ks = *(ULONGLONG *)(th + g_kso); ini = *(ULONGLONG *)(th + 0x28); lim = *(ULONGLONG *)(th + 0x30);
        LgS("T "); LgH(th & 0xFFFFFFFFFFULL, 10); LgS(" ks="); LgH(ks & 0xFFFFFF, 6); LgS(" :");
        {   /* toda la pila (de la cima hacia abajo): solo direcciones en codigo de modulos (ntoskrnl: .text..INIT), sin repetir la anterior */
            ULONGLONG lo2 = lim, lastpg = 0, prev = 0; int okpg = 0;
            for (q = (ini - 8) & ~7ULL; q >= lo2 && nf < 70; q -= 8) {
                ULONGLONG v;
                if ((q & ~0xFFFULL) != lastpg) { lastpg = q & ~0xFFFULL; okpg = MmIsAddressValid((PVOID)q) ? 1 : 0; }
                if (!okpg) continue;
                v = *(ULONGLONG *)q;
                if (v < 0xFFFF000000000000ULL || v == prev) continue;
                if (ModOf(v, &idx)) {
                    ULONGLONG rva = v - g_md[idx].base; ULONG k;
                    if (idx == 0 && (rva < 0x200000 || rva >= 0xA8C000)) continue;
                    prev = v; LgC(' ');
                    for (k = 0; k < 9 && g_md[idx].nm[k] != ' '; k++) LgC(g_md[idx].nm[k]);
                    LgC('+'); LgH(rva, 6); nf++;
                }
            }
        }
        LgS("\r\n");
        e = *(ULONGLONG *)e;
    }
    LgS("hilos "); LgH(tot, 4); LgS("\r\n");
fin:
    c = FsOpen("\\SystemRoot\\canary-log.txt", 0x40100000, 5, &h);
    if (c >= 0 && h) { io.St = 0; io.Info = 0; lo.QuadPart = 0; ZwWriteFile(h, 0, 0, 0, &io, g_lg, g_lgn, &lo, 0); ZwClose(h); }
}

static void FsThread(PVOID ctx)
{
    static UCHAR sec[4096]; ULONG n = 0;
    (void)ctx;
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    for (;;) {
        LONG a, b, c, d, w = 0x7FFFFFFF; PVOID h = 0; IOSB io; LARGE_INTEGER lo; ULONG sig = 0;
        while ((rd_cntvct() - g_t0v) / g_perv < 6000UL + 3000UL * n) ;
        n++;
        a = FsOpen("\\SystemRoot\\System32\\ntoskrnl.exe", 0x120089, 1, 0);
        FsLine(0, "SR ", a, 0x7FFFFFFF, n, 1);
        b = FsOpen("\\Device\\HarddiskVolume80\\Windows\\System32\\ntoskrnl.exe", 0x120089, 1, 0);
        FsLine(1, "V80 ", b, 0x7FFFFFFF, 0, 0);
        c = FsOpen("\\SystemRoot\\canary-w.txt", 0x40100000, 5, &h);
        if (c >= 0 && h) {
            static char msg[8] = { 'O', 'K', 13, 10, 0, 0, 0, 0 };
            io.St = 0; io.Info = 0; lo.QuadPart = 0;
            w = ZwWriteFile(h, 0, 0, 0, &io, msg, 4, &lo, 0);
            ZwClose(h);
        }
        FsLine(2, "WR ", c, w, 0, 0);
        h = 0;
        d = FsOpen("\\Device\\HarddiskVolume80", 0x120089, 1, &h);
        if (d >= 0 && h) {
            io.St = 0; io.Info = 0; lo.QuadPart = 0;
            d = ZwReadFile(h, 0, 0, 0, &io, sec, 4096, &lo, 0);
            if (d >= 0) sig = *(ULONG *)(sec + 3);
            ZwClose(h);
        }
        FsLine(3, "RAW ", d, 0x7FFFFFFF, sig, 1);
        ProcScan();
        if (n == 8 || n == 30 || n == 80) WriteLog(n);
    }
}

/* Recorre los hilos del proceso System, rastrea en cada pila direcciones de retorno en modulos
   (sin suponer el formato de los marcos) y muestra los hilos con modulos de almacenamiento. */
static void DrawThreads(void)
{
    ULONGLONG ep = (ULONGLONG)PsInitialSystemProcess, lh, first, e, t, off = 0, o;
    ULONG total = 0, inter = 0, shown = 0, y = 1060, p, i, nh, hitmod[8], k, any;
    ULONGLONG hitoff[8];
    char ln[100], h[17];
    if (!ep || !MmIsAddressValid((PVOID)(ep + 0x38))) { DrawStr(20, 1128, "TH noep", 0x00FFFF00); return; }
    LoadMods();
    {
        ULONGLONG ho, found = 0;
        for (ho = 0x18; ho <= 0x80 && !found; ho += 8) {
            ULONGLONG hd = ep + ho, f, b;
            if (!MmIsAddressValid((PVOID)(hd + 8))) continue;
            f = *(ULONGLONG *)hd; b = *(ULONGLONG *)(hd + 8);
            if (f == hd || f < 0xFFFF000000000000ULL || b < 0xFFFF000000000000ULL) continue;
            if (!MmIsAddressValid((PVOID)(f + 8)) || !MmIsAddressValid((PVOID)b)) continue;
            if (*(ULONGLONG *)(f + 8) == hd && *(ULONGLONG *)b == hd) found = ho;
        }
        if (!found) { DrawAddr(1000, "EP", ep, 0x00FFFF00); DrawStr(20, 1128, "TH nohead", 0x00FFFF00); return; }
        lh = ep + found; first = *(ULONGLONG *)lh;
        DrawAddr(1000, "HO", found, 0x00AAAAAA);
    }
    for (o = 0x100; o <= 0x600 && !off; o += 8) {
        ULONGLONG th = first - o, ini, lim;
        if (!MmIsAddressValid((PVOID)(th + 0x60))) continue;
        ini = *(ULONGLONG *)(th + 0x28); lim = *(ULONGLONG *)(th + 0x30);
        if (ini > 0xFFFF000000000000ULL && lim > 0xFFFF000000000000ULL && ini > lim && ini - lim >= 0x1000 && ini - lim <= 0x80000) off = o;
    }
    if (!off) {
        ULONGLONG t0 = first - 0x2F8;
        DrawAddr(1030, "F", first, 0x00FFFF00);
        if (MmIsAddressValid((PVOID)(t0 + 0x60))) {
            DrawAddr(1060, "+28", *(ULONGLONG *)(t0 + 0x28), 0x00FFFF00);
            DrawAddr(1090, "+30", *(ULONGLONG *)(t0 + 0x30), 0x00FFFF00);
        }
        DrawStr(20, 1128, "TH nooff", 0x00FFFF00); return;
    }
    DrawAddr(1030, "TO", off, 0x00AAAAAA);
    e = first;
    while (e != lh && total < 400) {
        ULONGLONG ks, ini, lim, q, end;
        t = e - off;
        if (!MmIsAddressValid((PVOID)(t + 0x60))) break;
        total++;
        ks = *(ULONGLONG *)(t + 0x58); ini = *(ULONGLONG *)(t + 0x28); lim = *(ULONGLONG *)(t + 0x30);
        nh = 0; any = 0;
        if (ks > lim && ks < ini && ini - ks <= 0x40000) {
            end = ks + 0x3000; if (end > ini) end = ini;
            ks &= ~7ULL;
            for (q = ks; q < end; q += 8) {
                ULONGLONG v, ue, csl, dev, drv, dev2, drv2, nb2;
                if (((q & 0xFFF) == 0 || q == ks) && !MmIsAddressValid((PVOID)q)) break;
                v = *(ULONGLONG *)q;
                if (v < 0xFFFF000000000000ULL || (v & 7)) continue;
                if (!MmIsAddressValid((PVOID)v) || !MmIsAddressValid((PVOID)(v + 0xC8))) continue;
                if (*(USHORT *)v != 6) continue;
                { USHORT sz = *(USHORT *)(v + 2); UCHAR sc = *(UCHAR *)(v + 0x42), cl = *(UCHAR *)(v + 0x43);
                  if (sz < 0xB8 || sz > 0x600 || sc == 0 || sc > 20 || cl == 0 || cl > sc + 1) continue; }
                ue = *(ULONGLONG *)(v + 0x50);
                if (ue < lim || ue >= ini) continue;           /* evento en la pila del propio hilo: espera sincrona */
                csl = *(ULONGLONG *)(v + 0xB8);
                if (csl < 0xFFFF000000000000ULL || !MmIsAddressValid((PVOID)csl) || !MmIsAddressValid((PVOID)(csl + 0x48))) continue;
                inter++; any = 1;
                if (shown < 3) {
                    char dn[16], un[16]; ULONG a2;
                    dev = *(ULONGLONG *)(csl + 0x28); dev2 = *(ULONGLONG *)(csl - 0x48 + 0x28);
                    for (a2 = 0; a2 < 15; a2++) { dn[a2] = '-'; un[a2] = '-'; } dn[15] = 0; un[15] = 0;
                    if (dev > 0xFFFF000000000000ULL && MmIsAddressValid((PVOID)(dev + 0x10))) {
                        drv = *(ULONGLONG *)(dev + 8);
                        if (drv > 0xFFFF000000000000ULL && MmIsAddressValid((PVOID)(drv + 0x48))) {
                            USHORT nl2 = *(USHORT *)(drv + 0x38) / 2; USHORT *nb = *(USHORT **)(drv + 0x40);
                            if (nb && MmIsAddressValid(nb) && MmIsAddressValid(nb + 15)) for (a2 = 0; a2 < 14 && a2 < nl2; a2++) dn[a2] = (char)nb[a2];
                        }
                    }
                    if (dev2 > 0xFFFF000000000000ULL && MmIsAddressValid((PVOID)(dev2 + 0x10))) {
                        drv2 = *(ULONGLONG *)(dev2 + 8);
                        if (drv2 > 0xFFFF000000000000ULL && MmIsAddressValid((PVOID)(drv2 + 0x48))) {
                            USHORT nl2 = *(USHORT *)(drv2 + 0x38) / 2; USHORT *nb = *(USHORT **)(drv2 + 0x40);
                            if (nb && MmIsAddressValid(nb) && MmIsAddressValid(nb + 15)) for (a2 = 0; a2 < 14 && a2 < nl2; a2++) un[a2] = (char)nb[a2];
                        }
                    }
                    (void)nb2;
                    p = 0; Hex(h, t >> 4, 16); for (i = 0; i < 4; i++) ln[p++] = h[12 + i]; ln[p++] = ' ';
                    Hex(h, (ULONGLONG)*(UCHAR *)csl, 2); ln[p++] = h[0]; ln[p++] = h[1]; ln[p++] = '/';
                    Hex(h, (ULONGLONG)*(UCHAR *)(csl + 1), 2); ln[p++] = h[0]; ln[p++] = h[1]; ln[p++] = ' ';
                    for (i = 0; dn[i] && p < 50; i++) ln[p++] = dn[i];
                    ln[p++] = ' '; ln[p++] = '<'; ln[p++] = ' ';
                    for (i = 0; un[i] && p < 80; i++) ln[p++] = un[i];
                    ln[p] = 0; DrawStr(20, y, ln, 0x0000FFFF); y += 32; shown++;
                }
                break;
            }
        }
        if (!MmIsAddressValid((PVOID)e)) break;
        e = *(ULONGLONG *)e;
    }
    p = 0; ln[p++]='T'; ln[p++]='H'; ln[p++]=' '; ln[p++]='n'; ln[p++]='='; PutDec(ln, &p, total);
    ln[p++]=' '; ln[p++]='i'; ln[p++]='='; PutDec(ln, &p, inter); ln[p] = 0;
    DrawStr(420, 1000, ln, 0x00FFFFFF);
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
static void PollDr0(ULONG ms)
{
    static UCHAR *ext; static ULONG la = 0xFFFFFFFF, lf = 0xFFFFFFFF, lr = 0xFFFFFFFF;
    ULONG a, f, r;
    if (!ext) {
        static USHORT nmp[40]; UNICODE_STRING up; PVOID fo = 0, dv = 0; const char *ap = "\\Device\\Harddisk0\\DR0"; ULONG q = 0;
        if (g_lastPm < 2) return;
        while (ap[q]) { nmp[q] = (USHORT)(UCHAR)ap[q]; q++; }
        up.Length = (USHORT)(q * 2); up.MaximumLength = up.Length; up.Buffer = nmp;
        if (IoGetDeviceObjectPointer(&up, 0x80, &fo, &dv) >= 0 && dv) {
            UCHAR *e = *(UCHAR **)((UCHAR *)dv + 0x40);
            if (e && MmIsAddressValid(e + 0x460)) { ext = e; g_dr0ext = e; WalkLst(0); }
        }
        if (!ext) return;
    }
    a = *(ULONG *)(ext + 0x210); f = *(ULONG *)(ext + 0x200); r = *(ULONG *)(ext + 0x204);
    if ((a != la || f != lf || r != lr) && g_pln < 16) {
        g_plms[g_pln] = ms; g_pla[g_pln] = a; g_plf[g_pln] = f; g_plr[g_pln] = r; g_pln++;
        la = a; lf = f; lr = r;
    }
}
static void MonThread(PVOID ctx)
{
    ULONGLONG t0 = rd_cntvct(), per, freq = rd_cntfrq();
    ULONG ms, lastdraw = 0, lastobj = 0, lastgrid = 0, w, b, p, pi, pv, pc;
    (void)ctx;
    KeSetPriorityThread(KeGetCurrentThread(), 1);
    if (freq == 0) freq = 19200000;
    per = freq / 1000;
    g_t0v = t0; g_perv = per;
    for (;;) {
        ms = (ULONG)((rd_cntvct() - t0) / per);
        if (ms - lastdraw < 100) continue;
        lastdraw = ms;
        PollDr0(ms);
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
            g_lastMs = ms;
            if (ms > 4000 && g_kn == 0) { PVOID th2 = 0; g_kn = 1; if (PsCreateSystemThread(&th2, 0x1FFFFF, 0, 0, 0, (PVOID)ProbeThread, 0) >= 0 && th2) ZwClose(th2); }
            if (0) KickPnp();
            if (ms > 6000 && !g_fsStarted) { PVOID th3 = 0; g_fsStarted = 1; if (PsCreateSystemThread(&th3, 0x1FFFFF, 0, 0, 0, (PVOID)FsThread, 0) >= 0 && th3) ZwClose(th3); }
        }
        if (ms - lastobj >= 500) {
            lastobj = ms;
            DrawHex(0, 8, CountDevs(nm_sdbus, (USHORT)(sizeof(nm_sdbus) / 2 - 1)),   0x0000C0FF);
            DrawHex(1, 8, CountDevs(nm_sdstor, (USHORT)(sizeof(nm_sdstor) / 2 - 1)),  0x0000C0FF);
            DrawHex(2, 8, CountDevs(nm_acpi, (USHORT)(sizeof(nm_acpi) / 2 - 1)),    0x0000C0FF);
            DrawHex(3, 8, CountDevs(nm_disk, (USHORT)(sizeof(nm_disk) / 2 - 1)),    0x0000C0FF);
            DrawHex(4, 8, CountDevs(nm_partmgr, (USHORT)(sizeof(nm_partmgr) / 2 - 1)), 0x0000C0FF);
            if (g_lastPm > 2 && !g_tPm1) g_tPm1 = ms;
            if (g_lastPm >= 82 && !g_tPm82) g_tPm82 = ms;
            DrawHex(5, 8, CountDevs(nm_volmgr, (USHORT)(sizeof(nm_volmgr) / 2 - 1)),  0x0000C0FF);
        }
        /* SDCC1 (eMMC) registros SDHCI.  Fila 6: PRESENT_STATE(24) HOSTCTL/POWER(28) CLOCK(2C) INT_STATUS(30) INT_EN(34) HC2(3C).
           Fila 7: CAPS(40) CAPS2(44) VERSION(FC) ADMA_ERR(54) CMD(0C) RESP0(10) */
        if (g_sd) {
            if (g_sd[0x24 / 4] & 7) { g_tIo = ms; g_nIo++; if (!g_tIo0) g_tIo0 = ms; }
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
