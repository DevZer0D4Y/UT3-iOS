// ut3 - ReXGlue Recompiled Project (iOS)
//
// Unreal Tournament 3 (Xbox 360) on the iOS port of ReXGlue. This class adds
// what iOS needs: paths inside the app container, the disc image picker, and
// the settings that suit an iPhone GPU through MoltenVK.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#if REX_PLATFORM_IOS
#include "ios_screen.h"
#include "touch_layout_ut3.h"
#include "iso_import.h"
#include <rex/chrono/clock.h>
#include <rex/audio/audio_system.h>
#include <rex/ui/windowed_app_context_sdl.h>
#endif

#include "game_check.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if REX_PLATFORM_MAC
#include <CoreFoundation/CoreFoundation.h>
#include <rex/cvar_defaults.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#endif

class Ut3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Ut3App>(new Ut3App(ctx, "ut3", PPCImageConfig));
  }

 protected:
#if REX_PLATFORM_MAC && !REX_PLATFORM_IOS
  // macOS: the same port running natively, used to test the iPhone build's
  // code paths (Metal through MoltenVK, arm64, 16 KB pages) on a Mac. The game
  // folder comes from --game_data_root as on desktop.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    SetDefaults();
    game_root_ = paths.game_data_root;
  }

  // After OnConfigurePaths, once there is a log to report to.
  void OnPostInitLogging() override {
    const ut3::GameSource game(game_root_);
    if (const std::string problem = ut3::CheckGame(game); !problem.empty()) {
      REXLOG_ERROR("{}: {}", game_root_.string(), problem);
    }
    SetLanguageDefault(game);
  }

  void OnPostSetup() override {
    SetGuestFrameStats([this] { return SampleFrame(); });
  }
#endif

#if REX_PLATFORM_IOS
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) override {
    std::error_code ec;
    std::string problem;
    if (std::filesystem::is_regular_file(defaults.game_data_root, ec) ||
        std::filesystem::is_regular_file(defaults.game_data_root / "default.xex", ec)) {
      const ut3::GameSource game(defaults.game_data_root);
      problem = ut3::CheckGame(game);
      if (problem.empty()) {
        SetLanguageDefault(game);
        return defaults;
      }
      REXLOG_ERROR("{}: {}", defaults.game_data_root.string(), problem);
    }
    // No game yet (a first launch), or one that cannot be used: ask for the
    // disc, saying why in the second case.
    Ut3ImportISO(
        [defaults, resume = std::move(resume)](std::filesystem::path image) mutable {
          // The importer has checked it.
          SetLanguageDefault(ut3::GameSource(image));
          auto paths = defaults;
          paths.game_data_root = std::move(image);
          resume(std::move(paths));
        },
        std::move(problem));
    return std::nullopt;
  }

  // iOS starts the app with no arguments and its bundle is read-only, so
  // everything lives in <container>/Documents, which the Files app shows
  // (UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace):
  //
  //   Documents/game/          the extracted disc (default.xex, UTGame/)
  //   Documents/userdata/      saves and the profile
  //   Documents/cache/         shader cache
  //   Documents/ut3.toml  settings, editable from Files
  //
  // Defaults go through SetDefaultByName: it ranks below the config file and,
  // unlike SetFlagByName, reaches cvars owned by the GPU plugin, which only
  // registers after this hook runs.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto docs = Documents();
    std::error_code ec;
    if (paths.game_data_root.empty()) {
      paths.game_data_root = docs / "game";
    }
    paths.user_data_root = docs / "userdata";
    paths.cache_root = docs / "cache";
    paths.config_path = docs / "ut3.toml";
    for (const auto& dir : {paths.game_data_root, paths.user_data_root, paths.cache_root}) {
      std::filesystem::create_directories(dir, ec);
    }
    RunCleanupRequest(docs, paths.game_data_root);
    WriteDefaultConfig(paths.config_path);
    SetDefaults();
    // Before the SDK builds its on-screen gamepad from the layout.
    Ut3InstallTouchLayout();
    // Files app import: prefer an explicitly named image over extracted data.
    for (const auto& image : {docs / "game.iso", docs / "game.xiso", docs / "game" / "game.iso"}) {
      if (std::filesystem::is_regular_file(image, ec)) {
        paths.game_data_root = image;
        break;
      }
    }
    REXLOG_INFO("iOS paths: game={} config={}", paths.game_data_root.string(),
                paths.config_path.string());
  }

  void OnPostSetup() override {
    static_cast<rex::ui::SDLWindowedAppContext&>(app_context()).SetLifecycleCallback(
        [this](bool backgrounded) {
          if (backgrounded) {
            static_cast<rex::audio::AudioSystem*>(runtime()->audio_system())->SetBackgrounded(true);
            rex::chrono::Clock::SetPaused(true);
            runtime()->graphics_system()->SetBackgrounded(true);
            REXLOG_INFO("iOS: gameplay suspended; session retained");
            rex::FlushLogging();
          } else {
            rex::chrono::Clock::SetPaused(false);
            static_cast<rex::audio::AudioSystem*>(runtime()->audio_system())->SetBackgrounded(false);
            runtime()->graphics_system()->SetBackgrounded(false);
            has_last_frame_ = false;
            Ut3KeepScreenAwake(true);
            REXLOG_INFO("iOS: gameplay resumed in the existing session");
          }
        });
    Ut3KeepScreenAwake(true);
    // "Guest: N FPS" in the F3 overlay; the SDK only shows it once an app
    // provides frame timing.
    SetGuestFrameStats([this] { return SampleFrame(); });
  }

  void OnShutdown() override {
    static_cast<rex::ui::SDLWindowedAppContext&>(app_context()).SetLifecycleCallback({});
  }
