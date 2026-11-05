# Diagnostico de la base de drivers offline (DRIVERS/SYSTEM) y del INF/FileRepository de volumen.
param([int]$Win = 80)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-drivers.txt'
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

Write-Host "`n== Tamaño de las colmenas =="
Get-ChildItem "$w\Windows\System32\config" -File | Where-Object Name -in 'SYSTEM','DRIVERS','SOFTWARE','COMPONENTS' |
    ForEach-Object { Write-Host ("{0,-12} {1,12:N0} bytes" -f $_.Name, $_.Length) }

reg load HKLM\TSYS "$w\Windows\System32\config\SYSTEM" | Out-Null
reg load HKLM\TDRV "$w\Windows\System32\config\DRIVERS" | Out-Null

Write-Host "`n== DRIVERS: DriverDatabase =="
$db = 'HKLM:\TDRV\DriverDatabase'
if (Test-Path $db) {
    Get-ChildItem $db | ForEach-Object { Write-Host ("{0,-28} {1} subclaves" -f $_.PSChildName, @(Get-ChildItem $_.PSPath -ErrorAction SilentlyContinue).Count) }
    Write-Host "`nDeviceIds (primer nivel, 40):"
    Get-ChildItem "$db\DeviceIds" -ErrorAction SilentlyContinue | Select-Object -First 40 | ForEach-Object { $_.PSChildName }
    Write-Host "`nDriverPackages que contienen volume/storage/disk (30):"
    Get-ChildItem "$db\DriverPackages" -ErrorAction SilentlyContinue | Where-Object PSChildName -match 'volume|storage|disk|partmgr|mount|sdbus|sdstor' | Select-Object -First 30 | ForEach-Object { $_.PSChildName }
    Write-Host "`nDriverInfFiles que contienen volume/disk/sd (30):"
    Get-ChildItem "$db\DriverInfFiles" -ErrorAction SilentlyContinue | Where-Object PSChildName -match 'volume|disk|sd|stor|mount|part' | Select-Object -First 30 | ForEach-Object { $_.PSChildName }
} else { Write-Host "NO EXISTE DriverDatabase" -ForegroundColor Yellow }

Write-Host "`n== INF y FileRepository de volumen =="
foreach ($f in 'volume.inf','c_volume.inf','disk.inf','sdbus.inf','mountmgr.inf','partmgr.inf') {
    Write-Host ("{0,-14} {1}" -f $f, $(if (Test-Path "$w\Windows\INF\$f") { 'OK' } else { 'FALTA' }))
}
Get-ChildItem "$w\Windows\System32\DriverStore\FileRepository" -Directory -ErrorAction SilentlyContinue |
    Where-Object Name -match '^(volume|disk|sdbus|sdstor|mountmgr|partmgr)' | ForEach-Object { $_.Name }

Write-Host "`n== Registro de arranque (ntbtlog.txt) =="
foreach ($l in "$w\Windows\ntbtlog.txt", "$w\Windows\System32\LogFiles\Srt\SrtTrail.txt") {
    if (Test-Path $l) { Write-Host "$l :"; Get-Content $l -Tail 60 } else { Write-Host "$l no existe" }
}

Set-Location C:\
[gc]::Collect(); [gc]::WaitForPendingFinalizers()
reg unload HKLM\TSYS | Out-Null
reg unload HKLM\TDRV | Out-Null
Set-Location $kit
Write-VolumeCache -DriveLetter $p.DriveLetter
Set-Disk -Number $d -IsOffline $true
Write-Host "`nListo. Disco offline. Resultado guardado en $out" -ForegroundColor Green
Stop-Transcript | Out-Null
