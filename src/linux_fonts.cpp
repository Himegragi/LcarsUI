#include "font_files.hpp"
#include "isoduo/fonts.hpp"
#include "utf.hpp"
#include <fontconfig/fontconfig.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>

namespace isoduo {
namespace {
using Config = std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)>;
using Pattern = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;
using FontSet = std::unique_ptr<FcFontSet, decltype(&FcFontSetDestroy)>;

std::string_view stringOf(FcPattern* pattern, const char* property, int index = 0) {
    FcChar8* value = nullptr;
    if (FcPatternGetString(pattern, property, index, &value) != FcResultMatch) return {};
    return reinterpret_cast<const char*>(value);
}

int integerOf(FcPattern* pattern, const char* property, int fallback = 0) {
    int value = fallback;
    FcPatternGetInteger(pattern, property, 0, &value);
    return value;
}

bool boolOf(FcPattern* pattern, const char* property) {
    FcBool value = FcFalse;
    FcPatternGetBool(pattern, property, 0, &value);
    return value == FcTrue;
}

std::u16string familyKey(FcPattern* pattern) {
    for (int i = 0; ; ++i) {
        auto lang = stringOf(pattern, FC_FAMILYLANG, i);
        if (lang.empty()) break;
        if (lang == "en") return detail::utf16FromUtf8(stringOf(pattern, FC_FAMILY, i));
    }
    return detail::utf16FromUtf8(stringOf(pattern, FC_FAMILY));
}

std::u16string displayName(FcPattern* pattern, const std::u16string& key) {
    auto languages = FcGetDefaultLangs();
    for (int i = 0; ; ++i) {
        auto lang = stringOf(pattern, FC_FAMILYLANG, i);
        if (lang.empty()) break;
        if (languages && FcStrSetMember(languages, reinterpret_cast<const FcChar8*>(lang.data())))
            return detail::utf16FromUtf8(stringOf(pattern, FC_FAMILY, i));
    }
    return key;
}

bool selectable(FcPattern* pattern) {
    FcCharSet* characters = nullptr;
    if (boolOf(pattern, FC_SYMBOL) || FcPatternGetCharSet(pattern, FC_CHARSET, 0, &characters) != FcResultMatch)
        return false;
    for (auto [first, last] : {std::pair{'0', '9'}, {'A', 'Z'}, {'a', 'z'}})
        for (auto cp = first; cp <= last; ++cp)
            if (!FcCharSetHasChar(characters, cp)) return false;
    return true;
}

struct FaceChoice {
    detail::FontFile file;
    int score = 0;

