#include "core_internal.hpp"
#include "component_type.hpp"
#include "utf.hpp"
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
bool valid_single_line(std::u16string_view text, std::size_t max_length) {
    if (text.size() > max_length) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        if (ch < 0x20 || ch == 0x7f) return false;
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (ch >= 0xdc00 && ch <= 0xdfff) return false;
    }
    return true;
}
void EditSession::select(unsigned anchor, unsigned caret) {
    if (composing) cancel_composition();
    auto clamp = [&](unsigned pos) {
        pos = std::min(pos, static_cast<unsigned>(text.size()));
        if (pos && pos < text.size() && text[pos] >= 0xdc00 && text[pos] <= 0xdfff &&
            text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff) --pos;
        return pos;
    };
    selection = {clamp(anchor), clamp(caret)};
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
        if (key_armed_ == id) key_armed_.clear();
        if (pointer_armed_ == id) { pointer_armed_.clear(); pointer_option_.reset(); pointer_option_value_.reset(); slider_drag_.reset(); }
        if (tooltip_hover_ == id) tooltip_hover_.clear();
        for (auto& [key, dialog] : dialogs_) if (dialog.opener == id) dialog.opener.clear();
        if (auto member = page_members_.find(id); member != page_members_.end()) {
            auto& members = tabs_.at(member->second.first).pages[member->second.second].members;
            std::erase_if(members, [&](const ComponentHandle& handle) { return layout_id(handle) == id; });
            page_members_.erase(member);
        }
        if (auto group = confirmation_members_.find(id); group != confirmation_members_.end()) {
            for (const auto& field : group->second) {
                if (auto it = edits_.find(field); it != edits_.end()) it->second.confirmation_member = false;
                if (auto it = bools_.find(field); it != bools_.end()) it->second.confirmation_member = false;
                if (auto it = radios_.find(field); it != radios_.end()) it->second.confirmation_member = false;
                if (auto it = dropdowns_.find(field); it != dropdowns_.end()) it->second.confirmation_member = false;
                if (auto it = numerics_.find(field); it != numerics_.end()) it->second.confirmation_member = false;
            }
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
Result detail::UiState::set_presentation(const PresentationSettings& settings) {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (settings.text_size < min_text_size || settings.text_size > max_text_size ||
        (settings.density != Density::standard && settings.density != Density::compact) ||
        (settings.hover != Hover::on && settings.hover != Hover::off))
        return {Status::invalid_value, u"Invalid presentation setting"};
    if (settings.variant.name == "high-contrast") return {Status::not_supported, u"High contrast is a later phase"};
    if (settings.variant.name != "light" && settings.variant.name != "dark") return {Status::not_found, u"Unknown theme variant"};
    if (settings == presentation_) return {};
    presentation_ = settings;
    if (presentation_changed_) presentation_changed_();
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
    if (palette_overrides_.empty() || !is_builtin_appearance(appearance)) return palette;
    return apply_overrides(palette, palette_overrides_, appearance.name(), variant);
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
        if (control.kind == Kind::slider || control.kind == Kind::dropdown || control.kind == Kind::list || control.kind == Kind::tabs || control.kind == Kind::progress) throw std::invalid_argument("Component requires typed creation");
        if (control.id.empty() || !ids.emplace(control.id, true).second)
            throw std::invalid_argument("Control IDs must be nonempty and unique");
        button_lifetimes_.emplace(control.id, std::make_shared<int>(0));
        if (control.kind == Kind::text) {
            if (!valid_single_line(control.value, control.max_length)) throw std::invalid_argument("Invalid initial text");
            Field field;
            field.session.text = control.value;
            edits_.emplace(control.id, std::move(field));
        }
    }
    for (const auto& control : controls_)
        if (!control.target.empty() && (!detail::component_type(control.kind).can_target() || !ids.contains(control.target)))
            throw std::invalid_argument("Only labels may target, and only known controls");
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
ComponentHandle detail::UiState::progress_layout_handle(const Control& c) {
    OperationScope operation(this); return ProgressHandle(this, lifetime_, c.id); }
Result ProgressHandle::connect_source(NumericSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"A numeric source requires read and subscribe"};
    auto& field = ui_->progresses_.at(id_);
    if (field.disconnect) field.disconnect();
    field.source = std::move(source);
    auto weak = lifetime_; auto state = ui_;
    field.disconnect = field.source.subscribe([weak, state] { if (!weak.expired() && state->invalidate_) state->invalidate_(); });
    if (ui_->invalidate_) ui_->invalidate_();
    return {};
}
Query<Semantics> ProgressHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
TextHandle detail::UiState::add_text(Control control) {
    OperationScope operation(this);
    if (control.kind != Kind::text || control.id.empty() || find(control.id) ||
        !valid_single_line(control.value, control.max_length))
        throw std::invalid_argument("Invalid text component");
    Field field;
    field.session.text = control.value;
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
ComponentHandle detail::UiState::checkbox_layout_handle(const Control& c) {
    OperationScope operation(this); return CheckboxHandle(this, lifetime_, c.id); }
Result CheckboxHandle::toggle() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->toggle_checkbox(id_);
}
Result CheckboxHandle::use_latest() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->bools_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
}
Result CheckboxHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result CheckboxHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result CheckboxHandle::connect_source(BoolSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"A boolean source requires read and subscribe"};
    auto& field = ui_->bools_.at(id_);
    if (field.disconnect) field.disconnect();
    field.source = std::move(source); field.connected = false;
    auto weak = lifetime_; auto state = ui_; auto id = id_;
    field.disconnect = field.source.subscribe([weak, state, id] { if (!weak.expired()) state->bool_source_changed(id); });
    ui_->bool_source_changed(id_); return {};
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
Result CheckboxHandle::connect_immediate(std::function<Result(const BoolChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& field = ui_->bools_.at(id_);
    if (!field.connected || field.confirmation_member || field.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    field.immediate = std::move(write); return {};
}
Result CheckboxHandle::connect_confirmed(std::function<Result(const BoolChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->bools_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result CheckboxHandle::confirm() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->bools_.at(id_);
    if (!f.confirmed) return {Status::invalid_value, u"No confirmed write is connected"};
    if (f.submitting || ui_->dispatching_ || ui_->invoking_) return {Status::busy, u"Operation is already executing"};
    BoolChange change{*this, f.value, f.origin}; f.submitting = true; ui_->invoking_ = true;
    Result result;
    try { result = f.confirmed(change); }
    catch (...) { f.submitting = false; ui_->invoking_ = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false; ui_->invoking_ = false;
    if (result) { f.origin = change.value; ui_->set_error(id_, u""); }
    else { ui_->set_error(id_, result.message); ui_->focus(id_); }
    auto notification = ui_->notify(); return notification ? result : notification;
}
Result detail::UiState::toggle_checkbox(const Id& id) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    auto& field = bools_.at(id);
    if (!field.connected || (!field.immediate && !field.confirmed && !field.confirmation_member)) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (field.value == field.source_value && !field.submitting) field.origin = field.source_value;
    field.value = !field.value;
    BoolChange change{CheckboxHandle(this, lifetime_, id), field.value, field.origin};
    if (!field.immediate || field.submitting) return notify();
    field.submitting = true;
    Result result;
    try { result = field.immediate(change); } catch (...) { field.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    field.submitting = false;
    if (result) { field.origin = change.value; set_error(id, u""); }
    else set_error(id, result.message);
    auto notification = notify(); return notification ? result : notification;
}
Query<Semantics> CheckboxHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<bool> CheckboxHandle::draft() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->bools_.at(id_).value};
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
ComponentHandle detail::UiState::radio_layout_handle(const Control& c) {
    OperationScope operation(this); return RadioGroupHandle(this, lifetime_, c.id); }
ListHandle detail::UiState::add_list(Control control, std::vector<RadioOption> items) {
    OperationScope operation(this);
    if (control.kind != Kind::list || control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid list");
    std::set<std::string> ids;
    for (const auto& item : items) if (item.value.id.empty() || !ids.insert(item.value.id).second) throw std::invalid_argument("Invalid list identity");
    auto id = control.id; controls_.push_back(std::move(control));
    ListField field; field.items = std::move(items); lists_.emplace(id, std::move(field));
    return ListHandle(this, lifetime_, std::move(id));
}
ComponentHandle detail::UiState::list_layout_handle(const Control& c) {
    OperationScope operation(this); return ListHandle(this, lifetime_, c.id); }
Result ListHandle::connect_source(OptionSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
    auto& f = ui_->lists_.at(id_); if (f.disconnect) f.disconnect();
    f.source = std::move(source); auto weak = lifetime_; auto* state = ui_;
    f.disconnect = f.source.subscribe([weak, state] { if (!weak.expired() && state->invalidate_) state->invalidate_(); });
    return {};
}
Result ListHandle::connect_immediate(std::function<Result(std::optional<OptionValue>)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->lists_.at(id_); if (!f.source.read || !write) return {Status::invalid_value, u"A source and write are required"};
    f.immediate = std::move(write); return {};
}
Result ListHandle::select(OptionValue value) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available_option(id_, value); !r) return r;
    auto& f = ui_->lists_.at(id_);
    auto item = std::find_if(f.items.begin(), f.items.end(), [&](const auto& row) { return row.value == value; });
    if (item == f.items.end()) return ui_->find(id_)->read_only ? Result{Status::read_only, u"List is read only"} : Result{Status::invalid_value, u"Invalid list item"};
    if (!item->enabled) return {Status::disabled, u"List item is disabled"};
    if (ui_->find(id_)->read_only) return {Status::read_only, u"List is read only"};
    if (!f.source.read || !f.immediate) return {Status::invalid_value, u"Connect an immediate write"};
    if (ui_->dispatching_ || f.writing) return {Status::busy, u"Operation is already executing"};
    if (f.source.read() == value) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); return {}; }
    f.writing = true; Result r;
    try { r = f.immediate(value); }
    catch (...) { f.writing = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.writing = false;
    if (r) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); auto n = ui_->notify(); if (!n) return n; } return r;
}
Result ListHandle::focus(OptionValue value) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, {}};
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
}
Result detail::UiState::list_key_down(KeyPress key) {
    OperationScope operation(this);
    auto& f = lists_.at(focus_);
    if (key.key == Key::space) return f.cursor ? ListHandle(this, lifetime_, focus_).select(*f.cursor) : Result{};
    if (key.key != Key::up && key.key != Key::down && key.key != Key::left && key.key != Key::right && key.key != Key::home && key.key != Key::end) return {};
    int n = static_cast<int>(f.items.size());
    int current = -1;
    for (int i = 0; i < n; ++i) if (f.cursor && f.items[i].value == *f.cursor) { current = i; break; }
    int step = key.key == Key::up || key.key == Key::left || key.key == Key::end ? -1 : 1;
    int start = key.key == Key::home ? 0 : key.key == Key::end ? n - 1 : current < 0 ? (step > 0 ? 0 : n - 1) : current + step;
    for (int i = start; i >= 0 && i < n; i += step) if (f.items[i].enabled) return ListHandle(this, lifetime_, focus_).select(f.items[i].value);
    return {};
}
Result ListHandle::set_items(std::vector<RadioOption> items) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
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
        try { result = f.immediate(std::nullopt); }
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
}
Result ListHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_); if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"}; return ui_->focus(id_); }
Result ListHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_); if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"}; if (auto r = ui_->configuration_access(id_); !r) return r; ui_->enable(id_, enabled); return {}; }
Query<Semantics> ListHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Result RadioGroupHandle::use_latest() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->radios_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
}
Result RadioGroupHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result RadioGroupHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result RadioGroupHandle::enable_option(OptionValue value, bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& options = ui_->radios_.at(id_).options;
    auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.value == value; });
    if (it == options.end()) return {Status::invalid_value, u"Unknown option"};
    it->enabled = enabled; if (ui_->invalidate_) ui_->invalidate_(); return {};
}
Result RadioGroupHandle::connect_source(OptionSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
    auto& field = ui_->radios_.at(id_);
    if (field.disconnect) field.disconnect();
    field.source = std::move(source); field.connected = false;
    auto weak = lifetime_; auto state = ui_; auto id = id_;
    field.disconnect = field.source.subscribe([weak, state, id] { if (!weak.expired()) state->option_source_changed(id); });
    ui_->option_source_changed(id_); return {};
}
void detail::UiState::option_source_changed(const Id& id) {
    OperationScope operation(this);
    auto& field = radios_.at(id); auto value = field.source.read();
    bool clean = !field.connected || field.value == field.source_value;
    field.source_value = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result RadioGroupHandle::connect_immediate(std::function<Result(const OptionChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& field = ui_->radios_.at(id_);
    if (!field.connected || field.confirmation_member || field.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    field.immediate = std::move(write); return {};
}
Result RadioGroupHandle::connect_confirmed(std::function<Result(const OptionChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->radios_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result RadioGroupHandle::confirm() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->radios_.at(id_);
    if (!f.confirmed) return {Status::invalid_value, u"No confirmed write is connected"};
    if (f.submitting || ui_->dispatching_ || ui_->invoking_) return {Status::busy, u"Operation is already executing"};
    OptionChange change{*this, f.value, f.origin}; f.submitting = true; ui_->invoking_ = true;
    Result result;
    try { result = f.confirmed(change); }
    catch (...) { f.submitting = false; ui_->invoking_ = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false; ui_->invoking_ = false;
    if (result) { f.origin = change.value; ui_->set_error(id_, u""); }
    else { ui_->set_error(id_, result.message); ui_->focus(id_); }
    auto notification = ui_->notify(); return notification ? result : notification;
}
Result RadioGroupHandle::select(OptionValue value) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->select_option(id_, std::move(value));
}
Result detail::UiState::select_option(const Id& id, OptionValue value) {
    OperationScope operation(this);
    if (auto r = available_option(id, value); !r) return r;
    auto& field = radios_.at(id);
    auto it = std::find_if(field.options.begin(), field.options.end(), [&](const auto& o) { return o.value == value; });
    if (it == field.options.end()) return {Status::invalid_value, u"Unknown option"};
    if (!it->enabled) return {Status::disabled, u"Option is disabled"};
    if (!field.connected || (!field.immediate && !field.confirmed && !field.confirmation_member)) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (field.value == value) return {};
    if (field.value == field.source_value && !field.submitting) field.origin = field.source_value;
    field.value = std::move(value);
    OptionChange change{RadioGroupHandle(this, lifetime_, id), field.value, field.origin};
    if (!field.immediate || field.submitting) return notify();
    field.submitting = true; Result result;
    try { result = field.immediate(change); } catch (...) { field.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    field.submitting = false;
    if (result) { field.origin = change.value; set_error(id, u""); } else set_error(id, result.message);
    auto notification = notify(); return notification ? result : notification;
}
Result detail::UiState::radio_key_down(KeyPress key) {
    OperationScope operation(this);
    if (key.repeat) return {};
    int direction = key.key == Key::right || key.key == Key::down ? 1 : key.key == Key::left || key.key == Key::up ? -1 : 0;
    if (!direction) return {};
    auto& field = radios_.at(focus_);
    int n = static_cast<int>(field.options.size()), index = direction > 0 ? -1 : n;
    for (int i = 0; i < n; ++i) if (field.value && field.options[i].value == *field.value) { index = i; break; }
    for (int i = index + direction; i >= 0 && i < n; i += direction)
        if (field.options[i].enabled) return select_option(focus_, field.options[i].value);
    return {};
}
Query<Semantics> RadioGroupHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<std::optional<OptionValue>> RadioGroupHandle::draft() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->radios_.at(id_).value};
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
ComponentHandle detail::UiState::dropdown_layout_handle(const Control& c) {
    OperationScope operation(this); return DropdownHandle(this, lifetime_, c.id); }
void detail::UiState::close_popup() {
    OperationScope operation(this);
    if (popup_source_.empty()) return;
    if (auto it = dropdowns_.find(popup_source_); it != dropdowns_.end()) it->second.candidate.reset();
    popup_source_.clear();
    if (invalidate_) invalidate_();
}
Result DropdownHandle::open() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.connected || (!f.confirmation_member && !f.immediate && !f.confirmed)) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (auto r = ui_->focus(id_); !r) return r;
    ui_->close_popup(); ui_->popup_source_ = id_;
    auto current = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.enabled && f.value && o.value == *f.value; });
    if (current == f.options.end()) current = std::find_if(f.options.begin(), f.options.end(), [](const auto& o) { return o.enabled; });
    f.reveal = true; f.scroll.offset = 0;
    f.candidate = current == f.options.end() ? std::nullopt : std::optional<OptionValue>(current->value);
    if (ui_->invalidate_) ui_->invalidate_();
    return {};
}
Result DropdownHandle::cancel() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    if (ui_->popup_source_ == id_) ui_->close_popup();
    return {};
}
Result DropdownHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_); if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"}; return ui_->focus(id_); }
Result DropdownHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result DropdownHandle::set_options(std::vector<RadioOption> options) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
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
}
Result DropdownHandle::connect_source(OptionSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
    auto& f = ui_->dropdowns_.at(id_); if (f.disconnect) f.disconnect();
    f.source = std::move(source); f.connected = false;
    auto weak = lifetime_; auto state = ui_; auto id = id_;
    f.disconnect = f.source.subscribe([weak, state, id] { if (!weak.expired()) state->dropdown_source_changed(id); });
    ui_->dropdown_source_changed(id_); return {};
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
Result DropdownHandle::connect_immediate(std::function<Result(const DropdownChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.connected || f.confirmation_member || f.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.immediate = std::move(write); return {};
}
Result DropdownHandle::connect_confirmed(std::function<Result(const DropdownChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result DropdownHandle::confirm() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.confirmed) return {Status::invalid_value, u"No confirmed write is connected"};
    if (f.submitting || ui_->dispatching_ || ui_->invoking_) return {Status::busy, u"Operation is already executing"};
    DropdownChange change{*this, f.value, f.origin}; f.submitting = true; ui_->invoking_ = true; Result result;
    try { result = f.confirmed(change); } catch (...) { f.submitting = false; ui_->invoking_ = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false; ui_->invoking_ = false;
    if (result) { f.origin = change.value; ui_->set_error(id_, u""); } else { ui_->set_error(id_, result.message); ui_->focus(id_); }
    auto notification = ui_->notify(); return notification ? result : notification;
}
Result DropdownHandle::use_latest() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return f.source_value && o.enabled && o.value == *f.source_value; });
    f.value = it == f.options.end() ? std::nullopt : f.source_value; f.origin = f.source_value;
    if (ui_->invalidate_) ui_->invalidate_();
    ui_->set_error(id_, u""); return ui_->notify();
}
Result DropdownHandle::select(OptionValue value) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->select_dropdown(id_, std::move(value));
}
Result detail::UiState::select_dropdown(const Id& id, OptionValue value) {
    OperationScope operation(this);
    if (auto r = available_option(id, value); !r) return r;
    auto& f = dropdowns_.at(id);
    auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.value == value; });
    if (it == f.options.end()) return {Status::invalid_value, u"Unknown option"};
    if (!it->enabled) return {Status::disabled, u"Option is disabled"};
    if (!f.connected || (!f.immediate && !f.confirmed && !f.confirmation_member)) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (f.value == value) return {};
    if (f.value == f.source_value && !f.submitting) f.origin = f.source_value;
    f.value = std::move(value); set_error(id, u"");
    if (invalidate_) invalidate_();
    DropdownChange change{DropdownHandle(this, lifetime_, id), f.value, f.origin};
    if (!f.immediate || f.submitting) return notify();
    f.submitting = true; Result result;
    try { result = f.immediate(change); } catch (...) { f.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false;
    if (result) { f.origin = change.value; set_error(id, u""); } else set_error(id, result.message);
    auto notification = notify(); return notification ? result : notification;
}
Result detail::UiState::dropdown_key_down(KeyPress key) {
    OperationScope operation(this);
    auto& f = dropdowns_.at(focus_);
    if (key.key == Key::escape) { close_popup(); return {}; }
    if (key.key == Key::enter || key.key == Key::space) {
        if (key.repeat) return {};
        if (popup_source_ != focus_) return DropdownHandle(this, lifetime_, focus_).open();
        auto candidate = f.candidate; close_popup(); return candidate ? select_dropdown(focus_, *candidate) : Result{};
    }
    if (popup_source_ != focus_) return {};
    int direction = key.key == Key::down || key.key == Key::right ? 1 : key.key == Key::up || key.key == Key::left ? -1 : 0;
    if (key.key == Key::home) direction = 1;
    if (key.key == Key::end) direction = -1;
    if (!direction) return {};
    int n = static_cast<int>(f.options.size()), index = direction > 0 ? -1 : n;
    if (key.key != Key::home && key.key != Key::end) for (int i = 0; i < n; ++i) if (f.candidate && f.options[i].value == *f.candidate) { index = i; break; }
    f.reveal = true;
    for (int i = index + direction; i >= 0 && i < n; i += direction) if (f.options[i].enabled) { f.candidate = f.options[i].value; break; }
    if (invalidate_) invalidate_(); return {};
}
Query<Semantics> DropdownHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<std::optional<OptionValue>> DropdownHandle::draft() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->dropdowns_.at(id_).value};
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
ComponentHandle detail::UiState::slider_layout_handle(const Control& c) {
    OperationScope operation(this); return SliderHandle(this, lifetime_, c.id); }
