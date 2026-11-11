#pragma once
// State transitions used by both the assembled core and native X04 checks.
// Each driver starts with a fresh Form and restores its application model afterwards.
#include "edit_form.hpp"
#include "core_internal.hpp"
#include <algorithm>

namespace x04 {
// Text a painter drew that has no ink by nature: spaces and ideographic spaces only. A justified or distributed line draws a
// full-width space as a piece of its own (code review 2026-11-12, item 11), and a reader that wants every drawn character
// must not drop it for lacking ink.
inline bool blank_only(std::u16string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char16_t c) { return c == u' ' || c == 0x3000; });
}
// Enumerate the semantic tree; the existing typed drivers still exercise each component.
struct Entry { std::string page; lcars::ComponentHandle handle; };
inline std::vector<Entry> inventory(lcars::Ui& ui) {
    std::vector<Entry> result;
    auto& state = lcars::detail::access(ui);
    auto visit = [&](auto&& self, const lcars::NodeRef& parent) -> void {
        auto children = ui.semantics().children(parent);
        if (!children) return;
        for (const auto& ref : *children) {
            auto node = ui.semantics().get(ref);
            if (!node) continue;
            const auto page = state.semantic_page(ref.id);
            if (!ref.item && node->role != lcars::Role::dialog && node->role != lcars::Role::tooltip)
                result.push_back({page, state.layout_handle(ref.id)});
            if (node->role == lcars::Role::dialog) self(self, ref);
        }
    };
    visit(visit, {});
    // Drivers compare independently constructed fixtures by index; keep that order stable
    // even when the semantic tree's reading order changes with the arranged page.
    const auto& controls = state.controls();
    auto ordinal = [&](const Entry& entry) {
        auto id = state.layout_id(entry.handle);
        return std::find_if(controls.begin(), controls.end(), [&](const auto& c) { return id && c.id == *id; }) - controls.begin();
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) { return ordinal(a) < ordinal(b); });
    return result;
}
inline std::vector<Entry> inventory(edit_form::Form& form) { return inventory(form.ui); }
inline std::vector<std::string> pages(edit_form::Form& form) {
    std::vector<std::string> result;
    auto tabs = form.handles.tabs.describe();
    auto children = form.ui.semantics().children({tabs->id});
    if (children) for (const auto& ref : *children) if (ref.item) result.push_back(ref.item->id);
    return result;
}
inline std::optional<lcars::Semantics> describe(const lcars::ComponentHandle& value) {
    return std::visit([](const auto& handle) -> std::optional<lcars::Semantics> {
        using T = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
        else return handle.describe().value;
    }, value);
}
inline const lcars::Placement* placement(const lcars::Frame& frame, const lcars::ComponentHandle& value) {
    return std::visit([&](const auto& handle) -> const lcars::Placement* {
        using T = std::decay_t<decltype(handle)>;
        if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
        else return frame.find(handle);
    }, value);
}
// Determine clipping applicability from the assembled layout tree, including modal
// content even when closed. Associated labels use their field's placement.
inline std::optional<bool> enclosing_scroll(const lcars::layout::Node& node, lcars::detail::UiState& ui,
                                             std::string_view id, bool inside = false) {
    inside = inside || node.type == lcars::layout::Node::Type::scroll;
    if (node.type == lcars::layout::Node::Type::control) {
        auto actual = ui.layout_id(node.component);
        if (actual && *actual == id) return inside;
    }
    for (const auto& child : node.children)
        if (auto found = enclosing_scroll(child, ui, id, inside)) return found;
    return std::nullopt;
}
inline std::optional<bool> enclosing_scroll(edit_form::Form& form, const lcars::Semantics& semantic) {
    return enclosing_scroll(form.page(), lcars::detail::access(form.ui),
                            semantic.target.empty() ? semantic.id : semantic.target);
}
inline bool inside_client(const lcars::Placement& placement, int width, int height) {
    lcars::Rect client{0, 0, width, height};
    return placement.bounds.w > 0 && placement.bounds.h > 0 &&
        lcars::intersect(placement.bounds, client) == placement.bounds &&
        lcars::intersect(placement.bounds, placement.clip) == placement.bounds;
}
// Passive labels and progress have no user operation. All other page members retry a
// typed operation; admission must reject it before examining the proposed value.
inline bool hidden_retry(const lcars::ComponentHandle& value) {
    return std::visit([](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        using namespace lcars;
        if constexpr (std::is_same_v<T, TextHandle>) return h.set_value(u"blocked").status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, ButtonHandle>) return h.invoke().status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, CheckboxHandle>) return h.toggle().status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, RadioGroupHandle> || std::is_same_v<T, DropdownHandle> || std::is_same_v<T, ListHandle>)
            return h.select({"blocked"}).status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, SliderHandle>) return h.set_value(0).status == Status::hidden && h.focus().status == Status::hidden;
        else return true;
    }, value);
}
inline bool normal_semantics(const lcars::Semantics& s) {
    using lcars::Kind;
    if (s.kind == Kind::checkbox) return s.checked && s.value == (*s.checked ? u"true" : u"false");
    if (s.kind == Kind::radio_group || s.kind == Kind::dropdown || s.kind == Kind::list || s.kind == Kind::tabs) {
        auto count = std::count_if(s.options.begin(), s.options.end(), [](const auto& option) { return option.selected; });
        if (count > 1) return false;
        if (count == 1) return std::any_of(s.options.begin(), s.options.end(), [&](const auto& o) { return o.selected && o.label == s.value; });
        return s.value.empty();
    }
    return true;
}
inline std::vector<std::u16string> normal_text(const lcars::Semantics& s) {
    using lcars::Kind;
    std::vector<std::u16string> result;
    auto add = [&](std::u16string text) {
        text.erase(std::remove(text.begin(), text.end(), u'\n'), text.end());
        if (!text.empty()) result.push_back(std::move(text));
    };
    if (s.kind == Kind::label) add(s.value);
    else {
        add(s.name);
        if (s.kind != Kind::button && s.kind != Kind::checkbox) add(s.value);
        if (s.kind == Kind::text) add(s.description);
    }
    return result;
}
inline bool baseline_context(edit_form::Form& form) {
    form.scroll.offset = 0; // save a zero offset for the page being left
    if (!form.handles.tabs.activate({"v01"})) return false;
    form.scroll.offset = 0;
    return bool(form.handles.tabs.focus());
}
inline bool set_enabled(const lcars::ComponentHandle& value, bool enabled) {
    return std::visit([&](const auto& h) {
        if constexpr (requires { h.enable(enabled); }) return bool(h.enable(enabled));
        else return false;
    }, value);
}
inline bool focus_applicable(const lcars::Semantics& semantic) {
    return semantic.enabled && semantic.focusable;
}
inline bool focus(lcars::Ui& ui, const lcars::ComponentHandle& value) {
    auto id = lcars::detail::access(ui).layout_id(value);
    return id && bool(ui.semantics().perform({*id}, lcars::Verb::focus));
}
inline bool disabled_retry(const lcars::ComponentHandle& value) {
    return std::visit([](const auto& h) {
        using T = std::decay_t<decltype(h)>; using namespace lcars;
        if constexpr (std::is_same_v<T, TextHandle>) return h.set_value(u"blocked").status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, ButtonHandle>) return h.invoke().status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, CheckboxHandle>) return h.toggle().status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, RadioGroupHandle> || std::is_same_v<T, DropdownHandle> || std::is_same_v<T, ListHandle>)
            return h.select({"blocked"}).status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, SliderHandle>) return h.set_value(0).status == Status::disabled && h.focus().status == Status::disabled;
        else return false;
    }, value);
}
// Exercise a selected value using each component's public operation. The demo reset
// actions restore each page's initial model after the observation.
inline bool drive_selected(const Entry& entry) {
    using namespace lcars;
    return std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, CheckboxHandle>) {
            if (*h.draft()) { if (!h.toggle()) return false; }
            return bool(h.toggle());
        } else if constexpr (std::is_same_v<T, RadioGroupHandle>) return bool(h.select({"focus"}));
        else if constexpr (std::is_same_v<T, ListHandle>) return bool(h.select({"T001"}));
        else if constexpr (std::is_same_v<T, TabsHandle>) return bool(h.activate({"v02"}));
        else return false;
    }, entry.handle);
}
inline bool selected_semantics(const lcars::Semantics& semantic) {
    using namespace lcars;
    if (semantic.kind == Kind::checkbox) return semantic.checked == true;
    return !semantic.value.empty() && std::count_if(semantic.options.begin(), semantic.options.end(),
        [&](const auto& option) { return option.selected && option.label == semantic.value; }) == 1;
}
inline bool same_selection(const lcars::Semantics& a, const lcars::Semantics& b) {
    if (a.checked != b.checked || a.options.size() != b.options.size()) return false;
    for (std::size_t i = 0; i < a.options.size(); ++i)
        if (a.options[i].value != b.options[i].value || a.options[i].label != b.options[i].label ||
            a.options[i].enabled != b.options[i].enabled || a.options[i].selected != b.options[i].selected ||
            a.options[i].invalid != b.options[i].invalid) return false;
    return true;
}
inline std::vector<lcars::Semantics> snapshot(edit_form::Form& form) {
    std::vector<lcars::Semantics> result;
    for (const auto& entry : inventory(form)) if (auto s = describe(entry.handle)) result.push_back(*s);
    return result;
}
inline bool same_model(const std::vector<lcars::Semantics>& a, const std::vector<lcars::Semantics>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].value != b[i].value || a[i].enabled != b[i].enabled ||
            a[i].focused != b[i].focused || a[i].candidate != b[i].candidate || a[i].expanded != b[i].expanded ||
            a[i].read_only != b[i].read_only || a[i].invalid != b[i].invalid || a[i].error != b[i].error ||
            a[i].source_conflict != b[i].source_conflict || !same_selection(a[i], b[i])) return false;
    return true;
}
inline bool restore_selected(edit_form::Form& form, const Entry& entry, const lcars::Semantics& before, const lcars::PresentationSettings& presentation) {
    using namespace lcars;
    bool restored = std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, CheckboxHandle>) {
            auto current = h.draft();
            return current && before.checked && (*current == *before.checked || bool(h.toggle()));
        } else if constexpr (std::is_same_v<T, RadioGroupHandle>) return bool(form.handles.reset.invoke());
        else if constexpr (std::is_same_v<T, ListHandle>) return bool(form.list->handles.zero.invoke()) && bool(form.list->handles.twenty.invoke());
        else if constexpr (std::is_same_v<T, TabsHandle>) return bool(h.activate({"v01"}));
        else return false;
    }, entry.handle);
    return restored && bool(form.ui.set_presentation(presentation)) && baseline_context(form);
}
// Applicability follows the application-owned connection, not component IDs.
inline bool conflict_inapplicable(const edit_form::Form& form, const Entry& entry) {
    auto field = std::get_if<lcars::TextHandle>(&entry.handle);
    if (field) return (form.list && form.list->search_connection().owns(*field)) || *field == form.handles.preview;
    auto semantic = describe(entry.handle);
    return semantic && (semantic->kind == lcars::Kind::slider || semantic->kind == lcars::Kind::checkbox) &&
        !lcars::detail::access(form.ui).confirmation_group_contains(form.handles.apply.describe()->id, semantic->id);
}
inline bool group_conflict_supported(edit_form::Form& form, const lcars::Semantics& s) {
    using lcars::Kind;
    bool type_supported = s.kind == Kind::checkbox || s.kind == Kind::radio_group || s.kind == Kind::dropdown || s.kind == Kind::slider;
    return type_supported && lcars::detail::access(form.ui).confirmation_group_contains(form.handles.apply.describe()->id, s.id);
}
inline bool drive_group_conflict(edit_form::Form& form, const Entry& entry) {
    using namespace lcars;
    return std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, CheckboxHandle>) {
            form.reject_next_apply_with_notices_source_change();
            return bool(h.toggle()) && form.handles.apply.invoke().status == Status::invalid_value && h.describe()->source_conflict;
        } else if constexpr (std::is_same_v<T, RadioGroupHandle>) {
            if (!h.select({"focus"})) return false;
            form.update_mode_source({"offline"}); return h.describe()->source_conflict;
        } else if constexpr (std::is_same_v<T, DropdownHandle>) {
            if (!h.select({"central"})) return false;
            form.update_region_source({"south"}); return h.describe()->source_conflict;
        } else if constexpr (std::is_same_v<T, SliderHandle>) {
            if (!h.set_value(55)) return false;
            form.update_volume_source(70); return h.describe()->source_conflict;
        } else return false;
    }, entry.handle);
}
inline lcars::ButtonHandle latest_for(edit_form::Form& form, const Entry& entry) {
    using namespace lcars;
    return std::visit([&](const auto& h) -> ButtonHandle {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, CheckboxHandle>) return form.handles.notices_latest;
        else if constexpr (std::is_same_v<T, RadioGroupHandle>) return form.handles.mode_latest;
        else if constexpr (std::is_same_v<T, DropdownHandle>) return form.handles.region_latest;
        else if constexpr (std::is_same_v<T, SliderHandle>) return form.handles.volume_latest;
        else return {};
    }, entry.handle);
}
inline bool execution_supported(edit_form::Form&, const lcars::ButtonHandle& button) {
    auto semantic = button.describe();
    return semantic && semantic->kind == lcars::Kind::button;
}
inline bool observe_execution(edit_form::Form& form, const lcars::ButtonHandle& button,
                              std::function<lcars::Result()> observer) {
    if (button.describe()->id == form.handles.apply.describe()->id) {
        // The confirmation action remains protected; observe its actual modification entry.
        if (button.bind([] { return lcars::Result{}; }).status != lcars::Status::invalid_value) return false;
        form.observe_next_apply([observer = std::move(observer)] { (void)observer(); });
        return true;
    }
    return bool(button.bind(std::move(observer)));
}
inline bool prepare_execution(edit_form::Form& form, const Entry& entry) {
    auto* button = std::get_if<lcars::ButtonHandle>(&entry.handle);
    if (!button || !baseline_context(form)) return false;
    if (entry.page == "modal") {
        if (!form.handles.tabs.activate({"v03"}) || !form.handles.dialog_open.invoke()) return false;
    } else if (!form.handles.tabs.activate({std::string(entry.page)})) return false;
    return bool(button->enable(true)) && bool(button->focus());
}
inline bool restore_execution(edit_form::Form& form, const Entry& entry, bool enabled) {
    auto* button = std::get_if<lcars::ButtonHandle>(&entry.handle);
    if (!button) return false;
    bool is_apply = button->describe()->id == form.handles.apply.describe()->id;
    bool restored = (is_apply ? bool(form.handles.reset.invoke()) : bool(form.restore_fixture_action(*button))) && bool(button->enable(enabled));
    if (entry.page == "modal")
        restored = bool(form.handles.dialog.settle(false)) && bool(form.handles.reset_v03.invoke()) && restored;
    return baseline_context(form) && restored;
}
// Give the active page enough content to exercise both edges at every viewport size.
// The model fixture temporarily expands an existing label and restores its original
// source after each component. The target itself is never used as the padding label.
inline bool prepare_clipped(edit_form::Form& form, const Entry& entry) {
    if (entry.page == "shared" || entry.page == "modal" || !form.handles.tabs.activate({std::string(entry.page)})) return false;
    auto target = describe(entry.handle);
    std::u16string padding;
    for (int i = 0; i < 60; ++i) padding += u"X04 padding\n";
    lcars::LabelHandle filler;
    if (entry.page == "v01") filler = target->id == form.handles.intro.describe()->id ? form.handles.title : form.handles.intro;
    else if (entry.page == "v02") filler = target->id == form.list->handles.title.describe()->id ? form.list->handles.detail : form.list->handles.title;
    else if (entry.page == "v03") filler = target->id == form.handles.v03_title.describe()->id ? form.handles.dialog_result : form.handles.v03_title;
    else filler = form.handles.v04_title;
    if (!form.set_clip_padding(filler, std::move(padding))) return false;
    if (target->kind == lcars::Kind::label && target->role == lcars::TextRole::error && !target->target.empty())
        return bool(form.handles.name.set_error(u"X04 clipped error"));
    return true;
}
inline bool restore_clipped(edit_form::Form& form, const Entry& entry) {
    auto target = describe(entry.handle);
    form.clear_clip_padding();
    if (target && target->kind == lcars::Kind::label && target->role == lcars::TextRole::error && !target->target.empty())
        if (!form.handles.name.set_error(u"")) return false;
    form.scroll.offset = 0;
    return bool(form.handles.tabs.activate({"v01"}));
}
inline const lcars::Placement* clipped_owner(const lcars::Frame& frame, const lcars::Semantics& s) {
    auto id = s.target.empty() ? s.id : s.target;
    auto it = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& item) { return item.id == id; });
    return it == frame.items.end() ? nullptr : &*it;
}
inline std::optional<lcars::Rect> clipped_rect(const lcars::Frame& frame, const lcars::Semantics& s) {
    auto* owner = clipped_owner(frame, s); if (!owner) return std::nullopt;
    if (s.target.empty()) return owner->bounds;
    const std::vector<lcars::Line>* lines = nullptr;
    if (s.role == lcars::TextRole::name) lines = &owner->label_lines;
    else if (s.role == lcars::TextRole::description) lines = &owner->description_lines;
    else if (s.role == lcars::TextRole::error) lines = &owner->error_lines;
    if (!lines || lines->empty()) return std::nullopt;
    auto r = lines->front().bounds;
    for (const auto& line : *lines) {
        int right = std::max(r.x + r.w, line.bounds.x + line.bounds.w);
        int bottom = std::max(r.y + r.h, line.bounds.y + line.bounds.h);
        r.x = std::min(r.x, line.bounds.x); r.y = std::min(r.y, line.bounds.y);
        r.w = right - r.x; r.h = bottom - r.y;
    }
    return r;
}
inline std::vector<int> clipped_offsets(const lcars::Frame& frame, lcars::Rect target) {
    auto viewport = frame.viewport;
    int half = std::max(1, std::min(target.h, viewport.h) / 2);
    auto clamp = [&](int value) { return std::clamp(value, 0, frame.maximum); };
    std::vector<int> result{0, frame.maximum,
        clamp(target.y - (viewport.y + viewport.h) + half),
        clamp(target.y - viewport.y + target.h - half)};
    std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
inline const std::u16string error_message = u"X04 field error";
inline bool error_supported(const lcars::Semantics& s) {
    using namespace lcars;
    return (s.kind == Kind::label && s.role == TextRole::error && !s.target.empty()) ||
        s.kind == Kind::text || s.kind == Kind::checkbox || s.kind == Kind::radio_group ||
        s.kind == Kind::dropdown || s.kind == Kind::slider || s.kind == Kind::tabs;
}
inline bool drive_error(edit_form::Form& form, const Entry& entry) {
    using namespace lcars;
    if (entry.page != "shared" && !form.handles.tabs.activate({std::string(entry.page)})) return false;
    return std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, LabelHandle> || std::is_same_v<T, TabsHandle>) return bool(form.handles.name.set_error(error_message));
        else if constexpr (std::is_same_v<T, TextHandle>) return bool(h.set_error(error_message));
        else if constexpr (std::is_same_v<T, CheckboxHandle> || std::is_same_v<T, RadioGroupHandle> ||
                           std::is_same_v<T, DropdownHandle> || std::is_same_v<T, SliderHandle>) {
            return bool(detail::access(form.ui).set_error(h.describe()->id, error_message));
        } else return false;
    }, entry.handle);
}
inline bool error_semantics(edit_form::Form& form, const lcars::Semantics& s) {
    using namespace lcars;
    if (s.kind == Kind::tabs) return !s.options.empty() && s.options[0].invalid;
    if (s.kind == Kind::label) {
        auto target = form.handles.name.describe();
        return s.role == TextRole::error && s.value == error_message && s.target == target->id && target->invalid &&
            target->error == error_message && std::find(target->labels.begin(), target->labels.end(), s.id) != target->labels.end();
    }
    if (s.kind == Kind::text && s.id == form.handles.name.describe()->id) {
        auto label = form.handles.name_error.describe();
        return s.invalid && s.error == error_message && label->role == TextRole::error && label->target == s.id &&
            label->value == s.error && std::find(s.labels.begin(), s.labels.end(), label->id) != s.labels.end();
    }
    return s.invalid && s.error == error_message;
}
inline bool restore_error(edit_form::Form& form, const Entry& entry) {
    using namespace lcars;
    bool restored = std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, LabelHandle> || std::is_same_v<T, TabsHandle>) return bool(form.handles.name.set_error(u""));
        else if constexpr (std::is_same_v<T, TextHandle>) return bool(h.set_error(u""));
        else if constexpr (std::is_same_v<T, CheckboxHandle> || std::is_same_v<T, RadioGroupHandle> ||
                           std::is_same_v<T, DropdownHandle> || std::is_same_v<T, SliderHandle>) return bool(detail::access(form.ui).set_error(h.describe()->id, u""));
        else return false;
    }, entry.handle);
    return restored && baseline_context(form);
}
inline bool prepare_candidate(edit_form::Form& form, const Entry& entry) {
    using namespace lcars;
    if (entry.page != "shared" && !form.handles.tabs.activate({std::string(entry.page)})) return false;
    return std::visit([](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, DropdownHandle>) return bool(h.focus());
        else if constexpr (std::is_same_v<T, TabsHandle> || std::is_same_v<T, ListHandle>) return bool(h.focus());
        else return false;
    }, entry.handle);
}
inline bool open_candidate(const Entry& entry) {
    if (const auto* dropdown = std::get_if<lcars::DropdownHandle>(&entry.handle)) return bool(dropdown->open());
    return true;
}
inline const lcars::Placement* candidate_placement(const lcars::Frame& frame, const lcars::Semantics& s) {
    if (s.kind == lcars::Kind::dropdown) {
        auto it = std::find_if(frame.overlays.begin(), frame.overlays.end(), [&](const auto& p) { return p.id == s.id && p.popup && !p.tooltip; });
        return it == frame.overlays.end() ? nullptr : &*it;
    }
    auto it = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& p) { return p.id == s.id; });
    return it == frame.items.end() ? nullptr : &*it;
}
inline std::optional<std::u16string> candidate_label(const lcars::Semantics& s) {
    if (!s.candidate) return std::nullopt;
    auto it = std::find_if(s.options.begin(), s.options.end(), [&](const auto& o) { return o.value == *s.candidate && o.enabled; });
    return it == s.options.end() ? std::nullopt : std::optional<std::u16string>{it->label};
}
inline bool restore_candidate(edit_form::Form& form, const Entry& entry) {
    using namespace lcars;
    bool restored = std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, DropdownHandle>) return bool(h.cancel());
        else if constexpr (std::is_same_v<T, TabsHandle>) return bool(h.activate({"v01"}));
        else if constexpr (std::is_same_v<T, ListHandle>) return bool(form.list->handles.zero.invoke()) && bool(form.list->handles.twenty.invoke());
        else return false;
    }, entry.handle);
    return restored && baseline_context(form);
}
inline bool drive_search_read_only(edit_form::Form& form) {
    return bool(form.handles.tabs.activate({"v02"})) && bool(form.list->handles.search.set_value(u"X04")) &&
           bool(form.list->handles.search.read_only(true));
}
inline bool restore_search_read_only(edit_form::Form& form) {
    return bool(form.list->handles.search.read_only(false)) && bool(form.list->handles.search.set_value(u"")) &&
           bool(form.handles.tabs.activate({"v01"}));
}
inline bool open_modal(edit_form::Form& form) {
    return bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialog_open.invoke());
}
inline bool close_modal(edit_form::Form& form) { return bool(form.handles.dialog.settle(false)); }
inline bool prepare_tip(edit_form::Form& form) {
    return bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.progress_half.focus());
}
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

