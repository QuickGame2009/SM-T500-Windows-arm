# Diagnostico de por que las particiones no se publican como \Device\HarddiskVolumeN:
# revisa en las colmenas offline de Windows (SYSTEM y DRIVERS) lo necesario para que el PnP
# empareje los dispositivos STORAGE\Volume con volmgr. Con -Aplicar anade las claves que falten.
param(
    [int]$Win = 80,
    [switch]$Aplicar
)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-volumen.txt'
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
reg load HKLM\TDRV "$w\Windows\System32\config\DRIVERS" | Out-Null
$sel = (Get-ItemProperty 'HKLM:\TSYS\Select').Current
$cs = 'ControlSet{0:D3}' -f $sel
Write-Host "ControlSet: $cs"

Write-Host "`n== Archivos de driver =="
foreach ($f in 'volmgr','volmgrx','partmgr','mountmgr','volsnap','disk','classpnp','ntfs','fltmgr','fvevol','storvsc') {
    $e = Test-Path "$w\Windows\System32\drivers\$f.sys"
    Write-Host ("{0,-10} {1}" -f $f, $(if ($e) { 'OK' } else { 'FALTA' }))
}

Write-Host "`n== Servicios de almacenamiento (Start / Group) =="
foreach ($s in 'volmgr','volmgrx','partmgr','mountmgr','volsnap','disk','ntfs','fvevol','fltmgr','volume') {
    $k = "HKLM:\TSYS\$cs\Services\$s"
    if (Test-Path $k) { $v = Get-ItemProperty $k; Write-Host ("{0,-9} Start={1} Group={2} Type={3}" -f $s, $v.Start, $v.Group, $v.Type) }
    else { Write-Host ("{0,-9} NO EXISTE" -f $s) }
}

Write-Host "`n== CriticalDeviceDatabase en SYSTEM =="
$cdd = "HKLM:\TSYS\$cs\Control\CriticalDeviceDatabase"
if (Test-Path $cdd) {
    $n = (Get-ChildItem $cdd).Count
    Write-Host "Existe, $n entradas"
    Get-ChildItem $cdd | Where-Object Name -match 'STORAGE|SD#|ACPI#QCOM24BF' | ForEach-Object {
        $v = Get-ItemProperty $_.PSPath; Write-Host ("{0}  Service={1} Class={2}" -f $_.PSChildName, $v.Service, $v.ClassGUID) }
} else { Write-Host "NO EXISTE" -ForegroundColor Yellow }

Write-Host "`n== DRIVERS: ids que contienen 'volume' o 'partition' (primeras 25 lineas) =="
reg query HKLM\TDRV\DriverDatabase\DeviceIds /s /k /f volume 2>&1 | Select-Object -First 25
reg query HKLM\TDRV\DriverDatabase\DeviceIds /s /k /f partition 2>&1 | Select-Object -First 15

Write-Host "`n== Clase Volume {71a27cdd-812a-11d0-bec7-08002be2092f} =="
$cls = "HKLM:\TSYS\$cs\Control\Class\{71a27cdd-812a-11d0-bec7-08002be2092f}"
if (Test-Path $cls) { Get-ItemProperty $cls | Format-List Class, ClassDesc, UpperFilters, LowerFilters, IconPath } else { Write-Host "NO EXISTE" -ForegroundColor Yellow }

if ($Aplicar) {
    Write-Host "`n== Aplicando CriticalDeviceDatabase para STORAGE#Volume =="
    reg export "HKLM\TSYS\$cs\Control\CriticalDeviceDatabase" (Join-Path $kit 'cdd-backup.reg') /y 2>&1 | Out-Null
    New-Item -Path $cdd -Force | Out-Null
    foreach ($e in @(
        @{ n = 'STORAGE#Volume';  svc = 'volmgr';  cls = '{71a27cdd-812a-11d0-bec7-08002be2092f}' },
        @{ n = 'STORAGE#Volume*'; svc = 'volmgr';  cls = '{71a27cdd-812a-11d0-bec7-08002be2092f}' })) {
        $k = "$cdd\$($e.n)"; New-Item -Path $k -Force | Out-Null
        Set-ItemProperty -Path $k -Name Service -Value $e.svc -Type String
        Set-ItemProperty -Path $k -Name ClassGUID -Value $e.cls -Type String
        Write-Host "añadido $($e.n)"
    }
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
