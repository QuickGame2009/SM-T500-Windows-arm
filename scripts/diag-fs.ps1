# Solo LECTURA. Revisa el FAT32 de la ESP y el NTFS de win en el disco expuesto de la tablet,
# incluidas las COPIAS del sector de arranque (FAT32: sector 6; NTFS: ultimo sector de la particion).
# Uso (PowerShell como administrador):  powershell -ExecutionPolicy Bypass -File scripts\diag-fs.ps1 -Disk 5
param(
  [int]$Disk = 5,
  [string]$Path = '',
  [int64]$EspOffset = 7759462400,  [int64]$EspSize = 536870912,
  [int64]$WinOffset = 8296333312,  [int64]$WinSize = 54239690752
)
$ErrorActionPreference = 'Stop'
if (-not $Path) {
  $Path = "\\.\PhysicalDrive$Disk"
  try {
    $parts = Get-Partition -DiskNumber $Disk
    $e = $parts | Where-Object { $_.Offset -eq $EspOffset } | Select-Object -First 1
    $w = $parts | Where-Object { $_.Offset -eq $WinOffset } | Select-Object -First 1
    if ($e) { $EspSize = [int64]$e.Size }
    if ($w) { $WinSize = [int64]$w.Size }
  } catch { Write-Host "No pude leer Get-Partition, uso tamanos por defecto." }
}
Write-Host ("Particiones: ESP offset {0} tam {1} | win offset {2} tam {3}" -f $EspOffset, $EspSize, $WinOffset, $WinSize)
$fs = New-Object System.IO.FileStream($Path, 'Open', 'Read', 'ReadWrite', 1)
function ReadAt([int64]$off, [int]$len = 512) {
  $len = [int]([math]::Ceiling($len / 512.0) * 512)
  try {
    $fs.Seek($off, 'Begin') | Out-Null
    $buf = New-Object byte[] $len; $got = 0
    while ($got -lt $len) { $n = $fs.Read($buf, $got, $len - $got); if ($n -le 0) { break }; $got += $n }
    if ($got -lt $len) { return $null }
    return ,$buf
  } catch { Write-Host ("   ERROR de lectura en byte {0}: {1}" -f $off, $_.Exception.Message); return $null }
}
function Asc($b, $o, $n) { ([System.Text.Encoding]::ASCII.GetString($b, $o, $n)) -replace '[^\x20-\x7E]', '?' }
function Nonzero($b) { if (-not $b) { return -1 }; $c = 0; foreach ($x in $b) { if ($x -ne 0) { $c++ } }; return $c }

function Show-Wipe([string]$label, [int64]$off) {
  $m = ReadAt $off 1048576
  if (-not $m) { Write-Host "   ${label}: no pude leer el primer MiB"; return }
  $first = -1
  for ($s = 0; $s -lt 2048; $s++) { $nz = $false; for ($i = $s * 512; $i -lt ($s + 1) * 512; $i++) { if ($m[$i] -ne 0) { $nz = $true; break } }; if ($nz) { $first = $s; break } }
  $cnt = Nonzero $m
  Write-Host ("   {0}: bytes distintos de cero en el primer MiB = {1}; primer sector con datos = {2}" -f $label, $cnt, $(if ($first -lt 0) { 'ninguno (todo ceros)' } else { $first }))
}

