#include "lcars/fonts.hpp"
#include "text.hpp"
#include "diagnostic_queue.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <set>
#include <cmath>
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

bool catalog_faces_are_local(const lcars::FontCatalog& catalog) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDWriteFactory6> factory; ComPtr<IDWriteFontSet1> all;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6), reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) ||
        FAILED(factory->GetSystemFontSet(TRUE, &all))) return false;
    std::set<std::pair<std::wstring,unsigned>> local;
    for (UINT32 i=0;i<all->GetFontCount();++i) {
        ComPtr<IDWriteFontFaceReference> ref; ComPtr<IDWriteFontFile> file; ComPtr<IDWriteFontFileLoader> loader; ComPtr<IDWriteLocalFontFileLoader> disk;
        const void* key=nullptr; UINT32 bytes=0, length=0;
        if (FAILED(all->GetFontFaceReference(i,&ref)) || ref->GetLocality()!=DWRITE_LOCALITY_LOCAL || FAILED(ref->GetFontFile(&file)) ||
            FAILED(file->GetLoader(&loader)) || FAILED(loader.As(&disk)) || FAILED(file->GetReferenceKey(&key,&bytes)) ||
            FAILED(disk->GetFilePathLengthFromKey(key,bytes,&length))) continue;
        std::wstring path(length+1,L'\0'); if (FAILED(disk->GetFilePathFromKey(key,bytes,path.data(),length+1))) continue;
        path.resize(length); std::transform(path.begin(),path.end(),path.begin(),::towlower);
        local.emplace(path,ref->GetFontFaceIndex());
    }
    for (const auto& family:catalog.families()) {
        auto source=lcars::detail::system_font_file(family.key); auto path=source.path.wstring();
        std::transform(path.begin(),path.end(),path.begin(),::towlower);
        if (!local.count({path,source.face_index})) return false;
    }
    return true;
}

