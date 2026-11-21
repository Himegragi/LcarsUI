#pragma once
#include "settings_demo.hpp"
#include <cstddef>
#include <optional>
#include <string>
namespace settingsDemo {
inline constexpr std::size_t maxSettingsBytes = 1024 * 1024;
std::optional<SettingsDocument> parseSettings(const std::string& bytes);
std::string writeSettings(const SettingsDocument& document);
}
