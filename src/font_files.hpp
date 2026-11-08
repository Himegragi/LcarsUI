#pragma once
#include <filesystem>
#include <string_view>
#include <vector>
#include <cstdint>

namespace lcars::detail {
struct FontAxis { std::uint32_t tag; float value; };
struct FontFile { std::filesystem::path path; unsigned face_index = 0; std::vector<FontAxis> axes; };
FontFile system_font_file(std::u16string_view family);
long system_font_catalog_error();
}
