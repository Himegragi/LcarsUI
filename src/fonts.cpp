#include "lcars/fonts.hpp"
#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <algorithm>

namespace lcars {
using Microsoft::WRL::ComPtr;

bool FontCatalog::contains(std::u16string_view key) const noexcept {
    return std::any_of(families_.begin(), families_.end(), [&](const auto& family) { return family.key == key; });
}

const FontCatalog& system_font_catalog() {
    static const FontCatalog catalog = [] {
        FontCatalog result;
        ComPtr<IDWriteFactory6> factory;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6),
                                       reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return result;
        ComPtr<IDWriteFontSet1> set;
        if (FAILED(factory->GetSystemFontSet(FALSE, &set))) return result;
        for (UINT32 i = 0; i < set->GetFontCount(); ++i) {
            ComPtr<IDWriteFontFaceReference> face;
            if (FAILED(set->GetFontFaceReference(i, &face)) || face->GetLocality() != DWRITE_LOCALITY_LOCAL) continue;
            BOOL exists = FALSE;
            ComPtr<IDWriteLocalizedStrings> names;
            if (FAILED(set->GetPropertyValues(i, DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME, &exists, &names)) || !exists) continue;
            UINT32 index = 0;
            BOOL found = FALSE;
            names->FindLocaleName(L"en-us", &index, &found);
            if (!found) index = 0;
            UINT32 length = 0;
            if (FAILED(names->GetStringLength(index, &length))) continue;
            std::wstring name(length + 1, L'\0');
            if (FAILED(names->GetString(index, name.data(), length + 1))) continue;
            name.resize(length);
            std::u16string key(name.begin(), name.end());
            if (!result.contains(key)) result.families_.push_back({key, key});
        }
        return result;
    }();
    return catalog;
}
}
