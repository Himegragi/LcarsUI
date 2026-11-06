#pragma once
// State transitions used by both the assembled core and native X04 checks.
// Each driver starts with a fresh Form and restores its application model afterwards.
#include "edit_form.hpp"

namespace x04 {
struct TextStateDriver {
    const char* name;
    lcars::Result (*drive)(edit_form::Form&);
    lcars::Result (*restore)(edit_form::Form&);
};
inline const TextStateDriver name_read_only{
    "read-only",
    [](edit_form::Form& form) { return form.handles.read_only.invoke(); },
    [](edit_form::Form& form) { return form.handles.normal.invoke(); }
};
inline const TextStateDriver name_conflict{
    "source conflict",
    [](edit_form::Form& form) {
        auto result = form.handles.name.set_value(u"Draft");
        if (!result) return result;
        form.update_source(u"Latest");
        return lcars::Result{};
    },
    [](edit_form::Form& form) { return form.handles.latest.invoke(); }
};
}
