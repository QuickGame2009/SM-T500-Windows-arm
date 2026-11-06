DefinitionBlock ("SSDT.aml", "SSDT", 2, "SAMSNG", "GTA4LWIF", 3)
{
    External (\_SB.UFS0, DeviceObj)
    External (\_SB.SDC1, DeviceObj)
    External (\_SB.URS0, DeviceObj)

    Scope (\_SB)
    {
        Name (PSUB, "IDP06115")
    }

    // PRUEBA: URS0 (USB role switch) desactivado para ver si el arranque deja de pararse
    Scope (\_SB.URS0)
    {
        Method (_STA, 0, NotSerialized) { Return (0x00) }
    }

    // El SM-T500 usa eMMC (SDC1), no UFS
    Scope (\_SB.UFS0)
    {
        Method (_STA, 0, NotSerialized) { Return (0x00) }
    }

    Scope (\_SB.SDC1)
    {
        Method (_STA, 0, NotSerialized) { Return (0x0F) }
    }
}
