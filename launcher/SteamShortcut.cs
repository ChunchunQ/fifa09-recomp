using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace Fifa09Launcher;

/// <summary>
/// Adds the launcher to Steam as a non-Steam game (shortcuts.vdf) and copies
/// library artwork into Steam's grid folder. Steam must not be running while
/// shortcuts.vdf is written, or it overwrites the file on exit.
///
/// Artwork, all optional, read from the "steam" folder next to the launcher:
///   cover.png   portrait capsule, 600x900          -> {id}p.png
///   wide.png    horizontal capsule, 920x430        -> {id}.png   (falls back to banner.png)
///   banner.png  library hero, 3840x1240 / 1920x620 -> {id}_hero.png
///   logo.png    transparent logo shown over hero   -> {id}_logo.png
///   icon.png / icon.ico                            -> {id}_icon.* and the shortcut icon
/// </summary>
public static class SteamShortcut
{
    public const string AppName = "FIFA 09";

    public static string? FindSteamPath()
    {
        var p = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
        if (string.IsNullOrEmpty(p))
            p = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath", null) as string;
        return !string.IsNullOrEmpty(p) && Directory.Exists(p) ? Path.GetFullPath(p) : null;
    }

    public static bool IsSteamRunning() => Process.GetProcessesByName("steam").Length > 0;

    /// Asks Steam to quit and waits up to 20 s.
    public static bool CloseSteam(string steamPath)
    {
        try
        {
            Process.Start(new ProcessStartInfo(Path.Combine(steamPath, "steam.exe"), "-shutdown") { UseShellExecute = false });
        }
        catch { return false; }
        for (int i = 0; i < 40; i++)
        {
            Thread.Sleep(500);
            if (!IsSteamRunning()) { Thread.Sleep(1000); return true; }
        }
        return false;
    }

    public static void StartSteam(string steamPath)
    {
        try { Process.Start(new ProcessStartInfo(Path.Combine(steamPath, "steam.exe")) { UseShellExecute = true }); }
        catch { }
    }

    /// The account folders under userdata to update: the most recently
    /// logged-in account if it can be found, otherwise all of them.
    public static List<string> UserDirs(string steamPath)
    {
        var userdata = Path.Combine(steamPath, "userdata");
        if (!Directory.Exists(userdata)) return new();
        var all = Directory.GetDirectories(userdata)
            .Where(d => ulong.TryParse(Path.GetFileName(d), out var id) && id != 0)
            .ToList();
        try
        {
            var text = File.ReadAllText(Path.Combine(steamPath, "config", "loginusers.vdf"));
            foreach (Match m in Regex.Matches(text, "\"(\\d{17})\"\\s*\\{([^}]*)\\}"))
            {
                if (!Regex.IsMatch(m.Groups[2].Value, "\"MostRecent\"\\s*\"1\"")) continue;
                ulong account = ulong.Parse(m.Groups[1].Value) - 76561197960265728UL;
                var dir = Path.Combine(userdata, account.ToString());
                if (Directory.Exists(dir)) return new() { dir };
            }
        }
        catch { }
        return all;
    }

    /// Steam's id for a non-Steam shortcut: CRC32 of exe + name with the top bit set.
    public static uint ShortcutId(string quotedExe, string name) =>
        Crc32(Encoding.UTF8.GetBytes(quotedExe + name)) | 0x80000000u;

    /// Adds or updates the shortcut for every target account. Returns the
    /// number of accounts updated.
    public static int AddOrUpdate(string steamPath, string launcherExe, string artDir)
    {
        string exe = "\"" + launcherExe + "\"";
        string startDir = "\"" + Path.GetDirectoryName(launcherExe) + "\\\"";
        uint id = ShortcutId(exe, AppName);

        string icon = "";
        foreach (var n in new[] { "icon.ico", "icon.png" })
            if (File.Exists(Path.Combine(artDir, n))) { icon = Path.Combine(artDir, n); break; }
        if (icon == "") icon = launcherExe;

        int updated = 0;
        foreach (var user in UserDirs(steamPath))
        {
            var config = Path.Combine(user, "config");
            Directory.CreateDirectory(config);
            var vdfPath = Path.Combine(config, "shortcuts.vdf");

            var root = File.Exists(vdfPath) ? Vdf.Read(File.ReadAllBytes(vdfPath)) : new Vdf.Map();
            var list = root.GetMap("shortcuts") ?? new Vdf.Map();
            root["shortcuts"] = list;

            // Drop our previous entry (same name or same exe), keep the rest in order.
            var kept = list.Values.OfType<Vdf.Map>()
                .Where(e => !(string.Equals(e.GetString("AppName") ?? e.GetString("appname"), AppName, StringComparison.OrdinalIgnoreCase)
                              || string.Equals(e.GetString("Exe") ?? e.GetString("exe"), exe, StringComparison.OrdinalIgnoreCase)))
                .ToList();

            var entry = new Vdf.Map
            {
                ["appid"] = unchecked((int)id),
                ["AppName"] = AppName,
                ["Exe"] = exe,
                ["StartDir"] = startDir,
                ["icon"] = icon,
                ["ShortcutPath"] = "",
                ["LaunchOptions"] = "--play",
                ["IsHidden"] = 0,
                ["AllowDesktopConfig"] = 1,
                ["AllowOverlay"] = 1,
                ["OpenVR"] = 0,
                ["Devkit"] = 0,
                ["DevkitGameID"] = "",
                ["DevkitOverrideAppID"] = 0,
                ["LastPlayTime"] = 0,
                ["FlatpakAppID"] = "",
                ["tags"] = new Vdf.Map(),
            };
            kept.Add(entry);

            var rebuilt = new Vdf.Map();
            for (int i = 0; i < kept.Count; i++) rebuilt[i.ToString()] = kept[i];
            root["shortcuts"] = rebuilt;

            if (File.Exists(vdfPath)) File.Copy(vdfPath, vdfPath + ".bak", true);
            File.WriteAllBytes(vdfPath, Vdf.Write(root));

            CopyArt(Path.Combine(config, "grid"), artDir, id);
            updated++;
        }
        return updated;
    }

