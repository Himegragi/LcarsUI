#include "isoduo/fonts.hpp"
#include "text.hpp"
#include "justify.hpp"
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

bool loadable(const isoduo::detail::FontFile& source, FT_Library library) {
    HANDLE file = CreateFileW(source.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    HANDLE mapping = nullptr; void* data = nullptr; FT_Face face = nullptr;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= LONG_MAX &&
        (mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) &&
        (data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) &&
        FT_New_Memory_Face(library, static_cast<const FT_Byte*>(data), static_cast<FT_Long>(size.QuadPart), source.faceIndex, &face) == 0;
    if (face) FT_Done_Face(face);
    if (data) UnmapViewOfFile(data);
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return ok;
}

bool catalogFacesAreLocal(const isoduo::FontCatalog& catalog) {
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
        auto source=isoduo::detail::systemFontFile(family.key); auto path=source.path.wstring();
        std::transform(path.begin(),path.end(),path.begin(),::towlower);
        if (!local.count({path,source.faceIndex})) return false;
    }
    return true;
}

int main() {
    {
        std::vector<std::string> delivered;
        int depth=0, maxDepth=0;
        isoduo::detail::DiagnosticQueue* queuePtr=nullptr;
        isoduo::detail::DiagnosticQueue queue([&](const isoduo::RenderDiagnostic& event) {
            ++depth; maxDepth=std::max(maxDepth,depth); delivered.push_back(event.code);
            if(event.code=="first") { isoduo::RenderDiagnostic nested; nested.code="second"; queuePtr->push(nested); queuePtr->drain(); }
            --depth;
        },nullptr);
        queuePtr=&queue;
        isoduo::RenderDiagnostic first; first.code="first";
        queue.push(first);
        if(!delivered.empty()) return 36;
        queue.drain(); queue.drain();
        if(maxDepth!=1 || delivered!=std::vector<std::string>{"first","second"}) return 37;
        delivered.clear();
        isoduo::FontResolution unavailable{u"missing",u"Segoe UI",false,{}};
        isoduo::detail::enqueueFontDiagnostics(queue,static_cast<long>(0x80004005u),unavailable);
        if(!delivered.empty()) return 38;
        queue.drain();
        if(delivered!=std::vector<std::string>{"font.catalog_unavailable","font.family_unavailable"}) return 39;
        std::size_t omitted=0, count=0;
        isoduo::detail::DiagnosticQueue bounded([&](const isoduo::RenderDiagnostic& event) { omitted+=event.omittedBefore; ++count; },nullptr);
        for(int i=0;i<300;++i) bounded.push(first);
        bounded.drain();
        if(count!=256 || omitted!=44) return 42;
    }
    const auto& catalog = isoduo::systemFontCatalog();
    std::cout << "Font families: " << catalog.families().size() << '\n';
    if (catalog.families().empty()) return 1;
    if (catalog.contains(u"Wingdings") || catalog.contains(u"Marlett")) return 6;
    for (std::size_t i = 1; i < catalog.families().size(); ++i) {
        const auto& a = catalog.families()[i-1].displayName; const auto& b = catalog.families()[i].displayName;
        if (CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, reinterpret_cast<const wchar_t*>(a.c_str()), -1,
                            reinterpret_cast<const wchar_t*>(b.c_str()), -1, nullptr, nullptr, 0) == CSTR_GREATER_THAN) return 49;
    }
    if (!isoduo::detail::systemFontFile(u"Microsoft JhengHei").path.empty() && !catalog.contains(u"Microsoft JhengHei")) return 50;
    isoduo::PresentationSettings settings;
    auto resolved = catalog.resolve(settings);
    if (resolved.requested != u"" || !resolved.requestedAvailable || resolved.primary != u"Segoe UI") return 27;
    settings.fontFamily = u"Isoduo nonexistent font family";
    resolved = catalog.resolve(settings);
    if (resolved.requestedAvailable || resolved.requested != settings.fontFamily || resolved.primary != u"Segoe UI") return 28;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library)) return 23;
    for (const auto& family : catalog.families()) if (!loadable(isoduo::detail::systemFontFile(family.key), library)) return 24;
    if (!catalogFacesAreLocal(catalog)) return 30;
    FT_Done_FreeType(library);
    std::cout << "Segoe UI " << catalog.contains(u"Segoe UI") << ", JhengHei " << catalog.contains(u"Microsoft JhengHei")
              << ", Emoji " << catalog.contains(u"Segoe UI Emoji") << '\n';
    {
        constexpr std::uint32_t weightTag=0x74686777u; // DirectWrite's little-endian 'wght'
        bool tested=false;
        for(const auto& family:catalog.families()) {
            auto file=isoduo::detail::systemFontFile(family.key);
            auto axis=std::find_if(file.axes.begin(),file.axes.end(),[](const auto& a) {return a.tag==weightTag;});
            if(axis==file.axes.end()) continue;
            isoduo::detail::Text probe([&](std::u16string_view name) { return name==u"Segoe UI"?file:isoduo::detail::FontFile{}; },[](std::string_view) {});
            if(!probe.testAxisCoordinate(0,weightTag)) continue;
            axis->value=axis->value>500.f?axis->value-1.f:axis->value+1.f;
            const float expected=axis->value;
            isoduo::detail::Text variable([&](std::u16string_view name) { return name==u"Segoe UI"?file:isoduo::detail::FontFile{}; },[](std::string_view) {});
            variable.shape(u"A",18);
            auto actual=variable.testAxisCoordinate(0,weightTag);
            if(!actual || std::abs(*actual-expected)>.01f) { std::cerr << "axis expected " << expected << " got " << (actual?*actual:-1) << '\n'; return 40; }
            tested=true; break;
        }
        if(!tested) { std::cerr << "No variable weight axis available for the transfer test\n"; return 41; }
    }
    isoduo::detail::Text text;
    if (catalog.contains(u"Microsoft JhengHei")) {
        text.setPrimaryFamily(u"Microsoft JhengHei");
        auto chosen=text.shape(u"A",18);
        if(chosen.glyphs.empty() || chosen.glyphs[0].font!=10) return 43;
        auto clears=text.atlasClears();
        text.setPrimaryFamily({});
        if (text.atlasClears()!=clears+1) return 46;
    }
    {
        auto before = text.shape(u"A", 18);
        text.setPrimaryFamily({}, true);
        auto after = text.shape(u"A", 18);
        if (before.glyphs.empty() || after.glyphs.empty() || before.glyphs[0].faceId == after.glyphs[0].faceId) return 51;
    }
    if (catalog.contains(u"MingLiU")) {
        text.setPrimaryFamily(u"MingLiU");
        auto ming = text.shape(u"\u4e2dA", 24);
        if (ming.missing || ming.glyphs.size() != 2 || ming.glyphs[0].font != 10 || ming.glyphs[1].font != 10 || !ming.glyphs[0].faceId || ming.glyphs[0].faceId != ming.glyphs[1].faceId) return 47;
        text.setPrimaryFamily(u"Segoe UI");
        auto other = text.shape(u"A", 24);
        if (other.glyphs.empty() || other.glyphs[0].faceId == ming.glyphs[1].faceId) return 48;
        text.setPrimaryFamily({});
    } else std::cout << "MingLiU selection: N/A (not installed)\n";
    auto layout = text.shape(u"笑😀心❤️", 18);
    if (layout.missing != 0) return 2;
    if (std::none_of(layout.glyphs.begin(), layout.glyphs.end(), [](const auto& glyph) { return glyph.cluster == 1 && glyph.font == 9 && glyph.id; }) ||
        std::none_of(layout.glyphs.begin(), layout.glyphs.end(), [](const auto& glyph) { return glyph.cluster == 4 && glyph.font == 9 && glyph.id; })) return 18;
    for (unsigned cluster : {1u, 4u}) {
        auto glyph = *std::find_if(layout.glyphs.begin(), layout.glyphs.end(), [cluster](const auto& g) { return g.cluster == cluster && g.font == 9 && g.id; });
        glyph.x = 0;
        isoduo::detail::TextLayout one; one.size = 18; one.glyphs.push_back(glyph);
        isoduo::detail::Canvas emojiCanvas(64, 64, {255,255,255});
        text.draw(emojiCanvas, one, 4, 32, {0,0,0});
        bool hasInk = false;
        for (std::size_t i = 0; i < emojiCanvas.pixels.size(); i += 4) {
            if (emojiCanvas.pixels[i] != emojiCanvas.pixels[i+1] || emojiCanvas.pixels[i] != emojiCanvas.pixels[i+2]) return 25;
            hasInk |= emojiCanvas.pixels[i] != 255;
        }
        if (!hasInk) return 26;
    }
    // U+10FFFF is a Unicode noncharacter and is absent even on systems whose
    // symbol fonts assign private-use characters such as U+E000.
    auto missing = text.shape(u"\U0010ffff", 18);
    if (missing.missing != 1 || missing.width <= 0 || missing.carets.count(1)) return 3;
    isoduo::detail::Canvas canvas(64, 64, {255, 255, 255});
    text.draw(canvas, missing, 0, 30, {0, 0, 0});
    bool ink = false;
    for (std::size_t i = 0; i < canvas.pixels.size(); i += 4) {
        if (canvas.pixels[i] != 255) ink = true;
        if (canvas.pixels[i] != canvas.pixels[i + 1] || canvas.pixels[i] != canvas.pixels[i + 2]) return 4;
    }
    if (!ink) return 5;
    // Missing-glyph outlines preserve the caller's text color on a dark surface.
    isoduo::detail::Canvas darkBox(64, 64, {10, 20, 30});
    text.draw(darkBox, missing, 0, 30, {210, 180, 150});
    bool coloredBox = false;
    for (std::size_t i = 0; i < darkBox.pixels.size(); i += 4)
        coloredBox |= darkBox.pixels[i] == 210 && darkBox.pixels[i+1] == 180 && darkBox.pixels[i+2] == 150;
    if (!coloredBox) return 27;
    using namespace isoduo::detail;
    std::vector<std::string> events;
    auto emit = [&](std::string_view code) { events.emplace_back(code); };
    auto withoutJhenghei = [](std::u16string_view name) { return name == u"Microsoft JhengHei" ? FontFile{} : systemFontFile(name); };
    Text fallback(withoutJhenghei, emit);
    if (fallback.shape(u"測試使用者", 18).missing || std::count(events.begin(), events.end(), "font.default_missing") != 1) return 20;
    auto extbFile=systemFontFile(u"MingLiU-ExtB");
    if(extbFile.path.empty()) { std::cerr << "FN06 requires an installed MingLiU-ExtB face\n"; return 21; }
    Text isolatedExtb([&](std::u16string_view family) { return family==u"MingLiU-ExtB" ? extbFile : FontFile{}; },emit);
    auto extb = isolatedExtb.shape(u"\U00020000", 18);
    if (extb.missing || extb.glyphs[0].font != 3 || extb.carets.count(1)) return 21;
    events.clear();
    auto none = [](std::u16string_view) { return FontFile{}; };
    Text withoutFonts(none, emit);
    auto boxes = withoutFonts.shape(u"測試使用者", 18);
    if (boxes.missing != 5 || boxes.carets.size() != 6) return 7;
    for (unsigned i = 1; i <= 5; ++i) if (boxes.at(i) <= boxes.at(i-1)) return 8;
    withoutFonts.shape(u"測試使用者", 18);
    if (std::count(events.begin(), events.end(), "text.glyph_missing") != 5) return 9;
    isoduo::Ui ui;
    auto field=ui.addTextField({.id="font-test",.value=u"測試使用者"});
    if(!field) return 31;
    if(!field->connectSource({[]{return std::u16string(u"測試使用者");}, [](std::function<void()>) { return std::function<void()>([]{}); }}) ||
       !field->connectConfirmed([](const isoduo::Change<isoduo::TextFieldHandle>&) {return isoduo::Result{};})) return 31;
    auto selection=field->select(5,5); auto insert=field->paste(u"字");
    if(!selection || !insert || field->describe()->value!=u"測試使用者字") {
        std::cerr << "editing status " << static_cast<int>(selection.status) << ' ' << static_cast<int>(insert.status) << "\n"; return 31;
    }
    if(withoutFonts.shape(field->describe()->value,18).missing!=6 || !field->select(5,6) || !field->paste(u"") || field->describe()->value!=u"測試使用者") return 32;
    if(!field->undo() || field->describe()->value!=u"測試使用者字") return 33;
    events.clear();
    auto privateGlyph = withoutFonts.shape(u"\ue000", 18);
    withoutFonts.shape(u"\ue000", 18);
    if (privateGlyph.missing != 1 || std::count(events.begin(), events.end(), "text.glyph_missing") != 1) return 19;
    auto original = systemFontFile(u"Segoe UI");
    if (original.path.empty()) return 10;
    auto dir = std::filesystem::temp_directory_path() / L"isoduo-字型測試";
    std::filesystem::create_directories(dir);
    auto copy = dir / L"font.ttf";
    std::filesystem::copy_file(original.path, copy, std::filesystem::copy_options::overwrite_existing);
    events.clear();
    {
        Text mapped([&](std::u16string_view family) { return family == u"Segoe UI" ? FontFile{copy, original.faceIndex} : FontFile{}; }, emit);
        if (mapped.shape(u"Alpha", 18).missing) return 11;
    }
    {
        std::ofstream broken(copy, std::ios::binary | std::ios::trunc); broken << "bad font";
    }
    events.clear();
    {
        Text broken([&](std::u16string_view family) {
            if (family == u"Microsoft JhengHei") return FontFile{copy, original.faceIndex};
            return systemFontFile(family);
        }, emit);
        if (broken.shape(u"測試使用者", 18).missing) return 22;
        broken.shape(u"測試使用者", 18);
        if (std::count(events.begin(), events.end(), "font.file_unreadable") != 1) return 12;
    }
    std::filesystem::remove(copy);
    std::filesystem::remove(dir);
    std::vector<unsigned char> coverage;
    const unsigned char mono[] = {0xa0}, gray2[] = {0x1b}, bgra[] = {1,2,3,17,4,5,6,201};
    if (!bitmapCoverage(1, mono, 3, coverage) || coverage != std::vector<unsigned char>{255,0,255}) return 13;
    if (!bitmapCoverage(3, gray2, 4, coverage) || coverage != std::vector<unsigned char>{0,85,170,255}) return 14;
    if (!bitmapCoverage(7, bgra, 2, coverage) || coverage != std::vector<unsigned char>{17,201}) return 15;
    if (bitmapCoverage(99, mono, 1, coverage)) return 16;
    events.clear();
    Text failedLoad(systemFontFile, emit);
    auto invalid = failedLoad.shape(u"A", 18);
    invalid.glyphs[0].id = 0xffffffffu;
    failedLoad.draw(canvas, invalid, 0, 30, {0,0,0});
    if (std::count(events.begin(), events.end(), "text.glyph_load_failed") != 1) return 17;
    std::vector<isoduo::RenderDiagnostic> detailed;
    Text diagnosticText(systemFontFile,std::function<void(const isoduo::RenderDiagnostic&)>([&](const auto& event) {detailed.push_back(event);}));
    auto bad=diagnosticText.shape(u"A",18); bad.glyphs[0].id=0xffffffffu;
    diagnosticText.draw(canvas,bad,0,30,{0,0,0});
    auto failure=std::find_if(detailed.begin(),detailed.end(),[](const auto& event) {return event.code=="text.glyph_load_failed";});
    if(failure==detailed.end() || failure->nativeCode==0 || failure->nativeMessage.find("glyph 4294967295")==std::string::npos || failure->nativeMessage.find("face ")==std::string::npos) return 44;
    events.clear();
    auto valid = failedLoad.shape(u"A", 18);
    failedLoad.testBitmapMode(99);
    isoduo::detail::Canvas unsupported(64,64,{255,255,255});
    failedLoad.draw(unsupported,valid,0,30,{0,0,0});
    if (std::count(events.begin(), events.end(), "text.glyph_format_unsupported") != 1 || unsupported.pixels == isoduo::detail::Canvas(64,64,{255,255,255}).pixels) return 29;
    events.clear();
    RecordingCanvas recorded(64,64,{255,255,255});
    failedLoad.draw(recorded,valid,0,30,{0,0,0});
    auto recordedImage=renderCpu(recorded.list,64,64);
    if(recordedImage.pixels!=unsupported.pixels) return 45;
    if(std::count(events.begin(),events.end(),"text.glyph_format_unsupported")!=1 || recorded.list.commands.size()<=1) return 34;
    RecordingCanvas recordedMissing(64,64,{255,255,255});
    withoutFonts.draw(recordedMissing,privateGlyph,0,30,{0,0,0});
    if(recordedMissing.list.commands.size()<=1) return 35;
    // #81: bold and italic faces. A family's real Bold / Italic file is used when there is one, else the
    // regular face is synthesized; the glyph cache tells the four looks of one glyph apart.
    {
        auto styleFlags = [](const FontFile& source) -> long {
            FT_Library lib = nullptr; long flags = -1;
            HANDLE file = CreateFileW(source.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            LARGE_INTEGER size{}; HANDLE mapping = nullptr; void* data = nullptr; FT_Face face = nullptr;
            if (file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && (mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) &&
                (data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) && !FT_Init_FreeType(&lib) &&
                !FT_New_Memory_Face(lib, static_cast<const FT_Byte*>(data), static_cast<FT_Long>(size.QuadPart), source.faceIndex, &face)) flags = face->style_flags;
            if (face) FT_Done_Face(face);
            if (lib) FT_Done_FreeType(lib);
            if (data) UnmapViewOfFile(data);
            if (mapping) CloseHandle(mapping);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            return flags;
        };
        auto inkOf = [](Text& renderer, const TextLayout& layout) {
            Canvas canvas(96, 48, {255, 255, 255}); canvas.countInk = true;
            renderer.draw(canvas, layout, 6, 34, {0, 0, 0});
            return canvas;
        };
        auto rasterized = [](Text& renderer, std::initializer_list<const TextLayout*> layouts) {
            RecordingCanvas record(96, 48, {255, 255, 255});
            const auto before = renderer.rasterizations();
            for (const auto* layout : layouts) renderer.draw(record, *layout, 4, 34, {0, 0, 0});
            renderCpu(record.list, 96, 48);
            return renderer.rasterizations() - before;
        };
        if (catalog.contains(u"Segoe UI")) {
            auto regular = systemStyledFontFile(u"Segoe UI", false, false), bold = systemStyledFontFile(u"Segoe UI", true, false);
            auto italic = systemStyledFontFile(u"Segoe UI", false, true), both = systemStyledFontFile(u"Segoe UI", true, true);
            if (regular.path.empty() || bold.path.empty() || italic.path.empty() || both.path.empty()) return 52;
            if (styleFlags(bold) < 0 || !(styleFlags(bold) & FT_STYLE_FLAG_BOLD) || (styleFlags(bold) & FT_STYLE_FLAG_ITALIC)) return 53;
            if (!(styleFlags(italic) & FT_STYLE_FLAG_ITALIC) || (styleFlags(italic) & FT_STYLE_FLAG_BOLD)) return 54;
            if (!(styleFlags(both) & FT_STYLE_FLAG_ITALIC) || !(styleFlags(both) & FT_STYLE_FLAG_BOLD)) return 55;
            if (regular.path != systemFontFile(u"Segoe UI").path) return 56;
            Text styled;
            auto aRegular = styled.shape(u"A", 24), aBold = styled.shape(u"A", 24, isoduo::FontStyle{true, false});
            auto aItalic = styled.shape(u"A", 24, isoduo::FontStyle{false, true}), aBoth = styled.shape(u"A", 24, isoduo::FontStyle{true, true});
            if (aBold.glyphs[0].synth || aItalic.glyphs[0].synth || aBoth.glyphs[0].synth || aRegular.glyphs[0].synth) return 57;  // real faces: nothing to synthesize
            if (aBold.glyphs[0].faceId == aRegular.glyphs[0].faceId || aItalic.glyphs[0].faceId == aRegular.glyphs[0].faceId ||
                aBoth.glyphs[0].faceId == aBold.glyphs[0].faceId || aBoth.glyphs[0].faceId == aItalic.glyphs[0].faceId) return 58;
            if (aBold.width <= aRegular.width) return 59;  // the real Bold face is wider
            auto pRegular = inkOf(styled, aRegular).pixels, pBold = inkOf(styled, aBold).pixels;
            auto pItalic = inkOf(styled, aItalic).pixels, pBoth = inkOf(styled, aBoth).pixels;
            if (pRegular == pBold || pRegular == pItalic || pBold == pBoth || pItalic == pBoth || pRegular == pBoth) return 60;
            // The glyph cache keeps the four looks apart and reuses each one.
            styled.clearAtlas();
            if (rasterized(styled, {&aRegular, &aBold, &aItalic, &aBoth}) != 4) return 61;
            if (rasterized(styled, {&aRegular, &aBold, &aItalic, &aBoth}) != 0) return 62;
        }
        if (catalog.contains(u"Microsoft JhengHei")) {
            auto regular = systemStyledFontFile(u"Microsoft JhengHei", false, false), bold = systemStyledFontFile(u"Microsoft JhengHei", true, false);
            if (bold.path.empty() || !(styleFlags(bold) & FT_STYLE_FLAG_BOLD) || (bold.path == regular.path && bold.faceIndex == regular.faceIndex)) return 64;  // the real Bold file
            Text jheng;
            jheng.setPrimaryFamily(u"Microsoft JhengHei");
            auto chRegular = jheng.shape(u"重", 24), chBold = jheng.shape(u"重", 24, isoduo::FontStyle{true, false});
            if (chBold.glyphs[0].synth || chBold.glyphs[0].faceId == chRegular.glyphs[0].faceId) return 65;
            if (inkOf(jheng, chBold).ink <= inkOf(jheng, chRegular).ink) return 66;  // the Bold face covers more pixels
            if (systemStyledFontFile(u"Microsoft JhengHei", false, true).path.empty()) {  // no italic file: synthesize
                auto chItalic = jheng.shape(u"重", 24, isoduo::FontStyle{false, true});
                if (!(chItalic.glyphs[0].synth & 2) || chItalic.glyphs[0].faceId != chRegular.glyphs[0].faceId) return 67;
                if (inkOf(jheng, chItalic).pixels == inkOf(jheng, chRegular).pixels) return 68;  // sheared: visibly different
                auto chBoth = jheng.shape(u"重", 24, isoduo::FontStyle{true, true});
                if (chBoth.glyphs[0].synth != 2 || chBoth.glyphs[0].faceId != chBold.glyphs[0].faceId) return 69;  // real bold face, synthetic slant
            } else std::cout << "JhengHei italic synthesis: N/A (an Italic file is installed)\n";
        }
        // A family with only a regular file: bold is synthesized (wider and heavier), italic is sheared.
        auto regularOnly = systemFontFile(u"Segoe UI");
        if (!regularOnly.path.empty()) {
            Text synthetic([&](std::u16string_view name) { return name == u"Segoe UI" ? regularOnly : FontFile{}; }, [](std::string_view) {});
            auto plain = synthetic.shape(u"Hi", 24), heavy = synthetic.shape(u"Hi", 24, isoduo::FontStyle{true, false}), slanted = synthetic.shape(u"Hi", 24, isoduo::FontStyle{false, true});
            if (!(heavy.glyphs[0].synth & 1) || !(slanted.glyphs[0].synth & 2) || heavy.glyphs[0].faceId != plain.glyphs[0].faceId) return 70;
            if (heavy.width <= plain.width) return 71;
            if (inkOf(synthetic, heavy).ink <= inkOf(synthetic, plain).ink) return 72;
            if (inkOf(synthetic, slanted).pixels == inkOf(synthetic, plain).pixels) return 73;
            auto boldItalic = synthetic.shape(u"Hi", 24, isoduo::FontStyle{true, true});
            if (boldItalic.glyphs[0].synth != 3) return 74;
            if (rasterized(synthetic, {&plain, &heavy, &slanted, &boldItalic}) != 8) return 75;  // two glyphs in four looks: one face, eight cache entries
            // #81 x #82: a synthesized style and a vertical-writing transform are separate parts of the cache key. The same
            // glyph of one face, plain / synthetic bold, each upright / rotated / squeezed, is six entries; none replaces another.
            {
                auto one = synthetic.shape(u"H", 24), heavyOne = synthetic.shape(u"H", 24, isoduo::FontStyle{true, false});
                auto with = [](TextLayout layout, bool rotated, float scaleX) { layout.glyphs[0].transform = GlyphTransform{rotated, scaleX, 1.f}; return layout; };
                auto rotated = with(one, true, 1.f), rotatedHeavy = with(heavyOne, true, 1.f), squeezed = with(one, false, .5f), squeezedHeavy = with(heavyOne, false, .5f);
                if (one.glyphs[0].faceId != heavyOne.glyphs[0].faceId || one.glyphs[0].synth != 0 || !(heavyOne.glyphs[0].synth & 1)) return 78;
                synthetic.clearAtlas();
                if (rasterized(synthetic, {&one, &heavyOne, &rotated, &rotatedHeavy, &squeezed, &squeezedHeavy}) != 6) return 79;  // style x transform: six entries
                if (rasterized(synthetic, {&one, &heavyOne, &rotated, &rotatedHeavy, &squeezed, &squeezedHeavy}) != 0) return 80;  // and each is reused
                if (inkOf(synthetic, rotatedHeavy).ink <= inkOf(synthetic, rotated).ink) return 81;     // the rotated bold is still heavier than the rotated regular
                if (inkOf(synthetic, rotated).pixels == inkOf(synthetic, one).pixels) return 82;         // and the rotation still shows
                if (inkOf(synthetic, rotatedHeavy).pixels == inkOf(synthetic, heavyOne).pixels) return 83;
            }
        }
        // The fallback fonts are styled the same way: a Latin and a CJK character in one bold run.
        if (catalog.contains(u"Segoe UI") && catalog.contains(u"Microsoft JhengHei")) {
            Text fallbackText;
            auto mixed = fallbackText.shape(u"A重", 24, isoduo::FontStyle{true, false});
            if (mixed.missing || mixed.glyphs.size() != 2 || mixed.glyphs[0].font != 0 || mixed.glyphs[1].font != 1 || mixed.glyphs[0].synth || mixed.glyphs[1].synth) return 76;
            if (mixed.glyphs[1].faceId == fallbackText.shape(u"重", 24).glyphs[0].faceId) return 77;
        }
    }
    // #84 justified lines through the real shaper: its cluster stops, and the shaping of the pieces that get drawn.
    {
        Text shaper;
        ShapedMeasure measure(shaper);
        // Offsets: 漢 0, variation selector 1, 語 2, woman 3-4, joiner 5, laptop 6-7, 字 8, 體 9.
        const std::u16string line = u"漢︀語\U0001f469‍\U0001f4bb字體";
        const auto natural = measure.measure(line, 18);
        for (auto method : {isoduo::JustifyMethod::automatic, isoduo::JustifyMethod::perCharacter}) {
            auto stretched = justifyLine(measure, line, 18, natural, natural.width + 60.f, method);
            if (!stretched) { std::cerr << "justify: a line with characters to spread was not stretched\n"; return 90; }
            for (const auto& piece : stretched->segments)
                for (unsigned inside : {1u, 4u, 5u, 6u, 7u})
                    if (piece.begin == inside) { std::cerr << "justify: cut inside a character at " << inside << '\n'; return 91; }
            if (std::abs(stretched->run.width - (natural.width + 60.f)) > 1.5f) { std::cerr << "justify: width " << stretched->run.width << " for " << natural.width + 60.f << '\n'; return 92; }
        }
        // Mixed text, every method: the content ends flush at the target width (within a pixel).
        const std::u16string mixed = u"Hello world 中文 test, 12345 繁體";
        const auto mixedRun = measure.measure(mixed, 18);
        for (auto method : {isoduo::JustifyMethod::automatic, isoduo::JustifyMethod::spaces, isoduo::JustifyMethod::perCharacter}) {
            auto stretched = justifyLine(measure, mixed, 18, mixedRun, mixedRun.width + 40.f, method);
            if (!stretched || std::abs(stretched->run.width - (mixedRun.width + 40.f)) > 2.f) { std::cerr << "justify: mixed line not flush\n"; return 93; }
        }
        // A ligature needs the letters shaped together; the per-character pieces are shaped one by one.
        const char16_t* ligatureFonts[] = {u"Times New Roman", u"Cambria", u"Georgia", u"Palatino Linotype", u"Calibri"};
        bool ligatureTested = false;
        for (const auto* family : ligatureFonts) {
            if (!catalog.contains(family)) continue;
            Text serif;
            serif.setPrimaryFamily(family);
            if (serif.shape(u"fi", 24).glyphs.size() != 1) continue;  // this face has no fi ligature
            ShapedMeasure serifMeasure(serif);
            const std::u16string word = u"office fi";
            const auto wordRun = serifMeasure.measure(word, 24);
            auto stretched = justifyLine(serifMeasure, word, 24, wordRun, wordRun.width + 30.f, isoduo::JustifyMethod::perCharacter);
            if (!stretched) return 94;
            for (const auto& piece : stretched->segments)
                if (serif.shape(word.substr(piece.begin, piece.end - piece.begin), 24).glyphs.size() > piece.end - piece.begin) return 95;
            if (stretched->segments.size() < 8) return 96;
            // The line shaped with ligatures off has a cluster per letter (the natural run has fewer: ffi, fi), so the
            // pieces come from one run and keep the font's kerning.
            if (serifMeasure.measureApart(word, 24, isoduo::FontStyle{}).stops.size() != word.size() + 1 || wordRun.stops.size() >= word.size() + 1) return 97;
            ligatureTested = true;
            break;
        }
        if (!ligatureTested) std::cout << "justify ligature check: N/A (no installed face with a fi ligature)\n";
    }
    return 0;
}
