#pragma once
#include <codecvt>
#include <locale>
#include <string>
#include <string_view>
namespace lcars::detail {
inline std::u16string utf16_from_utf8(std::string_view value) {
    if (value.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conversion;
    return conversion.from_bytes(value.data(), value.data() + value.size());
}
inline std::string utf8_from_utf16(std::u16string_view value) {
    if (value.empty()) return {};
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conversion;
    return conversion.to_bytes(value.data(), value.data() + value.size());
}
}
