// UT3's on-screen controls: two schemes in the game's red and black, one for
// menus and one for matches, swapped by what the game is doing.
//
// Gameplay: a mobile-shooter layout after the HUD mockup. The left thumb moves
// (stick, with Use and Feign above it); the right thumb aims by dragging and
// has everything used in a fight around the fire button: jump below-left,
// next weapon above-left, alt fire above, the weapon wheel further in. Mapped
// to UT3's gamepad
// bindings as the game actually applies them. The profile's controller preset,
// not Xe-UTInput.ini (which puts fire on RB), decides: checked on the Mac build
// with a scripted pad (ammo count, weapon wheel, scoreboard, feign death):
//
//   RT fire              LT alt fire          RB next weapon
//   LB weapon picker (hold, push the look stick, release)
//   A  jump              B  Impact Hammer (not on screen)
//   X  translocator / hoverboard
//   Y  map (Warfare only; not on screen)   D-pad down: feign death
//   Back: scores         Start: pause menu
//
// There is no crouch on console: the player crouches by itself under low
// ceilings (the tutorial's TUT_CrouchCon). UT3 has no aim-down-sights either:
// LT is each weapon's secondary fire (a zoom only on the sniper rifle).
//
// Menus: a pad for browsing: D-pad, A (select), B (back), X, Y, the shoulders
// and triggers (tabs, list pages), Back and Start.
//
// Switching: loading the front end (UTFrontEnd, UTM-MissionSelection) shows the
// menu scheme, loading a match map (DM-, CTF-, VCTF-, WAR-) the gameplay one.
// In a match the pause button opens the menu scheme with the pause menu; once
// the game has no UI scene open any more (however the menu was closed: B,
// Resume, Start) the gameplay scheme returns. A corner button switches by hand.
// Switches happen when the finger lifts, so the game still sees the press.
//
// UI scenes: UUISceneClient::execOpenScene (0x822ED908, found through the
// native function table: the name "UUISceneClient::execOpenScene" at
// 0x82101CCC, its {function, 0} pair stored by the static initializer at
// 0x82C6F840) gets the scene client in r3; the client's open scenes are a
// TArray at +0x100 (data, count +0x104). Measured on the Mac build: 5-6 in the
// front end, 0 in play, 1 or more while the pause menu or its settings are
// open (and briefly for each tutorial hint banner).
//
// Glyphs: SF Symbols available since iOS 13/14, and PNGs drawn by
// tools/make_touch_icons.swift (ios/touch_icons, copied into the bundle).

#include "touch_layout_ut3.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>

#include <dispatch/dispatch.h>

#include <rex/filesystem/open_observer.h>
#include <rex/input/input.h>
#include <rex/input/touch/touch_layout.h>
#include <rex/logging.h>
#include <rex/ppc.h>

