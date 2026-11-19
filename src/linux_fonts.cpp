#include "font_files.hpp"
#include "isoduo/fonts.hpp"
#include "utf.hpp"
#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <memory>

namespace isoduo {
namespace {
using Pattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;
using FontSet = std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)>;
using ObjectSet = std::unique_ptr<FcObjectSet, decltype(&FcObjectSetDestroy)>;

std::u16string stringOf(FcPattern* pattern, const char* property) {
    FcChar8* value = nullptr;
    if (FcPatternGetString(pattern, property, 0, &value) != FcResultMatch) return {};
    return detail::utf16FromUtf8(reinterpret_cast<char*>(value));
}
detail::FontFile matchFile(std::u16string_view family, bool bold, bool italic) {
    auto name = detail::utf8FromUtf16(family);
    const bool generic = family.starts_with(u"sans-serif:") || family == u"sans-serif" || family == u"emoji";
    Pattern request(generic ? FcNameParse(reinterpret_cast<const FcChar8*>(name.c_str())) : FcPatternCreate(),
        FcPatternDestroy);
    if (!request) return {};
    if (!generic) FcPatternAddString(request.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(name.c_str()));
    FcPatternAddBool(request.get(), FC_SCALABLE, FcTrue);
    FcPatternAddInteger(request.get(), FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(request.get(), FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcConfigSubstitute(nullptr, request.get(), FcMatchPattern);
    FcDefaultSubstitute(request.get());
    FcResult result;
    Pattern match(FcFontMatch(nullptr, request.get(), &result), FcPatternDestroy);
    if (!match || (!generic && stringOf(match.get(), FC_FAMILY) != family)) return {};
    int weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN, index = 0;
    FcPatternGetInteger(match.get(), FC_WEIGHT, 0, &weight);
    FcPatternGetInteger(match.get(), FC_SLANT, 0, &slant);
    if ((bold && weight < FC_WEIGHT_DEMIBOLD) || (italic && slant == FC_SLANT_ROMAN)) return {};
    FcPatternGetInteger(match.get(), FC_INDEX, 0, &index);
    FcChar8* file = nullptr;
    if (FcPatternGetString(match.get(), FC_FILE, 0, &file) != FcResultMatch) return {};
    return {reinterpret_cast<const char*>(file), static_cast<unsigned>(index), {}};
}
}

bool FontCatalog::contains(std::u16string_view key) const noexcept {
    return std::any_of(families_.begin(), families_.end(), [key](const auto& family) { return family.key == key; });
}
FontResolution FontCatalog::resolve(const PresentationSettings& settings) const {
    FontResolution result;
    result.requested = settings.fontFamily;
    result.requestedAvailable = result.requested.empty() || contains(result.requested);
    auto append = [&](std::u16string name) {
        if (name.empty() || name == result.primary || std::find(result.fallback.begin(), result.fallback.end(), name) != result.fallback.end()) return;
        if (result.primary.empty()) result.primary = std::move(name);
        else result.fallback.push_back(std::move(name));
    };
    if (result.requestedAvailable) append(result.requested);
    for (auto family : {"sans-serif", "sans-serif:lang=zh-tw", "sans-serif:lang=ja", "sans-serif:lang=ko", "emoji"}) {
        Pattern request(FcNameParse(reinterpret_cast<const FcChar8*>(family)), FcPatternDestroy);
        if (!request) continue;
        FcConfigSubstitute(nullptr, request.get(), FcMatchPattern);
        FcDefaultSubstitute(request.get());
        FcResult status;
        Pattern matched(FcFontMatch(nullptr, request.get(), &status), FcPatternDestroy);
        if (matched) append(stringOf(matched.get(), FC_FAMILY));
    }
    return result;
}
const FontCatalog& systemFontCatalog() {
    static const auto catalog = [] {
        FontCatalog fonts;
        Pattern pattern(FcPatternCreate(), FcPatternDestroy);
        ObjectSet objects(FcObjectSetBuild(FC_FAMILY, nullptr), FcObjectSetDestroy);
        if (!pattern || !objects) return fonts;
        FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);
        FontSet set(FcFontList(nullptr, pattern.get(), objects.get()), FcFontSetDestroy);
        if (!set) return fonts;
        for (int i = 0; i < set->nfont; ++i) {
            auto family = stringOf(set->fonts[i], FC_FAMILY);
            if (!family.empty()) fonts.families_.push_back({family, family});
        }
        auto& families = fonts.families_;
        std::sort(families.begin(), families.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
        families.erase(std::unique(families.begin(), families.end(), [](const auto& a, const auto& b) {
            return a.key == b.key;
        }), families.end());
        return fonts;
    }();
    return catalog;
}

namespace detail {
long systemFontCatalogError() { return FcInit() ? 0 : -1; }
FontFile systemFontFile(std::u16string_view family) { return matchFile(family, false, false); }
FontFile systemStyledFontFile(std::u16string_view family, bool bold, bool italic) { return matchFile(family, bold, italic); }
}
}