int main() {
    {
        std::vector<std::string> delivered;
        int depth=0, max_depth=0;
        lcars::detail::DiagnosticQueue* queue_ptr=nullptr;
        lcars::detail::DiagnosticQueue queue([&](const lcars::RenderDiagnostic& event) {
            ++depth; max_depth=std::max(max_depth,depth); delivered.push_back(event.code);
            if(event.code=="first") { lcars::RenderDiagnostic nested; nested.code="second"; queue_ptr->push(nested); queue_ptr->drain(); }
            --depth;
        },nullptr);
        queue_ptr=&queue;
        lcars::RenderDiagnostic first; first.code="first";
        queue.push(first);
        if(!delivered.empty()) return 36;
        queue.drain(); queue.drain();
        if(max_depth!=1 || delivered!=std::vector<std::string>{"first","second"}) return 37;
        delivered.clear();
        lcars::FontResolution unavailable{u"missing",u"Segoe UI",false,{}};
        lcars::detail::enqueue_font_diagnostics(queue,static_cast<long>(0x80004005u),unavailable);
        if(!delivered.empty()) return 38;
        queue.drain();
        if(delivered!=std::vector<std::string>{"font.catalog_unavailable","font.family_unavailable"}) return 39;
        std::size_t omitted=0, count=0;
        lcars::detail::DiagnosticQueue bounded([&](const lcars::RenderDiagnostic& event) { omitted+=event.omitted_before; ++count; },nullptr);
        for(int i=0;i<300;++i) bounded.push(first);
        bounded.drain();
        if(count!=256 || omitted!=44) return 42;
    }
    const auto& catalog = lcars::system_font_catalog();
    std::cout << "Font families: " << catalog.families().size() << '\n';
    if (catalog.families().empty()) return 1;
    if (catalog.contains(u"Wingdings") || catalog.contains(u"Marlett")) return 6;
    lcars::PresentationSettings settings;
    auto resolved = catalog.resolve(settings);
    if (resolved.requested != u"" || !resolved.requested_available || resolved.primary != u"Segoe UI") return 27;
    settings.font_family = u"LcarsUI nonexistent font family";
    resolved = catalog.resolve(settings);
    if (resolved.requested_available || resolved.requested != settings.font_family || resolved.primary != u"Segoe UI") return 28;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library)) return 23;
    for (const auto& family : catalog.families()) if (!loadable(lcars::detail::system_font_file(family.key), library)) return 24;
    if (!catalog_faces_are_local(catalog)) return 30;
    FT_Done_FreeType(library);
    std::cout << "Segoe UI " << catalog.contains(u"Segoe UI") << ", JhengHei " << catalog.contains(u"Microsoft JhengHei")
              << ", Emoji " << catalog.contains(u"Segoe UI Emoji") << '\n';
    {
        constexpr std::uint32_t weight_tag=0x74686777u; // DirectWrite's little-endian 'wght'
        bool tested=false;
        for(const auto& family:catalog.families()) {
            auto file=lcars::detail::system_font_file(family.key);
            auto axis=std::find_if(file.axes.begin(),file.axes.end(),[](const auto& a) {return a.tag==weight_tag;});
            if(axis==file.axes.end()) continue;
            lcars::detail::Text probe([&](std::u16string_view name) { return name==u"Segoe UI"?file:lcars::detail::FontFile{}; },[](std::string_view) {});
            if(!probe.test_axis_coordinate(0,weight_tag)) continue;
            axis->value=axis->value>500.f?axis->value-1.f:axis->value+1.f;
            const float expected=axis->value;
            lcars::detail::Text variable([&](std::u16string_view name) { return name==u"Segoe UI"?file:lcars::detail::FontFile{}; },[](std::string_view) {});
            variable.shape(u"A",18);
            auto actual=variable.test_axis_coordinate(0,weight_tag);
            if(!actual || std::abs(*actual-expected)>.01f) { std::cerr << "axis expected " << expected << " got " << (actual?*actual:-1) << '\n'; return 40; }
            tested=true; break;
        }
        if(!tested) { std::cerr << "No variable weight axis available for the transfer test\n"; return 41; }
    }
    lcars::detail::Text text;
    if (catalog.contains(u"Microsoft JhengHei")) {
        text.set_primary_family(u"Microsoft JhengHei");
        auto chosen=text.shape(u"A",18);
        if(chosen.glyphs.empty() || chosen.glyphs[0].font!=10) return 43;
        auto clears=text.atlas_clears();
        text.set_primary_family({});
        if (text.atlas_clears()!=clears+1) return 46;
    }
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
    auto extb_file=system_font_file(u"MingLiU-ExtB");
    if(extb_file.path.empty()) { std::cerr << "FN06 requires an installed MingLiU-ExtB face\n"; return 21; }
    Text isolated_extb([&](std::u16string_view family) { return family==u"MingLiU-ExtB" ? extb_file : FontFile{}; },emit);
    auto extb = isolated_extb.shape(u"\U00020000", 18);
    if (extb.missing || extb.glyphs[0].font != 3 || extb.carets.count(1)) return 21;
    events.clear();
    auto none = [](std::u16string_view) { return FontFile{}; };
    Text without_fonts(none, emit);
    auto boxes = without_fonts.shape(u"測試使用者", 18);
    if (boxes.missing != 5 || boxes.carets.size() != 6) return 7;
    for (unsigned i = 1; i <= 5; ++i) if (boxes.at(i) <= boxes.at(i-1)) return 8;
    without_fonts.shape(u"測試使用者", 18);
    if (std::count(events.begin(), events.end(), "text.glyph_missing") != 5) return 9;
    lcars::Ui ui;
    auto field=ui.add_text({.id="font-test",.kind=lcars::Kind::text,.value=u"測試使用者"});
    if(!field) return 31;
    if(!field->connect_source({[]{return std::u16string(u"測試使用者");}, [](std::function<void()>) { return std::function<void()>([]{}); }}) ||
       !field->connect_confirmed([](const lcars::TextChange&) {return lcars::Result{};})) return 31;
    auto selection=field->select(5,5); auto insert=field->paste(u"字");
    if(!selection || !insert || field->describe()->value!=u"測試使用者字") {
        std::cerr << "editing status " << static_cast<int>(selection.status) << ' ' << static_cast<int>(insert.status) << "\n"; return 31;
    }
    if(without_fonts.shape(field->describe()->value,18).missing!=6 || !field->select(5,6) || !field->paste(u"") || field->describe()->value!=u"測試使用者") return 32;
    if(!field->undo() || field->describe()->value!=u"測試使用者字") return 33;
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
    std::vector<lcars::RenderDiagnostic> detailed;
    Text diagnostic_text(system_font_file,std::function<void(const lcars::RenderDiagnostic&)>([&](const auto& event) {detailed.push_back(event);}));
    auto bad=diagnostic_text.shape(u"A",18); bad.glyphs[0].id=0xffffffffu;
    diagnostic_text.draw(canvas,bad,0,30,{0,0,0});
    auto failure=std::find_if(detailed.begin(),detailed.end(),[](const auto& event) {return event.code=="text.glyph_load_failed";});
    if(failure==detailed.end() || failure->native_code==0 || failure->native_message.find("glyph 4294967295")==std::string::npos || failure->native_message.find("face ")==std::string::npos) return 44;
    events.clear();
    auto valid = failed_load.shape(u"A", 18);
    failed_load.test_bitmap_mode(99);
    lcars::detail::Canvas unsupported(64,64,{255,255,255});
    failed_load.draw(unsupported,valid,0,30,{0,0,0});
    if (std::count(events.begin(), events.end(), "text.glyph_format_unsupported") != 1 || unsupported.pixels == lcars::detail::Canvas(64,64,{255,255,255}).pixels) return 29;
    events.clear();
    RecordingCanvas recorded(64,64,{255,255,255});
    failed_load.draw(recorded,valid,0,30,{0,0,0});
    auto recorded_image=render_cpu(recorded.list,64,64);
    if(recorded_image.pixels!=unsupported.pixels) return 45;
    if(std::count(events.begin(),events.end(),"text.glyph_format_unsupported")!=1 || recorded.list.commands.size()<=1) return 34;
    RecordingCanvas recorded_missing(64,64,{255,255,255});
    without_fonts.draw(recorded_missing,private_glyph,0,30,{0,0,0});
    if(recorded_missing.list.commands.size()<=1) return 35;
    return 0;
}
