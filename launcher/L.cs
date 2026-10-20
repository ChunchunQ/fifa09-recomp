using System.Globalization;
using System.Windows;
using System.Windows.Controls;

namespace Fifa09Launcher;

/// <summary>
/// Launcher languages. The English text is the key: the XAML stays in English and
/// Apply() swaps every TextBlock / button / checkbox text for the selected language.
/// To add a language: add it to Languages (flag = Assets/flags/{flag}.png, also
/// listed in the .csproj) and add its table to Tables.
/// </summary>
public static class L
{
    public sealed record Language(string Code, string Name, string Flag);

    // Shown as flags in the title bar, in this order.
    public static readonly Language[] Languages =
    {
        new("tr", "Türkçe", "tr"),
        new("en", "English", "gb"),
        // new("es", "Español", "es"),   // flag is already included
    };

    public static string Lang { get; set; } = "en";

    public static string DefaultLang
    {
        get
        {
            var sys = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            return Languages.Any(l => l.Code == sys) ? sys : "en";
        }
    }

    public static string T(string en) =>
        Lang != "en" && Tables.TryGetValue(Lang, out var table) && table.TryGetValue(en, out var s) ? s : en;

    public static string F(string en, params object[] args) => string.Format(T(en), args);

    /// <summary>Translates every text in the window (all pages, also hidden ones).</summary>
    public static void Apply(DependencyObject root)
    {
        if (Lang == "en") return;
        switch (root)
        {
            case TextBlock tb when !string.IsNullOrEmpty(tb.Text) && tb.Inlines.Count <= 1:
                tb.Text = T(tb.Text);
                break;
            case ContentControl cc when cc.Content is string s:
                cc.Content = T(s);
                break;
        }
        foreach (var child in LogicalTreeHelper.GetChildren(root))
            if (child is DependencyObject d) Apply(d);
    }

