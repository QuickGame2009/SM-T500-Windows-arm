# Como 5-preparar (y 14), pero con parche de partmgr.sys;  SIN dejar que Windows reescriba la GPT: el cambio de tipo de las particiones ESP/Windows
# se hace directamente en la tabla (con disco offline), y despues Windows solo monta los volumenes y copia archivos.
param([int]$Esp = 76, [int]$Win = 80)
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-setup.txt'
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

# --- Pone a 0 las marcas de "instalacion a medias" del registro SYSTEM (copia de seguridad antes) ---
if ($lw) {
    $hv = "${lw}:\Windows\System32\config\SYSTEM"
    $bkp = Join-Path $kit 'SYSTEM.antes-de-reset-setup'
    if (-not (Test-Path $bkp)) { Copy-Item $hv $bkp -Force; Write-Host "Copia de seguridad: $bkp" }
    & reg.exe load HKLM\TSYS $hv | Out-Host
    if (Test-Path HKLM:\TSYS) {
        $k = 'HKLM\TSYS\Setup'
        & reg.exe add $k /v SystemSetupInProgress /t REG_DWORD /d 0 /f | Out-Host
        & reg.exe add $k /v OOBEInProgress /t REG_DWORD /d 0 /f | Out-Host
        & reg.exe add $k /v SetupPhase /t REG_DWORD /d 0 /f | Out-Host
        & reg.exe add $k /v SetupType /t REG_DWORD /d 0 /f | Out-Host
        & reg.exe add $k /v CmdLine /t REG_SZ /d "" /f | Out-Host
        & reg.exe query $k | Out-Host
        [gc]::Collect(); Start-Sleep 1
        & reg.exe unload HKLM\TSYS | Out-Host
    }
}
Set-Location $kit
if ($lw) { Write-VolumeCache -DriveLetter $lw }
if ($le) { Write-VolumeCache -DriveLetter $le }
Set-Disk -Number $d -IsOffline $true
Write-Host ("[5] disco offline:     " + [GptTool]::Summary($raw, $ss))
Write-Host "Listo. Envia diag-setup.txt." -ForegroundColor Green
Stop-Transcript | Out-Null
