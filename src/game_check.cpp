#include "game_check.h"

#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/entry.h>
#include <rex/logging.h>
#include <rex/memory/mapped_memory.h>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "ut3_xex_id.h"  // UT3_XEX_IMAGE_DIGEST, written by CMakeLists.txt

namespace ut3 {

namespace {

// Unreal Tournament 3's Xbox 360 title ID.
constexpr uint32_t kTitleId = 0x4D5707DB;
// XEX2 optional header with the title ID, media ID and version.
constexpr uint32_t kExecutionInfoKey = 0x00040006;
// Offset of the image digest in the XEX2 security info.
constexpr size_t kImageDigestOffset = 0x114;
constexpr size_t kDigestSize = 20;

uint32_t ReadBE32(const std::vector<uint8_t>& data, size_t offset) {
  return uint32_t(data[offset]) << 24 | uint32_t(data[offset + 1]) << 16 |
         uint32_t(data[offset + 2]) << 8 | uint32_t(data[offset + 3]);
}

bool SameName(std::string_view a, std::string_view b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) ==
           std::tolower(static_cast<unsigned char>(y));
  });
}

std::vector<std::string_view> SplitPath(std::string_view path) {
  std::vector<std::string_view> parts;
  while (!path.empty()) {
    const size_t slash = path.find('/');
    const std::string_view part = path.substr(0, slash);
    if (!part.empty()) parts.push_back(part);
    if (slash == std::string_view::npos) break;
    path.remove_prefix(slash + 1);
  }
  return parts;
}

std::string DigestHex(const uint8_t* digest) {
  std::string hex;
  for (size_t i = 0; i < kDigestSize; ++i) hex += fmt::format("{:02x}", digest[i]);
  return hex;
}

}  // namespace

GameSource::GameSource(const std::filesystem::path& root) : root_(root) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(root, ec)) {
    disc_ = std::make_unique<rex::filesystem::DiscImageDevice>("game_check", root);
    opened_ = disc_->Initialize();
  } else {
    opened_ = std::filesystem::is_directory(root, ec);
  }
}

GameSource::~GameSource() = default;

std::optional<std::filesystem::path> GameSource::FindInFolder(std::string_view path) const {
  std::filesystem::path current = root_;
  std::error_code ec;
  for (const std::string_view part : SplitPath(path)) {
    const auto exact = current / std::string(part);
    if (std::filesystem::exists(exact, ec)) {
      current = exact;
      continue;
    }
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
      if (SameName(entry.path().filename().string(), part)) {
        current = entry.path();
        found = true;
        break;
      }
    }
    if (!found) return std::nullopt;
  }
  return current;
}

bool GameSource::Exists(std::string_view path) const {
  if (!opened_) return false;
  if (disc_) {
    std::string disc_path(path);
    std::replace(disc_path.begin(), disc_path.end(), '/', '\\');
    return disc_->ResolvePath(disc_path) != nullptr;
  }
  return FindInFolder(path).has_value();
}

std::optional<std::vector<uint8_t>> GameSource::Read(std::string_view path,
                                                     size_t max_bytes) const {
  if (!opened_) return std::nullopt;
  if (disc_) {
    std::string disc_path(path);
    std::replace(disc_path.begin(), disc_path.end(), '/', '\\');
    rex::filesystem::Entry* entry = disc_->ResolvePath(disc_path);
    if (!entry) return std::nullopt;
    auto mapped = entry->OpenMapped(rex::memory::MappedMemory::Mode::kRead, 0,
                                    std::min(max_bytes, entry->size()));
    if (!mapped) return std::nullopt;
    return std::vector<uint8_t>(mapped->data(), mapped->data() + mapped->size());
  }
  const auto file = FindInFolder(path);
  if (!file) return std::nullopt;
  std::ifstream in(*file, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<uint8_t> data(max_bytes);
  in.read(reinterpret_cast<char*>(data.data()), std::streamsize(max_bytes));
  data.resize(size_t(in.gcount()));
  return data;
}

std::string CheckGame(const GameSource& game) {
  if (!game.opened()) {
    return game.is_disc_image() ? "This file is not an Xbox 360 disc image (ISO/XISO)."
                                : "The game folder could not be opened.";
  }
  // The header, with the optional headers and the security info, fits well
  // within the first 64 KB.
  const auto xex = game.Read("default.xex", 64 * 1024);
  if (!xex) {
    return game.is_disc_image() ? "This disc image has no default.xex: it is not an Xbox 360 game "
                                  "disc, or not the game disc."
                                : "The game folder has no default.xex. Copy the whole extracted "
                                  "disc into it.";
  }
  if (xex->size() < 24 || std::memcmp(xex->data(), "XEX2", 4) != 0) {
    return "default.xex is not an Xbox 360 executable.";
  }
  const uint32_t security_offset = ReadBE32(*xex, 16);
  const uint32_t header_count = ReadBE32(*xex, 20);
  uint32_t title_id = 0, media_id = 0;
  for (uint32_t i = 0; i < header_count && 24 + i * 8 + 8 <= xex->size(); ++i) {
    const uint32_t key = ReadBE32(*xex, 24 + i * 8);
    const uint32_t value = ReadBE32(*xex, 24 + i * 8 + 4);
    if (key == kExecutionInfoKey && size_t(value) + 16 <= xex->size()) {
      media_id = ReadBE32(*xex, value);
      title_id = ReadBE32(*xex, value + 12);
    }
  }
  if (title_id != kTitleId) {
    return fmt::format("This is not Unreal Tournament 3 (title ID {:08X}).", title_id);
  }
  if (size_t(security_offset) + kImageDigestOffset + kDigestSize > xex->size()) {
    return "default.xex is damaged: its header is cut short.";
  }
  const uint8_t* digest = xex->data() + security_offset + kImageDigestOffset;
  const std::string hex = DigestHex(digest);
  REXLOG_INFO("Game: title {:08X} media {:08X} image {}", title_id, media_id, hex);
  if (hex != UT3_XEX_IMAGE_DIGEST) {
    return fmt::format(
        "This disc holds a different release of Unreal Tournament 3 (media ID {:08X}), with a "
        "game executable this port was not built from, so it cannot run it. This port runs "
        "the region-free disc in English, French, German, Spanish and Italian.",
        media_id);
  }
  return {};
}

}  // namespace ut3
