# Prepara el disco expuesto por 3-massstorage.cmd: online, tipo GPT basico en ESP/Windows,
# copia opcional de canary3.sys, vaciado de cache y disco offline (listo para reiniciar la tablet).
param(
    [int]$Esp = 76,
    [int]$Win = 80,
    [switch]$Instalar   # ejecuta tambien instalar-canarios.ps1 (solo hace falta la primera vez)
)
$ErrorActionPreference = 'Stop'
$kit = Split-Path -Parent $PSScriptRoot

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Ejecuta este script como administrador (clic derecho > Ejecutar como administrador)." -ForegroundColor Red
    exit 1
}

$disk = Get-Disk | Where-Object FriendlyName -like '*Linux File-Stor*' | Select-Object -First 1
if (-not $disk) { Write-Host "No encuentro el disco de la tablet. Ejecuta antes 3-massstorage.cmd." -ForegroundColor Red; exit 1 }
$d = $disk.Number
Write-Host "Disco de la tablet: $d"

Set-Disk -Number $d -IsOffline $false
Update-Disk -Number $d

$t = '{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}'
foreach ($n in $Esp, $Win) { Set-Partition -DiskNumber $d -PartitionNumber $n -GptType $t }
Update-Disk -Number $d

# Letras: si alguna partición no tiene, se le asigna una libre
foreach ($n in $Esp, $Win) {
    $p = Get-Partition -DiskNumber $d -PartitionNumber $n
    if (-not $p.DriveLetter) { Add-PartitionAccessPath -DiskNumber $d -PartitionNumber $n -AssignDriveLetter }
}
$pe = Get-Partition -DiskNumber $d -PartitionNumber $Esp
$pw = Get-Partition -DiskNumber $d -PartitionNumber $Win
$le = $pe.DriveLetter; $lw = $pw.DriveLetter
Get-Partition -DiskNumber $d | Where-Object PartitionNumber -in $Esp, $Win | Format-Table PartitionNumber, DriveLetter, Size
Write-Host "ESP = ${le}:   Windows = ${lw}:"

if ($Instalar) {
    $ins = @("$kit\canary\instalar-canarios.ps1", "$kit\dist-canary\instalar-canarios.ps1", "$kit\instalar-canarios.ps1") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($ins) { & $ins -Windows $lw } else { Write-Host "No encuentro instalar-canarios.ps1" -ForegroundColor Yellow }
}

# Copia canary3.sys si hay uno mas reciente en el kit
$src = @("$kit\canary\canary3.sys", "$kit\dist-canary\canary3.sys", "$kit\canary3.sys") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($src) {
    Copy-Item $src "${lw}:\Windows\System32\drivers\canary3.sys" -Force
    Write-Host "Copiado $src -> ${lw}:\Windows\System32\drivers\canary3.sys"
} else {
    Write-Host "No hay canary3.sys en el kit: no se copia nada." -ForegroundColor Yellow
}

Set-Location $kit
Write-VolumeCache -DriveLetter $lw
Write-VolumeCache -DriveLetter $le
Set-Disk -Number $d -IsOffline $true
Write-Host ""
Write-Host "Listo: disco offline. Reinicia la tablet con Encendido." -ForegroundColor Green