#endif

 private:
#if REX_PLATFORM_IOS
  static std::filesystem::path Documents() {
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home && home[0] ? home : ".") / "Documents";
  }

  // Documents/cleanup.txt: game data paths to delete, one per line, relative to
  // Documents/game (for example "UTGame/Movies"). Without USB the phone's
  // files can only be added from a computer, not removed, so a computer places
  // this list and the next launch applies it, once. Nothing outside the game
  // folder can be named.
  static void RunCleanupRequest(const std::filesystem::path& docs,
                                const std::filesystem::path& game_root) {
    const auto request = docs / "cleanup.txt";
    std::error_code ec;
    if (!std::filesystem::exists(request, ec)) {
      return;
    }
    std::ifstream in(request);
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
      }
      const std::filesystem::path relative(line);
      if (line.empty() || line[0] == '#' || relative.is_absolute() ||
          std::any_of(relative.begin(), relative.end(),
                      [](const std::filesystem::path& part) { return part == ".."; })) {
        continue;
      }
      const auto target = game_root / relative;
      const auto removed = std::filesystem::remove_all(target, ec);
      REXLOG_INFO("cleanup.txt: removed {} ({} entries){}", target.string(),
                  ec ? 0 : removed, ec ? " - " + ec.message() : "");
    }
    in.close();
    std::filesystem::remove(request, ec);
  }

#endif

#if REX_PLATFORM_MAC
#if REX_PLATFORM_IOS
  static void WriteDefaultConfig(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
      return;
    }
    std::ofstream out(path);
    if (!out) {
      return;
    }
    out << "# Unreal Tournament 3 settings. Edit in the Files app; takes effect on relaunch.\n"
           "gpu_plugin = \"xenos\"\n"
           "\n"
           "# The game renders 16:9 and the panel is wider.\n"
           "#   false -> stretch to fill, true -> keep the aspect with side bars\n"
           "present_letterbox = false\n"
           "\n"
           "# On-screen gamepad: \"auto\" shows it while no controller is connected,\n"
           "# \"on\" always, \"off\" never. touch_controls_opacity (0.1 to 1.0) dims it.\n"
           "touch_controls = \"auto\"\n"
           "\n"
           "# Frame rate cap. 60 by default; for the console's 30 FPS set\n"
           "# guest_vblank_rate = 60 and framerate_limit = 30.\n"
           "guest_vblank_rate = 120\n"
           "framerate_limit = 60\n"
           "# When the phone cannot hold the cap (warm, or a heavy scene), run at\n"
           "# this steady rate until it can again. 0 turns this off.\n"
           "framerate_limit_fallback = 30\n"
           "\n"
           "# The game's 2x/4x anti-aliasing, rendered single-sampled: much less GPU\n"
           "# work on a phone. false restores it.\n"
           "vulkan_msaa_as_single_sample = true\n"
           "\n"
           "# Touch look: swipe distance (points per update) for a full turn rate.\n"
           "# Lower is more sensitive. The game's own Settings > Controller\n"
           "# Sensitivity raises the fastest turn rate.\n"
           "touch_look_points_per_full_scale = 1.25\n";
  }

