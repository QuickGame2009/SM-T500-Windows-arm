# Guarda una copia de la tabla GPT buena (ESP + Windows) en 67 sectores LIBRES del final del disco (fuera de toda particion).
# La UEFI (compilada con diag=si) la compara al arrancar y, si el recovery ha revertido la tabla, la restaura y reinicia.
# Uso (PowerShell como administrador, despues de rehacer-tabla.ps1; ya lo llama el propio rehacer-tabla.ps1):
#   powershell -ExecutionPolicy Bypass -File scripts\guardar-gpt.ps1 -Disk 5
param([int]$Disk = 5, [int64]$BlobLba = 122140680, [switch]$SoloComprobar)
$ErrorActionPreference = 'Stop'
$NP = 33; $NB = 33
$espType = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
Add-Type -TypeDefinition @"
using System; using System.ComponentModel; using System.Runtime.InteropServices; using Microsoft.Win32.SafeHandles;
public static class RawIO {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
  public static SafeFileHandle Open(string n, bool write) {
    uint acc = write ? 0xC0000000u : 0x80000000u;
    SafeFileHandle h = CreateFile(n, acc, 3, IntPtr.Zero, 3, 0x80000000u, IntPtr.Zero);  // WRITE_THROUGH
    if (h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
    return h;
  }
}
"@
$path = "\\.\PhysicalDrive$Disk"
$total = [int64](Get-Disk -Number $Disk).Size / 512
$last = $total - 1
function LeerSec($fs, [int64]$lba, [int]$n) {
  $fs.Seek($lba * 512, 'Begin') | Out-Null
  $buf = New-Object byte[] ($n * 512); $got = 0
  while ($got -lt $buf.Length) { $r = $fs.Read($buf, $got, $buf.Length - $got); if ($r -le 0) { break }; $got += $r }
  if ($got -ne $buf.Length) { throw "Lectura corta en LBA $lba" }
  return ,$buf
}
$h = [RawIO]::Open($path, $false)
$fs = New-Object System.IO.FileStream($h, 'Read', 1, $false)
$prim = LeerSec $fs 1 $NP
$back = LeerSec $fs ($last - ($NB - 1)) $NB
$old = LeerSec $fs $BlobLba (1 + $NP + $NB)
$fs.Dispose()

if ([System.Text.Encoding]::ASCII.GetString($prim, 0, 8) -ne 'EFI PART') { throw 'No hay cabecera GPT primaria valida.' }
if ([System.Text.Encoding]::ASCII.GetString($back, 512 * ($NB - 1), 8) -ne 'EFI PART') { throw 'No hay cabecera GPT de respaldo valida.' }
if ([BitConverter]::ToInt64($prim, 32) -ne $last) { throw "La cabecera primaria no apunta al ultimo sector ($last)." }
$firstUsable = [BitConverter]::ToInt64($prim, 40); $lastUsable = [BitConverter]::ToInt64($prim, 48)
$nEnt = [BitConverter]::ToInt32($prim, 80); $eSz = [BitConverter]::ToInt32($prim, 84)
if ($eSz -ne 128 -or $nEnt * $eSz -gt 512 * ($NP - 1)) { throw 'Tamano de la tabla de entradas inesperado.' }
$maxEnd = 0; $minStart = [int64]::MaxValue; $hasEsp = $false; $espGuid = ([guid]$espType).ToByteArray()
for ($i = 0; $i -lt $nEnt; $i++) {
  $o = 512 + $i * 128
  $t = [byte[]]$prim[$o..($o + 15)]
  if (-not ($t | Where-Object { $_ -ne 0 })) { continue }
  $end = [BitConverter]::ToInt64($prim, $o + 40)
  if ($end -gt $maxEnd) { $maxEnd = $end }
  $st = [BitConverter]::ToInt64($prim, $o + 32); if ($st -lt $minStart) { $minStart = $st }
  if (-not (Compare-Object $t $espGuid)) { $hasEsp = $true }
}
Write-Host ("Disco {0}: {1} sectores. Ultima particion termina en {2}. Ultimo usable {3}. Copia en LBA {4}..{5}." -f $Disk, $total, $maxEnd, $lastUsable, $BlobLba, ($BlobLba + $NP + $NB))
if ($minStart -lt 34) { throw "Hay una particion que empieza antes del sector 34 ($minStart): la copia de la GPT pisaria datos. No guardo nada." }
if ($maxEnd -ge ($last - ($NB - 1))) { throw 'Hay una particion que llega a la zona de la GPT de respaldo. No guardo nada.' }
if (-not $hasEsp) { throw 'La tabla actual no tiene la ESP: no es la tabla de Windows. No guardo nada.' }
if ($BlobLba -le $maxEnd -or ($BlobLba + $NP + $NB) -gt $lastUsable) { throw "Los sectores de la copia ($BlobLba..) no estan libres (fuera de las particiones). No escribo." }

$blob = New-Object byte[] (512 * (1 + $NP + $NB))
[System.Text.Encoding]::ASCII.GetBytes('T500GPT1').CopyTo($blob, 0)
[BitConverter]::GetBytes([uint32]1).CopyTo($blob, 8)
[BitConverter]::GetBytes([uint32]$NP).CopyTo($blob, 12)
[BitConverter]::GetBytes([uint32]$NB).CopyTo($blob, 16)
[BitConverter]::GetBytes([uint32]0).CopyTo($blob, 20)
[BitConverter]::GetBytes([int64]$last).CopyTo($blob, 24)
$prim.CopyTo($blob, 512)
$back.CopyTo($blob, 512 + 512 * $NP)
if ($SoloComprobar) { Write-Host 'Comprobacion correcta (no se ha escrito nada).'; return }

# Con el disco offline, Windows deja escribir sectores en bruto fuera de los volumenes.
$f = Join-Path $env:TEMP 'guardar-gpt-dp.txt'
Set-Content -Path $f -Value @("select disk $Disk", 'offline disk') -Encoding ASCII
& diskpart /s $f | Out-Null; Remove-Item $f -ErrorAction SilentlyContinue
$h = [RawIO]::Open($path, $true)
$fs = New-Object System.IO.FileStream($h, 'ReadWrite', 1, $false)
$fs.Seek($BlobLba * 512, 'Begin') | Out-Null
$fs.Write($blob, 0, $blob.Length); $fs.Flush()
$chk = LeerSec $fs $BlobLba (1 + $NP + $NB)
$fs.Dispose()
if (Compare-Object $blob $chk) { throw 'La verificacion de lo escrito ha fallado.' }
Write-Host 'Copia de la GPT guardada y verificada (disco queda OFFLINE). Ahora reinicia la tablet.'
