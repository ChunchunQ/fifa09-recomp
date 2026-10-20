using System.IO;
using System.Text;

namespace Fifa09Launcher;

/// <summary>
/// Applies the port's game-file mods before launch:
///   mods\ps_buttons\data0\...  PlayStation button prompts (optional)
///   mods\signature\data1\...   "PC port by Chun" on the start screen (always)
///   mods\skipintro\data1\...   skip the intro videos (optional)
///   mods\nobk_adboards\data3\... NOBK Online ad boards around the pitch (always)
///   mods\nobk_text\locale.big   game texts with "Xbox LIVE" -> "NOBK.Online" (always)
/// plus the language-screen switch in data\loc\locale.big.
///
/// Archives are patched like tools\bigpatch.ps1: the pristine archive is kept
/// as &lt;file&gt;.orig, replacement files are appended at the end and only the
/// directory entries in the .big header and the .bh index are rewritten.
/// A small &lt;file&gt;.modstate records which mods are in, so nothing is
/// rewritten when the selection did not change.
/// </summary>
public static class ModManager
{
    public static void Sync(string gameDir, string modsDir, bool psButtons, bool skipIntro)
    {
        var wanted = new Dictionary<string, List<string>>
        {
            ["data0"] = new(),
            ["data1"] = new() { "signature" },
            ["data3"] = new() { "nobk_adboards" },
        };
        if (psButtons) wanted["data0"].Add("ps_buttons");
        if (skipIntro) wanted["data1"].Add("skipintro");

        foreach (var (archive, mods) in wanted)
        {
            var big = Path.Combine(gameDir, archive + ".big");
            if (!File.Exists(big)) continue;
            var present = mods.Where(m => Directory.Exists(Path.Combine(modsDir, m, archive))).ToList();
            var dirs = present.Select(m => Path.Combine(modsDir, m, archive)).ToList();
            // The content fingerprint makes an update with new mod files (same names and
            // sizes, e.g. new ad boards) re-patch the archive once.
            SyncArchive(big, dirs, dirs.Count == 0 ? "" : string.Join(",", present) + "#" + Fingerprint(dirs));
        }

        var locale = Path.Combine(gameDir, "data", "loc", "locale.big");
        ReplaceLocale(locale, Path.Combine(modsDir, "nobk_text", "locale.big"));
        DisableLanguageSelect(locale);
    }

    static string Fingerprint(List<string> dirs)
    {
        using var sha = System.Security.Cryptography.SHA256.Create();
        foreach (var dir in dirs)
            foreach (var file in Directory.GetFiles(dir, "*", SearchOption.AllDirectories).OrderBy(f => f, StringComparer.Ordinal))
            {
                var name = Encoding.UTF8.GetBytes(Path.GetRelativePath(dir, file).Replace('\\', '/') + "\n");
                sha.TransformBlock(name, 0, name.Length, null, 0);
                var data = File.ReadAllBytes(file);
                sha.TransformBlock(data, 0, data.Length, null, 0);
            }
        sha.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
        return Convert.ToHexString(sha.Hash!).Substring(0, 12);
    }

    // The original locale.big of the supported game version (4360448 bytes).
    const long OriginalLocaleSize = 4360448;

    // locale.big is a loose file: the mod is the whole archive (made from the original by
    // tools/locpatch.py). The original stays as locale.big.orig.
    static void ReplaceLocale(string localeBig, string modFile)
    {
        if (!File.Exists(localeBig) || !File.Exists(modFile)) return;
        var statePath = localeBig + ".modstate";
        var mod = new FileInfo(modFile);
        string expect = "nobk_text|" + mod.Length + "|" + mod.LastWriteTimeUtc.Ticks;
        if (File.Exists(statePath) && File.ReadAllText(statePath) == expect) return;
        var orig = localeBig + ".orig";
        if (!File.Exists(orig))
        {
            if (new FileInfo(localeBig).Length != OriginalLocaleSize) return;   // other version: leave it alone
            File.Copy(localeBig, orig);
        }
        if (new FileInfo(orig).Length != OriginalLocaleSize) return;
        File.Copy(modFile, localeBig, true);
        File.WriteAllText(statePath, expect);
    }

