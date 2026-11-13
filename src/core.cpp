#include "core_internal.hpp"
#include "component_type.hpp"
#include "utf.hpp"
#include "rich_text.hpp"
#include <stdexcept>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace lcars {
namespace {
#ifndef NDEBUG
void output_diagnostic(const std::string& message) {
#ifdef _WIN32
    auto utf16 = detail::utf16_from_utf8(message);
    std::wstring wide(utf16.begin(), utf16.end()); OutputDebugStringW(wide.c_str());
#else
    std::fputs(message.c_str(), stderr);
#endif
}
#endif
// Committed text from typing or an IME: control characters (Enter, Tab, Ctrl+letter) never insert.
std::u16string without_controls(std::u16string_view text) {
    std::u16string result;
    for (auto ch : text) if (ch >= 0x20 && ch != 0x7f) result += ch;
    return result;
}
}
namespace {
bool valid_text(std::u16string_view text, std::size_t max_length, bool allow_line_feed) {
    if (text.size() > max_length) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        if ((ch < 0x20 && !(allow_line_feed && ch == u'\n')) || ch == 0x7f) return false;
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (ch >= 0xdc00 && ch <= 0xdfff) return false;
    }
    return true;
}
// The value rule for one field: its kind decides whether a line feed is content.
bool valid_value(const Control& control, std::u16string_view text) { return valid_text(text, control.max_length, control.multiline); }
// A multi-line box is at least one line tall; the bound only keeps layout arithmetic sane.
constexpr unsigned max_visible_lines = 1000;
bool valid_visible_lines(const Control& control) { return !control.multiline || (control.visible_lines >= 1 && control.visible_lines <= max_visible_lines); }
// What a type says about itself; a Kind no type is registered for says nothing.
bool supports_text_flow(Kind kind) { const auto* type = detail::find_component_type(kind); return type && type->supports_text_flow(); }
bool valid_text_align(TextAlign align) {
    return align == TextAlign::left || align == TextAlign::center || align == TextAlign::right || align == TextAlign::justify || align == TextAlign::distribute;
}
bool valid_justify_method(JustifyMethod method) { return method == JustifyMethod::automatic || method == JustifyMethod::spaces || method == JustifyMethod::per_character; }
// Distributed alignment (the last line stretched too) is for labels: a field being typed in would respread its only line at every key.
bool admits_text_align(Kind kind, TextAlign align) {
    const auto* type = detail::find_component_type(kind);
    return align != TextAlign::distribute || (type && type->admits_distribute());
}
bool valid_text_flow(const Control& control) {
    if (!valid_text_align(control.text_align) || !valid_justify_method(control.justify_method) || !valid_line_spacing(control.line_spacing) || !valid_paragraph_spacing(control.paragraph_spacing)) return false;
    if (!admits_text_align(control.kind, control.text_align)) return false;
    return supports_text_flow(control.kind) || (control.text_align == TextAlign::left && control.justify_method == JustifyMethod::automatic && control.line_spacing == 1.0 && control.paragraph_spacing == 0);
}
// A rich field is a multi-line text field; the option exists for no other control.
bool valid_rich_option(const Control& control) {
    const auto* type = detail::find_component_type(control.kind);
    return !control.rich || (type && type->supports_rich_text() && control.multiline);
}
}
bool detail::valid_text_control(const Control& control) {
    return valid_value(control, control.value) && valid_visible_lines(control) && valid_rich_option(control);
}
bool valid_single_line(std::u16string_view text, std::size_t max_length) { return valid_text(text, max_length, false); }
bool valid_multi_line(std::u16string_view text, std::size_t max_length) { return valid_text(text, max_length, true); }
void EditSession::select(unsigned anchor, unsigned caret) {
    if (composing) cancel_composition();
    auto clamp = [&](unsigned pos) {
        pos = std::min(pos, static_cast<unsigned>(text.size()));
        if (pos && pos < text.size() && text[pos] >= 0xdc00 && text[pos] <= 0xdfff &&
            text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff) --pos;
        return pos;
    };
    selection = {clamp(anchor), clamp(caret)};
    caret_upstream = false;
    ++selection_changes;
}
void EditSession::start_composition() {
    if (composing) return;
    composing = true; preedit.clear(); preedit_caret = 0;
}
void EditSession::update_composition(std::u16string value, unsigned caret) {
    start_composition(); preedit = std::move(value);
    preedit_caret = std::min(caret, static_cast<unsigned>(preedit.size()));
}
void EditSession::cancel_composition() { composing = false; preedit.clear(); preedit_caret = 0; }
std::u16string EditSession::visible() const {
    auto value = text;
    if (composing) value.replace(begin(), end() - begin(), preedit);
    return value;
}
namespace {
// A handle query that reads one member of the handle's own description: the description's failure, or the member.
template <class Value> Query<Value> semantic_property(const Query<Semantics>& semantics, Value Semantics::*member) {
    if (!semantics) return {semantics.status, semantics.message, std::nullopt};
    return {Status::ok, {}, (*semantics).*member};
}
}
detail::UiState::OperationScope::OperationScope(UiState* state)
    : state_(state && std::this_thread::get_id() == state->owner_ ? state : nullptr) {
    if (state_ && ++state_->operation_depth_ == 1 && !state_->semantic_subscribers_.empty() &&
        !state_->semantic_inspecting_ && !state_->committing_removals_) {
        state_->semantic_inspecting_ = true;
        state_->semantic_before_ = state_->semantic_snapshot();
        state_->semantic_inspecting_ = false;
    }
}
detail::UiState::OperationScope::~OperationScope() {
    if (state_ && --state_->operation_depth_ == 0 && !state_->committing_removals_) {
        state_->commit_removals();
        if (!state_->semantic_subscribers_.empty() && !state_->semantic_inspecting_) {
            state_->semantic_inspecting_ = true;
            state_->semantic_diff();
            state_->semantic_inspecting_ = false;
        }
    }
}
// The write paths the checkbox, radio group, dropdown and slider share (#94); the text field uses connect_field_write too.
template <class Connection>
Result detail::UiState::connect_field_write(Connection& field, std::function<Result(const typename Connection::Change&)> write, bool immediate) {
    if (!field.accepts_write(immediate) || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    (immediate ? field.immediate : field.confirmed) = std::move(write);
    return {};
}
// The handle's confirm(): hand the draft to the confirmed write; a refusal shows its message on the field and focuses it.
template <class Handle> Result detail::UiState::confirm_field(const Handle& handle) {
    auto& f = fields_of<Handle>().at(handle.id_);
    if (!f.confirmed) return {Status::invalid_value, u"No confirmed write is connected"};
    if (f.submitting || dispatching_ || invoking_) return {Status::busy, u"Operation is already executing"};
    typename std::decay_t<decltype(f)>::Change change{handle, f.value, f.origin}; f.submitting = true; invoking_ = true;
    Result result;
    try { result = f.confirmed(change); }
    catch (...) { f.submitting = false; invoking_ = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false; invoking_ = false;
    if (result) { f.origin = change.value; set_error(handle.id_, u""); }
    else { set_error(handle.id_, result.message); focus(handle.id_); }
    auto notification = notify(); return notification ? result : notification;
}
// An edit of the value: an immediate write runs now (unless one is already running); otherwise the change is only announced.
template <class FieldType> Result detail::UiState::write_immediately(const Id& id, FieldType& field, const typename FieldType::Change& change) {
    if (!field.immediate || field.submitting) return notify();
    field.submitting = true;
    Result result;
    try { result = field.immediate(change); } catch (...) { field.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    field.submitting = false;
    if (result) { field.origin = change.value; set_error(id, u""); }
    else set_error(id, result.message);
    auto notification = notify(); return notification ? result : notification;
}
bool detail::UiState::removal_pending(std::string_view id) const {
    return retiring_ == id || std::find(pending_removals_.begin(), pending_removals_.end(), id) != pending_removals_.end();
}
void detail::UiState::commit_removals() {
    if (pending_removals_.empty()) return;
    committing_removals_ = true;
    while (!pending_removals_.empty()) {
        auto id = std::move(pending_removals_.back());
        pending_removals_.pop_back();
        if (!find(id)) continue;
        retiring_ = id;
        if (focus_ == id) {
            try { move_focus({}); } catch (...) { stopped_ = true; focus_.clear(); }
        }
        if (menu_ && menu_->target == id) close_menu();
        menu_attachments_.erase(id);
        if (key_armed_ == id) key_armed_.clear();
        if (pointer_armed_ == id) { pointer_armed_.clear(); pointer_option_.reset(); pointer_option_value_.reset(); slider_drag_.reset(); }
        slider_targets_.erase(id); animations_.cancel(slider_animation_key(id));
        if (tooltip_hover_ == id) tooltip_hover_.clear();
        for (auto& [key, dialog] : dialogs_) if (dialog.opener == id) dialog.opener.clear();
        if (auto member = page_members_.find(id); member != page_members_.end()) {
            auto& members = tabs_.at(member->second.first).pages[member->second.second].members;
            std::erase_if(members, [&](const ComponentHandle& handle) { return layout_id(handle) == id; });
            page_members_.erase(member);
        }
        if (auto group = confirmation_members_.find(id); group != confirmation_members_.end()) {
            for (const auto& field : group->second) with_connection(field, [](auto& connection) { connection.confirmation_member = false; });
            confirmation_members_.erase(group);
        }
        if (auto tip = tooltip_ids_.find(id); tip != tooltip_ids_.end()) { overlay_ids_.erase(tip->second); tooltip_ids_.erase(tip); }
        actions_.erase(id); confirmation_buttons_.erase(id); errors_.erase(id); tooltips_.erase(id); diagnosed_names_.erase(id);
        blur_callbacks_.erase(id); providers_.erase(id);
        std::erase_if(controls_, [&](const Control& control) { return control.id == id; });
        semantic_instances_.erase(id);
        button_lifetimes_.erase(id);
        retiring_.clear();
    }
    committing_removals_ = false;
    if (invalidate_) {
        try { invalidate_(); } catch (...) { stopped_ = true; }
    }
    if (!pending_removals_.empty() && operation_depth_ == 0) commit_removals();
}
void detail::UiState::focus_cues_changed() {
    refresh_tooltips();
    if (invalidate_) invalidate_();
}
void detail::UiState::pointer_pressed() {
    if (!keyboard_cues_) return;
    keyboard_cues_ = false;
    focus_cues_changed();
}
void detail::UiState::set_system_keyboard_cues(bool enabled) {
    if (system_cues_ == enabled) return;
    system_cues_ = enabled;
    focus_cues_changed();
}
Result detail::UiState::set_presentation(const PresentationSettings& settings) {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (settings.text_size < min_text_size || settings.text_size > max_text_size ||
        (settings.density != Density::standard && settings.density != Density::compact) ||
        (settings.hover != Hover::on && settings.hover != Hover::off) ||
        (settings.focus_indicator != FocusIndicatorMode::automatic && settings.focus_indicator != FocusIndicatorMode::always &&
         settings.focus_indicator != FocusIndicatorMode::never) ||
        (settings.focus_ring_width && (*settings.focus_ring_width < min_focus_ring_width || *settings.focus_ring_width > max_focus_ring_width)) ||
        (settings.focus_ring_radius && (*settings.focus_ring_radius < min_focus_ring_radius || *settings.focus_ring_radius > max_focus_ring_radius)) ||
        (settings.motion != Motion::automatic && settings.motion != Motion::off))
        return {Status::invalid_value, u"Invalid presentation setting"};
    if (settings.variant.name == "high-contrast") return {Status::not_supported, u"High contrast is a later phase"};
    if (settings.variant.name != "light" && settings.variant.name != "dark") return {Status::not_found, u"Unknown theme variant"};
    if (settings == presentation_) return {};
    auto reflows = presentation_;  // the focus and motion modes alone change no layout or font
    reflows.focus_indicator = settings.focus_indicator;
    reflows.motion = settings.motion;
    const bool reflow = !(reflows == settings);
    presentation_ = settings;
    if (reflow) ++layout_revision_;
    refresh_tooltips();
    if (!motion_enabled()) finish_animations();  // switching motion off lands every running transition at once
    if (reflow && presentation_changed_) presentation_changed_();
    if (invalidate_) invalidate_();
    return {};
}
namespace {
bool is_builtin_appearance(const Appearance& appearance) {
    const auto& list = detail::builtin_appearances();
    return std::find(list.begin(), list.end(), &appearance) != list.end();
}
Palette apply_overrides(Palette palette, const std::vector<PaletteOverride>& overrides, std::string_view appearance, const ThemeVariant& variant) {
    for (const auto& entry : overrides)
        if (entry.appearance == appearance && entry.variant == variant) set_palette_value(palette, entry.role, entry.value);
    return palette;
}
std::u16string ascii16(const std::string& text) { return std::u16string(text.begin(), text.end()); }
}
Palette detail::UiState::effective_palette(const Appearance& appearance, const ThemeVariant& variant) const {
    auto palette = appearance.palette(variant);
    if (!palette_overrides_.empty() && is_builtin_appearance(appearance)) palette = apply_overrides(palette, palette_overrides_, appearance.name(), variant);
    palette.focus = palette.focus_color();  // settled here for every appearance: the ring is the accent unless the palette or an override pinned it
    return palette;
}
Result detail::UiState::set_palette_overrides(std::vector<PaletteOverride> overrides) {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    std::u16string problems;
    auto reject = [&](const std::string& text) { problems += (problems.empty() ? u"" : u"; ") + ascii16(text); };
    std::vector<std::pair<const Appearance*, ThemeVariant>> affected;
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        const auto& entry = overrides[i];
        const auto* appearance = builtin_appearance(entry.appearance);
        if (!appearance) { reject("unknown appearance \"" + entry.appearance + "\""); continue; }
        if (!appearance->supports(entry.variant)) { reject(entry.appearance + " does not support theme variant \"" + entry.variant.name + "\""); continue; }
        Palette scratch = appearance->palette(entry.variant);
        if (!set_palette_value(scratch, entry.role, entry.value)) { reject("invalid palette value for role \"" + entry.role + "\" of " + entry.appearance + " / " + entry.variant.name); continue; }
        bool repeated = false;
        for (std::size_t j = 0; j < i; ++j)
            repeated = repeated || (overrides[j].appearance == entry.appearance && overrides[j].variant == entry.variant && overrides[j].role == entry.role);
        if (repeated) { reject("role \"" + entry.role + "\" of " + entry.appearance + " / " + entry.variant.name + " is overridden twice"); continue; }
        if (std::none_of(affected.begin(), affected.end(), [&](const auto& pair) { return pair.first == appearance && pair.second == entry.variant; }))
            affected.emplace_back(appearance, entry.variant);
    }
    if (problems.empty()) {
        for (const auto& [appearance, variant] : affected) {
            auto failures = check_palette_contrast(apply_overrides(appearance->palette(variant), overrides, appearance->name(), variant));
            for (const auto& failure : failures) {
                char numbers[64];
                std::snprintf(numbers, sizeof numbers, "%.2f:1 (needs %.1f:1)", std::floor(failure.ratio * 100) / 100, failure.required);
                reject(std::string(appearance->name()) + " / " + variant.name + ": " + failure.pair + " contrast " + numbers);
            }
        }
    }
    if (!problems.empty()) return {Status::invalid_value, u"Palette overrides rejected: " + problems};
    palette_overrides_ = std::move(overrides);
    if (invalidate_) invalidate_();
    return {};
}
detail::UiState::UiState(std::vector<Control> controls) : controls_(std::move(controls)) {
    OperationScope operation(this);
    std::map<Id, bool> ids;
    for (const auto& control : controls_) {
        (void)detail::component_type(control.kind);
        if (detail::component_type(control.kind).requires_typed_creation()) throw std::invalid_argument("Component requires typed creation");
        if (control.id.empty() || !ids.emplace(control.id, true).second)
            throw std::invalid_argument("Control IDs must be nonempty and unique");
        if (!valid_text_flow(control)) throw std::invalid_argument("Invalid text alignment or spacing");
        button_lifetimes_.emplace(control.id, std::make_shared<int>(0));
        detail::component_type(control.kind).adopt(*this, control);
    }
    for (const auto& control : controls_)
        if (!control.target.empty() && (!detail::component_type(control.kind).can_target() || !ids.contains(control.target)))
            throw std::invalid_argument("Only labels may target, and only known controls");
}
bool detail::UiState::draws_focus_indicator(const Semantics& semantics) const {
    if (presentation_.focus_indicator == FocusIndicatorMode::never) return false;
    return semantics.focused && (focus_indicator_visible() || component_type(semantics.kind).focus_always_visible(semantics));
}
detail::UiState::~UiState() {
    lifetime_.reset();
    button_lifetimes_.clear();
    for (auto& [id, field] : edits_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : bools_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : radios_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : dropdowns_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : lists_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : numerics_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : progresses_) if (field.disconnect) field.disconnect();
}
ProgressHandle detail::UiState::add_progress(Control control) {
    OperationScope operation(this);
    if (control.kind != Kind::progress || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid progress component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    progresses_.emplace(id, ProgressField{});
    return ProgressHandle(this, lifetime_, std::move(id));
}
Result ProgressHandle::connect_source(Source<int> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"A numeric source requires read and subscribe"};
        auto& field = ui_->progresses_.at(id_);
        if (field.disconnect) field.disconnect();
        field.source = std::move(source);
        auto weak = lifetime_; auto state = ui_;
        field.disconnect = field.source.subscribe([weak, state] { if (!weak.expired() && state->invalidate_) state->invalidate_(); });
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    });
}
Query<Semantics> ProgressHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
TextHandle detail::UiState::add_text(Control control) {
    OperationScope operation(this);
    if (control.kind != Kind::text || control.id.empty() || find(control.id) || !detail::valid_text_control(control))
        throw std::invalid_argument("Invalid text component");
    Field field;
    field.session.text = control.value; field.rich = control.rich;
    Id id = control.id;
    controls_.push_back(std::move(control));
    edits_.emplace(id, std::move(field));
    return TextHandle(this, lifetime_, std::move(id));
}
ButtonHandle detail::UiState::add_button(Control control) {
    OperationScope operation(this);
    if (control.kind != Kind::button || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid button component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    auto token = std::make_shared<int>(0);
    button_lifetimes_.emplace(id, token);
    return ButtonHandle(this, token, std::move(id));
}
LabelHandle detail::UiState::add_label(Control control) {
    OperationScope operation(this);
    if (control.kind != Kind::label || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid label component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    return LabelHandle(this, lifetime_, std::move(id));
}
CheckboxHandle detail::UiState::add_checkbox(Control control) {
    OperationScope operation(this);
    if (control.kind != Kind::checkbox || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid checkbox component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    bools_.emplace(id, BoolField{});
    bind(id, [this, id] { return toggle_checkbox(id); });
    return CheckboxHandle(this, lifetime_, std::move(id));
}
Result CheckboxHandle::toggle() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->toggle_checkbox(id_); });
}
Result CheckboxHandle::use_latest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->bools_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
        f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
    });
}
Result CheckboxHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result CheckboxHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result CheckboxHandle::connect_source(Source<bool> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"A boolean source requires read and subscribe"};
        auto& field = ui_->bools_.at(id_);
        if (field.disconnect) field.disconnect();
        field.source = std::move(source); field.connected = false;
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        field.disconnect = field.source.subscribe([weak, state, id] { if (!weak.expired()) state->bool_source_changed(id); });
        ui_->bool_source_changed(id_); return {};
    });
}
void detail::UiState::bool_source_changed(const Id& id) {
    OperationScope operation(this);
    auto& field = bools_.at(id);
    bool value = field.source.read();
    bool clean = !field.connected || field.value == field.source_value;
    field.source_value = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result CheckboxHandle::connect_immediate(std::function<Result(const Change<CheckboxHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->bools_.at(id_), std::move(write), true);
    });
}
Result CheckboxHandle::connect_confirmed(std::function<Result(const Change<CheckboxHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->bools_.at(id_), std::move(write), false);
    });
}
Result CheckboxHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirm_field(*this);
    });
}
Result detail::UiState::toggle_checkbox(const Id& id) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    auto& field = bools_.at(id);
    if (!field.writable()) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (field.value == field.source_value && !field.submitting) field.origin = field.source_value;
    field.value = !field.value;
    Change<CheckboxHandle> change{CheckboxHandle(this, lifetime_, id), field.value, field.origin};
    return write_immediately(id, field, change);
}
Query<Semantics> CheckboxHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<bool> CheckboxHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<bool> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->bools_.at(id_).value};
    });
}
RadioGroupHandle detail::UiState::add_radio_group(Control control, std::vector<RadioOption> options) {
    OperationScope operation(this);
    if (control.kind != Kind::radio_group || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid radio group");
    std::set<std::string> ids;
    for (const auto& option : options)
        if (option.value.id.empty() || !ids.insert(option.value.id).second) throw std::invalid_argument("Invalid radio option");
    auto id = control.id;
    controls_.push_back(std::move(control));
    RadioField field; field.options = std::move(options); radios_.emplace(id, std::move(field));
    return RadioGroupHandle(this, lifetime_, std::move(id));
}
ListHandle detail::UiState::add_list(Control control, std::vector<RadioOption> items) {
    OperationScope operation(this);
    if (control.kind != Kind::list || control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid list");
    std::set<std::string> ids;
    for (const auto& item : items) if (item.value.id.empty() || !ids.insert(item.value.id).second) throw std::invalid_argument("Invalid list identity");
    auto id = control.id; controls_.push_back(std::move(control));
    ListField field; field.items = std::move(items); lists_.emplace(id, std::move(field));
    return ListHandle(this, lifetime_, std::move(id));
}
Result ListHandle::connect_source(Source<std::optional<OptionValue>> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
        auto& f = ui_->lists_.at(id_); if (f.disconnect) f.disconnect();
        f.source = std::move(source); auto weak = lifetime_; auto* state = ui_;
        f.disconnect = f.source.subscribe([weak, state] { if (!weak.expired() && state->invalidate_) state->invalidate_(); });
        return {};
    });
}
Result ListHandle::connect_immediate(std::function<Result(const Change<ListHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto& f = ui_->lists_.at(id_); if (!f.source.read || !write) return {Status::invalid_value, u"A source and write are required"};
        f.immediate = std::move(write); return {};
    });
}
Result ListHandle::select(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available_option(id_, value); !r) return r;
        auto& f = ui_->lists_.at(id_);
        auto item = std::find_if(f.items.begin(), f.items.end(), [&](const auto& row) { return row.value == value; });
        if (item == f.items.end()) return ui_->find(id_)->read_only ? Result{Status::read_only, u"List is read only"} : Result{Status::invalid_value, u"Invalid list item"};
        if (!item->enabled) return {Status::disabled, u"List item is disabled"};
        if (ui_->find(id_)->read_only) return {Status::read_only, u"List is read only"};
        if (!f.source.read || !f.immediate) return {Status::invalid_value, u"Connect an immediate write"};
        if (ui_->dispatching_ || f.writing) return {Status::busy, u"Operation is already executing"};
        auto origin = f.source.read();
        if (origin == value) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); return {}; }
        f.writing = true; Result r;
        try { r = f.immediate(Change<ListHandle>{*this, value, std::move(origin)}); }
        catch (...) { f.writing = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
        f.writing = false;
        if (r) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); auto n = ui_->notify(); if (!n) return n; } return r;
    });
}
Result ListHandle::focus(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_, true); !result) return result;
        auto& list = ui_->lists_.at(id_);
        auto item = std::find_if(list.items.begin(), list.items.end(), [&](const auto& row) { return row.value == value; });
        if (item == list.items.end()) return {Status::not_found, {}};
        if (auto result = ui_->available_option(id_, value); !result) return result;
        if (!item->enabled) return {Status::disabled, {}};
        if (auto result = ui_->focus(id_); !result) return result;
        list.cursor = std::move(value);
        if (ui_->reveal_requested_) ui_->reveal_requested_(id_);
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    }, detail::UiState::gone_without_text);
}
Result ListHandle::set_items(std::vector<RadioOption> items) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        std::set<std::string> ids;
        for (const auto& item : items) if (item.value.id.empty() || !ids.insert(item.value.id).second) return {Status::invalid_value, u"Invalid list identity"};
        if (ui_->dispatching_) return {Status::busy, u"Operation is already executing"};
        auto& f = ui_->lists_.at(id_);
        if (f.writing) return {Status::busy, u"Operation is already executing"};
        auto exists = [&](const OptionValue& value) {
            return std::any_of(items.begin(), items.end(), [&](const auto& item) { return item.enabled && item.value == value; });
        };
        auto selected = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        if (selected && !exists(*selected)) {
            if (!f.immediate) return {Status::invalid_value, u"Cannot clear selection without a write connection"};
            f.writing = true; Result result;
            try { result = f.immediate(Change<ListHandle>{*this, std::nullopt, selected}); }
            catch (...) { f.writing = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
            f.writing = false; if (!result) return result;
        }
        if (f.cursor && !exists(*f.cursor)) {
            auto old = std::find_if(f.items.begin(), f.items.end(), [&](const auto& item) { return item.value == *f.cursor; });
            std::optional<OptionValue> replacement;
            if (old != f.items.end()) {
                for (auto it = old + 1; it != f.items.end(); ++it) if (exists(it->value)) { replacement = it->value; break; }
                if (!replacement) for (auto it = old; it != f.items.begin();) { --it; if (exists(it->value)) { replacement = it->value; break; } }
            }
            f.cursor = replacement;
        }
        f.items = std::move(items); if (ui_->invalidate_) ui_->invalidate_(); return {};
    });
}
Result ListHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result ListHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r; ui_->enable(id_, enabled); return {};
    });
}
Query<Semantics> ListHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Result RadioGroupHandle::use_latest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->radios_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
        f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
    });
}
Result RadioGroupHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result RadioGroupHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result RadioGroupHandle::enable_option(OptionValue value, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto& options = ui_->radios_.at(id_).options;
        auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.value == value; });
        if (it == options.end()) return {Status::invalid_value, u"Unknown option"};
        it->enabled = enabled; if (ui_->invalidate_) ui_->invalidate_(); return {};
    });
}
Result RadioGroupHandle::connect_source(Source<std::optional<OptionValue>> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
        auto& field = ui_->radios_.at(id_);
        if (field.disconnect) field.disconnect();
        field.source = std::move(source); field.connected = false;
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        field.disconnect = field.source.subscribe([weak, state, id] { if (!weak.expired()) state->option_source_changed(id); });
        ui_->option_source_changed(id_); return {};
    });
}
void detail::UiState::option_source_changed(const Id& id) {
    OperationScope operation(this);
    auto& field = radios_.at(id); auto value = field.source.read();
    bool clean = !field.connected || field.value == field.source_value;
    field.source_value = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result RadioGroupHandle::connect_immediate(std::function<Result(const Change<RadioGroupHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->radios_.at(id_), std::move(write), true);
    });
}
Result RadioGroupHandle::connect_confirmed(std::function<Result(const Change<RadioGroupHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->radios_.at(id_), std::move(write), false);
    });
}
Result RadioGroupHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirm_field(*this);
    });
}
Result RadioGroupHandle::select(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->select_option(id_, std::move(value)); });
}
Result detail::UiState::select_option(const Id& id, OptionValue value) {
    OperationScope operation(this);
    if (auto r = available_option(id, value); !r) return r;
    auto& field = radios_.at(id);
    auto it = std::find_if(field.options.begin(), field.options.end(), [&](const auto& o) { return o.value == value; });
    if (it == field.options.end()) return {Status::invalid_value, u"Unknown option"};
    if (!it->enabled) return {Status::disabled, u"Option is disabled"};
    if (!field.writable()) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (field.value == value) return {};
    if (field.value == field.source_value && !field.submitting) field.origin = field.source_value;
    field.value = std::move(value);
    Change<RadioGroupHandle> change{RadioGroupHandle(this, lifetime_, id), field.value, field.origin};
    return write_immediately(id, field, change);
}
Query<Semantics> RadioGroupHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<std::optional<OptionValue>> RadioGroupHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::optional<OptionValue>> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->radios_.at(id_).value};
    });
}
DropdownHandle detail::UiState::add_dropdown(Control control, std::vector<RadioOption> options) {
    OperationScope operation(this);
    if (control.kind != Kind::dropdown || control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid dropdown");
    std::set<std::string> ids;
    for (const auto& option : options)
        if (option.value.id.empty() || !ids.insert(option.value.id).second) throw std::invalid_argument("Invalid dropdown option");
    auto id = control.id; controls_.push_back(std::move(control));
    DropdownField field; field.options = std::move(options); dropdowns_.emplace(id, std::move(field));
    return DropdownHandle(this, lifetime_, std::move(id));
}
void detail::UiState::close_popup() {
    OperationScope operation(this);
    if (popup_source_.empty()) return;
    if (auto it = dropdowns_.find(popup_source_); it != dropdowns_.end()) it->second.candidate.reset();
    popup_source_.clear();
    if (invalidate_) invalidate_();
}
Result DropdownHandle::open() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->dropdowns_.at(id_);
        if (!f.writable()) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
        if (auto r = ui_->focus(id_); !r) return r;
        ui_->close_popup(); ui_->popup_source_ = id_;
        auto current = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.enabled && f.value && o.value == *f.value; });
        if (current == f.options.end()) current = std::find_if(f.options.begin(), f.options.end(), [](const auto& o) { return o.enabled; });
        f.reveal = true; f.scroll.offset = 0;
        f.candidate = current == f.options.end() ? std::nullopt : std::optional<OptionValue>(current->value);
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    });
}
Result DropdownHandle::cancel() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        if (ui_->popup_source_ == id_) ui_->close_popup();
        return {};
    });
}
Result DropdownHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result DropdownHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result DropdownHandle::set_options(std::vector<RadioOption> options) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (ui_->dispatching_) return {Status::busy, u"Operation is already executing"};
        std::set<std::string> ids;
        for (const auto& o : options) if (o.value.id.empty() || !ids.insert(o.value.id).second) return {Status::invalid_value, u"Invalid dropdown option"};
        auto& f = ui_->dropdowns_.at(id_); f.options = std::move(options);
        auto valid = [&](const std::optional<OptionValue>& value) {
            return value && std::any_of(f.options.begin(), f.options.end(), [&](const auto& o) { return o.enabled && o.value == *value; });
        };
        if (!valid(f.value)) f.value.reset();
        if (!valid(f.candidate)) f.candidate.reset();
        if (ui_->invalidate_) ui_->invalidate_();
        return ui_->notify();
    });
}
Result DropdownHandle::connect_source(Source<std::optional<OptionValue>> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
        auto& f = ui_->dropdowns_.at(id_); if (f.disconnect) f.disconnect();
        f.source = std::move(source); f.connected = false;
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        f.disconnect = f.source.subscribe([weak, state, id] { if (!weak.expired()) state->dropdown_source_changed(id); });
        ui_->dropdown_source_changed(id_); return {};
    });
}
void detail::UiState::dropdown_source_changed(const Id& id) {
    OperationScope operation(this);
    auto& f = dropdowns_.at(id); auto value = f.source.read();
    bool clean = !f.connected || f.value == f.source_value;
    f.source_value = value; f.connected = true;
    if (clean && !f.submitting) {
        auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return value && o.enabled && o.value == *value; });
        f.value = it == f.options.end() ? std::nullopt : value; f.origin = value;
    }
    if (invalidate_) invalidate_();
}
Result DropdownHandle::connect_immediate(std::function<Result(const Change<DropdownHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->dropdowns_.at(id_), std::move(write), true);
    });
}
Result DropdownHandle::connect_confirmed(std::function<Result(const Change<DropdownHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->dropdowns_.at(id_), std::move(write), false);
    });
}
Result DropdownHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirm_field(*this);
    });
}
Result DropdownHandle::use_latest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->dropdowns_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
        auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return f.source_value && o.enabled && o.value == *f.source_value; });
        f.value = it == f.options.end() ? std::nullopt : f.source_value; f.origin = f.source_value;
        if (ui_->invalidate_) ui_->invalidate_();
        ui_->set_error(id_, u""); return ui_->notify();
    });
}
Result DropdownHandle::select(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->select_dropdown(id_, std::move(value)); });
}
Result detail::UiState::select_dropdown(const Id& id, OptionValue value) {
    OperationScope operation(this);
    if (auto r = available_option(id, value); !r) return r;
    auto& f = dropdowns_.at(id);
    auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.value == value; });
    if (it == f.options.end()) return {Status::invalid_value, u"Unknown option"};
    if (!it->enabled) return {Status::disabled, u"Option is disabled"};
    if (!f.writable()) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (f.value == value) return {};
    if (f.value == f.source_value && !f.submitting) f.origin = f.source_value;
    f.value = std::move(value); set_error(id, u"");
    if (invalidate_) invalidate_();
    Change<DropdownHandle> change{DropdownHandle(this, lifetime_, id), f.value, f.origin};
    return write_immediately(id, f, change);
}
Query<Semantics> DropdownHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<std::optional<OptionValue>> DropdownHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::optional<OptionValue>> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->dropdowns_.at(id_).value};
    });
}
namespace {
int snap_slider(SliderRange range, int input) {
    const long long lo = range.minimum, hi = range.maximum, step = range.step;
    const long long value = std::clamp<long long>(input, lo, hi);
    const long long lower = lo + ((value - lo) / step) * step;
    const long long upper = std::min(hi, lower + step);
    return static_cast<int>(value - lower < upper - value ? lower : upper);
}
}
SliderHandle detail::UiState::add_slider(Control control, SliderRange range) {
    OperationScope operation(this);
    if (control.kind != Kind::slider || control.id.empty() || find(control.id) || range.minimum >= range.maximum || range.step <= 0)
        throw std::invalid_argument("Invalid slider component or range");
    auto id = control.id;
    controls_.push_back(std::move(control));
    NumericField field{}; field.range = range; field.value = field.source_value = field.origin = range.minimum;
    numerics_.emplace(id, std::move(field));
    return SliderHandle(this, lifetime_, std::move(id));
}
Result SliderHandle::set_value(int value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->available(id_); !result) return result;
        const auto range = ui_->numerics_.at(id_).range;
        if (value < range.minimum || value > range.maximum) return {Status::invalid_value, u"Value is outside the slider range"};
        return ui_->slider_set_value(id_, value);
    });
}
Result SliderHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result SliderHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result SliderHandle::use_latest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->numerics_.at(id_);
        if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
        if (f.value != f.source_value) ui_->slider_land(id_);
        f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
    });
}
Result SliderHandle::connect_source(Source<int> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"A numeric source requires read and subscribe"};
        auto& f = ui_->numerics_.at(id_);
        int value = source.read();
        if (snap_slider(f.range, value) != value) return {Status::invalid_value, u"Source value is outside the slider's valid values"};
        if (f.disconnect) f.disconnect();
        f.source = std::move(source); f.connected = false;
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        f.disconnect = f.source.subscribe([weak, state, id] { if (!weak.expired()) state->numeric_source_changed(id); });
        ui_->numeric_source_changed(id_); return {};
    });
}
void detail::UiState::numeric_source_changed(const Id& id) {
    OperationScope operation(this);
    auto& f = numerics_.at(id); int value = f.source.read();
    if (snap_slider(f.range, value) != value) {
        if (!f.source_invalid) { auto it = errors_.find(id); f.error_before_source = it == errors_.end() ? u"" : it->second; }
        f.source_invalid = true; set_error(id, u"Source value is outside the slider's valid values");
        if (invalidate_) invalidate_(); return;
    }
    if (f.source_invalid) { f.source_invalid = false; set_error(id, std::move(f.error_before_source)); }
    bool clean = !f.connected || f.value == f.source_value;
    f.source_value = value; f.connected = true;
    if (clean && !f.submitting) { if (f.value != value) slider_land(id); f.value = f.origin = value; }
    if (invalidate_) invalidate_();
}
Result SliderHandle::connect_immediate(std::function<Result(const Change<SliderHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->numerics_.at(id_), std::move(write), true);
    });
}
Result SliderHandle::connect_confirmed(std::function<Result(const Change<SliderHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        return detail::UiState::connect_field_write(ui_->numerics_.at(id_), std::move(write), false);
    });
}
Result SliderHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirm_field(*this);
    });
}
Result detail::UiState::slider_set_value(const Id& id, int value) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    auto& f = numerics_.at(id);
    if (find(id)->read_only) return {Status::read_only, u"Component is read only"};
    if (!f.writable()) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    value = snap_slider(f.range, value);
    if (f.value == value) return {};
    if (f.value == f.source_value && !f.submitting) f.origin = f.source_value;
    f.value = value;
    slider_land(id);  // the user's own input starts its slide afterwards (slider_started_moving); any other change shows at once
    Change<SliderHandle> change{SliderHandle(this, lifetime_, id), f.value, f.origin};
    return write_immediately(id, f, change);
}
namespace {
double slider_share(const SliderRange& range, int value) {
    return (static_cast<double>(value) - range.minimum) / (static_cast<double>(range.maximum) - range.minimum);
}
// Pointer movement up to this many device-independent pixels from the press still counts as a click, not a drag.
constexpr double slider_drag_threshold_dip = 4;
// The thumb's place on a track as a share of it: the pointer x clamped to the track's ends.
double track_share(double x, double origin, double width) { return std::clamp((x - origin) / width, 0.0, 1.0); }
}
Result detail::UiState::slider_pointer_down(const Id& id, int x, Rect track, float scale) {
    OperationScope operation(this);
    if (pointer_armed_ != id || track.w <= 0) return {Status::invalid_value, u"No slider drag is active"};
    // With motion the thumb stays on its tick (sliding there after a value change) until the pointer
    // moves like a drag; without motion it follows the pointer from the press, as in #73.
    slider_drag_ = SliderDrag{id, static_cast<double>(track.x), static_cast<double>(track.w), x, x, !motion_enabled(),
                              static_cast<double>(track.x), static_cast<double>(track.w), static_cast<int>(std::lround(slider_drag_threshold_dip * std::max(scale, 0.f)))};
    return slider_pointer(id, x);
}
Result detail::UiState::slider_pointer(const Id& id, int x) {
    OperationScope operation(this);
    if (pointer_armed_ != id || !slider_drag_ || slider_drag_->id != id) return {Status::invalid_value, u"No slider drag is active"};
    slider_drag_->x = x;
    if (!slider_drag_->held && std::abs(x - slider_drag_->press_x) > slider_drag_->threshold) {
        // A real drag: the thumb follows the hand, so a running slide to a tick is dropped.
        slider_drag_->held = true;
        slider_land(id);
    }
    const bool held = slider_drag_->held;
    const auto basis = *slider_drag_;
    auto& f = numerics_.at(id);
    const double from = slider_display_ratio(id);
    const int before = f.value;
    double ratio = track_share(x, basis.origin, basis.width);
    double value = static_cast<double>(f.range.minimum) + ratio * (static_cast<double>(f.range.maximum) - f.range.minimum);
    auto result = slider_set_value(id, static_cast<int>(std::floor(value + 0.5)));
    if (!held) slider_started_moving(id, from, before);
    return result;
}
std::optional<int> detail::UiState::slider_drag_x(std::string_view id) const {
    if (slider_drag_ && pointer_armed_ == id && slider_drag_->id == id) return slider_drag_->x;
    return std::nullopt;
}
std::optional<int> detail::UiState::slider_held_x(std::string_view id) const {
    if (slider_drag_ && slider_drag_->held && pointer_armed_ == id && slider_drag_->id == id) return slider_drag_->x;
    return std::nullopt;
}
// A thumb transition is live only while it is still heading to the slider's current value: any
// other change (application code, undo, a semantic or UIA set, the data source) leaves it stale
// and the thumb is drawn at the new value at once.
bool detail::UiState::slider_transition_live(std::string_view id) const {
    auto target = slider_targets_.find(id);
    auto field = numerics_.find(id);
    return target != slider_targets_.end() && field != numerics_.end() && field->second.value == target->second &&
           animations_.active(slider_animation_key(id));
}
std::optional<double> detail::UiState::slider_animated_ratio(std::string_view id) const {
    if (!slider_transition_live(id)) return std::nullopt;
    return animations_.value(slider_animation_key(id));
}
// Where the thumb is shown now, as a share of the track.
double detail::UiState::slider_display_ratio(std::string_view id) const {
    if (auto animated = slider_animated_ratio(id)) return *animated;
    const auto& f = numerics_.find(id)->second;
    // The held thumb is drawn at the pointer on the track of the latest frame, so its share is measured there.
    if (slider_drag_ && slider_drag_->held && slider_drag_->id == id)
        return track_share(slider_drag_->x, slider_drag_->seen_origin, slider_drag_->seen_width);
    return slider_share(f.range, f.value);
}
// The value changed through the user's own input on this slider: slide the thumb from where it was shown.
void detail::UiState::slider_started_moving(const Id& id, double from, int value_before) {
    auto it = numerics_.find(id);
    if (it == numerics_.end() || it->second.value == value_before || !motion_enabled()) return;
    animations_.start(slider_animation_key(id), from, slider_share(it->second.range, it->second.value));
    slider_targets_.insert_or_assign(id, it->second.value);
}
// The drag ended while holding the thumb off its tick. Letting go slides it there from the pixel it was drawn at;
// an interruption (capture loss, focus move, a new press) puts it on the tick at once.
void detail::UiState::end_slider_drag(bool slide) {
    if (!slider_drag_) return;
    const auto drag = *slider_drag_;
    slider_drag_.reset();
    auto it = numerics_.find(drag.id);
    if (!slide || !drag.held || it == numerics_.end() || !motion_enabled()) return;
    const double from = track_share(drag.x, drag.seen_origin, drag.seen_width);
    const double to = slider_share(it->second.range, it->second.value);
    if (std::abs(from - to) < 1e-9) return;
    animations_.start(slider_animation_key(drag.id), from, to);
    slider_targets_.insert_or_assign(drag.id, it->second.value);
}
// The latest frame's track for the dragged slider: the held thumb is drawn on it, wherever layout moved it.
void detail::UiState::slider_track_refresh(const Frame& frame) {
    if (!slider_drag_) return;
    if (const auto* item = find_placement(frame, slider_drag_->id); item && item->slider_track.w > 0) {
        slider_drag_->seen_origin = item->slider_track.x;
        slider_drag_->seen_width = item->slider_track.w;
    }
}
// The thumb is at its value from now on: nothing slides (any change that is not the user's own input on the slider).
void detail::UiState::slider_land(std::string_view id) {
    slider_targets_.erase(Id(id));
    animations_.cancel(slider_animation_key(id));
}
bool detail::UiState::motion_enabled() const {
    return animations_.has_time_source() && presentation_.motion == Motion::automatic && system_animations_ && !window_hidden_;
}
void detail::UiState::finish_animations() {
    const bool was_running = animation_running();
    animations_.finish_all(); slider_targets_.clear();
    if (was_running && invalidate_) invalidate_();  // draw the landed frame
}
void detail::UiState::set_system_animations(bool enabled) {
    system_animations_ = enabled;
    if (!motion_enabled()) finish_animations();
}
void detail::UiState::set_window_hidden(bool hidden) {
    window_hidden_ = hidden;
    if (!motion_enabled()) finish_animations();
}
bool detail::UiState::animation_running() const {
    for (const auto& [id, target] : slider_targets_) if (slider_transition_live(id)) return true;
    return false;
}
Query<bool> detail::UiState::animating() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread", std::nullopt};
    if (stopped_) return {Status::failed, u"UI is stopped", std::nullopt};
    return {Status::ok, {}, animation_running()};
}
Query<Semantics> SliderHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<int> SliderHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<int> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->numerics_.at(id_).value};
    });
}
ComponentHandle detail::UiState::layout_handle(std::string_view id) {
    OperationScope operation(this);
    auto control = find(id);
    if (!control) return {};
    return component_type(control->kind).handle(*this, *control);
}

