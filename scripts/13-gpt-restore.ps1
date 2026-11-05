# Restaura la GPT original (80 particiones) desde gpt-backup.bin, con los GUID repetidos corregidos.
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-gptrestore.txt'
Start-Transcript -Path $out -Force | Out-Null

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Ejecuta como administrador." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1
}
$disk = Get-Disk | Where-Object FriendlyName -like '*Linux File-Stor*' | Select-Object -First 1
if (-not $disk) { Write-Host "No encuentro el disco de la tablet (3-massstorage.cmd)." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1 }
$bk = Join-Path $kit 'gpt-backup.bin'
if (-not (Test-Path $bk)) { Write-Host "No existe gpt-backup.bin en la carpeta del kit." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1 }
$d = $disk.Number
$ss = [int]$disk.LogicalSectorSize
$total = [long]($disk.Size / $ss)
Write-Host ("Disco {0}  sector={1}  sectores={2}" -f $d, $ss, $total)
Set-Disk -Number $d -IsOffline $true -ErrorAction SilentlyContinue

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
public static class GptRestore {
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
    public static string Run(string path, int ss, long totalSectors, string backupPath) {
        StringBuilder sb = new StringBuilder();
        try { Inner(sb, path, ss, totalSectors, backupPath); }
        catch (Exception ex) { sb.AppendLine("ERROR: " + ex.Message); }
        return sb.ToString();
    }
    static void Inner(StringBuilder sb, string path, int ss, long totalSectors, string backupPath) {
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


try { Write-Host ([GptRestore]::Run("\\.\PhysicalDrive$d", $ss, $total, $bk)) }
catch { Write-Host ("ERROR: " + $_.Exception.ToString()) -ForegroundColor Red }
Stop-Transcript | Out-Null
