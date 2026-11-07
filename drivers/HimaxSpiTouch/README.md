# HimaxSpiTouch — driver táctil Himax HX83102E (SPI) para el SM-T500 (Windows ARM64)

Driver KMDF propio, sin SpbCx, GpioClx ni PEP (no existen para SM6115). El SSDT
solo le pasa regiones de memoria; el driver maneja directamente el controlador
SPI (SE0), los GPIO (TLMM) y los relojes (GCC).

**Estado: casi completo. Falta un módulo (`geni_spi.c`) y NO se ha compilado ni probado.**

## Módulos

| Archivo | Estado | Qué hace |
|---|---|---|
| `driver.c` | hecho | PnP/energía, mapea MMIO, hilo que arranca el táctil y sondea |
| `clocks.c` | hecho | enciende los relojes del QUP0/SE0 en el GCC (votación + tabla DFS) |
| `tlmm.c` | hecho | GPIO 80 (IRQ) y 31 (reset) por acceso directo |
| `himax.c` | hecho | protocolo HX83102E: bus SPI, registros AHB, sense_off/on, ID, CRC |
| `fwload.c` | hecho | carga del firmware sin flash (tabla de particiones + CRC) |
| `fileio.c` | hecho | lee el .bin del firmware (ruta en el registro) |
| `report.c` | hecho | paquete de 56 bytes → informe HID (con levantamiento de dedos) |
| `hid_desc.h` | hecho | descriptor HID de pantalla táctil, 10 dedos, 1200×2000 |
| `vhf.c` | hecho | dispositivo HID virtual (VHF) + GET_FEATURE del máximo de contactos |
| `geni_spi.c` | hecho | controlador SPI GENI en modo FIFO por sondeo (versión defensiva) |

`geni_spi.c` está implementado de forma conservadora: modo FIFO por sondeo
(sin DMA ni interrupciones), 1 byte por palabra de FIFO (sin manipular bits),
esperas acotadas por tiempo en cada bucle y aborto de la orden si algo se cuelga.
Es medio dúplex (TX y luego RX con el CS mantenido), que es lo que pide el Himax.
Compromiso: cargar el firmware (128 KiB) por FIFO byte a byte es lento; cuando
funcione, pasar esas escrituras grandes a DMA. Sigue SIN compilar ni probar.

## Flujo de arranque (driver.c, hilo de trabajo)

1. `ClocksEnableQup0Se0` — relojes del SE0.
2. `GeniSpiInit` — SPI en modo 3, ~10 MHz, FIFO.
3. `TlmmConfigure` — GPIO 80 entrada, 31 salida.
4. `HimaxDetect` — reset + sense_off + ID `83 10 2E`. **Primer hito.**
5. `FwReadFileFromRegistry` + `FwDownload` — cargar firmware a SRAM con CRC.
6. `HimaxSenseOn` — activar.
7. `VhfDeviceStart` + bucle de sondeo del GPIO 80 cada ~6 ms.

## Verificación hecha

- Todas las funciones que llama `driver.c` están definidas (grep).
- Firmas de VHF (`VHF_CONFIG_INIT`, `HID_XFER_PACKET`, `EVT_VHF_ASYNC_OPERATION`,
  `VhfReadReportSubmit`) comprobadas contra la documentación de Microsoft.
- Direcciones y protocolo sacados del kernel real de la tablet
  (`android_kernel_samsung_sm6115`, `hxchipset/`) y de `sm6115.dtsi`,
  `pinctrl-sm6115.c`, `gcc-sm6115.c`, `spi-geni-qcom.c`, `qcom-geni-se.c`.
- **No compilado** (no hay WDK aquí). **SSDT no validado** (no hay iasl aquí).

## Riesgos conocidos

- Nada compilado ni probado; es código de arranque para iterar en el dispositivo.
- `SE_SPI_WORD_LEN` = bits − 4 es la fórmula literal de Linux (MIN_WORD_LEN = 4);
  si el SPI manda basura, es el primer registro a revisar.
- `ClkSel` / divisor de reloj son aproximados (TCXO 19,2 MHz, objetivo ~10 MHz).
- El modo SPI del DT decía 1, pero el driver Android fuerza modo 3 → probar modo 3.
- Sondeo en vez de interrupción: sencillo pero gasta CPU; luego pasar a la IRQ del TLMM.
- La carga de config va partición por partición, no en bloque contiguo como Android;
  si el CRC de alguna parte falla, cambiar a bloque contiguo (nota en `fwload.c`).
- Detección de flanco del GPIO por "raw status": sin probar; hay respaldo por nivel.

Datos de referencia: documentos del proyecto `protocolo-himax-hx83102e.md` e
`investigacion-driver-tactil.md`. Compilación: `src/sources.txt`.