std::optional<Id> detail::UiState::layout_id(const ComponentHandle& handle) const {
    return std::visit([this](const auto& item) -> std::optional<Id> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
        else return item.ui_ == this && !item.lifetime_.expired() ? std::optional<Id>(item.id_) : std::nullopt;
    }, handle);
}
bool detail::UiState::retired_button(const ComponentHandle& handle) const {
    const auto* button = std::get_if<ButtonHandle>(&handle);
    return button && button->ui_ == this && button->lifetime_.expired();
}
TabsHandle detail::UiState::add_tabs(Control control, std::vector<TabPage> pages) {
    OperationScope operation(this);
    if (control.kind != Kind::tabs || control.id.empty() || find(control.id) || pages.empty() || !pages.front().enabled)
        throw std::invalid_argument("Invalid tabs component");
    std::set<std::string> values;
    std::set<Id> members;
    for (const auto& page : pages) {
        if (page.value.id.empty() || !values.insert(page.value.id).second) throw std::invalid_argument("Duplicate page identity");
        for (const auto& handle : page.members) {
            auto member = layout_id(handle);
            if (!member || removal_pending(*member) || page_members_.contains(*member) || !members.insert(*member).second)
                throw std::invalid_argument("Invalid or duplicate page member");
        }
    }
    Id id = control.id;
    controls_.push_back(std::move(control));
    for (std::size_t i = 0; i < pages.size(); ++i)
        for (const auto& handle : pages[i].members) page_members_.emplace(*layout_id(handle), std::make_pair(id, i));
    tabs_.emplace(id, TabsState{std::move(pages), 0, 0, false, {}});
    bind(id, [this, id] { auto& t = tabs_.at(id); return activate_tab(id, t.pages[t.candidate].value); });
    return TabsHandle(this, lifetime_, std::move(id));
}
NavigationListHandle detail::UiState::add_navigation_list(Control control, std::vector<NavigationPage> pages, OptionValue initial) {
    OperationScope operation(this);
    if (control.kind != Kind::navigation_list || control.id.empty() || find(control.id) || pages.empty())
        throw std::invalid_argument("Invalid navigation list");
    auto selected = std::find_if(pages.begin(), pages.end(), [&](const auto& p) { return p.value == initial && p.enabled; });
    if (selected == pages.end()) throw std::invalid_argument("Invalid initial page");
    auto index = static_cast<std::size_t>(selected - pages.begin());
    std::set<std::string> values;
    std::set<Id> members;
    for (const auto& page : pages) {
        if (page.value.id.empty() || page.value.id.find_first_not_of(" \t\r\n") == std::string::npos || !values.insert(page.value.id).second) throw std::invalid_argument("Invalid page identity");
        for (const auto& handle : page.members) {
            auto member = layout_id(handle);
            if (!member || !find(*member) || removal_pending(*member) || page_members_.contains(*member) ||
                dialog_member(*member) || !members.insert(*member).second)
                throw std::invalid_argument("Invalid page member");
        }
    }
    Id id = control.id;
    controls_.push_back(std::move(control));
    for (std::size_t i = 0; i < pages.size(); ++i)
        for (const auto& handle : pages[i].members) page_members_.emplace(*layout_id(handle), std::make_pair(id, i));
    tabs_.emplace(id, TabsState{std::move(pages), index, index, false, {}});
    return NavigationListHandle(this, lifetime_, std::move(id));
}
TooltipHandle detail::UiState::add_tooltip(ComponentHandle source, std::u16string text, unsigned delay_ms) {
    OperationScope operation(this);
    auto id = layout_id(source);
    if (!id || !find(*id) || removal_pending(*id) || text.empty() || tooltips_.contains(*id))
        throw std::invalid_argument("Invalid tooltip source or text");
    tooltips_.emplace(*id, TooltipState{std::move(text), delay_ms});
    refresh_tooltips();
    auto source_lifetime = std::visit([](const auto& h) -> std::weak_ptr<void> {
        if constexpr (std::is_same_v<std::decay_t<decltype(h)>, std::monostate>) return {};
        else return h.lifetime_;
    }, source);
    return TooltipHandle(this, source_lifetime, *id);
}
void detail::UiState::refresh_tooltips() {
    OperationScope operation(this);
    if (renderer_failed_) return;
    if (tooltip_clock_) tooltip_now_ = tooltip_clock_();
    bool changed = false;
    for (auto& [id, tip] : tooltips_) {
        auto visible = visibility_.find(id);
        bool on_screen = !has_frame_visibility_ || (visible != visibility_.end() && visible->second);
        bool eligible = on_screen && window_active_ && !modal_ && ((focus_ == id && keyboard_focus_cues()) || tooltip_hover_ == id) && bool(available(id));
        if (eligible != tip.eligible) {
            tip.eligible = eligible;
            tip.visible = false;
            tip.dismissed = false;
            if (eligible) tip.deadline = tooltip_now_ + tip.delay_ms;
            changed = true;
        }
        if (eligible && !tip.dismissed && !tip.visible && tooltip_now_ >= tip.deadline) {
            tip.visible = true; changed = true;
        }
    }
    if (changed && invalidate_) invalidate_();
}
void detail::UiState::renderer_failed(bool failed) {
    if (renderer_failed_ == failed) return;
    if (failed) {
        close_menu();
        if (tooltip_clock_) tooltip_paused_at_ = tooltip_clock_();
        renderer_failed_ = true;
        return;
    }
    if (tooltip_paused_at_ && tooltip_clock_) {
        auto now = tooltip_clock_();
        auto elapsed = now >= *tooltip_paused_at_ ? now - *tooltip_paused_at_ : 0;
        for (auto& [id, tip] : tooltips_) if (tip.eligible && !tip.visible && !tip.dismissed) tip.deadline += elapsed;
        tooltip_now_ = now;
    }
    tooltip_paused_at_.reset();
    renderer_failed_ = false;
}
void detail::UiState::frame_visibility(std::map<Id, bool, std::less<>> visibility) {
    visibility_ = std::move(visibility);
    has_frame_visibility_ = true;
    refresh_tooltips();
}
void detail::UiState::scrolled() {
    OperationScope operation(this);
    close_popup();
    close_menu();
    if (tooltip_clock_) tooltip_now_ = tooltip_clock_();
    bool changed = false;
    for (auto& [id, tip] : tooltips_) {
        if (!tip.eligible || tip.dismissed) continue;
        tip.visible = false;
        tip.deadline = tooltip_now_ + tip.delay_ms;
        changed = true;
    }
    if (changed && invalidate_) invalidate_();
}
std::optional<std::uint64_t> detail::UiState::tooltip_deadline() const {
    std::optional<std::uint64_t> deadline;
    for (const auto& [id, tip] : tooltips_) {
        if (tip.eligible && !tip.visible && !tip.dismissed && (!deadline || tip.deadline < *deadline))
            deadline = tip.deadline;
    }
    return deadline;
}
const std::u16string* detail::UiState::tooltip_text(std::string_view id) const {
    auto it = tooltips_.find(id);
    return it != tooltips_.end() && it->second.visible ? &it->second.text : nullptr;
}
void detail::UiState::tooltip_time(std::uint64_t now) {
    OperationScope operation(this);
    tooltip_now_ = now;
    refresh_tooltips();
}
void detail::UiState::tooltip_hover(std::string_view id) {
    OperationScope operation(this);
    if (tooltip_hover_ == id) return;
    tooltip_hover_ = Id(id);
    if (auto it = tooltips_.find(tooltip_hover_); it != tooltips_.end() && it->second.dismissed) {
        it->second.eligible = false;
        it->second.dismissed = false;
    }
    refresh_tooltips();
}
void detail::UiState::window_active(bool active) {
    OperationScope operation(this);
    if (window_active_ == active) return;
    window_active_ = active;
    if (!active) { tooltip_hover_.clear(); hover_clear(); close_menu(); }  // a menu does not outlive window activation
    refresh_tooltips();
}
bool detail::UiState::hover_locked() const {
    return hover_capture_ || (slider_drag_ && pointer_armed_ == slider_drag_->id);
}
bool detail::UiState::hover_admitted(const HoverTarget& target) const {
    if (menu_open() || !available(target.id)) return false;  // an open context menu takes the pointer: its own row highlight is the hover
    if (!target.item) return true;
    auto semantics = describe(target.id);
    if (!semantics) return false;
    auto option = std::find_if(semantics->options.begin(), semantics->options.end(), [&](const auto& o) { return o.value == *target.item; });
    return option != semantics->options.end() && option->enabled;
}
void detail::UiState::hover_set(std::optional<HoverTarget> target) {
    if (hover_ == target) return;
    hover_ = std::move(target);
    if (invalidate_) invalidate_();
}
void detail::UiState::hover_clear() {
    hover_simulated_ = hover_capture_ = false;
    hover_position_.reset();
    hover_set(std::nullopt);
}
void detail::UiState::hover_from_frame(const Frame& frame, int x, int y) {
    const auto* hit = frame.hit(x, y);
    tooltip_hover(hit && !hit->popup ? std::string_view(hit->id) : std::string_view{});
    if (!window_active_ || hover_locked()) return;
    std::optional<HoverTarget> target;
    // Labels and progress bars ignore the pointer, so they are never hovered (the tooltip above still sees them).
    if (hit && !menu_open() && component_type(hit->kind).accepts_pointer()) {
        target = HoverTarget{hit->id, std::nullopt};
        if (auto index = frame.option_at(*hit, x, y)) {
            auto semantics = describe(hit->id);
            // A disabled item hovers nothing: the target keeps the component without the item.
            if (semantics && *index < semantics->options.size() && semantics->options[*index].enabled)
                target->item = semantics->options[*index].value;
        }
        if (!hover_admitted(*target)) target.reset();
    }
    hover_set(std::move(target));
}
void detail::UiState::pointer_over(const Frame& frame, int x, int y, bool touch) {
    if (touch) return;
    OperationScope operation(this);
    if (window_active_) { hover_position_ = {x, y}; hover_simulated_ = false; }
    hover_from_frame(frame, x, y);
}
void detail::UiState::pointer_left() {
    OperationScope operation(this);
    tooltip_hover({});
    hover_clear();
}
void detail::UiState::hover_capture(std::string_view id) {
    OperationScope operation(this);
    if (!id.empty()) { hover_simulated_ = false; hover_set(HoverTarget{Id(id), std::nullopt}); }
    hover_capture_ = true;
}
void detail::UiState::hover_release() { hover_capture_ = false; }
void detail::UiState::hover_refresh(const Frame& frame) {
    if (hover_simulated_ || hover_locked() || !hover_position_ || !window_active_) return;
    OperationScope operation(this);
    hover_from_frame(frame, hover_position_->first, hover_position_->second);
}
std::optional<HoverTarget> detail::UiState::hover_target() const {
    if (hover_ && hover_admitted(*hover_)) return hover_;
    return std::nullopt;
}
bool detail::UiState::hover_drawn(std::string_view id, const OptionValue* item) const {
    if (presentation_.hover != Hover::on || !hover_ || hover_->id != id) return false;
    if (item ? !hover_->item || !(*hover_->item == *item) : hover_->item.has_value()) return false;
    return hover_admitted(*hover_);
}
Query<std::optional<HoverTarget>> detail::UiState::hover_query() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread", std::nullopt};
    if (stopped_) return {Status::failed, u"UI is stopped", std::nullopt};
    return {Status::ok, {}, hover_target()};
}
Result detail::UiState::simulate_hover(std::string_view id, const std::optional<OptionValue>& item) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    if (menu_open()) return {Status::blocked_by_modal, u"A context menu is open"};
    if (hover_locked()) return {Status::busy, u"A pointer drag holds the hover"};
    const auto* control = find(id);
    if (item) {
        auto semantics = describe(id);
        if (!semantics) return {Status::not_found, u"Component no longer exists"};
        auto option = std::find_if(semantics->options.begin(), semantics->options.end(), [&](const auto& o) { return o.value == *item; });
        if (option == semantics->options.end()) return {Status::not_found, u"Item no longer exists"};
        if (!option->enabled) return {Status::disabled, u"目前無法執行"};
    }
    hover_simulated_ = true;
    hover_set(HoverTarget{control->id, item});
    // An item of an open dropdown lives in its popup, which never starts a tooltip.
    tooltip_hover(item && component_type(control->kind).opens_popup() ? std::string_view{} : std::string_view(control->id));
    return {};
}
Result detail::UiState::clear_simulated_hover() {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!hover_simulated_) return {};
    hover_simulated_ = false;
    hover_set(std::nullopt);
    tooltip_hover({});
    return {};
}
Query<bool> TooltipHandle::visible() const {
    return detail::UiState::operate(*this, [&]() -> Query<bool> {
        if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
        auto it = ui_->tooltips_.find(id_);
        if (it == ui_->tooltips_.end()) return {Status::not_found, u"Tooltip no longer exists", std::nullopt};
        return {Status::ok, {}, it->second.visible};
    }, detail::UiState::ui_gone);
}
bool detail::UiState::dialog_member(std::string_view id) const {
    for (const auto& [key, dialog] : dialogs_)
        if (std::find(dialog.members.begin(), dialog.members.end(), id) != dialog.members.end()) return true;
    return false;
}
DialogHandle detail::UiState::add_dialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial, ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name) {
    OperationScope operation(this);
    auto first = layout_id(initial), backup = layout_id(fallback);
    if (!first || !backup || !find(*first) || !find(*backup) || removal_pending(*first) || removal_pending(*backup) || !result || members.empty() || !component_type(find(*first)->kind).can_focus()) throw std::invalid_argument("Invalid dialog");
    std::set<Id> unique;
    for (const auto& member : members) {
        auto item = layout_id(member);
        if (!item || !find(*item) || removal_pending(*item) || page_members_.contains(*item) || dialog_member(*item) || !unique.insert(*item).second)
            throw std::invalid_argument("Invalid dialog member");
    }
    if (!unique.contains(*first) || unique.contains(*backup)) throw std::invalid_argument("Invalid dialog focus targets");
    auto name_id = name ? layout_id(*name) : std::optional<Id>{};
    if (name && (!name_id || !unique.contains(*name_id))) throw std::invalid_argument("Dialog name must be a member label");
    DialogState dialog;
    dialog.members.assign(unique.begin(), unique.end());
    dialog.initial = *first; dialog.fallback = *backup; dialog.result = std::move(result); dialog.configured = true;
    if (name_id) dialog.name_label = *name_id;
    for (const auto& member : dialog.members) const_cast<Control*>(find(member))->page = "dialog";
    dialogs_.emplace(id, std::move(dialog));
    return DialogHandle(this, lifetime_, std::move(id));
}
Result detail::UiState::open_dialog(const Id& id, const ComponentHandle* opener) {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return {Status::not_found, u"Dialog not found"};
    if (renderer_failed_) return {Status::renderer_failed, u"Renderer failed"};
    auto& dialog = it->second;
    if (modal_ || dialog_transitioning_) return {Status::busy, u"Dialog already open or transitioning"};
    Id source = focus_;
    if (opener) {
        auto selected = layout_id(*opener);
        if (!selected) return {Status::not_found, u"Invalid opener"};
        if (auto r = available(*selected); !r) return r;
        source = *selected;
    }
    if (source.empty()) source = dialog.fallback;
    const auto* initial = find(dialog.initial);
    if (!initial || !initial->enabled) return {Status::disabled, u"Initial focus is unavailable"};
    dialog.transitioning = true; dialog_transitioning_ = true;
    struct Guard { bool& local; bool& global; ~Guard() { local = false; global = false; } } guard{dialog.transitioning, dialog_transitioning_};
    dialog.opener = source;
    if (auto r = move_focus(dialog.initial); !r) return r;
    active_dialog_ = id;
    modal(true);
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::settle_dialog(const Id& id, bool confirmed) {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return {Status::not_found, u"Dialog not found"};
    if (renderer_failed_) return {Status::renderer_failed, u"Renderer failed"};
    auto& dialog = it->second;
    if (!modal_ || active_dialog_ != id) return {Status::hidden, u"Dialog is not open"};
    if (dialog_transitioning_) return {Status::busy, u"Dialog is transitioning"};
    dialog.transitioning = true; dialog_transitioning_ = true;
    struct Guard { bool& local; bool& global; ~Guard() { local = false; global = false; } } guard{dialog.transitioning, dialog_transitioning_};
    modal(false); active_dialog_.clear();
    auto source = std::exchange(dialog.opener, {});
    auto target = find(source);
    if (!target || !focusable(*target)) target = find(dialog.fallback);
    if (target && focusable(*target)) move_focus(target->id); else move_focus({});
    try { dialog.result(confirmed); }
    catch (...) { stopped_ = true; return {Status::failed, u"Dialog result failed"}; }
    if (invalidate_) invalidate_();
    return {};
}
Result DialogHandle::open(ComponentHandle opener) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->open_dialog(id_, &opener); }, detail::UiState::ui_gone);
}
Result DialogHandle::open() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->open_dialog(id_, nullptr); }, detail::UiState::ui_gone);
}
Result DialogHandle::settle(bool confirmed) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->settle_dialog(id_, confirmed); }, detail::UiState::ui_gone);
}
Query<bool> DialogHandle::is_open() const {
    return detail::UiState::operate(*this, [&]() -> Query<bool> {
        auto it = ui_->dialogs_.find(id_);
        if (it == ui_->dialogs_.end()) return {Status::not_found, u"Dialog not found", std::nullopt};
        if (auto r = ui_->configuration_access(it->second.initial, true); !r) return {r.status, r.message, std::nullopt};
        return {Status::ok, {}, ui_->modal_ && ui_->active_dialog_ == id_};
    }, detail::UiState::ui_gone);
}
Result detail::UiState::activate_tab(const Id& id, OptionValue page) {
    OperationScope operation(this);
    if (auto r = available_option(id, page); !r) return r;
    auto& t = tabs_.at(id);
    if (t.switching) return {Status::busy, u"Page change is already in progress"};
    t.switching = true;
    struct SwitchingGuard { bool& flag; ~SwitchingGuard() { flag = false; } } guard{t.switching};
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalid_value, u"Unknown page"};
    if (!it->enabled) return {Status::disabled, u"Page is disabled"};
    auto next = static_cast<std::size_t>(it - t.pages.begin());
    if (next == t.active) { t.candidate = next; return {}; }
    // Finish an edit while its page is still admitted.
    if (auto member = page_members_.find(focus_); member != page_members_.end() && member->second.first == id && member->second.second == t.active)
        if (auto r = move_focus(id); !r) return r;
    close_popup();
    close_menu();
    auto old = t.pages[t.active].value;
    t.active = t.candidate = next;
    refresh_tooltips();
    if (t.changed) {
        try { t.changed(old, page); }
        catch (...) { stopped_ = true; return {Status::failed, u"Page change notification failed"}; }
    }
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::focus_error(const Id& id) {
    OperationScope operation(this);
    if (auto member = page_members_.find(id); member != page_members_.end()) {
        auto& tabs = tabs_.at(member->second.first);
        auto page = tabs.pages[member->second.second].value;
        if (auto result = activate_tab(member->second.first, page); !result) return result;
    }
    return focus(id);
}
Result detail::UiState::add_page_member(const Id& id, OptionValue page, ComponentHandle component) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto member = layout_id(component);
    if (!member || !find(*member) || removal_pending(*member) || page_members_.contains(*member) ||
        dialog_member(*member))
        return {Status::invalid_value, u"Invalid page member"};
    auto& t = tabs_.at(id);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalid_value, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    it->members.push_back(std::move(component));
    page_members_.emplace(*member, std::make_pair(id, index));
    if (invalidate_) invalidate_();
    return {};
}
// Tabs and a navigation list are one component inside (#94): both handles run these operations, and differ only in the
// names of activate / select and active / selected.
Result detail::UiState::enable_page(const Id& id, const OptionValue& page, bool enabled) {
    if (auto r = configuration_access(id); !r) return r;
    auto& t = tabs_.at(id);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalid_value, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    if (!enabled && index == t.active) return {Status::invalid_value, u"Active page cannot be disabled"};
    it->enabled = enabled;
    if (!enabled && index == t.candidate) t.candidate = t.active;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::on_page_change(const Id& id, std::function<void(OptionValue, OptionValue)> callback) {
    if (auto r = configuration_access(id); !r) return r;
    tabs_.at(id).changed = std::move(callback);
    return {};
}
Query<OptionValue> detail::UiState::active_page(const Id& id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    const auto& t = tabs_.at(id);
    return {Status::ok, {}, t.pages[t.active].value};
}
Result TabsHandle::activate(OptionValue page) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->activate_tab(id_, std::move(page)); });
}
Result TabsHandle::add_member(OptionValue page, ComponentHandle component) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->add_page_member(id_, std::move(page), std::move(component)); });
}
Result TabsHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result TabsHandle::enable_page(OptionValue page, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->enable_page(id_, page, enabled); });
}
Result TabsHandle::on_page_change(std::function<void(OptionValue, OptionValue)> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->on_page_change(id_, std::move(callback)); });
}
Query<Semantics> TabsHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<OptionValue> TabsHandle::active() const {
    return detail::UiState::operate(*this, [&]() -> Query<OptionValue> { return ui_->active_page(id_); });
}
Result NavigationListHandle::select(OptionValue page) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->activate_tab(id_, std::move(page)); });
}
Result NavigationListHandle::add_member(OptionValue page, ComponentHandle component) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->add_page_member(id_, std::move(page), std::move(component)); });
}
Result NavigationListHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result NavigationListHandle::enable_page(OptionValue page, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->enable_page(id_, page, enabled); });
}
Result NavigationListHandle::on_page_change(std::function<void(OptionValue, OptionValue)> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->on_page_change(id_, std::move(callback)); });
}
Query<Semantics> NavigationListHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<OptionValue> NavigationListHandle::selected() const {
    return detail::UiState::operate(*this, [&]() -> Query<OptionValue> { return ui_->active_page(id_); });
}
Result LabelHandle::connect_source(std::function<std::u16string()> read) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (!read) return {Status::invalid_value, u"A label source requires a reader"};
        ui_->bind_text(id_, std::move(read));
        return {};
    });
}
Result detail::UiState::writing_support(const Control& control) {
    if (control.vertical_digits < 0 || control.vertical_digits > max_vertical_digits)
        return {Status::invalid_value, u"The digit limit of vertical writing is 0-3"};
    if (!detail::component_type(control.kind).supports_vertical_writing() && control.writing_mode == WritingMode::vertical)
        return {Status::invalid_value, u"Only labels support vertical writing"};
    return {};
}
Result LabelHandle::set_writing_mode(WritingMode mode) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (mode != WritingMode::horizontal && mode != WritingMode::vertical) return {Status::invalid_value, u"Unknown writing mode"};
        ui_->set_label_writing(id_, mode, std::nullopt);
        return {};
    });
}
Query<WritingMode> LabelHandle::writing_mode() const {
    return detail::UiState::operate(*this, [&]() -> Query<WritingMode> {
        if (auto result = ui_->configuration_access(id_, true); !result) return {result.status, result.message, std::nullopt};
        return {Status::ok, {}, ui_->find(id_)->writing_mode};
    });
}
Result LabelHandle::set_vertical_digits(int digits) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (digits < 0 || digits > max_vertical_digits) return {Status::invalid_value, u"The digit limit of vertical writing is 0-3"};
        ui_->set_label_writing(id_, std::nullopt, digits);
        return {};
    });
}
Query<int> LabelHandle::vertical_digits() const {
    return detail::UiState::operate(*this, [&]() -> Query<int> {
        if (auto result = ui_->configuration_access(id_, true); !result) return {result.status, result.message, std::nullopt};
        return {Status::ok, {}, ui_->find(id_)->vertical_digits};
    });
}
void detail::UiState::set_label_writing(std::string_view id, std::optional<WritingMode> mode, std::optional<int> digits) {
    OperationScope operation(this);
    auto* control = const_cast<Control*>(find(id));
    if (!control) return;
    bool changed = (mode && *mode != control->writing_mode) || (digits && *digits != control->vertical_digits);
    if (mode) control->writing_mode = *mode;
    if (digits) control->vertical_digits = *digits;
    if (changed && invalidate_) invalidate_();
}
Result ButtonHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result ButtonHandle::bind(std::function<Result()> action) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (!action) return {Status::invalid_value, u"An action is required"};
        if (ui_->confirmation_buttons_.contains(id_)) return {Status::invalid_value, u"Confirmation action cannot be rebound"};
        ui_->bind(id_, std::move(action));
        return {};
    });
}
Result ButtonHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        ui_->enable(id_, enabled);
        return {};
    });
}
Result ButtonHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        if (ui_->dialog_member(id_) || std::any_of(ui_->dialogs_.begin(), ui_->dialogs_.end(), [&](const auto& entry) { return entry.second.fallback == id_; }))
            return {Status::invalid_value, u"Dialog focus targets must remain alive"};
        if (std::find(ui_->pending_removals_.begin(), ui_->pending_removals_.end(), id_) == ui_->pending_removals_.end())
            ui_->pending_removals_.push_back(id_);
        return {};
    });
}

