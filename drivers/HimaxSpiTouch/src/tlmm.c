#include "tlmm.h"
#include "util.h"

/* Solo hacen falta dos GPIO: 80 (WEST) y 31 (EAST). */
static PUCHAR TileFor(PDEVICE_CONTEXT Ctx, ULONG Gpio)
{
    switch (Gpio) {
    case GPIO_TOUCH_IRQ: return Ctx->TlmmWest;
    case GPIO_TOUCH_RST: return Ctx->TlmmEast;
    default:             return NULL;
    }
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

    HxLog("GPIO80 antes: CTL=%08X IO=%08X INTR_CFG=%08X",
          TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_CTL(GPIO_TOUCH_IRQ)),
          TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_IO(GPIO_TOUCH_IRQ)),
          TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_INTR_CFG(GPIO_TOUCH_IRQ)));
    HxLog("GPIO31 antes: CTL=%08X IO=%08X",
          TlmmRd(Ctx, GPIO_TOUCH_RST, TLMM_CTL(GPIO_TOUCH_RST)),
          TlmmRd(Ctx, GPIO_TOUCH_RST, TLMM_IO(GPIO_TOUCH_RST)));

    /* GPIO 80: función 0 (GPIO), entrada, pull-up */
    ctl = TlmmRd(Ctx, GPIO_TOUCH_IRQ, TLMM_CTL(GPIO_TOUCH_IRQ));
    ctl &= ~(TLMM_CTL_FUNC_MASK | TLMM_CTL_OE | TLMM_CTL_PULL_MASK);
    ctl |= TLMM_CTL_PULL_UP;
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

    /* GPIO 31: función 0, salida a 1 (reset inactivo), fuerza 2 mA */
    TlmmWr(Ctx, GPIO_TOUCH_RST, TLMM_IO(GPIO_TOUCH_RST), TLMM_IO_OUT);
    ctl = TlmmRd(Ctx, GPIO_TOUCH_RST, TLMM_CTL(GPIO_TOUCH_RST));
    ctl &= ~(TLMM_CTL_FUNC_MASK | TLMM_CTL_PULL_MASK | TLMM_CTL_DRV_MASK);
    ctl |= TLMM_CTL_OE;
    TlmmWr(Ctx, GPIO_TOUCH_RST, TLMM_CTL(GPIO_TOUCH_RST), ctl);

    /* Las líneas SPI (GPIO 0-3, función qup0 = 1) las deja la UEFI/bootloader.
     * TODO: si el SPI no responde, comprobar su CTL (función 1) en TlmmWest. */
    for (ULONG g = 0; g < 4; g++) {
        HxLog("GPIO%u (SPI) CTL=%08X", g,
              READ_REGISTER_ULONG((volatile ULONG *)(Ctx->TlmmWest + TLMM_CTL(g))));
    }
    return STATUS_SUCCESS;
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
