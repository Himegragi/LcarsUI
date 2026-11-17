#pragma once
#include <span>
#include <string_view>

namespace isoduo {
struct ThirdPartyNotice {
    std::u16string_view name, version, license, source, text;
};
std::span<const ThirdPartyNotice> thirdPartyNotices() noexcept;
}
