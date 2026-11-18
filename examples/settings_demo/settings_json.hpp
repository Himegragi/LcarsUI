#pragma once
#include "settings_demo.hpp"
#include <optional>
#include <string>
namespace settingsDemo {
std::optional<SettingsDocument> parseSettings(const std::string& bytes);
std::string writeSettings(const SettingsDocument& document);
}
