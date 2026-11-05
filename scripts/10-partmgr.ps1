# Lee la configuracion de partmgr/volmgr y copia sus drivers al kit para analizarlos. No cambia nada en Windows.
param([int]$Win = 80)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-partmgr.txt'
Start-Transcript -Path $out -Force | Out-Null

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Ejecuta como administrador." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1
}
$disk = Get-Disk | Where-Object FriendlyName -like '*Linux File-Stor*' | Select-Object -First 1
if (-not $disk) { Write-Host "No encuentro el disco de la tablet (3-massstorage.cmd)." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1 }
$d = $disk.Number
Set-Disk -Number $d -IsOffline $false; Update-Disk -Number $d
$t = '{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}'
Set-Partition -DiskNumber $d -PartitionNumber $Win -GptType $t; Update-Disk -Number $d
$p = Get-Partition -DiskNumber $d -PartitionNumber $Win
if (-not $p.DriveLetter) { Add-PartitionAccessPath -DiskNumber $d -PartitionNumber $Win -AssignDriveLetter; $p = Get-Partition -DiskNumber $d -PartitionNumber $Win }
$w = "$($p.DriveLetter):"
Write-Host "Windows en $w"

reg load HKLM\TSYS "$w\Windows\System32\config\SYSTEM" | Out-Null
$cs = 'HKLM:\TSYS\ControlSet001'

foreach ($s in 'partmgr','volmgr','mountmgr','disk','sdstor','sdbus','vdrvroot') {
    Write-Host "`n== Services\$s =="
    $k = "$cs\Services\$s"
    if (Test-Path $k) {
        Get-ItemProperty $k | Format-List Start,Type,ErrorControl,Group,Tag,ImagePath,DependOnService,DependOnGroup | Out-String | Write-Host
        Get-ChildItem $k -Recurse -ErrorAction SilentlyContinue | ForEach-Object { Write-Host $_.Name; Get-ItemProperty $_.PSPath | Format-List | Out-String | Write-Host }
    } else { Write-Host "NO EXISTE" -ForegroundColor Yellow }
}

Write-Host "`n== Class de disco/volumen: filtros =="
foreach ($g in '{4d36e967-e325-11ce-bfc1-08002be10318}','{71a27cdd-812a-11d0-bec7-08002be2092f}','{533c5b84-ec70-11d2-9505-00c04f79deaf}') {
    $k = "$cs\Control\Class\$g"
    Write-Host $g
    if (Test-Path $k) { Get-ItemProperty $k | Format-List UpperFilters,LowerFilters | Out-String | Write-Host } else { Write-Host "  NO EXISTE" }
}

Write-Host "`n== ServiceGroupOrder =="
(Get-ItemProperty "$cs\Control\ServiceGroupOrder").List -join ', '

Write-Host "`n== Control\StorPort / Storage =="
foreach ($k in "$cs\Control\StorPort","$cs\Control\Storage") { if (Test-Path $k) { Get-ItemProperty $k | Format-List | Out-String | Write-Host } else { Write-Host "$k NO EXISTE" } }

Set-Location C:\
[gc]::Collect(); [gc]::WaitForPendingFinalizers()
reg unload HKLM\TSYS | Out-Null
Set-Location $kit

$dst = Join-Path $kit 'drv-copia'
New-Item -ItemType Directory -Path $dst -Force | Out-Null
foreach ($f in 'partmgr.sys','volmgr.sys','mountmgr.sys','volume.sys','disk.sys','sdstor.sys') {
    $src = "$w\Windows\System32\drivers\$f"
    if (Test-Path $src) { Copy-Item $src $dst -Force; Write-Host ("Copiado {0} ({1:N0} bytes)" -f $f, (Get-Item $src).Length) } else { Write-Host "$f NO EXISTE" }
}
$ver = (Get-Item "$w\Windows\System32\drivers\partmgr.sys").VersionInfo.FileVersion
Write-Host "partmgr.sys version: $ver"

Write-VolumeCache -DriveLetter $p.DriveLetter
Set-Disk -Number $d -IsOffline $true
Stop-Transcript | Out-Null