Result SliderHandle::set_value(int value) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->available(id_); !result) return result;
    const auto range = ui_->numerics_.at(id_).range;
    if (value < range.minimum || value > range.maximum) return {Status::invalid_value, u"Value is outside the slider range"};
    return ui_->slider_set_value(id_, value);
}
Result SliderHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result SliderHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result SliderHandle::use_latest() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
}
Result SliderHandle::connect_source(NumericSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
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
    if (clean && !f.submitting) f.value = f.origin = value;
    if (invalidate_) invalidate_();
}
Result SliderHandle::connect_immediate(std::function<Result(const NumericChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.connected || f.confirmation_member || f.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.immediate = std::move(write); return {};
}
Result SliderHandle::connect_confirmed(std::function<Result(const NumericChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result SliderHandle::confirm() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.confirmed) return {Status::invalid_value, u"No confirmed write is connected"};
    if (f.submitting || ui_->dispatching_ || ui_->invoking_) return {Status::busy, u"Operation is already executing"};
    NumericChange change{*this, f.value, f.origin}; f.submitting = true; ui_->invoking_ = true;
    Result result;
    try { result = f.confirmed(change); }
    catch (...) { f.submitting = false; ui_->invoking_ = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false; ui_->invoking_ = false;
    if (result) { f.origin = change.value; ui_->set_error(id_, u""); }
    else { ui_->set_error(id_, result.message); ui_->focus(id_); }
    auto notification = ui_->notify(); return notification ? result : notification;
}
Result detail::UiState::slider_set_value(const Id& id, int value) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    auto& f = numerics_.at(id);
    if (find(id)->read_only) return {Status::read_only, u"Component is read only"};
    if (!f.connected || (!f.immediate && !f.confirmed && !f.confirmation_member)) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    value = snap_slider(f.range, value);
    if (f.value == value) return {};
    if (f.value == f.source_value && !f.submitting) f.origin = f.source_value;
    f.value = value;
    NumericChange change{SliderHandle(this, lifetime_, id), f.value, f.origin};
    if (!f.immediate || f.submitting) return notify();
    f.submitting = true; Result result;
    try { result = f.immediate(change); } catch (...) { f.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false;
    if (result) { f.origin = change.value; set_error(id, u""); } else set_error(id, result.message);
    auto notification = notify(); return notification ? result : notification;
}
Result detail::UiState::slider_pointer_down(const Id& id, int x, Rect track) {
    OperationScope operation(this);
    if (pointer_armed_ != id || track.w <= 0) return {Status::invalid_value, u"No slider drag is active"};
    slider_drag_ = SliderDrag{id, static_cast<double>(track.x), static_cast<double>(track.w), x};
    return slider_pointer(id, x);
}
Result detail::UiState::slider_pointer(const Id& id, int x) {
    OperationScope operation(this);
    if (pointer_armed_ != id || !slider_drag_ || slider_drag_->id != id) return {Status::invalid_value, u"No slider drag is active"};
    slider_drag_->x = x;
    const auto basis = *slider_drag_;
    auto& f = numerics_.at(id);
    double ratio = std::clamp((static_cast<double>(x) - basis.origin) / basis.width, 0.0, 1.0);
    double value = static_cast<double>(f.range.minimum) + ratio * (static_cast<double>(f.range.maximum) - f.range.minimum);
    return slider_set_value(id, static_cast<int>(std::floor(value + 0.5)));
}
std::optional<int> detail::UiState::slider_drag_x(std::string_view id) const {
    if (slider_drag_ && pointer_armed_ == id && slider_drag_->id == id) return slider_drag_->x;
    return std::nullopt;
}
Result detail::UiState::slider_key_down(KeyPress key) {
    OperationScope operation(this);
    auto& f = numerics_.at(focus_);
    const long long lo = f.range.minimum, hi = f.range.maximum, step = f.range.step, value = f.value;
    long long target = value;
    switch (key.key) {
    case Key::home: target = lo; break;
    case Key::end: target = hi; break;
    case Key::right: case Key::up:
        if (value < hi) target = std::min(hi, lo + ((value - lo) / step + 1) * step);
        break;
    case Key::left: case Key::down:
        if (value > lo) target = lo + ((value - lo - 1) / step) * step;
        break;
    default: return {};
    }
    return slider_set_value(focus_, static_cast<int>(target));
}
Query<Semantics> SliderHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<int> SliderHandle::draft() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->numerics_.at(id_).value};
}
ComponentHandle detail::UiState::layout_handle(std::string_view id) {
    OperationScope operation(this);
    auto control = find(id);
    if (!control) return {};
    return component_type(control->kind).handle(*this, *control);
}
ComponentHandle detail::UiState::text_layout_handle(const Control& control) {
    OperationScope operation(this); return TextHandle(this, lifetime_, control.id); }
