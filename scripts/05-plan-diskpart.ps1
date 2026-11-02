# Calcula los comandos de diskpart (Windows) para crear las particiones esp y win. NO toca la tablet.
# Se usa DESPUÉS de exponer la tablet como disco USB con 3-massstorage.cmd. No necesita parted ni gdisk.
# Uso:  .\4-plan.cmd      (o)   powershell -ExecutionPolicy Bypass -File scripts\05-plan-diskpart.ps1 [-InfoDir info] [-EspMiB 512]
#
# Se BORRA la partición userdata (datos de Android) y su espacio se reparte entre esp (FAT32) y win (NTFS).
param(
  [string]$InfoDir = (Join-Path (Get-Location).Path 'info'),
  [int]$EspMiB = 512
)
. (Join-Path $PSScriptRoot 'common.ps1')

$SecMiB = [int64]2048
$partsFile = Join-Path $InfoDir 'partitions.txt'
$diskFile = Join-Path $InfoDir 'disk.txt'
if (-not (Test-Path $partsFile) -or -not (Test-Path $diskFile)) { throw "Falta $partsFile o $diskFile. Ejecuta antes 1-info.cmd." }

$parts = @()
foreach ($line in Get-Content $partsFile) {
  if ($line.Trim() -match '^(\S+) (\S+) (\d+) (\d+)$') {
    $parts += [pscustomobject]@{ Name = $Matches[1]; Start = [int64]$Matches[3]; Size = [int64]$Matches[4] }
  }
}
$ud = $parts | Where-Object { $_.Name -eq 'userdata' } | Select-Object -First 1
if (-not $ud) { throw 'No encuentro userdata en partitions.txt.' }
$maxStart = ($parts | Measure-Object -Property Start -Maximum).Maximum
if ($ud.Start -ne $maxStart) { throw 'userdata no es la última partición. Envíame la carpeta info y lo reviso a mano.' }
if (($ud.Start % $SecMiB) -ne 0) { throw 'userdata no empieza alineada a 1 MiB. Envíame la carpeta info.' }

$udEnd = $ud.Start + $ud.Size - 1
$espStart = $ud.Start
$winStart = $espStart + [int64]$EspMiB * $SecMiB
$winMiB = [int64][math]::Floor(($udEnd - $winStart + 1) / $SecMiB)
if ($winMiB -lt 20480) { throw "Solo quedarían $winMiB MiB para Windows (mínimo 20480)." }
$udMiB = [int64][math]::Floor($ud.Size / $SecMiB)
$espOffKB = $espStart / 2
$winOffKB = $winStart / 2

$t = @"
# userdata actual: empieza en el sector $($ud.Start), unos $udMiB MiB. Es la ÚLTIMA partición del disco.
# Nuevo reparto (se borra userdata):
#   esp  $EspMiB MiB, FAT32, empieza en el sector $espStart
#   win  $winMiB MiB (unos $([int64][math]::Floor($winMiB / 1024)) GiB), NTFS, empieza en el sector $winStart

# 1) Abre "Símbolo del sistema" COMO ADMINISTRADOR y escribe:
diskpart
  list disk
  select disk N                  <- el disco de unos 58 GiB que apareció al exponer la tablet. COMPRUEBA EL TAMAÑO; si eliges tu disco del PC lo borras.
  list partition
  select partition P             <- la ÚLTIMA de la lista, de unos $udMiB MiB (~$([math]::Round($udMiB/1024)) GB), tipo "Desconocido".
  detail partition               <- debe poner Offset $espOffKB KB (o equivalente en MB/GB). Si no coincide, PARA.
  delete partition override
  create partition efi offset=$espOffKB size=$EspMiB
  format fs=fat32 quick label=ESP
  assign letter=S
  create partition primary offset=$winOffKB size=$winMiB
  format fs=ntfs quick label=WIN
  assign letter=R
  list partition
  exit

# 2) Instala Windows:
windows\apply-windows.cmd C:\ruta\install.wim R S 1

# Nota: diskpart no puede poner nombre a las particiones (en Android se llamarían "esp"/"win").
# No hace falta para Windows. Sin userdata, Android no arrancará; para volver a Android hay que
# restaurar el firmware de fábrica con Odin.
"@
Save-Text (Join-Path $InfoDir 'plan-diskpart.txt') $t
Write-Host $t
Write-Host "(Guardado también en $(Join-Path $InfoDir 'plan-diskpart.txt'))"
