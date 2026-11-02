# Solo LECTURA. Vigila la tabla GPT del disco expuesto y anota CUANDO cambia (copia principal y de respaldo).
# Dejalo corriendo en una ventana mientras haces otras cosas en otra. Ctrl+C para parar.
# Uso (PowerShell como administrador):  powershell -ExecutionPolicy Bypass -File scripts\monitor-gpt.ps1 -Disk 5
param(
  [int]$Disk = 5, [int]$EverySeconds = 3, [int]$Count = 0,   # Count 0 = hasta Ctrl+C
  [string]$Path = '', [int64]$SizeBytes = 0
)
$ErrorActionPreference = 'Stop'
if (-not $Path) { $Path = "\\.\PhysicalDrive$Disk" }
if ($SizeBytes -eq 0) { $SizeBytes = (Get-Disk -Number $Disk).Size }
$lastLba = [int64]($SizeBytes / 512) - 1
$orig = 'D545D65E'   # CRC de entradas de la tabla ORIGINAL de la tablet
function Snap([int64]$lba) {
  try {
    $fs = New-Object System.IO.FileStream($Path, 'Open', 'Read', 'ReadWrite', 1)
    $fs.Seek($lba * 512, 'Begin') | Out-Null
    $b = New-Object byte[] 512; $got = 0
    while ($got -lt 512) { $n = $fs.Read($b, $got, 512 - $got); if ($n -le 0) { break }; $got += $n }
    $fs.Close()
    if ($got -lt 512 -or [System.Text.Encoding]::ASCII.GetString($b, 0, 8) -ne 'EFI PART') { return 'sin-cabecera' }
    return ('hdr={0:X8} entradas={1:X8} ultimoUsable={2}' -f [BitConverter]::ToUInt32($b, 16), [BitConverter]::ToUInt32($b, 88), [BitConverter]::ToInt64($b, 48))
  } catch { return 'ERROR: ' + $_.Exception.Message }
}
$prev = ''; $i = 0
Write-Host ("Vigilando {0} (cada {1} s). Tabla ORIGINAL = entradas {2}." -f $Path, $EverySeconds, $orig)
while ($true) {
  $p = Snap 1; $b = Snap $lastLba
  $now = "$p | RESPALDO $b"
  if ($now -ne $prev) {
    $tag = if ($p -like "*entradas=$orig*" -and $b -like "*entradas=$orig*") { 'ORIGINAL' } else { 'otra' }
    Write-Host ("{0}  [{1}]  PRINCIPAL {2}" -f (Get-Date -Format 'HH:mm:ss'), $tag, $now)
    $prev = $now
  }
  $i++; if ($Count -gt 0 -and $i -ge $Count) { break }
  Start-Sleep -Seconds $EverySeconds
}
