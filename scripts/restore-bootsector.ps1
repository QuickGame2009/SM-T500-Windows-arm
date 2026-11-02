# Restaura el sector de arranque (sector 0) de un volumen FAT32 o NTFS a partir de su copia de seguridad.
# Sin -Apply solo MUESTRA lo que haria (no escribe nada).
# Uso (PowerShell como administrador):
#   powershell -ExecutionPolicy Bypass -File scripts\restore-bootsector.ps1 -Letter S -Type fat32
#   powershell -ExecutionPolicy Bypass -File scripts\restore-bootsector.ps1 -Letter R -Type ntfs
#   (y con -Apply para escribir de verdad)
param(
  [string]$Letter = '',
  [int]$Disk = -1, [int]$Partition = -1,   # alternativa a -Letter: -Disk 5 -Partition 80
  [ValidateSet('fat32', 'ntfs')][string]$Type = 'ntfs',
  [switch]$Apply,
  [string]$ImagePath = '', [int64]$ImageOffset = 0, [int64]$ImageSize = 0   # solo para pruebas con un fichero
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @"
using System; using System.ComponentModel; using System.Runtime.InteropServices; using Microsoft.Win32.SafeHandles;
public static class VolIO {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern bool DeviceIoControl(SafeFileHandle h, uint code, IntPtr inb, uint inl, IntPtr outb, uint outl, out uint ret, IntPtr ov);
  public static SafeFileHandle Open(string n, bool write) {
    uint acc = write ? 0xC0000000u : 0x80000000u;
    SafeFileHandle h = CreateFile(n, acc, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
    if (h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
    return h;
  }
  public static void Ioctl(SafeFileHandle h, uint code) {
    uint r; if (!DeviceIoControl(h, code, IntPtr.Zero, 0, IntPtr.Zero, 0, out r, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
  }
}
"@

function Asc($b, $o, $n) { ([System.Text.Encoding]::ASCII.GetString($b, $o, $n)) -replace '[^\x20-\x7E]', '?' }
function Sig($b) { if ($Type -eq 'fat32') { (Asc $b 82 5) -eq 'FAT32' } else { (Asc $b 3 4) -eq 'NTFS' } }

$locked = $false; $handle = $null
if ($ImagePath) {
  $fs = New-Object System.IO.FileStream($ImagePath, 'Open', $(if ($Apply) { 'ReadWrite' } else { 'Read' }), 'ReadWrite', 1)
  $base = $ImageOffset; $size = $ImageSize
} else {
  if ($Disk -ge 0 -and $Partition -ge 0) {
    $pt = Get-Partition -DiskNumber $Disk -PartitionNumber $Partition
    $size = [int64]$pt.Size
    $vol = $pt.AccessPaths | Where-Object { $_ -like '\\?\Volume*' } | Select-Object -First 1
    if (-not $vol) { throw 'La particion no tiene ruta de volumen. Haz offline disk / online disk en diskpart y reintenta.' }
    $volPath = $vol.TrimEnd('\')
    $Letter = ''
  } else {
    if (-not $Letter) { throw 'Indica -Letter R (por ejemplo) o bien -Disk 5 -Partition 80.' }
    $Letter = $Letter.TrimEnd(':').ToUpper()
    $size = [int64](Get-Partition -DriveLetter $Letter).Size
    $volPath = "\\.\$($Letter):"
  }
  $handle = [VolIO]::Open($volPath, [bool]$Apply)
  $fs = New-Object System.IO.FileStream($handle, $(if ($Apply) { 'ReadWrite' } else { 'Read' }), 1, $false)
  $base = 0
  if ($Apply) {
    [VolIO]::Ioctl($handle, 0x00090018)   # FSCTL_LOCK_VOLUME
    [VolIO]::Ioctl($handle, 0x00090020)   # FSCTL_DISMOUNT_VOLUME
    $locked = $true
  }
}
function ReadSec([int64]$off) {
  $fs.Seek($base + $off, 'Begin') | Out-Null
  $buf = New-Object byte[] 512; $got = 0
  while ($got -lt 512) { $n = $fs.Read($buf, $got, 512 - $got); if ($n -le 0) { throw "No pude leer el byte $off" }; $got += $n }
  return ,$buf
}

try {
  Write-Host ("Volumen: {0}  Tipo: {1}  Tamano de la particion: {2} bytes" -f $(if ($ImagePath) { $ImagePath } elseif ($Letter) { $Letter + ':' } else { $volPath }), $Type, $size)
  $zero = New-Object byte[] 512
  $s0 = ReadSec 0
  $isZero = (-not (Compare-Object $s0 $zero -SyncWindow 0))
  Write-Host ("Sector 0 actual: {0}" -f $(if ($isZero) { 'todo ceros' } elseif (Sig $s0) { 'ya tiene un sector de arranque valido' } else { 'datos desconocidos' }))
  $bkOff = if ($Type -eq 'fat32') { 6 * 512 } else { $size - 512 }
  $bk = ReadSec $bkOff
  if (-not (Sig $bk)) { throw ("La copia (byte {0}) NO tiene firma {1}. No escribo nada." -f $bkOff, $Type) }
  Write-Host ("Copia encontrada en el byte {0}: OEM='{1}'" -f $bkOff, (Asc $bk 3 8))
  if (-not $isZero -and -not (Sig $s0)) { throw 'El sector 0 tiene datos desconocidos. No escribo nada.' }
  if (-not $isZero -and (Sig $s0)) { Write-Host 'El sector 0 ya es valido; no hay nada que restaurar.'; return }
  if (-not $Apply) { Write-Host 'SIMULACION: con -Apply copiaria esa copia al sector 0. No se ha escrito nada.'; return }
  $fs.Seek($base, 'Begin') | Out-Null
  $fs.Write($bk, 0, 512); $fs.Flush()
  $chk = ReadSec 0
  if (-not (Compare-Object $chk $bk -SyncWindow 0)) { Write-Host 'HECHO: sector 0 restaurado y verificado.' } else { throw 'La verificacion fallo: el sector 0 no coincide con la copia.' }
} finally {
  if ($locked) { try { [VolIO]::Ioctl($handle, 0x0009001C) } catch { } }   # FSCTL_UNLOCK_VOLUME
  $fs.Close()
}
