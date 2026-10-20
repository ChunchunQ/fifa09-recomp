using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Fifa09Launcher;

public partial class MainWindow : Window
{
    // SHA-256 of the default.xex the port was recompiled from. A different
    // dump (other region, title update) has different addresses and will crash.
    const string ExpectedXexSha256 = "A6BD02A6A8B15DA801CC05B33BAFA24CD7B1140938BB43FCAEBB30521F37F86B";

    readonly LauncherSettings _s = LauncherSettings.Load();
    readonly Dictionary<string, Button> _keyButtons = new();
    string? _capturing;

    internal static string BinDir => Path.Combine(AppContext.BaseDirectory, "bin");
    internal static string GameExe => Path.Combine(BinDir, "fifa09.exe");

    public MainWindow()
    {
        L.Lang = string.IsNullOrEmpty(_s.Language) ? L.DefaultLang : _s.Language;
        InitializeComponent();
        L.Apply(this);
        BuildLanguageFlags();
        LoadBackground();
        LoadLogo();
        LauncherSettings.ScreenHeight = PhysicalScreenHeight();
        FillCombos();
        BuildKeybindList();
        ApplySettingsToUi();
        VersionText.Text = "Launcher v" + Updater.CurrentText;
        UpdateLater.ToolTip = L.T("Later");
        Loaded += async (_, _) => await CheckForUpdate();
        PreviewKeyDown += Window_PreviewKeyDown;
        Closing += (_, _) => SaveFromUi();
        RefreshStatus();
    }

    // ---------------------------------------------------------------- setup

    // Launcher art is built into the exe (csproj: Assets/art, from the local, not committed
    // Launcher\background.png / logo.png). Files next to the exe still work as a fallback.
    static BitmapImage? Art(string name)
    {
        try
        {
            var res = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/art/" + name));
            if (res != null)
            {
                var bmp = new BitmapImage();
                bmp.BeginInit();
                bmp.CacheOption = BitmapCacheOption.OnLoad;
                bmp.StreamSource = res.Stream;
                bmp.EndInit();
                bmp.Freeze();
                return bmp;
            }
        }
        catch { /* not built in */ }
        var p = Path.Combine(AppContext.BaseDirectory, name);
        if (!File.Exists(p)) return null;
        try
        {
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.CacheOption = BitmapCacheOption.OnLoad;
            bmp.UriSource = new Uri(p);
            bmp.EndInit();
            bmp.Freeze();
            return bmp;
        }
        catch { return null; }
    }

    void LoadBackground()
    {
        var bmp = Art("background.jpg") ?? Art("background.png");
        if (bmp != null) BackgroundArt.Source = bmp;
    }

    // The original logo has black "FIFA" lettering made for light backgrounds.
    // On the dark launcher it would vanish, so near-black neutral pixels are
    // recolored to the text color (alpha kept, so edges stay smooth).
    void LoadLogo()
    {
        var src = Art("logo.png");
        if (src == null) return;
        try
        {

            var bgra = new FormatConvertedBitmap(src, PixelFormats.Bgra32, null, 0);
            int w = bgra.PixelWidth, h = bgra.PixelHeight, stride = w * 4;
            var px = new byte[stride * h];
            bgra.CopyPixels(px, stride, 0);

            for (int i = 0; i < px.Length; i += 4)
            {
                byte b = px[i], g = px[i + 1], r = px[i + 2], a = px[i + 3];
                if (a == 0) continue;
                int max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b));
                if (max < 90 && max - min < 30)
                {
                    px[i] = 0xE6; px[i + 1] = 0xEF; px[i + 2] = 0xF5; // #F5EFE6
                }
            }

