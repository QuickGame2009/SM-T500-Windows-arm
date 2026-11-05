# Comprueba (y con -Aplicar crea) el dispositivo raiz ROOT\VOLMGR\0000 que falta en el registro offline.
param([int]$Win = 80, [switch]$Aplicar)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-volmgr.txt'
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

Write-Host "`n== Enum\ROOT (claves) =="
Get-ChildItem "$cs\Enum\ROOT" -ErrorAction SilentlyContinue | ForEach-Object { $_.PSChildName }

Write-Host "`n== Enum\ROOT\VOLMGR =="
if (Test-Path "$cs\Enum\ROOT\VOLMGR") { Get-ChildItem "$cs\Enum\ROOT\VOLMGR" -Recurse | ForEach-Object { $_.Name; Get-ItemProperty $_.PSPath | Format-List | Out-String } } else { Write-Host "NO EXISTE" -ForegroundColor Yellow }

Write-Host "`n== Services de almacenamiento =="
foreach ($s in 'volmgr','volume','partmgr','mountmgr','disk','vdrvroot','volsnap','fvevol','sdbus','sdstor') {
    $k = "$cs\Services\$s"
    if (Test-Path $k) { $v = Get-ItemProperty $k; Write-Host ("{0,-10} Start={1} Type={2} Group={3} Tag={4} Img={5}" -f $s,$v.Start,$v.Type,$v.Group,$v.Tag,$v.ImagePath) }
    else { Write-Host ("{0,-10} NO EXISTE" -f $s) -ForegroundColor Yellow }
}

Write-Host "`n== CriticalDeviceDatabase (root#volmgr, root#vdrvroot, storage#volume) =="
foreach ($c in 'root#volmgr','root#vdrvroot','storage#volume','storage#partition') {
    $k = "$cs\Control\CriticalDeviceDatabase\$c"
    if (Test-Path $k) { Write-Host $c; Get-ItemProperty $k | Format-List Service,ClassGUID,Class | Out-String | Write-Host } else { Write-Host "$c NO EXISTE" }
}

Write-Host "`n== Class System ({4d36e97d...}) =="
Get-ChildItem "$cs\Control\Class\{4d36e97d-e325-11ce-bfc1-08002be10318}" -ErrorAction SilentlyContinue | ForEach-Object {
    $v = Get-ItemProperty $_.PSPath; Write-Host ("{0} {1} {2}" -f $_.PSChildName, $v.DriverDesc, $v.InfPath) }

if ($Aplicar) {
    Write-Host "`n== APLICANDO: creando Enum\ROOT\VOLMGR\0000 =="
    reg export "HKLM\TSYS\ControlSet001\Enum\ROOT" (Join-Path $kit 'enum-root-backup.reg') /y | Out-Null
    $k = "$cs\Enum\ROOT\VOLMGR\0000"
    New-Item -Path $k -Force | Out-Null
    New-ItemProperty $k -Name Service -Value 'volmgr' -PropertyType String -Force | Out-Null
    New-ItemProperty $k -Name ClassGUID -Value '{4d36e97d-e325-11ce-bfc1-08002be10318}' -PropertyType String -Force | Out-Null
    New-ItemProperty $k -Name Class -Value 'System' -PropertyType String -Force | Out-Null
    New-ItemProperty $k -Name HardwareID -Value ([string[]]@('ROOT\VOLMGR')) -PropertyType MultiString -Force | Out-Null
    New-ItemProperty $k -Name DeviceDesc -Value 'Volume Manager' -PropertyType String -Force | Out-Null
    New-ItemProperty $k -Name ConfigFlags -Value 0 -PropertyType DWord -Force | Out-Null
    New-ItemProperty $k -Name Capabilities -Value 0 -PropertyType DWord -Force | Out-Null
    New-Item -Path "$k\Device Parameters" -Force | Out-Null
    Write-Host "Creado. Copia de seguridad: enum-root-backup.reg"
}

Set-Location C:\
[gc]::Collect(); [gc]::WaitForPendingFinalizers()
reg unload HKLM\TSYS | Out-Null
Set-Location $kit
Write-VolumeCache -DriveLetter $p.DriveLetter
Set-Disk -Number $d -IsOffline $true
Stop-Transcript | Out-Null
