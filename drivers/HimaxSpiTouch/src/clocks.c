#include "clocks.h"
#include "util.h"

static ULONG GccRead(PDEVICE_CONTEXT Ctx, ULONG Off)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Ctx->Gcc + Off));
}

static VOID GccWrite(PDEVICE_CONTEXT Ctx, ULONG Off, ULONG Val)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Ctx->Gcc + Off), Val);
}

/* Espera a que CLK_OFF = 0 (máx. ~1 ms, como Linux con BRANCH_HALT_VOTED). */
static NTSTATUS WaitBranchOn(PDEVICE_CONTEXT Ctx, ULONG Cbcr, const char *Name)
{
    for (ULONG i = 0; i < 200; i++) {
        if ((GccRead(Ctx, Cbcr) & CBCR_CLK_OFF) == 0) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }
    HxLog("reloj %s sigue apagado (CBCR=%08X)", Name, GccRead(Ctx, Cbcr));
    return STATUS_IO_TIMEOUT;
}

/* Elige el nivel DFS que use el TCXO (fuente 0) y divisor 1 → 19,2 MHz.
 * Los niveles con PLL no se usan porque haría falta conocer su frecuencia. */
static NTSTATUS PickDfsLevel(PDEVICE_CONTEXT Ctx, PULONG SeClkHz, PULONG ClkSel)
{
    for (ULONG l = 0; l < RCG_MAX_PERF_LEVEL; l++) {
        ULONG cfg = GccRead(Ctx, GCC_QUPV3_WRAP0_S0_CMD_RCGR + RCG_PERF_DFSR(l));
        ULONG src = (cfg >> 8) & 0x7;
        ULONG div = cfg & 0x1F;           /* 2*div-1, 0 = sin dividir */
        ULONG mode = (cfg >> 12) & 0x3;   /* != 0 → MND activo */

        HxLog("DFS nivel %u: CFG=%08X (src=%u div=%u mode=%u)", l, cfg, src, div, mode);

        if (src == 0 && (div == 0 || div == 1) && mode == 0) {
            *SeClkHz = TCXO_HZ;
            *ClkSel = l;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS ClocksEnableQup0Se0(PDEVICE_CONTEXT Ctx, PULONG SeClkHz, PULONG ClkSel)
{
    if (!Ctx || !Ctx->Gcc || !SeClkHz || !ClkSel)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS status;
    HxLog("ANTES: leer GCC voto de ramas (GCC+%08X)", GCC_APCS_CLOCK_BRANCH_ENA_VOTE);
    ULONG vote = GccRead(Ctx, GCC_APCS_CLOCK_BRANCH_ENA_VOTE);

    HxLog("GCC voto antes = %08X", vote);
    vote |= VOTE_QUPV3_WRAP_0_M_AHB | VOTE_QUPV3_WRAP_0_S_AHB |
            VOTE_QUPV3_WRAP0_CORE | VOTE_QUPV3_WRAP0_CORE_2X;
    HxLog("ANTES: escribir voto de ramas = %08X", vote);
    GccWrite(Ctx, GCC_APCS_CLOCK_BRANCH_ENA_VOTE, vote);
    HxLog("voto escrito; esperando ramas");

    status = WaitBranchOn(Ctx, GCC_QUPV3_WRAP_0_M_AHB_CBCR, "m_ahb");
    if (!NT_SUCCESS(status)) return status;
    status = WaitBranchOn(Ctx, GCC_QUPV3_WRAP_0_S_AHB_CBCR, "s_ahb");
    if (!NT_SUCCESS(status)) return status;
    status = WaitBranchOn(Ctx, GCC_QUPV3_WRAP0_CORE_CBCR, "core");
    if (!NT_SUCCESS(status)) return status;
    status = WaitBranchOn(Ctx, GCC_QUPV3_WRAP0_CORE_2X_CBCR, "core_2x");
    if (!NT_SUCCESS(status)) return status;

    /* Frecuencia del SE */
    HxLog("ramas AHB/core encendidas");
    ULONG dfsr = GccRead(Ctx, GCC_QUPV3_WRAP0_S0_CMD_RCGR + RCG_DFSR);
    if (dfsr & 1) {
        status = PickDfsLevel(Ctx, SeClkHz, ClkSel);
        if (!NT_SUCCESS(status)) {
            HxLog("DFS activo pero sin nivel de 19,2 MHz");
            return status;
        }
    } else {
        /* Sin DFS: RCG = TCXO, divisor 1, sin MND */
        GccWrite(Ctx, GCC_QUPV3_WRAP0_S0_CMD_RCGR + RCG_CFG, (0u << 8) | 1u);
        GccWrite(Ctx, GCC_QUPV3_WRAP0_S0_CMD_RCGR,
                 GccRead(Ctx, GCC_QUPV3_WRAP0_S0_CMD_RCGR) | RCG_CMD_UPDATE);
        BOOLEAN updateDone = FALSE;
        for (ULONG i = 0; i < 500; i++) {
            if ((GccRead(Ctx, GCC_QUPV3_WRAP0_S0_CMD_RCGR) & RCG_CMD_UPDATE) == 0) {
                updateDone = TRUE;
                break;
            }
            KeStallExecutionProcessor(1);
        }
        if (!updateDone) {
            HxLog("timeout esperando GCC RCG_CMD_UPDATE del SE0");
            return STATUS_IO_TIMEOUT;
        }
        *SeClkHz = TCXO_HZ;
        *ClkSel = 0;
    }

    /* Rama del SE0 */
    HxLog("ANTES: votar rama wrap0_s0");
    GccWrite(Ctx, GCC_APCS_CLOCK_BRANCH_ENA_VOTE,
             GccRead(Ctx, GCC_APCS_CLOCK_BRANCH_ENA_VOTE) | VOTE_QUPV3_WRAP0_S0);
    status = WaitBranchOn(Ctx, GCC_QUPV3_WRAP0_S0_CBCR, "wrap0_s0");
    if (!NT_SUCCESS(status)) return status;

    HxLog("relojes QUP0/SE0 encendidos, SE=%u Hz, CLK_SEL=%u (DFS=%u)",
          *SeClkHz, *ClkSel, dfsr & 1);
    return STATUS_SUCCESS;
}
