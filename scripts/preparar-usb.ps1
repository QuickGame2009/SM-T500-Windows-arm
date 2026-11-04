# Prepara un pendrive USB como disco de arranque de prueba: GPT, ESP FAT32 (512 MB) + Windows NTFS.
# Uso (PowerShell como administrador):  .\scripts\preparar-usb.ps1 -Disco 6
# BORRA TODO el disco indicado. Solo acepta discos USB de menos de 130 GB.
param([Parameter(Mandatory = $true)][int]$Disco)
$ErrorActionPreference = 'Stop'

$d = Get-Disk -Number $Disco
if ($d.BusType -ne 'USB') { throw "El disco $Disco no es USB (es $($d.BusType)). Me niego a tocarlo." }
if ($d.Size -gt 130GB) { throw "El disco $Disco mide $([math]::Round($d.Size/1GB)) GB. Demasiado grande para un pendrive de prueba." }
if ($d.Size -lt 14GB) { throw "El disco $Disco mide $([math]::Round($d.Size/1GB,1)) GB. Hacen falta al menos 16 GB." }

Write-Host ""
Write-Host "Disco $Disco : $($d.FriendlyName) - $([math]::Round($d.Size/1GB,1)) GB - USB"
Write-Host "SE BORRARA TODO EL CONTENIDO."
$r = Read-Host "Escribe SI para continuar"
if ($r -ne 'SI') { Write-Host "Cancelado."; exit 1 }

Set-Disk -Number $Disco -IsOffline $false -ErrorAction SilentlyContinue
Set-Disk -Number $Disco -IsReadOnly $false -ErrorAction SilentlyContinue
Clear-Disk -Number $Disco -RemoveData -RemoveOEM -Confirm:$false
Update-Disk -Number $Disco
if ((Get-Disk -Number $Disco).PartitionStyle -eq 'RAW') { Initialize-Disk -Number $Disco -PartitionStyle GPT }

$p1 = New-Partition -DiskNumber $Disco -Size 512MB -AssignDriveLetter
Format-Volume -Partition $p1 -FileSystem FAT32 -NewFileSystemLabel USBESP -Confirm:$false | Out-Null
$p2 = New-Partition -DiskNumber $Disco -UseMaximumSize -AssignDriveLetter
Format-Volume -Partition $p2 -FileSystem NTFS -NewFileSystemLabel USBWIN -Confirm:$false | Out-Null

Start-Sleep -Seconds 2
Write-Host ""
Write-Host "Listo. Letras asignadas:"
Get-Partition -DiskNumber $Disco | Format-Table PartitionNumber, DriveLetter, Size
Write-Host "La de 512 MB es la ESP; la grande es Windows."
