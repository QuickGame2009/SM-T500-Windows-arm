# Convierte la imagen del UEFI (cabecera Android v1, pagina 2048, sin version de Android) a
# cabecera v2, pagina 4096 y version/parche de Android como el recovery de Lineage.
# NO cambia el contenido del UEFI ni las direcciones de carga. Es una prueba para el modo recovery de Samsung.
# Uso:  powershell -ExecutionPolicy Bypass -File scripts\convertir-cabecera.ps1 [-In .\Mu-gta4lwifi.img] [-Out .\Mu-gta4lwifi-v2.img]
param(
    [string]$In  = '.\Mu-gta4lwifi.img',
    [string]$Out = '.\Mu-gta4lwifi-v2.img',
    [uint32]$OsVersion = 0x200001A8
)
$ErrorActionPreference = 'Stop'
$inPath = (Resolve-Path $In).Path
$d = [IO.File]::ReadAllBytes($inPath)
if ([Text.Encoding]::ASCII.GetString($d, 0, 8) -ne 'ANDROID!') { throw 'No es una imagen Android (falta ANDROID!).' }

$ks = [BitConverter]::ToUInt32($d, 8)
$rs = [BitConverter]::ToUInt32($d, 16)
$ss = [BitConverter]::ToUInt32($d, 24)
$pg = [BitConverter]::ToUInt32($d, 36)
$hv = [BitConverter]::ToUInt32($d, 40)
if ($hv -gt 1) { throw "Cabecera version $hv: este script solo convierte v0/v1." }
if ($pg -eq 0) { throw 'Tamano de pagina 0.' }

function Alinear([uint64]$n, [uint64]$p) { if ($n -eq 0) { return 0 }; return [int]([math]::Ceiling($n / $p) * $p) }

$ko = [int]$pg
$ro = $ko + (Alinear $ks $pg)
$so = $ro + (Alinear $rs $pg)
if (($so + $ss) -gt $d.Length) { throw 'La imagen es mas corta de lo que dice su cabecera.' }

$np = 4096
$h = New-Object byte[] $np
[Array]::Copy($d, 0, $h, 0, 1648)                                   # cabecera v1 completa
[BitConverter]::GetBytes([uint32]$np).CopyTo($h, 36)                # page_size
[BitConverter]::GetBytes([uint32]2).CopyTo($h, 40)                  # header_version
[BitConverter]::GetBytes([uint32]$OsVersion).CopyTo($h, 44)         # os_version / parche
[BitConverter]::GetBytes([uint32]0).CopyTo($h, 1632)                # recovery_dtbo_size
[BitConverter]::GetBytes([uint64]0).CopyTo($h, 1636)                # recovery_dtbo_offset
[BitConverter]::GetBytes([uint32]1660).CopyTo($h, 1644)             # header_size (v2)
[BitConverter]::GetBytes([uint32]0).CopyTo($h, 1648)                # dtb_size
[BitConverter]::GetBytes([uint64]0).CopyTo($h, 1652)                # dtb_addr

$ms = New-Object IO.MemoryStream
$ms.Write($h, 0, $h.Length)
function Seccion($src, [int]$off, [uint32]$len) {
    if ($len -eq 0) { return }
    $ms.Write($src, $off, [int]$len)
    $pad = (Alinear $len $np) - [int]$len
    if ($pad -gt 0) { $ms.Write((New-Object byte[] $pad), 0, $pad) }
}
Seccion $d $ko $ks
Seccion $d $ro $rs
Seccion $d $so $ss

$outDir = Split-Path -Parent (Join-Path (Get-Location) $Out)
$outPath = Join-Path (Resolve-Path $outDir).Path (Split-Path -Leaf $Out)
[IO.File]::WriteAllBytes($outPath, $ms.ToArray())
$len = (Get-Item $outPath).Length
Write-Host ("Listo: {0}  ({1} bytes)" -f $outPath, $len)
Write-Host "Usa TAM = $len en la comprobacion con head -c."
