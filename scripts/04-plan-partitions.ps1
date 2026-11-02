# Calcula el plan de particionado a partir de la carpeta "info" que genera 01-info.ps1.
# NO toca la tablet: solo imprime (y guarda) los comandos de parted para que los revises.
# Uso:  .\4-plan.cmd      (o)   powershell -ExecutionPolicy Bypass -File scripts\04-plan-partitions.ps1 [-InfoDir info] [-UserdataMiB 4096] [-EspMiB 512]
#
# userdata se reduce a UserdataMiB y se crean esp (FAT32) y win (NTFS) con el resto.
# Requisito: userdata debe ser la ÚLTIMA partición del disco (lo habitual en Samsung).
param(
  [string]$InfoDir = (Join-Path (Get-Location).Path 'info'),
  [int]$UserdataMiB = 4096,
  [int]$EspMiB = 512
)
. (Join-Path $PSScriptRoot 'common.ps1')

$SecMiB = [int64]2048   # sectores de 512 B por MiB
$partsFile = Join-Path $InfoDir 'partitions.txt'
$diskFile = Join-Path $InfoDir 'disk.txt'
if (-not (Test-Path $partsFile) -or -not (Test-Path $diskFile)) {
  throw "Falta $partsFile o $diskFile. Ejecuta antes 1-info.cmd."
}

$d = (Get-Content $diskFile | Select-Object -First 1).Trim() -split '\s+'
$disk = $d[0]; $diskSectors = [int64]$d[1]

$parts = @()
foreach ($line in Get-Content $partsFile) {
  if ($line.Trim() -match '^(\S+) (\S+) (\d+) (\d+)$') {
    $parts += [pscustomobject]@{ Name = $Matches[1]; Node = $Matches[2]; Start = [int64]$Matches[3]; Size = [int64]$Matches[4] }
  }
}
$ud = $parts | Where-Object { $_.Name -eq 'userdata' } | Select-Object -First 1
if (-not $ud) { throw 'No encuentro la partición userdata en partitions.txt.' }

$start = $ud.Start
$end = $ud.Start + $ud.Size - 1
$num = [int]([regex]::Match($ud.Node, '(\d+)$').Groups[1].Value)

$maxStart = ($parts | Measure-Object -Property Start -Maximum).Maximum
if ($start -ne $maxStart) {
  throw "userdata no es la última partición (empieza en $start; la última empieza en $maxStart). No genero un plan automático: envíame la carpeta info y lo reviso a mano."
}

$udEnd = $start + [int64]$UserdataMiB * $SecMiB - 1
$espStart = [int64]([math]::Ceiling(($udEnd + 1) / $SecMiB)) * $SecMiB   # alinear a 1 MiB hacia arriba
$espEnd = $espStart + [int64]$EspMiB * $SecMiB - 1
$winStart = $espEnd + 1
$winEnd = $end
$winMiB = [int64][math]::Floor(($winEnd - $winStart + 1) / $SecMiB)

if ($udEnd -ge $end) {
  throw "userdata actual ($([int64][math]::Floor($ud.Size / $SecMiB)) MiB) no es mayor que lo pedido ($UserdataMiB MiB). Reduce -UserdataMiB."
}
if ($winMiB -lt 20480) {
  throw "Solo quedarían $winMiB MiB para Windows (mínimo 20480). Reduce -UserdataMiB."
}

$n1 = $num + 1; $n2 = $num + 2
$lines = @()
$lines += "# Disco: /dev/block/$disk  ($diskSectors sectores)"
$lines += "# userdata actual: partición $num, sectores $start-$end ($([int64][math]::Floor($ud.Size / $SecMiB)) MiB)"
if (($start % $SecMiB) -ne 0) {
  $lines += '# AVISO: userdata no empieza alineada a 1 MiB; parted puede avisar. Responde Ignore solo si el inicio coincide con el original.'
}
$lines += ''
$lines += '# ---- Plan nuevo (sectores de 512 B)'
$lines += "#   userdata  $start-$udEnd  ($UserdataMiB MiB)"
$lines += "#   esp       $espStart-$espEnd  ($EspMiB MiB)"
$lines += "#   win       $winStart-$winEnd  ($winMiB MiB = $([int64][math]::Floor($winMiB / 1024)) GiB)"
$lines += ''
$lines += '# ---- Pasos. Ponte en la carpeta donde estén los ficheros "parted" y "gdisk" (ARM64).'
$lines += '# En TWRP, con TODO desmontado (Montar > desmarcar todo):'
$lines += 'adb shell mkdir -p /workspace'
$lines += 'adb push parted /workspace/'
$lines += 'adb push gdisk /workspace/'
$lines += 'adb shell chmod 744 /workspace/parted /workspace/gdisk'
$lines += 'adb shell'
$lines += '  cd /workspace'
$lines += "  ./parted /dev/block/$disk"
$lines += '    (parted) unit s'
$lines += "    (parted) print                      # comprueba que userdata es la $num y que los numeros coinciden con lo de arriba"
$lines += "    (parted) rm $num"
$lines += "    (parted) mkpart userdata ext4 $($start)s $($udEnd)s"
$lines += "    (parted) mkpart esp fat32 $($espStart)s $($espEnd)s"
$lines += "    (parted) mkpart win ntfs $($winStart)s $($winEnd)s"
$lines += "    (parted) print                      # deberian ser las particiones $num, $n1 y $n2"
$lines += "    (parted) set $n1 esp on"
$lines += '    (parted) quit'
$lines += '  reboot recovery'
$lines += ''
$lines += '# ---- Después, otra vez en TWRP, formatear'
$lines += 'adb shell'
$lines += '  mke2fs -t ext4 /dev/block/by-name/userdata'
$lines += '  mkfs.fat -F32 -s1 /dev/block/by-name/esp'
$lines += '  mkfs.ntfs -f /dev/block/by-name/win'

$plan = ($lines -join "`n") + "`n"
Save-Text (Join-Path $InfoDir 'plan-particionado.txt') $plan
Write-Host $plan
Write-Host "(Guardado también en $(Join-Path $InfoDir 'plan-particionado.txt'))"
