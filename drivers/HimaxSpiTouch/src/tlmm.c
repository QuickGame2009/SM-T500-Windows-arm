#include "tlmm.h"
#include "util.h"

/* El SM6115 tiene un único bloque TLMM en 0x00500000 para GPIO0..126. */
static PUCHAR TileFor(PDEVICE_CONTEXT Ctx, ULONG Gpio)
{
    return (Gpio <= 126) ? Ctx->Tlmm : NULL;
}

static ULONG TlmmRd(PDEVICE_CONTEXT Ctx, ULONG Gpio, ULONG Off)
{
    PUCHAR t = TileFor(Ctx, Gpio);
    return t ? READ_REGISTER_ULONG((volatile ULONG *)(t + Off)) : 0;
}

static VOID TlmmWr(PDEVICE_CONTEXT Ctx, ULONG Gpio, ULONG Off, ULONG Val)
{
    PUCHAR t = TileFor(Ctx, Gpio);
    if (t) WRITE_REGISTER_ULONG((volatile ULONG *)(t + Off), Val);
}

NTSTATUS TlmmConfigure(PDEVICE_CONTEXT Ctx)
{
    ULONG ctl;

    HxLog("ANTES: leer registros TLMM de GPIO80/31");
    HxLog("GPIO80 antes: CTL=%08X IO=%08X INTR_CFG=%08X",
          TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_CTL(GPIO_TOUCH_IRQ)),
          TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_IO(GPIO_TOUCH_IRQ)),
          TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_CFG(GPIO_TOUCH_IRQ)));
    HxLog("GPIO31 antes: CTL=%08X IO=%08X",
          TlmmRd(Ctx, GPIO_TOUCH_RST, TLMM_CTL(GPIO_TOUCH_RST)),
          TlmmRd(Ctx, GPIO_TOUCH_RST, TLMM_IO(GPIO_TOUCH_RST)));

    /* QUP0 SE0: GPIO0..3, función qup0=1, drive 6 mA, sin pull (scuba.dts). */
    for (ULONG g = 0; g < 4; g++) {
        ctl = TlmmRd(Ctx, g, TLMM_CTL(g));
        ctl &= ~(TLMM_CTL_FUNC_MASK | TLMM_CTL_OE |
                 TLMM_CTL_PULL_MASK | TLMM_CTL_DRV_MASK);
        ctl |= (1u << TLMM_CTL_FUNC_SHIFT) | (2u << TLMM_CTL_DRV_SHIFT);
        TlmmWr(Ctx, g, TLMM_CTL(g), ctl);
    }

    /* GPIO80: IRQ táctil, GPIO input, pull-up y drive-strength 8 mA. */
    ctl = TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_CTL(GPIO_TOUCH_IRQ));
    ctl &= ~(TLMM_CTL_FUNC_MASK | TLMM_CTL_OE |
             TLMM_CTL_PULL_MASK | TLMM_CTL_DRV_MASK);
    ctl |= TLMM_CTL_PULL_UP | (3u << TLMM_CTL_DRV_SHIFT);
    TlmmWr(Ctx, GPIO_TOUCH_IRQ, TLMM_CTL(GPIO_TOUCH_IRQ), ctl);

    /* Detección de flanco de bajada con "raw status": el TLMM guarda el evento
     * en INTR_STATUS sin enviarlo al GIC (enable = 0), así el sondeo no pierde
     * pulsos cortos. Sin probar: si nunca se activa, el hilo de trabajo también
     * mira el nivel de la entrada. */
    ULONG icfg = TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_CFG(GPIO_TOUCH_IRQ));
    icfg &= ~(TLMM_INTR_ENABLE | TLMM_INTR_DET_MASK);
    icfg |= TLMM_INTR_DET_FALLING | TLMM_INTR_POL_HIGH | TLMM_INTR_RAW_STATUS;
    TlmmWr(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_CFG(GPIO_TOUCH_IRQ), icfg);
    TlmmWr(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_STATUS(GPIO_TOUCH_IRQ), 0);

    /* GPIO71: reset táctil según pmx_ts_reset_active de scuba.dts.
     * Salida alta, pull-up y drive-strength de 8 mA. */
    TlmmWr(Ctx, GPIO_TOUCH_RST, TLMM_IO(GPIO_TOUCH_RST), TLMM_IO_OUT);
    ctl = TlmmRd(Ctx, GPIO_TOUCH_RST, TLMM_CTL(GPIO_TOUCH_RST));
    ctl &= ~(TLMM_CTL_FUNC_MASK | TLMM_CTL_PULL_MASK | TLMM_CTL_DRV_MASK);
    ctl |= TLMM_CTL_OE | TLMM_CTL_PULL_UP | (3u << TLMM_CTL_DRV_SHIFT);
    TlmmWr(Ctx, GPIO_TOUCH_RST, TLMM_CTL(GPIO_TOUCH_RST), ctl);

    for (ULONG g = 0; g < 4; g++)
        HxLog("GPIO%u (QUP0 SPI) CTL=%08X", g, TlmmRd(Ctx, g, TLMM_CTL(g)));
    return STATUS_SUCCESS;
}

ULONG TlmmReadRaw(PDEVICE_CONTEXT Ctx, ULONG Gpio, ULONG Off)
{
    return TlmmRd(Ctx, Gpio, (Off == 0 ? TLMM_CTL(Gpio) : Off == 4 ? TLMM_IO(Gpio) :
                              Off == 8 ? TLMM_INTR_CFG(Gpio) : TLMM_INTR_STATUS(Gpio)));
}

ULONG TlmmReadInput(PDEVICE_CONTEXT Ctx, ULONG Gpio)
{
    return TlmmRd(Ctx, Gpio, TLMM_IO(Gpio)) & TLMM_IO_IN;
}

VOID TlmmWriteOutput(PDEVICE_CONTEXT Ctx, ULONG Gpio, ULONG Value)
{
    TlmmWr(Ctx, Gpio, TLMM_IO(Gpio), Value ? TLMM_IO_OUT : 0);
}

BOOLEAN TlmmTouchEdgePending(PDEVICE_CONTEXT Ctx)
{
    if (TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_STATUS(GPIO_TOUCH_IRQ)) & 1) {
        TlmmWr(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_STATUS(GPIO_TOUCH_IRQ), 0);
        return TRUE;
    }
    return FALSE;
}
