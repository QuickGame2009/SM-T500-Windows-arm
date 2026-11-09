#include "report.h"
#include "hid_desc.h"
#include "util.h"

BOOLEAN ReportChecksumOk(const UCHAR *Buf, ULONG Len)
{
    ULONG sum = 0, zeros = 0;
    for (ULONG i = 0; i < Len; i++) {
        sum += Buf[i];
        if (Buf[i] == 0) zeros++;
    }
    if (zeros == Len) return FALSE;       /* paquete vacío */
    return (sum & 0xFF) == 0;
}

NTSTATUS ReportParse(const UCHAR *Buf, TOUCH_DATA *Out)
{
    RtlZeroMemory(Out, sizeof(*Out));

    if (!ReportChecksumOk(Buf, HX_TOUCH_INFO_SIZE))
        return STATUS_DATA_ERROR;

    /*
     * HX_TOUCH_INFO_POINT_CNT está en el byte 52 para 10 contactos.
     * Los bytes 40..51 son datos de área/anchura, no el contador.
     * No interpretar coordenadas residuales cuando el firmware informa 0.
     */
    UCHAR pointCount = Buf[HX_FINGER_NUM_OFFSET] & 0x0F;
    if (pointCount > TOUCH_MAX_CONTACTS)
        return STATUS_DATA_ERROR;

    UCHAR parsed = 0;
    for (ULONG i = 0; i < pointCount; i++) {
        USHORT x = (USHORT)(((USHORT)Buf[i * 4] << 8) | Buf[i * 4 + 1]);
        USHORT y = (USHORT)(((USHORT)Buf[i * 4 + 2] << 8) | Buf[i * 4 + 3]);
        UCHAR  w = Buf[HX_AREA_OFFSET + i];

        if (x <= TOUCH_MAX_X && y <= TOUCH_MAX_Y) {
            Out->Contacts[i].Down = TRUE;
            Out->Contacts[i].X = x;
            Out->Contacts[i].Y = y;
            Out->Contacts[i].Width = w;
            parsed++;
        }
    }

    Out->Count = parsed;
    return STATUS_SUCCESS;
}

VOID ReportBuildHid(PDEVICE_CONTEXT Ctx, const TOUCH_DATA *In, HID_TOUCH_REPORT *Out)
{
    RtlZeroMemory(Out, sizeof(*Out));
    Out->ReportId = REPORTID_TOUCH;

    UCHAR reported = 0;
    for (ULONG i = 0; i < TOUCH_MAX_CONTACTS; i++) {
        BOOLEAN down = In->Contacts[i].Down;
        /* Enviar los que están abajo y, una vez, los que acaban de levantarse. */
        if (down || Ctx->PrevDown[i]) {
            Out->Contacts[reported].Status    = down ? 0x03 : 0x00;  /* Tip | Confidence */
            Out->Contacts[reported].ContactId = (UCHAR)i;
            Out->Contacts[reported].X = down ? In->Contacts[i].X : Ctx->PrevX[i];
            Out->Contacts[reported].Y = down ? In->Contacts[i].Y : Ctx->PrevY[i];
            reported++;
        }
        Ctx->PrevDown[i] = down;
        Ctx->PrevX[i] = In->Contacts[i].X;
        Ctx->PrevY[i] = In->Contacts[i].Y;
    }
    Out->ContactCount = reported;

    /* Scan Time: contador de 100 µs a partir del QPC. */
    LARGE_INTEGER now = KeQueryPerformanceCounter(NULL);
    if (Ctx->QpcFreq.QuadPart)
        Out->ScanTime = (USHORT)((now.QuadPart * 10000) / Ctx->QpcFreq.QuadPart);
}
