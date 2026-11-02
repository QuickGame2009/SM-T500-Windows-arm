# Solo LECTURA. Compara las dos copias de la tabla GPT del disco de la tablet y mira si los datos de ESP y win siguen ahi.
# Uso (PowerShell como administrador, con el disco de la tablet expuesto):
#   powershell -ExecutionPolicy Bypass -File scripts\diag-gpt.ps1 -Disk 5
param(
  [int]$Disk = 5,
  [string]$Path = '',
  [int64]$SizeBytes = 0,
  [int64]$EspOffset = 7759462400,   # 7400 MiB
  [int64]$WinOffset = 8296333312    # 8101888 KiB
)
$ErrorActionPreference = 'Stop'
if (-not $Path) { $Path = "\\.\PhysicalDrive$Disk" }
if ($SizeBytes -eq 0) { $SizeBytes = (Get-Disk -Number $Disk).Size }
$lastLba = [int64]($SizeBytes / 512) - 1
# bufferSize 1 = sin buffer: con el buffer por defecto (4096) la lectura del ultimo sector se pasa del final del disco
$fs = New-Object System.IO.FileStream($Path, 'Open', 'Read', 'ReadWrite', 1)

function Read-Bytes([int64]$off, [int]$len) {
  $fs.Seek($off, 'Begin') | Out-Null
  $len = [int]([math]::Ceiling($len / 512.0) * 512)
  $buf = New-Object byte[] $len
  $got = 0
  while ($got -lt $len) { $n = $fs.Read($buf, $got, $len - $got); if ($n -le 0) { break }; $got += $n }
  return ,$buf
}
function Show-Gpt([string]$label, [int64]$lba) {
  Write-Host "=== $label (LBA $lba)"
  $h = Read-Bytes ($lba * 512) 512
  $sig = [System.Text.Encoding]::ASCII.GetString($h, 0, 8)
  if ($sig -ne 'EFI PART') { Write-Host "   NO hay cabecera GPT valida (firma: '$sig')"; return $null }
  $o = [pscustomobject]@{
    Current = [BitConverter]::ToInt64($h, 24); Backup = [BitConverter]::ToInt64($h, 32)
    FirstUsable = [BitConverter]::ToInt64($h, 40); LastUsable = [BitConverter]::ToInt64($h, 48)
    DiskGuid = ([guid][byte[]]$h[56..71]).ToString(); EntriesLba = [BitConverter]::ToInt64($h, 72)
    NumEntries = [BitConverter]::ToInt32($h, 80); EntrySize = [BitConverter]::ToInt32($h, 84)
    EntriesCrc = '{0:X8}' -f [BitConverter]::ToUInt32($h, 88)
  }
  Write-Host ('   Disk GUID {0}  Ultimo usable {1}  CRC entradas {2}' -f $o.DiskGuid, $o.LastUsable, $o.EntriesCrc)
  $e = Read-Bytes ($o.EntriesLba * 512) ($o.NumEntries * $o.EntrySize)
  $list = @()
  for ($i = 0; $i -lt $o.NumEntries; $i++) {
    $b = $i * $o.EntrySize
    $type = [guid][byte[]]$e[$b..($b + 15)]
    if ($type -eq [guid]::Empty) { continue }
    $first = [BitConverter]::ToInt64($e, $b + 32); $last = [BitConverter]::ToInt64($e, $b + 40)
    $name = [System.Text.Encoding]::Unicode.GetString($e, $b + 56, 72).Trim([char]0)
    $list += [pscustomobject]@{ N = $i + 1; Nombre = $name; Tipo = $type.ToString(); Inicio = $first; Fin = $last; MiB = [math]::Round(($last - $first + 1) / 2048) }
  }
  Write-Host ("   Particiones definidas: {0}. Ultimas 4:" -f $list.Count)
  foreach ($x in ($list | Select-Object -Last 4)) {
    Write-Host ('   #{0,-3} {1,-10} {2,8} MiB  sectores {3}-{4}  tipo {5}' -f $x.N, $x.Nombre, $x.MiB, $x.Inicio, $x.Fin, $x.Tipo)
  }
  return $o
}
$p = Show-Gpt 'GPT primaria' 1
$b = Show-Gpt 'GPT de respaldo' $lastLba

Write-Host '=== Contenido en las zonas de ESP y win'
function Probe([string]$label, [int64]$off) {
  $s = Read-Bytes $off 2048
  $oem = ([System.Text.Encoding]::ASCII.GetString($s, 3, 8)) -replace '[^\x20-\x7E]', '?'
  $fat = ([System.Text.Encoding]::ASCII.GetString($s, 82, 8)) -replace '[^\x20-\x7E]', '?'
  $ext = ('{0:X2}{1:X2}' -f $s[1081], $s[1080])
  Write-Host ("   {0} (byte {1}): OEM='{2}'  FAT='{3}'  ext4magic={4}" -f $label, $off, $oem, $fat, $ext)
}
Probe 'ESP (esperado FAT32)' $EspOffset
Probe 'win (esperado NTFS)' $WinOffset
$fs.Close()
