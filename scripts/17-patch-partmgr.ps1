# Como 5-preparar (y 14), pero con parche de partmgr.sys;  SIN dejar que Windows reescriba la GPT: el cambio de tipo de las particiones ESP/Windows
# se hace directamente en la tabla (con disco offline), y despues Windows solo monta los volumenes y copia archivos.
param([int]$Esp = 76, [int]$Win = 80, [switch]$SinVolmgr, [string[]]$Desactivar = @('dam'), [int]$Numproc = -1, [string[]]$Activar = @(), [int]$Keepalive = -1, [switch]$ArreglarSetup, [switch]$ArreglarLogon, [switch]$Consola, [switch]$QuitarConsola)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-partmgr-patch.txt'
Start-Transcript -Path $out -Force | Out-Null
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Ejecuta como administrador." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1
}
$disk = Get-Disk | Where-Object FriendlyName -like '*Linux File-Stor*' | Select-Object -First 1
if (-not $disk) { Write-Host "No encuentro el disco de la tablet (3-massstorage.cmd)." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1 }
$d = $disk.Number; $ss = [int]$disk.LogicalSectorSize
$raw = "\\.\PhysicalDrive$d"
$total = [long]($disk.Size / $ss)
$bk = Join-Path $kit 'gpt-backup.bin'
Write-Host "Disco de la tablet: $d"

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
public static class GptTool {
    static uint[] T;
    static uint Crc(byte[] b, int off, int len) {
        if (T == null) { T = new uint[256]; for (uint i = 0; i < 256; i++) { uint c = i; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); T[i] = c; } }
        uint r = 0xFFFFFFFFu; for (int i = 0; i < len; i++) r = T[(r ^ b[off + i]) & 0xFF] ^ (r >> 8); return ~r;
    }
    static byte[] Rd(FileStream fs, long lba, int ss, int len) { byte[] b = new byte[len]; fs.Seek(lba * ss, SeekOrigin.Begin); int got = 0; while (got < len) { int n = fs.Read(b, got, len - got); if (n <= 0) break; got += n; } return b; }
    static void Wr(FileStream fs, long lba, int ss, byte[] b) { fs.Seek(lba * ss, SeekOrigin.Begin); fs.Write(b, 0, b.Length); fs.Flush(true); }
    static bool IsZero(byte[] b, int off, int len) { for (int i = 0; i < len; i++) if (b[off + i] != 0) return false; return true; }
    static bool Eq(byte[] a, int ao, byte[] b, int bo, int len) { for (int i = 0; i < len; i++) if (a[ao + i] != b[bo + i]) return false; return true; }
    static string G(byte[] b, int off) { byte[] g = new byte[16]; Array.Copy(b, off, g, 0, 16); return new Guid(g).ToString(); }
    static List<int> Dups(byte[] ea, int n, int es, byte[] dg) {
        List<int> d = new List<int>(); HashSet<string> seen = new HashSet<string>();
        for (int i = 0; i < n; i++) {
            int o = i * es; if (IsZero(ea, o, 16)) continue;
            string u = G(ea, o + 16);
            if (IsZero(ea, o + 16, 16) || Eq(ea, o + 16, dg, 0, 16) || seen.Contains(u)) d.Add(i);
            seen.Add(u);
        }
        return d;
    }
    static void SetCrcs(byte[] h, int hs, uint ecrc) {
        BitConverter.GetBytes(ecrc).CopyTo(h, 88); BitConverter.GetBytes((uint)0).CopyTo(h, 16);
        BitConverter.GetBytes(Crc(h, 0, hs)).CopyTo(h, 16);
    }
    public static string PatchPartmgr(string path) {
        byte[] b = File.ReadAllBytes(path);
        if (b.Length != 176496) return "ERROR: tamano inesperado de partmgr.sys (" + b.Length + "), no es la version esperada";
        int off = 0x17c28;
        byte[] orig = new byte[] { 0x7F,0x23,0x03,0xD5, 0xF3,0x53,0xBB,0xA9, 0xF5,0x5B,0x01,0xA9, 0xF7,0x63,0x02,0xA9 };
        byte[] nw = new byte[] { 0x7F,0x00,0x00,0xB9, 0x5F,0x00,0x00,0x39, 0x00,0x00,0x80,0x52, 0xC0,0x03,0x5F,0xD6 };
        bool isOrig = true, isNew = true;
        for (int i = 0; i < 16; i++) { if (b[off + i] != orig[i]) isOrig = false; if (b[off + i] != nw[i]) isNew = false; }
        if (isNew) return "partmgr.sys ya estaba parcheado";
        if (!isOrig) return "ERROR: los bytes en 0x17C28 no son los esperados; no se toca";
        Array.Copy(nw, 0, b, off, 16);
        int pe = BitConverter.ToInt32(b, 0x3C); int co = pe + 4 + 20 + 64;
        uint total = 0;
        for (int i = 0; i < b.Length; i += 2) {
            if (i >= co && i < co + 4) continue;
            uint w = (uint)(b[i] | ((i + 1 < b.Length ? b[i + 1] : 0) << 8));
            total += w; total = (total & 0xFFFF) + (total >> 16);
        }
        total = (total & 0xFFFF) + (total >> 16);
        uint cs = total + (uint)b.Length;
        BitConverter.GetBytes(cs).CopyTo(b, co);
        File.WriteAllBytes(path, b);
        return "partmgr.sys parcheado (checksum 0x" + cs.ToString("X8") + ")";
    }
    public static string Summary(string path, int ss) {
        try {
            using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1)) {
                byte[] h = Rd(fs, 1, ss, ss);
                if (Encoding.ASCII.GetString(h, 0, 8) != "EFI PART") return "sin cabecera GPT";
                long ent = BitConverter.ToInt64(h, 72); int n = BitConverter.ToInt32(h, 80), es = BitConverter.ToInt32(h, 84);
                int asec = (n * es + ss - 1) / ss; byte[] ea = Rd(fs, ent, ss, asec * ss); byte[] dg = new byte[16]; Array.Copy(h, 56, dg, 0, 16);
                int used = 0; for (int i = 0; i < n; i++) if (!IsZero(ea, i * es, 16)) used++;
                return string.Format("tabla en LBA {0}, entradas={1}, en uso={2}, repetidos={3}", ent, n, used, Dups(ea, n, es, dg).Count);
            }
        } catch (Exception ex) { return "ERROR: " + ex.Message; }
    }
    // Cambia el GUID de TIPO de las particiones indicadas (1..n) y corrige GUID unicos repetidos; recalcula CRC (principal y copia).
    public static string SetTypes(string path, int ss, int[] parts, byte[] typeGuid) {
        StringBuilder sb = new StringBuilder();
        try {
            using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite, 1)) {
                byte[] h = Rd(fs, 1, ss, ss);
                if (Encoding.ASCII.GetString(h, 0, 8) != "EFI PART") return "ERROR: sin cabecera GPT";
                int hs = BitConverter.ToInt32(h, 12); long alt = BitConverter.ToInt64(h, 32); long ent = BitConverter.ToInt64(h, 72);
                int n = BitConverter.ToInt32(h, 80), es = BitConverter.ToInt32(h, 84); int asec = (n * es + ss - 1) / ss;
                byte[] ea = Rd(fs, ent, ss, asec * ss); byte[] bh = Rd(fs, alt, ss, ss); long bent = BitConverter.ToInt64(bh, 72);
                byte[] dg = new byte[16]; Array.Copy(h, 56, dg, 0, 16);
                foreach (int p in parts) {
                    int o = (p - 1) * es;
                    if (p < 1 || p > n || IsZero(ea, o, 16)) { sb.AppendLine("ERROR: la particion " + p + " no existe"); return sb.ToString(); }
                    sb.AppendLine("Particion " + p + ": tipo " + G(ea, o) + " -> " + new Guid(typeGuid));
                    Array.Copy(typeGuid, 0, ea, o, 16);
                }
                List<int> dl = Dups(ea, n, es, dg);
                foreach (int i in dl) Array.Copy(Guid.NewGuid().ToByteArray(), 0, ea, i * es + 16, 16);
                sb.AppendLine("GUID unicos corregidos: " + dl.Count);
                uint ec = Crc(ea, 0, n * es); SetCrcs(h, hs, ec); SetCrcs(bh, hs, ec);
                Wr(fs, ent, ss, ea); Wr(fs, bent, ss, ea); Wr(fs, alt, ss, bh); Wr(fs, 1, ss, h);
            }
        } catch (Exception ex) { sb.AppendLine("ERROR: " + ex.Message); }
        return sb.ToString();
    }
    public static string Restore(string path, int ss, long totalSectors, string backupPath) {
        StringBuilder sb = new StringBuilder();
        try { RestoreInner(sb, path, ss, totalSectors, backupPath); }
        catch (Exception ex) { sb.AppendLine("ERROR: " + ex.Message); }
        return sb.ToString();
    }
    static void RestoreInner(StringBuilder sb, string path, int ss, long totalSectors, string backupPath) {
        byte[] b = File.ReadAllBytes(backupPath);
        if (b.Length < ss * 3) { sb.AppendLine("ERROR: la copia es demasiado corta"); return; }
        byte[] l0 = new byte[ss]; Array.Copy(b, 0, l0, 0, ss);
        byte[] h = new byte[ss]; Array.Copy(b, ss, h, 0, ss);
        if (Encoding.ASCII.GetString(h, 0, 8) != "EFI PART") { sb.AppendLine("ERROR: la copia no tiene cabecera GPT"); return; }
        int hs = BitConverter.ToInt32(h, 12); long alt = BitConverter.ToInt64(h, 32); long ent = BitConverter.ToInt64(h, 72);
        int n = BitConverter.ToInt32(h, 80), es = BitConverter.ToInt32(h, 84);
        int asec = (n * es + ss - 1) / ss;
        long need = (long)(2 + 2 * asec + 1) * ss;
        if (b.Length < need) { sb.AppendLine("ERROR: la copia esta incompleta (" + b.Length + " < " + need + ")"); return; }
        if (alt != totalSectors - 1) { sb.AppendLine("ERROR: la copia es de otro disco (alt=" + alt + " y el disco tiene " + totalSectors + " sectores)"); return; }
        byte[] ea = new byte[asec * ss]; Array.Copy(b, 2 * ss, ea, 0, asec * ss);
        byte[] bh = new byte[ss]; Array.Copy(b, (2 + 2 * asec) * ss, bh, 0, ss);
        long bent = BitConverter.ToInt64(bh, 72);
        byte[] dg = new byte[16]; Array.Copy(h, 56, dg, 0, 16);
        int used = 0; for (int i = 0; i < n; i++) if (!IsZero(ea, i * es, 16)) used++;
        List<int> dl = Dups(ea, n, es, dg);
        sb.AppendLine(string.Format("Copia: entradas={0} en uso={1} tabla LBA {2} copia LBA {3} (tabla copia {4}); repetidos a corregir={5}", n, used, ent, alt, bent, dl.Count));
        if (used < 60) { sb.AppendLine("ERROR: la copia no parece la GPT completa (menos de 60 particiones). No se escribe nada."); return; }
        foreach (int i in dl) Array.Copy(Guid.NewGuid().ToByteArray(), 0, ea, i * es + 16, 16);
        uint ec = Crc(ea, 0, n * es);
        SetCrcs(h, hs, ec); SetCrcs(bh, hs, ec);
        using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite, 1)) {
            byte[] cur = Rd(fs, 1, ss, ss);
            sb.AppendLine("Estado actual: cabecera tabla LBA " + BitConverter.ToInt64(cur, 72) + ", entradas " + BitConverter.ToInt32(cur, 80));
            Wr(fs, 0, ss, l0); Wr(fs, ent, ss, ea); Wr(fs, bent, ss, ea); Wr(fs, alt, ss, bh); Wr(fs, 1, ss, h);
            sb.AppendLine("Restaurado.");
            byte[] h2 = Rd(fs, 1, ss, ss); long e2l = BitConverter.ToInt64(h2, 72); byte[] e2 = Rd(fs, e2l, ss, asec * ss);
            byte[] bh2 = Rd(fs, alt, ss, ss); byte[] be2 = Rd(fs, BitConverter.ToInt64(bh2, 72), ss, asec * ss);
            int used2 = 0; for (int i = 0; i < n; i++) if (!IsZero(e2, i * es, 16)) used2++;
            uint c1 = BitConverter.ToUInt32(h2, 16); BitConverter.GetBytes((uint)0).CopyTo(h2, 16);
            uint c2 = BitConverter.ToUInt32(bh2, 16); BitConverter.GetBytes((uint)0).CopyTo(bh2, 16);
            bool same = true; for (int i = 0; i < n * es; i++) if (e2[i] != be2[i]) { same = false; break; }
            sb.AppendLine(string.Format("Verificacion: tabla LBA {0}, en uso={1}, repetidos={2}, crc cab.={3}, crc copia cab.={4}, crc tabla={5}, principal==copia={6}",
                e2l, used2, Dups(e2, n, es, dg).Count, c1 == Crc(h2, 0, hs), c2 == Crc(bh2, 0, hs), BitConverter.ToUInt32(h2, 88) == Crc(e2, 0, n * es), same));
        }
    }
}
'@