    static void CopyArt(string grid, string artDir, uint id)
    {
        if (!Directory.Exists(artDir)) return;
        Directory.CreateDirectory(grid);
        void Copy(string src, string dst)
        {
            var s = Path.Combine(artDir, src);
            if (File.Exists(s)) File.Copy(s, Path.Combine(grid, dst), true);
        }
        Copy("cover.png", $"{id}p.png");
        if (File.Exists(Path.Combine(artDir, "wide.png"))) Copy("wide.png", $"{id}.png");
        else Copy("banner.png", $"{id}.png");
        Copy("banner.png", $"{id}_hero.png");
        Copy("logo.png", $"{id}_logo.png");
        Copy("icon.png", $"{id}_icon.png");
        Copy("icon.ico", $"{id}_icon.ico");
    }

    static uint Crc32(byte[] data)
    {
        uint crc = 0xFFFFFFFF;
        foreach (byte b in data)
        {
            crc ^= b;
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        return ~crc;
    }

    /// Minimal binary VDF (the format of shortcuts.vdf).
    public static class Vdf
    {
        const byte TMap = 0x00, TString = 0x01, TInt = 0x02, TFloat = 0x03, TPtr = 0x04,
                   TWString = 0x05, TColor = 0x06, TUInt64 = 0x07, TEnd = 0x08;

        public sealed class Map : Dictionary<string, object>
        {
            public Map() : base(StringComparer.Ordinal) { }
            public Map? GetMap(string k) => TryGetValue(k, out var v) ? v as Map : null;
            public string? GetString(string k) => TryGetValue(k, out var v) ? v as string : null;
        }

        // Values we do not understand are kept as raw bytes and written back unchanged.
        sealed record Raw(byte Type, byte[] Data);

        public static Map Read(byte[] bytes)
        {
            int pos = 0;
            return ReadMap(bytes, ref pos);
        }

        static Map ReadMap(byte[] b, ref int pos)
        {
            var map = new Map();
            while (pos < b.Length)
            {
                byte type = b[pos++];
                if (type == TEnd) break;
                string key = ReadCString(b, ref pos);
                switch (type)
                {
                    case TMap: map[key] = ReadMap(b, ref pos); break;
                    case TString: map[key] = ReadCString(b, ref pos); break;
                    case TInt: map[key] = BitConverter.ToInt32(b, pos); pos += 4; break;
                    case TFloat: case TPtr: case TColor:
                        map[key] = new Raw(type, b[pos..(pos + 4)]); pos += 4; break;
                    case TUInt64: map[key] = new Raw(type, b[pos..(pos + 8)]); pos += 8; break;
                    case TWString:
                    {
                        int start = pos;
                        while (pos + 1 < b.Length && (b[pos] != 0 || b[pos + 1] != 0)) pos += 2;
                        pos += 2;
                        map[key] = new Raw(type, b[start..pos]);
                        break;
                    }
                    default: throw new InvalidDataException($"Unknown VDF type {type:X2}");
                }
            }
            return map;
        }

        static string ReadCString(byte[] b, ref int pos)
        {
            int start = pos;
            while (pos < b.Length && b[pos] != 0) pos++;
            var s = Encoding.UTF8.GetString(b, start, pos - start);
            pos++;
            return s;
        }

        public static byte[] Write(Map root)
        {
            using var ms = new MemoryStream();
            WriteMap(ms, root);
            ms.WriteByte(TEnd);
            return ms.ToArray();
        }

        static void WriteMap(Stream s, Map map)
        {
            foreach (var (key, value) in map)
            {
                switch (value)
                {
                    case Map m: s.WriteByte(TMap); CString(s, key); WriteMap(s, m); s.WriteByte(TEnd); break;
                    case string str: s.WriteByte(TString); CString(s, key); CString(s, str); break;
                    case int i: s.WriteByte(TInt); CString(s, key); s.Write(BitConverter.GetBytes(i)); break;
                    case Raw r: s.WriteByte(r.Type); CString(s, key); s.Write(r.Data); break;
                }
            }
        }

        static void CString(Stream s, string str)
        {
            s.Write(Encoding.UTF8.GetBytes(str));
            s.WriteByte(0);
        }
    }
}