#endif

  // Default the console language to the device's own when the disc has it
  // (UTGame/Localization/Coalesced.<language>); otherwise the game's English.
  // The region-free disc has English, French, German, Spanish and Italian.
  // user_language in ut3.toml still overrides it.
  static void SetLanguageDefault(const ut3::GameSource& game) {
    // Device language, Xbox 360 XC_LANGUAGE_* value, Unreal's language code.
    static const struct {
      const char* device;
      int xbox;
      const char* unreal;
    } kLanguages[] = {
        {"en", 1, "int"}, {"ja", 2, "jpn"}, {"de", 3, "deu"}, {"fr", 4, "fra"},
        {"es", 5, "esn"}, {"it", 6, "ita"}, {"ko", 7, "kor"}, {"zh", 8, "cht"},
        {"pt", 9, "ptb"}, {"pl", 11, "pol"}, {"ru", 12, "rus"},
    };
    CFArrayRef preferred = CFLocaleCopyPreferredLanguages();
    if (!preferred) {
      return;
    }
    for (CFIndex i = 0; i < CFArrayGetCount(preferred); ++i) {
      char code[16] = {};
      auto language = static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, i));
      if (!CFStringGetCString(language, code, sizeof(code), kCFStringEncodingUTF8)) {
        continue;
      }
      const std::string two(code, std::min<size_t>(2, std::strlen(code)));
      const auto match = std::find_if(std::begin(kLanguages), std::end(kLanguages),
                                      [&](const auto& entry) { return two == entry.device; });
      if (match == std::end(kLanguages) ||
          !game.Exists(std::string("UTGame/Localization/Coalesced.") + match->unreal)) {
        continue;
      }
      rex::cvar::SetDefaultByName("user_language", std::to_string(match->xbox));
      REXLOG_INFO("Game language: {} (user_language {})", match->device, match->xbox);
      break;
    }
    CFRelease(preferred);
  }

  static void SetDefaults() {
    using rex::cvar::SetDefaultByName;
    // The Xenos GPU emulation; empty would disable graphics entirely.
    SetDefaultByName("gpu_plugin", "xenos");
    SetDefaultByName("vulkan_log_debug_messages", "false");
    SetDefaultByName("present_letterbox", "false");
    // Games that ask for the profile on the controller's own port refuse to
    // save when only slot 0 is signed in: alias every slot to the one local
    // profile.
    SetDefaultByName("signin_all_user_slots", "true");

    // MoltenVK has no fragment-input coverage mask, and emulating the EDRAM in
    // the pixel shader is slow on this GPU: host render targets let the
    // hardware do coverage and blending.
    SetDefaultByName("fsi_sample_mask_from_sample_id", "true");
    SetDefaultByName("render_target_path_vulkan", "fbo");

    // Performance defaults.
    //
    // Marking every uploaded page stale each frame re-copied all of guest
    // memory the GPU reads, every frame.
    SetDefaultByName("clear_memory_page_state", "false");
    // One queue submission per frame, not one per command buffer end.
    SetDefaultByName("vulkan_submit_on_primary_buffer_end", "false");
    // FIFO only: vsync-locked, evenly paced frames.
    SetDefaultByName("vulkan_allow_present_mode_immediate", "false");
    SetDefaultByName("vulkan_allow_present_mode_mailbox", "false");
    SetDefaultByName("vulkan_allow_present_mode_fifo_relaxed", "false");
    // 60 FPS instead of the console's 30. UT3 presents every second vblank
    // (its own frame smoothing allows up to 62), so the guest sees 120 vblanks
    // a second and the swap is capped at 60. Gameplay runs on the real clock,
    // not on vblanks, so it keeps its speed; the intro movies keep their 30.
    // ut3.toml: guest_vblank_rate = 60 and framerate_limit = 30 for the
    // console's pacing.
    // UT3 renders parts of the frame into 2x and 4x MSAA targets. On a phone
    // GPU those multiply the pixel, depth and EDRAM transfer work for an edge
    // smoothing barely visible on the small screen: back them with
    // single-sampled host images. (gpu_disable_msaa, which changes the guest's
    // own surface info, corrupts UT3's EDRAM layout: a band of garbage across
    // the screen and no weapon model.)
    SetDefaultByName("vulkan_msaa_as_single_sample", "true");
    // A new effect (muzzle flash, explosion, gibs, a weapon's first draw)
    // needs its pipeline compiled for Metal; that happens in the background,
    // but by default every frame that would draw a pipeline still compiling is
    // dropped. In a fight that is many frames in a row: hitches on shooting,
    // switching weapons and kills (well over a thousand pipelines in the first
    // minute of a session). Present them instead, without that one
    // effect for a frame or two. Compiled pipelines are kept in cache/shaders,
    // so each one is compiled once.
    SetDefaultByName("vulkan_async_skip_incomplete_frames", "false");
    // Touch look: a swipe drives the right stick, and the SDK's 4 points per
    // update for full deflection turned slowly in UT3 (the game caps the turn
    // rate at full stick; its Controller Sensitivity option raises that cap).
    SetDefaultByName("touch_look_points_per_full_scale", "1.25");
    SetDefaultByName("guest_vblank_rate", "120");
    SetDefaultByName("framerate_limit", "60");
    // A warm phone cannot hold 60: the GPU falls behind after a
    // minute or so of a match, and scenes of 6k-13k draws a frame are too much
    // for the GPU thread. Fall back to an even 30 then, instead of 35-50 FPS
    // that keep changing, and return to 60 when there is headroom.
    SetDefaultByName("framerate_limit_fallback", "30");
#if REX_PLATFORM_IOS
    SetDefaultByName("clock_no_scaling", "false");
#endif
    // Clear rectangles drawn with Direct3D's unbounded 8192 scissor: find their
    // real extent by running the vertex shader on the CPU, as Xenia does.
    SetDefaultByName("execute_unclipped_draw_vs_on_cpu", "true");
    // Apple's tile-based GPU loads and stores the whole render area of every
    // render pass: upload rewritten guest pages between passes, and size the
    // passes to what their draws write.
    SetDefaultByName("vulkan_batch_dirty_uploads", "true");
    SetDefaultByName("vulkan_tight_render_area", "true");
    // Unified memory: write CPU-modified pages straight into the GPU's buffer
    // when no queued work reads them, and move the rest to the start of the
    // submission instead of a copy that ends the render pass (36 -> 25 pass
    // ends per frame).
    SetDefaultByName("vulkan_shared_memory_direct_upload", "true");
    SetDefaultByName("vulkan_defer_cpu_uploads", "true");
    // Room for scheduling hiccups on a busy or warm phone.
    SetDefaultByName("audio_maxqframes", "16");
    // A CPU write to memory the GPU reads invalidates only its own 16 KB page,
    // not the surrounding 1 MB: game heaps mix CPU data with static vertex
    // buffers and textures, and the widening re-uploaded and re-decoded them
    // every frame.
    SetDefaultByName("shared_memory_invalidation_widen_kb", "16");
    // Shader memory exports are for the GPU's own later draws. Reading each one
    // back for the CPU waits for the GPU every time, which can halve the frame
    // rate.
    SetDefaultByName("readback_memexport", "false");
#if REX_PLATFORM_IOS
    // iOS pulls 1024 samples per audio request; call the game's mixer once per
    // 256-sample frame as the console does instead of four times back to back
    // (choppy audio otherwise).
    SetDefaultByName("audio_pace_callbacks", "true");
#endif
  }

  rex::ui::FrameStats SampleFrame() {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const double dt_ms = std::chrono::duration<double, std::milli>(now - last_frame_).count();
    // Skip the first interval and any absurd one: after the overlay reopens,
    // "previous" covers the whole time it was closed.
    const bool valid = has_last_frame_ && dt_ms > 0.0 && dt_ms < 1000.0;
    last_frame_ = now;
    has_last_frame_ = true;
    if (valid) {
      smoothed_ms_ = smoothed_ms_ <= 0.0 ? dt_ms : smoothed_ms_ * 0.9 + dt_ms * 0.1;
      stats_.frame_time_ms = smoothed_ms_;
      stats_.fps = smoothed_ms_ > 0.0 ? 1000.0 / smoothed_ms_ : 0.0;
      stats_.frame_count = ++frame_count_;
    }
    return stats_;
  }

  std::filesystem::path game_root_;
  rex::ui::FrameStats stats_{};
  std::chrono::steady_clock::time_point last_frame_{};
  double smoothed_ms_ = 0.0;
  uint64_t frame_count_ = 0;
  bool has_last_frame_ = false;
#endif
};