Result TextHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result SliderHandle::on_blur(std::function<void()> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (callback) ui_->blur_callbacks_.insert_or_assign(id_, std::move(callback));
        else ui_->blur_callbacks_.erase(id_);
        return {};
    });
}
Result TextHandle::on_blur(std::function<void()> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (callback) ui_->blur_callbacks_.insert_or_assign(id_, std::move(callback));
        else ui_->blur_callbacks_.erase(id_);
        return {};
    });
}
Result TextHandle::set_error(std::u16string message) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_error(id_, std::move(message)); });
}
Result TextHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        ui_->enable(id_, enabled);
        return {};
    });
}
Result TextHandle::read_only(bool is_read_only) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        ui_->set_read_only(id_, is_read_only);
        return {};
    });
}
Result TextHandle::reset_to_source() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->reset_to_source(id_); });
}
Result detail::UiState::reset_to_source(const Id& id) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.connected)
        return {Status::invalid_value, u"Text source is not connected"};
    replace_rich(id, RichText{it->second.source_value, it->second.source_spans});
    it->second.origin = it->second.source_value;
    it->second.origin_spans = it->second.source_spans;
    return set_error(id, u"");
}
Result ButtonHandle::invoke() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->invoke(id_); });
}
Query<bool> ButtonHandle::executing() const { return semantic_property(describe(), &Semantics::executing); }
Result TextHandle::paste(std::u16string_view text) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->paste(id_, text); });
}
Result TextHandle::undo() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->undo(id_); });
}
Result TextHandle::redo() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->redo(id_); });
}
Result TextHandle::select(unsigned anchor, unsigned caret) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->select(id_, anchor, caret); });
}
Result TextHandle::copy() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->copy(id_); });
}
Result TextHandle::cut() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->cut(id_); });
}
Result detail::UiState::configuration_access(std::string_view id, bool read_only) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!find(id) || removal_pending(id)) return {Status::not_found, u"Component no longer exists"};
    if (renderer_failed_ && !read_only) return {Status::renderer_failed, u"Renderer failed"};
    return {};
}
Result detail::UiState::connect_mixed_confirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<MixedConfirmationResult(const std::vector<FieldChange>&)> submit) {
    OperationScope operation(this);
    if (button.ui_ != this || button.lifetime_.expired() || !submit || fields.empty())
        return {Status::invalid_value, u"Invalid confirmation group"};
    if (auto r = configuration_access(button.id_); !r) return r;
    if (actions_.contains(button.id_)) return {Status::invalid_value, u"Confirmation button already has an action"};
    std::set<Id> members;
    for (const auto& member : fields) {
        bool valid = std::visit([&](const auto& h) {
            if (h.ui_ != this || h.lifetime_.expired() || !members.insert(h.id_).second) return false;
            auto& state = fields_of<std::decay_t<decltype(h)>>();
            auto it = state.find(h.id_);
            return it != state.end() && it->second.joinable();
        }, member);
        if (!valid) return {Status::invalid_value, u"Confirmation fields must be distinct, connected and ungrouped"};
    }
    for (const auto& member : fields) std::visit([&](const auto& h) { fields_of<std::decay_t<decltype(h)>>().at(h.id_).confirmation_member = true; }, member);
    confirmation_buttons_.insert(button.id_);
    confirmation_members_[button.id_] = {members.begin(), members.end()};
    bind(button.id_, [this, fields = std::move(fields), submit = std::move(submit)] {
        std::vector<FieldChange> changes;
        for (const auto& member : fields) std::visit([&](const auto& h) {
            auto& f = fields_of<std::decay_t<decltype(h)>>().at(h.id_);
            if constexpr (std::is_same_v<std::decay_t<decltype(h)>, TextHandle>) {
                if (f.rich) changes.emplace_back(Change<TextHandle, RichText>{h, RichText{f.session.text, f.spans}, RichText{f.origin, f.origin_spans}});
                else changes.emplace_back(Change<TextHandle>{h, f.session.text, f.origin});
            } else changes.emplace_back(typename std::decay_t<decltype(f)>::Change{h, f.value, f.origin});
            f.submitting = true;
        }, member);
        MixedConfirmationResult outcome;
        try { outcome = submit(changes); }
        catch (...) {
            for (const auto& member : fields) std::visit([&](const auto& h) { fields_of<std::decay_t<decltype(h)>>().at(h.id_).submitting = false; }, member);
            throw;
        }
        for (std::size_t i = 0; i < fields.size(); ++i) std::visit([&](const auto& h) {
            auto& f = fields_of<std::decay_t<decltype(h)>>().at(h.id_);
            f.submitting = false;
            if (outcome.result) {
                if constexpr (std::is_same_v<std::decay_t<decltype(h)>, TextHandle>) {
                    if (f.rich) { const auto& rich = std::get<Change<TextHandle, RichText>>(changes[i]).value; f.origin = rich.text; f.origin_spans = rich.spans; }
                    else f.origin = std::get<Change<TextHandle>>(changes[i]).value;
                } else f.origin = std::get<typename std::decay_t<decltype(f)>::Change>(changes[i]).value;
            }
            if (outcome.result) set_error(h.id_, u"");
        }, fields[i]);
        if (!outcome.result) {
            std::optional<std::size_t> first;
            for (const auto& error : outcome.errors) {
                auto id = std::visit([&](const auto& h) { return layout_id(ComponentHandle{h}); }, error.field);
                if (!id) continue;
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    auto field_id = std::visit([&](const auto& h) { return h.id_; }, fields[i]);
                    if (field_id == *id) { set_error(*id, error.message); if (!error.message.empty() && (!first || i < *first)) first = i; break; }
                }
            }
            if (first) std::visit([&](const auto& h) { focus_error(h.id_); }, fields[*first]);
        }
        auto notification = notify(); return notification ? outcome.result : notification;
    });
    return {};
}
Result TextHandle::connect_immediate(std::function<Result(const Change<TextHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (ui_->is_rich(id_)) return {Status::not_supported, u"A rich field connects with connect_rich_immediate"};
        return ui_->connect_write(id_, std::move(write), true);
    });
}
Result TextHandle::connect_confirmed(std::function<Result(const Change<TextHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (ui_->is_rich(id_)) return {Status::not_supported, u"A rich field connects with connect_rich_confirmed"};
        return ui_->connect_write(id_, std::move(write), false);
    });
}
Result TextHandle::connect_rich_immediate(std::function<Result(const Change<TextHandle, RichText>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->connect_rich_write(id_, std::move(write), true); });
}
Result TextHandle::connect_rich_confirmed(std::function<Result(const Change<TextHandle, RichText>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->connect_rich_write(id_, std::move(write), false); });
}
// The shared write path hands over a plain Change<TextHandle>; a rich write reads the field's spans at that moment.
Result detail::UiState::connect_rich_write(const Id& id, std::function<Result(const Change<TextHandle, RichText>&)> write, bool immediate) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::not_supported, u"Not a rich text field"};
    if (!write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    return connect_write(id, [this, write = std::move(write)](const Change<TextHandle>& change) {
        auto& field = edits_.at(change.field.id_);
        return write(Change<TextHandle, RichText>{change.field, RichText{change.value, field.spans}, RichText{change.origin, field.origin_spans}});
    }, immediate);
}
Result detail::UiState::connect_write(const Id& id, std::function<Result(const Change<TextHandle>&)> write, bool immediate) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    return connect_field_write(it->second, std::move(write), immediate);
}
Result TextHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->confirm(id_); });
}
Result detail::UiState::confirm(const Id& id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.confirmed)
        return {Status::invalid_value, u"No confirmed write is connected"};
    auto& field = it->second;
    if (field.submitting || field.session.composing || dispatching_ || invoking_)
        return {Status::busy, u"Operation is already executing"};
    return submit_field(id, field, field.confirmed, true);
}
Result TextHandle::connect_source(Source<std::u16string> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (ui_->is_rich(id_)) return {Status::not_supported, u"A rich field connects with connect_rich_source"};
        return ui_->connect_source(id_, std::move(source));
    });
}
Result TextHandle::connect_rich_source(Source<RichText> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configuration_access(id_); !result) return result;
        if (!ui_->is_rich(id_)) return {Status::not_supported, u"Not a rich text field"};
        if (!source.read || !source.subscribe) return {Status::invalid_value, u"A rich text source requires read and subscribe"};
        return ui_->connect_source(id_, std::move(source));
    });
}
Result detail::UiState::connect_source(const Id& id, TextFieldSource source) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_found, u"Component no longer exists"};
    if (!std::visit([](const auto& s) { return s.read && s.subscribe; }, source))
        return {Status::invalid_value, u"A text source requires read and subscribe"};
    if (it->second.disconnect) it->second.disconnect();
    it->second.source = std::move(source);
    it->second.connected = false;
    auto weak = std::weak_ptr<void>(lifetime_);
    it->second.disconnect = std::visit([&](const auto& s) {
        return s.subscribe([weak, this, id] {
            if (!weak.expired()) source_changed(id);
        });
    }, it->second.source);
    source_changed(id);
    return {};
}
void detail::UiState::source_changed(const Id& id) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || !std::visit([](const auto& s) { return bool(s.read); }, it->second.source)) return;
    auto& field = it->second;
    RichText latest;
    if (const auto* rich = std::get_if<Source<RichText>>(&field.source)) {  // only a value in normal form (or one that normalizes) is adopted
        auto read = normalize_rich_text(rich->read(), find(id)->max_length);
        if (!read) return;
        latest = std::move(*read);
    } else {
        latest.text = std::get<Source<std::u16string>>(field.source).read();
        if (!valid_value(*find(id), latest.text)) return;
    }
    const bool clean = !field.connected || source_matches(field);
    field.source_value = latest.text;
    field.source_spans = latest.spans;
    field.connected = true;
    if (clean && !field.submitting && !field.session.composing) {
        field.origin = latest.text;
        field.origin_spans = latest.spans;
        replace_rich(id, std::move(latest));
    }
    if (invalidate_) invalidate_();
}
Query<bool> TextHandle::source_conflict() const { return semantic_property(describe(), &Semantics::source_conflict); }
Result TextHandle::use_latest() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->use_latest(id_); });
}
Result detail::UiState::use_latest(const Id& id) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.connected) return {Status::invalid_value, u"Text source is not connected"};
    auto latest = it->second.source_value;
    auto latest_spans = it->second.source_spans;
    const bool was_submitting = it->second.submitting;
    it->second.submitting = true;
    auto result = it->second.rich ? set_rich_value(id, RichText{latest, latest_spans}) : set_value(id, latest);
    it->second.submitting = was_submitting;
    if (result) {
        it->second.origin = std::move(latest);
        it->second.origin_spans = std::move(latest_spans);
        set_error(id, u"");
    }
    return result;
}
Result TextHandle::set_value(std::u16string value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_value(id_, std::move(value)); });
}
Result TextHandle::set_rich_value(RichText value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_rich_value(id_, std::move(value)); });
}
Query<RichText> TextHandle::rich_value() const {
    return detail::UiState::operate(*this, [&]() -> Query<RichText> { return ui_->rich_value(id_); });
}
Result TextHandle::set_style(unsigned begin, unsigned end, StyleChange change) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_style(id_, begin, end, change); });
}
Result TextHandle::toggle_style(StyleAttribute attribute) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->toggle_style(id_, attribute); });
}
Result TextHandle::apply_style(StyleChange change) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->apply_style(id_, change); });
}
Query<TextStyle> TextHandle::typing_style() const {
    return detail::UiState::operate(*this, [&]() -> Query<TextStyle> { return ui_->typing_style(id_); });
}
Result TextHandle::set_typing_style(TextStyle style) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_typing_style(id_, std::move(style)); });
}
Result TextHandle::set_colors(std::vector<NamedColor> colors) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_colors(id_, std::move(colors)); });
}
Query<std::vector<NamedColor>> TextHandle::colors() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::vector<NamedColor>> { return ui_->colors(id_); });
}
Query<Semantics> TextHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Result TextHandle::set_visible_lines(unsigned lines) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_visible_lines(id_, lines); });
}
Query<unsigned> TextHandle::visible_lines() const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> {
        if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
        const auto* control = ui_->find(id_);
        if (!control->multiline) return {Status::not_supported, u"Not a multi-line field", std::nullopt};
        return {Status::ok, {}, control->visible_lines};
    });
}
Result TextHandle::set_scroll_line(unsigned first_visible_line) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_scroll_line(id_, first_visible_line); });
}
Query<unsigned> TextHandle::scroll_line() const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> { return ui_->scroll_line(id_); });
}
Result TextHandle::set_word_segmentation(WordSegmentation mode) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_word_segmentation(id_, mode); });
}
Query<WordSegmentation> TextHandle::word_segmentation() const {
    return detail::UiState::operate(*this, [&]() -> Query<WordSegmentation> { return ui_->word_segmentation(id_); });
}
Query<WordSegmentation> TextHandle::word_segmentation_in_use() const {
    return detail::UiState::operate(*this, [&]() -> Query<WordSegmentation> { return ui_->word_segmentation_in_use(id_); });
}
Query<TextSpan> TextHandle::word_at(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<TextSpan> { return ui_->word_at(id_, offset); });
}
Query<TextSpan> TextHandle::paragraph_at(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<TextSpan> { return ui_->paragraph_at(id_, offset); });
}
Query<unsigned> TextHandle::next_word_stop(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> { return ui_->word_stop(id_, offset, true); });
}
Query<unsigned> TextHandle::previous_word_stop(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> { return ui_->word_stop(id_, offset, false); });
}
Result TextHandle::select_word(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->select_word(id_, offset); });
}
Result TextHandle::select_paragraph(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->select_paragraph(id_, offset); });
}
Result TextHandle::double_click(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->double_click(id_, offset); });
}
Result TextHandle::triple_click(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->triple_click(id_, offset); });
}
Result detail::UiState::set_visible_lines(std::string_view id, unsigned lines) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto* control = const_cast<Control*>(find(id));
    if (!control->multiline) return {Status::not_supported, u"Not a multi-line field"};
    if (lines < 1 || lines > max_visible_lines) return {Status::invalid_value, u"A multi-line field shows at least one line"};
    control->visible_lines = lines;
    auto& m = edits_.find(id)->second.multi;
    if (m.placed) {  // keep the position valid for the new page size until the next arrange settles it
        m.view.visible = lines;
        const auto count = static_cast<unsigned>(m.view.lines.size());
        scroll_lines_to(id, std::min(m.first, count > lines ? count - lines : 0));
    }
    if (invalidate_) invalidate_();
    return {};
}
// Text flow (#80): owner configuration shared by the typed handles and the semantic verbs.
Result detail::UiState::set_text_align(std::string_view id, TextAlign align) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto* control = const_cast<Control*>(find(id));
    if (!supports_text_flow(control->kind)) return {Status::not_supported, u"Only labels and text fields align their text"};
    if (!valid_text_align(align)) return {Status::invalid_value, u"Unknown alignment"};
    if (!admits_text_align(control->kind, align)) return {Status::invalid_value, u"Only a label can distribute its text"};
    control->text_align = align;
    ++layout_revision_;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::set_justify_method(std::string_view id, JustifyMethod method) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto* control = const_cast<Control*>(find(id));
    if (!supports_text_flow(control->kind)) return {Status::not_supported, u"Only labels and text fields justify their text"};
    if (!valid_justify_method(method)) return {Status::invalid_value, u"Unknown justification method"};
    control->justify_method = method;
    ++layout_revision_;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::set_line_spacing(std::string_view id, double multiple) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto* control = const_cast<Control*>(find(id));
    if (!supports_text_flow(control->kind)) return {Status::not_supported, u"Only labels and text fields have line spacing"};
    if (!valid_line_spacing(multiple)) return {Status::invalid_value, u"Line spacing is 1.0 to 3.0 times the line height"};
    control->line_spacing = multiple;
    ++layout_revision_;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::set_paragraph_spacing(std::string_view id, int dip) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto* control = const_cast<Control*>(find(id));
    if (!supports_text_flow(control->kind)) return {Status::not_supported, u"Only labels and text fields have paragraph spacing"};
    if (!valid_paragraph_spacing(dip)) return {Status::invalid_value, u"Paragraph spacing is 0 to 48 DIP"};
    control->paragraph_spacing = dip;
    ++layout_revision_;
    if (invalidate_) invalidate_();
    return {};
}
// Text flow (#80, #84): one implementation for the text field and the label (#94). The setters run the UiState operations
// above, the getters read the handle's own description; both handles declare the same eight operations.
Result TextHandle::set_text_align(TextAlign align) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_text_align(id_, align); });
}
Query<TextAlign> TextHandle::text_align() const { return semantic_property(describe(), &Semantics::text_align); }
Result TextHandle::set_justify_method(JustifyMethod method) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_justify_method(id_, method); });
}
Query<JustifyMethod> TextHandle::justify_method() const { return semantic_property(describe(), &Semantics::justify_method); }
Result TextHandle::set_line_spacing(double multiple) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_line_spacing(id_, multiple); });
}
Query<double> TextHandle::line_spacing() const { return semantic_property(describe(), &Semantics::line_spacing); }
Result TextHandle::set_paragraph_spacing(int dip) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_paragraph_spacing(id_, dip); });
}
Query<int> TextHandle::paragraph_spacing() const { return semantic_property(describe(), &Semantics::paragraph_spacing); }
Result LabelHandle::set_text_align(TextAlign align) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_text_align(id_, align); });
}
Query<TextAlign> LabelHandle::text_align() const { return semantic_property(describe(), &Semantics::text_align); }
Result LabelHandle::set_justify_method(JustifyMethod method) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_justify_method(id_, method); });
}
Query<JustifyMethod> LabelHandle::justify_method() const { return semantic_property(describe(), &Semantics::justify_method); }
Result LabelHandle::set_line_spacing(double multiple) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_line_spacing(id_, multiple); });
}
Query<double> LabelHandle::line_spacing() const { return semantic_property(describe(), &Semantics::line_spacing); }
Result LabelHandle::set_paragraph_spacing(int dip) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->set_paragraph_spacing(id_, dip); });
}
Query<int> LabelHandle::paragraph_spacing() const { return semantic_property(describe(), &Semantics::paragraph_spacing); }
Result detail::UiState::set_scroll_line(std::string_view id, unsigned first) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    if (!find(id)->multiline) return {Status::not_supported, u"Not a multi-line field"};
    auto& m = edits_.find(id)->second.multi;
    if (!m.placed) { m.pending_first = first; if (invalidate_) invalidate_(); return {}; }  // clamped by the first arrange
    const auto count = static_cast<unsigned>(m.view.lines.size());
    if (first > (count > m.view.visible ? count - m.view.visible : 0)) return {Status::invalid_value, u"No such scroll position"};
    scroll_lines_to(id, first);
    return {};
}
Query<unsigned> detail::UiState::scroll_line(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    if (!find(id)->multiline) return {Status::not_supported, u"Not a multi-line field", std::nullopt};
    const auto& m = edits_.find(id)->second.multi;
    return {Status::ok, {}, m.pending_first.value_or(m.first)};
}
Query<std::u16string> TextHandle::draft() const { return semantic_property(describe(), &Semantics::value); }
Query<Semantics> ButtonHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
Query<Semantics> LabelHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<Semantics> { return ui_->handle_describe(id_); });
}
// What every component handle's describe() answers once its lifetime has been checked.
Query<Semantics> detail::UiState::handle_describe(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, describe(id)};
}
std::optional<Semantics> detail::UiState::describe(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return std::nullopt;
    Semantics s;
    s.id = control->id; s.kind = control->kind; s.target = control->target; s.role = control->role;
    s.enabled = control->enabled; s.read_only = control->read_only;
    if (auto it = visibility_.find(control->id); it != visibility_.end())
        s.visible = it->second && available(id, true).status != Status::hidden;
    auto text_scroll = text_scrolls_.find(id);
    s.focusable = bool(available(id, true)) && (component_type(control->kind).can_focus() || (text_scroll != text_scrolls_.end() && text_scroll->second.maximum > 0));
    s.focused = focus_ == control->id;
    s.executing = executing_ == control->id;
    s.name = control->label; s.value = text(id);
    s.text_align = control->text_align; s.justify_method = control->justify_method; s.line_spacing = control->line_spacing; s.paragraph_spacing = control->paragraph_spacing;
    s.writing_mode = control->writing_mode; s.vertical_digits = control->vertical_digits;
    component_type(control->kind).describe(*this, *control, s);
    if (auto it = errors_.find(id); it != errors_.end()) s.error = it->second;
    s.invalid = !s.error.empty();
    for (const auto& other : controls_) {
        if (other.target != id) continue;
        s.labels.push_back(other.id);
        if (other.role == TextRole::name) s.name = text(other.id);
        else if (other.role == TextRole::description) s.description = text(other.id);
    }
    if (auto tip = tooltips_.find(id); tip != tooltips_.end()) {
        if (!s.description.empty()) s.description += u"\n";
        s.description += tip->second.text;
    }
    if (!s.focusable || renderer_failed_) return s;
    s.operations.push_back(Operation::focus);
    component_type(control->kind).describe_operations(*this, *control, s);
    return s;
}
std::u16string detail::UiState::text(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return {};
    if (auto shown = component_type(control->kind).display_text(*this, *control)) return *shown;
    return control->value;
}
void detail::UiState::bind_text(std::string_view id, std::function<std::u16string()> provider) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (!control || !component_type(control->kind).accepts_text_provider() || !provider) throw std::invalid_argument("Text binding requires a label and a provider");
    providers_.insert_or_assign(control->id, std::move(provider));
}
const Control* detail::UiState::find(std::string_view id) const {
    auto it = std::find_if(controls_.begin(), controls_.end(), [&](const auto& control) { return control.id == id; });
    return it == controls_.end() ? nullptr : &*it;
}
EditSession* detail::UiState::edit(std::string_view id) {
    OperationScope operation(this);
    auto it = edits_.find(id); return it == edits_.end() ? nullptr : &it->second.session;
}
Result detail::UiState::available(std::string_view id, bool ignore_renderer_failure) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto control = find(id);
    if (!control || removal_pending(id)) return {Status::not_found, u"Component no longer exists"};
    if (renderer_failed_ && !ignore_renderer_failure) return {Status::renderer_failed, u"Renderer failed"};
    if (auto member = page_members_.find(id); member != page_members_.end() && tabs_.at(member->second.first).active != member->second.second)
        return {Status::hidden, u"Page is hidden"};
    if (control->page == "dialog" && (!modal_ || (dialog_member(id) && (active_dialog_.empty() || std::find(dialogs_.at(active_dialog_).members.begin(), dialogs_.at(active_dialog_).members.end(), id) == dialogs_.at(active_dialog_).members.end())))) return {Status::hidden, u"對話框尚未開啟"};
    if (!control->enabled) return {Status::disabled, u"目前無法執行"};
    if (modal_ && control->page != "dialog") return {Status::blocked_by_modal, u"請先關閉對話框"};
    return {};
}
Result detail::UiState::available_option(std::string_view id, const OptionValue& value) const {
    auto result = available(id);
    if (result.status == Status::blocked_by_modal) {
        auto snapshot = describe(id);
        if (snapshot) {
            auto option = std::find_if(snapshot->options.begin(), snapshot->options.end(), [&](const auto& o) { return o.value == value; });
            if (option != snapshot->options.end() && !option->enabled) return {Status::disabled, {}};
        }
    }
    return result;
}
Result detail::UiState::set_value(std::string_view id, std::u16string value) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.text == value && field->spans.empty()) return {};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    // On a rich field the new text is unstyled: the inserted spans are empty, not the typing style.
    static const std::vector<StyleSpan> unstyled;
    return replace_range(id, *field, 0, static_cast<unsigned>(field->session.text.size()), value, field->rich ? &unstyled : nullptr);
}
Result detail::UiState::writable(std::string_view id, Field*& field) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (find(id)->read_only) return {Status::read_only, u"欄位為唯讀"};
    if (!it->second.writable())
        return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (it->second.session.composing || dispatching_) return {Status::busy, u"操作尚未結束"};
    field = &it->second;
    return {};
}
// Every content change funnels through here so history, value mirror and notification agree.
Result detail::UiState::replace_selection(std::string_view id, Field& field, std::u16string_view insertion) {
    OperationScope operation(this);
    return replace_range(id, field, field.session.begin(), field.session.end(), insertion, nullptr);
}
Result detail::UiState::replace_range(std::string_view id, Field& field, unsigned first, unsigned last, std::u16string_view insertion,
                                      const std::vector<StyleSpan>* inserted_spans) {
    OperationScope operation(this);
    auto& session = field.session;
    auto next = session.text;
    next.replace(first, last - first, insertion);
    const auto& control = *find(id);
    const auto limit = control.max_length;
    if (!valid_value(control, next)) {
        auto digits = std::to_string(limit);
        return {Status::invalid_value, std::u16string(control.multiline ? u"請輸入有效文字（最多 " : u"請輸入有效單行文字（最多 ") + std::u16string(digits.begin(), digits.end()) + u" 個 UTF-16 單位）"};
    }
    auto caret = first + static_cast<unsigned>(insertion.size());
    // Typed or plain-pasted text takes the typing style; inserted spans (a rich paste, a rich value) bring their own.
    std::vector<StyleSpan> next_spans;
    if (field.rich) {
        const auto length = static_cast<unsigned>(insertion.size());
        next_spans = detail::splice_spans(field.spans, first, last - first, length,
                                          inserted_spans ? *inserted_spans : detail::uniform_spans(length, insertion_style(field, first, last)));
    }
    if (next == session.text && next_spans == field.spans) { session.select(caret, caret); return {}; }
    if (field.connected && !field.submitting && source_matches(field)) {
        field.origin = field.source_value;
        field.origin_spans = field.source_spans;
    }
    field.past.push_back({session.text, session.selection, field.spans});
    field.future.clear();
    session.text = std::move(next);
    field.spans = std::move(next_spans);
    field.typing.reset();
    ++session.revision;
    session.select(caret, caret);
    return finish_edit(id, field);
}
// The style of text inserted at [first, last): the temporary typing style while the caret has not moved, else the
// character before (a paragraph's first character takes the one after it; an empty paragraph is plain).
TextStyle detail::UiState::insertion_style(const Field& field, unsigned first, unsigned last) const {
    if (field.typing && field.typing_moves == field.session.selection_changes) return *field.typing;
    const auto& text = field.session.text;
    if (first > 0 && text[first - 1] != u'\n') return detail::style_at(field.spans, first - 1);
    if (last < text.size() && text[last] != u'\n') return detail::style_at(field.spans, last);
    return {};
}
bool detail::UiState::is_rich(std::string_view id) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.rich;
}
Result detail::UiState::rich_writable(std::string_view id, Field*& field) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    if (!is_rich(id)) return {Status::not_supported, u"Not a rich text field"};
    return writable(id, field);
}
Result detail::UiState::commit_style(std::string_view id, Field& field, std::vector<StyleSpan> spans) {
    OperationScope operation(this);
    if (spans == field.spans) return {};
    auto& session = field.session;
    if (field.connected && !field.submitting && source_matches(field)) {
        field.origin = field.source_value;
        field.origin_spans = field.source_spans;
    }
    field.past.push_back({session.text, session.selection, field.spans});
    field.future.clear();
    field.spans = std::move(spans);
    ++session.revision;
    return finish_edit(id, field);
}
Result detail::UiState::set_rich_value(std::string_view id, RichText value) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = rich_writable(id, field); !result) return result;
    auto normal = normalize_rich_text(std::move(value), find(id)->max_length);
    if (!normal) return {normal.status, normal.message};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    return replace_range(id, *field, 0, static_cast<unsigned>(field->session.text.size()), normal->text, &normal->spans);
}
Query<RichText> detail::UiState::rich_value(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::not_supported, u"Not a rich text field", std::nullopt};
    return {Status::ok, {}, RichText{it->second.session.text, it->second.spans}};
}
Result detail::UiState::set_style(std::string_view id, unsigned begin, unsigned end, const StyleChange& change) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = rich_writable(id, field); !result) return result;
    const auto& text = field->session.text;
    auto boundary = [&](unsigned pos) { return pos <= text.size() && !(pos > 0 && pos < text.size() && text[pos] >= 0xdc00 && text[pos] <= 0xdfff && text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff); };
    if (begin > end || !boundary(begin) || !boundary(end)) return {Status::invalid_value, u"No such text range"};
    return commit_style(id, *field, detail::apply_style_change(field->spans, begin, end, change));
}
Query<TextStyle> detail::UiState::typing_style(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::not_supported, u"Not a rich text field", std::nullopt};
    const auto& session = it->second.session;
    return {Status::ok, {}, insertion_style(it->second, session.begin(), session.end())};
}
Result detail::UiState::set_typing_style(std::string_view id, TextStyle style) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = rich_writable(id, field); !result) return result;
    field->typing = std::move(style);
    field->typing_moves = field->session.selection_changes;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::apply_style(std::string_view id, const StyleChange& change) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = rich_writable(id, field); !result) return result;
    const auto& session = field->session;
    if (session.begin() != session.end()) return set_style(id, session.begin(), session.end(), change);
    return set_typing_style(id, detail::changed(insertion_style(*field, session.begin(), session.end()), change));
}
Result detail::UiState::toggle_style(std::string_view id, StyleAttribute attribute) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = rich_writable(id, field); !result) return result;
    const auto& session = field->session;
    auto flip = [&](bool on) { StyleChange change; (attribute == StyleAttribute::bold ? change.bold : attribute == StyleAttribute::italic ? change.italic : change.underline) = on; return change; };
    if (session.begin() != session.end())
        return set_style(id, session.begin(), session.end(), flip(!detail::all_have(field->spans, session.text, session.begin(), session.end(), attribute)));
    auto current = insertion_style(*field, session.begin(), session.end());
    bool on = attribute == StyleAttribute::bold ? current.bold : attribute == StyleAttribute::italic ? current.italic : current.underline;
    return set_typing_style(id, detail::changed(current, flip(!on)));
}
Result detail::UiState::set_colors(std::string_view id, std::vector<NamedColor> colors) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::not_supported, u"Not a rich text field"};
    std::set<std::string> names;
    for (const auto& color : colors)
        if (color.name.empty() || !names.insert(color.name).second) return {Status::invalid_value, u"Color names must be non-empty and distinct"};
    it->second.colors = std::move(colors);
    if (invalidate_) invalidate_();
    return {};
}
Query<std::vector<NamedColor>> detail::UiState::colors(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::not_supported, u"Not a rich text field", std::nullopt};
    return {Status::ok, {}, it->second.colors};
}
std::optional<Color> detail::UiState::named_color(std::string_view id, std::string_view name) const {
    auto it = edits_.find(id);
    if (it == edits_.end() || name.empty()) return std::nullopt;
    for (const auto& color : it->second.colors)
        if (color.name == name) return presentation_.variant == ThemeVariant::dark() ? color.dark : color.light;
    return std::nullopt;
}
const std::vector<StyleSpan>* detail::UiState::style_spans(std::string_view id) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.rich ? &it->second.spans : nullptr;
}
std::vector<StyleSpan> detail::UiState::visible_style_spans(std::string_view id) const {
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {};
    const auto& field = it->second;
    const auto& session = field.session;
    if (!session.composing) return field.spans;
    return detail::composition_spans(field.spans, session.begin(), session.end(), static_cast<unsigned>(session.preedit.size()),
                                     insertion_style(field, session.begin(), session.end()));
}
Result detail::UiState::travel(std::string_view id, bool forward) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    auto& from = forward ? field->future : field->past;
    auto& to = forward ? field->past : field->future;
    if (from.empty()) return {Status::empty_history, forward ? u"沒有可重做的編輯" : u"沒有可復原的編輯"};
    auto& session = field->session;
    if (field->connected && !field->submitting && source_matches(*field)) {
        field->origin = field->source_value;
        field->origin_spans = field->source_spans;
    }
    to.push_back({session.text, session.selection, field->spans});
    auto snapshot = std::move(from.back());
    from.pop_back();
    if (session.text != snapshot.text || field->spans != snapshot.spans) ++session.revision;
    session.text = std::move(snapshot.text);
    field->spans = std::move(snapshot.spans);
    field->typing.reset();
    session.select(snapshot.selection.anchor, snapshot.selection.caret);
    return finish_edit(id, *field);
}
Result detail::UiState::submit_field(std::string_view id, Field& field,
        const std::function<Result(const Change<TextHandle>&)>& write, bool command) {
    auto change = Change<TextHandle>{TextHandle(this, lifetime_, Id(id)), field.session.text, field.origin};
    field.submitting = true;
    if (command) invoking_ = true;
    Result result;
    try { result = write(change); }
    catch (...) {
        field.submitting = false;
        if (command) invoking_ = false;
        stopped_ = true;
        return {Status::failed, u"Modification failed"};
    }
    if (command) invoking_ = false;
    field.submitting = false;
    if (result) { field.origin = change.value; field.origin_spans = field.spans; set_error(id, u""); }
    else { set_error(id, result.message); if (command) focus(id); }
    auto notification = notify();
    return notification ? result : notification;
}
Result detail::UiState::finish_edit(std::string_view id, Field& field) {
    OperationScope operation(this);
    if (field.immediate && !field.submitting) return submit_field(id, field, field.immediate, false);
    return notify();
}
Result detail::UiState::undo(std::string_view id) {
    OperationScope operation(this); return travel(id, false); }
