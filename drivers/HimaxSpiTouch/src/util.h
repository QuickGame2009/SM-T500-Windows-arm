#pragma once
#include <ntddk.h>

/* Registro por el depurador (DbgView / WinDbg con filtro IHVDRIVER). */
#define HxLog(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HimaxSpiTouch: " fmt "\n", __VA_ARGS__)

/* Espera en milisegundos (PASSIVE_LEVEL). */
static __forceinline VOID HxSleepMs(ULONG Ms)
{
    LARGE_INTEGER t;
    t.QuadPart = -(LONGLONG)Ms * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &t);
}

static __forceinline VOID HxPutLe32(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)v; p[1] = (UCHAR)(v >> 8); p[2] = (UCHAR)(v >> 16); p[3] = (UCHAR)(v >> 24);
}

static __forceinline ULONG HxGetLe32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}
