#pragma once
#include <filesystem>
#include <functional>
#include <string>
// macOS: the game the player chose last time, empty if none.
std::filesystem::path Ut3SavedGamePath();
// Asks for the game's disc image or extracted disc folder, checks it and
// remembers it; the game is used where it is, not copied. `problem`, when not
// empty, says why the game the app has cannot be used.
void Ut3ChooseGame(std::function<void(std::filesystem::path)> ready, std::string problem = {});
