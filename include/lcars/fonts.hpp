#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace lcars {
struct FontFamily {
    std::u16string key;
    std::u16string display_name;
};

class FontCatalog {
public:
    const std::vector<FontFamily>& families() const noexcept { return families_; }
    bool contains(std::u16string_view key) const noexcept;
private:
    friend const FontCatalog& system_font_catalog();
    std::vector<FontFamily> families_;
};
const FontCatalog& system_font_catalog();
}
