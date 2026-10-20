using System.IO;
using System.Text.Json;

namespace Fifa09Launcher;

/// <summary>
/// Everything the launcher remembers between runs. Saved as launcher.json next
/// to the launcher. Turned into fifa09.exe command-line flags by BuildArgs().
/// Only flags verified against the ReXGlue SDK source are emitted.
/// </summary>
public sealed class LauncherSettings
{
    // Display
    public bool Fullscreen { get; set; } = true;
    public string WindowSize { get; set; } = "1920x1080"; // only used when not fullscreen
    public int Monitor { get; set; } = 0;                 // SDK --monitor: 0 default, 1 primary, 2 second...
    public bool DiagLog { get; set; } = false;            // NOBK_DIAG=1: detailed log for bug reports
    public int FpsCap { get; set; } = 60;                // 60/120/180/240, 0 = unlimited

    // Graphics
    public string GraphicsApi { get; set; } = "d3d12";  // d3d12, vulkan
    // Not in the UI. The game reads a small render target back on the CPU for its
    // exposure; "none" leaves some stadiums overexposed, "full" halves the frame rate.
    // "fast" uses the previous frame's copy: right exposure, almost no cost.
    // (New key name: old launcher.json files still say ReadbackResolve "full".)
    public string ReadbackMode { get; set; } = "fast";
    public int Anisotropic { get; set; } = 5;            // anisotropic_override: 5=16x 4=8x 3=4x 2=2x 0=off
    public int RenderScale { get; set; } = 0;            // old setting (0 = auto, 1..4), read once

    // Render quality: "720p", "1080p", "2k", "4k".
    // The game draws at 1280 x 720 (Xbox 360). 720p and 1080p draw at that size and AMD FSR
    // upscales the picture to the screen / window (fast); 2K and 4K draw natively at 2x / 3x
    // (sharpest, costs FPS). The window size is a separate choice (windowed mode only).
    public string Resolution { get; set; } = "";

    public static readonly string[] Resolutions = { "720p", "1080p", "2k", "4k" };

    // Height of the primary screen in physical pixels, set by the launcher.
    public static int ScreenHeight { get; set; } = 1080;

    public string EffectiveResolution =>
        Array.IndexOf(Resolutions, Resolution) >= 0 ? Resolution : RenderScale switch
        {
            2 => "2k",
            3 or 4 => "4k",
            _ => "1080p",   // auto and 1x: 720p drawn, FSR to the screen
        };

    public int EffectiveRenderScale => EffectiveResolution switch { "2k" => 2, "4k" => 3, _ => 1 };

    public bool UseFsr => EffectiveResolution is "720p" or "1080p";

    // Window size for windowed mode.
    public (int W, int H) WindowPixels
    {
        get
        {
            var parts = WindowSize.Split('x');
            return parts.Length == 2 && int.TryParse(parts[0], out var w) && int.TryParse(parts[1], out var h)
                   && w >= 640 && h >= 360 ? (w, h) : (1920, 1080);
        }
    }

    // Achievements
    public bool AchievementPopup { get; set; } = true;
    public bool AchievementSound { get; set; } = true;

    // Input
    public bool KeyboardEnabled { get; set; } = false;   // --mnk_mode
    public Dictionary<string, string> Keybinds { get; set; } = new(Keybind.Defaults);
    public string ButtonPrompts { get; set; } = "xbox";  // xbox, ps

    // Game
    public bool SkipIntro { get; set; } = true;

    // NOBK Online account. The token replaces the password (which is never saved);
    // signing out or changing the password on the website makes it invalid.
    public string AccountName { get; set; } = "";
    public string AccountToken { get; set; } = "";
    public string AccountCountry { get; set; } = "";
    public bool PlayOnline { get; set; } = true;
    // Off: the login only lasts until the launcher is closed (the token is not written to disk).
    public bool RememberMe { get; set; } = true;

    // Launcher language: a code from L.Languages ("" = follow Windows).
    public string Language { get; set; } = "";

    public void SetAccount(OnlineIdentity id)
    {
        AccountName = id.Gamertag;
        AccountCountry = id.Country;
        if (!string.IsNullOrEmpty(id.Token)) AccountToken = id.Token;
    }