    bool prefer(int rank, const detail::FontFile& candidate) const {
        return file.path.empty() || std::tie(rank, candidate.path, candidate.faceIndex) <
                                    std::tie(score, file.path, file.faceIndex);
    }
};

struct Family {
    std::array<FaceChoice, 4> faces;
    std::u16string display;
};

struct FontSnapshot {
    std::map<std::u16string, Family, std::less<>> families;
    std::vector<std::u16string> order;
    std::u16string emoji;
    long error = -1;
};

FontSet orderedFonts(FcConfig* config, const char* name) {
    Pattern request(FcNameParse(reinterpret_cast<const FcChar8*>(name)), FcPatternDestroy);
    if (!request) return {nullptr, FcFontSetDestroy};
    FcPatternAddBool(request.get(), FC_SCALABLE, FcTrue);
    FcConfigSubstitute(config, request.get(), FcMatchPattern);
    FcDefaultSubstitute(request.get());
    FcResult result;
    return {FcFontSort(config, request.get(), FcFalse, nullptr, &result), FcFontSetDestroy};
}

const FontSnapshot& snapshot() {
    static const auto value = [] {
        FontSnapshot out;
        Config config(FcInitLoadConfigAndFonts(), FcConfigDestroy);
        if (!config) return out;
        auto all = FcConfigGetFonts(config.get(), FcSetSystem);
        if (!all) return out;
        out.error = 0;
        for (int i = 0; i < all->nfont; ++i) {
            auto pattern = all->fonts[i];
            auto path = stringOf(pattern, FC_FILE);
            if (path.empty() || !boolOf(pattern, FC_SCALABLE)) continue;
            auto key = familyKey(pattern);
            if (key.empty()) continue;
            auto& family = out.families[key];
            const detail::FontFile file{path, static_cast<unsigned>(integerOf(pattern, FC_INDEX)), {}};
            const int weight = integerOf(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
            const int width = integerOf(pattern, FC_WIDTH, FC_WIDTH_NORMAL);
            const bool italic = integerOf(pattern, FC_SLANT, FC_SLANT_ROMAN) != FC_SLANT_ROMAN;
            const int regularScore = std::abs(weight - FC_WEIGHT_REGULAR) +
                1000 * std::abs(width - FC_WIDTH_NORMAL) + (italic ? 10000 : 0);
            if (family.faces[0].prefer(regularScore, file)) {
                family.faces[0] = {file, regularScore};
                family.display = selectable(pattern) ? displayName(pattern, key) : std::u16string{};
            }

            const bool bold = weight >= FC_WEIGHT_DEMIBOLD;
            const unsigned style = (bold ? 1 : 0) | (italic ? 2 : 0);
            const int styleScore = std::abs(weight - (bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR)) +
                1000 * std::abs(width - FC_WIDTH_NORMAL);
            if (style && family.faces[style].prefer(styleScore, file))
                family.faces[style] = {file, styleScore};
        }
        std::set<std::u16string> added;
        auto append = [&](const std::u16string& family) {
            if (out.families.contains(family) && added.insert(family).second) out.order.push_back(family);
        };
        // Keep the desktop sans and CJK preferences ahead of the general Fontconfig fallback list.
        auto firstUsable = [&](const FontSet& matches) {
            if (matches) {
                for (int i = 0; i < matches->nfont; ++i) {
                    auto key = familyKey(matches->fonts[i]);
                    if (out.families.contains(key)) return key;
                }
            }
            return std::u16string{};
        };
        auto defaults = orderedFonts(config.get(), "sans-serif");
        append(firstUsable(defaults));
        for (auto request : {"sans-serif:lang=zh-tw", "sans-serif:lang=zh-cn", "sans-serif:lang=ja", "sans-serif:lang=ko"}) {
            auto matches = orderedFonts(config.get(), request);
            append(firstUsable(matches));
        }
        if (auto matches = orderedFonts(config.get(), "emoji"); matches && matches->nfont) {
            auto face = matches->fonts[0];
            FcCharSet* characters = nullptr;
            if (FcPatternGetCharSet(face, FC_CHARSET, 0, &characters) == FcResultMatch &&
                FcCharSetHasChar(characters, 0x1f600)) out.emoji = familyKey(face);
            append(out.emoji);
        }
        if (defaults)
            for (int i = 0; i < defaults->nfont; ++i) append(familyKey(defaults->fonts[i]));
        return out;
    }();
    return value;
}
}

bool FontCatalog::contains(std::u16string_view key) const noexcept {
    return std::ranges::binary_search(families_, key, std::less<>{}, &FontFamily::key);
}

FontResolution FontCatalog::resolve(const PresentationSettings& settings) const {
    FontResolution result;
    result.requested = settings.fontFamily;
    result.requestedAvailable = result.requested.empty() || contains(result.requested);
    const auto& order = snapshot().order;
    if (result.requestedAvailable && !result.requested.empty()) result.primary = result.requested;
    else if (!order.empty()) result.primary = order.front();
    for (const auto& family : order)
        if (family != result.primary) result.fallback.push_back(family);
    return result;
}

const FontCatalog& systemFontCatalog() {
    static const auto catalog = [] {
        FontCatalog fonts;
        for (const auto& [key, family] : snapshot().families)
            if (!family.display.empty()) fonts.families_.push_back({key, family.display});
        return fonts;
    }();
    return catalog;
}

namespace detail {
long systemFontCatalogError() { return snapshot().error; }

FontFile systemFontFile(std::u16string_view family) { return systemStyledFontFile(family, false, false); }

FontFile systemStyledFontFile(std::u16string_view family, bool bold, bool italic) {
    const auto& families = snapshot().families;
    auto found = families.find(family);
    if (found == families.end()) return {};
    const unsigned slot = (bold ? 1 : 0) | (italic ? 2 : 0);
    return found->second.faces[slot].file;
}

std::u16string_view systemEmojiFamily() { return snapshot().emoji; }
}
}
