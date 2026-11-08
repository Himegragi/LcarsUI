#pragma once
#include "settings_demo.hpp"
#include <optional>
#include <string>
namespace settings_demo {
std::optional<SettingsDocument> parse_settings(const std::string& bytes);
std::string write_settings(const SettingsDocument& document);
}
