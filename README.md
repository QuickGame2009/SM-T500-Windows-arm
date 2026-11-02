# Kit de pruebas: Windows ARM64 en el Galaxy Tab A7 Wi-Fi (SM-T500)

Todo lo necesario para probar el port UEFI del SM-T500 el día que tengas la tablet.
Basado en Mu-Silicium (port gta4l) y en la guía de Project-Silicium (`Guides/General/OS/Full-Windows`, marcada por sus autores como desactualizada).

## Qué está comprobado y qué no

| Pieza | Estado |
|---|---|
| SSDT/ACPI del SM-T500 | Compila. Es una copia del del Redmi 9T (mismo SoC) con eMMC en vez de UFS. |
| Scripts de info, backup, almacenamiento masivo y plan de particiones (PowerShell) | Probados con PowerShell 7 y un `adb` simulado con datos inventados. **No probados en Windows PowerShell 5.1 ni en la tablet.** |
| Flujo de compilación (GitHub Actions) | Escrito según el flujo oficial de Mu-Silicium. **Nunca se ha ejecutado.** Puede fallar y haya que ajustarlo. |
| `MassStorage-emmc.sh` | Adaptación del script oficial de SM6115 a lo que se vio en tu tablet. No probado. |
| Bootloader S8 (BIT-8), `msm-id`, mapa de memoria, eMMC, UDC | **Comprobado** con tu `info.zip`: coincide con lo que espera el port gta4l. |
| `board-id` | Tu tablet tiene 0x1000b; el DTB por defecto trae 0 0 (como el T505N). Variante `boardid` disponible. |
| `apply-windows.cmd`, `unattend.xml` | Sin probar. |
| Que el UEFI de gta4l arranque en tu SM-T500 | **Desconocido.** Los binarios vienen del firmware del modelo LTE. |
| Que Windows arranque | **Desconocido.** En el port gta4l figura como no funcional. En el Redmi 9T (mismo SoC) sí arranca, con CPU, pantalla, almacenamiento y USB host únicamente. |

Cuenta con: **sin táctil, sin Wi-Fi, sin GPU, sin batería ni audio.** Necesitarás un hub USB-C con teclado y ratón.
No existen drivers de Windows públicos para este SoC (la guía de Silicium tiene esa sección en blanco).

## Contenido

```
overlay/            Port gta4lwifi (se copia sobre Mu-Silicium al compilar)
.github/workflows/  Compilación automática en GitHub
1-info.cmd ...       Accesos directos (doble clic) a los scripts de PowerShell
scripts/            Scripts .ps1 y MassStorage-emmc.sh (se ejecuta dentro de la tablet)
scripts/linux-mac/  Versiones en bash para Linux/macOS
windows/            apply-windows.cmd, unattend.xml
```

Los accesos directos `1-info.cmd`, `2-backup.cmd`, `3-massstorage.cmd` y `4-plan.cmd` se ejecutan con doble clic (usan PowerShell, que ya trae Windows).
Hacen falta las platform-tools de Android: descomprímelas dentro de esta carpeta para que quede `platform-tools\adb.exe` (o ponlas en el PATH).
Si Windows avisa de que el script viene de Internet, desbloquea el zip antes de extraerlo (clic derecho > Propiedades > Desbloquear).

## Fase 0: lo que puedes hacer ya, sin la tablet

