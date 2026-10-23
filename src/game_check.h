// Which game files the app was given, and whether the port can run them.
//
// The port is the game's PowerPC code translated ahead of time, so it runs one
// executable: the default.xex it was generated from. Discs of any region carry
// the game for this port as long as their default.xex holds that same code,
// whatever their media ID or encryption key. The XEX security header has a
// SHA-1 over the loaded image's page hashes, which identifies the code itself;
// the build records the one of the default.xex the port came from
// (CMakeLists.txt), and a disc with any other is turned away with a reason
// instead of crashing in code that does not match it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rex::filesystem {
class DiscImageDevice;
}

namespace ut3 {

// An extracted disc folder or an Xbox 360 ISO/XISO.
class GameSource {
 public:
  explicit GameSource(const std::filesystem::path& root);
  ~GameSource();

  // A file inside the game, "UTGame/Localization/Coalesced.int"; names are
  // matched without regard to case, as on the console.
  bool Exists(std::string_view path) const;
  std::optional<std::vector<uint8_t>> Read(std::string_view path, size_t max_bytes) const;

  bool is_disc_image() const { return disc_ != nullptr; }
  bool opened() const { return opened_; }

 private:
  std::optional<std::filesystem::path> FindInFolder(std::string_view path) const;

  std::filesystem::path root_;
  std::unique_ptr<rex::filesystem::DiscImageDevice> disc_;
  bool opened_ = false;
};

// Empty when `game` holds the executable this port was built from, otherwise a
// message for the player saying why it cannot be used.
std::string CheckGame(const GameSource& game);

}  // namespace ut3
