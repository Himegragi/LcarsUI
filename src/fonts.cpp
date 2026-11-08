#include "lcars/fonts.hpp"
#include "font_files.hpp"
#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <algorithm>

namespace lcars {
using Microsoft::WRL::ComPtr;

bool FontCatalog::contains(std::u16string_view key) const noexcept {
    return std::any_of(families_.begin(), families_.end(), [&](const auto& family) { return family.key == key; });
}

namespace detail {
FontFile system_font_file(std::u16string_view family) {
    ComPtr<IDWriteFactory6> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6),
                                   reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return {};
    ComPtr<IDWriteFontSet1> set;
    if (FAILED(factory->GetSystemFontSet(FALSE, &set))) return {};
    const std::wstring name(family.begin(), family.end());
    DWRITE_FONT_PROPERTY property{DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME, name.c_str(), L"en-us"};
    ComPtr<IDWriteFontSet> matches;
    if (FAILED(static_cast<IDWriteFontSet*>(set.Get())->GetMatchingFonts(&property, 1, &matches)) || !matches->GetFontCount()) return {};
    for (UINT32 i = 0; i < matches->GetFontCount(); ++i) {
        ComPtr<IDWriteFontFaceReference> face;
        if (FAILED(matches->GetFontFaceReference(i, &face)) || face->GetLocality() != DWRITE_LOCALITY_LOCAL) continue;
        ComPtr<IDWriteFontFile> file;
        ComPtr<IDWriteFontFileLoader> loader;
        ComPtr<IDWriteLocalFontFileLoader> local;
        const void* key = nullptr;
        UINT32 key_size = 0, length = 0;
        if (FAILED(face->GetFontFile(&file)) || FAILED(file->GetLoader(&loader)) ||
            FAILED(loader.As(&local)) || FAILED(file->GetReferenceKey(&key, &key_size)) ||
            FAILED(local->GetFilePathLengthFromKey(key, key_size, &length))) continue;
        std::wstring path(length + 1, L'\0');
        if (FAILED(local->GetFilePathFromKey(key, key_size, path.data(), length + 1))) continue;
        path.resize(length);
        return {path, face->GetFontFaceIndex()};
    }
    return {};
}
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
