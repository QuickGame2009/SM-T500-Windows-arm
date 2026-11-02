# Recoge información del SM-T500 con TWRP abierto. Solo lee; no modifica nada.
# Uso:  .\1-info.cmd        (o)   powershell -ExecutionPolicy Bypass -File scripts\01-info.ps1 [-Out info]
param([string]$Out = (Join-Path (Get-Location).Path 'info'))
. (Join-Path $PSScriptRoot 'common.ps1')

Assert-Device
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$infoScript = @'
O=/tmp/info
rm -rf $O; mkdir -p $O
id                                   > $O/id.txt 2>&1
getprop                              > $O/getprop.txt 2>&1
cat /proc/cmdline                    > $O/cmdline.txt 2>&1
cat /proc/version                    > $O/kernel-version.txt 2>&1
cat /proc/iomem                      > $O/iomem.txt 2>&1
cat /proc/meminfo                    > $O/meminfo.txt 2>&1
cat /proc/partitions                 > $O/proc-partitions.txt 2>&1
cat /proc/device-tree/model          > $O/dt-model.txt 2>&1
cat /proc/device-tree/compatible     > $O/dt-compatible.txt 2>&1
od -An -tx1 /proc/device-tree/qcom,msm-id   > $O/dt-qcom-msm-id.txt 2>&1
od -An -tx1 /proc/device-tree/qcom,board-id > $O/dt-qcom-board-id.txt 2>&1
ls -l /dev/block/by-name /dev/block/platform/*/by-name > $O/by-name.txt 2>&1
{ ls /sys/class/udc; echo ---; ls /config/usb_gadget/g1; echo ---; ls /config/usb_gadget/g1/functions; echo ---; ls /config/usb_gadget/g1/configs/b.1; } > $O/usb-gadget.txt 2>&1
{ ls /sys/class/mmc_host; echo ---; ls -d /sys/class/ufs*; echo ---; ls /dev/block | grep -E '^(mmcblk0|sda)$'; } > $O/storage-type.txt 2>&1
ls -1 $O
'@

Write-Host '[1/3] Leyendo datos de la tablet...'
$r = Invoke-DeviceScript $infoScript 'info.sh'
$names = @($r.Out -split '\s+' | Where-Object { $_ -ne '' })   # los nombres no llevan espacios; vale con 1 por línea o en columnas
if ($names.Count -eq 0) { throw "El script de la tablet no generó ficheros. $($r.Err)" }

Write-Host "[2/3] Copiando $($names.Count) ficheros al PC..."
$errors = @()
foreach ($n in $names) {
  $res = Copy-FromDevice "/tmp/info/$n" (Join-Path $Out $n) 120
  if (-not $res.Ok) { $errors += "$n : $($res.Msg)"; Write-Warning "No pude copiar $n : $($res.Msg)" }
}

Write-Host '[3/3] Leyendo la tabla de particiones...'
$layout = Get-DeviceLayout
$lines = $layout.Parts | ForEach-Object { '{0} {1} {2} {3}' -f $_.Name, $_.Node, $_.Start, $_.Size }
Save-Text (Join-Path $Out 'partitions.txt') (($lines -join "`n") + "`n")
Save-Text (Join-Path $Out 'disk.txt') ("{0} {1}`n" -f $layout.Disk, $layout.DiskSectors)

$getprop = Join-Path $Out 'getprop.txt'
if (Test-Path $getprop) {
  $pat = 'bootloader|verifiedboot|vbmeta|flash\.locked|warranty|ro\.product\.(device|model|name)|ro\.boot\.hardware|ro\.build\.fingerprint'
  $hits = Select-String -Path $getprop -Pattern $pat | ForEach-Object { $_.Line }
  Save-Text (Join-Path $Out 'arranque-resumen.txt') (($hits -join "`n") + "`n")
}

Write-Host ''
Write-Host 'Listo. Resumen de lo importante:'
Write-Host '---- Bootloader / estado de arranque'
if (Test-Path (Join-Path $Out 'arranque-resumen.txt')) { Get-Content (Join-Path $Out 'arranque-resumen.txt') }
Write-Host '---- Disco (nombre y sectores)'
Get-Content (Join-Path $Out 'disk.txt')
foreach ($f in 'dt-qcom-msm-id.txt', 'dt-qcom-board-id.txt') {
  Write-Host "---- $f"
  if (Test-Path (Join-Path $Out $f)) { Get-Content (Join-Path $Out $f) }
}
$expected = 'id.txt', 'getprop.txt', 'cmdline.txt', 'kernel-version.txt', 'iomem.txt', 'meminfo.txt', 'proc-partitions.txt',
  'dt-model.txt', 'dt-compatible.txt', 'dt-qcom-msm-id.txt', 'dt-qcom-board-id.txt', 'by-name.txt', 'usb-gadget.txt',
  'storage-type.txt', 'arranque-resumen.txt', 'partitions.txt', 'disk.txt'
$missing = @($expected | Where-Object { -not (Test-Path (Join-Path $Out $_)) })
$report = @("Ficheros en la tablet: $($names -join ', ')") + $errors + @("Faltan en el PC: $($missing -join ', ')")
Save-Text (Join-Path $Out '_informe.txt') (($report -join "`n") + "`n")
Write-Host ''
if ($missing.Count -gt 0) {
  Write-Warning ('FALTAN estos ficheros en la carpeta info: ' + ($missing -join ', '))
  Write-Warning "Detalle en $(Join-Path $Out '_informe.txt')"
} else {
  Write-Host 'Comprobación: están los 17 ficheros esperados.'
}
Write-Host ''
Write-Host "Envíame la carpeta '$Out' completa (o comprímela en un zip)."