ComponentHandle detail::UiState::label_layout_handle(const Control& control) {
    OperationScope operation(this); return LabelHandle(this, lifetime_, control.id); }
ComponentHandle detail::UiState::button_layout_handle(const Control& control) {
    OperationScope operation(this); return ButtonHandle(this, button_lifetimes_.at(control.id), control.id); }

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
ComponentHandle detail::UiState::navigation_layout_handle(const Control& c) {
    OperationScope operation(this); return NavigationListHandle(this, lifetime_, c.id);
}
ComponentHandle detail::UiState::tabs_layout_handle(const Control& c) {
    OperationScope operation(this); return TabsHandle(this, lifetime_, c.id); }
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
        bool eligible = on_screen && window_active_ && !modal_ && ((focus_ == id && !pointer_focus_) || tooltip_hover_ == id) && bool(available(id));
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
    if (!active) { tooltip_hover_.clear(); hover_clear(); }
    refresh_tooltips();
}
bool detail::UiState::hover_locked() const {
    return hover_capture_ || (slider_drag_ && pointer_armed_ == slider_drag_->id);
}
bool detail::UiState::hover_admitted(const HoverTarget& target) const {
    if (!available(target.id)) return false;
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
    if (hit) {
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
    tooltip_hover(item && control->kind == Kind::dropdown ? std::string_view{} : std::string_view(control->id));
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
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"UI no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(source_, true); !r) return {r.status, r.message, std::nullopt};
    auto it = ui_->tooltips_.find(source_);
    if (it == ui_->tooltips_.end()) return {Status::not_found, u"Tooltip no longer exists", std::nullopt};
    return {Status::ok, {}, it->second.visible};
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
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    return lifetime_.expired() ? Result{Status::not_found, u"UI no longer exists"} : ui_->open_dialog(id_, &opener);
}
Result DialogHandle::open() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    return lifetime_.expired() ? Result{Status::not_found, u"UI no longer exists"} : ui_->open_dialog(id_, nullptr);
}
Result DialogHandle::settle(bool confirmed) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    return lifetime_.expired() ? Result{Status::not_found, u"UI no longer exists"} : ui_->settle_dialog(id_, confirmed);
}
Query<bool> DialogHandle::is_open() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"UI no longer exists", std::nullopt};
    auto it = ui_->dialogs_.find(id_);
    if (it == ui_->dialogs_.end()) return {Status::not_found, u"Dialog not found", std::nullopt};
    if (auto r = ui_->configuration_access(it->second.initial, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->modal_ && ui_->active_dialog_ == id_};
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
Result TabsHandle::activate(OptionValue page) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->activate_tab(id_, std::move(page));
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
Result TabsHandle::add_member(OptionValue page, ComponentHandle component) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->add_page_member(id_, std::move(page), std::move(component));
}
Result TabsHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result TabsHandle::enable_page(OptionValue page, bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& t = ui_->tabs_.at(id_);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalid_value, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    if (!enabled && index == t.active) return {Status::invalid_value, u"Active page cannot be disabled"};
    it->enabled = enabled;
    if (!enabled && index == t.candidate) t.candidate = t.active;
    if (ui_->invalidate_) ui_->invalidate_();
    return {};
}
Result TabsHandle::on_page_change(std::function<void(OptionValue, OptionValue)> callback) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->tabs_.at(id_).changed = std::move(callback); return {};
}
Query<Semantics> TabsHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<OptionValue> TabsHandle::active() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->tabs_.at(id_).pages[ui_->tabs_.at(id_).active].value};
}
Result NavigationListHandle::select(OptionValue page) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->activate_tab(id_, std::move(page));
}
Result NavigationListHandle::add_member(OptionValue page, ComponentHandle component) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->add_page_member(id_, std::move(page), std::move(component));
}
Result NavigationListHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result NavigationListHandle::enable_page(OptionValue page, bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& t = ui_->tabs_.at(id_);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalid_value, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    if (!enabled && index == t.active) return {Status::invalid_value, u"Active page cannot be disabled"};
    it->enabled = enabled;
    if (!enabled && index == t.candidate) t.candidate = t.active;
    if (ui_->invalidate_) ui_->invalidate_();
    return {};
}
Result NavigationListHandle::on_page_change(std::function<void(OptionValue, OptionValue)> callback) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->tabs_.at(id_).changed = std::move(callback); return {};
}
Query<Semantics> NavigationListHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<OptionValue> NavigationListHandle::selected() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->tabs_.at(id_).pages[ui_->tabs_.at(id_).active].value};
}
Result detail::UiState::tabs_key_down(KeyPress key) {
    OperationScope operation(this);
    auto& t = tabs_.at(focus_);
    if (key.key == Key::left || key.key == Key::right) {
        int dir = key.key == Key::left ? -1 : 1;
        int i = static_cast<int>(t.candidate);
        for (std::size_t n = 0; n < t.pages.size(); ++n) {
            i = (i + dir + static_cast<int>(t.pages.size())) % static_cast<int>(t.pages.size());
            if (t.pages[static_cast<std::size_t>(i)].enabled) { t.candidate = static_cast<std::size_t>(i); break; }
        }
        if (invalidate_) invalidate_();
        return {};
    }
    return command_key_down(key);
}
Result detail::UiState::navigation_key_down(KeyPress key) {
    OperationScope operation(this);
    if (auto r = available(focus_); !r) return r;
    auto& t = tabs_.at(focus_);
    int index = static_cast<int>(t.active), last = static_cast<int>(t.pages.size()) - 1;
    if (key.key == Key::home) index = -1;
    else if (key.key == Key::end) index = last + 1;
    else if (key.key != Key::up && key.key != Key::down) return {};
    int direction = key.key == Key::up || key.key == Key::end ? -1 : 1;
    for (index += direction; index >= 0 && index <= last; index += direction)
        if (t.pages[static_cast<std::size_t>(index)].enabled) return activate_tab(focus_, t.pages[static_cast<std::size_t>(index)].value);
    return {};
}
Result LabelHandle::connect_source(std::function<std::u16string()> read) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (!read) return {Status::invalid_value, u"A label source requires a reader"};
    ui_->bind_text(id_, std::move(read));
    return {};
}
Result ButtonHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result ButtonHandle::bind(std::function<Result()> action) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (!action) return {Status::invalid_value, u"An action is required"};
    if (ui_->confirmation_buttons_.contains(id_)) return {Status::invalid_value, u"Confirmation action cannot be rebound"};
    ui_->bind(id_, std::move(action));
    return {};
}
Result ButtonHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    ui_->enable(id_, enabled);
    return {};
}
Result ButtonHandle::remove() const {
    detail::UiState::OperationScope scope(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (ui_->dialog_member(id_) || std::any_of(ui_->dialogs_.begin(), ui_->dialogs_.end(), [&](const auto& entry) { return entry.second.fallback == id_; }))
        return {Status::invalid_value, u"Dialog focus targets must remain alive"};
    if (std::find(ui_->pending_removals_.begin(), ui_->pending_removals_.end(), id_) == ui_->pending_removals_.end())
        ui_->pending_removals_.push_back(id_);
    return {};
}

Result TextHandle::focus() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result SliderHandle::on_blur(std::function<void()> callback) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (callback) ui_->blur_callbacks_.insert_or_assign(id_, std::move(callback));
    else ui_->blur_callbacks_.erase(id_);
    return {};
}
Result TextHandle::on_blur(std::function<void()> callback) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (callback) ui_->blur_callbacks_.insert_or_assign(id_, std::move(callback));
    else ui_->blur_callbacks_.erase(id_);
    return {};
}
Result TextHandle::set_error(std::u16string message) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->set_error(id_, std::move(message));
}
Result TextHandle::enable(bool enabled) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    ui_->enable(id_, enabled);
    return {};
}
Result TextHandle::read_only(bool is_read_only) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    ui_->set_read_only(id_, is_read_only);
    return {};
}
Result TextHandle::reset_to_source() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->reset_to_source(id_);
}
Result detail::UiState::reset_to_source(const Id& id) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.has_source_value)
        return {Status::invalid_value, u"Text source is not connected"};
    replace_text(id, it->second.source_value);
    it->second.origin = it->second.source_value;
    return set_error(id, u"");
}
Result ButtonHandle::invoke() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->invoke(id_);
}
Query<bool> ButtonHandle::executing() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto snapshot = describe();
    if (!snapshot) return {snapshot.status, snapshot.message, std::nullopt};
    return {Status::ok, {}, snapshot->executing};
}
Result TextHandle::paste(std::u16string_view text) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->paste(id_, text);
}
Result TextHandle::undo() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->undo(id_);
}
Result TextHandle::redo() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->redo(id_);
}
Result TextHandle::select(unsigned anchor, unsigned caret) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->select(id_, anchor, caret);
}
Result TextHandle::copy() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->copy(id_);
}
Result TextHandle::cut() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->cut(id_);
}
Result detail::UiState::configuration_access(std::string_view id, bool read_only) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!find(id) || removal_pending(id)) return {Status::not_found, u"Component no longer exists"};
    if (renderer_failed_ && !read_only) return {Status::renderer_failed, u"Renderer failed"};
    return {};
}
Result detail::UiState::connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
                                std::function<ConfirmationResult(const std::vector<TextChange>&)> submit) {
    OperationScope operation(this);
    if (button.ui_ != this || button.lifetime_.expired() || !submit || fields.empty())
        return {Status::invalid_value, u"Invalid confirmation group"};
    if (auto result = configuration_access(button.id_); !result) return result;
    if (actions_.contains(button.id_)) return {Status::invalid_value, u"Confirmation button already has an action"};
    std::set<Id> members;
    for (const auto& field : fields) {
        auto it = edits_.find(field.id_);
        if (field.ui_ != this || field.lifetime_.expired() || it == edits_.end() || !it->second.has_source_value ||
            it->second.confirmation_member || it->second.immediate || it->second.confirmed || !members.insert(field.id_).second)
            return {Status::invalid_value, u"Confirmation fields must be distinct, connected and ungrouped"};
    }
    for (const auto& field : fields) edits_.at(field.id_).confirmation_member = true;
    confirmation_buttons_.insert(button.id_);
    confirmation_members_[button.id_] = {members.begin(), members.end()};
    bind(button.id_, [this, fields = std::move(fields), submit = std::move(submit)] {
        std::vector<TextChange> changes;
        for (const auto& handle : fields) {
            auto& field = edits_.at(handle.id_);
            changes.push_back({handle, field.session.text, field.origin});
            field.submitting = true;
        }
        ConfirmationResult outcome;
        try { outcome = submit(changes); }
        catch (...) {
            for (const auto& handle : fields) edits_.at(handle.id_).submitting = false;
            throw;
        }
        for (std::size_t i = 0; i < fields.size(); ++i) {
            auto& field = edits_.at(fields[i].id_);
            field.submitting = false;
            if (outcome.result) {
                field.origin = changes[i].value;
                set_error(fields[i].id_, u"");
            }
        }
        if (!outcome.result) {
            std::optional<std::size_t> first;
            for (const auto& error : outcome.errors) {
                auto it = std::find(fields.begin(), fields.end(), error.field);
                if (it == fields.end()) continue;
                const auto index = static_cast<std::size_t>(it - fields.begin());
                set_error(it->id_, error.message);
                if (!error.message.empty() && (!first || index < *first)) first = index;
            }
            if (first) focus_error(fields[*first].id_);
        }
        auto notification = notify();
        return notification ? outcome.result : notification;
    });
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
            using T = std::decay_t<decltype(h)>;
            if constexpr (std::is_same_v<T, TextHandle>) {
                auto it = edits_.find(h.id_); return it != edits_.end() && it->second.has_source_value && !it->second.confirmation_member && !it->second.immediate && !it->second.confirmed;
            } else if constexpr (std::is_same_v<T, CheckboxHandle>) {
                auto it = bools_.find(h.id_); return it != bools_.end() && it->second.connected && !it->second.confirmation_member && !it->second.immediate && !it->second.confirmed;
            } else if constexpr (std::is_same_v<T, SliderHandle>) {
                auto it = numerics_.find(h.id_); return it != numerics_.end() && it->second.connected && !it->second.confirmation_member && !it->second.immediate && !it->second.confirmed;
            } else if constexpr (std::is_same_v<T, DropdownHandle>) {
                auto it = dropdowns_.find(h.id_); return it != dropdowns_.end() && it->second.connected && !it->second.confirmation_member && !it->second.immediate && !it->second.confirmed;
            } else {
                auto it = radios_.find(h.id_); return it != radios_.end() && it->second.connected && !it->second.confirmation_member && !it->second.immediate && !it->second.confirmed;
            }
        }, member);
        if (!valid) return {Status::invalid_value, u"Confirmation fields must be distinct, connected and ungrouped"};
    }
    for (const auto& member : fields) std::visit([&](const auto& h) {
        using T = std::decay_t<decltype(h)>;
        if constexpr (std::is_same_v<T, TextHandle>) edits_.at(h.id_).confirmation_member = true;
        else if constexpr (std::is_same_v<T, CheckboxHandle>) bools_.at(h.id_).confirmation_member = true;
        else if constexpr (std::is_same_v<T, SliderHandle>) numerics_.at(h.id_).confirmation_member = true;
        else if constexpr (std::is_same_v<T, DropdownHandle>) dropdowns_.at(h.id_).confirmation_member = true;
        else radios_.at(h.id_).confirmation_member = true;
    }, member);
    confirmation_buttons_.insert(button.id_);
    confirmation_members_[button.id_] = {members.begin(), members.end()};
    bind(button.id_, [this, fields = std::move(fields), submit = std::move(submit)] {
        std::vector<FieldChange> changes;
        for (const auto& member : fields) std::visit([&](const auto& h) {
            using T = std::decay_t<decltype(h)>;
            if constexpr (std::is_same_v<T, TextHandle>) { auto& f = edits_.at(h.id_); changes.emplace_back(TextChange{h, f.session.text, f.origin}); f.submitting = true; }
            else if constexpr (std::is_same_v<T, CheckboxHandle>) { auto& f = bools_.at(h.id_); changes.emplace_back(BoolChange{h, f.value, f.origin}); f.submitting = true; }
            else if constexpr (std::is_same_v<T, SliderHandle>) { auto& f = numerics_.at(h.id_); changes.emplace_back(NumericChange{h, f.value, f.origin}); f.submitting = true; }
            else if constexpr (std::is_same_v<T, DropdownHandle>) { auto& f = dropdowns_.at(h.id_); changes.emplace_back(DropdownChange{h, f.value, f.origin}); f.submitting = true; }
            else { auto& f = radios_.at(h.id_); changes.emplace_back(OptionChange{h, f.value, f.origin}); f.submitting = true; }
        }, member);
        MixedConfirmationResult outcome;
        try { outcome = submit(changes); }
        catch (...) {
            for (const auto& member : fields) std::visit([&](const auto& h) {
                using T = std::decay_t<decltype(h)>;
                if constexpr (std::is_same_v<T, TextHandle>) edits_.at(h.id_).submitting = false;
                else if constexpr (std::is_same_v<T, CheckboxHandle>) bools_.at(h.id_).submitting = false;
                else if constexpr (std::is_same_v<T, SliderHandle>) numerics_.at(h.id_).submitting = false;
                else if constexpr (std::is_same_v<T, DropdownHandle>) dropdowns_.at(h.id_).submitting = false;
                else radios_.at(h.id_).submitting = false;
            }, member);
            throw;
        }
        for (std::size_t i = 0; i < fields.size(); ++i) std::visit([&](const auto& h) {
            using T = std::decay_t<decltype(h)>;
            if constexpr (std::is_same_v<T, TextHandle>) { auto& f = edits_.at(h.id_); f.submitting = false; if (outcome.result) f.origin = std::get<TextChange>(changes[i]).value; }
            else if constexpr (std::is_same_v<T, CheckboxHandle>) { auto& f = bools_.at(h.id_); f.submitting = false; if (outcome.result) f.origin = std::get<BoolChange>(changes[i]).value; }
            else if constexpr (std::is_same_v<T, SliderHandle>) { auto& f = numerics_.at(h.id_); f.submitting = false; if (outcome.result) f.origin = std::get<NumericChange>(changes[i]).value; }
            else if constexpr (std::is_same_v<T, DropdownHandle>) { auto& f = dropdowns_.at(h.id_); f.submitting = false; if (outcome.result) f.origin = std::get<DropdownChange>(changes[i]).value; }
            else { auto& f = radios_.at(h.id_); f.submitting = false; if (outcome.result) f.origin = std::get<OptionChange>(changes[i]).value; }
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
Result TextHandle::connect_immediate(std::function<Result(const TextChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_write(id_, std::move(write), true);
}
Result TextHandle::connect_confirmed(std::function<Result(const TextChange&)> write) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_write(id_, std::move(write), false);
}
Result detail::UiState::connect_write(const Id& id, std::function<Result(const TextChange&)> write, bool immediate) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.has_source_value || it->second.confirmation_member ||
        (immediate ? bool(it->second.confirmed) : bool(it->second.immediate)) || !write)
        return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    (immediate ? it->second.immediate : it->second.confirmed) = std::move(write);
    return {};
}
Result TextHandle::confirm() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->confirm(id_);
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
Result TextHandle::connect_source(TextSource source) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_source(id_, std::move(source));
}
Result detail::UiState::connect_source(const Id& id, TextSource source) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::not_found, u"Component no longer exists"};
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"A text source requires read and subscribe"};
    if (it->second.disconnect) it->second.disconnect();
    it->second.source = std::move(source);
    it->second.has_source_value = false;
    auto weak = std::weak_ptr<void>(lifetime_);
    it->second.disconnect = it->second.source.subscribe([weak, this, id] {
        if (!weak.expired()) source_changed(id);
    });
    source_changed(id);
    return {};
}
void detail::UiState::source_changed(const Id& id) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.source.read) return;
    auto& field = it->second;
    auto latest = field.source.read();
    if (!valid_single_line(latest, find(id)->max_length)) return;
    const bool clean = !field.has_source_value || field.session.text == field.source_value;
    field.source_value = latest;
    field.has_source_value = true;
    if (clean && !field.submitting && !field.session.composing) {
        replace_text(id, latest);
        field.origin = latest;
    }
    if (invalidate_) invalidate_();
}
Query<bool> TextHandle::source_conflict() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto snapshot = describe();
    if (!snapshot) return {snapshot.status, snapshot.message, std::nullopt};
    return {Status::ok, {}, snapshot->source_conflict};
}
Result TextHandle::use_latest() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->use_latest(id_);
}
Result detail::UiState::use_latest(const Id& id) {
    OperationScope operation(this);
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.has_source_value) return {Status::invalid_value, u"Text source is not connected"};
    auto latest = it->second.source_value;
    const bool was_submitting = it->second.submitting;
    it->second.submitting = true;
    auto result = set_value(id, latest);
    it->second.submitting = was_submitting;
    if (result) {
        it->second.origin = std::move(latest);
        set_error(id, u"");
    }
    return result;
}
Result TextHandle::set_value(std::u16string value) const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->set_value(id_, std::move(value));
}
Query<Semantics> TextHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto result = ui_->configuration_access(id_, true); !result) return {result.status, result.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<std::u16string> TextHandle::draft() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    auto snapshot = describe();
    if (!snapshot) return {snapshot.status, snapshot.message, std::nullopt};
    return {Status::ok, {}, snapshot->value};
}
Query<Semantics> ButtonHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto result = ui_->configuration_access(id_, true); !result) return {result.status, result.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<Semantics> LabelHandle::describe() const {
    detail::UiState::OperationScope operation(lifetime_.expired() ? nullptr : ui_);
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto result = ui_->configuration_access(id_, true); !result) return {result.status, result.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
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
    if (auto it = bools_.find(id); it != bools_.end()) {
        s.checked = it->second.value;
        s.source_conflict = it->second.connected && it->second.origin != it->second.source_value && it->second.value != it->second.source_value;
    }
    if (auto it = numerics_.find(id); it != numerics_.end()) {
        s.source_conflict = it->second.connected && it->second.origin != it->second.source_value && it->second.value != it->second.source_value;
        s.slider_range = it->second.range; s.numeric_value = it->second.value;
    }
    if (auto it = progresses_.find(id); it != progresses_.end()) {
        s.read_only = true;
        if (it->second.source.read) {
            int displayed = std::clamp(it->second.source.read(), 0, 100);
            s.numeric_value = displayed;
            auto number = std::to_string(displayed);
            s.value = std::u16string(number.begin(), number.end()) + u"%";
        }
    }
    if (auto it = radios_.find(id); it != radios_.end()) {
        s.source_conflict = it->second.connected && it->second.origin != it->second.source_value && it->second.value != it->second.source_value;
        for (const auto& option : it->second.options) {
            bool selected = it->second.value && *it->second.value == option.value;
            s.options.push_back({option.value, option.label, option.enabled, selected});
            if (selected) s.value = option.label;
        }
    }
    if (auto it = dropdowns_.find(id); it != dropdowns_.end()) {
        const auto& f = it->second;
        s.source_conflict = f.connected && f.origin != f.source_value && f.value != f.source_value;
        s.expanded = popup_source_ == id; s.candidate = f.candidate;
        for (const auto& option : f.options) {
            bool selected = f.value && *f.value == option.value;
            s.options.push_back({option.value, option.label, option.enabled, selected});
            if (selected) s.value = option.label;
        }
    }
    if (auto it = tabs_.find(id); it != tabs_.end()) {
        const auto& t = it->second;
        s.candidate = t.pages[t.candidate].value;
        for (std::size_t i = 0; i < t.pages.size(); ++i) {
            const auto& page = t.pages[i];
            bool invalid = false;
            for (const auto& member : page.members) {
                auto member_id = layout_id(member);
                if (member_id) {
                    auto error = errors_.find(*member_id);
                    if (error != errors_.end() && !error->second.empty()) { invalid = true; break; }
                }
            }
            s.options.push_back({page.value, page.label, page.enabled, i == t.active, invalid});
            if (i == t.active) s.value = page.label;
        }
    }
    if (auto it = lists_.find(id); it != lists_.end()) {
        const auto& f = it->second; auto value = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        s.candidate = f.cursor;
        for (const auto& item : f.items) {
            bool selected = value && *value == item.value;
            s.options.push_back({item.value, item.label, item.enabled, selected});
            if (selected) s.value = item.label;
        }
    }
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
    if (auto it = edits_.find(id); it != edits_.end()) {
        const auto& field = it->second;
        s.source_conflict = field.has_source_value && field.origin != field.source_value && field.session.text != field.source_value;
    }
    if (!s.focusable || renderer_failed_) return s;
    s.operations.push_back(Operation::focus);
    bool admits_activation = component_type(control->kind).has_activate_operation() && !s.executing;
    if (auto it = bools_.find(id); it != bools_.end()) {
        const auto& f = it->second;
        admits_activation = admits_activation && f.connected && (f.confirmation_member || f.immediate || f.confirmed);
    }
    if (admits_activation) s.operations.push_back(Operation::activate);
    if (auto it = radios_.find(id); it != radios_.end()) {
        const auto& f = it->second;
        if (f.connected && (f.confirmation_member || f.immediate || f.confirmed) &&
            std::any_of(f.options.begin(), f.options.end(), [](const auto& option) { return option.enabled; }))
            s.operations.push_back(Operation::select);
    }
    if (auto it = numerics_.find(id); it != numerics_.end()) {
        const auto& f = it->second;
        if (!control->read_only && f.connected && (f.confirmation_member || f.immediate || f.confirmed)) s.operations.push_back(Operation::edit);
    }
    if (auto it = dropdowns_.find(id); it != dropdowns_.end()) {
        const auto& f = it->second;
        if (f.connected && (f.confirmation_member || f.immediate || f.confirmed)) s.operations.push_back(Operation::select);
    }
    if (auto it = lists_.find(id); it != lists_.end()) {
        const auto& f = it->second;
        if (!control->read_only && f.source.read && f.immediate &&
            std::any_of(f.items.begin(), f.items.end(), [](const auto& item) { return item.enabled; })) s.operations.push_back(Operation::select);
    }
    if (auto it = edits_.find(id); it != edits_.end()) {
        s.operations.insert(s.operations.end(), {Operation::select, Operation::copy});
        const auto& field = it->second;
        if (!control->read_only && !field.session.composing && (field.has_source_value && (field.confirmation_member || field.immediate || field.confirmed))) {
            s.operations.insert(s.operations.end(), {Operation::edit, Operation::cut, Operation::paste});
            if (!field.past.empty()) s.operations.push_back(Operation::undo);
            if (!field.future.empty()) s.operations.push_back(Operation::redo);
        }
    }
    return s;
}
std::u16string detail::UiState::text(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return {};
    if (control->kind == Kind::label && control->role == TextRole::error) {
        auto it = errors_.find(control->target);
        return it == errors_.end() ? std::u16string{} : it->second;
    }
    if (auto it = edits_.find(id); it != edits_.end()) return it->second.session.text;
    if (auto it = bools_.find(id); it != bools_.end()) return it->second.value ? u"true" : u"false";
    if (auto it = numerics_.find(id); it != numerics_.end()) { auto value = std::to_string(it->second.value); return {value.begin(), value.end()}; }
    if (auto it = providers_.find(id); it != providers_.end()) return it->second();
    return control->value;
}
void detail::UiState::bind_text(std::string_view id, std::function<std::u16string()> provider) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (!control || control->kind != Kind::label || !provider) throw std::invalid_argument("Text binding requires a label and a provider");
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
    if (field->session.text == value) return {};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    return replace_selection(id, *field, value);
}
Result detail::UiState::writable(std::string_view id, Field*& field) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (find(id)->read_only) return {Status::read_only, u"欄位為唯讀"};
    if (!it->second.has_source_value || (!it->second.confirmation_member && !it->second.immediate && !it->second.confirmed))
        return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (it->second.session.composing || dispatching_) return {Status::busy, u"操作尚未結束"};
    field = &it->second;
    return {};
}
// Every content change funnels through here so history, value mirror and notification agree.
Result detail::UiState::replace_selection(std::string_view id, Field& field, std::u16string_view insertion) {
    OperationScope operation(this);
    auto& session = field.session;
    auto first = session.begin();
    auto next = session.text;
    next.replace(first, session.end() - first, insertion);
    const auto limit = find(id)->max_length;
    if (!valid_single_line(next, limit)) {
        auto digits = std::to_string(limit);
        return {Status::invalid_value, u"請輸入有效單行文字（最多 " + std::u16string(digits.begin(), digits.end()) + u" 個 UTF-16 單位）"};
    }
    auto caret = first + static_cast<unsigned>(insertion.size());
    if (next == session.text) { session.select(caret, caret); return {}; }
    if (field.has_source_value && !field.submitting && session.text == field.source_value)
        field.origin = field.source_value;
    field.past.push_back({session.text, session.selection});
    field.future.clear();
    session.text = std::move(next);
    ++session.revision;
    session.select(caret, caret);
    return finish_edit(id, field);
}
Result detail::UiState::travel(std::string_view id, bool forward) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    auto& from = forward ? field->future : field->past;
    auto& to = forward ? field->past : field->future;
    if (from.empty()) return {Status::empty_history, forward ? u"沒有可重做的編輯" : u"沒有可復原的編輯"};
    auto& session = field->session;
    if (field->has_source_value && !field->submitting && session.text == field->source_value)
        field->origin = field->source_value;
    to.push_back({session.text, session.selection});
    auto snapshot = std::move(from.back());
    from.pop_back();
    if (session.text != snapshot.text) ++session.revision;
    session.text = std::move(snapshot.text);
    session.select(snapshot.selection.anchor, snapshot.selection.caret);
    return finish_edit(id, *field);
}
Result detail::UiState::submit_field(std::string_view id, Field& field,
        const std::function<Result(const TextChange&)>& write, bool command) {
    auto change = TextChange{TextHandle(this, lifetime_, Id(id)), field.session.text, field.origin};
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
    if (result) { field.origin = change.value; set_error(id, u""); }
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
Result detail::UiState::paste(std::string_view id, std::u16string_view text) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    return replace_selection(id, *field, paste_as_single_line(text));
}
Result detail::UiState::copy(std::string_view id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    const auto& session = it->second.session;
    if (session.begin() == session.end()) return {};
    if (!clipboard_ || !clipboard_->write(std::u16string_view(session.text).substr(session.begin(), session.end() - session.begin())))
        return {Status::failed, u"無法寫入剪貼簿"};
    return {};
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
    return {};
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
    key_armed_.clear(); pointer_armed_.clear(); slider_drag_.reset();
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
    pointer_focus_ = false;
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
    if (!composing_.empty()) return {Status::busy, u"Composition in progress"};
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
        pointer_focus_ = false;
        refresh_tooltips();
        auto order = focus_order();
        if (order.empty()) return {Status::not_found, u"沒有可聚焦的元件"};
        auto it = std::find(order.begin(), order.end(), focus_);
        auto n = static_cast<long>(order.size());
        long index = it == order.end() ? (key.shift ? 0 : -1) : static_cast<long>(it - order.begin());
        index = (index + (key.shift ? -1 : 1) + n) % n;
        return move_focus(order[static_cast<std::size_t>(index)]);
    }
    const auto* control = find(focus_);
    if (control) {
        if (auto result = available(focus_); !result) return result;
        if (auto it = text_scrolls_.find(focus_); it != text_scrolls_.end() && it->second.maximum > 0 && !key.ctrl && !key.shift) {
            auto& scroll = it->second;
            int next = scroll.state->offset;
            bool handled = true;
            if (key.key == Key::up) next -= scroll.line_height;
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
        bool handled = false;
        if (control->kind == Kind::text) {
            handled = key.ctrl ? (key.letter == u'A' || key.letter == u'C' || key.letter == u'X' || key.letter == u'V' || key.letter == u'Z' || key.letter == u'Y') :
                (key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end || key.key == Key::backspace || key.key == Key::del);
        } else if (!key.ctrl || !key.letter) {
            switch (control->kind) {
            case Kind::list: handled = key.key == Key::space || key.key == Key::up || key.key == Key::down || key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end; break;
            case Kind::navigation_list: handled = key.key == Key::up || key.key == Key::down || key.key == Key::home || key.key == Key::end; break;
            case Kind::radio_group: case Kind::slider: handled = key.key == Key::up || key.key == Key::down || key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end; break;
            case Kind::dropdown: handled = key.key == Key::enter || key.key == Key::space || (popup_source_ == focus_ && (key.key == Key::up || key.key == Key::down || key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end || key.key == Key::escape)); break;
            case Kind::button: case Kind::checkbox: case Kind::tabs: handled = key.key == Key::enter || key.key == Key::space || (control->kind == Kind::tabs && (key.key == Key::left || key.key == Key::right)); break;
            case Kind::label: case Kind::progress: break;
            default: handled = true; break;
            }
        }
        if (handled) return component_type(control->kind).key_down(*this, key);
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
Result detail::UiState::text_key_down(KeyPress key) {
    OperationScope operation(this); return edit_key(key); }
Result detail::UiState::command_key_down(KeyPress key) {
    OperationScope operation(this);
    if (key.repeat) return {};
    if (key.key == Key::enter) return invoke(focus_);
    if (key.key == Key::space) key_armed_ = focus_;
    return {};
}
void detail::UiState::command_pointer_down() {
    OperationScope operation(this); pointer_armed_ = focus_; }

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
    if (key.ctrl) {
        if (key.letter == u'Z') return travel(focus_, key.shift);
        if (key.letter == u'Y') return travel(focus_, true);
        if (key.letter == u'A') { session.select(0, size); return {}; }
        if (key.letter == u'C') return copy(focus_);
        if (key.letter == u'X') return cut(focus_);
        if (key.letter == u'V') {
            if (!clipboard_) return {Status::not_supported, u"\u526a\u8cbc\u7c3f\u4e0d\u53ef\u7528"};
            auto content = clipboard_->read();
            return content ? paste(focus_, *content) : Result{content.status, content.message};
        }
        return {};
    }
    switch (key.key) {
    case Key::left:
    case Key::right: {
        int direction = key.key == Key::left ? -1 : 1;
        if (!key.shift && session.begin() != session.end()) return move(direction < 0 ? session.begin() : session.end());
        return move(step(session.text, session.selection.caret, direction));
    }
    case Key::home: return move(0);
    case Key::end: return move(size);
    case Key::backspace:
    case Key::del: {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        if (session.begin() == session.end())
            session.select(session.selection.caret, step(session.text, session.selection.caret, key.key == Key::del ? 1 : -1));
        if (session.begin() == session.end()) return {};
        return replace_selection(focus_, *field, u"");
    }
    default: return {};
    }
}
Result detail::UiState::key_up(Key key) {
    OperationScope operation(this);
    if (key != Key::space || key_armed_.empty() || key_armed_ != focus_) return {};
    auto target = std::exchange(key_armed_, {});
    return invoke(target);
}
Result detail::UiState::pointer_down(std::string_view id, std::optional<std::size_t> option) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    const auto* control = find(id);
    if (auto result = focusable(*control); !result) return result;
    if (!popup_source_.empty() && popup_source_ != id) close_popup();
    if (control->kind == Kind::navigation_list && option) {
        const auto& pages = tabs_.at(control->id).pages;
        if (*option >= pages.size() || !pages[*option].enabled) return {};
    }
    pointer_focus_ = true; slider_drag_.reset();
    const Id target = control->id;
    if (auto result = move_focus(target); !result) return result;
    // Focus notifications can remove, disable, hide, or add controls. Revalidate and
    // reacquire the target before dispatch instead of retaining a vector pointer.
    refresh_tooltips();
    if (auto result = available(target); !result) return result;
    control = find(target);
    if (control->kind == Kind::dropdown) {
        pointer_armed_ = control->id; pointer_option_ = option; pointer_option_value_.reset();
        auto& f = dropdowns_.at(control->id);
        if (popup_source_ == target && option && *option < f.options.size()) pointer_option_value_ = f.options[*option].value;
    }
    else if (control->kind == Kind::list) {
        pointer_armed_ = control->id; pointer_option_ = option; pointer_option_value_.reset();
        const auto& items = lists_.at(control->id).items;
        if (option && *option < items.size()) pointer_option_value_ = items[*option].value;
    }
    else if (control->kind == Kind::radio_group || control->kind == Kind::slider || control->kind == Kind::tabs || control->kind == Kind::navigation_list) { pointer_armed_ = control->id; pointer_option_ = option; }
    else component_type(control->kind).pointer_down(*this);
    return {};
}
Result detail::UiState::pointer_up(std::string_view id, std::optional<std::size_t> option) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto armed = std::exchange(pointer_armed_, {}); slider_drag_.reset();
    auto pressed_option = std::exchange(pointer_option_, std::nullopt);
    auto pressed_value = std::exchange(pointer_option_value_, std::nullopt);
    if (armed.empty() || armed != id) return {};
    if (auto it = dropdowns_.find(armed); it != dropdowns_.end()) {
        if (pressed_value) {
            if (popup_source_ != armed || !option || *option >= it->second.options.size() || it->second.options[*option].value != *pressed_value) return {};
            close_popup(); return select_dropdown(armed, *pressed_value);
        }
        if (pressed_option || option) return {};
        return DropdownHandle(this, lifetime_, armed).open();
    }
    if (auto it = tabs_.find(armed); it != tabs_.end()) {
        if (!pressed_option || pressed_option != option || *option >= it->second.pages.size()) return {};
        return activate_tab(armed, it->second.pages[*option].value);
    }
    if (numerics_.contains(armed)) return {};
    if (auto it = lists_.find(armed); it != lists_.end()) {
        if (!pressed_value || !option || *option >= it->second.items.size() || it->second.items[*option].value != *pressed_value) return {};
        return ListHandle(this, lifetime_, armed).select(*pressed_value);
    }
    if (auto it = radios_.find(armed); it != radios_.end()) {
        if (!pressed_option || pressed_option != option || *option >= it->second.options.size()) return {};
        return select_option(armed, it->second.options[*option].value);
    }
    return invoke(armed);
}
void detail::UiState::cancel_input() {
    OperationScope operation(this); key_armed_.clear(); pointer_armed_.clear(); pointer_option_.reset(); pointer_option_value_.reset(); slider_drag_.reset(); }
