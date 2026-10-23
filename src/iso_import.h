#pragma once
#include <filesystem>
#include <functional>
#include <string>
// Asks for the game's disc image and copies it into the app. `problem`, when
// not empty, says why the game the app has cannot be used.
void Ut3ImportISO(std::function<void(std::filesystem::path)> ready, std::string problem = {});
