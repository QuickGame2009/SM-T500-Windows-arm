# Parche definitivo de la GPT, hecho DESDE la tablet (TWRP) con dd + sync, que si persiste en el eMMC.
# Usa gpt-backup.bin (la GPT original): corrige GUID repetidos y deja ESP/Windows como "datos basicos".
# Ejecutar con la tablet en TWRP con adb (SIN haber lanzado 3-massstorage en este arranque).
. (Join-Path $PSScriptRoot 'common.ps1')
$kit = Split-Path -Parent $PSScriptRoot
$bk = Join-Path $kit 'gpt-backup.bin'
if (-not (Test-Path $bk)) { throw "No existe $bk" }

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
public static class GptPatch {
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
    public static string Build(string backupPath, string outDir, int[] parts, byte[] typeGuid, long totalSectors, int ss) {
        byte[] b = File.ReadAllBytes(backupPath);
        byte[] h = new byte[ss]; Array.Copy(b, ss, h, 0, ss);
        if (Encoding.ASCII.GetString(h, 0, 8) != "EFI PART") return "ERROR: la copia no tiene cabecera GPT";
        int hs = BitConverter.ToInt32(h, 12); long alt = BitConverter.ToInt64(h, 32); long ent = BitConverter.ToInt64(h, 72);
        int n = BitConverter.ToInt32(h, 80), es = BitConverter.ToInt32(h, 84); int asec = (n * es + ss - 1) / ss;
        if (b.Length < (long)(2 + 2 * asec + 1) * ss) return "ERROR: la copia esta incompleta";
        if (alt != totalSectors - 1) return "ERROR: la copia no es de este disco (alt=" + alt + ", sectores=" + totalSectors + ")";
        if (ent != 2) return "ERROR: la copia no es la GPT original (tabla en LBA " + ent + ")";
        byte[] ea = new byte[asec * ss]; Array.Copy(b, 2 * ss, ea, 0, asec * ss);
        byte[] bh = new byte[ss]; Array.Copy(b, (2 + 2 * asec) * ss, bh, 0, ss);
        long bent = BitConverter.ToInt64(bh, 72);
        byte[] dg = new byte[16]; Array.Copy(h, 56, dg, 0, 16);
        int used = 0; for (int i = 0; i < n; i++) if (!IsZero(ea, i * es, 16)) used++;
        if (used < 60) return "ERROR: la copia no parece la GPT completa (" + used + " particiones)";
        foreach (int p in parts) { int o = (p - 1) * es; if (p < 1 || p > n || IsZero(ea, o, 16)) return "ERROR: la particion " + p + " no existe"; Array.Copy(typeGuid, 0, ea, o, 16); }
        List<int> dl = Dups(ea, n, es, dg);
        foreach (int i in dl) Array.Copy(Guid.NewGuid().ToByteArray(), 0, ea, i * es + 16, 16);
        uint ec = Crc(ea, 0, n * es); SetCrcs(h, hs, ec); SetCrcs(bh, hs, ec);
        byte[] p1 = new byte[ss + asec * ss]; Array.Copy(h, 0, p1, 0, ss); Array.Copy(ea, 0, p1, ss, asec * ss);
        File.WriteAllBytes(Path.Combine(outDir, "gpt-p1.bin"), p1);
        File.WriteAllBytes(Path.Combine(outDir, "gpt-p2.bin"), ea);
        File.WriteAllBytes(Path.Combine(outDir, "gpt-p3.bin"), bh);
        System.Security.Cryptography.MD5 md5 = System.Security.Cryptography.MD5.Create();
        string m1 = BitConverter.ToString(md5.ComputeHash(p1)).Replace("-", "").ToLower();
        string m2 = BitConverter.ToString(md5.ComputeHash(ea)).Replace("-", "").ToLower();
        string m3 = BitConverter.ToString(md5.ComputeHash(bh)).Replace("-", "").ToLower();
        return string.Format("OK {0} {1} {2} {3} {4} {5} {6} {7}", m1, m2, m3, bent, alt, asec, dl.Count, used);
    }
}
'@

Assert-Device
$tot = (Invoke-Adb @('shell', 'echo $(( $(cat /sys/block/mmcblk0/size) ))') 30).Out.Trim()
if ($tot -notmatch '^\d+$') { throw "No pude leer el tamano de mmcblk0: $tot" }
Write-Host "mmcblk0: $tot sectores"
$t = [Guid]'ebd0a0a2-b9e5-4433-87c0-68b6b72699c7'
$work = Join-Path $kit 'gpt-parche'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$res = [GptPatch]::Build($bk, $work, [int[]]@(76, 80), $t.ToByteArray(), [long]$tot, 512)
Write-Host $res
if (-not $res.StartsWith('OK ')) { throw $res }
$f = $res.Split(' ')
$m1 = $f[1]; $m2 = $f[2]; $m3 = $f[3]; $bent = $f[4]; $alt = $f[5]; $asec = [int]$f[6]
Write-Host ("GUID corregidos: {0}   particiones en uso: {1}" -f $f[7], $f[8])

foreach ($n in 'gpt-p1.bin', 'gpt-p2.bin', 'gpt-p3.bin') {
    $r = Invoke-Adb @('push', (Join-Path $work $n), "/cache/$n") 60
    if ($r.Code -ne 0) { throw "adb push $n fallo: $($r.Err) $($r.Out)" }
}
$cnt1 = 1 + $asec
$sh = @"
#!/sbin/sh
D=/dev/block/mmcblk0
dd if=/cache/gpt-p1.bin of=`$D bs=512 seek=1 conv=notrunc 2>&1
dd if=/cache/gpt-p2.bin of=`$D bs=512 seek=$bent conv=notrunc 2>&1
dd if=/cache/gpt-p3.bin of=`$D bs=512 seek=$alt conv=notrunc 2>&1
sync
echo 3 > /proc/sys/vm/drop_caches
sleep 1
echo R1 `$(dd if=`$D bs=512 skip=1 count=$cnt1 2>/dev/null | md5sum)
echo R2 `$(dd if=`$D bs=512 skip=$bent count=$asec 2>/dev/null | md5sum)
echo R3 `$(dd if=`$D bs=512 skip=$alt count=1 2>/dev/null | md5sum)
"@
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) 'gpt-patch.sh'
[System.IO.File]::WriteAllText($tmp, ($sh -replace "`r`n", "`n"), (New-Object System.Text.UTF8Encoding($false)))
$r = Invoke-Adb @('push', $tmp, '/cache/gpt-patch.sh') 30
if ($r.Code -ne 0) { throw "adb push script fallo: $($r.Err)" }
Write-Host "Escribiendo la GPT en el eMMC..."
$run = Invoke-Adb @('shell', 'sh /cache/gpt-patch.sh') 120
Write-Host $run.Out
if ($run.Err.Trim() -ne '') { Write-Host $run.Err }
$ok1 = $run.Out -match ("R1 " + $m1); $ok2 = $run.Out -match ("R2 " + $m2); $ok3 = $run.Out -match ("R3 " + $m3)
Write-Host ("Verificacion (lectura desde la tablet tras sync): tabla principal={0}  tabla copia={1}  cabecera copia={2}" -f $ok1, $ok2, $ok3)
if ($ok1 -and $ok2 -and $ok3) { Write-Host "OK: la GPT esta escrita y verificada en el eMMC. Ya puedes reiniciar la tablet con Encendido." -ForegroundColor Green }
else { Write-Host "FALLO: no coincide. No reinicies; envia esta salida." -ForegroundColor Red }
