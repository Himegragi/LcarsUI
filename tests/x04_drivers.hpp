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
inline bool blankOnly(std::u16string_view text) {
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
            const auto page = state.semanticPage(ref.id);
            if (!ref.item && node->role != lcars::Role::dialog && node->role != lcars::Role::tooltip)
                result.push_back({page, state.layoutHandle(ref.id)});
            if (node->role == lcars::Role::dialog) self(self, ref);
        }
    };
    visit(visit, {});
    // Drivers compare independently constructed fixtures by index; keep that order stable
    // even when the semantic tree's reading order changes with the arranged page.
    const auto& controls = state.controls();
    auto ordinal = [&](const Entry& entry) {
        auto id = state.layoutId(entry.handle);
        return std::find_if(controls.begin(), controls.end(), [&](const auto& c) { return id && c.id == *id; }) - controls.begin();
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) { return ordinal(a) < ordinal(b); });
    return result;
}
inline std::vector<Entry> inventory(editForm::Form& form) { return inventory(form.ui); }
inline std::vector<std::string> pages(editForm::Form& form) {
    std::vector<std::string> result;
    auto tabs = form.handles.tabs.describe();
    auto children = form.ui.semantics().children({tabs->id});
    if (children) for (const auto& ref : *children) if (ref.item) result.push_back(ref.item->id);
    return result;
}
inline std::optional<lcars::ComponentSnapshot> describe(const lcars::ComponentHandle& value) {
    return std::visit([](const auto& handle) -> std::optional<lcars::ComponentSnapshot> {
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
inline std::optional<bool> enclosingScroll(const lcars::layout::Node& node, lcars::detail::UiState& ui,
                                             std::string_view id, bool inside = false) {
    inside = inside || node.kind == lcars::layout::Node::Kind::scroll;
    if (node.kind == lcars::layout::Node::Kind::component) {
        auto actual = ui.layoutId(node.component);
        if (actual && *actual == id) return inside;
    }
    for (const auto& child : node.children)
        if (auto found = enclosingScroll(child, ui, id, inside)) return found;
    return std::nullopt;
}
inline std::optional<bool> enclosingScroll(editForm::Form& form, const lcars::ComponentSnapshot& semantic) {
    return enclosingScroll(form.page(), lcars::detail::access(form.ui),
                            semantic.target.empty() ? semantic.id : semantic.target);
}
inline bool insideClient(const lcars::Placement& placement, int width, int height) {
    lcars::Rect client{0, 0, width, height};
    return placement.bounds.w > 0 && placement.bounds.h > 0 &&
        lcars::intersect(placement.bounds, client) == placement.bounds &&
        lcars::intersect(placement.bounds, placement.clip) == placement.bounds;
}
// Passive labels and progress have no user operation. All other page members retry a
// typed operation; admission must reject it before examining the proposed value.
inline bool hiddenRetry(const lcars::ComponentHandle& value) {
    return std::visit([](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        using namespace lcars;
        if constexpr (std::is_same_v<T, TextFieldHandle>) return h.setValue(u"blocked").status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, ButtonHandle>) return h.invoke().status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, CheckboxHandle>) return h.toggle().status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, RadioGroupHandle> || std::is_same_v<T, DropdownHandle> || std::is_same_v<T, ListHandle>)
            return h.select({"blocked"}).status == Status::hidden && h.focus().status == Status::hidden;
        else if constexpr (std::is_same_v<T, SliderHandle>) return h.setValue(0).status == Status::hidden && h.focus().status == Status::hidden;
        else return true;
    }, value);
}
inline bool normalSemantics(const lcars::ComponentSnapshot& s) {
    using lcars::ComponentKind;
    if (s.kind == ComponentKind::checkbox) return s.checked && s.value == (*s.checked ? u"true" : u"false");
    if (s.kind == ComponentKind::radioGroup || s.kind == ComponentKind::dropdown || s.kind == ComponentKind::list || s.kind == ComponentKind::tabs) {
        auto count = std::count_if(s.items.begin(), s.items.end(), [](const auto& option) { return option.selected; });
        if (count > 1) return false;
        if (count == 1) return std::any_of(s.items.begin(), s.items.end(), [&](const auto& o) { return o.selected && o.label == s.value; });
        return s.value.empty();
    }
    return true;
}
inline std::vector<std::u16string> normalText(const lcars::ComponentSnapshot& s) {
    using lcars::ComponentKind;
    std::vector<std::u16string> result;
    auto add = [&](std::u16string text) {
        text.erase(std::remove(text.begin(), text.end(), u'\n'), text.end());
        if (!text.empty()) result.push_back(std::move(text));
    };
    if (s.kind == ComponentKind::label) add(s.value);
    else {
        add(s.name);
        if (s.kind != ComponentKind::button && s.kind != ComponentKind::checkbox) add(s.value);
        if (s.kind == ComponentKind::textField) add(s.description);
    }
    return result;
}
inline bool baselineContext(editForm::Form& form) {
    form.scroll.offset = 0; // save a zero offset for the page being left
    if (!form.handles.tabs.activate({"v01"})) return false;
    form.scroll.offset = 0;
    return bool(form.handles.tabs.focus());
}
inline bool setEnabled(const lcars::ComponentHandle& value, bool enabled) {
    return std::visit([&](const auto& h) {
        if constexpr (requires { h.enable(enabled); }) return bool(h.enable(enabled));
        else return false;
    }, value);
}
inline bool focusApplicable(const lcars::ComponentSnapshot& semantic) {
    return semantic.enabled && semantic.focusable;
}
inline bool focus(lcars::Ui& ui, const lcars::ComponentHandle& value) {
    auto id = lcars::detail::access(ui).layoutId(value);
    return id && bool(ui.semantics().perform({*id}, lcars::Verb::focus));
}
inline bool disabledRetry(const lcars::ComponentHandle& value) {
    return std::visit([](const auto& h) {
        using T = std::decay_t<decltype(h)>; using namespace lcars;
        if constexpr (std::is_same_v<T, TextFieldHandle>) return h.setValue(u"blocked").status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, ButtonHandle>) return h.invoke().status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, CheckboxHandle>) return h.toggle().status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, RadioGroupHandle> || std::is_same_v<T, DropdownHandle> || std::is_same_v<T, ListHandle>)
            return h.select({"blocked"}).status == Status::disabled && h.focus().status == Status::disabled;
        else if constexpr (std::is_same_v<T, SliderHandle>) return h.setValue(0).status == Status::disabled && h.focus().status == Status::disabled;
        else return false;
    }, value);
}
// Exercise a selected value using each component's public operation. The demo reset
// actions restore each page's initial model after the observation.
inline bool driveSelected(const Entry& entry) {
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
inline bool selectedSemantics(const lcars::ComponentSnapshot& semantic) {
    using namespace lcars;
    if (semantic.kind == ComponentKind::checkbox) return semantic.checked == true;
    return !semantic.value.empty() && std::count_if(semantic.items.begin(), semantic.items.end(),
        [&](const auto& option) { return option.selected && option.label == semantic.value; }) == 1;
}
inline bool sameSelection(const lcars::ComponentSnapshot& a, const lcars::ComponentSnapshot& b) {
    if (a.checked != b.checked || a.items.size() != b.items.size()) return false;
    for (std::size_t i = 0; i < a.items.size(); ++i)
        if (a.items[i].value != b.items[i].value || a.items[i].label != b.items[i].label ||
            a.items[i].enabled != b.items[i].enabled || a.items[i].selected != b.items[i].selected ||
            a.items[i].invalid != b.items[i].invalid) return false;
    return true;
}
inline std::vector<lcars::ComponentSnapshot> snapshot(editForm::Form& form) {
    std::vector<lcars::ComponentSnapshot> result;
    for (const auto& entry : inventory(form)) if (auto s = describe(entry.handle)) result.push_back(*s);
    return result;
}
inline bool sameModel(const std::vector<lcars::ComponentSnapshot>& a, const std::vector<lcars::ComponentSnapshot>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].value != b[i].value || a[i].enabled != b[i].enabled ||
            a[i].focused != b[i].focused || a[i].candidate != b[i].candidate || a[i].expanded != b[i].expanded ||
            a[i].readOnly != b[i].readOnly || a[i].invalid != b[i].invalid || a[i].error != b[i].error ||
            a[i].sourceConflict != b[i].sourceConflict || !sameSelection(a[i], b[i])) return false;
    return true;
}
inline bool restoreSelected(editForm::Form& form, const Entry& entry, const lcars::ComponentSnapshot& before, const lcars::PresentationSettings& presentation) {
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
    return restored && bool(form.ui.setPresentation(presentation)) && baselineContext(form);
}
// Applicability follows the application-owned connection, not component IDs.
inline bool conflictInapplicable(const editForm::Form& form, const Entry& entry) {
    auto field = std::get_if<lcars::TextFieldHandle>(&entry.handle);
    if (field) return (form.list && form.list->searchConnection().owns(*field)) || *field == form.handles.preview;
    auto semantic = describe(entry.handle);
    return semantic && (semantic->kind == lcars::ComponentKind::slider || semantic->kind == lcars::ComponentKind::checkbox) &&
        !lcars::detail::access(form.ui).confirmationGroupContains(form.handles.apply.describe()->id, semantic->id);
}
inline bool groupConflictSupported(editForm::Form& form, const lcars::ComponentSnapshot& s) {
    using lcars::ComponentKind;
    bool typeSupported = s.kind == ComponentKind::checkbox || s.kind == ComponentKind::radioGroup || s.kind == ComponentKind::dropdown || s.kind == ComponentKind::slider;
    return typeSupported && lcars::detail::access(form.ui).confirmationGroupContains(form.handles.apply.describe()->id, s.id);
}
inline bool driveGroupConflict(editForm::Form& form, const Entry& entry) {
    using namespace lcars;
    return std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, CheckboxHandle>) {
            form.rejectNextApplyWithNoticesSourceChange();
            return bool(h.toggle()) && form.handles.apply.invoke().status == Status::invalidValue && h.describe()->sourceConflict;
        } else if constexpr (std::is_same_v<T, RadioGroupHandle>) {
            if (!h.select({"focus"})) return false;
            form.updateModeSource({"offline"}); return h.describe()->sourceConflict;
        } else if constexpr (std::is_same_v<T, DropdownHandle>) {
            if (!h.select({"central"})) return false;
            form.updateRegionSource({"south"}); return h.describe()->sourceConflict;
        } else if constexpr (std::is_same_v<T, SliderHandle>) {
            if (!h.setValue(55)) return false;
            form.updateVolumeSource(70); return h.describe()->sourceConflict;
        } else return false;
    }, entry.handle);
}
inline lcars::ButtonHandle latestFor(editForm::Form& form, const Entry& entry) {
    using namespace lcars;
    return std::visit([&](const auto& h) -> ButtonHandle {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, CheckboxHandle>) return form.handles.noticesLatest;
        else if constexpr (std::is_same_v<T, RadioGroupHandle>) return form.handles.modeLatest;
        else if constexpr (std::is_same_v<T, DropdownHandle>) return form.handles.regionLatest;
        else if constexpr (std::is_same_v<T, SliderHandle>) return form.handles.volumeLatest;
        else return {};
    }, entry.handle);
}
inline bool executionSupported(editForm::Form&, const lcars::ButtonHandle& button) {
    auto semantic = button.describe();
    return semantic && semantic->kind == lcars::ComponentKind::button;
}
inline bool observeExecution(editForm::Form& form, const lcars::ButtonHandle& button,
                              std::function<lcars::Result()> observer) {
    if (button.describe()->id == form.handles.apply.describe()->id) {
        // The confirmation action remains protected; observe its actual modification entry.
        if (button.bind([] { return lcars::Result{}; }).status != lcars::Status::invalidValue) return false;
        form.observeNextApply([observer = std::move(observer)] { (void)observer(); });
        return true;
    }
    return bool(button.bind(std::move(observer)));
}
inline bool prepareExecution(editForm::Form& form, const Entry& entry) {
    auto* button = std::get_if<lcars::ButtonHandle>(&entry.handle);
    if (!button || !baselineContext(form)) return false;
    if (entry.page == "modal") {
        if (!form.handles.tabs.activate({"v03"}) || !form.handles.dialogOpen.invoke()) return false;
    } else if (!form.handles.tabs.activate({std::string(entry.page)})) return false;
    return bool(button->enable(true)) && bool(button->focus());
}
inline bool restoreExecution(editForm::Form& form, const Entry& entry, bool enabled) {
    auto* button = std::get_if<lcars::ButtonHandle>(&entry.handle);
    if (!button) return false;
    bool isApply = button->describe()->id == form.handles.apply.describe()->id;
    bool restored = (isApply ? bool(form.handles.reset.invoke()) : bool(form.restoreFixtureAction(*button))) && bool(button->enable(enabled));
    if (entry.page == "modal")
        restored = bool(form.handles.dialog.settle(false)) && bool(form.handles.resetV03.invoke()) && restored;
    return baselineContext(form) && restored;
}
// Give the active page enough content to exercise both edges at every viewport size.
// The model fixture temporarily expands an existing label and restores its original
// source after each component. The target itself is never used as the padding label.
inline bool prepareClipped(editForm::Form& form, const Entry& entry) {
    if (entry.page == "shared" || entry.page == "modal" || !form.handles.tabs.activate({std::string(entry.page)})) return false;
    auto target = describe(entry.handle);
    std::u16string padding;
    for (int i = 0; i < 60; ++i) padding += u"X04 padding\n";
    lcars::LabelHandle filler;
    if (entry.page == "v01") filler = target->id == form.handles.intro.describe()->id ? form.handles.title : form.handles.intro;
    else if (entry.page == "v02") filler = target->id == form.list->handles.title.describe()->id ? form.list->handles.detail : form.list->handles.title;
    else if (entry.page == "v03") filler = target->id == form.handles.v03Title.describe()->id ? form.handles.dialogResult : form.handles.v03Title;
    else filler = form.handles.v04Title;
    if (!form.setClipPadding(filler, std::move(padding))) return false;
    if (target->kind == lcars::ComponentKind::label && target->labelKind == lcars::LabelKind::error && !target->target.empty())
        return bool(form.handles.name.setError(u"X04 clipped error"));
    return true;
}
inline bool restoreClipped(editForm::Form& form, const Entry& entry) {
    auto target = describe(entry.handle);
    form.clearClipPadding();
    if (target && target->kind == lcars::ComponentKind::label && target->labelKind == lcars::LabelKind::error && !target->target.empty())
        if (!form.handles.name.setError(u"")) return false;
    form.scroll.offset = 0;
    return bool(form.handles.tabs.activate({"v01"}));
}
inline const lcars::Placement* clippedOwner(const lcars::Frame& frame, const lcars::ComponentSnapshot& s) {
    auto id = s.target.empty() ? s.id : s.target;
    auto it = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& item) { return item.id == id; });
    return it == frame.items.end() ? nullptr : &*it;
}
inline std::optional<lcars::Rect> clippedRect(const lcars::Frame& frame, const lcars::ComponentSnapshot& s) {
    auto* owner = clippedOwner(frame, s); if (!owner) return std::nullopt;
    if (s.target.empty()) return owner->bounds;
    const std::vector<lcars::Line>* lines = nullptr;
    if (s.labelKind == lcars::LabelKind::name) lines = &owner->labelLines;
    else if (s.labelKind == lcars::LabelKind::description) lines = &owner->descriptionLines;
    else if (s.labelKind == lcars::LabelKind::error) lines = &owner->errorLines;
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
inline std::vector<int> clippedOffsets(const lcars::Frame& frame, lcars::Rect target) {
    auto viewport = frame.viewport;
    int half = std::max(1, std::min(target.h, viewport.h) / 2);
    auto clamp = [&](int value) { return std::clamp(value, 0, frame.maximum); };
    std::vector<int> result{0, frame.maximum,
        clamp(target.y - (viewport.y + viewport.h) + half),
        clamp(target.y - viewport.y + target.h - half)};
    std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
inline const std::u16string errorMessage = u"X04 field error";
inline bool errorSupported(const lcars::ComponentSnapshot& s) {
    using namespace lcars;
    return (s.kind == ComponentKind::label && s.labelKind == LabelKind::error && !s.target.empty()) ||
        s.kind == ComponentKind::textField || s.kind == ComponentKind::checkbox || s.kind == ComponentKind::radioGroup ||
        s.kind == ComponentKind::dropdown || s.kind == ComponentKind::slider || s.kind == ComponentKind::tabs;
}
inline bool driveError(editForm::Form& form, const Entry& entry) {
    using namespace lcars;
    if (entry.page != "shared" && !form.handles.tabs.activate({std::string(entry.page)})) return false;
    return std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, LabelHandle> || std::is_same_v<T, TabsHandle>) return bool(form.handles.name.setError(errorMessage));
        else if constexpr (std::is_same_v<T, TextFieldHandle>) return bool(h.setError(errorMessage));
        else if constexpr (std::is_same_v<T, CheckboxHandle> || std::is_same_v<T, RadioGroupHandle> ||
                           std::is_same_v<T, DropdownHandle> || std::is_same_v<T, SliderHandle>) {
            return bool(detail::access(form.ui).setError(h.describe()->id, errorMessage));
        } else return false;
    }, entry.handle);
}
inline bool errorSemantics(editForm::Form& form, const lcars::ComponentSnapshot& s) {
    using namespace lcars;
    if (s.kind == ComponentKind::tabs) return !s.items.empty() && s.items[0].invalid;
    if (s.kind == ComponentKind::label) {
        auto target = form.handles.name.describe();
        return s.labelKind == LabelKind::error && s.value == errorMessage && s.target == target->id && target->invalid &&
            target->error == errorMessage && std::find(target->labels.begin(), target->labels.end(), s.id) != target->labels.end();
    }
    if (s.kind == ComponentKind::textField && s.id == form.handles.name.describe()->id) {
        auto label = form.handles.nameError.describe();
        return s.invalid && s.error == errorMessage && label->labelKind == LabelKind::error && label->target == s.id &&
            label->value == s.error && std::find(s.labels.begin(), s.labels.end(), label->id) != s.labels.end();
    }
    return s.invalid && s.error == errorMessage;
}
inline bool restoreError(editForm::Form& form, const Entry& entry) {
    using namespace lcars;
    bool restored = std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, LabelHandle> || std::is_same_v<T, TabsHandle>) return bool(form.handles.name.setError(u""));
        else if constexpr (std::is_same_v<T, TextFieldHandle>) return bool(h.setError(u""));
        else if constexpr (std::is_same_v<T, CheckboxHandle> || std::is_same_v<T, RadioGroupHandle> ||
                           std::is_same_v<T, DropdownHandle> || std::is_same_v<T, SliderHandle>) return bool(detail::access(form.ui).setError(h.describe()->id, u""));
        else return false;
    }, entry.handle);
    return restored && baselineContext(form);
}
inline bool prepareCandidate(editForm::Form& form, const Entry& entry) {
    using namespace lcars;
    if (entry.page != "shared" && !form.handles.tabs.activate({std::string(entry.page)})) return false;
    return std::visit([](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, DropdownHandle>) return bool(h.focus());
        else if constexpr (std::is_same_v<T, TabsHandle> || std::is_same_v<T, ListHandle>) return bool(h.focus());
        else return false;
    }, entry.handle);
}
inline bool openCandidate(const Entry& entry) {
    if (const auto* dropdown = std::get_if<lcars::DropdownHandle>(&entry.handle)) return bool(dropdown->open());
    return true;
}
inline const lcars::Placement* candidatePlacement(const lcars::Frame& frame, const lcars::ComponentSnapshot& s) {
    if (s.kind == lcars::ComponentKind::dropdown) {
        auto it = std::find_if(frame.overlays.begin(), frame.overlays.end(), [&](const auto& p) { return p.id == s.id && p.popup && !p.tooltip; });
        return it == frame.overlays.end() ? nullptr : &*it;
    }
    auto it = std::find_if(frame.items.begin(), frame.items.end(), [&](const auto& p) { return p.id == s.id; });
    return it == frame.items.end() ? nullptr : &*it;
}
inline std::optional<std::u16string> candidateLabel(const lcars::ComponentSnapshot& s) {
    if (!s.candidate) return std::nullopt;
    auto it = std::find_if(s.items.begin(), s.items.end(), [&](const auto& o) { return o.value == *s.candidate && o.enabled; });
    return it == s.items.end() ? std::nullopt : std::optional<std::u16string>{it->label};
}
inline bool restoreCandidate(editForm::Form& form, const Entry& entry) {
    using namespace lcars;
    bool restored = std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, DropdownHandle>) return bool(h.cancel());
        else if constexpr (std::is_same_v<T, TabsHandle>) return bool(h.activate({"v01"}));
        else if constexpr (std::is_same_v<T, ListHandle>) return bool(form.list->handles.zero.invoke()) && bool(form.list->handles.twenty.invoke());
        else return false;
    }, entry.handle);
    return restored && baselineContext(form);
}
inline bool driveSearchReadOnly(editForm::Form& form) {
    return bool(form.handles.tabs.activate({"v02"})) && bool(form.list->handles.search.setValue(u"X04")) &&
           bool(form.list->handles.search.readOnly(true));
}
inline bool restoreSearchReadOnly(editForm::Form& form) {
    return bool(form.list->handles.search.readOnly(false)) && bool(form.list->handles.search.setValue(u"")) &&
           bool(form.handles.tabs.activate({"v01"}));
}
inline bool openModal(editForm::Form& form) {
    return bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialogOpen.invoke());
}
inline bool closeModal(editForm::Form& form) { return bool(form.handles.dialog.settle(false)); }
inline bool prepareTip(editForm::Form& form) {
    return bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.progressHalf.focus());
}
struct TextStateDriver {
    const char* name;
    lcars::Result (*drive)(editForm::Form&);
    lcars::Result (*restore)(editForm::Form&);
};
inline const TextStateDriver nameReadOnly{
    "read-only",
    [](editForm::Form& form) { return form.handles.readOnly.invoke(); },
    [](editForm::Form& form) { return form.handles.normal.invoke(); }
};
inline const TextStateDriver nameConflict{
    "source conflict",
    [](editForm::Form& form) {
        auto result = form.handles.name.setValue(u"Draft");
        if (!result) return result;
        form.updateSource(u"Latest");
        return lcars::Result{};
    },
    [](editForm::Form& form) { return form.handles.latest.invoke(); }
};
}

