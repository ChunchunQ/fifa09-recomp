#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>

#include "achievement_popup.h"
#include "achievement_export.h"
#include "crash_handler.h"
#include "online_hooks.h"

REXCVAR_DEFINE_STRING(graphics_api, "any", "GPU",
                      "Graphics backend for the GPU plugin: any, d3d12, vulkan")
    .allowed({"any", "d3d12", "vulkan"});

// F2: shows what resolution the game is really producing.
//  - Window:      size of the host window / back buffer.
//  - Game output: size of the image the game hands to the presenter (the swap
//                 source). With working resolution scaling this is the guest
//                 resolution multiplied by the scale (1280x720 x2 = 2560x1440).
//                 If it stays at the unscaled size, the game presents from a
//                 path the scaler does not cover.
//  - Requested scale: the cvars as the GPU plugin read them.
class DisplayInfoDialog : public rex::ui::ImGuiDialog {
 public:
  DisplayInfoDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::Presenter* presenter)
      : ImGuiDialog(drawer), presenter_(presenter) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    // CaptureGuestOutput reads the image back from the GPU, so only once a second.
    auto now = std::chrono::steady_clock::now();
    if (presenter_ && now >= next_capture_) {
      next_capture_ = now + std::chrono::seconds(1);
      rex::ui::RawImage image;
      if (presenter_->CaptureGuestOutput(image)) {
        out_w_ = image.width;
        out_h_ = image.height;
      } else {
        out_w_ = out_h_ = 0;
      }
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 300, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("Display##fifa09", nullptr, ImGuiWindowFlags_NoCollapse)) {
      ImGui::Text("Window:      %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
      if (out_w_ && out_h_) {
        ImGui::Text("Game output: %u x %u", out_w_, out_h_);
      } else {
        ImGui::TextUnformatted("Game output: (no image)");
      }
      ImGui::Separator();
      ImGui::Text("resolution_scale:        %s", Flag("resolution_scale").c_str());
      ImGui::Text("draw_resolution_scale_x: %s", Flag("draw_resolution_scale_x").c_str());
      ImGui::Text("draw_resolution_scale_y: %s", Flag("draw_resolution_scale_y").c_str());
      ImGui::Text("graphics_api:            %s", Flag("graphics_api").c_str());
    }
    ImGui::End();
  }

 private:
  static std::string Flag(const char* name) {
    std::string v = rex::cvar::GetFlagByName(name);
    return v.empty() ? "?" : v;
  }

  rex::ui::Presenter* presenter_;
  std::chrono::steady_clock::time_point next_capture_{};
  uint32_t out_w_ = 0, out_h_ = 0;
};

class Fifa09App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fifa09App>(new Fifa09App(ctx, "fifa09", PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    fifa09::CrashHandler::Install();
    std::string api = REXCVAR_GET(graphics_api);
    if (api != "any" && !config.gpu_plugin.empty())
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, api);
  }

  void OnPostSetup() override {
    SetGuestFrameStats([]() {
      rex::ui::FrameStats stats;
      auto ft_us = rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFrameTimeUs);
      if (ft_us > 0) {
        stats.frame_time_ms = ft_us / 1000.0;
        stats.fps = 1000.0 / stats.frame_time_ms;
        stats.frame_count = 1;
      }
      return stats;
    });
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override { fifa09::LoadPopupFont(atlas); }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rex::ui::RegisterBind("bind_display_info", "F2", "Toggle display info", [this, drawer] {
      if (display_info_) {
        display_info_.reset();
        return;
      }
      rex::ui::Presenter* presenter = nullptr;
      if (runtime() && runtime()->graphics_system())
        presenter = runtime()->graphics_system()->presenter();
      display_info_ = std::make_unique<DisplayInfoDialog>(drawer, presenter);
    });
    // F10: capture the next guest frame in RenderDoc (needs the SDK patch
    // patches/renderdoc-guest-frame-capture.patch and the game launched from RenderDoc).
    rex::ui::RegisterBind("bind_gpu_capture", "F10", "Capture next frame in RenderDoc", [] {
      rex::cvar::SetFlagByName("gpu_capture_next_frame", "true");
    });
    // Keeps achievements\achievements.json (+ icons) up to date for the launcher.
    achievement_export_ = std::make_unique<fifa09::AchievementExportDialog>(
        drawer, [this]() -> rex::Runtime* { return runtime(); });
    // F8: preview the achievement pop-up (does not unlock anything).
    rex::ui::RegisterBind("bind_achievement_test", "F8", "Preview achievement pop-up", [this] {
      if (!runtime() || !runtime()->kernel_state()) return;
      auto list = achievements().ListAchievements();
      if (list.empty()) return;
      achievements().ShowAchievementNotification(list[test_index_++ % list.size()].id);
    });
  }

  // Console-style pop-up with sound instead of the SDK's plain toast.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog()
      override {
    if (!imgui_drawer() || !immediate_drawer() || !runtime()) return nullptr;
    return std::make_unique<fifa09::AchievementPopup>(imgui_drawer(), immediate_drawer(),
                                                      runtime());
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_display_info");
    rex::ui::UnregisterBind("bind_gpu_capture");
    rex::ui::UnregisterBind("bind_achievement_test");
    display_info_.reset();
    achievement_export_.reset();
  }

 private:
  std::unique_ptr<DisplayInfoDialog> display_info_;
  size_t test_index_ = 0;
  std::unique_ptr<fifa09::AchievementExportDialog> achievement_export_;
};