Result detail::UiState::redo(std::string_view id) {
    OperationScope operation(this); return travel(id, true); }
Result detail::UiState::notify() {
    OperationScope operation(this);
    dispatching_ = true;
    try { if (changed_) changed_(); }
    catch (...) { stopped_ = true; dispatching_ = false; return {Status::failed, u"資料更新失敗，介面已停止"}; }
    dispatching_ = false;
    return {};
}
Result detail::UiState::text_input(std::u16string_view text) {
    OperationScope operation(this);
    if (menu_) return menu_text(text);  // quick-key letters
    auto committed = without_controls(text);
    if (committed.empty()) return {};
    Field* field = nullptr;
    if (auto result = writable(focus_, field); !result) return result;
    return replace_selection(focus_, *field, committed);
}
std::u16string paste_as_single_line(std::u16string_view text) {
    std::u16string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        result += text[i] == u'\r' || text[i] == u'\n' ? u' ' : text[i];
    }
    return result;
}
std::u16string paste_as_multi_line(std::u16string_view text) {
    std::u16string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        result += text[i] == u'\r' ? u'\n' : text[i];
    }
    return result;
}
Result detail::UiState::paste(std::string_view id, std::u16string_view text) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    return replace_selection(id, *field, find(id)->multiline ? paste_as_multi_line(text) : paste_as_single_line(text));
}
Result detail::UiState::copy(std::string_view id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    const auto& session = it->second.session;
    if (session.begin() == session.end()) return {};
    auto selected = std::u16string(session.text, session.begin(), session.end() - session.begin());
    if (find(id)->multiline) {  // the clipboard carries CRLF, as other Windows text does
        std::u16string crlf;
        for (auto ch : selected) { if (ch == u'\n') crlf += u'\r'; crlf += ch; }
        selected = std::move(crlf);
    }
    // A rich field also hands over the styles of the selection, beside the plain text.
    const bool written = it->second.rich && clipboard_
        ? clipboard_->write_rich(selected, detail::serialize_rich_payload(RichText{std::u16string(session.text, session.begin(), session.end() - session.begin()),
                                                                                    detail::slice_spans(it->second.spans, session.begin(), session.end())}))
        : clipboard_ && clipboard_->write(selected);
    if (!written) return {Status::failed, u"無法寫入剪貼簿"};
    return {};
}
// A paste from the clipboard: a rich field takes the styles of a payload that belongs to the plain text it
// came with; anything else (another program's text, a payload left from another copy, a plain field) is plain text.
Result detail::UiState::paste_clipboard(std::string_view id) {
    OperationScope operation(this);
    if (!clipboard_) return {Status::not_supported, u"剪貼簿不可用"};
    auto content = clipboard_->read();
    if (!content) return {content.status, content.message};
    if (is_rich(id)) {
        auto payload = clipboard_->read_rich();
        auto rich = payload ? detail::parse_rich_payload(*payload) : std::nullopt;
        if (rich && rich->text == paste_as_multi_line(*content)) {
            Field* field = nullptr;
            if (auto result = writable(id, field); !result) return result;
            return replace_range(id, *field, field->session.begin(), field->session.end(), rich->text, &rich->spans);
        }
    }
    return paste(id, *content);
}
Result detail::UiState::cut(std::string_view id) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.begin() == field->session.end()) return {};
    if (auto result = copy(id); !result) return result;
    return replace_selection(id, *field, u"");
}
float detail::UiState::text_offset(std::string_view id) const {
    auto it = edits_.find(id);
    return it == edits_.end() ? 0.f : it->second.offset;
}
void detail::UiState::set_text_offset(std::string_view id, float offset) {
    OperationScope operation(this);
    if (auto it = edits_.find(id); it != edits_.end()) it->second.offset = std::max(0.f, offset);
}
unsigned detail::UiState::text_anchor(std::string_view id, int text_size) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.offset_text_size && it->second.offset_text_size != text_size
        ? it->second.offset_anchor : 0;
}
void detail::UiState::set_text_anchor(std::string_view id, int text_size, unsigned anchor) {
    if (auto it = edits_.find(id); it != edits_.end()) { it->second.offset_text_size = text_size; it->second.offset_anchor = anchor; }
}
const FieldView* detail::UiState::field_view(std::string_view id) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.multi.placed ? &it->second.multi.view : nullptr;
}
void detail::UiState::settle_lines(std::string_view id, FieldView& view, int text_size, int width) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || view.lines.empty()) return;
    auto& f = it->second;
    auto& m = f.multi;
    const auto& session = f.session;
    auto text = session.visible();
    const auto count = static_cast<unsigned>(view.lines.size());
    const unsigned most = count > view.visible ? count - view.visible : 0;
    auto styles = visible_style_spans(id);
    const bool relaid = m.text != text || m.size != text_size || m.width != width || m.styles != styles || m.revision != layout_revision_;
    unsigned first = m.first;
    // A new wrap (resize, text size, edit) keeps the same text on top: the anchor is a text offset.
    if (m.placed && relaid) { view.upstream = false; first = view.line_of(std::min<unsigned>(m.anchor, static_cast<unsigned>(text.size()))); }
    first = std::min(first, most);
    const unsigned caret = session.visible_caret();
    const bool upstream = session.caret_upstream && !session.composing;
    view.upstream = upstream;
    // A caret that moved since the last arrange scrolls itself into view; scrolling alone never does.
    if (caret != m.caret || upstream != m.upstream) {
        auto line = view.line_of(caret);
        if (line < first) first = line;
        else if (line >= first + view.visible) first = line + 1 - view.visible;
    }
    if (m.pending_first) { first = *m.pending_first; m.pending_first.reset(); }
    first = std::min(first, most);
    view.first = first;
    m.view = view;
    m.first = first; m.anchor = view.lines[first].begin; m.caret = caret; m.upstream = upstream; m.placed = true;
    if (relaid) { m.text = std::move(text); m.styles = std::move(styles); m.size = text_size; m.width = width; m.revision = layout_revision_; }
    m.scroll.offset = static_cast<int>(first) * view.row_height;
}
bool detail::UiState::scroll_lines_to(std::string_view id, unsigned first) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.multi.placed) return false;
    auto& m = it->second.multi;
    const auto count = static_cast<unsigned>(m.view.lines.size());
    first = std::min(first, count > m.view.visible ? count - m.view.visible : 0);
    if (first == m.first) return false;
    m.first = m.view.first = first;
    m.anchor = m.view.lines[first].begin;
    m.scroll.offset = static_cast<int>(first) * m.view.row_height;
    if (invalidate_) invalidate_();
    return true;
}
Result detail::UiState::select_pointer(std::string_view id, unsigned anchor, unsigned caret, bool upstream) {
    OperationScope operation(this);
    if (auto result = select(id, anchor, caret); !result) return result;
    auto& session = edits_.find(id)->second.session;
    session.caret_upstream = upstream && session.selection.caret == caret;
    return {};
}
Result detail::UiState::select(std::string_view id, unsigned anchor, unsigned caret) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (it->second.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    const auto& text = it->second.session.text;
    auto boundary = [&](unsigned pos) {
        return pos <= text.size() && !(pos > 0 && pos < text.size() &&
            text[pos] >= 0xdc00 && text[pos] <= 0xdfff && text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff);
    };
    if (!boundary(anchor) || !boundary(caret)) return {Status::invalid_value, {}};
    it->second.session.select(anchor, caret);
    it->second.drag = {};
    return {};
}
// ---- Words, paragraphs and multi-click (#83) --------------------------------------------------
Result detail::UiState::set_word_segmentation(std::string_view id, WordSegmentation mode) {
    OperationScope operation(this);
    if (auto r = configuration_access(id); !r) return r;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_supported, u"Not a text field"};
    if (mode != WordSegmentation::dictionary && mode != WordSegmentation::per_character && mode != WordSegmentation::script_run)
        return {Status::invalid_value, u"Unknown word segmentation"};
    it->second.segmentation = mode;
    it->second.drag = {};
    return {};
}
Query<WordSegmentation> detail::UiState::word_segmentation(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_supported, u"Not a text field", std::nullopt};
    return {Status::ok, {}, it->second.segmentation};
}
Query<WordSegmentation> detail::UiState::word_segmentation_in_use(std::string_view id) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_supported, u"Not a text field", std::nullopt};
    return {Status::ok, {}, segmentation_in_use(it->second.segmentation, dictionary())};
}
Query<TextSpan> detail::UiState::word_at(std::string_view id, unsigned offset) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_supported, u"Not a text field", std::nullopt};
    const auto& text = it->second.session.text;
    if (offset > text.size()) return {Status::invalid_value, u"Offset is past the end of the text", std::nullopt};
    return {Status::ok, {}, word_span(text, words_of(it->second), offset)};
}
Query<TextSpan> detail::UiState::paragraph_at(std::string_view id, unsigned offset) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_supported, u"Not a text field", std::nullopt};
    const auto& text = it->second.session.text;
    if (offset > text.size()) return {Status::invalid_value, u"Offset is past the end of the text", std::nullopt};
    return {Status::ok, {}, paragraph_span(text, offset)};
}
Query<unsigned> detail::UiState::word_stop(std::string_view id, unsigned offset, bool forward) const {
    if (auto r = configuration_access(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_supported, u"Not a text field", std::nullopt};
    const auto& text = it->second.session.text;
    if (offset > text.size()) return {Status::invalid_value, u"Offset is past the end of the text", std::nullopt};
    return {Status::ok, {}, next_word_stop(text, words_of(it->second), offset, forward)};
}
std::vector<unsigned> detail::UiState::word_unit_boundaries(std::string_view id) const {
    auto it = edits_.find(id);
    if (it == edits_.end()) return {};
    return word_unit_bounds(it->second.session.text, words_of(it->second));
}
std::vector<unsigned> detail::UiState::paragraph_boundaries(std::string_view id) const {
    auto it = edits_.find(id);
    return it == edits_.end() ? std::vector<unsigned>{} : paragraph_bounds(it->second.session.text);
}
Result detail::UiState::select_unit(std::string_view id, unsigned offset, Field::UnitDrag::Unit unit, bool arm) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    auto& field = it->second;
    if (field.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    const auto& text = field.session.text;
    if (offset > text.size()) return {Status::invalid_value, u"Offset is past the end of the text"};
    const bool word = unit == Field::UnitDrag::Unit::word;
    const TextSpan span = word ? word_span(text, words_of(field), offset) : paragraph_span(text, offset);
    field.session.select(span.begin, span.end);
    field.drag = {};
    if (arm) field.drag = {unit, span, field.session.selection};
    return {};
}
Result detail::UiState::select_word(std::string_view id, unsigned offset) { return select_unit(id, offset, Field::UnitDrag::Unit::word, false); }
Result detail::UiState::select_paragraph(std::string_view id, unsigned offset) { return select_unit(id, offset, Field::UnitDrag::Unit::paragraph, false); }
Result detail::UiState::double_click(std::string_view id, unsigned offset) { return select_unit(id, offset, Field::UnitDrag::Unit::word, true); }
Result detail::UiState::triple_click(std::string_view id, unsigned offset) { return select_unit(id, offset, Field::UnitDrag::Unit::paragraph, true); }
unsigned detail::UiState::count_click(const Id& id, int x, int y) {
    if (!click_clock_) return 1;
    const std::uint64_t now = click_clock_();
    const bool continues = click_run_.count > 0 && click_run_.count < 3 && click_run_.id == id && now >= click_run_.time &&
        now - click_run_.time <= click_settings_.interval_ms &&
        std::abs(x - click_run_.x) <= click_settings_.width / 2 && std::abs(y - click_run_.y) <= click_settings_.height / 2;
    if (continues) { ++click_run_.count; click_run_.time = now; }
    else click_run_ = {id, x, y, now, 1};
    return click_run_.count;
}
Result detail::UiState::text_pointer_down(std::string_view id, TextHit caret, unsigned cell, int x, int y, bool extend) {
    OperationScope operation(this);
    const unsigned count = count_click(Id(id), x, y);
    if (count == 2) return double_click(id, cell);
    if (count == 3) return triple_click(id, cell);
    auto it = edits_.find(id);
    const unsigned anchor = extend && it != edits_.end() ? it->second.session.selection.anchor : caret.offset;
    return select_pointer(id, anchor, caret.offset, caret.upstream);
}
Result detail::UiState::text_pointer_drag(std::string_view id, TextHit caret, unsigned cell) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    auto& field = it->second;
    auto& drag = field.drag;
    const auto& selection = field.session.selection;
    const bool armed = drag.unit != Field::UnitDrag::Unit::character && drag.last.anchor == selection.anchor && drag.last.caret == selection.caret;
    if (!armed) return select_pointer(id, selection.anchor, caret.offset, caret.upstream);
    if (auto result = available(id); !result) return result;
    if (field.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    const auto& text = field.session.text;
    cell = std::min<unsigned>(cell, static_cast<unsigned>(text.size()));
    const TextSpan span = drag.unit == Field::UnitDrag::Unit::word ? word_span(text, words_of(field), cell) : paragraph_span(text, cell);
    // The unit first selected always stays inside the selection; the pointer decides which side grows.
    const bool before = span.begin < drag.origin.begin;
    const auto kept = drag;
    field.session.select(before ? kept.origin.end : kept.origin.begin, before ? span.begin : std::max(span.end, kept.origin.end));
    field.drag = kept;
    field.drag.last = field.session.selection;
    return {};
}
void detail::UiState::end_text_drag() {
    OperationScope operation(this);
    for (auto& [id, field] : edits_) field.drag = {};
}
Result detail::UiState::invoke(std::string_view id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    if (dispatching_ || invoking_) return {Status::busy, u"操作尚未結束"};
    for (const auto& [key, field] : edits_)
        if (field.session.composing) return {Status::busy, u"請先完成文字組字"};
    auto it = actions_.find(id);
    if (it == actions_.end()) return {Status::not_supported, u"元件沒有此動作"};
    invoking_ = true;
    executing_ = id;
    try { auto result = it->second(); invoking_ = false; executing_.clear(); return result; }
    catch (...) { stopped_ = true; invoking_ = false; executing_.clear(); return {Status::failed, u"操作失敗，介面已停止"}; }
}
void detail::UiState::bind(Id id, Action action) {
    OperationScope operation(this);
    if (!find(id) || !action) throw std::invalid_argument("Action requires a known ID and a callback");
    actions_.insert_or_assign(std::move(id), std::move(action));
}
Result detail::UiState::focusable(const Control& control) const {
    if (auto result = available(control.id); !result) return result;
    auto scroll = text_scrolls_.find(control.id);
    if (!component_type(control.kind).can_focus() && (scroll == text_scrolls_.end() || scroll->second.maximum <= 0)) return {Status::not_supported, u"標籤不能取得焦點"};
    return {};
}
void detail::UiState::set_scrollable_text(std::string id, ScrollState& state, int maximum, int line_height) {
    text_scrolls_.insert_or_assign(std::move(id), TextScroll{&state, maximum, std::max(1, line_height)});
}
void detail::UiState::set_vertical_scroll(const Id& id, int maximum, int column_width) {
    text_scrolls_.insert_or_assign(id, TextScroll{&vertical_scrolls_[id], maximum, std::max(1, column_width), true});
}
void detail::UiState::clear_vertical_scroll(const Id& id) {
    if (auto it = text_scrolls_.find(id); it != text_scrolls_.end() && it->second.horizontal) text_scrolls_.erase(it);
}
std::vector<Id> detail::UiState::focus_order() const {
    std::vector<Id> order;
    for (const auto& control : controls_)
        if (focusable(control)) order.push_back(control.id);
    return order;
}
// The new focus is readable at once; owner notifications queue behind the one being delivered
// (ADR 0003: no direct recursion into subscribers).
Result detail::UiState::move_focus(Id next) {
    OperationScope operation(this);
    if (next == focus_) return {};
    if (!composing_.empty() && before_focus_) before_focus_();
    if (!popup_source_.empty() && next != popup_source_) close_popup();
    if (menu_ && next != menu_->target) close_menu();
    key_armed_.clear(); pointer_armed_.clear(); end_slider_drag(false);
    if (!composing_.empty()) {
        // E01 default, matching the native IME on blur: leftover preedit is committed once.
        auto preedit = edits_.at(composing_).session.preedit;
        composition_commit(preedit);
    }
    auto previous = std::exchange(focus_, std::move(next));
    refresh_tooltips();
    if (auto it = lists_.find(focus_); it != lists_.end()) {
        auto& f = it->second; auto selected = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        auto valid = [&](const auto& item) { return item.enabled && selected && item.value == *selected; };
        auto target = std::find_if(f.items.begin(), f.items.end(), valid);
        if (target == f.items.end()) target = std::find_if(f.items.begin(), f.items.end(), [](const auto& item) { return item.enabled; });
        f.cursor = target == f.items.end() ? std::optional<OptionValue>{} : std::optional<OptionValue>{target->value};
    }
    if (!focus_moved_ && blur_callbacks_.empty()) return {};
    pending_focus_.emplace_back(std::move(previous), focus_);
    if (notifying_focus_) return {};
    notifying_focus_ = true;
    Result result;
    for (std::size_t i = 0; i < pending_focus_.size(); ++i) {
        auto [from, to] = pending_focus_[i];
        try {
            if (auto it = blur_callbacks_.find(from); it != blur_callbacks_.end()) {
                auto callback = it->second;
                callback();
            }
            if (focus_moved_) focus_moved_(from, to);
        }
        catch (...) { stopped_ = true; result = {Status::failed, u"焦點通知失敗，介面已停止"}; break; }
    }
    pending_focus_.clear();
    notifying_focus_ = false;
    return result;
}
Result detail::UiState::composition_update(std::u16string preedit, unsigned caret) {
    OperationScope operation(this);
    if (!composing_.empty()) if (auto result = available(composing_); !result) return result;
    if (composing_.empty()) {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        composing_ = focus_;
    }
    edits_.at(composing_).session.update_composition(std::move(preedit), caret);
    return {};
}
Result detail::UiState::composition_commit(std::u16string_view text) {
    OperationScope operation(this);
    auto target = composing_.empty() ? focus_ : composing_;
    if (auto result = available(target); !result) return result;
    composition_cancel();
    if (!edits_.contains(target)) return {Status::invalid_value, u"沒有接收組字的文字欄位"};
    auto committed = without_controls(text);
    Field* field = nullptr;
    if (auto result = writable(target, field); !result) return result;
    if (committed.empty()) return {};
    return replace_selection(target, *field, committed);
}
Result detail::UiState::composition_cancel() {
    OperationScope operation(this);
    if (composing_.empty()) return {};
    edits_.at(composing_).session.cancel_composition();
    composing_.clear();
    return {};
}
Result detail::UiState::focus(std::string_view id) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (auto result = available(id); !result) return result;
    if (auto result = focusable(*control); !result) return result;
    refresh_tooltips();
    return move_focus(control->id);
}
Result detail::UiState::add_shortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope) {
    OperationScope operation(this);
    auto target_id = layout_id(target);
    auto scope_id = layout_id(scope);
    if (!target_id || !find(*target_id) || (!std::holds_alternative<std::monostate>(scope) && (!scope_id || !find(*scope_id))) ||
        chord.key == Key::tab || chord.key == Key::escape || (chord.key == Key::other && !chord.letter))
        return {Status::invalid_value, u"Invalid shortcut"};
    if (auto r = configuration_access(*target_id); !r) return r;
    if (chord.letter && !chord.ctrl) return {Status::invalid_value, u"Letter shortcuts require Ctrl"};
    Id scope_name = scope_id.value_or(Id{});
    for (const auto& entry : shortcuts_)
        if (entry.scope == scope_name && entry.chord.key == chord.key && entry.chord.letter == chord.letter && entry.chord.ctrl == chord.ctrl && entry.chord.shift == chord.shift)
            return {Status::invalid_value, u"Duplicate shortcut"};
    shortcuts_.push_back({chord, *target_id, std::move(scope_name)});
    return {};
}
Result detail::UiState::key_down(KeyPress key) {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (renderer_failed_) return {Status::renderer_failed, u"Renderer failed"};
    // A key that uses the keyboard shows the focus indicator. A modifier on its own does not (code review 2026-11-12,
    // item 2): Ctrl chords, a lone Alt, and the F10 that only closes an open menu all leave the state alone.
    const bool modifier_only = key.ctrl || key.key == Key::alt || (key.key == Key::f10 && menu_);
    if (!modifier_only && !keyboard_cues_) { keyboard_cues_ = true; focus_cues_changed(); }
    if (!composing_.empty()) return {Status::busy, u"Composition in progress"};
    if (menu_) return menu_key_down(key);  // an open context menu owns the keyboard
    if (key.key == Key::escape && !key.repeat) {
        for (auto& [id, tip] : tooltips_) {
            if (tip.visible) {
                tip.visible = false; tip.dismissed = true;
                if (invalidate_) invalidate_();
                return {};
            }
        }
    }
    if (key.key == Key::escape && !popup_source_.empty()) { if (!key.repeat) close_popup(); return {}; }
    if (modal_ && !active_dialog_.empty() && key.key == Key::escape) {
        if (key.repeat) return {};
        return settle_dialog(active_dialog_, false);
    }
    if (key.key == Key::tab) {
        auto order = focus_order();
        if (order.empty()) return {Status::not_found, u"沒有可聚焦的元件"};
        auto it = std::find(order.begin(), order.end(), focus_);
        auto n = static_cast<long>(order.size());
        long index = it == order.end() ? (key.shift ? 0 : -1) : static_cast<long>(it - order.begin());
        index = (index + (key.shift ? -1 : 1) + n) % n;
        return move_focus(order[static_cast<std::size_t>(index)]);
    }
    if ((key.key == Key::menu || (key.key == Key::f10 && key.shift)) && !key.ctrl) {
        if (key.repeat || focus_.empty()) return {};
        return open_menu(focus_, std::nullopt, std::nullopt);
    }
    const auto* control = find(focus_);
    if (control) {
        if (auto result = available(focus_); !result) return result;
        if (auto it = text_scrolls_.find(focus_); it != text_scrolls_.end() && it->second.maximum > 0 && !key.ctrl && !key.shift) {
            auto& scroll = it->second;
            int next = scroll.state->offset;
            bool handled = true;
            // Sideways text: the offset counts from the start (right edge), so Left reads on, Right reads back.
            if (scroll.horizontal && key.key == Key::left) next += scroll.line_height;
            else if (scroll.horizontal && key.key == Key::right) next -= scroll.line_height;
            else if (scroll.horizontal && (key.key == Key::up || key.key == Key::down)) handled = false;
            else if (key.key == Key::up) next -= scroll.line_height;
            else if (key.key == Key::down) next += scroll.line_height;
            else if (key.key == Key::home) next = 0;
            else if (key.key == Key::end) next = scroll.maximum;
            else handled = false;
            if (handled) {
                scroll.state->offset = std::clamp(next, 0, scroll.maximum);
                scrolled();
                return {};
            }
        }
        const auto& type = component_type(control->kind);
        if (type.handles_key(*this, *control, key)) return type.key_down(*this, key);
    }
    for (int scoped = 1; scoped >= 0; --scoped) for (const auto& entry : shortcuts_) {
        if ((scoped ? entry.scope == focus_ && !entry.scope.empty() : entry.scope.empty()) && entry.chord.key == key.key && entry.chord.letter == key.letter && entry.chord.ctrl == key.ctrl && entry.chord.shift == key.shift) {
            if (key.repeat) return {};
            if (modal_) {
                const auto* target = find(entry.target);
                if (target && target->page != "dialog") return {Status::blocked_by_modal, u"Dialog is open"};
            }
            return invoke(entry.target);
        }
    }
    return {};
}
namespace {
// Code-point stepping is the S1 support boundary: no grapheme clusters, no RTL.
unsigned step(std::u16string_view text, unsigned pos, int direction) {
    auto low = [&](unsigned i) { return i < text.size() && text[i] >= 0xdc00 && text[i] <= 0xdfff; };
    auto high = [&](unsigned i) { return i < text.size() && text[i] >= 0xd800 && text[i] <= 0xdbff; };
    if (direction < 0) {
        if (pos == 0) return 0;
        --pos;
        return pos && low(pos) && high(pos - 1) ? pos - 1 : pos;
    }
    if (pos >= text.size()) return static_cast<unsigned>(text.size());
    return high(pos) && low(pos + 1) ? pos + 2 : pos + 1;
}
}
Result detail::UiState::edit_key(KeyPress key) {
    OperationScope operation(this);
    auto& session = edits_.at(focus_).session;
    if (session.composing) return {Status::busy, u"組字中，按鍵交由輸入法處理"};
    const auto size = static_cast<unsigned>(session.text.size());
    auto move = [&](unsigned pos) { session.select(key.shift ? session.selection.anchor : pos, pos); return Result{}; };
    const bool multi = find(focus_)->multiline;
    if (multi && key.ctrl && !key.letter && (key.key == Key::home || key.key == Key::end)) return move(key.key == Key::home ? 0 : size);
    // Ctrl+arrows and Ctrl+Backspace/Delete go by words: the stops a double click's words define (#83).
    if (key.ctrl && !key.letter && (key.key == Key::left || key.key == Key::right || key.key == Key::backspace || key.key == Key::del)) {
        const bool forward = key.key == Key::right || key.key == Key::del;
        if (key.key == Key::left || key.key == Key::right)
            return move(next_word_stop(session.text, words_of(edits_.at(focus_)), session.selection.caret, forward));
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        if (session.begin() == session.end())
            session.select(session.selection.caret, next_word_stop(session.text, words_of(*field), session.selection.caret, forward));
        if (session.begin() == session.end()) return {};
        return replace_selection(focus_, *field, u"");
    }
    if (key.ctrl) {
        if (key.letter == u'Z') return travel(focus_, key.shift);
        if (key.letter == u'Y') return travel(focus_, true);
        if (key.letter == u'A') { session.select(0, size); return {}; }
        if (key.letter == u'C') return copy(focus_);
        if (key.letter == u'X') return cut(focus_);
        if (is_rich(focus_) && (key.letter == u'B' || key.letter == u'I' || key.letter == u'U'))
            return toggle_style(focus_, key.letter == u'B' ? StyleAttribute::bold : key.letter == u'I' ? StyleAttribute::italic : StyleAttribute::underline);
        if (key.letter == u'V') return paste_clipboard(focus_);
        return {};
    }
    switch (key.key) {
    case Key::left:
    case Key::right: {
        int direction = key.key == Key::left ? -1 : 1;
        if (!key.shift && session.begin() != session.end()) return move(direction < 0 ? session.begin() : session.end());
        return move(step(session.text, session.selection.caret, direction));
    }
    case Key::home: return multi ? move_line_edge(focus_, false, key.shift) : move(0);
    case Key::end: return multi ? move_line_edge(focus_, true, key.shift) : move(size);
    case Key::up: case Key::down:
        return multi ? move_lines(focus_, key.key == Key::up ? -1 : 1, 1, key.shift) : Result{};
    case Key::page_up: case Key::page_down:
        return multi ? move_lines(focus_, key.key == Key::page_up ? -1 : 1, find(focus_)->visible_lines, key.shift) : Result{};
    case Key::enter: {
        if (!find(focus_)->multiline) return {};
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        return replace_selection(focus_, *field, u"\n");
    }
    case Key::backspace:
    case Key::del: {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        if (session.begin() == session.end())
            session.select(session.selection.caret, step(session.text, session.selection.caret, key.key == Key::del ? 1 : -1));
        return delete_selection(focus_);  // the Delete menu item runs the same path
    }
    default: return {};
    }
}
// The lines cursor keys move over: the last arrange's when they still describe the text, else a fresh
// arrange from the host, else the hard lines alone (no wrapping, one pixel per column).
FieldView detail::UiState::vertical_view(const Id& id) {
    auto& field = edits_.find(id)->second;
    auto fresh = [&] {
        return field.multi.placed && field.multi.revision == layout_revision_ && field.multi.text == field.session.visible() &&
               field.multi.styles == visible_style_spans(id);
    };
    if (!fresh() && relayout_) relayout_();
    FieldView view;
    if (fresh()) { view = field.multi.view; view.upstream = field.session.caret_upstream; return view; }
    const auto& text = field.session.text;
    for (std::size_t start = 0;;) {
        auto end = text.find(u'\n', start);
        FieldLine line;
        line.begin = static_cast<unsigned>(start);
        line.end = static_cast<unsigned>(end == std::u16string::npos ? text.size() : end);
        line.run.stops.push_back({0, 0.f});
        for (unsigned i = 1; i <= line.end - line.begin; ++i) line.run.stops.push_back({i, static_cast<float>(i)});
        line.run.width = static_cast<float>(line.end - line.begin);
        view.lines.push_back(std::move(line));
        if (end == std::u16string::npos) break;
        start = end + 1;
    }
    view.visible = find(id)->visible_lines;
    view.upstream = field.session.caret_upstream;
    return view;
}
Result detail::UiState::move_line_edge(const Id& id, bool end, bool shift) {
    OperationScope operation(this);
    auto view = vertical_view(id);
    auto& session = edits_.find(id)->second.session;
    const auto& line = view.lines[view.line_of(session.selection.caret)];
    const unsigned target = end ? line.end : line.begin;
    session.select(shift ? session.selection.anchor : target, target);
    session.caret_upstream = end && line.soft;  // the caret stays on the line it was moved to the end of
    return {};
}
Result detail::UiState::move_lines(const Id& id, int direction, unsigned count, bool shift) {
    OperationScope operation(this);
    auto& field = edits_.find(id)->second;
    auto& session = field.session;
    auto view = vertical_view(id);
    const unsigned from = view.line_of(session.selection.caret);
    const bool real = field.multi.placed && field.multi.text == session.visible();
    // One run of Up/Down presses keeps the column it started at.
    if (field.multi.want_from != session.selection.caret || !real) field.multi.want_x = view.x_in_line(from, session.selection.caret);
    const long target = std::clamp(static_cast<long>(from) + static_cast<long>(direction) * static_cast<long>(count), 0L, static_cast<long>(view.lines.size()) - 1);
    if (target == static_cast<long>(from)) return {};
    auto [offset, upstream] = view.hit_in_line(static_cast<unsigned>(target), field.multi.want_x);
    session.select(shift ? session.selection.anchor : offset, offset);
    session.caret_upstream = upstream;
    field.multi.want_from = real ? offset : ~0u;
    if (count > 1 && real) {  // a page key moves the view as far as the caret moved
        const long moved = target - static_cast<long>(from);
        scroll_lines_to(id, static_cast<unsigned>(std::max(0L, static_cast<long>(field.multi.first) + moved)));
    }
    return {};
}
Result detail::UiState::key_up(Key key) {
    OperationScope operation(this);
    if (key != Key::space || key_armed_.empty() || key_armed_ != focus_) return {};
    auto target = std::exchange(key_armed_, {});
    return invoke(target);
}
Result detail::UiState::pointer_down(std::string_view id, std::optional<std::size_t> option) {
    OperationScope operation(this);
    pointer_pressed();
    if (auto result = available(id); !result) return result;
    const auto* control = find(id);
    if (auto result = focusable(*control); !result) return result;
    if (!popup_source_.empty() && popup_source_ != id) close_popup();
    if (component_type(control->kind).ignores_press(*this, *control, option)) return {};
    end_slider_drag(false);
    const Id target = control->id;
    if (auto result = move_focus(target); !result) return result;
    // Focus notifications can remove, disable, hide, or add controls. Revalidate and
    // reacquire the target before dispatch instead of retaining a vector pointer.
    refresh_tooltips();
    if (auto result = available(target); !result) return result;
    control = find(target);
    component_type(control->kind).pointer_down(*this, *control, option);
    return {};
}
Result detail::UiState::pointer_up(std::string_view id, std::optional<std::size_t> option) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto armed = std::exchange(pointer_armed_, {}); end_slider_drag(true);
    auto pressed_option = std::exchange(pointer_option_, std::nullopt);
    auto pressed_value = std::exchange(pointer_option_value_, std::nullopt);
    if (armed.empty() || armed != id) return {};
    return component_type(find(armed)->kind).pointer_up(*this, {armed, pressed_option, option, pressed_value});
}
void detail::UiState::cancel_input() {
    OperationScope operation(this); end_text_drag(); key_armed_.clear(); pointer_armed_.clear(); pointer_option_.reset(); pointer_option_value_.reset(); end_slider_drag(false); }
