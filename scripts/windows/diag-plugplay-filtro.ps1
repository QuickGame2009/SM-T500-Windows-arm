param([string]$Out = 'C:\DiagPnP')
$ErrorActionPreference = 'Continue'
$pidSvc = $null
$q = & sc.exe queryex plugplay | Select-String 'PID'
if ($q) { $pidSvc = ($q.ToString() -split ':')[1].Trim() }
$r = Import-Csv (Join-Path $Out 'pm.csv')
$cols = 'Time of Day','Process Name','PID','Operation','Path','Result','Detail'
$r | Where-Object { $_.Result -match 'NOT FOUND|DENIED|FAIL|ERROR|INVALID|UNSUCCESSFUL|TIMEOUT' } |
  Select-Object $cols | Export-Csv (Join-Path $Out '5-errores.csv') -NoTypeInformation
if ($pidSvc -and $pidSvc -ne '0') {
  $r | Where-Object { $_.PID -eq $pidSvc } | Select-Object -Last 300 |
    Select-Object $cols | Export-Csv (Join-Path $Out '5-ultimas-ops-svchost.csv') -NoTypeInformation
}
$r | Where-Object { $_.Path -match 'umpnpmgr|PlugPlay' } |
  Select-Object $cols | Export-Csv (Join-Path $Out '5-plugplay.csv') -NoTypeInformation
