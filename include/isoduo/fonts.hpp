#pragma once
#include "isoduo/presentation.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace isoduo {
struct FontFamily {
    std::u16string key;
    std::u16string displayName;
};
struct FontResolution {
    std::u16string requested;
    std::u16string primary;
    bool requestedAvailable = true;
    std::vector<std::u16string> fallback;
};

class FontCatalog {
public:
    const std::vector<FontFamily>& families() const noexcept { return families_; }
    bool contains(std::u16string_view key) const noexcept;
    FontResolution resolve(const PresentationSettings& settings) const;
private:
    friend const FontCatalog& systemFontCatalog();
    std::vector<FontFamily> families_;
};
const FontCatalog& systemFontCatalog();
}
