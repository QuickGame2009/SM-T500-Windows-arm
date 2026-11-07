/*
 * touch-ssdt.asl — se añade al SSDT de gta4lwifi en Silicium-ACPI.
 * No usa SpiSerialBusV2 ni GpioInt: no hay controlador SPI ni GPIO de Windows
 * para SM6115. El driver propio recibe solo regiones de memoria y las maneja.
 *
 * El orden de las regiones debe coincidir con MapResource() de driver.c:
 *   0 SE0, 1 TLMM west, 2 TLMM east, 3 GCC, 4 wrapper QUP0.
 */
DefinitionBlock ("", "SSDT", 2, "SMT500", "HMXTOUCH", 1)
{
    Scope (\_SB)
    {
        Device (TSC1)
        {
            Name (_HID, "HMXT8310")       // coincide con el INF (ACPI\HMXT8310)
            Name (_UID, 1)
            Name (_CCA, Zero)             // sin coherencia de caché (DMA no usado)

            Method (_STA, 0, NotSerialized)
            {
                // TODO: devolver 0 hasta que geni_spi.c esté implementado,
                // para no cargar un driver que de todos modos parará en el SPI.
                Return (0x0F)
            }

            Method (_CRS, 0, NotSerialized)
            {
                Return (ResourceTemplate ()
                {
                    Memory32Fixed (ReadWrite, 0x04A80000, 0x00004000)   // 0: QUP0 SE0 (SPI)
                    Memory32Fixed (ReadWrite, 0x00500000, 0x00400000)   // 1: TLMM west (GPIO 80, SPI 0-3)
                    Memory32Fixed (ReadWrite, 0x00D00000, 0x00400000)   // 2: TLMM east (GPIO 31)
                    Memory32Fixed (ReadWrite, 0x01400000, 0x001F0000)   // 3: GCC
                    Memory32Fixed (ReadWrite, 0x04AC0000, 0x00002000)   // 4: wrapper QUP0
                    // Más adelante, al pasar de sondeo a interrupción:
                    // Interrupt (ResourceConsumer, Edge, ActiveLow, Exclusive) { 0x103 } // TLMM GIC SPI 227
                })
            }
        }
    }
}