            var bmp = BitmapSource.Create(w, h, src.DpiX, src.DpiY, PixelFormats.Bgra32, null, px, stride);
            bmp.Freeze();
            LogoArt.Source = bmp;
            LogoArt.Visibility = Visibility.Visible;
            LogoText.Visibility = Visibility.Collapsed;
        }
        catch { /* bad image: keep the text logo */ }
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd, IntPtr hdc);
    [System.Runtime.InteropServices.DllImport("gdi32.dll")] static extern int GetDeviceCaps(IntPtr hdc, int index);

    // Primary screen height in physical pixels, independent of Windows scaling.
    // Monitors, primary first, in physical pixels: the SDK's --monitor counts 1 = primary,
    // 2 = second ... (Windows enumeration order after the primary one).
    delegate bool MonitorEnumProc(IntPtr hMon, IntPtr hdc, ref MonRect rc, IntPtr data);
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct MonRect { public int L, T, R, B; }
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    struct MonInfo { public int cbSize; public MonRect Monitor; public MonRect Work; public uint Flags; }
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr clip, MonitorEnumProc proc, IntPtr data);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool GetMonitorInfo(IntPtr hMon, ref MonInfo info);

    static List<(int W, int H)> Monitors()
    {
        var primary = new List<(int, int)>();
        var others = new List<(int, int)>();
        try
        {
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr h, IntPtr dc, ref MonRect rc, IntPtr d) =>
            {
                var mi = new MonInfo { cbSize = System.Runtime.InteropServices.Marshal.SizeOf<MonInfo>() };
                if (GetMonitorInfo(h, ref mi))
                {
                    var size = (mi.Monitor.R - mi.Monitor.L, mi.Monitor.B - mi.Monitor.T);
                    if ((mi.Flags & 1) != 0) primary.Add(size); else others.Add(size);
                }
                return true;
            }, IntPtr.Zero);
        }
        catch { /* no list: the box shows "Primary screen" */ }
        primary.AddRange(others);
        return primary;
    }

    static int PhysicalScreenHeight()
    {
        const int DESKTOPVERTRES = 117;
        IntPtr dc = GetDC(IntPtr.Zero);
        try
        {
            int h = GetDeviceCaps(dc, DESKTOPVERTRES);
            return h > 0 ? h : (int)SystemParameters.PrimaryScreenHeight;
        }
        finally { ReleaseDC(IntPtr.Zero, dc); }
    }

    static void Fill(ComboBox box, params (string Label, object Value)[] items)
    {
        box.Items.Clear();
        foreach (var (label, value) in items)
            box.Items.Add(new ComboBoxItem { Content = L.T(label), Tag = value });
    }

    static void Select(ComboBox box, object value)
    {
        foreach (ComboBoxItem it in box.Items)
            if (Equals(it.Tag, value)) { box.SelectedItem = it; return; }
        if (box.Items.Count > 0) box.SelectedIndex = 0;
    }

    static T Selected<T>(ComboBox box, T fallback) =>
        box.SelectedItem is ComboBoxItem { Tag: T v } ? v : fallback;

    // Row of big selectable tiles (resolution, FPS): a big label and a small one under it.
    void FillTiles(UniformGrid grid, string group, params (string Big, string Small, object Value)[] items)
    {
        grid.Children.Clear();
        foreach (var (big, small, value) in items)
        {
            var sp = new StackPanel();
            sp.Children.Add(new TextBlock
            {
                Text = L.T(big), FontFamily = new FontFamily("Bahnschrift"), FontSize = 19, FontWeight = FontWeights.Bold,
                HorizontalAlignment = HorizontalAlignment.Center,
            });
            if (!string.IsNullOrEmpty(small))
                sp.Children.Add(new TextBlock
                {
                    Text = L.T(small), FontSize = 10.5, Foreground = (Brush)FindResource("Muted"),
                    HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 1, 0, 0),
                });
            grid.Children.Add(new RadioButton { Content = sp, Tag = value, GroupName = group, Style = (Style)FindResource("Tile") });
        }
    }

    static void SelectTile(UniformGrid grid, object value)
    {
        var tiles = grid.Children.OfType<RadioButton>().ToList();
        var hit = tiles.FirstOrDefault(r => Equals(r.Tag, value)) ?? tiles.FirstOrDefault();
        if (hit != null) hit.IsChecked = true;
    }

    static T SelectedTile<T>(UniformGrid grid, T fallback) =>
        grid.Children.OfType<RadioButton>().FirstOrDefault(r => r.IsChecked == true)?.Tag is T v ? v : fallback;

    void FillCombos()
    {
        Fill(WindowSizeBox, ("1280 × 720", "1280x720"), ("1600 × 900", "1600x900"), ("1920 × 1080", "1920x1080"),
             ("2560 × 1440", "2560x1440"));
        FillTiles(FpsTiles, "fps", ("60", "FPS", 60), ("120", "FPS", 120), ("180", "FPS", 180), ("240", "FPS", 240),
                  ("∞", "Unlimited", 0));
        Fill(ApiBox, ("Direct3D 12 (recommended)", "d3d12"), ("Vulkan (experimental)", "vulkan"));
        var mons = Monitors();
        var monItems = new List<(string, object)>();
        for (int i = 0; i < mons.Count; i++)
            monItems.Add((i == 0 ? L.F("Primary screen ({0} × {1})", mons[i].W, mons[i].H)
                                 : L.F("Screen {0} ({1} × {2})", i + 1, mons[i].W, mons[i].H), i + 1));
        if (monItems.Count == 0) monItems.Add((L.T("Primary screen"), 1));
        Fill(MonitorBox, monItems.ToArray());
        FillTiles(ResTiles, "res", ("720p", "FSR", "720p"), ("1080p", "FSR · recommended", "1080p"),
                  ("2K", "native", "2k"), ("4K", "native", "4k"));
        Fill(PromptsBox, ("Xbox 360", "xbox"), ("PlayStation", "ps"));
        Fill(AnisoBox, ("16x (recommended)", 5), ("8x", 4), ("4x", 3), ("2x", 2), ("Off", 0));
    }

    void ApplySettingsToUi()
    {
        Select(WindowSizeBox, _s.WindowSize);
        SelectTile(FpsTiles, _s.FpsCap);
        Select(ApiBox, _s.GraphicsApi);
        SelectTile(ResTiles, _s.EffectiveResolution);
        Select(AnisoBox, _s.Anisotropic);
        FullscreenBox.IsChecked = _s.Fullscreen;
        Select(MonitorBox, _s.Monitor >= 1 && _s.Monitor <= MonitorBox.Items.Count ? _s.Monitor : 1);
        DiagBox.IsChecked = _s.DiagLog;
        WindowSizePanel.Visibility = _s.Fullscreen ? Visibility.Collapsed : Visibility.Visible;
        KeyboardBox.IsChecked = _s.KeyboardEnabled;
        Select(PromptsBox, _s.ButtonPrompts);
        AchPopupBox.IsChecked = _s.AchievementPopup;
        AchSoundBox.IsChecked = _s.AchievementSound;
        GameFolderBox.Text = _s.ResolveGameFolder();
        OnlineBox.IsChecked = _s.PlayOnline;
        ShowAccount();
        foreach (var (id, btn) in _keyButtons)
            btn.Content = _s.Keybinds.TryGetValue(id, out var k) ? k : "";
    }

    void SaveFromUi()
    {
        _s.WindowSize = Selected(WindowSizeBox, _s.WindowSize);
        _s.FpsCap = SelectedTile(FpsTiles, _s.FpsCap);
        _s.GraphicsApi = Selected(ApiBox, _s.GraphicsApi);
        _s.Resolution = SelectedTile(ResTiles, _s.EffectiveResolution);
        _s.Anisotropic = Selected(AnisoBox, _s.Anisotropic);
        _s.Fullscreen = FullscreenBox.IsChecked == true;
        _s.Monitor = Selected(MonitorBox, _s.Monitor);
        _s.DiagLog = DiagBox.IsChecked == true;
        _s.KeyboardEnabled = KeyboardBox.IsChecked == true;
        _s.ButtonPrompts = Selected(PromptsBox, _s.ButtonPrompts);
        _s.SkipIntro = false; // the intro option was removed: the intro always plays
        _s.PlayOnline = OnlineBox.IsChecked == true;
        _s.AchievementPopup = AchPopupBox.IsChecked == true;
        _s.AchievementSound = AchSoundBox.IsChecked == true;
        try { _s.Save(); } catch { /* read-only folder: settings just won't persist */ }
    }

    // ------------------------------------------------------------- keybinds

    void BuildKeybindList()
    {
        KeybindList.Children.Clear();
        _keyButtons.Clear();
        foreach (var (id, label) in Keybind.All)
        {
            var row = new Grid { Margin = new Thickness(0, 3, 8, 3) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var text = new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center, FontSize = 13 };
            var btn = new Button { Style = (Style)FindResource("KeyButton"), Tag = id };
            btn.Click += KeyButton_Click;
            Grid.SetColumn(btn, 1);

            row.Children.Add(text);
            row.Children.Add(btn);
            KeybindList.Children.Add(row);
            _keyButtons[id] = btn;
        }
    }

    void KeyButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string id } btn) return;
        _capturing = id;
        btn.Content = L.T("press a key…");
        KeyHint.Text = L.T("Press a key. Esc cancels.");
    }

    void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (_capturing == null) return;
        e.Handled = true;
        var id = _capturing;
        var key = e.Key == Key.System ? e.SystemKey : e.Key;

        if (key == Key.Escape)
        {
            _capturing = null;
            _keyButtons[id].Content = _s.Keybinds.GetValueOrDefault(id, "");
            KeyHint.Text = "";
            return;
        }

        var name = Keybind.ToSdkName(key);
        if (name == null)
        {
            KeyHint.Text = L.F("{0} can't be bound. Try another key.", key);
            return;
        }

        _s.Keybinds[id] = name;
        _keyButtons[id].Content = name;
        _capturing = null;
        KeyHint.Text = "";
    }

    void ResetKeys_Click(object sender, RoutedEventArgs e)
    {
        _s.Keybinds = new Dictionary<string, string>(Keybind.Defaults);
        _capturing = null;
        ApplySettingsToUi();
    }

    // --------------------------------------------------------- achievements

    sealed class AchFile
    {
        public List<AchItem> achievements { get; set; } = new();
    }

    sealed class AchItem
    {
        public uint id { get; set; }
        public string name { get; set; } = "";
        public string description { get; set; } = "";
        public string locked_description { get; set; } = "";
        public int gamerscore { get; set; }
        public string icon { get; set; } = "";
        public bool unlocked { get; set; }
        public long unlocked_at { get; set; }
    }

    // Written by the game (src/achievement_export.h) to bin\achievements.
    static string AchDir => Path.Combine(BinDir, "achievements");

    void LoadAchievements()
    {
        AchList.Children.Clear();
        AchFile? data = null;
        try
        {
            var path = Path.Combine(AchDir, "achievements.json");
            if (File.Exists(path))
                data = System.Text.Json.JsonSerializer.Deserialize<AchFile>(File.ReadAllText(path));
        }
        catch { /* unreadable: treat as missing */ }

        if (data == null || data.achievements.Count == 0)
        {
            AchCount.Text = L.T("No achievement data yet");
            AchScore.Text = L.T("Start the game once and the list will appear here.");
            AchPercent.Text = "";
            AchBar.Width = 0;
            return;
        }

        var list = data.achievements;
        int unlocked = list.Count(a => a.unlocked);
        int score = list.Where(a => a.unlocked).Sum(a => a.gamerscore);
        int total = list.Sum(a => a.gamerscore);
        double pct = list.Count == 0 ? 0 : 100.0 * unlocked / list.Count;

        AchCount.Text = L.F("{0} / {1} unlocked", unlocked, list.Count);
        AchScore.Text = $"{score} / {total} G";
        AchPercent.Text = $"{pct:0}%";
        AchBar.Width = 0;
        Dispatcher.BeginInvoke(() =>
        {
            if (AchBar.Parent is FrameworkElement track)
                AchBar.Width = track.ActualWidth * pct / 100.0;
        }, System.Windows.Threading.DispatcherPriority.Loaded);

        // Unlocked first (newest first), then locked in game order.
        var ordered = list.Where(a => a.unlocked).OrderByDescending(a => a.unlocked_at)
                          .Concat(list.Where(a => !a.unlocked));
        foreach (var a in ordered)
            AchList.Children.Add(BuildAchRow(a));
    }

    UIElement BuildAchRow(AchItem a)
    {
        var row = new Grid { Margin = new Thickness(0, 4, 8, 4) };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(56) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var card = new Border
        {
            Background = (Brush)FindResource(a.unlocked ? "FieldHover" : "Field"),
            CornerRadius = new CornerRadius(4),
            Padding = new Thickness(8),
            Child = row,
            Margin = new Thickness(0, 0, 0, 6),
        };

        // Icon (greyed out while locked).
        var img = new Image { Width = 44, Height = 44, Opacity = a.unlocked ? 1.0 : 0.35 };
        RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality);
        var iconPath = string.IsNullOrEmpty(a.icon) ? "" : Path.Combine(AchDir, a.icon);
        if (File.Exists(iconPath))
        {
            try
            {
                var bmp = new BitmapImage();
                bmp.BeginInit();
                bmp.CacheOption = BitmapCacheOption.OnLoad;
                bmp.UriSource = new Uri(iconPath);
                bmp.EndInit();
                img.Source = a.unlocked ? bmp : new FormatConvertedBitmap(bmp, PixelFormats.Gray8, null, 0);
            }
            catch { }
        }
        row.Children.Add(img);

        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 8, 0) };
        text.Children.Add(new TextBlock
        {
            Text = a.name,
            FontSize = 14,
            FontWeight = FontWeights.SemiBold,
            Foreground = (Brush)FindResource(a.unlocked ? "Text" : "Muted"),
            TextTrimming = TextTrimming.CharacterEllipsis,
        });
        string desc = a.unlocked || string.IsNullOrWhiteSpace(a.locked_description) ? a.description : a.locked_description;
        text.Children.Add(new TextBlock
        {
            Text = string.IsNullOrWhiteSpace(desc) ? L.T("Secret achievement") : desc,
            FontSize = 11.5,
            Foreground = (Brush)FindResource("Muted"),
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 2, 0, 0),
        });
        Grid.SetColumn(text, 1);
        row.Children.Add(text);

        var right = new StackPanel { VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Right };
        right.Children.Add(new TextBlock
        {
            Text = $"{a.gamerscore}G",
            FontSize = 14,
            FontWeight = FontWeights.Bold,
            HorizontalAlignment = HorizontalAlignment.Right,
            Foreground = (Brush)FindResource(a.unlocked ? "Accent" : "Muted"),
        });
        if (a.unlocked && a.unlocked_at > 0)
        {
            var when = DateTimeOffset.FromUnixTimeMilliseconds(a.unlocked_at).LocalDateTime;
            right.Children.Add(new TextBlock
            {
                Text = when.ToString("dd.MM.yyyy"),
                FontSize = 11,
                HorizontalAlignment = HorizontalAlignment.Right,
                Foreground = (Brush)FindResource("Muted"),
                Margin = new Thickness(0, 2, 0, 0),
            });
            right.Children.Add(new TextBlock
            {
                Text = when.ToString("HH:mm"),
                FontSize = 11,
                HorizontalAlignment = HorizontalAlignment.Right,
                Foreground = (Brush)FindResource("Muted"),
            });
        }
        else
        {
            right.Children.Add(new TextBlock
            {
                Text = L.T("Locked"),
                FontSize = 11,
                HorizontalAlignment = HorizontalAlignment.Right,
                Foreground = (Brush)FindResource("Muted"),
                Margin = new Thickness(0, 2, 0, 0),
            });
        }
        Grid.SetColumn(right, 2);
        row.Children.Add(right);
        return card;
    }

    // ---------------------------------------------------------------- steam

    async void AddToSteam_Click(object sender, RoutedEventArgs e)
    {
        var steam = SteamShortcut.FindSteamPath();
        if (steam == null)
        {
            SetSteamStatus("Steam is not installed (or was never started on this PC).", "Bad");
            return;
        }
        bool wasRunning = SteamShortcut.IsSteamRunning();
        if (wasRunning)
        {
            var answer = MessageBox.Show(this,
                L.T("Steam has to be closed to add the game. Close Steam now? It will be restarted afterwards."),
                L.T("Add to Steam"), MessageBoxButton.OKCancel, MessageBoxImage.Question);
            if (answer != MessageBoxResult.OK) return;
            SetSteamStatus("Closing Steam…", "Muted");
            if (!await Task.Run(() => SteamShortcut.CloseSteam(steam)))
            {
                SetSteamStatus("Steam did not close. Exit it from the tray icon and try again.", "Bad");
                return;
            }
        }
        try
        {
            string exe = Environment.ProcessPath ?? Path.Combine(AppContext.BaseDirectory, "FIFA09 Launcher.exe");
            string art = Path.Combine(AppContext.BaseDirectory, "steam");
            int n = await Task.Run(() => SteamShortcut.AddOrUpdate(steam, exe, art));
            if (n == 0)
            {
                SetSteamStatus("No Steam account found. Log in to Steam once, then try again.", "Bad");
                return;
            }
            SetSteamStatus("✓ Added to Steam. Hold Shift while starting from Steam to open this launcher.", "Good");
        }
        catch (Exception ex)
        {
            SteamStatus.Text = L.T("Could not add to Steam: ") + ex.Message;
            SteamStatus.Foreground = (Brush)FindResource("Bad");
        }
        finally
        {
            if (wasRunning) SteamShortcut.StartSteam(steam);
        }
    }

    void SetSteamStatus(string text, string brushKey)
    {
        SteamStatus.Text = L.T(text);
        SteamStatus.Foreground = (Brush)FindResource(brushKey);
    }

    // ---------------------------------------------------------- send a log

    async void SendLog_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_s.AccountToken))
        {
            SetLogStatus("Sign in on the Online tab first.", "Warn");
            return;
        }
        var path = Path.Combine(BinDir, "run.log");
        if (!File.Exists(path))
        {
            SetLogStatus("No log yet: start the game once.", "Warn");
            return;
        }
        LogButton.IsEnabled = false;
        SetLogStatus("Sending…", "Muted");
        try
        {
            var gz = await Task.Run(() =>
            {
                // the game may still have the file open: share it; keep only the last 48 MB
                using var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                const long Max = 48L * 1024 * 1024;
                if (f.Length > Max) f.Position = f.Length - Max;
                using var ms = new MemoryStream();
                using (var z = new System.IO.Compression.GZipStream(ms, System.IO.Compression.CompressionLevel.Optimal, true))
                    f.CopyTo(z);
                return ms.ToArray();
            });
            await OnlineAccount.SendLog(_s.AccountToken, LogNote.Text.Trim(), gz);
            LogNote.Clear();
            SetLogStatus("✓ Sent. Thank you!", "Good");
        }
        catch (OnlineLoginException ex) { SetLogStatus(ex.Message, "Bad", false); }
        catch (Exception ex) { SetLogStatus(L.T("Could not send the log: ") + ex.Message, "Bad", false); }
        finally { LogButton.IsEnabled = true; }
    }

    void SetLogStatus(string text, string brushKey, bool translate = true)
    {
        LogStatus.Text = translate ? L.T(text) : text;
        LogStatus.Foreground = (Brush)FindResource(brushKey);
    }

    // ----------------------------------------------------------- game files

    void Browse_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.OpenFolderDialog
        {
            Title = "Select your FIFA 09 game folder (the one with default.xex)",
            InitialDirectory = Directory.Exists(_s.ResolveGameFolder()) ? _s.ResolveGameFolder() : AppContext.BaseDirectory,
        };
        if (dlg.ShowDialog(this) == true)
        {
            _s.GameFolder = dlg.FolderName;
            GameFolderBox.Text = dlg.FolderName;
            VerifyResult.Text = "";
            RefreshStatus();
        }
    }

    async void Verify_Click(object sender, RoutedEventArgs e)
    {
        var xex = Path.Combine(_s.ResolveGameFolder(), "default.xex");
        if (!File.Exists(xex))
        {
            SetVerify("default.xex not found in this folder.", "Bad");
            return;
        }
        SetVerify("Checking…", "Muted");
        string hash = await Task.Run(() =>
        {
            using var f = File.OpenRead(xex);
            return Convert.ToHexString(SHA256.HashData(f));
        });
        if (hash.Equals(ExpectedXexSha256, StringComparison.OrdinalIgnoreCase))
            SetVerify("✓ default.xex matches the supported version.", "Good");
        else
            SetVerify("✗ default.xex is a different version (other region or title update). " +
                      "This build will likely crash with it.", "Bad"); // translated in SetVerify
        RefreshStatus();
    }

    void SetVerify(string text, string brushKey)
    {
        VerifyResult.Text = L.T(text);
        VerifyResult.Foreground = (Brush)FindResource(brushKey);
    }

    bool RefreshStatus()
    {
        string? problem = null;
        if (!File.Exists(GameExe))
            problem = "bin\\fifa09.exe is missing. Reinstall the port.";
        else if (!File.Exists(Path.Combine(_s.ResolveGameFolder(), "default.xex")))
            problem = "Game files not found. Set your FIFA 09 folder under Game files.";

        if (problem == null)
        {
            StatusText.Text = L.T("Ready");
            StatusText.Foreground = (Brush)FindResource("Good");
            PlayButton.IsEnabled = true;
            return true;
        }
        StatusText.Text = L.T(problem);
        StatusText.Foreground = (Brush)FindResource("Warn");
        PlayButton.IsEnabled = false;
        return false;
    }

    // --------------------------------------------------------------- online

    void ShowAccount()
    {
        bool signedIn = !string.IsNullOrEmpty(_s.AccountToken);
        SignedOutPanel.Visibility = signedIn ? Visibility.Collapsed : Visibility.Visible;
        SignedInPanel.Visibility = signedIn ? Visibility.Visible : Visibility.Collapsed;
        if (signedIn)
        {
            SignedInName.Text = _s.AccountName;
            SignedInInfo.Text = string.IsNullOrEmpty(_s.AccountCountry) ? "" : L.F("Country: {0}", _s.AccountCountry);
        }
        else
        {
            if (string.IsNullOrEmpty(UserBox.Text)) UserBox.Text = _s.AccountName;
            RememberBox.IsChecked = _s.RememberMe;
        }
    }

    void SetOnlineStatus(string text, string brushKey)
    {
        OnlineStatus.Text = L.T(text);
        OnlineStatus.Foreground = (Brush)FindResource(brushKey);
    }

    void PassBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) SignIn_Click(sender, e);
    }

    async void SignIn_Click(object sender, RoutedEventArgs e)
    {
        var user = UserBox.Text.Trim();
        if (user.Length == 0 || PassBox.Password.Length == 0)
        {
            SetOnlineStatus("Enter your username and password.", "Warn");
            return;
        }
        SignInButton.IsEnabled = false;
        SetOnlineStatus("Signing in…", "Muted");
        try
        {
            var id = await OnlineAccount.Login(user, PassBox.Password);
            PassBox.Clear();
            _s.SetAccount(id);
            _s.RememberMe = RememberBox.IsChecked == true;
            _s.PlayOnline = true;
            OnlineBox.IsChecked = true;
            SaveFromUi();
            ShowAccount();
            SetOnlineStatus("✓ Signed in. Press PLAY and open EA Sports Online in the game.", "Good");
        }
        catch (OnlineLoginException ex)
        {
            SetOnlineStatus(ex.Message, "Bad");
        }
        finally
        {
            SignInButton.IsEnabled = true;
        }
    }

    void SignOut_Click(object sender, RoutedEventArgs e)
    {
        _s.AccountToken = "";
        SaveFromUi();
        ShowAccount();
        SetOnlineStatus("Signed out. The game will start offline.", "Muted");
    }

    void Register_Click(object sender, RoutedEventArgs e) => OpenUrl(OnlineAccount.RegisterUrl);
    void Forgot_Click(object sender, RoutedEventArgs e) => OpenUrl(OnlineAccount.SiteUrl + "sifremi-unuttum/");
    void MyAccount_Click(object sender, RoutedEventArgs e) => OpenUrl(OnlineAccount.SiteUrl + "hesabim/");

    static void OpenUrl(string url)
    {
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch { }
    }

    /// <summary>
    /// Refreshes the saved login right before starting (this also tells the server
    /// that this PC is about to connect). Null = start offline.
    /// </summary>
    async Task<(bool Start, OnlineIdentity? Identity)> PrepareOnline()
    {
        if (!_s.PlayOnline || string.IsNullOrEmpty(_s.AccountToken)) return (true, null);
        StatusText.Text = L.T("Connecting to NOBK.Online…");
        try
        {
            var id = await OnlineAccount.Refresh(_s.AccountToken);
            _s.SetAccount(id);
            try { _s.Save(); } catch { }
            return (true, id);
        }
        catch (OnlineLoginException ex)
        {
            if (ex.SignedOut)
            {
                _s.AccountToken = "";
                try { _s.Save(); } catch { }
                ShowAccount();
            }
            var answer = MessageBox.Show(this, ex.Message + "\n\n" + L.T("Start the game offline instead?"), "NOBK.Online",
                MessageBoxButton.YesNo, MessageBoxImage.Warning);
            return (answer == MessageBoxResult.Yes, null);
        }
    }

    // ----------------------------------------------------------------- play

    async void Play_Click(object sender, RoutedEventArgs e)
    {
        SaveFromUi();
        if (!RefreshStatus()) return;

        try
        {
            PlayButton.IsEnabled = false;
            StatusText.Text = L.T("Preparing game files…");
            await Task.Run(() => _s.ApplyMods());
            RefreshStatus();
        }
        catch (Exception ex)
        {
            PlayButton.IsEnabled = true;
            RefreshStatus();
            MessageBox.Show(this, L.T("Could not apply the mods (buttons / intro / signature):") + "\n" + ex.Message +
                "\n\n" + L.T("The game will start without them."), "FIFA 09 PC", MessageBoxButton.OK, MessageBoxImage.Warning);
        }

        PlayButton.IsEnabled = false;
        var (start, online) = await PrepareOnline();
        if (!start)
        {
            RefreshStatus();
            return;
        }

        var psi = new ProcessStartInfo(GameExe) { WorkingDirectory = BinDir, UseShellExecute = false };
        foreach (var a in _s.BuildArgs(online)) psi.ArgumentList.Add(a);
        if (_s.DiagLog) psi.Environment["NOBK_DIAG"] = "1";

        try
        {
            var game = Process.Start(psi);
            // Stay alive (hidden) until the game exits, so Steam sees it as running.
            Hide();
            if (game != null) await game.WaitForExitAsync();
            Close();
        }
        catch (Exception ex)
        {
            Show();
            PlayButton.IsEnabled = true;
            MessageBox.Show(this, L.T("Could not start the game:") + "\n" + ex.Message, "FIFA 09 PC",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    // --------------------------------------------------------------- chrome

    void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (PageDisplay == null) return; // fires during InitializeComponent
        PageOnline.Visibility = NavOnline.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PageDisplay.Visibility = NavDisplay.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PageGraphics.Visibility = NavGraphics.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PageControls.Visibility = NavControls.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PageGame.Visibility = NavGame.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        PageAch.Visibility = NavAch.IsChecked == true ? Visibility.Visible : Visibility.Collapsed;
        if (NavAch.IsChecked == true) LoadAchievements();
    }

    void TitleBar_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ButtonState == MouseButtonState.Pressed) DragMove();
    }

    void Fullscreen_Changed(object sender, RoutedEventArgs e)
    {
        if (WindowSizePanel != null)
            WindowSizePanel.Visibility = FullscreenBox.IsChecked == true ? Visibility.Collapsed : Visibility.Visible;
    }

    // One flag per language in L.Languages; the current one is lit and underlined.
    void BuildLanguageFlags()
    {
        LangPanel.Children.Clear();
        foreach (var lang in L.Languages)
        {
            var img = new Image { Width = 26, Height = 19.5, Stretch = Stretch.Fill };
            RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality);
            try { img.Source = new BitmapImage(new Uri($"pack://application:,,,/Assets/flags/{lang.Flag}.png")); }
            catch { /* missing flag: the button still works */ }
            var btn = new Button
            {
                Content = img,
                Style = (Style)FindResource("FlagButton"),
                Tag = lang.Code == L.Lang ? "on" : null,
                ToolTip = lang.Name,
            };
            btn.Click += (_, _) => SwitchLanguage(lang.Code);
            LangPanel.Children.Add(btn);
        }
    }

    void SwitchLanguage(string lang)
    {
        if (lang == L.Lang) return;
        SaveFromUi();
        _s.Language = lang;
        try { _s.Save(); } catch { }
        // Rebuild the window in the new language (settings are re-read from launcher.json).
        var w = new MainWindow { Left = Left, Top = Top, WindowStartupLocation = WindowStartupLocation.Manual };
        Application.Current.MainWindow = w;
        w.Show();
        Close();
    }

    // ------------------------------------------------------- launcher update

    UpdateInfo? _update;
    static UpdateInfo? _dismissed; // "later": hidden until the launcher is started again

    async Task CheckForUpdate()
    {
        _update = await Updater.CheckAsync();
        if (_update == null || _dismissed?.Version == _update.Version) return;
        UpdateTitle.Text = L.F("New launcher version: v{0}", _update.Version);
        var notes = _update.NotesFor(L.Lang);
        UpdateNotes.Text = notes;
        UpdateNotes.Visibility = string.IsNullOrWhiteSpace(notes) ? Visibility.Collapsed : Visibility.Visible;
        UpdateButton.Content = L.T(_update.CanInstall ? "Update now" : "Download");
        UpdateBar.Visibility = Visibility.Visible;
    }

    void UpdateLater_Click(object sender, RoutedEventArgs e)
    {
        _dismissed = _update;
        UpdateBar.Visibility = Visibility.Collapsed;
    }

    async void Update_Click(object sender, RoutedEventArgs e)
    {
        if (_update == null) return;
        if (!_update.CanInstall)
        {
            OpenUrl(string.IsNullOrEmpty(_update.Url) ? OnlineAccount.SiteUrl : _update.Url);
            return;
        }
        UpdateButton.IsEnabled = false;
        PlayButton.IsEnabled = false;
        try
        {
            var progress = new Progress<int>(p => UpdateNotes.Text = L.F("Downloading… {0}%", p));
            UpdateNotes.Visibility = Visibility.Visible;
            var files = await Updater.DownloadAsync(_update, progress);
            UpdateNotes.Text = L.T("Installing the update. The launcher will open again in a moment.");
            SaveFromUi();
            Updater.InstallAndRestart(files);
            await Task.Delay(400);
            Application.Current.Shutdown();
        }
        catch (Exception ex)
        {
            UpdateButton.IsEnabled = true;
            RefreshStatus();
            MessageBox.Show(this, L.T("The update could not be downloaded:") + "\n" + ex.Message, "NOBK.Online",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            UpdateNotes.Text = _update.NotesFor(L.Lang);
        }
    }

    void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    void Close_Click(object sender, RoutedEventArgs e) => Close();
}
