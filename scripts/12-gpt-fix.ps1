# Lee la GPT REAL del disco de la tablet (sin pasar por Windows) y, si hay GUID de particion repetidos o a cero,
# los reemplaza por GUID nuevos (recalcula CRC de la tabla principal y de la copia de seguridad).
# Antes de escribir guarda una copia de los sectores de la GPT en gpt-backup.bin.
$ErrorActionPreference = 'Continue'
$kit = Split-Path -Parent $PSScriptRoot
$out = Join-Path $kit 'diag-gptfix.txt'
Start-Transcript -Path $out -Force | Out-Null

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Ejecuta como administrador." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1
}
$disk = Get-Disk | Where-Object FriendlyName -like '*Linux File-Stor*' | Select-Object -First 1
if (-not $disk) { Write-Host "No encuentro el disco de la tablet (3-massstorage.cmd)." -ForegroundColor Red; Stop-Transcript | Out-Null; exit 1 }
$d = $disk.Number
$ss = [int]$disk.LogicalSectorSize
Write-Host ("Disco {0}  sector={1}  tamano={2} bytes" -f $d, $ss, $disk.Size)
Set-Disk -Number $d -IsOffline $true -ErrorAction SilentlyContinue

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
public static class GptFix {
    static uint[] T;
    static uint Crc(byte[] b, int off, int len) {
        if (T == null) { T = new uint[256]; for (uint i = 0; i < 256; i++) { uint c = i; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); T[i] = c; } }
        uint r = 0xFFFFFFFFu; for (int i = 0; i < len; i++) r = T[(r ^ b[off + i]) & 0xFF] ^ (r >> 8); return ~r;
    }
    static byte[] Rd(FileStream fs, long lba, int ss, int len) { byte[] b = new byte[len]; fs.Seek(lba * ss, SeekOrigin.Begin); int got = 0; while (got < len) { int n = fs.Read(b, got, len - got); if (n <= 0) break; got += n; } return b; }
    static void Wr(FileStream fs, long lba, int ss, byte[] b) { fs.Seek(lba * ss, SeekOrigin.Begin); fs.Write(b, 0, b.Length); fs.Flush(); }
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
    public static string Run(string path, int ss, bool write, string backupPath) {
        StringBuilder sb = new StringBuilder();
        try { RunInner(sb, path, ss, write, backupPath); }
        catch (Exception ex) { sb.AppendLine("ERROR: " + ex.Message); }
        return sb.ToString();
    }
    static string RunInner(StringBuilder sb, string path, int ss, bool write, string backupPath) {
        using (FileStream fs = new FileStream(path, FileMode.Open, write ? FileAccess.ReadWrite : FileAccess.Read, FileShare.ReadWrite, 1)) {
            byte[] h = Rd(fs, 1, ss, ss);
            if (Encoding.ASCII.GetString(h, 0, 8) != "EFI PART") { sb.AppendLine("ERROR: no hay cabecera GPT en el LBA 1"); return ""; }
            int hs = BitConverter.ToInt32(h, 12); long alt = BitConverter.ToInt64(h, 32); long ent = BitConverter.ToInt64(h, 72);
            int n = BitConverter.ToInt32(h, 80), es = BitConverter.ToInt32(h, 84);
            int asec = (n * es + ss - 1) / ss;
            sb.AppendLine(string.Format("Cabecera primaria: alt={0} tabla={1} n={2} es={3}", alt, ent, n, es));
            byte[] dg = new byte[16]; Array.Copy(h, 56, dg, 0, 16);
            byte[] ea = Rd(fs, ent, ss, asec * ss);
            byte[] bh = Rd(fs, alt, ss, ss); long bent = BitConverter.ToInt64(bh, 72);
            byte[] bea = Rd(fs, bent, ss, asec * ss);
            sb.AppendLine(string.Format("GPT: entradas={0} tamano={1} tabla en LBA {2}, copia en LBA {3} (tabla copia en {4})", n, es, ent, alt, bent));
            bool same = true; for (int i = 0; i < n * es; i++) if (ea[i] != bea[i]) { same = false; break; }
            sb.AppendLine("Tabla principal == copia de seguridad: " + same);
            int used = 0; for (int i = 0; i < n; i++) if (!IsZero(ea, i * es, 16)) used++;
            sb.AppendLine("Particiones en uso: " + used);
            for (int i = 0; i < 6 && i < n; i++) sb.AppendLine(string.Format("  #{0}: unico={1} tipo={2}", i + 1, G(ea, i * es + 16), G(ea, i * es)));
            List<int> dl = Dups(ea, n, es, dg);
            sb.AppendLine("Entradas con GUID unico repetido/cero/igual al disco (en el disco real): " + dl.Count);
            if (dl.Count > 0) { StringBuilder s2 = new StringBuilder(); foreach (int i in dl) s2.Append((i + 1) + " "); sb.AppendLine("  particiones: " + s2); }
            if (dl.Count == 0 || !write) return sb.ToString();
            // copia de seguridad
            using (FileStream bf = new FileStream(backupPath, FileMode.Create, FileAccess.Write)) {
                byte[] l0 = Rd(fs, 0, ss, ss); bf.Write(l0, 0, l0.Length); bf.Write(h, 0, h.Length); bf.Write(ea, 0, ea.Length); bf.Write(bea, 0, bea.Length); bf.Write(bh, 0, bh.Length);
            }
            sb.AppendLine("Copia guardada en " + backupPath + " (LBA0, cabecera, tabla, tabla copia, cabecera copia)");
            foreach (int i in dl) Array.Copy(Guid.NewGuid().ToByteArray(), 0, ea, i * es + 16, 16);
            uint ec = Crc(ea, 0, n * es);
            SetCrcs(h, hs, ec); SetCrcs(bh, hs, ec);
            Wr(fs, ent, ss, ea); Wr(fs, 1, ss, h); Wr(fs, bent, ss, ea); Wr(fs, alt, ss, bh);
            sb.AppendLine("Escrito: " + dl.Count + " GUID nuevos.");
            // verificacion
            byte[] h2 = Rd(fs, 1, ss, ss); byte[] e2 = Rd(fs, ent, ss, asec * ss);
            List<int> d2 = Dups(e2, n, es, dg);
            uint c = BitConverter.ToUInt32(h2, 16); BitConverter.GetBytes((uint)0).CopyTo(h2, 16);
            sb.AppendLine("Verificacion: repetidos=" + d2.Count + " crc cabecera ok=" + (c == Crc(h2, 0, hs)) + " crc tabla ok=" + (BitConverter.ToUInt32(h2, 88) == Crc(e2, 0, n * es)));
        }
        return sb.ToString();
    }
}
'@

$path = "\\.\PhysicalDrive$d"
try {
    $r = [GptFix]::Run($path, $ss, $true, (Join-Path $kit 'gpt-backup.bin'))
    Write-Host $r
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    Write-Host ($_.Exception.ToString())
}
Stop-Transcript | Out-Null
