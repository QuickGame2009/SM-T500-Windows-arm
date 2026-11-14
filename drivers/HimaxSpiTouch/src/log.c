/*
 * log.c — registro a fichero con volcado a disco en cada línea.
 *
 * Sirve para diagnosticar apagados bruscos: si la tablet se apaga en mitad de
 * un acceso a hardware, la última línea de C:\Windows\HimaxSpiTouch.log dice
 * hasta dónde llegó el driver. Solo escribe a PASSIVE_LEVEL; en otro nivel
 * queda únicamente la salida por DbgPrintEx.
 */
#include <ntifs.h>
#include <ntstrsafe.h>
#include "util.h"

static HANDLE    g_LogFile;
/* KMUTEX, no FAST_MUTEX: este deja la IRQL en PASSIVE_LEVEL. Con FAST_MUTEX la
 * E/S de fichero (que necesita APC especiales) se bloquearía para siempre. */
static KMUTEX    g_LogLock;
static LONG      g_Seq;
static BOOLEAN   g_Init;

VOID HxLogInit(VOID)
{
    KeInitializeMutex(&g_LogLock, 0);
    g_Init = TRUE;
}

static VOID EnsureOpen(VOID)
{
    if (g_LogFile) return;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\SystemRoot\\HimaxSpiTouch.log");
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE h = NULL;
    NTSTATUS s = ZwCreateFile(&h, FILE_APPEND_DATA | SYNCHRONIZE, &oa, &iosb, NULL,
                              FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN_IF,
                              FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE |
                              FILE_WRITE_THROUGH, NULL, 0);
    if (NT_SUCCESS(s)) g_LogFile = h;
}

VOID HxLogWrite(const char *Fmt, ...)
{
    char line[256];
    char body[220];
    va_list ap;

    va_start(ap, Fmt);
    if (!NT_SUCCESS(RtlStringCbVPrintfA(body, sizeof(body), Fmt, ap)))
        body[sizeof(body) - 1] = 0;
    va_end(ap);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "HimaxSpiTouch: %s\n", body);

    if (!g_Init || KeGetCurrentIrql() != PASSIVE_LEVEL) return;

    {
        /* Espera corta (100 ms): hay dos hilos (táctil y acelerómetro) y no debe perderse
         * la línea previa a un acceso a hardware. KMUTEX: se puede esperar a PASSIVE_LEVEL. */
        LARGE_INTEGER lim; lim.QuadPart = -(LONGLONG)100 * 10000;
        if (KeWaitForSingleObject(&g_LogLock, Executive, KernelMode, FALSE, &lim) != STATUS_SUCCESS)
            return;
    }
    EnsureOpen();
    if (g_LogFile) {
        size_t len = 0;
        LONG seq = ++g_Seq;
        if (NT_SUCCESS(RtlStringCbPrintfA(line, sizeof(line), "[%04ld] %s\r\n", seq, body)) &&
            NT_SUCCESS(RtlStringCbLengthA(line, sizeof(line), &len))) {
            IO_STATUS_BLOCK iosb;
            LARGE_INTEGER off;
            off.LowPart = FILE_WRITE_TO_END_OF_FILE;
            off.HighPart = -1;
            if (NT_SUCCESS(ZwWriteFile(g_LogFile, NULL, NULL, NULL, &iosb, line, (ULONG)len, &off, NULL)))
                (VOID)ZwFlushBuffersFile(g_LogFile, &iosb);
        }
    }
    KeReleaseMutex(&g_LogLock, FALSE);
}

VOID HxLogClose(VOID)
{
    if (!g_Init) return;
    KeWaitForSingleObject(&g_LogLock, Executive, KernelMode, FALSE, NULL);
    if (g_LogFile) { ZwClose(g_LogFile); g_LogFile = NULL; }
    KeReleaseMutex(&g_LogLock, FALSE);
}