function Parse-Fat([byte[]]$b, [string]$origen) {
  $bps = [BitConverter]::ToUInt16($b, 11); $spc = $b[13]; $res = [BitConverter]::ToUInt16($b, 14); $nf = $b[16]
  $hid = [BitConverter]::ToUInt32($b, 28); $tot = [BitConverter]::ToUInt32($b, 32); $fsz = [BitConverter]::ToUInt32($b, 36)
  $root = [BitConverter]::ToUInt32($b, 44)
  Write-Host ("   [{0}] OEM='{1}' FAT='{2}' bytes/sector={3} sect/cluster={4} reservados={5} FATs={6} ocultos={7} sectoresTotales={8} tamFAT={9} raiz={10}" -f $origen, (Asc $b 3 8), (Asc $b 82 8), $bps, $spc, $res, $nf, $hid, $tot, $fsz, $root)
  Write-Host ("   Tamano del FS {0} bytes; la particion mide {1}: {2}" -f ($tot * 512), $EspSize, $(if ($tot * 512 -le $EspSize) { 'cabe' } else { 'NO CABE' }))
  if ($bps -eq 512 -and $spc -gt 0 -and $nf -gt 0) {
    $f = ReadAt ($EspOffset + $res * 512); if ($f) { Write-Host ("   Inicio de la FAT: {0:X2} {1:X2} {2:X2} {3:X2} (esperado F8 FF FF 0F)" -f $f[0], $f[1], $f[2], $f[3]) }
    $dataStart = $res + $nf * $fsz
    $r = ReadAt ($EspOffset + ($dataStart + ($root - 2) * $spc) * 512); if ($r) { Write-Host ("   Raiz del FS (primeros 11 bytes): '{0}'" -f (Asc $r 0 11)) }
  }
}
function Parse-Ntfs([byte[]]$b, [string]$origen) {
  $bps = [BitConverter]::ToUInt16($b, 11); $spc = $b[13]; $hid = [BitConverter]::ToUInt32($b, 28)
  $tot = [BitConverter]::ToInt64($b, 40); $mft = [BitConverter]::ToInt64($b, 48); $mir = [BitConverter]::ToInt64($b, 56)
  Write-Host ("   [{0}] OEM='{1}' bytes/sector={2} sect/cluster={3} ocultos={4} sectoresTotales={5} MFT en cluster {6}, MFTMirr {7}" -f $origen, (Asc $b 3 8), $bps, $spc, $hid, $tot, $mft, $mir)
  Write-Host ("   Tamano del FS {0} bytes; la particion mide {1}: {2}" -f (($tot + 1) * 512), $WinSize, $(if (($tot + 1) * 512 -le $WinSize) { 'cabe' } else { 'NO CABE' }))
  if ($bps -eq 512 -and $spc -gt 0 -and $mft -gt 0) {
    $m = ReadAt ($WinOffset + $mft * $spc * $bps); if ($m) { Write-Host ("   Primer registro de la MFT: '{0}' (esperado FILE)" -f (Asc $m 0 4)) }
    $mm = ReadAt ($WinOffset + $mir * $spc * $bps); if ($mm) { Write-Host ("   MFT espejo: '{0}' (esperado FILE)" -f (Asc $mm 0 4)) }
  }
}

Write-Host "=== ESP (FAT32)"
Show-Wipe 'ESP' $EspOffset
$b = ReadAt $EspOffset
if ($b -and (Asc $b 82 5) -eq 'FAT32') { Parse-Fat $b 'sector 0' } else { Write-Host "   Sector 0 sin FAT32." }
$bk = ReadAt ($EspOffset + 6 * 512)
if ($bk -and (Asc $bk 82 5) -eq 'FAT32') { Write-Host "   COPIA en sector 6: ENCONTRADA"; Parse-Fat $bk 'copia sector 6' } else { Write-Host "   Copia en sector 6: no hay FAT32 ('$(if($bk){Asc $bk 82 8})')" }

Write-Host "=== win (NTFS)"
Show-Wipe 'win' $WinOffset
$b = ReadAt $WinOffset
if ($b -and (Asc $b 3 4) -eq 'NTFS') { Parse-Ntfs $b 'sector 0' } else { Write-Host "   Sector 0 sin NTFS." }
$bk = ReadAt ($WinOffset + $WinSize - 512)
if ($bk -and (Asc $bk 3 4) -eq 'NTFS') { Write-Host "   COPIA en el ultimo sector de la particion: ENCONTRADA"; Parse-Ntfs $bk 'copia final' } else { Write-Host "   Copia en el ultimo sector: no hay NTFS ('$(if($bk){Asc $bk 3 8})')" }
$fs.Close()
