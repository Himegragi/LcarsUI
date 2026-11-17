#pragma once
#include <filesystem>
#include <string_view>
#include <vector>
#include <cstdint>

namespace lcars::detail {
struct FontAxis { std::uint32_t tag; float value; };
struct FontFile { std::filesystem::path path; unsigned faceIndex = 0; std::vector<FontAxis> axes; };
FontFile systemFontFile(std::u16string_view family);
// The family's own file for a bold and/or italic face; empty (path) when it has none, and the text backend
// then draws the look from the regular face. (false, false) is systemFontFile.
FontFile systemStyledFontFile(std::u16string_view family, bool bold, bool italic);
long systemFontCatalogError();
}