namespace {

using namespace rex::input::touch;

// The layouts are drawn for an iPhone's ~2.17:1 landscape screen. Sizes are
// fractions of the screen height; a circle's frame width is scaled by this so
// it is square there (the overlay keeps themed circles round on any aspect).
constexpr float kHeightOverWidth = 430.0f / 932.0f;

// App buttons (TouchControlDefinition::app_button): handled here, not sent to
// the game.
constexpr uint8_t kToggleButton = 1;  // switch scheme by hand
constexpr uint8_t kResumeButton = 2;  // menu scheme's Start: back to the match

TouchRect Circle(float centre_x, float centre_y, float diameter) {
  const float width = diameter * kHeightOverWidth;
  return {centre_x - width * 0.5f, centre_y - diameter * 0.5f, width, diameter};
}

TouchControlDefinition Button(const char* id, TouchAction action, const char* icon,
                              const TouchRect& frame, uint8_t priority) {
  TouchControlDefinition control;
  control.identifier = id;
  control.type = TouchControlType::kActionButton;
  control.shape = TouchControlShape::kCircle;
  control.normalized_frame = frame;
  control.activation_radius = 0.5f;
  control.analog_tuning.activation_radius = control.activation_radius;
  control.visual_opacity = 0.92f;
  control.capture_priority = priority;
  ConfigureTouchControlAction(action, &control);
  control.icon = icon;
  return control;
}

// A button showing a text label instead of an icon.
TouchControlDefinition LabelButton(const char* id, TouchAction action, const char* label,
                                   const TouchRect& frame, uint8_t priority) {
  TouchControlDefinition control = Button(id, action, "", frame, priority);
  control.label = label;
  return control;
}

// Held, and a drag while held aims: fire with one thumb and keep turning.
void AimWhileHeld(TouchControlDefinition& control, float look_scale) {
  control.hold_while_captured = true;
  control.enables_relative_look = true;
  control.drag_output = TouchAnalogOutput::kLook;
  control.relative_look_scale = look_scale;
  control.analog_tuning.horizontal_scale = look_scale;
  control.analog_tuning.vertical_scale = look_scale;
}

TouchLayoutTheme Theme() {
  TouchLayoutTheme theme;
  theme.face = 0x2C2C30EB;
  theme.face_edge = 0x0B0B0DEB;
  theme.rim = 0xC4161FFF;  // UT3 red
  theme.rim_active = 0xFF4A4AFF;
  theme.glyph = 0xF4F4F4FF;
  theme.stick_face = 0x8A8A9050;  // translucent grey base
  theme.knob = 0x141416F2;
  theme.rim_width = 2.5f;
  return theme;
}

TouchControlDefinition ToggleButton(const char* icon) {
  TouchControlDefinition toggle =
      Button("scheme_toggle", TouchAction::kNone, icon, Circle(0.050f, 0.090f, 0.105f), 252);
  toggle.app_button = kToggleButton;
  toggle.visual_opacity = 0.75f;
  return toggle;
}

TouchLayoutModel GameplayLayout() {
  TouchLayoutModel layout;
  layout.layout_id = "ut3_gameplay";
  layout.display_name = "UT3 Gameplay";
  layout.theme = Theme();

  // Left thumb: move stick, with the less frequent Use and Feign above it.
  TouchControlDefinition stick;
  stick.identifier = "move_stick";
  stick.type = TouchControlType::kMoveStick;
  stick.action = TouchAction::kMove;
  stick.shape = TouchControlShape::kCircle;
  stick.normalized_frame = Circle(0.135f, 0.735f, 0.38f);
  stick.deadzone = 0.12f;
  stick.activation_radius = 0.48f;
  stick.analog_tuning.deadzone = stick.deadzone;
  stick.analog_tuning.activation_radius = stick.activation_radius;
  stick.visual_opacity = 0.85f;
  stick.label_hidden = true;
  stick.capture_priority = 220;
  layout.controls.push_back(stick);

  TouchControlDefinition look;
  look.identifier = "look_zone";
  look.type = TouchControlType::kLookSwipeZone;
  look.action = TouchAction::kLook;
  look.shape = TouchControlShape::kRoundedRect;
  look.normalized_frame = {0.0f, 0.0f, 1.0f, 1.0f};
  look.drag_output = TouchAnalogOutput::kLook;
  look.visual_opacity = 0.0f;
  look.label_hidden = true;
  look.capture_priority = 8;  // under every other control
  layout.controls.push_back(look);

  TouchControlDefinition use = Button("translocator", TouchAction::kButtonX, "sf:hand.raised.fill",
                                      Circle(0.083f, 0.29f, 0.165f), 236);
  use.label = "Use";
  layout.controls.push_back(use);
  TouchControlDefinition feign = Button("feign_death", TouchAction::kDpadDown, "sf:figure.fall",
                                        Circle(0.083f, 0.47f, 0.165f), 236);
  feign.label = "Feign";
  layout.controls.push_back(feign);

  // Right thumb: the fire cluster, in an arc around the fire button.
  TouchControlDefinition fire =
      Button("fire", TouchAction::kRightTrigger, "ut3_fire", Circle(0.895f, 0.745f, 0.300f), 243);
  AimWhileHeld(fire, 1.0f);
  fire.icon_scale = 0.50f;
  layout.controls.push_back(fire);

  TouchControlDefinition alt_fire = Button("alt_fire", TouchAction::kLeftTrigger, "ut3_altfire",
                                           Circle(0.940f, 0.445f, 0.150f), 242);
  AimWhileHeld(alt_fire, 1.0f);
  layout.controls.push_back(alt_fire);

  layout.controls.push_back(
      Button("jump", TouchAction::kButtonA, "ut3_jump", Circle(0.765f, 0.835f, 0.170f), 241));

  layout.controls.push_back(Button("next_weapon", TouchAction::kRightBumper,
                                  "sf:arrow.triangle.2.circlepath", Circle(0.800f, 0.555f, 0.130f),
                                  240));

  // Held, the quick-pick wheel opens; the drag picks a weapon with the look
  // stick, which is how the wheel is driven on a pad.
  TouchControlDefinition picker = Button("weapon_wheel", TouchAction::kLeftBumper, "ut3_wheel",
                                         Circle(0.680f, 0.700f, 0.130f), 240);
  AimWhileHeld(picker, 1.0f);
  layout.controls.push_back(picker);


  // Top: scores and the pause menu (which also brings up the menu scheme).
  layout.controls.push_back(
      Button("scores", TouchAction::kBack, "sf:list.number", Circle(0.465f, 0.075f, 0.11f), 250));
  layout.controls.push_back(
      Button("pause", TouchAction::kStart, "sf:pause.fill", Circle(0.535f, 0.075f, 0.11f), 250));

  layout.controls.push_back(ToggleButton("sf:list.bullet"));
  return layout;
}

TouchLayoutModel MenuLayout() {
  TouchLayoutModel layout;
  layout.layout_id = "ut3_menu";
  layout.display_name = "UT3 Menus";
  layout.theme = Theme();

  // Left: D-pad.
  constexpr float kPadX = 0.140f, kPadY = 0.640f, kPadStep = 0.170f, kPadSize = 0.165f;
  layout.controls.push_back(Button("dpad_up", TouchAction::kDpadUp, "sf:chevron.up",
                                   Circle(kPadX, kPadY - kPadStep, kPadSize), 240));
  layout.controls.push_back(Button("dpad_down", TouchAction::kDpadDown, "sf:chevron.down",
                                   Circle(kPadX, kPadY + kPadStep, kPadSize), 240));
  layout.controls.push_back(
      Button("dpad_left", TouchAction::kDpadLeft, "sf:chevron.left",
             Circle(kPadX - kPadStep * kHeightOverWidth, kPadY, kPadSize), 240));
  layout.controls.push_back(
      Button("dpad_right", TouchAction::kDpadRight, "sf:chevron.right",
             Circle(kPadX + kPadStep * kHeightOverWidth, kPadY, kPadSize), 240));

  // Right: A (select), B (back), X, Y.
  constexpr float kFaceX = 0.860f, kFaceY = 0.640f, kFaceStep = 0.170f, kFaceSize = 0.170f;
  TouchControlDefinition a =
      Button("a_button", TouchAction::kButtonA, "sf:checkmark", Circle(kFaceX, kFaceY + kFaceStep, kFaceSize), 240);
  a.label = "A";
  layout.controls.push_back(a);
  TouchControlDefinition b = Button("b_button", TouchAction::kButtonB, "sf:arrow.uturn.backward",
                                    Circle(kFaceX + kFaceStep * kHeightOverWidth, kFaceY, kFaceSize),
                                    240);
  b.label = "B";
  layout.controls.push_back(b);
  layout.controls.push_back(LabelButton("x_button", TouchAction::kButtonX, "X",
                                        Circle(kFaceX - kFaceStep * kHeightOverWidth, kFaceY, kFaceSize),
                                        240));
  layout.controls.push_back(LabelButton("y_button", TouchAction::kButtonY, "Y",
                                        Circle(kFaceX, kFaceY - kFaceStep, kFaceSize), 240));

  // Top: tabs (bumpers), list pages (triggers), Back and Start.
  layout.controls.push_back(
      LabelButton("lb", TouchAction::kLeftBumper, "LB", Circle(0.200f, 0.090f, 0.120f), 245));
  layout.controls.push_back(
      LabelButton("lt", TouchAction::kLeftTrigger, "LT", Circle(0.300f, 0.090f, 0.120f), 245));
  layout.controls.push_back(
      LabelButton("rt", TouchAction::kRightTrigger, "RT", Circle(0.700f, 0.090f, 0.120f), 245));
  layout.controls.push_back(
      LabelButton("rb", TouchAction::kRightBumper, "RB", Circle(0.800f, 0.090f, 0.120f), 245));
  layout.controls.push_back(
      Button("back", TouchAction::kBack, "sf:list.number", Circle(0.450f, 0.090f, 0.120f), 245));
  TouchControlDefinition start =
      Button("start", TouchAction::kStart, "sf:play.fill", Circle(0.550f, 0.090f, 0.120f), 245);
  start.app_button = kResumeButton;
  layout.controls.push_back(start);

  layout.controls.push_back(ToggleButton("sf:gamecontroller.fill"));
  return layout;
}

// The game's UI scene client (guest address) and guest memory base, from the
// first execOpenScene.
std::atomic<uint32_t> g_scene_client{0};
std::atomic<uint8_t*> g_guest_base{nullptr};

// Scenes the game has open, or -1 before the first scene opened.
int OpenSceneCount() {
  const uint32_t client = g_scene_client.load(std::memory_order_acquire);
  uint8_t* base = g_guest_base.load(std::memory_order_acquire);
  if (!client || !base) {
    return -1;
  }
  uint32_t count;
  std::memcpy(&count, base + client + 0x104, sizeof(count));
  return int(__builtin_bswap32(count));
}

enum class Scheme : int { kMenu, kGameplay };

std::atomic<int> g_scheme{int(Scheme::kMenu)};
std::atomic<bool> g_in_match{false};

void ShowScheme(Scheme scheme) {
  if (g_scheme.exchange(int(scheme)) == int(scheme)) {
    return;
  }
  REXLOG_INFO("Touch controls: {} scheme", scheme == Scheme::kMenu ? "menu" : "gameplay");
  // The overlay reads the layout on the UI thread; replace it there.
  dispatch_async_f(dispatch_get_main_queue(), reinterpret_cast<void*>(intptr_t(scheme)),
                   [](void* context) {
                     const auto scheme = Scheme(intptr_t(context));
                     GetTouchRuntimeModel().SetLayout(scheme == Scheme::kMenu ? MenuLayout()
                                                                              : GameplayLayout());
                   });
}

// Map loads decide the scheme: the front end has menus, match maps gameplay.
void OnFileOpened(const char* guest_path) {
  const std::string_view path(guest_path);
  if (path.find("CookedXenon") == std::string_view::npos) {
    return;
  }
  const size_t slash = path.find_last_of("\\/");
  const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  if (name.find("_LOC_") != std::string_view::npos || !name.ends_with(".xxx")) {
    return;
  }
  if (name == "UTFrontEnd.xxx" || name.starts_with("UTM-")) {
    g_in_match = false;
    ShowScheme(Scheme::kMenu);
  } else if (name.starts_with("DM-") || name.starts_with("CTF-") || name.starts_with("VCTF-") ||
             name.starts_with("WAR-")) {
    g_in_match = true;
    ShowScheme(Scheme::kGameplay);
  }
}

// Watches the overlay's own buttons (the corner toggle, pause and Resume) and,
// in a match, the game's open UI scenes.
void WatchButtons() {
  using Clock = std::chrono::steady_clock;
  uint32_t previous_app = 0;
  uint16_t previous_buttons = 0;
  Clock::time_point menu_since = Clock::now();
  Clock::time_point no_scenes_since{};
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    // In a match, the menu scheme stays while a UI scene is open; once none has
    // been for a moment (the pause menu closed), back to gameplay. A grace
    // period lets the pause menu open after the press that switched.
    if (Scheme(g_scheme.load()) == Scheme::kMenu && g_in_match) {
      const auto now = Clock::now();
      if (OpenSceneCount() == 0) {
        if (no_scenes_since == Clock::time_point{}) no_scenes_since = now;
        if (now - menu_since > std::chrono::milliseconds(1200) &&
            now - no_scenes_since > std::chrono::milliseconds(250)) {
          ShowScheme(Scheme::kGameplay);
        }
      } else {
        no_scenes_since = {};
      }
    } else {
      menu_since = Clock::now();
      no_scenes_since = {};
    }
    const TouchResolvedState state = GetTouchRuntimeModel().LoadResolvedState();
    const uint32_t released_app = previous_app & ~state.app_buttons;
    const uint16_t released = previous_buttons & ~state.buttons;
    previous_app = state.app_buttons;
    previous_buttons = state.buttons;
    const auto scheme = Scheme(g_scheme.load());
    if (released_app & (1u << (kToggleButton - 1))) {
      ShowScheme(scheme == Scheme::kMenu ? Scheme::kGameplay : Scheme::kMenu);
    } else if (scheme == Scheme::kGameplay && (released & rex::input::X_INPUT_GAMEPAD_START)) {
      ShowScheme(Scheme::kMenu);  // the pause menu opened
    } else if (scheme == Scheme::kMenu && g_in_match &&
               (released_app & (1u << (kResumeButton - 1)))) {
      ShowScheme(Scheme::kGameplay);  // Start closes the pause menu
    }
  }
}

}  // namespace

// UUISceneClient::execOpenScene: note the scene client, then open the scene.
extern "C" void __imp__sub_822ED908(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_822ED908) {
  uint32_t expected = 0;
  if (g_scene_client.compare_exchange_strong(expected, ctx.r3.u32)) {
    g_guest_base.store(base, std::memory_order_release);
  }
  __imp__sub_822ED908(ctx, base);
}

void Ut3InstallTouchLayout() {
  // The game boots into the front end.
  GetTouchRuntimeModel().SetLayout(MenuLayout());
  rex::filesystem::SetFileOpenObserver(OnFileOpened);
  std::thread(WatchButtons).detach();
}
