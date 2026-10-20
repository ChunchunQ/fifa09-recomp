#pragma once
// Console-style achievement pop-up for FIFA 09 PC.
//
// Replaces the SDK's plain toast: a dark pill at the bottom centre that grows
// out of a round badge, shows the icon, "Achievement unlocked" and
// "<gamerscore>G - <name>", then shrinks away. Plays a short chime.
//
// Sound: sounds\achievement.mp3 (or .wav) next to fifa09.exe if present,
// otherwise a chime synthesised at runtime.
//
// Cvars (command line, env or fifa09.toml):
//   achievement_popup         true/false
//   achievement_sound         true/false
//   achievement_sound_volume  0..100

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/overlay/achievement_notification.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

REXCVAR_DEFINE_BOOL(achievement_popup, true, "FIFA09", "Show a pop-up when an achievement unlocks");
REXCVAR_DEFINE_BOOL(achievement_sound, true, "FIFA09", "Play a sound when an achievement unlocks");
REXCVAR_DEFINE_INT32(achievement_sound_volume, 70, "FIFA09",
                     "Volume of the built-in achievement chime, 0-100")
    .range(0, 100);

namespace fifa09 {

// Large, smooth font for the pop-up (the SDK's overlay font is a tiny bitmap
// font). Loaded from Windows' own fonts in LoadPopupFont(); null = fallback.
inline ImFont* g_popup_font = nullptr;

inline void LoadPopupFont(ImFontAtlas* atlas) {
#if defined(_WIN32)
  static const ImWchar ranges[] = {0x0020, 0x017F, 0};  // Latin, Latin-1, Latin Extended-A
  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  for (const char* path : {"C:\\Windows\\Fonts\\segoeuisb.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
                           "C:\\Windows\\Fonts\\arial.ttf"}) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      g_popup_font = atlas->AddFontFromFileTTF(path, 48.0f, &cfg, ranges);
      if (g_popup_font) return;
    }
  }
#else
  (void)atlas;
#endif
}

// ----------------------------------------------------------------- sound

class AchievementSound {
 public:
  void Play() {
#if defined(_WIN32)
    if (!REXCVAR_GET(achievement_sound)) return;
    std::error_code ec;
    const auto dir = ExeDir() / "sounds";
    // MP3 (or WAV) through MCI, which uses Windows' own decoders.
    for (const char* name : {"achievement.mp3", "achievement.wav"}) {
      auto path = dir / name;
      if (!std::filesystem::exists(path, ec)) continue;
      mciSendStringW(L"close fifa09_ach", nullptr, 0, nullptr);
      std::wstring open = L"open \"" + path.wstring() + L"\" type mpegvideo alias fifa09_ach";
      if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
        int volume = REXCVAR_GET(achievement_sound_volume) * 10;  // MCI volume is 0..1000
        mciSendStringW((L"setaudio fifa09_ach volume to " + std::to_wstring(volume)).c_str(),
                       nullptr, 0, nullptr);
        mciSendStringW(L"play fifa09_ach from 0", nullptr, 0, nullptr);
        return;
      }
    }
    int volume = REXCVAR_GET(achievement_sound_volume);
    if (chime_.empty() || chime_volume_ != volume) {
      chime_ = SynthesiseChime(volume / 100.0f);
      chime_volume_ = volume;
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(chime_.data()), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
  }

 private:
#if defined(_WIN32)
  static std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
  }
#endif

  // Two soft bell tones (E6 then B6) with a quick decay, as a 16-bit mono WAV.
  static std::vector<uint8_t> SynthesiseChime(float volume) {
    const uint32_t rate = 44100;
    const float length_s = 0.9f;
    const uint32_t samples = uint32_t(rate * length_s);
    std::vector<int16_t> pcm(samples);
    struct Note { float start, freq; };
    const Note notes[] = {{0.00f, 1318.5f}, {0.12f, 1975.5f}};
    for (uint32_t i = 0; i < samples; ++i) {
      float t = float(i) / rate;
      float v = 0.0f;
      for (const Note& n : notes) {
        float nt = t - n.start;
        if (nt < 0.0f) continue;
        float attack = std::min(1.0f, nt / 0.004f);
        float env = attack * std::exp(-nt * 5.5f);
        // Fundamental plus a quiet octave partial for a bell-like tone.
        v += env * (std::sin(6.2831853f * n.freq * nt) +
                    0.25f * std::sin(6.2831853f * n.freq * 2.0f * nt));
      }
      v *= 0.35f * volume;
      pcm[i] = int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    std::vector<uint8_t> wav(44 + samples * 2);
    auto put32 = [&](size_t at, uint32_t x) { std::memcpy(&wav[at], &x, 4); };
    auto put16 = [&](size_t at, uint16_t x) { std::memcpy(&wav[at], &x, 2); };
    std::memcpy(&wav[0], "RIFF", 4);
    put32(4, uint32_t(36 + samples * 2));
    std::memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);         // PCM
    put16(22, 1);         // mono
    put32(24, rate);
    put32(28, rate * 2);  // byte rate
    put16(32, 2);         // block align
    put16(34, 16);        // bits
    std::memcpy(&wav[36], "data", 4);
    put32(40, samples * 2);
    std::memcpy(&wav[44], pcm.data(), samples * 2);
    return wav;
  }

  std::vector<uint8_t> chime_;
  int chime_volume_ = -1;
};

// ----------------------------------------------------------------- pop-up