Set-Disk -Number $d -IsOffline $true -ErrorAction SilentlyContinue
Write-Host ("[0] inicio:            " + [GptTool]::Summary($raw, $ss))
$t = [Guid]'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7'
Write-Host ([GptTool]::SetTypes($raw, $ss, @($Esp, $Win), $t.ToByteArray()))
Write-Host ("[1] tras editar tipos: " + [GptTool]::Summary($raw, $ss))

Set-Disk -Number $d -IsOffline $false
Update-Disk -Number $d
Write-Host ("[2] disco online:      " + [GptTool]::Summary($raw, $ss))

foreach ($n in $Esp, $Win) {
    $p = Get-Partition -DiskNumber $d -PartitionNumber $n -ErrorAction SilentlyContinue
    if (-not $p) { Write-Host "No veo la particion $n en Windows." -ForegroundColor Red; continue }
    Write-Host ("Particion {0}: tipo={1} letra={2}" -f $n, $p.GptType, $p.DriveLetter)
    if (-not $p.DriveLetter) { Add-PartitionAccessPath -DiskNumber $d -PartitionNumber $n -AssignDriveLetter }
}
$pe = Get-Partition -DiskNumber $d -PartitionNumber $Esp -ErrorAction SilentlyContinue
$pw = Get-Partition -DiskNumber $d -PartitionNumber $Win -ErrorAction SilentlyContinue
$le = $pe.DriveLetter; $lw = $pw.DriveLetter
Write-Host "ESP = ${le}:   Windows = ${lw}:"
Write-Host ("[3] letras asignadas:  " + [GptTool]::Summary($raw, $ss))

