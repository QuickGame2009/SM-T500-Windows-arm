# Expone la memoria interna de la tablet al PC como un disco USB (desde TWRP).
# Úsalo solo en la fase de instalación, después de particionar y formatear.
# Uso:  .\3-massstorage.cmd
. (Join-Path $PSScriptRoot 'common.ps1')

Assert-Device

$src = Join-Path $PSScriptRoot 'MassStorage-emmc.sh'
if (-not (Test-Path $src)) { throw "Falta $src" }

Write-Host 'Antes de seguir, en TWRP: Montar (Mount) y DESMARCA todas las particiones.'
Write-Host 'Esto expone TODA la memoria interna al PC con permiso de escritura.'
Write-Host 'Windows puede ofrecer "inicializar" o "formatear" discos/particiones: cancela siempre,'
Write-Host 'salvo cuando formatees a propósito win y esp.'
$ans = Read-Host 'Escribe SI para continuar'
if ($ans -ne 'SI') { Write-Host 'Cancelado.'; return }

# Copia temporal con saltos de línea Unix (con CRLF el script falla en la tablet).
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) 'MassStorage-emmc.sh'
$text = [System.IO.File]::ReadAllText($src) -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($tmp, $text, (New-Object System.Text.UTF8Encoding($false)))

$r = Invoke-Adb @('push', $tmp, '/cache/MassStorage-emmc.sh') 60
if ($r.Code -ne 0) { throw "adb push falló: $($r.Err) $($r.Out)" }
Invoke-Adb @('shell', 'chmod 744 /cache/MassStorage-emmc.sh') 30 | Out-Null

Write-Host 'Lanzando almacenamiento masivo (la conexión adb se cortará en unos segundos, es normal)...'
$run = Invoke-Adb @('shell', 'sh /cache/MassStorage-emmc.sh') 30
if ($run.Out.Trim() -ne '') { Write-Host $run.Out.Trim() }
if ($run.Err.Trim() -ne '') { Write-Host $run.Err.Trim() }

Write-Host ''
Write-Host 'Espera unos segundos y abre "Administración de discos" (diskmgmt.msc).'
Write-Host 'Debe aparecer un disco nuevo de unos 60 GB con muchas particiones.'
Write-Host 'Si no aparece: reinicia la tablet a TWRP y ejecuta  adb shell cat /cache/ms.log  y envíame el resultado.'
Write-Host ''
Write-Host 'Siguiente paso, en una consola de administrador:'
Write-Host '  diskpart'
Write-Host '  lis dis                  (busca el disco de ~60 GB)'
Write-Host '  sel dis N'
Write-Host '  lis par                  (busca "win" (NTFS grande) y "esp" (FAT32 de 512 MB))'
Write-Host '  sel par X'
Write-Host '  assign letter=R          (para win)'
Write-Host '  sel par Y'
Write-Host '  assign letter=S          (para esp)'
Write-Host '  exit'
Write-Host 'Luego:  windows\apply-windows.cmd C:\ruta\install.wim R S 1'