class AchievementPopup : public rex::ui::AchievementNotificationDialog {
 public:
  AchievementPopup(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer,
                   rex::Runtime* runtime)
      : AchievementNotificationDialog(drawer), icons_(immediate_drawer, runtime) {}

  // Called from guest threads.
  void Push(const rex::system::AchievementEvent& event) override {
    if (!REXCVAR_GET(achievement_popup)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back({event, std::nullopt});
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();

    Entry current;
    float age = 0.0f;
    bool just_started = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      while (!queue_.empty()) {
        Entry& front = queue_.front();
        if (!front.shown_since) {
          front.shown_since = now;
          just_started = true;
        }
        age = std::chrono::duration<float>(now - *front.shown_since).count();
        if (age < kTotal) break;
        queue_.pop_front();
        just_started = false;
      }
      if (queue_.empty()) return;
      current = queue_.front();
    }
    if (just_started) sound_.Play();

    // Size relative to a 1080p screen, so it looks the same at any resolution.
    const float s = std::clamp(io.DisplaySize.y / 1080.0f, 0.75f, 2.5f);
    const float height = 86.0f * s;
    const float full_width = 500.0f * s;
    const float radius = height * 0.5f;

    // 0..1 progress of the open / close animations.
    auto smooth = [](float x) {
      x = std::clamp(x, 0.0f, 1.0f);
      return x * x * (3.0f - 2.0f * x);
    };
    float badge_in = smooth(age / kBadgeIn);                                  // circle pops in
    float expand = smooth((age - kBadgeIn) / kExpand);                        // pill grows
    float close = smooth((age - (kTotal - kClose)) / kClose);                 // shrink + fade
    float width = height + (full_width - height) * expand * (1.0f - close);
    float alpha = badge_in * (1.0f - close);
    if (alpha <= 0.0f) return;

    const ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 90.0f * s - radius);
    const ImVec2 p0(centre.x - width * 0.5f, centre.y - radius);
    const ImVec2 p1(centre.x + width * 0.5f, centre.y + radius);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto col = [alpha](int r, int g, int b, float a = 1.0f) {
      return IM_COL32(r, g, b, int(255.0f * a * alpha));
    };

    // Body: dark pill with a subtle top highlight and outline.
    dl->AddRectFilled(ImVec2(p0.x + 3 * s, p0.y + 5 * s), ImVec2(p1.x + 3 * s, p1.y + 5 * s),
                      col(0, 0, 0, 0.35f), radius);                           // shadow
    dl->AddRectFilled(p0, p1, col(34, 34, 36, 0.96f), radius);
    dl->AddRectFilled(p0, ImVec2(p1.x, centre.y), col(255, 255, 255, 0.07f), radius,
                      ImDrawFlags_RoundCornersTop);                         // top sheen
    dl->AddRect(p0, p1, col(120, 120, 124, 0.9f), radius, 0, 1.5f * s);

    // Badge: green ring with the achievement icon (or a "G") inside.
    const ImVec2 badge(p0.x + radius, centre.y);
    const float badge_r = radius - 9.0f * s;
    dl->AddCircleFilled(badge, badge_r + 4.0f * s, col(30, 30, 32), 48);
    dl->AddCircle(badge, badge_r + 2.0f * s, col(104, 196, 46), 48, 3.5f * s);
    const auto& info = current.event.achievement;
    if (auto* icon = icons_.GetIcon(info)) {
      const float half = badge_r * 0.78f;
      dl->AddImageRounded(reinterpret_cast<ImTextureID>(icon),
                          ImVec2(badge.x - half, badge.y - half),
                          ImVec2(badge.x + half, badge.y + half), ImVec2(0, 0), ImVec2(1, 1),
                          col(255, 255, 255), half);
    } else {
      DrawCentred(dl, badge, 34.0f * s, col(104, 196, 46), "G");
    }

    // Text fades in once the pill is mostly open, clipped to the pill.
    float text_alpha = smooth((expand - 0.6f) / 0.4f) * (1.0f - close);
    if (text_alpha > 0.0f) {
      dl->PushClipRect(ImVec2(p0.x + height, p0.y), ImVec2(p1.x - radius * 0.5f, p1.y), true);
      ImFont* font = g_popup_font ? g_popup_font : ImGui::GetFont();
      const float x = p0.x + height + 4.0f * s;
      auto tcol = [&](int r, int g, int b) {
        return IM_COL32(r, g, b, int(255.0f * text_alpha * badge_in));
      };
      dl->AddText(font, 19.0f * s, ImVec2(x, centre.y - 27.0f * s), tcol(190, 190, 194),
                  "Achievement unlocked");
      std::string line = std::to_string(info.gamerscore) + "G - " + info.label;
      dl->AddText(font, 25.0f * s, ImVec2(x, centre.y + 0.0f * s), tcol(255, 255, 255),
                  line.c_str());
      dl->PopClipRect();
    }
  }

 private:
  static constexpr float kBadgeIn = 0.25f;
  static constexpr float kExpand = 0.45f;
  static constexpr float kClose = 0.45f;
  static constexpr float kTotal = 5.5f;

  struct Entry {
    rex::system::AchievementEvent event;
    std::optional<std::chrono::steady_clock::time_point> shown_since;
  };

  static void DrawCentred(ImDrawList* dl, ImVec2 c, float size, ImU32 colour, const char* text) {
    ImFont* font = g_popup_font ? g_popup_font : ImGui::GetFont();
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), colour, text);
  }

  std::mutex mutex_;
  std::deque<Entry> queue_;
  rex::ui::AchievementIconCache icons_;
  AchievementSound sound_;
};

}  // namespace fifa09
