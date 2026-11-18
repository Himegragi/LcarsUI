#pragma once
#include "rich_text.hpp"

namespace test {
inline bool canonicalRichRoundTrip(std::string_view payload, const isoduo::RichText& expected) {
    return payload.starts_with("Isoduo.RichText.1\n") &&
           isoduo::detail::parseRichPayload(payload) == expected;
}
}