    static void SyncArchive(string big, List<string> modDirs, string key)
    {
        var bh = Path.ChangeExtension(big, ".bh");
        var statePath = big + ".modstate";
        string state = File.Exists(statePath) ? File.ReadAllText(statePath) : "";
        string expect = key + "|" + new FileInfo(big).Length;
        if (state == expect) return;                       // already as wanted
        if (state == "" && key == "" && !File.Exists(big + ".orig")) return;   // never patched, nothing wanted

        foreach (var f in new[] { big, bh })
        {
            if (!File.Exists(f + ".orig")) File.Copy(f, f + ".orig");
            File.Copy(f + ".orig", f, true);               // start from the original
        }

        var jobs = new List<(string entry, string file)>();
        foreach (var dir in modDirs)
            foreach (var file in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
                jobs.Add((Path.GetRelativePath(dir, file).Replace('\\', '/'), file));
        if (jobs.Count > 0) BigPatcher.Apply(big, bh, jobs);

        File.WriteAllText(statePath, key + "|" + new FileInfo(big).Length);
    }

    // locale.ini inside locale.big: USE_LANGUAGE_SELECT = 1 -> 0 (same length, in place).
    static void DisableLanguageSelect(string localeBig)
    {
        if (!File.Exists(localeBig)) return;
        var bytes = File.ReadAllBytes(localeBig);
        var pat = Encoding.ASCII.GetBytes("USE_LANGUAGE_SELECT = ");
        int at = IndexOf(bytes, pat);
        if (at < 0 || bytes[at + pat.Length] != (byte)'1') return;
        if (!File.Exists(localeBig + ".orig")) File.Copy(localeBig, localeBig + ".orig");
        bytes[at + pat.Length] = (byte)'0';
        File.WriteAllBytes(localeBig, bytes);
    }

    static int IndexOf(byte[] hay, byte[] needle)
    {
        for (int i = 0; i <= hay.Length - needle.Length; i++)
        {
            int k = 0;
            while (k < needle.Length && hay[i + k] == needle[k]) k++;
            if (k == needle.Length) return i;
        }
        return -1;
    }
}

/// <summary>C# version of tools\bigpatch.ps1 (BIG4 + .bh).</summary>
public static class BigPatcher
{
    static uint Be(byte[] b, int o) => (uint)(b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]);
    static void Be(byte[] b, int o, long v)
    {
        b[o] = (byte)(v >> 24); b[o + 1] = (byte)(v >> 16); b[o + 2] = (byte)(v >> 8); b[o + 3] = (byte)v;
    }
    static long Align(long v, long a) => (v + a - 1) / a * a;

    public static void Apply(string bigPath, string bhPath, List<(string entry, string file)> jobs)
    {
        var moves = new List<(uint oldOff, long newOff, int size, long unpacked)>();
        long dataEnd;
        using (var fs = new FileStream(bigPath, FileMode.Open, FileAccess.ReadWrite))
        {
            var head = new byte[16];
            fs.ReadExactly(head);
            if (Encoding.ASCII.GetString(head, 0, 4) != "BIG4") throw new InvalidDataException("Not a BIG4 archive: " + bigPath);
            int count = (int)Be(head, 8), hlen = (int)Be(head, 12);
            var dir = new byte[hlen];
            fs.Position = 0;
            fs.ReadExactly(dir);

            var entries = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            int pos = 16;
            for (int n = 0; n < count; n++)
            {
                int end = pos + 8;
                while (dir[end] != 0) end++;
                entries[Encoding.ASCII.GetString(dir, pos + 8, end - pos - 8)] = pos;
                pos = end + 1;
            }

            foreach (var (entry, file) in jobs)
            {
                if (!entries.TryGetValue(entry, out int epos))
                    throw new InvalidDataException($"'{entry}' not found in {Path.GetFileName(bigPath)}");
                var payload = File.ReadAllBytes(file);
                long unpacked = payload.Length;
                if (payload.Length > 5 && payload[1] == 0xFB)
                {
                    if (payload[0] == 0x10) unpacked = payload[2] << 16 | payload[3] << 8 | payload[4];
                    else if (payload[0] == 0x90) unpacked = Be(payload, 2);
                }
                uint oldOff = Be(dir, epos);
                long newOff = Align(fs.Length, 16);
                fs.SetLength(newOff);
                fs.Position = newOff;
                fs.Write(payload);
                Be(dir, epos, newOff);
                Be(dir, epos + 4, payload.Length);
                moves.Add((oldOff, newOff, payload.Length, unpacked));
            }

            dataEnd = Align(fs.Length, 16);
            fs.SetLength(Align(fs.Length, 0x8000));
            BitConverter.GetBytes((uint)fs.Length).CopyTo(dir, 4);   // total size, little endian
            fs.Position = 0;
            fs.Write(dir);
        }

        var index = File.ReadAllBytes(bhPath);
        int icount = (int)Be(index, 8);
        var done = new HashSet<uint>();
        foreach (var mv in moves)
        {
            // Files with identical content share one copy in the archive (e.g. adboard_0/1/33):
            // the first move already re-pointed every index entry of that copy.
            if (!done.Add(mv.oldOff)) continue;
            bool found = false;
            for (int n = 0; n < icount; n++)
            {
                int at = 16 + n * 20;
                if (Be(index, at) != mv.oldOff) continue;
                Be(index, at, mv.newOff);
                Be(index, at + 4, Align(mv.size, 16));
                Be(index, at + 8, mv.unpacked);
                found = true;
            }
            if (!found) throw new InvalidDataException($"Offset 0x{mv.oldOff:X} not found in {Path.GetFileName(bhPath)}");
        }
        Be(index, 16 + icount * 20, dataEnd);
        File.WriteAllBytes(bhPath, index);
    }
}