1. **Compilar el UEFI.** Crea un repositorio en GitHub, sube el contenido de este kit (incluida la carpeta oculta `.github`), ve a *Actions > Compilar UEFI gta4lwifi > Run workflow*. En el desplegable *dtb* deja `default` la primera vez. Al terminar, descarga el artefacto `Mu-gta4lwifi-default-DEBUG` y guarda `Mu-gta4lwifi.img`. Si la tablet no arranca el UEFI, repite con `dtb = boardid` (usa el board-id real de tu tablet, 0x1000b, en vez de 0 0). Si la compilación falla, envíame el log.
2. **Imagen de Windows.** En [uupdump.net](https://uupdump.net/) elige **Windows 11 arm64, build 22621 o 22631** (22H2/23H2). No elijas 24H2 ni posteriores: el Kryo 260 no tiene las instrucciones que exigen y el proyecto avisa de que pueden dejar el equipo inservible. Descarga el paquete, ejecuta `uup_download_windows.cmd` (ruta sin espacios) y, del ISO resultante, extrae `sources\install.wim`.
3. **Descargas.** Android platform-tools (en `platform-tools\` dentro de esta carpeta), Odin, el firmware oficial del SM-T500 (con Frija o SamFW), y los binarios `parted` y `gdisk` para ARM64 que enlaza la guía de Silicium (`renegade-project.tech/tools/parted.7z` y `gdisk.7z`).
4. Si no la tienes ya, guarda una copia del TWRP que usas.

## Fase 1: con la tablet en TWRP (solo lectura)

```
1-info.cmd        (crea la carpeta info\)
2-backup.cmd      (crea la carpeta backup\)
```

- Envíame la carpeta `info\`. Con eso compruebo el bootloader, el `msm-id`/`board-id` del DTB, el mapa de memoria y la tabla de particiones.
- Copia `backup\` a otro disco o a la nube. Sin eso no sigas.
- El proyecto exige para gta4l un bootloader "versión 8". En `info\arranque-resumen.txt` mira `ro.boot.bootloader`: normalmente el dígito que sigue a la U o S (por ejemplo `T500XXU4…` sería 4). Si es distinto, no lo toques todavía y consúltame.

## Fase 2: probar el UEFI antes de tocar el disco

Esta es la prueba de menor riesgo: solo se sustituye `boot`, y tienes su copia.

```
adb push Mu-gta4lwifi.img /tmp/
adb shell "dd if=/tmp/Mu-gta4lwifi.img of=/dev/block/by-name/boot bs=1048576 && sync"
```

Apaga y enciende. Android ya no arrancará (es normal). Resultados posibles:

- **Aparece el menú de Mu / Windows Boot Manager:** el UEFI funciona. Sigue a la fase 3.
- **Pantalla negra o reinicios:** vuelve a TWRP (apagada, mantén Volumen+ y Encendido) y restaura:
  `adb push backup/boot.img /tmp/` y `adb shell "dd if=/tmp/boot.img of=/dev/block/by-name/boot bs=1048576 && sync"`.
  Cuéntame qué viste exactamente; probablemente haya que ajustar el DTB o el mapa de memoria.

## Fase 3: particionar e instalar Windows (borra la memoria interna)

1. Copia `parted` y `gdisk` (ARM64) a esta carpeta. `4-plan.cmd` imprime los comandos exactos de `parted` para tu tabla (por defecto userdata 4096 MiB y esp 512 MiB; para cambiarlos: `4-plan.cmd -UserdataMiB 8192`) y los guarda en `info\plan-particionado.txt`. Revísalos y pégalos en TWRP con todo desmontado. Si dice que `userdata` no es la última partición, para y consúltame.
2. Formatea `userdata`, `esp` (FAT32) y `win` (NTFS) con los comandos que imprime el plan. Reinicia a TWRP.
3. Expón el disco al PC con `3-massstorage.cmd` (te pide escribir SI). El script se lanza en segundo plano y adb se corta unos segundos (es normal). Si Windows no ve un disco nuevo, reinicia la tablet a TWRP y ejecuta `adb shell cat /cache/ms.log`; envíame el resultado. (En tu tablet TWRP no trae la función `mass_storage`; el script la crea, pero no está probado.) Si pide "inicializar" o "formatear" algún disco, cancela.
4. En una consola de administrador (`diskpart`): `lis dis`, `sel dis N` (el de unos 60 GB), `lis par`, y asigna letras: `sel par X` / `assign letter=R` para `win` y otra letra para `esp`.
5. Copia `unattend.xml` junto a `apply-windows.cmd` (edita la contraseña) y ejecuta:
   `windows\apply-windows.cmd C:\ruta\install.wim R X 1`
   Aplica la imagen, crea el arranque, activa `testsigning`/`nointegritychecks`, arregla el USB (`OsDefaultRoleSwitchMode`) y copia la respuesta desatendida.
6. Reinicia la tablet (quita el almacenamiento masivo) con el hub USB, teclado y ratón conectados.

## Si algo sale mal

- **Volver a TWRP:** apagada, Volumen+ y Encendido. **Modo Download (Odin):** apagada, mantén Volumen+ y Volumen− y conecta el cable al PC.
- **Restaurar `boot`:** como en la fase 2.
- **Restaurar todo:** Odin con el firmware oficial (BL, AP, CP y CSC). Si modificaste las particiones y Odin falla, usa el `.pit` que trae el firmware y marca *Re-Partition*. La copia de la GPT está en `backup\gpt-*.bin`.
- Ten en cuenta que el bootloader desbloqueado ya activó Knox; eso no se revierte.

## Qué envíame para seguir

1. La carpeta `info\` (fase 1).
2. El resultado de la compilación (o su log si falla).
3. Lo que muestra la tablet tras la fase 2 (una foto vale).
