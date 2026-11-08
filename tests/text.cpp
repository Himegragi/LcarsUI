#include "lcars/fonts.hpp"
#include "text.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <windows.h>
#include <ft2build.h>
#include FT_FREETYPE_H

bool loadable(const lcars::detail::FontFile& source, FT_Library library) {
    HANDLE file = CreateFileW(source.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    HANDLE mapping = nullptr; void* data = nullptr; FT_Face face = nullptr;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= LONG_MAX &&
        (mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) &&
        (data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) &&
        FT_New_Memory_Face(library, static_cast<const FT_Byte*>(data), static_cast<FT_Long>(size.QuadPart), source.face_index, &face) == 0;
    if (face) FT_Done_Face(face);
    if (data) UnmapViewOfFile(data);
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return ok;
}

int main() {
    const auto& catalog = lcars::system_font_catalog();
    std::cout << "Font families: " << catalog.families().size() << '\n';
    if (catalog.families().empty()) return 1;
    if (catalog.contains(u"Wingdings") || catalog.contains(u"Marlett")) return 6;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library)) return 23;
    for (const auto& family : catalog.families()) if (!loadable(lcars::detail::system_font_file(family.key), library)) return 24;
    FT_Done_FreeType(library);
    std::cout << "Segoe UI " << catalog.contains(u"Segoe UI") << ", JhengHei " << catalog.contains(u"Microsoft JhengHei")
              << ", Emoji " << catalog.contains(u"Segoe UI Emoji") << '\n';
    lcars::detail::Text text;
    auto layout = text.shape(u"笑😀心❤️", 18);
    if (layout.missing != 0) return 2;
    if (std::none_of(layout.glyphs.begin(), layout.glyphs.end(), [](const auto& glyph) { return glyph.cluster == 1 && glyph.font == 9 && glyph.id; }) ||
        std::none_of(layout.glyphs.begin(), layout.glyphs.end(), [](const auto& glyph) { return glyph.cluster == 4 && glyph.font == 9 && glyph.id; })) return 18;
    for (unsigned cluster : {1u, 4u}) {
        auto glyph = *std::find_if(layout.glyphs.begin(), layout.glyphs.end(), [cluster](const auto& g) { return g.cluster == cluster && g.font == 9 && g.id; });
        glyph.x = 0;
        lcars::detail::TextLayout one; one.size = 18; one.glyphs.push_back(glyph);
        lcars::detail::Canvas emoji_canvas(64, 64, {255,255,255});
        text.draw(emoji_canvas, one, 4, 32, {0,0,0});
        bool has_ink = false;
        for (std::size_t i = 0; i < emoji_canvas.pixels.size(); i += 4) {
            if (emoji_canvas.pixels[i] != emoji_canvas.pixels[i+1] || emoji_canvas.pixels[i] != emoji_canvas.pixels[i+2]) return 25;
            has_ink |= emoji_canvas.pixels[i] != 255;
        }
        if (!has_ink) return 26;
    }
    // U+10FFFF is a Unicode noncharacter and is absent even on systems whose
    // symbol fonts assign private-use characters such as U+E000.
    auto missing = text.shape(u"\U0010ffff", 18);
    if (missing.missing != 1 || missing.width <= 0 || missing.carets.count(1)) return 3;
    lcars::detail::Canvas canvas(64, 64, {255, 255, 255});
    text.draw(canvas, missing, 0, 30, {0, 0, 0});
    bool ink = false;
    for (std::size_t i = 0; i < canvas.pixels.size(); i += 4) {
        if (canvas.pixels[i] != 255) ink = true;
        if (canvas.pixels[i] != canvas.pixels[i + 1] || canvas.pixels[i] != canvas.pixels[i + 2]) return 4;
    }
    if (!ink) return 5;
    using namespace lcars::detail;
    std::vector<std::string> events;
    auto emit = [&](std::string_view code) { events.emplace_back(code); };
    auto without_jhenghei = [](std::u16string_view name) { return name == u"Microsoft JhengHei" ? FontFile{} : system_font_file(name); };
    Text fallback(without_jhenghei, emit);
    if (fallback.shape(u"測試使用者", 18).missing || std::count(events.begin(), events.end(), "font.default_missing") != 1) return 20;
    auto extb = text.shape(u"\U00020000", 18);
    if (!system_font_file(u"MingLiU-ExtB").path.empty() && (extb.missing || extb.glyphs[0].font != 3 || extb.carets.count(1))) return 21;
    events.clear();
    auto none = [](std::u16string_view) { return FontFile{}; };
    Text without_fonts(none, emit);
    auto boxes = without_fonts.shape(u"測試使用者", 18);
    if (boxes.missing != 5 || boxes.carets.size() != 6) return 7;
    for (unsigned i = 1; i <= 5; ++i) if (boxes.at(i) <= boxes.at(i-1)) return 8;
    without_fonts.shape(u"測試使用者", 18);
    if (std::count(events.begin(), events.end(), "text.glyph_missing") != 5) return 9;
    events.clear();
    auto private_glyph = without_fonts.shape(u"\ue000", 18);
    without_fonts.shape(u"\ue000", 18);
    if (private_glyph.missing != 1 || std::count(events.begin(), events.end(), "text.glyph_missing") != 1) return 19;
    auto original = system_font_file(u"Segoe UI");
    if (original.path.empty()) return 10;
    auto dir = std::filesystem::temp_directory_path() / L"lcars-字型測試";
    std::filesystem::create_directories(dir);
    auto copy = dir / L"font.ttf";
    std::filesystem::copy_file(original.path, copy, std::filesystem::copy_options::overwrite_existing);
    events.clear();
    {
        Text mapped([&](std::u16string_view family) { return family == u"Segoe UI" ? FontFile{copy, original.face_index} : FontFile{}; }, emit);
        if (mapped.shape(u"Alpha", 18).missing) return 11;
    }
    {
        std::ofstream broken(copy, std::ios::binary | std::ios::trunc); broken << "bad font";
    }
    events.clear();
    {
        Text broken([&](std::u16string_view family) {
            if (family == u"Microsoft JhengHei") return FontFile{copy, original.face_index};
            return system_font_file(family);
        }, emit);
        if (broken.shape(u"測試使用者", 18).missing) return 22;
        broken.shape(u"測試使用者", 18);
        if (std::count(events.begin(), events.end(), "font.file_unreadable") != 1) return 12;
    }
    std::filesystem::remove(copy);
    std::filesystem::remove(dir);
    std::vector<unsigned char> coverage;
    const unsigned char mono[] = {0xa0}, gray2[] = {0x1b}, bgra[] = {1,2,3,17,4,5,6,201};
    if (!bitmap_coverage(1, mono, 3, coverage) || coverage != std::vector<unsigned char>{255,0,255}) return 13;
    if (!bitmap_coverage(3, gray2, 4, coverage) || coverage != std::vector<unsigned char>{0,85,170,255}) return 14;
    if (!bitmap_coverage(7, bgra, 2, coverage) || coverage != std::vector<unsigned char>{17,201}) return 15;
    if (bitmap_coverage(99, mono, 1, coverage)) return 16;
    events.clear();
    Text failed_load(system_font_file, emit);
    auto invalid = failed_load.shape(u"A", 18);
    invalid.glyphs[0].id = 0xffffffffu;
    failed_load.draw(canvas, invalid, 0, 30, {0,0,0});
    if (std::count(events.begin(), events.end(), "text.glyph_load_failed") != 1) return 17;
    return 0;
}