    public static string ModsDir => Path.Combine(AppContext.BaseDirectory, "mods");

    /// <summary>Puts the selected mods into the game archives (fast when nothing changed).</summary>
    public void ApplyMods() =>
        ModManager.Sync(ResolveGameFolder(), ModsDir, ButtonPrompts == "ps", false); // intro always plays

    // Paths (relative to the launcher folder unless absolute)
    public string GameFolder { get; set; } = "Game";

    static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };

    public static string FilePath => Path.Combine(AppContext.BaseDirectory, "launcher.json");

    public static LauncherSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                var s = JsonSerializer.Deserialize<LauncherSettings>(File.ReadAllText(FilePath));
                if (s != null)
                {
                    // Fill in binds added in newer launcher versions.
                    foreach (var kv in Keybind.Defaults)
                        s.Keybinds.TryAdd(kv.Key, kv.Value);
                    return s;
                }
            }
        }
        catch
        {
            // Corrupt file: fall back to defaults rather than refusing to start.
        }
        return new LauncherSettings();
    }

    public void Save()
    {
        var token = AccountToken;
        if (!RememberMe) AccountToken = "";
        try { File.WriteAllText(FilePath, JsonSerializer.Serialize(this, JsonOpts)); }
        finally { AccountToken = token; }
    }

    public string ResolveGameFolder() =>
        Path.IsPathRooted(GameFolder) ? GameFolder : Path.Combine(AppContext.BaseDirectory, GameFolder);

    public List<string> BuildArgs(OnlineIdentity? online = null)
    {
        var a = new List<string>
        {
            // Always "--name=value": the SDK only applies the separated "--name value" form to
            // settings registered before the command line is parsed (readback_resolve was not).
            "--game_data_root=" + ResolveGameFolder(),
            "--gpu_plugin=xenos",
            "--graphics_api=" + GraphicsApi,
            "--readback_resolve=" + (ReadbackMode is "none" or "fast" or "some" or "full" ? ReadbackMode : "fast"),
            "--resolution_scale=" + EffectiveRenderScale,
            "--present_effect=" + (UseFsr ? "fsr" : "bilinear"),
            "--anisotropic_override=" + Anisotropic,
            "--achievement_popup=" + (AchievementPopup ? "true" : "false"),
            "--achievement_sound=" + (AchievementSound ? "true" : "false"),
            // Fixes the black screen in arena / set pieces (needs the SDK patch
            // patches/edram-ownership-no-wrap.patch).
            "--edram_ownership_no_wrap=true",
            Fullscreen ? "--fullscreen" : "--no-fullscreen",
            "--monitor=" + Math.Clamp(Monitor, 0, 16),
            "--log_level=info",
            "--log_file=run.log",
        };

        // The guest presents once per vblank, so the guest refresh rate is the
        // FPS cap. The SDK clamps it to 24..240.
        if (FpsCap <= 0)
            a.Add("--no-vsync");
        else
            a.AddRange(new[] { "--vsync", "--video_mode_refresh_rate=" + FpsCap });

        if (!Fullscreen)
        {
            var (w, h) = WindowPixels;
            a.AddRange(new[] { "--window_width=" + w, "--window_height=" + h });
        }

        if (online != null)
        {
            // Identity comes from the account: name = username, XUID fixed per account.
            a.Add("--online_enabled=true");
            a.Add("--online_server_ip=" + online.Server);
            if (!string.IsNullOrEmpty(online.Relay)) a.Add("--online_relay=" + online.Relay);
            a.Add("--user_gamertag=" + online.Gamertag);
            a.Add("--user_xuid=" + online.Xuid);
            a.Add("--net_packet_log=false");
            a.Add("--trace_imports=false");
        }

        if (KeyboardEnabled)
        {
            a.Add("--mnk_mode");
            a.Add("--no-mnk_mouse");
            foreach (var kv in Keybinds)
            {
                if (!string.IsNullOrWhiteSpace(kv.Value))
                    a.Add("--keybind_" + kv.Key + "=" + kv.Value);
            }
        }
        return a;
    }
}
