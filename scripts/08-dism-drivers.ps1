# Muestra que drivers de almacenamiento ve DISM en la imagen offline y, con -Aplicar,
# intenta registrar en la base de drivers los INF de volumen/disco/SD que faltan.
param([int]$Win = 80, [switch]$Aplicar)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-dism.txt'
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

Write-Host "`n== Edicion/version de la imagen =="
dism.exe /Image:$w\ /Get-CurrentEdition 2>&1 | Select-String -Pattern 'Image Version|Current Edition|Version|Edition'

Write-Host "`n== Drivers que ve DISM (filtrado: volume|disk|sdbus|sdstor|storage|mount|part) =="
$all = dism.exe /Image:$w\ /Get-Drivers /All /Format:Table 2>&1
$all | Select-String -Pattern 'Published|volume|disk\.inf|sdbus|sdstor|storahci|stornvme|mount|part|-----' | Select-Object -First 40
Write-Host ("Lineas totales de la lista: {0}" -f @($all).Count)

if ($Aplicar) {
    Write-Host "`n== Copia de seguridad de las colmenas =="
    $bk = Join-Path $kit 'backup-hives'; New-Item -ItemType Directory -Path $bk -Force | Out-Null
    foreach ($h in 'SYSTEM','DRIVERS','SOFTWARE') { Copy-Item "$w\Windows\System32\config\$h" "$bk\$h" -Force }
    Write-Host "Copiadas a $bk"

    Write-Host "`n== Registrando INF de almacenamiento =="
    $repo = "$w\Windows\System32\DriverStore\FileRepository"
    foreach ($n in 'volume','disk','sdbus','sdstor') {
        $dir = Get-ChildItem $repo -Directory -ErrorAction SilentlyContinue | Where-Object Name -match "^$n\.inf_arm64" | Select-Object -First 1
        $inf = if ($dir) { Join-Path $dir.FullName "$n.inf" } else { "$w\Windows\INF\$n.inf" }
        Write-Host "--- $n : $inf"
        dism.exe /Image:$w\ /Add-Driver /Driver:$inf /ForceUnsigned 2>&1 | Select-Object -Last 8
    }
}

Set-Location C:\
Set-Location $kit
Write-VolumeCache -DriveLetter $p.DriveLetter
Set-Disk -Number $d -IsOffline $true
Write-Host "`nListo. Disco offline. Resultado guardado en $out" -ForegroundColor Green
Stop-Transcript | Out-Null
