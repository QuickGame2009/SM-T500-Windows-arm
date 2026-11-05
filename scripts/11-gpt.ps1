# Lee la GPT de la tablet y busca GUID duplicados, a cero o iguales al GUID del disco. No cambia nada.
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-gpt.txt'
Start-Transcript -Path $out -Force | Out-Null

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Ejecuta como administrador." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1
}
$disk = Get-Disk | Where-Object FriendlyName -like '*Linux File-Stor*' | Select-Object -First 1
if (-not $disk) { Write-Host "No encuentro el disco de la tablet (3-massstorage.cmd)." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1 }
$d = $disk.Number
Set-Disk -Number $d -IsOffline $false; Update-Disk -Number $d
$disk = Get-Disk -Number $d
Write-Host ("Disco {0}  estilo={1}  GUID del disco={2}" -f $d, $disk.PartitionStyle, $disk.Guid)

$parts = Get-Partition -DiskNumber $d | Sort-Object PartitionNumber
Write-Host ("Particiones: {0}" -f $parts.Count)
$zero = '{00000000-0000-0000-0000-000000000000}'
$dg = ('' + $disk.Guid).ToLower().Trim('{}')
$nZero = 0; $nDisk = 0
$byGuid = @{}
foreach ($p in $parts) {
    $g = ('' + $p.Guid).ToLower().Trim('{}')
    if ($g -eq '00000000-0000-0000-0000-000000000000' -or $g -eq '') { $nZero++ }
    if ($g -eq $dg) { $nDisk++ }
    if (-not $byGuid.ContainsKey($g)) { $byGuid[$g] = @() }
    $byGuid[$g] += $p.PartitionNumber
}
$dups = $byGuid.GetEnumerator() | Where-Object { $_.Value.Count -gt 1 }
Write-Host "`n== RESUMEN =="
Write-Host ("Particiones con GUID a cero: {0}" -f $nZero)
Write-Host ("Particiones con GUID igual al del disco: {0}" -f $nDisk)
Write-Host ("GUID de particion repetidos: {0}" -f (@($dups).Count))
foreach ($x in $dups) { Write-Host ("  {0} -> particiones {1}" -f $x.Key, ($x.Value -join ',')) }

Write-Host "`n== TABLA =="
$parts | ForEach-Object { "{0,3}  {1}  tipo={2}  {3,12} bytes" -f $_.PartitionNumber, $_.Guid, $_.GptType, $_.Size } | Write-Host

Set-Disk -Number $d -IsOffline $true
Stop-Transcript | Out-Null