$src = @("$kit\canary\canary3.sys", "$kit\dist-canary\canary3.sys", "$kit\canary3.sys") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($src -and $lw) {
    Copy-Item $src "${lw}:\Windows\System32\drivers\canary3.sys" -Force
    Write-Host "Copiado $src -> ${lw}:\Windows\System32\drivers\canary3.sys"
} else { Write-Host "No se copia canary3.sys (no hay archivo o no hay letra)." -ForegroundColor Yellow }
# --- Parche de partmgr.sys: la politica de GUID repetidos deja de poner el disco offline ---
if ($lw) {
    $pm = "${lw}:\Windows\System32\drivers\partmgr.sys"
    if (Test-Path $pm) {
        if (-not (Test-Path "$pm.orig")) {
            & takeown.exe /f $pm | Out-Null
            & icacls.exe $pm /grant "*S-1-5-32-544:F" | Out-Null
            Copy-Item $pm "$pm.orig" -Force
            Write-Host "Copia original: $pm.orig"
        } else {
            & takeown.exe /f $pm | Out-Null
            & icacls.exe $pm /grant "*S-1-5-32-544:F" | Out-Null
        }
        # Version ya parcheada Y firmada con el certificado de prueba (igual que canary3.sys): el parche sin firma daba 0xC0000428
        $pat = @("$kit\partmgr-parcheado.sys", "$kit\scripts\partmgr-parcheado.sys") | Where-Object { Test-Path $_ } | Select-Object -First 1
        $h0 = (Get-FileHash "$pm.orig" -Algorithm SHA256).Hash
        Write-Host "SHA256 partmgr.sys.orig: $h0"
        if ($h0 -ne '6B9F427CA3ED805E00EA9596D7DF337D440C6E455175E3121D15C8F119AB8D37') {
            Write-Host "ERROR: partmgr.sys.orig no es la version esperada; no se toca" -ForegroundColor Red
        } elseif (-not $pat) {
            Write-Host "ERROR: falta partmgr-parcheado.sys en la carpeta del kit" -ForegroundColor Red
        } else {
            Copy-Item $pat $pm -Force
            Write-Host ("partmgr.sys parcheado y firmado. SHA256: " + (Get-FileHash $pm -Algorithm SHA256).Hash)
        }
    } else { Write-Host "No encuentro $pm" -ForegroundColor Red }
}
# --- Parche de volmgr.sys: reconoce el tipo de particion de Samsung (userdata 1b81e7e6-...) como volumen normal ---
# Sin esto, la particion de Windows (tipo de fabrica de Samsung) no se registra en mountmgr y el arranque se queda
# esperando en IopMarkBootPartition. Con -SinVolmgr se vuelve a poner el original.
if ($lw) {
    $vm = "${lw}:\Windows\System32\drivers\volmgr.sys"
    if (Test-Path $vm) {
        & takeown.exe /f $vm | Out-Null
        & icacls.exe $vm /grant "*S-1-5-32-544:F" | Out-Null
        if (-not (Test-Path "$vm.orig")) { Copy-Item $vm "$vm.orig" -Force; Write-Host "Copia original: $vm.orig" }
        $hv = (Get-FileHash "$vm.orig" -Algorithm SHA256).Hash
        Write-Host "SHA256 volmgr.sys.orig: $hv"
        $vpat = @("$kit\volmgr-parcheado.sys", "$kit\scripts\volmgr-parcheado.sys") | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($SinVolmgr) {
            Copy-Item "$vm.orig" $vm -Force
            Write-Host "volmgr.sys original restaurado." -ForegroundColor Yellow
        } elseif ($hv -ne '0769135F1C9E8A3020E23DCAB52D69F590B0716916BD627117F48F9633B1A8AB') {
            Write-Host "ERROR: volmgr.sys.orig no es la version esperada; no se toca" -ForegroundColor Red
        } elseif (-not $vpat) {
            Write-Host "AVISO: falta volmgr-parcheado.sys en la carpeta del kit; volmgr sin parchear" -ForegroundColor Yellow
        } else {
            Copy-Item $vpat $vm -Force
            Write-Host ("volmgr.sys parcheado y firmado. SHA256: " + (Get-FileHash $vm -Algorithm SHA256).Hash)
        }
    } else { Write-Host "No encuentro $vm" -ForegroundColor Red }
}
# --- Servicios que se desactivan (Start=4) en el registro de Windows. Por defecto: dam (pantalla azul 0xCE en dam.sys) ---
# Para no desactivar ninguno: -Desactivar @()
$Desactivar = @($Desactivar | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($lw -and ($Desactivar.Count -gt 0 -or $Activar.Count -gt 0 -or $Keepalive -ne -1)) {
    $hive = "${lw}:\Windows\System32\config\SYSTEM"
    & reg.exe load 'HKLM\TSYS17' $hive | Out-Null
    if ($LASTEXITCODE -eq 0) {
        foreach ($svc in $Desactivar) {
            $k = "HKLM\TSYS17\ControlSet001\Services\$svc"
            & reg.exe query $k /v Start 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) {
                & reg.exe add $k /v Start /t REG_DWORD /d 4 /f | Out-Null
                Write-Host "Servicio $svc desactivado (Start=4)"
            } else { Write-Host "Servicio $svc no existe en el registro" -ForegroundColor Yellow }
        }
        $Activar = @($Activar | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        foreach ($svc in $Activar) {
            $k = "HKLM\TSYS17\ControlSet001\Services\$svc"
            & reg.exe query $k /v Start 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { & reg.exe add $k /v Start /t REG_DWORD /d 0 /f | Out-Null; Write-Host "Servicio $svc activado (Start=0, arranque)" }
            else { Write-Host "Servicio $svc no existe en el registro" -ForegroundColor Yellow }
        }
        # keepalive.sys: -Keepalive 0 (temporizador + hilo), 1 (solo temporizador), 2 (solo hilo), -2 (desactivar)
        if ($Keepalive -ge 0) {
            $ksrc = @("$kit\keepalive.sys", "$kit\scripts\keepalive.sys") | Where-Object { Test-Path $_ } | Select-Object -First 1
            if ($ksrc) {
                Copy-Item $ksrc "${lw}:\Windows\System32\drivers\keepalive.sys" -Force
                $k = 'HKLM\TSYS17\ControlSet001\Services\keepalive'
                & reg.exe add $k /v Type /t REG_DWORD /d 1 /f | Out-Null
                & reg.exe add $k /v Start /t REG_DWORD /d 0 /f | Out-Null
                & reg.exe add $k /v ErrorControl /t REG_DWORD /d 0 /f | Out-Null
                & reg.exe add $k /v Group /t REG_SZ /d 'Boot Bus Extender' /f | Out-Null
                & reg.exe add $k /v ImagePath /t REG_EXPAND_SZ /d 'System32\drivers\keepalive.sys' /f | Out-Null
                & reg.exe add $k /v Modo /t REG_DWORD /d $Keepalive /f | Out-Null
                Write-Host "keepalive.sys instalado (Modo=$Keepalive)"
            } else { Write-Host "ERROR: falta keepalive.sys en la carpeta del kit" -ForegroundColor Red }
        } elseif ($Keepalive -eq -2) {
            & reg.exe add 'HKLM\TSYS17\ControlSet001\Services\keepalive' /v Start /t REG_DWORD /d 4 /f | Out-Null
            Write-Host "keepalive desactivado"
        }
        # "El equipo se reinicio de manera inesperada...": marcar setup.exe como completado (ChildCompletion = 3)
        if ($ArreglarSetup) {
            & reg.exe query 'HKLM\TSYS17\Setup\Status\ChildCompletion' 2>&1 | Out-Host
            & reg.exe add 'HKLM\TSYS17\Setup\Status\ChildCompletion' /v setup.exe /t REG_DWORD /d 3 /f | Out-Null
            Write-Host "Setup\Status\ChildCompletion\setup.exe = 3"
        }
        [gc]::Collect()
        & reg.exe unload 'HKLM\TSYS17' | Out-Null
    } else { Write-Host "ERROR: no se pudo cargar $hive" -ForegroundColor Red }
}
# --- Evitar la pantalla "Recovery / Your PC couldn't start properly" tras varios arranques fallidos ---
if ($le) {
    $bcd0 = "${le}:\EFI\Microsoft\Boot\BCD"
    & bcdedit.exe /store $bcd0 /set "{default}" bootstatuspolicy IgnoreAllFailures | Out-Null
    & bcdedit.exe /store $bcd0 /set "{default}" recoveryenabled No | Out-Null
    Write-Host "BCD: bootstatuspolicy = IgnoreAllFailures"
}
if ($lw) {
    foreach ($bs in @("${lw}:\Windows\bootstat.dat", "${le}:\EFI\Microsoft\Boot\bootstat.dat")) {
        if (Test-Path $bs) { Remove-Item $bs -Force -ErrorAction SilentlyContinue; Write-Host "Borrado $bs" }
    }
}
# --- Inicio de sesion automatico: "El dominio especificado no existe" -> DefaultDomainName = nombre del equipo local (".") ---
if ($lw -and $ArreglarLogon) {
    $sw = "${lw}:\Windows\System32\config\SOFTWARE"
    & reg.exe load 'HKLM\TSW17' $sw | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $k = 'HKLM\TSW17\Microsoft\Windows NT\CurrentVersion\Winlogon'
        & reg.exe query $k /v AutoAdminLogon 2>&1 | Out-Host
        & reg.exe query $k /v DefaultUserName 2>&1 | Out-Host
        & reg.exe query $k /v DefaultDomainName 2>&1 | Out-Host
        $cn = $null
        & reg.exe load 'HKLM\TSYS18' "${lw}:\Windows\System32\config\SYSTEM" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $cn = (Get-ItemProperty 'HKLM:\TSYS18\ControlSet001\Control\ComputerName\ComputerName' -ErrorAction SilentlyContinue).ComputerName
            [gc]::Collect(); & reg.exe unload 'HKLM\TSYS18' | Out-Null
        }
        if (-not $cn) { $cn = 'DESKTOP-DJPRG0N' }
        & reg.exe add $k /v DefaultDomainName /t REG_SZ /d $cn /f | Out-Null
        & reg.exe add $k /v AltDefaultDomainName /t REG_SZ /d $cn /f | Out-Null
        & reg.exe add $k /v AutoAdminLogon /t REG_SZ /d '1' /f | Out-Null
        & reg.exe add $k /v ForceAutoLogon /t REG_SZ /d '1' /f | Out-Null
        & reg.exe add $k /v DefaultUserName /t REG_SZ /d 'tablet' /f | Out-Null
        & reg.exe add $k /v DefaultPassword /t REG_SZ /d 'Cambiar123' /f | Out-Null
        & reg.exe delete $k /v AutoLogonCount /f 2>&1 | Out-Null
        Write-Host "Winlogon: dominio = $cn, usuario = tablet, contrasena en claro, AutoAdminLogon = 1"
        [gc]::Collect()
        & reg.exe unload 'HKLM\TSW17' | Out-Null
    } else { Write-Host "ERROR: no se pudo cargar $sw" -ForegroundColor Red }
}
# --- Consola de rescate: el boton de Accesibilidad de la pantalla de inicio abre un cmd como SYSTEM ---
if ($lw -and ($Consola -or $QuitarConsola)) {
    $u = "${lw}:\Windows\System32\Utilman.exe"; $uo = "$u.orig"
    & takeown.exe /f $u /a 2>&1 | Out-Null
    & icacls.exe $u /grant '*S-1-5-32-544:F' 2>&1 | Out-Null
    if ($Consola) {
        if (-not (Test-Path $uo)) { Copy-Item $u $uo -Force }
        Copy-Item "${lw}:\Windows\System32\cmd.exe" $u -Force
        Write-Host "Consola: Utilman.exe = cmd.exe (copia original en Utilman.exe.orig)"
    } elseif (Test-Path $uo) {
        Copy-Item $uo $u -Force; Write-Host "Consola quitada: Utilman.exe restaurado"
    }
    Get-FileHash $u, "${lw}:\Windows\System32\cmd.exe" | Format-Table -AutoSize | Out-Host
}
# --- Numero de nucleos (numproc en el BCD). -Numproc 0 = todos los nucleos; -Numproc N = solo N; sin el parametro no se toca ---
if ($le -and $Numproc -ge 0) {
    $bcd = "${le}:\EFI\Microsoft\Boot\BCD"
    if ($Numproc -eq 0) { & bcdedit.exe /store $bcd /deletevalue "{default}" numproc | Out-Host; Write-Host "numproc quitado: Windows usara todos los nucleos" }
    else { & bcdedit.exe /store $bcd /set "{default}" numproc $Numproc | Out-Host; Write-Host "numproc = $Numproc" }
}
Set-Location $kit
if ($lw) { Write-VolumeCache -DriveLetter $lw }
if ($le) { Write-VolumeCache -DriveLetter $le }
Write-Host ("[4] tras copiar:       " + [GptTool]::Summary($raw, $ss))
Set-Disk -Number $d -IsOffline $true
Write-Host ("[5] disco offline:     " + [GptTool]::Summary($raw, $ss))
Write-Host ""
Write-Host "Listo. Si arriba pone 'partmgr.sys parcheado' (o 'ya estaba parcheado'), reinicia la tablet con Encendido. No ejecutes nada mas en el PC." -ForegroundColor Green
Stop-Transcript | Out-Null
