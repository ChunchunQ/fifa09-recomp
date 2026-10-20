#pragma once
// Writes the achievement list, unlock state and icons next to fifa09.exe so the
// launcher can show them without running the game:
//
//   achievements\achievements.json
//   achievements\icons\<image_id>.png
//
// Exported once the title's achievement data is loaded, and again after every
// unlock. Icons are written only once.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <imgui.h>

#include <rex/runtime.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>
#include <rex/ui/imgui_dialog.h>

namespace fifa09 {

inline std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += char(c);  // UTF-8 passes through
        }
    }
  }
  return out;
}

// FILETIME (100 ns since 1601) -> Unix milliseconds, 0 if never.
inline int64_t FileTimeToUnixMs(uint64_t ft) {
  if (ft == 0) return 0;
  return int64_t((ft - 116444736000000000ull) / 10000ull);
}

class AchievementExporter {
 public:
  using RuntimeGetter = std::function<rex::Runtime*()>;
  explicit AchievementExporter(RuntimeGetter runtime) : runtime_(std::move(runtime)) {}

  static std::filesystem::path Dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path() / "achievements";
#else
    return std::filesystem::current_path() / "achievements";
#endif
  }

  // Returns false while the title's achievements are not loaded yet.
  bool Export() {
    std::lock_guard<std::mutex> lock(mutex_);
    rex::Runtime* runtime = runtime_ ? runtime_() : nullptr;
    if (!runtime || !runtime->kernel_state()) return false;
    auto* ks = runtime->kernel_state();
    auto& mgr = ks->achievements();
    auto list = mgr.ListAchievements();
    if (list.empty()) return false;

    std::error_code ec;
    const auto dir = Dir();
    std::filesystem::create_directories(dir / "icons", ec);

    const auto db = ks->title_xdbf();
    std::string json = "{\n";
    json += "  \"title_id\": \"" + Hex(ks->title_id()) + "\",\n";
    json += "  \"title\": \"" + JsonEscape(db.is_valid() ? db.title() : std::string()) + "\",\n";
    json += "  \"achievements\": [\n";
    for (size_t i = 0; i < list.size(); ++i) {
      const auto& a = list[i];
      bool unlocked = mgr.IsUnlocked(a.id);
      std::string icon;
      if (a.image_id) {
        icon = "icons/" + std::to_string(a.image_id) + ".png";
        auto icon_path = dir / "icons" / (std::to_string(a.image_id) + ".png");
        if (db.is_valid() && !std::filesystem::exists(icon_path, ec)) {
          auto block = db.GetEntry(rex::system::util::XdbfSection::kImage, a.image_id);
          if (block) {
            std::ofstream f(icon_path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(block.buffer), std::streamsize(block.size));
          }
        }
      }
      json += "    {";
      json += "\"id\": " + std::to_string(a.id);
      json += ", \"name\": \"" + JsonEscape(a.label) + "\"";
      json += ", \"description\": \"" + JsonEscape(a.description) + "\"";
      json += ", \"locked_description\": \"" + JsonEscape(a.unachieved_description) + "\"";
      json += ", \"gamerscore\": " + std::to_string(a.gamerscore);
      json += ", \"icon\": \"" + icon + "\"";
      json += ", \"unlocked\": " + std::string(unlocked ? "true" : "false");
      json += ", \"unlocked_at\": " +
              std::to_string(unlocked ? FileTimeToUnixMs(mgr.GetUnlockTime(a.id)) : 0);
      json += i + 1 < list.size() ? "},\n" : "}\n";
    }
    json += "  ]\n}\n";

    // Write to a temp file and swap, so the launcher never reads half a file.
    auto tmp = dir / "achievements.json.tmp";
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f) return true;
      f << json;
    }
    std::filesystem::rename(tmp, dir / "achievements.json", ec);
    return true;
  }

 private:
  static std::string Hex(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
  }

  RuntimeGetter runtime_;
  std::mutex mutex_;
};

// Invisible dialog: polls once a second until the achievements are loaded,
// exports them, hooks unlocks, then stays idle.
class AchievementExportDialog : public rex::ui::ImGuiDialog {
 public:
  AchievementExportDialog(rex::ui::ImGuiDrawer* drawer, AchievementExporter::RuntimeGetter runtime)
      : ImGuiDialog(drawer), runtime_(runtime), exporter_(runtime) {}

  ~AchievementExportDialog() override {
    rex::Runtime* runtime = runtime_();
    if (listener_ && runtime && runtime->kernel_state()) {
      runtime->kernel_state()->achievements().UnregisterCallback(listener_);
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    poll_ += io.DeltaTime;
    if (poll_ < 1.0f) return;
    poll_ = 0.0f;
    if (!exporter_.Export()) return;
    done_ = true;
    listener_ = runtime_()->kernel_state()->achievements().RegisterUnlockCallback(
        [this](const rex::system::AchievementEvent&) { exporter_.Export(); });
  }

 private:
  AchievementExporter::RuntimeGetter runtime_;
  AchievementExporter exporter_;
  bool done_ = false;
  float poll_ = 1.0f;
  rex::system::AchievementListenerHandle listener_ = 0;
};

}  // namespace fifa09
