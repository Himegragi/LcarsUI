#pragma once
#include <filesystem>
#include <string_view>

namespace lcars::detail {
struct FontFile { std::filesystem::path path; unsigned face_index = 0; };
FontFile system_font_file(std::u16string_view family);
}