Result detail::UiState::set_error(std::string_view id, std::u16string message) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"Component no longer exists"};
    if (!component_type(control->kind).accepts_error()) return {Status::not_supported, u"標籤不能帶有錯誤"};
    if (message.empty()) { if (auto it = errors_.find(id); it != errors_.end()) errors_.erase(it); }
    else errors_.insert_or_assign(control->id, std::move(message));
    return {};
}
void detail::UiState::enable(std::string_view id, bool enabled) {
    OperationScope operation(this);
    auto* control = const_cast<Control*>(find(id));
    if (!control) return;
    control->enabled = enabled;
    if (!enabled && menu_ && menu_->target == id) close_menu();
    if (!enabled && popup_source_ == id) close_popup();
    if (!enabled && focus_ == control->id) move_focus({});
    refresh_tooltips();
}
void detail::UiState::set_read_only(std::string_view id, bool read_only) {
    OperationScope operation(this);
    if (auto* control = const_cast<Control*>(find(id))) control->read_only = read_only;
}
void detail::UiState::check(std::string_view id, bool checked) {
    OperationScope operation(this);
    if (auto* control = const_cast<Control*>(find(id))) control->checked = checked;
}
void detail::UiState::replace_text(std::string_view id, std::u16string value) {
    replace_rich(id, RichText{std::move(value), {}});
}
void detail::UiState::replace_rich(std::string_view id, RichText rich) {
    OperationScope operation(this);
    auto* control = const_cast<Control*>(find(id));
    auto it = edits_.find(id);
    if (!control || it == edits_.end() || !valid_value(*control, rich.text)) throw std::invalid_argument("Invalid owner refresh");
    if (composing_ == control->id) composition_cancel();
    auto& field = it->second;
    field.session.cancel_composition();
    if (field.session.text != rich.text || field.spans != rich.spans) ++field.session.revision;
    field.session.text = std::move(rich.text);
    field.spans = field.rich ? std::move(rich.spans) : std::vector<StyleSpan>{};
    field.typing.reset();
    field.session.select(field.session.selection.anchor, field.session.selection.caret);
    field.past.clear(); field.future.clear(); field.offset = 0; field.offset_text_size = 0; field.offset_anchor = 0;
    field.multi.first = field.multi.anchor = 0; field.multi.caret = ~0u; field.multi.pending_first.reset();  // show the caret afresh
}
Ui::Ui() : state_(std::make_unique<detail::UiState>(std::vector<Control>{})), semantics_(new SemanticTree(*state_)) {}
SemanticTree& Ui::semantics() { return *semantics_; }
Ui::~Ui() = default;
// Controls supplied as data after construction: registered exactly as the constructor registers them.
void detail::UiState::seed_test_controls(std::vector<Control> controls) {
    OperationScope operation(this);
    for (auto& control : controls) {
        const auto& type = detail::component_type(control.kind);
        if (control.id.empty() || find(control.id)) throw std::invalid_argument("Control IDs must be nonempty and unique");
        button_lifetimes_.emplace(control.id, std::make_shared<int>(0));
        controls_.push_back(std::move(control));
        type.adopt(*this, controls_.back());
    }
}
bool detail::UiState::registerable(std::string_view id) {
    const auto* existing = find(id);
    auto overlay = overlay_ids_.find(id);
    if (!id.empty() && !existing && overlay == overlay_ids_.end()) return true;
#ifndef NDEBUG
    const auto* registered = existing ? find_component_type(existing->kind) : nullptr;
    std::string type = existing ? std::string(registered ? registered->name() : "custom") : overlay != overlay_ids_.end() ? overlay->second : "none";
    std::string message = "Invalid component ID '" + std::string(id) + "'; existing component type: " + type;
    diagnostics_.push_back(message);
    output_diagnostic(message);
#endif
    return false;
}
Result detail::UiState::creation_access() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (renderer_failed_) return {Status::renderer_failed, u"Renderer failed"};
    return {};
}
const std::vector<std::string>& Ui::diagnostics() const { return state_->diagnostics_; }
Query<TextHandle> Ui::add_text(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_text(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<ButtonHandle> Ui::add_button(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_button(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<LabelHandle> Ui::add_label(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_label(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<CheckboxHandle> Ui::add_checkbox(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_checkbox(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<RadioGroupHandle> Ui::add_radio_group(Control control, std::vector<RadioOption> options) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_radio_group(std::move(control), std::move(options))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<SliderHandle> Ui::add_slider(Control control, SliderRange range) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    if (range.minimum >= range.maximum || range.step <= 0) return {Status::invalid_value, u"Invalid slider range", std::nullopt};
    try { return {Status::ok, {}, state_->add_slider(std::move(control), range)}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<ProgressHandle> Ui::add_progress(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_progress(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<TabsHandle> Ui::add_tabs(Control control, std::vector<TabPage> pages) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_tabs(std::move(control), std::move(pages))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<NavigationListHandle> Ui::add_navigation_list(Control control, std::vector<NavigationPage> pages, OptionValue initial) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_navigation_list(std::move(control), std::move(pages), std::move(initial))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<DropdownHandle> Ui::add_dropdown(Control control, std::vector<RadioOption> options) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_dropdown(std::move(control), std::move(options))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<ListHandle> Ui::add_list(Control control, std::vector<RadioOption> items) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (!valid_text_flow(control)) return {Status::invalid_value, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writing_support(control); !support) return {support.status, support.message, std::nullopt};
    try { return {Status::ok, {}, state_->add_list(std::move(control), std::move(items))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<TooltipHandle> Ui::add_tooltip(std::string id, ComponentHandle source, std::u16string text, unsigned delay_ms) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try {
        auto source_id = state_->layout_id(source);
        auto handle = state_->add_tooltip(std::move(source), std::move(text), delay_ms);
        state_->tooltip_ids_.emplace(*source_id, id);
        state_->overlay_ids_.emplace(std::move(id), "tooltip");
        return {Status::ok, {}, std::move(handle)};
    } catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid tooltip", std::nullopt}; }
}
Query<DialogHandle> Ui::add_dialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial, ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try {
        auto handle = state_->add_dialog(id, std::move(members), initial, fallback, std::move(result), std::move(name));
        state_->overlay_ids_.emplace(std::move(id), "dialog");
        return {Status::ok, {}, std::move(handle)};
    } catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid dialog", std::nullopt}; }
}
Result Ui::add_shortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope) {
    return state_->add_shortcut(chord, std::move(target), std::move(scope));
}
Result Ui::connect_mixed_confirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<MixedConfirmationResult(const std::vector<FieldChange>&)> submit) {
    return state_->connect_mixed_confirmation(std::move(button), std::move(fields), std::move(submit));
}
void Ui::on_change(std::function<void()> callback) { state_->on_change(std::move(callback)); }
Result Ui::set_presentation(const PresentationSettings& settings) { return state_->set_presentation(settings); }
Query<bool> Ui::animating() const { return state_->animating(); }
Query<std::optional<HoverTarget>> Ui::hover() const { return state_->hover_query(); }
Result Ui::simulate_hover(const ComponentHandle& target, std::optional<OptionValue> item) {
    auto id = state_->layout_id(target);
    if (!id) return {Status::not_found, u"Component no longer exists"};
    return state_->simulate_hover(*id, item);
}
Result Ui::clear_simulated_hover() { return state_->clear_simulated_hover(); }
const PresentationSettings& Ui::presentation() const { return state_->presentation(); }
Result Ui::set_palette_overrides(std::vector<PaletteOverride> overrides) { return state_->set_palette_overrides(std::move(overrides)); }
const std::vector<PaletteOverride>& Ui::palette_overrides() const { return state_->palette_overrides(); }
Query<Palette> Ui::effective_palette(const Appearance& appearance, const ThemeVariant& variant) const {
    if (!appearance.supports(variant)) return {Status::invalid_value, u"Appearance does not support this theme variant", {}};
    return {Status::ok, {}, state_->effective_palette(appearance, variant)};
}
void Ui::bind_appearance(const Appearance* appearance) { state_->bind_appearance(appearance); }

bool Ui::focus_indicator_visible() const { return state_->focus_indicator_visible(); }
detail::UiState& detail::access(Ui& ui) { return *ui.state_; }
const detail::UiState& detail::access(const Ui& ui) { return *ui.state_; }

}

namespace lcars {
void detail::UiState::semantic_frame(const Frame& frame) {
    OperationScope operation(this);
    semantic_bounds_.clear(); semantic_options_.clear(); semantic_order_.clear(); semantic_positions_.clear();
    auto store = [&](const Placement& p) {
        auto rect = intersect(p.bounds, p.clip);
        // An open dropdown panel repeats its dropdown's id; only its option rectangles are kept.
        // Modal dialog members are popups too, but they are ordinary components with their own bounds.
        if (!p.dropdown_panel && rect.w > 0 && rect.h > 0) semantic_bounds_[p.id] = rect;
        if (!p.dropdown_panel) { semantic_order_.push_back(p.id); semantic_positions_[p.id] = p.bounds; }
        auto& options = semantic_options_[p.id];
        if (p.dropdown_panel) {
            options.assign(dropdowns_.at(p.id).options.size(), {});
            for (std::size_t i=0; i<p.options.size(); ++i) options[p.option_start+i] = intersect(p.options[i], p.clip);
        } else for (auto option : p.options) options.push_back(intersect(option, p.clip));
        for (const auto& control : controls_) {
            if (control.target != p.id) continue;
            const std::vector<Line>* lines = nullptr;
            if (control.role == TextRole::name) lines = &p.label_lines;
            else if (control.role == TextRole::description) lines = &p.description_lines;
            else if (control.role == TextRole::error) lines = &p.error_lines;
            if (!lines) continue;
            for (const auto& line : *lines) {
                auto r = intersect(line.bounds, p.clip);
                if (r.w <= 0 || r.h <= 0) continue;
                auto it = semantic_bounds_.find(control.id);
                if (it == semantic_bounds_.end()) semantic_bounds_[control.id] = r;
                else {
                    auto old = it->second; int x = std::min(old.x, r.x), y = std::min(old.y, r.y);
                    it->second = {x, y, std::max(old.x + old.w, r.x + r.w) - x, std::max(old.y + old.h, r.y + r.h) - y};
                }
            }
        }
    };
    for (const auto& p : frame.items) store(p);
    for (const auto& p : frame.overlays) {
        if (p.tooltip) {
            auto id = tooltip_ids_.find(p.id);
            if (id != tooltip_ids_.end()) { auto r = intersect(p.bounds, p.clip); if (r.w > 0 && r.h > 0) semantic_bounds_[id->second] = r; }
        } else if (!p.menu) store(p);
    }
    menu_frame(frame);
    if (frame.modal && !active_dialog_.empty()) semantic_bounds_[active_dialog_] = frame.modal_panel;
#ifndef NDEBUG
    for (const auto& id : semantic_order_) {
        auto c = find(id); auto s = describe(id);
        if (c && s && component_type(c->kind).needs_name() && s->name.empty() && diagnosed_names_.insert(id).second)
        {
            auto message = id + " is focusable but has no name";
            diagnostics_.push_back(message);
            output_diagnostic(message);
        }
    }
#endif
}
std::string detail::UiState::semantic_page(std::string_view id) const {
    auto c = find(id); if (!c) return {};
    if (c->page == "dialog") return "modal";
    auto it = page_members_.find(id);
    if (it == page_members_.end()) return "shared";
    return tabs_.at(it->second.first).pages[it->second.second].value.id;
}
Query<SemanticNode> SemanticTree::get(const NodeRef& ref) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrong_thread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (auto menu = u.menu_node(ref)) return *menu;
    auto c = u.find(ref.id);
    if (!c) {
        auto overlay = u.overlay_ids_.find(ref.id);
        if (overlay == u.overlay_ids_.end() || ref.item) return {Status::not_found, {}, {}};
        SemanticNode n; n.ref = ref; n.role = overlay->second == "dialog" ? Role::dialog : Role::tooltip;
        auto [instance, inserted] = u.semantic_instances_.try_emplace(ref.id, 0);
        if (inserted) instance->second = ++u.next_semantic_instance_;
        n.instance = instance->second;
        if (n.role == Role::dialog) {
            n.hidden = !u.modal_ || u.active_dialog_ != ref.id;
            const auto& label = u.dialogs_.at(ref.id).name_label;
            if (!label.empty() && u.find(label)) { n.name = u.text(label); n.labelled_by.push_back({label}); }
        }
        else for (const auto& [source, id] : u.tooltip_ids_) if (id == ref.id) {
            auto tip = u.tooltips_.find(source);
            if (tip != u.tooltips_.end()) { n.name = tip->second.text; n.hidden = !tip->second.visible; }
        }
        auto bounds = u.semantic_bounds_.find(ref.id);
        if (!n.hidden && bounds != u.semantic_bounds_.end()) { n.bounds = bounds->second; n.visible = true; }
        if (n.role == Role::dialog && !n.hidden && !u.renderer_failed_) n.verbs.push_back(Verb::cancel);
        return {Status::ok, {}, n};
    }
    if (u.removal_pending(ref.id)) return {Status::not_found, {}, {}};
    auto s = *u.describe(ref.id); SemanticNode n; n.ref = ref; n.role = detail::component_type(c->kind).role();
    auto [instance, inserted] = u.semantic_instances_.try_emplace(ref.id, 0);
    if (inserted) instance->second = ++u.next_semantic_instance_;
    n.instance = instance->second;
    n.name = s.name; n.value = s.value; n.description = s.description; n.error = s.error;
    if (n.role == Role::label) n.name = s.value;
    // While a context menu is open, keyboard focus belongs to its items (the component keeps core focus).
    n.enabled = s.enabled; n.read_only = s.read_only; n.focusable = s.focusable; n.focused = s.focused && !u.menu_;
    n.expanded = s.expanded; n.invalid = s.invalid; n.executing = s.executing; n.checked = s.checked;
    n.range = s.slider_range; n.numeric_value = s.numeric_value;
    n.hidden = u.available(ref.id, true).status == Status::hidden;
    n.blocked_by_modal = u.modal_ && c->page != "dialog";
    if (!n.hidden) { auto bounds = u.semantic_bounds_.find(ref.id); if (bounds != u.semantic_bounds_.end()) n.bounds = bounds->second; }
    n.visible = n.bounds.has_value();
    for (const auto& id : s.labels) { auto label = u.find(id); if (label && label->role == TextRole::name) n.labelled_by.push_back({id}); else if (label && (label->role == TextRole::description || label->role == TextRole::error)) n.described_by.push_back({id}); }
    n.text_align = s.text_align; n.justify_method = s.justify_method; n.line_spacing = s.line_spacing; n.paragraph_spacing = s.paragraph_spacing;
    detail::component_type(c->kind).describe_node(u, *c, s, ref, n);
    if (ref.item) {
        auto it = std::find_if(s.options.begin(), s.options.end(), [&](const auto& o) { return o.value == *ref.item; });
        if (it == s.options.end()) return {Status::not_found, {}, {}};
        if (auto item_role = detail::component_type(c->kind).item_role()) n.role = *item_role;
        else return {Status::not_found, {}, {}};
        n.name = it->label; n.value.clear(); n.selected = it->selected; n.enabled = s.enabled && it->enabled; n.invalid = it->invalid;
        n.focused = n.role == Role::list_item && s.focused && !u.menu_ && s.candidate && *s.candidate == *ref.item;
        n.bounds.reset(); auto pos = static_cast<size_t>(it - s.options.begin()); auto rects = u.semantic_options_.find(ref.id);
        if (!n.hidden && (!detail::component_type(c->kind).opens_popup() || s.expanded) && rects != u.semantic_options_.end() && pos < rects->second.size()) { auto r = rects->second[pos]; if (r.w > 0 && r.h > 0) n.bounds = r; }
        n.visible = n.bounds.has_value();
    }
    if (n.enabled && !n.hidden && !n.blocked_by_modal) {
        const auto& type = detail::component_type(c->kind);
        type.verbs({u, *c, s, n, ref.item.has_value()}, n.verbs);
        if (!ref.item && type.confirm_pending(u, *c) && !n.read_only) n.verbs.push_back(Verb::confirm);
    }
    return {Status::ok, {}, n};
}
Query<std::vector<NodeRef>> SemanticTree::children(const NodeRef& parent) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrong_thread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (auto menu = u.menu_children(parent)) return {Status::ok, {}, *menu};
    std::vector<NodeRef> result;
    if (parent.id.empty() && !parent.item) {
        for (const auto& c : u.controls_) if (c.page != "dialog" && !u.removal_pending(c.id)) result.push_back({c.id});
        for (const auto& [id, type] : u.overlay_ids_) result.push_back({id});
        std::stable_sort(result.begin(), result.end(), [&](const NodeRef& a, const NodeRef& b) {
            auto ai = u.semantic_positions_.find(a.id), bi = u.semantic_positions_.find(b.id);
            if (ai == u.semantic_positions_.end()) return false;
            if (bi == u.semantic_positions_.end()) return true;
            return ai->second.y != bi->second.y ? ai->second.y < bi->second.y : ai->second.x < bi->second.x;
        });
    } else {
        auto node = get(parent); if (!node) return {node.status, node.message, {}};
        if (!parent.item && node->role == Role::dialog) {
            const auto& members = u.dialogs_.at(parent.id).members;
            for (const auto& c : u.controls_)
                if (std::find(members.begin(), members.end(), c.id) != members.end() && !u.removal_pending(c.id)) result.push_back({c.id});
        } else if (!parent.item) {
            auto s = u.describe(parent.id); if (s) for (const auto& o : s->options) result.push_back({parent.id, o.value});
            if (u.menu_ && u.menu_->target == parent.id) result.push_back({menu_node_id(parent.id)});
        }
    }
    return {Status::ok, {}, result};
}
Query<bool> SemanticTree::animating() const { return state_->animating(); }
Query<Palette> SemanticTree::effective_palette() const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrong_thread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (!u.bound_appearance()) return {Status::not_found, u"No appearance is bound to this Ui", {}};
    return {Status::ok, {}, u.effective_palette(*u.bound_appearance())};
}
Query<std::optional<NodeRef>> SemanticTree::hover() const {
    auto target = state_->hover_query();
    if (!target) return {target.status, target.message, std::nullopt};
    if (!*target) return {Status::ok, {}, std::optional<NodeRef>{}};
    return {Status::ok, {}, std::optional<NodeRef>{NodeRef{(*target)->id, (*target)->item}}};
}
Result SemanticTree::clear_simulated_hover() { return state_->clear_simulated_hover(); }
Query<Color> SemanticTree::text_color(const NodeRef& ref, std::string_view name) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrong_thread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (ref.item || !u.find(ref.id) || u.removal_pending(ref.id)) return {Status::not_found, {}, {}};
    if (!u.is_rich(ref.id)) return {Status::not_supported, u"Not a rich text field", {}};
    if (auto named = u.named_color(ref.id, name)) return {Status::ok, {}, *named};
    if (!u.bound_appearance()) return {Status::not_found, u"No appearance is bound to this Ui", {}};
    return {Status::ok, {}, u.effective_palette(*u.bound_appearance()).text};
}
Query<TextSpan> SemanticTree::text_unit(std::string_view id, TextUnitKind unit, unsigned offset) const {
    auto n = get({std::string(id)}); if (!n) return {n.status, n.message, std::nullopt};
    if (n->role != Role::text) return {Status::not_supported, {}, std::nullopt};
    return unit == TextUnitKind::word ? state_->word_at(id, offset) : state_->paragraph_at(id, offset);
}
Query<unsigned> SemanticTree::word_stop(std::string_view id, unsigned offset, bool forward) const {
    auto n = get({std::string(id)}); if (!n) return {n.status, n.message, std::nullopt};
    if (n->role != Role::text) return {Status::not_supported, {}, std::nullopt};
    return state_->word_stop(id, offset, forward);
}
Query<std::u16string> SemanticTree::text(std::string_view id) const {
    auto n = get({std::string(id)}); if (!n) return {n.status, n.message, {}};
    if (n->role != Role::text) return {Status::not_supported, {}, {}};
    return {Status::ok, {}, n->value};
}
std::vector<SemanticNode> detail::UiState::semantic_snapshot() {
    SemanticTree tree(*this); std::vector<SemanticNode> nodes;
    auto visit = [&](auto&& self, const NodeRef& parent) -> void {
        auto children = tree.children(parent); if (!children) return;
        for (const auto& ref : *children) {
            auto node = tree.get(ref); if (!node) continue;
            nodes.push_back(*node); self(self, ref);
        }
    };
    visit(visit, {}); return nodes;
}
void detail::UiState::semantic_diff() {
    const bool was_empty = semantic_pending_.empty();
    auto after = semantic_snapshot();
    std::vector<SemanticEvent> changes;
    auto emit = [&](SemanticEventKind kind, NodeRef ref, std::string property = {}, std::optional<bool> hidden = {}) {
        auto same = [&](const SemanticEvent& e) { return e.kind == kind && e.ref == ref && e.property == property; };
        if (std::none_of(changes.begin(), changes.end(), same)) changes.push_back({kind, std::move(ref), std::move(property), hidden});
    };
    auto find = [](const auto& nodes, const NodeRef& ref) -> const SemanticNode* {
        auto i = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.ref == ref; });
        return i == nodes.end() ? nullptr : &*i;
    };
    // A menu's structure changes belong to the component that owns it.
    auto owner_of = [](const NodeRef& ref) -> NodeRef {
        if (auto target = menu_target_id(ref.id)) return NodeRef{*target};
        return ref.item ? NodeRef{ref.id} : NodeRef{};
    };
    for (const auto& old : semantic_before_) if (!find(after, old.ref)) {
        if (old.role == Role::menu) emit(SemanticEventKind::menu_closed, old.ref);
        emit(SemanticEventKind::structure_changed, owner_of(old.ref));
    }
    for (const auto& node : after) if (!node.ref.item) {
        std::vector<NodeRef> before_items, after_items;
        for (const auto& old : semantic_before_) if (old.ref.item && old.ref.id == node.ref.id) before_items.push_back(old.ref);
        for (const auto& current : after) if (current.ref.item && current.ref.id == node.ref.id) after_items.push_back(current.ref);
        if (before_items != after_items) emit(SemanticEventKind::structure_changed, node.ref);
    }
    for (const auto& node : after) {
        auto old = find(semantic_before_, node.ref);
        if (!old) {
            if (node.role == Role::menu) emit(SemanticEventKind::menu_opened, node.ref);
            emit(SemanticEventKind::structure_changed, node.role == Role::menu ? owner_of(node.ref) : node.ref.item ? NodeRef{node.ref.id} : NodeRef{});
            if (node.role == Role::menu_item && node.focused) emit(SemanticEventKind::focus_changed, node.ref);
            continue;
        }
        if (node.hidden != old->hidden) {
            if (node.role == Role::dialog || node.role == Role::tooltip)
                emit(SemanticEventKind::property_changed, node.ref, "hidden", node.hidden);
            emit(SemanticEventKind::structure_changed, {});
        }
        bool focused_item = !node.ref.item && std::any_of(after.begin(), after.end(), [&](const auto& child) {
            return child.ref.id == node.ref.id && child.ref.item && child.focused;
        });
        if (node.focused && !old->focused && !focused_item) emit(SemanticEventKind::focus_changed, node.ref);
        auto property = [&](bool changed, const char* name) { if (changed) emit(SemanticEventKind::property_changed, node.ref, name); };
        property(node.name != old->name, "name"); property(node.value != old->value, "value");
        property(node.enabled != old->enabled, "enabled"); property(node.read_only != old->read_only, "read_only");
        property(node.checked != old->checked, "checked"); property(node.selected != old->selected, "selected");
        property(node.expanded != old->expanded, "expanded"); property(node.invalid != old->invalid, "invalid");
        property(node.blocked_by_modal != old->blocked_by_modal, "blocked_by_modal");
        property(node.numeric_value != old->numeric_value, "numeric_value");
        property(node.text_align != old->text_align, "text_align"); property(node.justify_method != old->justify_method, "justify_method");
        property(node.line_spacing != old->line_spacing, "line_spacing");
        property(node.paragraph_spacing != old->paragraph_spacing, "paragraph_spacing");
        property(node.styles != old->styles, "styles"); property(node.typing_style != old->typing_style, "typing_style");
        property(node.word_segmentation != old->word_segmentation, "word_segmentation");
        if (node.scroll && old->scroll) {
            property(node.scroll->first_line != old->scroll->first_line, "scroll_line");
            property(node.scroll->visible_lines != old->scroll->visible_lines, "visible_lines");
        }
        property(node.writing_mode != old->writing_mode, "writing_mode"); property(node.vertical_digits != old->vertical_digits, "vertical_digits");
        if (node.role == Role::text && node.value != old->value) emit(SemanticEventKind::text_changed, node.ref);
        if (node.text_selection && old->text_selection && (node.text_selection->anchor != old->text_selection->anchor || node.text_selection->caret != old->text_selection->caret))
            emit(SemanticEventKind::text_selection_changed, node.ref);
        if (node.bounds != old->bounds) emit(SemanticEventKind::layout_changed, {});
    }
    semantic_before_.clear();
    semantic_pending_.insert(semantic_pending_.end(), std::make_move_iterator(changes.begin()), std::make_move_iterator(changes.end()));
    if (was_empty && !semantic_pending_.empty() && semantic_schedule_) semantic_schedule_();
}
Subscription SemanticTree::subscribe(std::function<void(const SemanticEvent&)> callback) {
    if (std::this_thread::get_id() != state_->owner_ || !callback) return Subscription{};
    auto id = ++state_->next_semantic_subscriber_; state_->semantic_subscribers_.emplace(id, std::move(callback));
    std::weak_ptr<void> lifetime = state_->lifetime_; auto state = state_;
    return Subscription([state, lifetime, id] { if (!lifetime.expired()) state->semantic_subscribers_.erase(id); });
}
void SemanticTree::dispatch_events() {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_ || u.semantic_dispatching_) return;
    u.semantic_dispatching_ = true;
    for (unsigned round = 0; round < 64 && !u.semantic_pending_.empty(); ++round) {
        auto batch = std::exchange(u.semantic_pending_, {});
        for (const auto& event : batch) {
            auto subscribers = u.semantic_subscribers_;
            for (const auto& [id, callback] : subscribers) if (u.semantic_subscribers_.contains(id)) {
                try { callback(event); } catch (...) { u.stopped_ = true; }
            }
        }
        u.commit_removals();
    }
    u.semantic_dispatching_ = false;
    if (!u.semantic_pending_.empty() && u.semantic_schedule_) u.semantic_schedule_();
}
Result SemanticTree::perform(const NodeRef& ref, Verb verb, Argument argument) {
    auto node = get(ref);
    if (!node) return {node.status, node.message};
    if (state_->renderer_failed_) return {Status::renderer_failed, {}};
    if (node->role == Role::menu) return verb == Verb::cancel ? state_->close_menu() : Result{Status::not_supported, {}};
    if (node->role == Role::menu_separator) return {Status::not_supported, {}};
    if (node->role == Role::menu_item) {
        if (verb == Verb::invoke) return state_->menu_execute(ref.item->id);
        if (verb == Verb::focus || verb == Verb::hover) return state_->menu_highlight(ref.item->id);  // a menu row's hover is its highlight
        return {Status::not_supported, {}};
    }
    if (verb == Verb::hover) {
        if (node->role == Role::dialog || node->role == Role::tooltip) return {Status::not_supported, {}};
        return state_->simulate_hover(ref.id, ref.item);
    }
    auto invalid_argument = [&]() -> Result {
        if (auto result = state_->available(ref.id); !result) return result;
        if ((verb == Verb::set_value || verb == Verb::paste) && node->read_only) return {Status::read_only, {}};
        if (state_->text_composing(ref.id)) return {Status::busy, {}};
        return {Status::invalid_value, {}};
    };
    if (verb == Verb::context_menu) {
        std::optional<MenuPosition> at; std::optional<OptionValue> item = ref.item;
        if (auto position = std::get_if<MenuPosition>(&argument)) at = *position;
        else if (auto value = std::get_if<OptionValue>(&argument)) item = *value;
        else if (!std::holds_alternative<std::monostate>(argument)) return invalid_argument();
        return state_->open_menu(ref.id, at, std::move(item));
    }
    const auto* control = state_->find(ref.id);
    const detail::ComponentType* type = control ? &detail::component_type(control->kind) : nullptr;
    if (type && !ref.item && verb == Verb::select && std::holds_alternative<OptionValue>(argument) && type->item_role()) return invalid_argument();
    if (type && !ref.item && verb == Verb::focus && (type->can_focus() || node->focusable)) return state_->focus(ref.id);
    if (node->role == Role::dialog && verb == Verb::cancel) return state_->settle_dialog(ref.id, false);
    if (!type) return {Status::not_supported, {}};
    if (auto result = type->perform({*state_, ref, verb, argument, *node, invalid_argument})) return *result;
    if (verb == Verb::confirm && !ref.item && type->confirm_pending(*state_, *control))
        return std::visit([](const auto& handle) -> Result {
            if constexpr (requires { handle.confirm(); }) return handle.confirm();
            else return {Status::not_supported, {}};
        }, state_->layout_handle(ref.id));
    return {Status::not_supported, {}};
}
}
