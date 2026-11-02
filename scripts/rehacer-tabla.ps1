# Rehace la tabla de particiones (ESP + Windows en lugar de userdata) y restaura los sectores de arranque.
# Sustituye a: diskpart (borrar, crear x2, set id) + restore-bootsector x2.
# Necesario cada vez que se entra al recovery, porque el reinicio revierte la tabla. Los datos de las particiones NO se tocan.
# Uso (PowerShell como administrador, con el disco expuesto):
#   powershell -ExecutionPolicy Bypass -File scripts\rehacer-tabla.ps1 -Disk 5
param([int]$Disk = 5, [string]$LetraEsp = 'U', [string]$LetraWin = 'R')
$ErrorActionPreference = 'Stop'
$espType = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
$userdata = '1b81e7e6-f50d-419b-a739-2aeef8da3335'
function Dp($lines) {
  $f = Join-Path $env:TEMP 'rehacer-dp.txt'
  Set-Content -Path $f -Value $lines -Encoding ASCII
  $out = & diskpart /s $f
  $out | ForEach-Object { Write-Host ("   dp> " + $_) }
  Remove-Item $f -ErrorAction SilentlyContinue
  return ($out -join "`n")
}
Write-Host "1) Poniendo el disco $Disk en linea y mirando la tabla actual..."
Dp @("select disk $Disk", 'online disk', 'attributes disk clear readonly') | Out-Null
Update-Disk -Number $Disk
$parts = Get-Partition -DiskNumber $Disk | Sort-Object Offset
$last = $parts[-1]
Write-Host ("   Ultima particion: n.{0}  {1} MiB  tipo {2}" -f $last.PartitionNumber, [int]($last.Size / 1MB), $last.GptType)
$hasEsp = $parts | Where-Object { $_.GptType -eq "{$espType}" -and $_.Offset -eq 7759462400 }
if ($hasEsp) { Write-Host "   La ESP ya existe en su sitio; no recreo la tabla. Solo compruebo los sectores de arranque."; $n = $hasEsp.PartitionNumber }
else {
  if ($last.GptType -ne "{$userdata}" -or $last.Offset -ne 7759462400) {
    throw "La ultima particion no es userdata en el offset esperado (7759462400). No toco nada."
  }
  $n = $last.PartitionNumber
  Write-Host "2) Borrando userdata (n.$n) y creando ESP + Windows..."
  $o = Dp @("select disk $Disk", "select partition $n", 'delete partition override',
            'create partition primary offset=7577600 size=512',
            'create partition primary offset=8101888 size=51727')
  if ($o -match 'rror|incorrecto|no se pudo') { throw 'diskpart dio un error; mira las lineas dp> de arriba. Detengo el proceso.' }
  Write-Host "3) Marcando la ESP..."
  Dp @("select disk $Disk", "select partition $n", "set id=$espType") | Out-Null
  Write-Host "4) Refrescando volumenes (offline/online)..."
  Dp @("select disk $Disk", 'offline disk') | Out-Null
  Dp @("select disk $Disk", 'online disk') | Out-Null
  Update-Disk -Number $Disk
}
Write-Host "5) Restaurando sector 0 de la ESP (n.$n) y de Windows (n.$($n+1))..."
$rb = Join-Path $PSScriptRoot 'restore-bootsector.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $rb -Disk $Disk -Partition $n -Type fat32 -Apply
& powershell -NoProfile -ExecutionPolicy Bypass -File $rb -Disk $Disk -Partition ($n + 1) -Type ntfs -Apply
Write-Host "6) Asignando letras: ESP = ${LetraEsp}:  Windows = ${LetraWin}:"
foreach ($pair in @(@($n, $LetraEsp), @(($n + 1), $LetraWin))) {
  $o = Dp @("select disk $Disk", ("select partition " + $pair[0]), ("assign letter=" + $pair[1]))
  if ($o -match 'rror|incorrecto|no se pudo') { Write-Host ("   AVISO: no se pudo asignar " + $pair[1] + ": (letra en uso o reservada). Prueba otra con -LetraEsp / -LetraWin.") }
}
# Las particiones nuevas tienen GUIDs nuevos: hay que reapuntar el BCD o queda con device/osdevice = unknown.
$bcd = "${LetraEsp}:\EFI\Microsoft\Boot\BCD"
if (Test-Path $bcd) {
  Write-Host "7) Reapuntando el BCD a las particiones actuales ($bcd)..."
  & bcdedit /store $bcd /set '{bootmgr}' device "partition=${LetraEsp}:"
  & bcdedit /store $bcd /set '{default}' device "partition=${LetraWin}:"
  & bcdedit /store $bcd /set '{default}' osdevice "partition=${LetraWin}:"
  & bcdedit /store $bcd /enum '{default}' | Select-String 'device'
} else { Write-Host "7) No encuentro $bcd; salto el reapuntado del BCD." }
Write-Host "Listo. Comprueba con: powershell -ExecutionPolicy Bypass -File scripts\diag-gpt.ps1 -Disk $Disk"
