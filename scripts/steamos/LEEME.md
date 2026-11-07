# Scripts para Steam Deck (SteamOS) / Linux

Son los mismos pasos que los `.cmd` / `.ps1` de Windows, pero para hacerlos desde la Steam Deck en **modo escritorio** (Konsole).

## Preparación (una sola vez)

1. En Konsole, pon contraseña al usuario `deck` (si no la tiene):
   ```
   passwd
   ```
2. Descarga el repositorio:
   ```
   cd ~ && git clone https://github.com/QuickGame2009/SM-T500-Windows-arm sm-t500-repo
   cd ~/sm-t500-repo/scripts/steamos
   ```
3. Instala las herramientas (adb y un contenedor con `hivex` para editar el registro de Windows). No toca SteamOS:
   ```
   ./00-preparar-steamdeck.sh
   ```

Los archivos necesarios ya están en `binarios/` del repositorio:

| Archivo | Para qué |
|---|---|
| `Mu-gta4lwifi-memdtb-xhci.img` | UEFI con USB (teclado/ratón) y el mapa de memoria corregido. **El bueno ahora mismo** |
| `Mu-gta4lwifi-memdtb.img` | UEFI sin USB, mapa de memoria corregido (vuelta atrás) |
| `volmgr-parcheado.sys`, `partmgr-parcheado.sys` | drivers de arranque parcheados y firmados |
| `keepalive.sys` | evita que la tablet se congele (Modo 23 = temporizador + hilo + lectura de disco + línea verde) |

Los logs y las copias de seguridad se guardan en `~/sm-t500/kit` y `~/sm-t500/trabajo`.

## Equivalencias

| Windows | Steam Deck |
|---|---|
| `3-massstorage.cmd` | `./03-massstorage.sh` |
| `17-patch-partmgr.cmd -Activar X -Keepalive 23 ...` | `./17-parchear.sh --activar X --keepalive 23 ...` |
| `18-leer-logs.cmd` | `./18-leer-logs.sh` |
| flashear el UEFI con adb + dd | `./flashear-uefi.sh ../../binarios/Mu-gta4lwifi-memdtb-xhci.img` |
| — | `./22-info-tactil.sh` (datos de la pantalla táctil, desde el recovery) |

`./17-parchear.sh --help` muestra todas las opciones. Ejemplo con el estado actual (USB + inicio de sesión automático + consola de rescate):

```
./17-parchear.sh --activar USBXHCI,UrsSynopsys,UrsCx01000,USBHUB3 --keepalive 23 --numproc 1 --arreglar-logon --consola
```

## Diferencias con la versión de Windows

- No cambia los tipos de partición en la GPT: Linux monta las particiones por número sin necesitarlo.
- El registro se edita con `hivex` sobre una copia, que solo se devuelve a la tablet si ha cambiado. Antes de cada cambio se guarda una copia de seguridad de `SYSTEM`, `SOFTWARE` y `BCD` en `~/sm-t500/trabajo/copia-FECHA`.
- Si Windows se apagó a la fuerza, la colmena aparece como "sucia". Los cambios de registro de esa última sesión que no se guardaron se pierden (normalmente no importa: son de un arranque fallido).
- Si SteamOS monta solo alguna partición de la tablet al conectarla, el script la desmonta primero. **Nunca aceptes formatear nada.**
