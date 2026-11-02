# Funciones comunes de los scripts del kit. No se ejecuta solo: lo cargan los demás.
# Compatible con Windows PowerShell 5.1 (el que trae Windows 10/11) y con PowerShell 7.
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:AdbPath = $null

function Find-Adb {
  $cmd = Get-Command adb -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $kit = Split-Path -Parent $PSScriptRoot
  $candidates = @(
    (Join-Path $kit 'platform-tools\adb.exe'),
    (Join-Path $PSScriptRoot 'platform-tools\adb.exe'),
    (Join-Path $env:USERPROFILE 'Downloads\platform-tools\adb.exe'),
    (Join-Path $env:USERPROFILE 'platform-tools\adb.exe'),
    (Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe')
  )
  foreach ($c in $candidates) { if (Test-Path $c) { return (Resolve-Path $c).Path } }
  throw @"
No encuentro adb.exe.
Descarga 'SDK Platform-Tools para Windows' de developer.android.com/tools/releases/platform-tools
y descomprímelo dentro de la carpeta del kit (debe quedar kit\platform-tools\adb.exe).
"@
}

function ConvertTo-ArgString([string[]]$Items) {
  $parts = foreach ($i in $Items) {
    if ($i.Contains('"')) { throw "Argumento con comillas no soportado: $i" }
    if ($i -eq '' -or $i -match '\s') { '"' + $i + '"' } else { $i }
  }
  return ($parts -join ' ')
}

# Ejecuta adb y devuelve Code, Out, Err y TimedOut. No falla si adb devuelve error.
function Invoke-Adb {
  param([string[]]$ArgList, [int]$TimeoutSec = 600)
  if (-not $script:AdbPath) { $script:AdbPath = Find-Adb }
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $script:AdbPath
  $psi.Arguments = ConvertTo-ArgString $ArgList
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $p = [System.Diagnostics.Process]::Start($psi)
  $tOut = $p.StandardOutput.ReadToEndAsync()
  $tErr = $p.StandardError.ReadToEndAsync()
  $timedOut = $false
  if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    $timedOut = $true
    try { $p.Kill() } catch { }
  }
  $p.WaitForExit()
  $code = if ($timedOut) { -1 } else { $p.ExitCode }
  [pscustomobject]@{
    Code     = $code
    Out      = ($tOut.Result -replace "`r", '')
    Err      = ($tErr.Result -replace "`r", '')
    TimedOut = $timedOut
  }
}

function Assert-Device {
  $r = Invoke-Adb @('get-state') 30
  $state = $r.Out.Trim()
  if ($r.Code -ne 0 -or $state -eq '') {
    throw "No veo la tablet por adb. Arranca en TWRP, conecta el cable y comprueba 'adb devices'.`n$($r.Err)"
  }
  $id = Invoke-Adb @('shell', 'id') 30
  if ($id.Out -notmatch 'uid=0') {
    throw "adb no tiene permisos de root (estado: $state). Estos scripts son para TWRP, no para Android."
  }
  if ($state -ne 'recovery') {
    Write-Warning "El estado de adb es '$state', no 'recovery'. Comprueba que estás en TWRP."
  }
}

# Sube un script de shell a la tablet (con saltos de línea Unix) y lo ejecuta.
# Evita los problemas de comillas de pasar comandos largos por la línea de órdenes.
function Invoke-DeviceScript {
  param([string]$Text, [string]$Name = 'kit.sh', [int]$TimeoutSec = 120)
  $local = Join-Path ([System.IO.Path]::GetTempPath()) $Name
  [System.IO.File]::WriteAllText($local, ($Text -replace "`r`n", "`n"), (New-Object System.Text.UTF8Encoding($false)))
  $r = Invoke-Adb @('push', $local, "/tmp/$Name") 60
  if ($r.Code -ne 0) { throw "adb push falló: $($r.Err) $($r.Out)" }
  return (Invoke-Adb @('exec-out', 'sh', "/tmp/$Name") $TimeoutSec)
}

# Lista de particiones y disco. Devuelve un objeto con Disk, DiskSectors y Parts.
$script:LayoutScript = @'
BYN=$(ls -d /dev/block/by-name /dev/block/platform/*/by-name 2>/dev/null | head -n1)
for l in $BYN/*; do
  n=${l##*/}; d=$(readlink -f "$l"); b=${d##*/}
  echo "PART $n $d $(cat /sys/class/block/$b/start 2>/dev/null) $(cat /sys/class/block/$b/size 2>/dev/null)"
done
d=$(readlink -f $BYN/userdata); b=${d##*/}
disk=$(basename "$(readlink -f /sys/class/block/$b/..)")
echo "DISK $disk $(cat /sys/class/block/$disk/size)"
'@

function Get-DeviceLayout {
  $r = Invoke-DeviceScript $script:LayoutScript 'layout.sh'
  if ($r.Code -ne 0) { throw "No pude leer las particiones: $($r.Err)" }
  $parts = @()
  $disk = $null
  $sectors = [int64]0
  foreach ($line in ($r.Out -split "`n")) {
    $l = $line.Trim()
    if ($l -match '^PART (\S+) (\S+) (\d+) (\d+)$') {
      $parts += [pscustomobject]@{ Name = $Matches[1]; Node = $Matches[2]; Start = [int64]$Matches[3]; Size = [int64]$Matches[4] }
    } elseif ($l -match '^DISK (\S+) (\d+)$') {
      $disk = $Matches[1]; $sectors = [int64]$Matches[2]
    }
  }
  if ($parts.Count -eq 0 -or -not $disk) { throw "Salida inesperada al leer particiones:`n$($r.Out)" }
  [pscustomobject]@{ Disk = $disk; DiskSectors = $sectors; Parts = $parts }
}

# Copia un fichero de la tablet al PC. Hace 'adb pull' a una carpeta temporal y lo mueve con PowerShell:
# así no depende de que adb.exe pueda escribir en la carpeta final (p. ej. Documentos o Escritorio con
# "Acceso controlado a carpetas" de Windows Defender activado).
function Copy-FromDevice {
  param([string]$Remote, [string]$Local, [int]$TimeoutSec = 300)
  $tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) 'sm-t500-pull'
  New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
  $tmp = Join-Path $tmpDir ([guid]::NewGuid().ToString('N') + '.bin')
  $r = Invoke-Adb @('pull', $Remote, $tmp) $TimeoutSec
  if ($r.Code -ne 0 -or -not (Test-Path -LiteralPath $tmp)) {
    return [pscustomobject]@{ Ok = $false; Msg = (($r.Err + ' ' + $r.Out).Trim()) }
  }
  try {
    Copy-Item -LiteralPath $tmp -Destination $Local -Force
  } catch {
    return [pscustomobject]@{ Ok = $false; Msg = "No pude escribir en $Local : $($_.Exception.Message)" }
  } finally {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
  }
  [pscustomobject]@{ Ok = $true; Msg = '' }
}

function Save-Text([string]$Path, [string]$Text) {
  [System.IO.File]::WriteAllText($Path, ($Text -replace "`r`n", "`n"))
}
