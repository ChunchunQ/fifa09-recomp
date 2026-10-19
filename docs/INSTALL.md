# FIFA 09 PC + NOBK.Online — Kurulum rehberi

[English below](#english)

FIFA 09'un Xbox 360 sürümü, PC'ye doğal olarak derlenmiş haliyle çalışır; online
maçlar [nobkonline.com](https://nobkonline.com) sunucularından oynanır. Ücretsizdir.
**Oyun dosyaları bu pakette yoktur:** kendi Xbox 360 FIFA 09 kopyandan çıkardığın
dosyalara ihtiyacın var.

## Gerekenler

- Windows 10 veya 11 (64 bit)
- DirectX 12 destekli bir ekran kartı (son 7–8 yılın kartlarının çoğu)
- Kol (Xbox / PlayStation) ya da klavye + fare
- Kendi FIFA 09 Xbox 360 diskinden çıkarılmış oyun klasörü
  (içinde `default.xex`, `data0.big` … `data6.big`, `audiodata\`, `data\` olmalı)

## Kurulum

1. **İndir.** [Releases](../../releases/latest) sayfasından son sürümün zip'ini indir.
2. **Aç.** Zip'i kolay bulacağın bir klasöre çıkar, örneğin `C:\Oyunlar\FIFA09`.
   `Program Files`, OneDrive veya Masaüstü gibi korumalı / eşitlenen klasörlerden kaçın.
3. **Oyun dosyalarını koy.** Kendi oyun dosyalarını `Game\` klasörünün içine kopyala
   (`Game\default.xex` olacak şekilde). Klasörde `PUT_YOUR_GAME_FILES_HERE.txt` dosyası var.
4. **Launcher'ı aç.** `FIFA09 Launcher.exe` dosyasını çalıştır. Windows "bilgisayarınız
   korundu" uyarısı gösterirse **Ek bilgi → Yine de çalıştır** de (launcher imzasız olduğu için).
5. **Dosyaları doğrula.** **Dosyalar** sekmesinde **Oyun dosyalarını doğrula**'ya bas.
   Port belirli bir `default.xex` sürümünden yapıldı; farklı bölge veya güncelleme
   sürümleri çöker. Doğrulama bunu söyler.
6. **Hesap aç.** [nobkonline.com/tr/register](https://nobkonline.com/tr/register/) adresinden
   ücretsiz hesap aç. Kullanıcı adın oyundaki adın olur; maçların, puanın ve istatistiklerin
   bu hesaba kaydedilir.
7. **Giriş yap.** Launcher'ın **Online** sekmesinde aynı kullanıcı adı ve şifreyle gir,
   **NOBK.Online servislerine bağlan** kutusu işaretli kalsın.
8. **Oyna.** **OYNA**'ya bas. Oyunda **EA Sports Online**'a gir; Dereceli, Dostluk veya
   Team Play seç. Bekleyen biri varsa eşleşirsin (arama 2 dakika sürer).
   Team Play / Be A Pro hâlâ test aşamasında: 3 ve daha fazla oyuncuyla maç başlamayabilir.

Maç bitince sonuç birkaç saniye içinde sitede ve liderlik tablosunda görünür.

## Launcher ayarları

| Sekme | Ne var |
|---|---|
| **Ekran** | **Çözünürlük:** 720p / 1080p (oyun 720p çizer, AMD FSR keskinleştirip büyütür, her bilgisayarda hızlı) veya 2K / 4K (doğal çözünürlük, en keskin ama FPS düşer). Tam ekran ya da pencere, pencere boyutu, **Monitör** (birden çok ekranda), **FPS sınırı**. |
| **Grafik** | Doku filtreleme, grafik API (Direct3D 12 önerilen). |
| **Kontroller** | Kol takılıysa doğrudan çalışır. Klavye için **Klavyeyle oyna**'yı aç; tuşlar değiştirilebilir. Tuş simgeleri: Xbox veya PlayStation. |
| **Dosyalar** | Oyun klasörü, dosya doğrulama, **Steam'e ekle** (görselleriyle, Steam Deck'te de çalışır), **Sorun bildir** ve **Ayrıntılı kayıt**. |
| **Başarımlar** | Kazandığın başarımlar (oyunu bir kez açınca liste dolar). |

Launcher kendini günceller: yeni sürüm çıkınca üstte bildirim görünür, **Şimdi güncelle**
yeter. Ayarların ve giriş bilgin korunur.

## Sorun giderme

- **Oyun açılmıyor, 0xc0000142 hatası:** paketteki Visual C++ dosyaları eksik veya eski.
  Microsoft'un "Visual C++ 2015–2022 Redistributable (x64)" paketini kur.
- **FPS düşük / takılma:** **Ekran** sekmesinde **1080p** veya **720p** seç. Dizüstünde şarjı tak,
  Windows güç modunu "En iyi performans" yap, NVIDIA/AMD panelinde `bin\fifa09.exe` için
  yüksek performanslı ekran kartını seç.
- **Online'a bağlanmıyor:** launcher'da giriş yaptığından ve "NOBK.Online servislerine bağlan"
  kutusunun işaretli olduğundan emin ol. Güvenlik duvarı sorarsa `fifa09.exe`'ye izin ver.
  Port açmana gerek yok.
- **Maçta bağlantı koptu:** sonuç yine kaydedilir, kopan taraf **DC** olarak işaretlenir.
- **Çökme veya hata:** **Dosyalar → Ayrıntılı kayıt**'ı aç, sorunu bir kez daha yaşa, sonra
  ne olduğunu yazıp **Log gönder**'e bas. Kayıt bize gelir. Normal oyunda ayrıntılı kaydı kapat.
- **Antivirüs uyarısı:** launcher ve oyun imzasız olduğu için bazı antivirüsler yanlış alarm
  verebilir. Dosyalar bu sayfadan indirildiyse güvenlidir.

Destek ve duyurular: Discord sunucumuz · [nobkonline.com](https://nobkonline.com) · sorun bildirmek için [Hesabım → Sorun bildir](https://nobkonline.com/tr/account/#support)

---

<a name="english"></a>
# FIFA 09 PC + NOBK.Online — Setup guide

The Xbox 360 version of FIFA 09 runs natively on PC (static recompilation); online
matches are played through the [nobkonline.com](https://nobkonline.com) servers. It is free.
**No game files are included:** you need the files from your own Xbox 360 copy of FIFA 09.

## Requirements

- Windows 10 or 11 (64-bit)
- A DirectX 12 graphics card (most cards from the last 7–8 years)
- A controller (Xbox / PlayStation) or keyboard + mouse
- The game folder extracted from your own FIFA 09 Xbox 360 disc
  (with `default.xex`, `data0.big` … `data6.big`, `audiodata\`, `data\`)

## Setup

1. **Download** the zip of the latest version from [Releases](../../releases/latest).
2. **Extract** it to a folder that is easy to find, e.g. `C:\Games\FIFA09`. Avoid
   `Program Files`, OneDrive or the Desktop (protected / synced folders).
3. **Add your game files:** copy them into the `Game\` folder (so that `Game\default.xex`
   exists). The folder contains `PUT_YOUR_GAME_FILES_HERE.txt`.
4. **Start** `FIFA09 Launcher.exe`. If Windows says "Windows protected your PC", choose
   **More info → Run anyway** (the launcher is not code-signed).
5. **Verify:** on the **Files** tab press **Verify game files**. The port was built from
   one specific `default.xex`; other regions or title updates crash. Verify tells you.
6. **Create an account** at [nobkonline.com/register](https://nobkonline.com/register/). Your
   username is your in-game name; matches, rating and stats are saved to it.
7. **Sign in** on the launcher's **Online** tab with the same username and password and keep
   **Connect NOBK.Online services** ticked.
8. **Play:** press **PLAY**, open **EA Sports Online** in the game and pick Ranked, Unranked
   or Team Play. You are paired with whoever is searching (a search lasts 2 minutes).
   Team Play / Be A Pro is still in testing: with 3 or more players the match may not start.

The result shows up on the site and the leaderboard a few seconds after the match.

## Launcher settings

| Tab | What |
|---|---|
| **Display** | **Resolution:** 720p / 1080p (drawn at 720p, AMD FSR sharpens and upscales it, fast on any PC) or 2K / 4K (native, sharpest, costs FPS). Fullscreen or window, window size, **Monitor**, **Frame rate limit**. |
| **Graphics** | Texture filtering, graphics API (Direct3D 12 recommended). |
| **Controls** | A controller just works. For keyboard turn on **Play with keyboard**; keys can be rebound. Button prompts: Xbox or PlayStation. |
| **Files** | Game folder, verify, **Add to Steam** (with artwork, works on Steam Deck too), **Report a problem** and **Detailed log**. |
| **Achievements** | Your unlocked achievements (the list appears after the first start). |

The launcher updates itself: when a new version is out a bar appears at the top, press
**Update now**. Your settings and sign-in are kept.

## Troubleshooting

- **Game does not start, error 0xc0000142:** install Microsoft's "Visual C++ 2015–2022
  Redistributable (x64)".
- **Low FPS / stutter:** pick **1080p** or **720p** under Display. On a laptop plug in the
  charger, set the Windows power mode to "Best performance" and pick the high-performance
  GPU for `bin\fifa09.exe` in the NVIDIA / AMD panel.
- **Cannot connect online:** make sure you are signed in and "Connect NOBK.Online services"
  is ticked. Allow `fifa09.exe` if the firewall asks. No port forwarding is needed.
- **Connection dropped mid-match:** the result is still saved, the side that dropped is marked **DC**.
- **Crash or bug:** turn on **Files → Detailed log**, make it happen once more, write what
  happened and press **Send log**. Turn the detailed log off for normal play.
- **Antivirus warning:** the launcher and the game are not code-signed, so some antivirus
  programs give false alarms. Files downloaded from this page are safe.

NOBK Online is a non-profit community project and is not affiliated with EA Sports.
Game names and artwork belong to their owners.