Result detail::UiState::set_error(std::string_view id, std::u16string message) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"Component no longer exists"};
    if (control->kind == Kind::label) return {Status::not_supported, u"標籤不能帶有錯誤"};
    if (message.empty()) { if (auto it = errors_.find(id); it != errors_.end()) errors_.erase(it); }
    else errors_.insert_or_assign(control->id, std::move(message));
    return {};
}
void detail::UiState::enable(std::string_view id, bool enabled) {
    OperationScope operation(this);
    auto* control = const_cast<Control*>(find(id));
    if (!control) return;
    control->enabled = enabled;
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
    OperationScope operation(this);
    auto* control = const_cast<Control*>(find(id));
    auto it = edits_.find(id);
    if (!control || it == edits_.end() || !valid_single_line(value, control->max_length)) throw std::invalid_argument("Invalid owner refresh");
    if (composing_ == control->id) composition_cancel();
    auto& field = it->second;
    field.session.cancel_composition();
    if (field.session.text != value) ++field.session.revision;
    field.session.text = std::move(value);
    field.session.select(field.session.selection.anchor, field.session.selection.caret);
    field.past.clear(); field.future.clear(); field.offset = 0; field.offset_text_size = 0; field.offset_anchor = 0;
}
Ui::Ui() : state_(std::make_unique<detail::UiState>(std::vector<Control>{})), semantics_(new SemanticTree(*state_)) {}
SemanticTree& Ui::semantics() { return *semantics_; }
Ui::~Ui() = default;
void detail::UiState::seed_test_controls(std::vector<Control> controls) {
    for (auto& control : controls) {
        if (control.kind == Kind::text) add_text(std::move(control));
        else if (control.kind == Kind::button) add_button(std::move(control));
        else if (control.kind == Kind::label) add_label(std::move(control));
        else { (void)detail::component_type(control.kind); button_lifetimes_.emplace(control.id, std::make_shared<int>(0)); controls_.push_back(std::move(control)); }
    }
}
bool detail::UiState::registerable(std::string_view id) {
    const auto* existing = find(id);
    auto overlay = overlay_ids_.find(id);
    if (!id.empty() && !existing && overlay == overlay_ids_.end()) return true;
#ifndef NDEBUG
    const char* names[] = {"text", "button", "label", "checkbox", "radio_group", "slider", "dropdown", "list", "tabs", "progress", "navigation_list"};
    auto kind = existing ? static_cast<unsigned>(existing->kind) : 0;
    std::string type = existing ? (kind < std::size(names) ? names[kind] : "custom") : overlay != overlay_ids_.end() ? overlay->second : "none";
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
    try { return {Status::ok, {}, state_->add_text(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<ButtonHandle> Ui::add_button(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_button(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<LabelHandle> Ui::add_label(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_label(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<CheckboxHandle> Ui::add_checkbox(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_checkbox(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<RadioGroupHandle> Ui::add_radio_group(Control control, std::vector<RadioOption> options) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_radio_group(std::move(control), std::move(options))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<SliderHandle> Ui::add_slider(Control control, SliderRange range) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    if (range.minimum >= range.maximum || range.step <= 0) return {Status::invalid_value, u"Invalid slider range", std::nullopt};
    try { return {Status::ok, {}, state_->add_slider(std::move(control), range)}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<ProgressHandle> Ui::add_progress(Control control) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_progress(std::move(control))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<TabsHandle> Ui::add_tabs(Control control, std::vector<TabPage> pages) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_tabs(std::move(control), std::move(pages))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<NavigationListHandle> Ui::add_navigation_list(Control control, std::vector<NavigationPage> pages, OptionValue initial) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_navigation_list(std::move(control), std::move(pages), std::move(initial))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<DropdownHandle> Ui::add_dropdown(Control control, std::vector<RadioOption> options) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
    try { return {Status::ok, {}, state_->add_dropdown(std::move(control), std::move(options))}; }
    catch (const std::invalid_argument&) { return {Status::invalid_value, u"Invalid component", std::nullopt}; }
}
Query<ListHandle> Ui::add_list(Control control, std::vector<RadioOption> items) {
    if (auto access=state_->creation_access(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(control.id)) return {Status::invalid_value, u"Invalid or duplicate ID", std::nullopt};
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
Result Ui::connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
        std::function<ConfirmationResult(const std::vector<TextChange>&)> submit) {
    return state_->connect_confirmation(std::move(button), std::move(fields), std::move(submit));
}
Result Ui::connect_mixed_confirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<MixedConfirmationResult(const std::vector<FieldChange>&)> submit) {
    return state_->connect_mixed_confirmation(std::move(button), std::move(fields), std::move(submit));
}
void Ui::on_change(std::function<void()> callback) { state_->on_change(std::move(callback)); }
Result Ui::set_presentation(const PresentationSettings& settings) { return state_->set_presentation(settings); }
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
        } else store(p);
    }
    if (frame.modal && !active_dialog_.empty()) semantic_bounds_[active_dialog_] = frame.modal_panel;
#ifndef NDEBUG
    for (const auto& id : semantic_order_) {
        auto c = find(id); auto s = describe(id);
        if (c && s && c->kind != Kind::label && c->kind != Kind::progress && s->name.empty() && diagnosed_names_.insert(id).second)
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
static Role semantic_role(Kind kind) {
    switch (kind) {
    case Kind::text: return Role::text; case Kind::button: return Role::button; case Kind::label: return Role::label;
    case Kind::checkbox: return Role::checkbox; case Kind::radio_group: return Role::radio_group; case Kind::slider: return Role::slider;
    case Kind::dropdown: return Role::dropdown; case Kind::list: case Kind::navigation_list: return Role::list; case Kind::tabs: return Role::tabs; case Kind::progress: return Role::progress;
    }
    return Role::label;
}
Query<SemanticNode> SemanticTree::get(const NodeRef& ref) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrong_thread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
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
    auto s = *u.describe(ref.id); SemanticNode n; n.ref = ref; n.role = semantic_role(c->kind);
    auto [instance, inserted] = u.semantic_instances_.try_emplace(ref.id, 0);
    if (inserted) instance->second = ++u.next_semantic_instance_;
    n.instance = instance->second;
    n.name = s.name; n.value = s.value; n.description = s.description; n.error = s.error;
    if (!ref.item && c->kind == Kind::button) {
        for (const auto& binding : u.shortcuts_) {
            if (binding.target != ref.id) continue;
            const auto& chord = binding.chord;
            if (chord.ctrl) n.accelerator_key += u"Ctrl+";
            if (chord.shift) n.accelerator_key += u"Shift+";
            if (chord.letter) n.accelerator_key += chord.letter;
            else {
                switch (chord.key) {
                case Key::enter: n.accelerator_key += u"Enter"; break;
                case Key::space: n.accelerator_key += u"Space"; break;
                case Key::left: n.accelerator_key += u"Left"; break;
                case Key::right: n.accelerator_key += u"Right"; break;
                case Key::up: n.accelerator_key += u"Up"; break;
                case Key::down: n.accelerator_key += u"Down"; break;
                case Key::home: n.accelerator_key += u"Home"; break;
                case Key::end: n.accelerator_key += u"End"; break;
                case Key::backspace: n.accelerator_key += u"Backspace"; break;
                case Key::del: n.accelerator_key += u"Delete"; break;
                default: break;
                }
            }
            break;
        }
    }
    if (n.role == Role::label) n.name = s.value;
    n.enabled = s.enabled; n.read_only = s.read_only; n.focusable = s.focusable; n.focused = s.focused;
    n.expanded = s.expanded; n.invalid = s.invalid; n.executing = s.executing; n.checked = s.checked;
    n.selection_required = c->kind == Kind::navigation_list || c->kind == Kind::tabs;
    n.range = s.slider_range; n.numeric_value = s.numeric_value;
    n.hidden = u.available(ref.id, true).status == Status::hidden;
    n.blocked_by_modal = u.modal_ && c->page != "dialog";
    if (!n.hidden) { auto bounds = u.semantic_bounds_.find(ref.id); if (bounds != u.semantic_bounds_.end()) n.bounds = bounds->second; }
    n.visible = n.bounds.has_value();
    for (const auto& id : s.labels) { auto label = u.find(id); if (label && label->role == TextRole::name) n.labelled_by.push_back({id}); else if (label && (label->role == TextRole::description || label->role == TextRole::error)) n.described_by.push_back({id}); }
    auto edit = u.edits_.find(ref.id); if (edit != u.edits_.end()) { n.text_selection = edit->second.session.selection; n.text_revision = edit->second.session.revision; }
    if (ref.item) {
        auto it = std::find_if(s.options.begin(), s.options.end(), [&](const auto& o) { return o.value == *ref.item; });
        if (it == s.options.end()) return {Status::not_found, {}, {}};
        if (c->kind == Kind::radio_group) n.role = Role::radio_option;
        else if (c->kind == Kind::dropdown) n.role = Role::dropdown_option;
        else if (c->kind == Kind::list || c->kind == Kind::navigation_list) n.role = Role::list_item;
        else if (c->kind == Kind::tabs) n.role = Role::tab_item;
        else return {Status::not_found, {}, {}};
        n.name = it->label; n.value.clear(); n.selected = it->selected; n.enabled = s.enabled && it->enabled; n.invalid = it->invalid;
        n.focused = n.role == Role::list_item && s.focused && s.candidate && *s.candidate == *ref.item;
        n.bounds.reset(); auto pos = static_cast<size_t>(it - s.options.begin()); auto rects = u.semantic_options_.find(ref.id);
        if (!n.hidden && (c->kind != Kind::dropdown || s.expanded) && rects != u.semantic_options_.end() && pos < rects->second.size()) { auto r = rects->second[pos]; if (r.w > 0 && r.h > 0) n.bounds = r; }
        n.visible = n.bounds.has_value();
    }
    auto admits = [&](Operation op) { return std::find(s.operations.begin(), s.operations.end(), op) != s.operations.end(); };
    if (n.enabled && !n.hidden && !n.blocked_by_modal) {
        auto add = [&](Verb verb, bool condition) { if (condition) n.verbs.push_back(verb); };
        if (ref.item) {
            add(Verb::select, (u.tabs_.contains(ref.id) && admits(Operation::focus)) || admits(Operation::select));
            add(Verb::focus, n.role == Role::list_item && u.lists_.contains(ref.id) && admits(Operation::focus));
        } else {
            add(Verb::focus, admits(Operation::focus));
            add(Verb::invoke, n.role == Role::button && u.actions_.contains(ref.id) && admits(Operation::activate));
            add(Verb::toggle, n.role == Role::checkbox && admits(Operation::activate));
            add(Verb::set_value, (n.role == Role::text || n.role == Role::slider) && admits(Operation::edit));
            add(Verb::select, n.role == Role::text && admits(Operation::select));
            add(Verb::open, n.role == Role::dropdown && admits(Operation::select));
            add(Verb::cancel, n.role == Role::dropdown && admits(Operation::select));
            add(Verb::undo, n.role == Role::text && admits(Operation::undo));
            add(Verb::redo, n.role == Role::text && admits(Operation::redo));
            add(Verb::copy, n.role == Role::text && admits(Operation::copy));
            add(Verb::cut, n.role == Role::text && admits(Operation::cut));
            add(Verb::paste, n.role == Role::text && admits(Operation::paste));
            bool confirmed = (u.edits_.contains(ref.id) && bool(u.edits_.at(ref.id).confirmed)) ||
                (u.bools_.contains(ref.id) && bool(u.bools_.at(ref.id).confirmed)) ||
                (u.radios_.contains(ref.id) && bool(u.radios_.at(ref.id).confirmed)) ||
                (u.dropdowns_.contains(ref.id) && bool(u.dropdowns_.at(ref.id).confirmed)) ||
                (u.numerics_.contains(ref.id) && bool(u.numerics_.at(ref.id).confirmed));
            add(Verb::confirm, confirmed && !n.read_only);
        }
    }
    return {Status::ok, {}, n};
}
Query<std::vector<NodeRef>> SemanticTree::children(const NodeRef& parent) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrong_thread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
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
        }
    }
    return {Status::ok, {}, result};
}
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
    for (const auto& old : semantic_before_) if (!find(after, old.ref))
        emit(SemanticEventKind::structure_changed, old.ref.item ? NodeRef{old.ref.id} : NodeRef{});
    for (const auto& node : after) if (!node.ref.item) {
        std::vector<NodeRef> before_items, after_items;
        for (const auto& old : semantic_before_) if (old.ref.item && old.ref.id == node.ref.id) before_items.push_back(old.ref);
        for (const auto& current : after) if (current.ref.item && current.ref.id == node.ref.id) after_items.push_back(current.ref);
        if (before_items != after_items) emit(SemanticEventKind::structure_changed, node.ref);
    }
    for (const auto& node : after) {
        auto old = find(semantic_before_, node.ref);
        if (!old) { emit(SemanticEventKind::structure_changed, node.ref.item ? NodeRef{node.ref.id} : NodeRef{}); continue; }
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
    if (verb == Verb::hover) {
        if (node->role == Role::dialog || node->role == Role::tooltip) return {Status::not_supported, {}};
        return state_->simulate_hover(ref.id, ref.item);
    }
    auto invalid_argument = [&]() -> Result {
        if (auto result = state_->available(ref.id); !result) return result;
        if ((verb == Verb::set_value || verb == Verb::paste) && node->read_only) return {Status::read_only, {}};
        if (auto it = state_->edits_.find(ref.id); it != state_->edits_.end() && it->second.session.composing)
            return {Status::busy, {}};
        return {Status::invalid_value, {}};
    };
    if (!ref.item && verb == Verb::select && std::holds_alternative<OptionValue>(argument) &&
        (node->role == Role::radio_group || node->role == Role::dropdown || node->role == Role::list || node->role == Role::tabs))
        return invalid_argument();
    if (ref.item && node->role == Role::list_item && state_->lists_.contains(ref.id) && verb == Verb::focus) {
        auto handle = state_->layout_handle(ref.id);
        return std::get<ListHandle>(handle).focus(*ref.item);
    }
    if (!ref.item && verb == Verb::focus && (node->role != Role::label || node->focusable) && node->role != Role::progress && node->role != Role::dialog && node->role != Role::tooltip)
        return state_->focus(ref.id);
    if (node->role == Role::dialog && verb == Verb::cancel) return state_->settle_dialog(ref.id, false);
    if (node->role == Role::button && verb == Verb::invoke) return state_->invoke(ref.id);
    if (node->role == Role::text && verb == Verb::set_value) {
        if (auto value = std::get_if<std::u16string>(&argument)) return state_->set_value(ref.id, *value);
        return invalid_argument();
    }
    if (node->role == Role::text) {
        if (verb == Verb::select) {
            if (auto selection = std::get_if<Selection>(&argument)) return state_->select(ref.id, selection->anchor, selection->caret);
            return invalid_argument();
        }
        if (verb == Verb::undo) return state_->undo(ref.id);
        if (verb == Verb::redo) return state_->redo(ref.id);
        if (verb == Verb::copy) return state_->copy(ref.id);
        if (verb == Verb::cut) return state_->cut(ref.id);
        if (verb == Verb::paste) {
            if (auto value = std::get_if<std::u16string>(&argument)) return state_->paste(ref.id, *value);
            return invalid_argument();
        }
    }
    auto handle = state_->layout_handle(ref.id);
    if (!ref.item && verb == Verb::confirm) {
        if (auto text = std::get_if<TextHandle>(&handle); text && state_->edits_.at(ref.id).confirmed) return text->confirm();
        if (auto checkbox = std::get_if<CheckboxHandle>(&handle); checkbox && state_->bools_.at(ref.id).confirmed) return checkbox->confirm();
        if (auto radio = std::get_if<RadioGroupHandle>(&handle); radio && state_->radios_.at(ref.id).confirmed) return radio->confirm();
        if (auto slider = std::get_if<SliderHandle>(&handle); slider && state_->numerics_.at(ref.id).confirmed) return slider->confirm();
        if (auto dropdown = std::get_if<DropdownHandle>(&handle); dropdown && state_->dropdowns_.at(ref.id).confirmed) return dropdown->confirm();
    }
    if (auto tabs = std::get_if<TabsHandle>(&handle); tabs && ref.item && verb == Verb::select) return tabs->activate(*ref.item);
    if (auto nav = std::get_if<NavigationListHandle>(&handle); nav && ref.item && verb == Verb::select) return nav->select(*ref.item);
    if (auto list = std::get_if<ListHandle>(&handle); list && ref.item && verb == Verb::select) {
        detail::UiState::OperationScope operation(state_);
        auto previous = state_->lists_.at(ref.id).cursor;
        auto result = list->select(*ref.item);
        if (auto it = state_->lists_.find(ref.id); it != state_->lists_.end()) it->second.cursor = previous;
        return result;
    }
    if (auto checkbox = std::get_if<CheckboxHandle>(&handle); checkbox && verb == Verb::toggle) return checkbox->toggle();
    if (auto radio = std::get_if<RadioGroupHandle>(&handle); radio && ref.item && verb == Verb::select) return radio->select(*ref.item);
    if (auto dropdown = std::get_if<DropdownHandle>(&handle)) {
        if (ref.item && verb == Verb::select) return dropdown->select(*ref.item);
        if (!ref.item && verb == Verb::open) return dropdown->open();
        if (!ref.item && verb == Verb::cancel) return dropdown->cancel();
    }
    if (auto slider = std::get_if<SliderHandle>(&handle); slider && verb == Verb::set_value) {
        if (auto value = std::get_if<int>(&argument)) return slider->set_value(*value);
        return invalid_argument();
    }
    return {Status::not_supported, {}};
}
}
