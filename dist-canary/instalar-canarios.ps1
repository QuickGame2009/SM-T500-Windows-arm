# Instala (o quita) los tres drivers canario en la imagen de Windows de la tablet.
# Uso (PowerShell como administrador, con la particion de Windows montada con letra):
#   .\instalar-canarios.ps1 -Windows K          instala
#   .\instalar-canarios.ps1 -Windows K -Quitar  los desactiva (Start=4) y borra los .sys
# Los .sys (canary1.sys, canary2.sys, canary3.sys) deben estar en la misma carpeta que este script.
param(
    [Parameter(Mandatory = $true)][string]$Windows,
    [switch]$Quitar
)
$ErrorActionPreference = 'Stop'
$W = $Windows.TrimEnd(':').ToUpper() + ':'
$hive = "$W\Windows\System32\Config\SYSTEM"
$drv  = "$W\Windows\System32\drivers"
if (-not (Test-Path $hive)) { throw "No encuentro $hive. Comprueba la letra de la particion de Windows." }
if (-not (Test-Path $drv))  { throw "No encuentro $drv." }

# slot -> grupo de arranque (orden de carga: Boot Bus Extender < System Bus Extender < Boot File System)
$slots = @(
    @{ N = 1; Group = 'Boot Bus Extender' },
    @{ N = 2; Group = 'System Bus Extender' },
    @{ N = 3; Group = 'Boot File System' }
)

if (-not $Quitar) {
    foreach ($s in $slots) {
        $f = Join-Path $PSScriptRoot "canary$($s.N).sys"
        if (-not (Test-Path $f)) { throw "Falta $f (debe estar junto al script)." }
        Copy-Item $f "$drv\canary$($s.N).sys" -Force
    }
}

reg load HKLM\CANARY $hive | Out-Null
if ($LASTEXITCODE -ne 0) { throw "No se pudo cargar el hive SYSTEM (reg load fallo)." }
try {
    $cur = (Get-ItemProperty 'HKLM:\CANARY\Select').Current
    $cs  = 'ControlSet{0:D3}' -f $cur
    Write-Host "ControlSet en uso: $cs"
    $list = (Get-ItemProperty "HKLM:\CANARY\$cs\Control\ServiceGroupOrder").List
    Write-Host "Primeros grupos de carga: $(($list | Select-Object -First 8) -join ' | ')"
    foreach ($s in $slots) {
        if ($list -notcontains $s.Group) { Write-Warning "El grupo '$($s.Group)' no esta en ServiceGroupOrder; se cargara al final de los grupos." }
        $k = "HKLM:\CANARY\$cs\Services\canary$($s.N)"
        if ($Quitar) {
            if (Test-Path $k) { Set-ItemProperty $k -Name Start -Value 4 -Type DWord; Write-Host "canary$($s.N): Start=4 (desactivado)" }
        } else {
            if (-not (Test-Path $k)) { New-Item $k -Force | Out-Null }
            Set-ItemProperty $k -Name Type         -Value 1 -Type DWord
            Set-ItemProperty $k -Name Start        -Value 0 -Type DWord
            Set-ItemProperty $k -Name ErrorControl -Value 0 -Type DWord
            Set-ItemProperty $k -Name Group        -Value $s.Group -Type String
            Set-ItemProperty $k -Name ImagePath    -Value "System32\drivers\canary$($s.N).sys" -Type ExpandString
            Write-Host "canary$($s.N): Start=0, grupo '$($s.Group)'"
        }
    }
} finally {
    Set-Location C:\
    [gc]::Collect(); [gc]::WaitForPendingFinalizers()
    reg unload HKLM\CANARY | Out-Null
}
if ($Quitar) {
    foreach ($s in $slots) { Remove-Item "$drv\canary$($s.N).sys" -Force -ErrorAction SilentlyContinue }
}
Write-Host "Hecho."
