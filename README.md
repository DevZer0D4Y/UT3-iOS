# Unreal Tournament 3 for iPhone and iPad

A static recompilation of *Unreal Tournament 3* (Xbox 360, 2008) for iOS. The game's PowerPC code
is translated ahead of time to C++ and compiled for arm64, and runs on a runtime that provides the
Xbox 360 kernel, audio, input and graphics (Vulkan on Metal through MoltenVK).

**No game content is included. You need your own copy of the Xbox 360 game.**

## Requirements

- An iPhone or iPad with an A15 Bionic or newer (iPhone 13, iPad mini 6th generation and later)
  and iOS 15 or later.
- *Unreal Tournament 3* for Xbox 360: a disc image (ISO/XISO) or the extracted disc.
- Free space for the game: about 6 GB for an extracted disc, or the size of the image for an ISO.
- A Bluetooth controller is recommended. Without one, an on-screen gamepad is shown.

### Supported disc

The port runs the code of one game executable, so it needs the disc that carries it: the
region-free release with English, French, German, Spanish and Italian. A disc from any region
works as long as its `default.xex` holds that same code. When an image is picked, the app checks
it before copying anything, and checks the game again at every launch; a different release of the
game is turned away with an explanation instead of crashing.

## How to play

1. **Download** `UT3-iOS.ipa` from the [latest release](https://github.com/DevZer0D4Y/UT3-iOS/releases/latest)
   (or build it yourself, see [Building](#building)).
2. **Install it** with a sideloading tool: AltStore, SideStore, Sideloadly, ESign or TrollStore.
   The IPA is unsigned; the tool signs it. With a free Apple ID the app must be re-signed every
   7 days; your game files and saves are kept when you do.
3. **Make a disc image of your game.** Dump your own *Unreal Tournament 3* Xbox 360 disc to an
   ISO/XISO and put it in the Files app (On My iPhone, iCloud Drive, or a USB drive).
4. **Open UT3 and tap Choose ISO**, then pick the image. The app checks it first and tells you
   if it is not a disc it can run; then it copies it into the app, which takes a few minutes for a
   full disc. You need free space for the copy.

   Instead of an ISO, you can copy the extracted disc into the app's `game` folder with the Files
   app (On My iPhone > UT3) or from a computer (Finder's Files tab). `game/` must contain
   `default.xex` directly; the `$SystemUpdate` folder is not needed.
5. **Play.** Connect a Bluetooth controller (Xbox, PlayStation or MFi) for the best experience,
   or use the on-screen controls. Start with *Instant Action* to jump into a match.

The first matches stutter now and then while the game's effects are prepared for the GPU; they
are kept in `cache/`, so later sessions are smoother. Your files in the app:

```
UT3/
├── game/        the extracted disc: default.xex, UTGame/
├── game.iso     or the imported disc image
├── userdata/    saves (created by the app)
├── cache/       shader cache (created by the app)
└── ut3.toml     settings (created by the app)
```

The game language follows the device's when the disc has it, and is English otherwise. Switching
apps or locking the device pauses the game, which resumes where it was.

## Controls

The on-screen gamepad shows while no controller is connected. It hides as soon as a controller
connects and comes back when the controller disconnects.

| Controller | Action |
|---|---|
| Right trigger | Fire |
| Left trigger | Alternate fire |
| Right bumper | Next weapon |
| Left bumper (hold) + right stick | Weapon picker |
| A | Jump |
| B | Impact Hammer |
| X | Translocator / hoverboard |
| Back | Scores |
| D-pad down | Feign death |

## Performance

The game runs at up to 60 FPS. When the device cannot hold that (a warm phone, a heavy scene), it
switches to a steady 30 FPS, and goes back to 60 once there is room again. Pipelines for new
effects are compiled the first time they are seen and kept in `cache/`, so the first minutes of
play can stutter more than later sessions.

## Settings

`ut3.toml` can be edited in the Files app; changes apply on the next launch.

| Setting | Default | |
|---|---|---|
| `touch_controls` | `"auto"` | On-screen gamepad: `"auto"` while no controller is connected, `"on"`, or `"off"`. |
| `touch_controls_opacity` | `1.0` | Opacity of the on-screen gamepad, 0.1 to 1.0. |
| `present_letterbox` | `false` | `true` keeps the 16:9 picture with side bars instead of filling the screen. |
| `framerate_limit` | `60` | Frame rate cap. |
| `framerate_limit_fallback` | `30` | Steady rate used while the cap cannot be held; `0` turns the fallback off. |
| `guest_vblank_rate` | `120` | Set `60`, with `framerate_limit = 30` and `framerate_limit_fallback = 0`, for the console's own 30 FPS. |
| `user_language` | device language | Xbox 360 language: 1 English, 3 German, 4 French, 5 Spanish, 6 Italian. |

## Building

On a Mac with Xcode, CMake 3.25+, Ninja and Python 3.

### 1. The SDK

`sdk/` holds the runtime and the code generator. Build it once for the Mac (the code generator
and a macOS test runtime) and once for iOS:

```bash
cd sdk
cmake --preset mac-arm64
cmake --build --preset mac-arm64-release --parallel 6
cmake --install out/build/mac-arm64 --config Release

cmake --preset ios-arm64
cmake --build --preset ios-arm64-release --parallel 6
cmake --install out/build/ios-arm64 --config Release
cd ..
```

### 2. The game code

Put the `default.xex` from your disc in a `game_xex` folder next to this repository
(`../game_xex/default.xex`), then generate the C++ from it:

```bash
export REXGLUE_HOST_TOOL=$PWD/sdk/out/install/mac-arm64/bin/rexglue
export REXGLUE_IOS_SDK=$PWD/sdk/out/install/ios-arm64
export REXGLUE_MAC_SDK=$PWD/sdk/out/install/mac-arm64

"$REXGLUE_HOST_TOOL" codegen ut3_manifest.toml
```

The output goes to `generated/` and is never committed: it is derived from the game.

### 3. The app

```bash
cmake --preset ios-arm64-release
cmake --build --preset ios-arm64-release --parallel 6
bash ios/make-ipa.sh            # writes out/UT3-iOS.ipa
```

A macOS build runs the same code against an extracted disc, which is handy for testing:

```bash
cmake --preset mac-arm64-release
cmake --build --preset mac-arm64-release --parallel 6
out/build/mac-arm64-release/ut3 --game_data_root=<extracted disc>
```

### Analysis fixes

The code generator needs help in a few places, all in `config/`:

- `functions.toml`: two functions reached only by tail branches, a function body split by a jump
  over its neighbour, and five `catch(...)` funclets ending in a rethrow (which never returns, so
  the analyzer ran on into the next funclet).
- `leaf_functions.toml`: 117 leaf functions only reached through pointers.

## Legal

*Unreal Tournament 3* © Epic Games, Inc. Xbox 360 is a trademark of Microsoft. This project is
not affiliated with or endorsed by Epic Games or Microsoft and contains no game content; the app
icon uses the game's cover art. The
runtime in `sdk/` is distributed under the terms in `sdk/LICENSE`.
