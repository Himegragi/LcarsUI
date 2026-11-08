#pragma once
#include "lcars/core.hpp"
inline bool renderer_failure_preserves_draft(const lcars::TextHandle& field, std::u16string_view expected) {
    return field.set_value(u"unexpected change").status == lcars::Status::renderer_failed &&
        field.draft().value == expected;
}
