using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Fifa09Launcher;

/// <summary>What the server says about the newest launcher release.</summary>
public sealed class UpdateInfo
{
    [JsonPropertyName("version")] public string Version { get; set; } = "";
    [JsonPropertyName("url")] public string Url { get; set; } = "";
    [JsonPropertyName("sha256")] public string Sha256 { get; set; } = "";
    [JsonPropertyName("notes")] public Dictionary<string, string>? Notes { get; set; }

    public string NotesFor(string lang) =>
        Notes == null ? "" : Notes.TryGetValue(lang, out var n) ? n : Notes.TryGetValue("en", out var en) ? en : "";

    /// <summary>Installable in place only with an https zip and its checksum; otherwise just a download link.</summary>
    public bool CanInstall =>
        Url.StartsWith("https://", StringComparison.OrdinalIgnoreCase) &&
        Url.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) && Sha256.Length == 64;
}

/// <summary>
/// Launcher self-update. On start the launcher asks the server for the newest
/// version (/api/launcher/version, made from /opt/fifa09/launcher_version.json).
/// When it is newer, a notice is shown; "Update now" downloads the zip, checks
/// its SHA-256, unpacks it to a temp folder and lets a small PowerShell script
/// copy it over the launcher folder once the launcher has exited, then start it
/// again. The zip has the same layout as the player package (FIFA09 Launcher.exe,
/// bin\, mods\ ...). The game folder and launcher.json are never touched.
/// </summary>
public static class Updater
{
    const string VersionUrl = "https://nobkonline.com/api/launcher/version";

    static readonly HttpClient Http = CreateClient();

    static HttpClient CreateClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd("NOBK-Launcher/" + CurrentText);
        return c;
    }

    public static Version Current => Normalize(typeof(Updater).Assembly.GetName().Version ?? new Version(0, 0, 0));
    public static string CurrentText => Current.ToString(3);

    static Version Normalize(Version v) => new(v.Major, v.Minor, Math.Max(v.Build, 0));

    /// <summary>The newer release, or null (none, or the server could not be reached).</summary>
    public static async Task<UpdateInfo?> CheckAsync()
    {
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            var text = await Http.GetStringAsync(VersionUrl + "?v=" + CurrentText, cts.Token).ConfigureAwait(false);
            var info = JsonSerializer.Deserialize<UpdateInfo>(text);
            if (info == null || !System.Version.TryParse(info.Version, out var v)) return null;
            return Normalize(v) > Current ? info : null;
        }
        catch
        {
            return null; // offline or no release published: just no notice
        }
    }

    /// <summary>Downloads and unpacks the release. Returns the folder holding the new files.</summary>
    public static async Task<string> DownloadAsync(UpdateInfo info, IProgress<int> progress)
    {
        var work = Path.Combine(Path.GetTempPath(), "nobk-launcher-update");
        if (Directory.Exists(work)) Directory.Delete(work, true);
        Directory.CreateDirectory(work);
        var zip = Path.Combine(work, "update.zip");

        using (var resp = await Http.GetAsync(info.Url, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
        {
            resp.EnsureSuccessStatusCode();
            long total = resp.Content.Headers.ContentLength ?? 0;
            await using var src = await resp.Content.ReadAsStreamAsync().ConfigureAwait(false);
            await using var dst = File.Create(zip);
            var buf = new byte[1 << 16];
            long done = 0;
            int n, last = -1;
            while ((n = await src.ReadAsync(buf).ConfigureAwait(false)) > 0)
            {
                await dst.WriteAsync(buf.AsMemory(0, n)).ConfigureAwait(false);
                done += n;
                int pct = total > 0 ? (int)(done * 100 / total) : 0;
                if (pct != last) { progress.Report(pct); last = pct; }
            }
        }

        string hash;
        using (var f = File.OpenRead(zip)) hash = Convert.ToHexString(SHA256.HashData(f));
        if (!hash.Equals(info.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(L.T("The downloaded file is damaged (checksum mismatch). Try again later."));

        var files = Path.Combine(work, "files");
        ZipFile.ExtractToDirectory(zip, files);
        // A zip made from the folder itself has one top folder: use what is inside it.
        var dirs = Directory.GetDirectories(files);
        if (Directory.GetFiles(files).Length == 0 && dirs.Length == 1) files = dirs[0];
        if (!File.Exists(Path.Combine(files, Path.GetFileName(LauncherExe))))
            throw new InvalidDataException("update.zip: " + Path.GetFileName(LauncherExe) + " missing");
        return files;
    }

    static string LauncherExe => Environment.ProcessPath ?? Path.Combine(AppContext.BaseDirectory, "FIFA09 Launcher.exe");

    /// <summary>Starts the copy script; the caller must exit the launcher right after.</summary>
    public static void InstallAndRestart(string files)
    {
        var target = AppContext.BaseDirectory.TrimEnd('\\');
        var script = Path.Combine(Path.GetTempPath(), "nobk-launcher-update", "install.ps1");
        string Q(string s) => "'" + s.Replace("'", "''") + "'";
        var ps = new StringBuilder();
        ps.AppendLine("$ErrorActionPreference = 'Stop'");
        ps.AppendLine("$src = " + Q(files));
        ps.AppendLine("$dst = " + Q(target));
        ps.AppendLine("$exe = " + Q(LauncherExe));
        ps.AppendLine("$log = Join-Path $dst 'update.log'");
        ps.AppendLine("try { Wait-Process -Id " + Environment.ProcessId + " -Timeout 60 -ErrorAction SilentlyContinue } catch { }");
        ps.AppendLine("Start-Sleep -Milliseconds 500");
        ps.AppendLine("try {");
        ps.AppendLine("  foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File) {");
        ps.AppendLine("    $rel = $f.FullName.Substring($src.Length).TrimStart('\\')");
        // never overwrite the player's settings or game files
        ps.AppendLine("    if ($rel -ieq 'launcher.json' -or $rel -like 'Game\\*') { continue }");
        ps.AppendLine("    $to = Join-Path $dst $rel");
        ps.AppendLine("    New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null");
        ps.AppendLine("    for ($i = 0; ; $i++) {");
        ps.AppendLine("      try { Copy-Item -LiteralPath $f.FullName -Destination $to -Force; break }");
        ps.AppendLine("      catch { if ($i -ge 20) { throw }; Start-Sleep -Milliseconds 500 }");
        ps.AppendLine("    }");
        ps.AppendLine("  }");
        ps.AppendLine("  \"$(Get-Date -Format s) updated from $src\" | Out-File -Append $log");
        ps.AppendLine("} catch {");
        ps.AppendLine("  \"$(Get-Date -Format s) update failed: $_\" | Out-File -Append $log");
        ps.AppendLine("}");
        ps.AppendLine("Start-Process -FilePath $exe -WorkingDirectory $dst");
        File.WriteAllText(script, ps.ToString(), new UTF8Encoding(true));

        Process.Start(new ProcessStartInfo("powershell.exe")
        {
            ArgumentList = { "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", script },
            UseShellExecute = false,
            CreateNoWindow = true,
        });
    }
}