    static readonly Dictionary<string, string> Tr = new()
    {
        // tabs
        ["Online"] = "Online",
        ["Display"] = "Ekran",
        ["Graphics"] = "Grafik",
        ["Controls"] = "Kontroller",
        ["Files"] = "Dosyalar",
        ["Achievements"] = "Başarımlar",
        ["PLAY"] = "OYNA",

        // online
        ["Sign in with your NOBK.Online account to play online. Your username is your name in the game."] =
            "Online oynamak için NOBK.Online hesabınla giriş yap. Kullanıcı adın oyundaki adın olur.",
        ["Username"] = "Kullanıcı adı",
        ["Password"] = "Şifre",
        ["Remember me (sign in automatically next time)"] = "Beni hatırla (sonraki açılışta otomatik giriş)",
        ["Sign in"] = "Giriş yap",
        ["Create account"] = "Hesap oluştur",
        ["Forgot password?"] = "Şifremi unuttum",
        ["Not signed in: the game starts offline (NOBK.Online is not available)."] =
            "Giriş yapılmadı: oyun çevrimdışı açılır (NOBK.Online kullanılamaz).",
        ["Signed in as"] = "Giriş yapılan hesap",
        ["Connect NOBK.Online services"] = "NOBK.Online servislerine bağlan",
        ["Off: the game starts offline, even when you are signed in."] =
            "Kapalıyken oyun, giriş yapmış olsan bile çevrimdışı açılır.",
        ["My account"] = "Hesabım",
        ["Sign out"] = "Çıkış yap",
        ["Country: {0}"] = "Ülke: {0}",
        ["Enter your username and password."] = "Kullanıcı adını ve şifreni gir.",
        ["Signing in…"] = "Giriş yapılıyor…",
        ["✓ Signed in. Press PLAY and open EA Sports Online in the game."] =
            "✓ Giriş yapıldı. OYNA'ya bas, oyunda EA Sports Online'a gir.",
        ["Signed out. The game will start offline."] = "Çıkış yapıldı. Oyun çevrimdışı açılacak.",
        ["Connecting to NOBK.Online…"] = "NOBK.Online'a bağlanılıyor…",
        ["Start the game offline instead?"] = "Oyunu çevrimdışı başlatayım mı?",
        ["Could not reach the NOBK.Online server. Check your internet connection."] =
            "NOBK.Online sunucusuna ulaşılamadı. İnternet bağlantını kontrol et.",
        ["The server sent an unexpected answer."] = "Sunucudan beklenmeyen bir cevap geldi.",
        ["Sign-in failed (HTTP {0})."] = "Giriş yapılamadı (HTTP {0}).",
        ["Language"] = "Dil",

        // display
        ["Render resolution"] = "Çizim çözünürlüğü",
        ["Resolution"] = "Çözünürlük",
        ["FSR · recommended"] = "FSR · önerilen",
        ["native"] = "doğal",
        ["720p and 1080p: AMD FSR sharpens and upscales the picture, fast on any PC. 2K and 4K: drawn natively, sharpest, but cost a lot of FPS. Fullscreen fills your screen; windowed uses the window size below."] =
            "720p ve 1080p: AMD FSR görüntüyü keskinleştirip büyütür, her bilgisayarda hızlı. 2K ve 4K: doğal çözünürlükte çizilir, en keskin ama FPS'i çok düşürür. Tam ekran ekranını doldurur; pencerede aşağıdaki pencere boyutu kullanılır.",
        ["Fullscreen (borderless, uses your desktop resolution)"] = "Tam ekran (kenarlıksız, masaüstü çözünürlüğü)",
        ["Window size"] = "Pencere boyutu",
        ["Monitor"] = "Monitör",
        ["Primary screen ({0} × {1})"] = "Ana monitör ({0} × {1})",
        ["Screen {0} ({1} × {2})"] = "{0}. monitör ({1} × {2})",
        ["Primary screen"] = "Ana monitör",
        ["Detailed log (turn on to report a bug, then play once and send the log)"] =
            "Ayrıntılı kayıt (hata bildirmek için aç, bir kez oyna ve kaydı gönder)",
        ["Writes much more to the log file. Leave it off for normal play."] =
            "Kayıt dosyasına çok daha fazla yazar. Normal oyunda kapalı kalsın.",
        ["Frame rate limit"] = "FPS sınırı",
        ["Unlimited runs as fast as your PC allows. Game speed is not affected."] =
            "Sınırsız, bilgisayarın ne kadar hızlıysa o kadar çalışır. Oyun hızı değişmez.",
        ["Unlimited"] = "Sınırsız",
        ["Auto (for your screen: {0} × {1} + FSR)"] = "Otomatik (ekranına göre: {0} × {1} + FSR)",
        ["1280 × 720 + FSR  (fastest, 1x)"] = "1280 × 720 + FSR  (en hızlı, 1x)",

        // graphics
        ["Texture filtering"] = "Doku filtreleme",
        ["Keeps the pitch, lines and crowd sharp at a distance. Almost free on modern GPUs."] =
            "Saha, çizgiler ve seyirci uzaktan da net görünür. Yeni ekran kartlarında neredeyse bedava.",
        ["Graphics API"] = "Grafik API",
        ["Vulkan is experimental: jersey numbers may be missing."] = "Vulkan deneysel: forma numaraları görünmeyebilir.",
        ["Direct3D 12 (recommended)"] = "Direct3D 12 (önerilen)",
        ["Vulkan (experimental)"] = "Vulkan (deneysel)",
        ["16x (recommended)"] = "16x (önerilen)",
        ["Off"] = "Kapalı",

        // controls
        ["Button prompts"] = "Tuş simgeleri",
        ["Play with keyboard (a controller works without this)"] = "Klavyeyle oyna (kol için gerekmez)",
        ["Reset to defaults"] = "Varsayılana dön",
        ["Press a key. Esc cancels."] = "Bir tuşa bas. Esc iptal eder.",
        ["press a key…"] = "tuşa bas…",
        ["{0} can't be bound. Try another key."] = "{0} atanamaz. Başka bir tuş dene.",

        // achievements
        ["Pop-up in game"] = "Oyunda bildirim",
        ["Sound"] = "Ses",
        ["No achievement data yet"] = "Henüz başarım verisi yok",
        ["Start the game once and the list will appear here."] = "Oyunu bir kez başlat, liste burada görünür.",
        ["{0} / {1} unlocked"] = "{0} / {1} açıldı",
        ["Secret achievement"] = "Gizli başarım",
        ["Locked"] = "Kilitli",

        // files
        ["Folder with your extracted Xbox 360 FIFA 09 files (default.xex, data0.big, ...)"] =
            "Xbox 360 FIFA 09 dosyalarının bulunduğu klasör (default.xex, data0.big, ...)",
        ["Browse…"] = "Gözat…",
        ["Verify game files"] = "Oyun dosyalarını doğrula",
        ["Add to Steam"] = "Steam'e ekle",
        ["Adds the game to your Steam library with artwork. Works on Steam Deck too."] =
            "Oyunu görselleriyle Steam kütüphanene ekler. Steam Deck'te de çalışır.",
        ["Game content is not included. Use files from your own copy of the game."] =
            "Oyun içeriği dahil değildir. Kendi oyun kopyanın dosyalarını kullan.",
        ["default.xex not found in this folder."] = "Bu klasörde default.xex bulunamadı.",
        ["Checking…"] = "Kontrol ediliyor…",
        ["✓ default.xex matches the supported version."] = "✓ default.xex desteklenen sürümle aynı.",
        ["✗ default.xex is a different version (other region or title update). This build will likely crash with it."] =
            "✗ default.xex farklı bir sürüm (başka bölge veya güncelleme). Bu sürümle oyun büyük ihtimalle çöker.",
        ["Steam is not installed (or was never started on this PC)."] = "Steam kurulu değil (ya da bu PC'de hiç açılmamış).",
        ["Steam has to be closed to add the game. Close Steam now? It will be restarted afterwards."] =
            "Oyunu eklemek için Steam'in kapanması gerekiyor. Şimdi kapatılsın mı? Sonra yeniden açılacak.",
        ["Closing Steam…"] = "Steam kapatılıyor…",
        ["Steam did not close. Exit it from the tray icon and try again."] =
            "Steam kapanmadı. Sistem tepsisinden çıkıp tekrar dene.",
        ["No Steam account found. Log in to Steam once, then try again."] =
            "Steam hesabı bulunamadı. Steam'e bir kez giriş yapıp tekrar dene.",
        ["✓ Added to Steam. Hold Shift while starting from Steam to open this launcher."] =
            "✓ Steam'e eklendi. Steam'den açarken Shift'e basılı tutarsan bu launcher açılır.",
        ["Could not add to Steam: "] = "Steam'e eklenemedi: ",

        // status / play
        ["Ready"] = "Hazır",
        ["bin\\fifa09.exe is missing. Reinstall the port."] = "bin\\fifa09.exe yok. Oyunu yeniden kur.",
        ["Game files not found. Set your FIFA 09 folder under Game files."] =
            "Oyun dosyaları bulunamadı. Dosyalar sekmesinden FIFA 09 klasörünü seç.",
        ["Preparing game files…"] = "Oyun dosyaları hazırlanıyor…",
        ["Could not apply the mods (buttons / intro / signature):"] = "Modlar uygulanamadı (tuşlar / intro / imza):",
        ["The game will start without them."] = "Oyun bunlar olmadan açılacak.",
        ["Could not start the game:"] = "Oyun başlatılamadı:",

        // send a log
        ["Report a problem"] = "Sorun bildir",
        ["Send log"] = "Log gönder",
        ["Write what happened (minute, opponent), then send. The game's log file goes to the NOBK.Online team."] =
            "Ne olduğunu yaz (dakika, rakip), sonra gönder. Oyunun kayıt dosyası NOBK.Online ekibine gider.",
        ["Sign in on the Online tab first."] = "Önce Online sekmesinden giriş yap.",
        ["No log yet: start the game once."] = "Henüz kayıt yok: oyunu bir kez başlat.",
        ["Sending…"] = "Gönderiliyor…",
        ["✓ Sent. Thank you!"] = "✓ Gönderildi. Teşekkürler!",
        ["Could not send the log: "] = "Log gönderilemedi: ",

        // launcher update
        ["New launcher version: v{0}"] = "Yeni launcher sürümü: v{0}",
        ["Update now"] = "Şimdi güncelle",
        ["Download"] = "İndir",
        ["Later"] = "Sonra",
        ["Downloading… {0}%"] = "İndiriliyor… %{0}",
        ["Installing the update. The launcher will open again in a moment."] =
            "Güncelleme kuruluyor. Launcher birazdan yeniden açılacak.",
        ["The update could not be downloaded:"] = "Güncelleme indirilemedi:",
        ["The downloaded file is damaged (checksum mismatch). Try again later."] =
            "İndirilen dosya bozuk (sağlama tutmadı). Daha sonra tekrar dene.",
    };

    // After Tr: static fields are initialised in order.
    static readonly Dictionary<string, Dictionary<string, string>> Tables = new()
    {
        ["tr"] = Tr,
    };
}
