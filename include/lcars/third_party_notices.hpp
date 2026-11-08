#pragma once
#include <span>
#include <string_view>

namespace lcars {
struct ThirdPartyNotice {
    std::u16string_view name, version, license, source, text;
};
std::span<const ThirdPartyNotice> third_party_notices() noexcept;
}
