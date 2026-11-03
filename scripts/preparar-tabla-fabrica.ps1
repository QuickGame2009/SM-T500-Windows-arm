# PLAN B: prepara Windows SIN tocar la tabla de particiones (la tablet la restaura de fabrica en cada arranque normal).
#   - cache    (200 MiB)  -> se formatea FAT32 y hace de ESP
#   - userdata (51 GiB)   -> se formatea NTFS y hace de Windows
# No cambia ningun tipo ni nombre de particion: solo formatea y asigna letras.
# Uso (PowerShell como administrador, con el disco expuesto por Lineage y la tabla de FABRICA):
#   powershell -ExecutionPolicy Bypass -File scripts\preparar-tabla-fabrica.ps1 -Disk 5 -LetraEsp O -LetraWin R
param([int]$Disk = 5, [string]$LetraEsp = 'O', [string]$LetraWin = 'R')
$ErrorActionPreference = 'Stop'

$espOff  = [int64]14548992 * 512      # cache
$espSize = [int64]409600 * 512
$winOff  = [int64]15155200 * 512      # userdata
$winSize = [int64]106987487 * 512

function Dp($lines) {
  $f = Join-Path $env:TEMP 'preparar-dp.txt'
  Set-Content -Path $f -Value $lines -Encoding ASCII
  $out = & diskpart /s $f
  $out | ForEach-Object { Write-Host ("   dp> " + $_) }
  Remove-Item $f -ErrorAction SilentlyContinue
  return ($out -join "`n")
}

Write-Host "1) Poniendo el disco $Disk en linea y mirando la tabla..."
Dp @("select disk $Disk", 'online disk', 'attributes disk clear readonly') | Out-Null
Update-Disk -Number $Disk
$d = Get-Disk -Number $Disk
if ($d.Size -ne ([int64]122142720 * 512)) { throw ("El disco $Disk no mide lo que la tablet (" + $d.Size + " bytes). No toco nada.") }
$parts = Get-Partition -DiskNumber $Disk
$esp = $parts | Where-Object { [int64]$_.Offset -eq $espOff } | Select-Object -First 1
$win = $parts | Where-Object { [int64]$_.Offset -eq $winOff } | Select-Object -First 1
if (-not $esp -or -not $win) { throw 'No encuentro cache/userdata en su sitio: la tabla no es la de fabrica. No toco nada (entra a Lineage por el arranque normal, que la deja de fabrica).' }
if ([math]::Abs([int64]$esp.Size - $espSize) -gt 1MB -or [math]::Abs([int64]$win.Size - $winSize) -gt 1MB) { throw 'cache o userdata no tienen el tamano esperado. No toco nada.' }
Write-Host ("   cache    = particion {0}, {1} MiB  -> ESP FAT32" -f $esp.PartitionNumber, [int]($esp.Size / 1MB))
Write-Host ("   userdata = particion {0}, {1} MiB  -> Windows NTFS" -f $win.PartitionNumber, [int]($win.Size / 1MB))

Write-Host ''
Write-Host 'Se van a FORMATEAR esas dos particiones (se pierden sus datos). La tabla no se modifica.'
$ans = Read-Host 'Escribe SI para continuar'
if ($ans -ne 'SI') { Write-Host 'Cancelado.'; return }

Write-Host "2) Formateando cache como FAT32 (ESP) y asignando ${LetraEsp}:..."
$o = Dp @("select disk $Disk", ("select partition " + $esp.PartitionNumber), 'format fs=fat32 quick label=ESP override', ("assign letter=" + $LetraEsp))
if ($o -match 'rror|incorrecto|no se pudo|no es v') { throw 'diskpart dio un error con la ESP; mira las lineas dp> de arriba. Detengo el proceso.' }

Write-Host "3) Formateando userdata como NTFS (Windows) y asignando ${LetraWin}:..."
$o = Dp @("select disk $Disk", ("select partition " + $win.PartitionNumber), 'format fs=ntfs quick label=Windows override', ("assign letter=" + $LetraWin))
if ($o -match 'rror|incorrecto|no se pudo|no es v') { throw 'diskpart dio un error con Windows; mira las lineas dp> de arriba. Detengo el proceso.' }

Write-Host ''
foreach ($l in @($LetraEsp, $LetraWin)) {
  if (Test-Path "${l}:\") { Write-Host ("   {0}: OK" -f $l) } else { Write-Host ("   AVISO: {0}: no aparece; asignala a mano con diskpart." -f $l) }
}
Write-Host ''
Write-Host 'Listo. Siguiente: windows\apply-windows.cmd <install.wim> <letra_win> <letra_esp> 1'
Write-Host ("  ejemplo: windows\apply-windows.cmd C:\ruta\install.wim {0} {1} 1" -f $LetraWin, $LetraEsp)
