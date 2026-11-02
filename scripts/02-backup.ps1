# Copia de seguridad de las particiones pequeñas y de la tabla GPT, con TWRP abierto.
# Solo lee de la tablet; guarda los .img en el PC.
# Uso:  .\2-backup.cmd      (o)   powershell -ExecutionPolicy Bypass -File scripts\02-backup.ps1 [-Out backup] [-MaxMiB 300]
#
# Se omiten userdata y las particiones grandes (system, vendor, super...). Esas se
# restauran con el firmware oficial. Importan boot, recovery, vbmeta, efs, persist, modem...
param(
  [string]$Out = (Join-Path (Get-Location).Path 'backup'),
  [int]$MaxMiB = 300
)
. (Join-Path $PSScriptRoot 'common.ps1')

Assert-Device
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$layout = Get-DeviceLayout
Write-Host ("Disco: {0}  sectores: {1}" -f $layout.Disk, $layout.DiskSectors)
Write-Host "== Particiones (límite $MaxMiB MiB)"

$saved = @(); $skipped = @(); $failed = @()
foreach ($p in $layout.Parts) {
  if ($p.Name -in 'userdata', 'cache') { $skipped += "$($p.Name)(omitida)"; continue }
  if ($p.Size -le 0) { $skipped += "$($p.Name)(sin tamaño)"; continue }
  $mib = [int64][math]::Floor($p.Size * 512 / 1048576)
  if ($mib -gt $MaxMiB) { $skipped += "$($p.Name)($mib MiB)"; continue }

  Write-Host ("  {0,-14} {1,6} MiB ... " -f $p.Name, $mib) -NoNewline
  $dst = Join-Path $Out ($p.Name + '.img')
  $d = Invoke-Adb @('shell', "dd if=/dev/block/by-name/$($p.Name) of=/tmp/bk.img bs=1048576") 300
  $g = Copy-FromDevice '/tmp/bk.img' $dst 300
  Invoke-Adb @('shell', 'rm -f /tmp/bk.img') 30 | Out-Null
  $want = $p.Size * 512
  $got = if (Test-Path $dst) { (Get-Item $dst).Length } else { -1 }
  if ($g.Ok -and $got -eq $want) { Write-Host 'ok'; $saved += $p.Name }
  else { Write-Host "FALLO (esperado $want bytes, obtenido $got) $($g.Msg)"; $failed += $p.Name }
}

Write-Host '== Tabla de particiones GPT (34 sectores iniciales y 33 finales)'
$total = $layout.DiskSectors
$skip = $total - 33
$cmds = @(
  @{ Cmd = "dd if=/dev/block/$($layout.Disk) of=/tmp/gpt.bin bs=512 count=34"; File = 'gpt-primary.bin' },
  @{ Cmd = "dd if=/dev/block/$($layout.Disk) of=/tmp/gpt.bin bs=512 skip=$skip count=33"; File = 'gpt-backup.bin' }
)
foreach ($c in $cmds) {
  Invoke-Adb @('shell', $c.Cmd) 60 | Out-Null
  $g = Copy-FromDevice '/tmp/gpt.bin' (Join-Path $Out $c.File) 60
  Invoke-Adb @('shell', 'rm -f /tmp/gpt.bin') 30 | Out-Null
  if (-not $g.Ok) { Write-Warning "No pude copiar $($c.File) : $($g.Msg)"; $failed += $c.File }
}
Save-Text (Join-Path $Out 'disk.txt') ("{0} {1}`n" -f $layout.Disk, $layout.DiskSectors)

Write-Host '== Comprobaciones'
foreach ($must in 'boot', 'recovery', 'vbmeta', 'efs', 'persist') {
  if ($must -notin $saved) { Write-Warning "No se guardó '$must'. Revisa antes de seguir." }
}
$sums = Get-ChildItem -Path $Out -File | Where-Object { $_.Extension -in '.img', '.bin' } |
  ForEach-Object { '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 -Path $_.FullName).Hash.ToLower(), $_.Name }
Save-Text (Join-Path $Out 'SHA256SUMS.txt') (($sums -join "`n") + "`n")

Write-Host ''
Write-Host ('Guardadas: ' + ($saved -join ' '))
Write-Host ('Omitidas:  ' + ($skipped -join ' '))
if ($failed.Count -gt 0) { Write-Warning ('Con fallo:  ' + ($failed -join ' ')) }
Write-Host "Copia la carpeta '$Out' a otro disco o a la nube ANTES de particionar."
