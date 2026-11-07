# Compilar y probar HimaxSpiTouch

**No se puede compilar en Linux.** Un driver de kernel de Windows necesita el WDK
(solo Windows). Estos pasos son para una máquina Windows; la tablet solo recibe
el `.sys` ya compilado.

## 1. Preparar el PC de compilación (Windows x64 o ARM64)

1. Instala **Visual Studio 2022** (Community vale) con la carga "Desarrollo para escritorio con C++".
2. Instala el **Windows SDK** y el **WDK** de la misma versión (p. ej. 10.0.22621).
   El instalador del WDK añade la extensión de VS al final.
3. Abre `HimaxSpiTouch.sln` con Visual Studio (o crea un proyecto
   "Kernel Mode Driver, Empty (KMDF)" y añade los archivos de `src\`).
4. Ajusta en el `.vcxproj`:
   - `WindowsTargetPlatformVersion` a la versión de SDK/WDK que tengas.
   - `KmdfLibraryVersion` del INF a la que instaló el WDK (Propiedades → Driver Settings).

## 2. Compilar

- Configuración **Debug**, plataforma **ARM64**.
- Compila (Ctrl+B). Lo esperable en el primer intento: errores de tipos o de
  cabeceras que haya que corregir (el código no se ha compilado nunca).
- Al terminar genera `HimaxSpiTouch.sys`, `.inf` y, al firmar, `.cat`.

## 3. Firma de prueba

La tablet ya arranca con `testsigning` activado (el port lo pone). Firma el driver
con tu certificado de prueba, el mismo flujo que usas para los parches de kernel:
```
MakeCert / signtool sign /v /fd sha256 /s PruebaWOA /n "PruebaWOA" HimaxSpiTouch.sys
Inf2Cat /driver:. /os:10_NI_ARM64
signtool sign /v /fd sha256 /s PruebaWOA /n "PruebaWOA" HimaxSpiTouch.cat
```

## 4. ACPI: declarar el dispositivo

El driver necesita que el UEFI exponga el dispositivo con las 5 regiones de memoria.
Añade `acpi\touch-ssdt.asl` al conjunto de tablas de tu build de Mu-Silicium
(o inyéctalo como SSDT extra) y recompila el UEFI. Comprueba antes el `.asl` con
`iasl touch-ssdt.asl`. Sin esta entrada ACPI, Windows no cargará el driver.

## 5. Copiar el firmware

Saca `Himax_firmware_lide_hsd.bin` de la tablet (está en `/vendor/firmware/`,
accesible desde TWRP/recovery con `adb pull`) y ponlo junto al `.sys`. El INF lo
copia a `System32\drivers\` y el driver lo lee de ahí.

## 6. Instalar en la tablet

Con Windows ya arrancado en la tablet, teclado y ratón por el hub USB:
```
pnputil /add-driver HimaxSpiTouch.inf /install
```
Si el dispositivo ACPI aparece sin driver en el Administrador de dispositivos,
actualízalo a mano apuntando a la carpeta del INF.

## 7. Ver qué pasa (lo importante)

Antes de esperar que el táctil funcione, mira el log del driver:

1. En el PC de depuración: **DbgView** (Sysinternals) con "Capture Kernel" activado,
   o **WinDbg** conectado. Filtra por `HimaxSpiTouch`.
2. Alternativa en la tablet sin depurador: añade un visor de ETW, o compila con
   los `HxLog` redirigidos a un archivo.

**Secuencia esperada en el log (orden de arranque):**
```
SE0 FW_REVISION=... protocolo=1 (1 = SPI)      <- si protocolo != 1, el UEFI no cargó el firmware del SE
relojes QUP0/SE0 encendidos, SE=... Hz
SPI: modo 3, div=..., SE=... Hz
GPIO80 antes: CTL=... / GPIO31 antes: ...
detect: IC ID = 83 10 2E                        <- EL HITO. Si sale, SPI+relojes+GPIO+reset funcionan.
HX83102E detectado
firmware leído: 131072 bytes
firmware cargado (N particiones)
táctil listo; entrando en el bucle de sondeo
```

## 8. Interpretar fallos

| Síntoma en el log | Dónde mirar |
|---|---|
| `protocolo=0/2` (no 1) | el UEFI/XBL no cargó el firmware del SE0; problema de ACPI/UEFI, no del driver |
| `reloj X sigue apagado` | offsets del GCC o bits de voto (`clocks.c`) |
| ID distinto de `83 10 2E` o `sense_off ha fallado` | modo SPI (probar quitar CPHA/CPOL), `WORD_LEN`, divisor de reloj, pines SPI (GPIO 0-3), reset (GPIO 31) |
| ID correcto pero `CRC ... ha fallado` | la carga de SRAM; revisar el troceado o el fichero de firmware |
| Todo bien pero sin toques | el GPIO 80 (sondeo) o el formato del paquete (`report.c`); volcar los 56 bytes crudos |

## 9. Si cuelga la tablet

El driver arranca como DEMAND_START y el trabajo pesado va en un hilo con esperas
acotadas, así que un fallo debería quedar en el log sin colgar el sistema. Si aun
así se cuelga, desinstala con `pnputil /delete-driver HimaxSpiTouch.inf /uninstall`
desde otro arranque o quita la entrada del SSDT.

## Compilar en GitHub (sin instalar nada)
Sube el proyecto a un repositorio (el `.github/workflows/build.yml` ya viene incluido). En la pestaña **Actions** se lanza solo con cada push, o a mano con *Run workflow*. Si termina bien, el `.sys` y el `.inf` quedan en *Artifacts*. Si falla, copia el log del paso "Compilar" y corregimos. Es posible que la instalación del WDK en el runner necesite ajustes en el primer intento.
