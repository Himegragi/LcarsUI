#include "core_internal.hpp"
#include "component_type.hpp"
#include <stdexcept>
#include <cmath>
namespace lcars {
namespace {
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
detail::UiState::UiState(std::vector<Control> controls) : controls_(std::move(controls)) {
    std::map<Id, bool> ids;
    for (const auto& control : controls_) {
        (void)detail::component_type(control.kind);
        if (control.kind == Kind::slider || control.kind == Kind::dropdown || control.kind == Kind::list || control.kind == Kind::tabs || control.kind == Kind::progress) throw std::invalid_argument("Component requires typed creation");
        if (control.id.empty() || !ids.emplace(control.id, true).second)
            throw std::invalid_argument("Control IDs must be nonempty and unique");
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
    for (auto& [id, field] : edits_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : bools_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : radios_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : dropdowns_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : lists_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : numerics_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : progresses_) if (field.disconnect) field.disconnect();
}
ProgressHandle detail::UiState::add_progress(Control control) {
    if (control.kind != Kind::progress || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid progress component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    progresses_.emplace(id, ProgressField{});
    return ProgressHandle(this, lifetime_, std::move(id));
}
ComponentHandle detail::UiState::progress_layout_handle(const Control& c) { return ProgressHandle(this, lifetime_, c.id); }
Result ProgressHandle::connect_source(NumericSource source) const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
TextHandle detail::UiState::add_text(Control control) {
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
    if (control.kind != Kind::button || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid button component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    return ButtonHandle(this, lifetime_, std::move(id));
}
LabelHandle detail::UiState::add_label(Control control) {
    if (control.kind != Kind::label || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid label component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    return LabelHandle(this, lifetime_, std::move(id));
}
CheckboxHandle detail::UiState::add_checkbox(Control control) {
    if (control.kind != Kind::checkbox || control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid checkbox component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    bools_.emplace(id, BoolField{});
    bind(id, [this, id] { return toggle_checkbox(id); });
    return CheckboxHandle(this, lifetime_, std::move(id));
}
ComponentHandle detail::UiState::checkbox_layout_handle(const Control& c) { return CheckboxHandle(this, lifetime_, c.id); }
Result CheckboxHandle::toggle() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->toggle_checkbox(id_);
}
Result CheckboxHandle::use_latest() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->bools_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
}
Result CheckboxHandle::focus() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result CheckboxHandle::enable(bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result CheckboxHandle::connect_source(BoolSource source) const {
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
    auto& field = bools_.at(id);
    bool value = field.source.read();
    bool clean = !field.connected || field.value == field.source_value;
    field.source_value = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result CheckboxHandle::connect_immediate(std::function<Result(const BoolChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& field = ui_->bools_.at(id_);
    if (!field.connected || field.confirmation_member || field.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    field.immediate = std::move(write); return {};
}
Result CheckboxHandle::connect_confirmed(std::function<Result(const BoolChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->bools_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result CheckboxHandle::confirm() const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<bool> CheckboxHandle::draft() const {
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->bools_.at(id_).value};
}
RadioGroupHandle detail::UiState::add_radio_group(Control control, std::vector<RadioOption> options) {
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
ComponentHandle detail::UiState::radio_layout_handle(const Control& c) { return RadioGroupHandle(this, lifetime_, c.id); }
ListHandle detail::UiState::add_list(Control control, std::vector<RadioOption> items) {
    if (control.kind != Kind::list || control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid list");
    std::set<std::string> ids;
    for (const auto& item : items) if (item.value.id.empty() || !ids.insert(item.value.id).second) throw std::invalid_argument("Invalid list identity");
    auto id = control.id; controls_.push_back(std::move(control));
    ListField field; field.items = std::move(items); lists_.emplace(id, std::move(field));
    return ListHandle(this, lifetime_, std::move(id));
}
ComponentHandle detail::UiState::list_layout_handle(const Control& c) { return ListHandle(this, lifetime_, c.id); }
Result ListHandle::connect_source(OptionSource source) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (!source.read || !source.subscribe) return {Status::invalid_value, u"An option source requires read and subscribe"};
    auto& f = ui_->lists_.at(id_); if (f.disconnect) f.disconnect();
    f.source = std::move(source); auto weak = lifetime_; auto* state = ui_;
    f.disconnect = f.source.subscribe([weak, state] { if (!weak.expired() && state->invalidate_) state->invalidate_(); });
    return {};
}
Result ListHandle::connect_immediate(std::function<Result(std::optional<OptionValue>)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->lists_.at(id_); if (!f.source.read || !write) return {Status::invalid_value, u"A source and write are required"};
    f.immediate = std::move(write); return {};
}
Result ListHandle::select(OptionValue value) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->lists_.at(id_);
    if (ui_->find(id_)->read_only) return {Status::read_only, u"List is read only"};
    if (!f.source.read || !f.immediate) return {Status::invalid_value, u"Connect an immediate write"};
    if (std::none_of(f.items.begin(), f.items.end(), [&](const auto& item) { return item.enabled && item.value == value; })) return {Status::invalid_value, u"Invalid list item"};
    if (ui_->dispatching_ || f.writing) return {Status::busy, u"Operation is already executing"};
    if (f.source.read() == value) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); return {}; }
    f.writing = true; Result r;
    try { r = f.immediate(value); }
    catch (...) { f.writing = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.writing = false;
    if (r) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); auto n = ui_->notify(); if (!n) return n; } return r;
}
Result detail::UiState::list_key_down(KeyPress key) {
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
Result ListHandle::focus() const { if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"}; return ui_->focus(id_); }
Result ListHandle::enable(bool enabled) const { if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"}; if (auto r = ui_->configuration_access(id_); !r) return r; ui_->enable(id_, enabled); return {}; }
Query<Semantics> ListHandle::describe() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Result RadioGroupHandle::use_latest() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->radios_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
}
Result RadioGroupHandle::focus() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result RadioGroupHandle::enable(bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result RadioGroupHandle::enable_option(OptionValue value, bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& options = ui_->radios_.at(id_).options;
    auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.value == value; });
    if (it == options.end()) return {Status::invalid_value, u"Unknown option"};
    it->enabled = enabled; if (ui_->invalidate_) ui_->invalidate_(); return {};
}
Result RadioGroupHandle::connect_source(OptionSource source) const {
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
    auto& field = radios_.at(id); auto value = field.source.read();
    bool clean = !field.connected || field.value == field.source_value;
    field.source_value = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result RadioGroupHandle::connect_immediate(std::function<Result(const OptionChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& field = ui_->radios_.at(id_);
    if (!field.connected || field.confirmation_member || field.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    field.immediate = std::move(write); return {};
}
Result RadioGroupHandle::connect_confirmed(std::function<Result(const OptionChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->radios_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result RadioGroupHandle::confirm() const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->select_option(id_, std::move(value));
}
Result detail::UiState::select_option(const Id& id, OptionValue value) {
    if (auto r = available(id); !r) return r;
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<std::optional<OptionValue>> RadioGroupHandle::draft() const {
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->radios_.at(id_).value};
}
DropdownHandle detail::UiState::add_dropdown(Control control, std::vector<RadioOption> options) {
    if (control.kind != Kind::dropdown || control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid dropdown");
    std::set<std::string> ids;
    for (const auto& option : options)
        if (option.value.id.empty() || !ids.insert(option.value.id).second) throw std::invalid_argument("Invalid dropdown option");
    auto id = control.id; controls_.push_back(std::move(control));
    DropdownField field; field.options = std::move(options); dropdowns_.emplace(id, std::move(field));
    return DropdownHandle(this, lifetime_, std::move(id));
}
ComponentHandle detail::UiState::dropdown_layout_handle(const Control& c) { return DropdownHandle(this, lifetime_, c.id); }
void detail::UiState::close_popup() {
    if (popup_source_.empty()) return;
    if (auto it = dropdowns_.find(popup_source_); it != dropdowns_.end()) it->second.candidate.reset();
    popup_source_.clear();
    if (invalidate_) invalidate_();
}
Result DropdownHandle::open() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.connected || (!f.confirmation_member && !f.immediate && !f.confirmed)) return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (auto r = ui_->focus(id_); !r) return r;
    ui_->close_popup(); ui_->popup_source_ = id_;
    auto current = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.enabled && f.value && o.value == *f.value; });
    if (current == f.options.end()) current = std::find_if(f.options.begin(), f.options.end(), [](const auto& o) { return o.enabled; });
    f.candidate = current == f.options.end() ? std::nullopt : std::optional<OptionValue>(current->value);
    if (ui_->invalidate_) ui_->invalidate_();
    return {};
}
Result DropdownHandle::cancel() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    if (ui_->popup_source_ == id_) ui_->close_popup();
    return {};
}
Result DropdownHandle::focus() const { if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"}; return ui_->focus(id_); }
Result DropdownHandle::enable(bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result DropdownHandle::set_options(std::vector<RadioOption> options) const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.connected || f.confirmation_member || f.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.immediate = std::move(write); return {};
}
Result DropdownHandle::connect_confirmed(std::function<Result(const DropdownChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result DropdownHandle::confirm() const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->dropdowns_.at(id_); if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return f.source_value && o.enabled && o.value == *f.source_value; });
    f.value = it == f.options.end() ? std::nullopt : f.source_value; f.origin = f.source_value;
    if (ui_->invalidate_) ui_->invalidate_();
    ui_->set_error(id_, u""); return ui_->notify();
}
Result DropdownHandle::select(OptionValue value) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->select_dropdown(id_, std::move(value));
}
Result detail::UiState::select_dropdown(const Id& id, OptionValue value) {
    if (auto r = available(id); !r) return r;
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
    auto& f = dropdowns_.at(focus_);
    if (key.key == Key::escape) { close_popup(); return {}; }
    if (key.key == Key::enter || key.key == Key::space) {
        if (key.repeat) return {};
        if (popup_source_ != focus_) return DropdownHandle(this, lifetime_, focus_).open();
        auto candidate = f.candidate; close_popup(); return candidate ? select_dropdown(focus_, *candidate) : Result{};
    }
    if (popup_source_ != focus_) return {};
    int direction = key.key == Key::down || key.key == Key::right ? 1 : key.key == Key::up || key.key == Key::left ? -1 : 0;
    if (!direction) return {};
    int n = static_cast<int>(f.options.size()), index = direction > 0 ? -1 : n;
    for (int i = 0; i < n; ++i) if (f.candidate && f.options[i].value == *f.candidate) { index = i; break; }
    for (int i = index + direction; i >= 0 && i < n; i += direction) if (f.options[i].enabled) { f.candidate = f.options[i].value; break; }
    if (invalidate_) invalidate_(); return {};
}
Query<Semantics> DropdownHandle::describe() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<std::optional<OptionValue>> DropdownHandle::draft() const {
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
    if (control.kind != Kind::slider || control.id.empty() || find(control.id) || range.minimum >= range.maximum || range.step <= 0)
        throw std::invalid_argument("Invalid slider component or range");
    auto id = control.id;
    controls_.push_back(std::move(control));
    NumericField field{}; field.range = range; field.value = field.source_value = field.origin = range.minimum;
    numerics_.emplace(id, std::move(field));
    return SliderHandle(this, lifetime_, std::move(id));
}
ComponentHandle detail::UiState::slider_layout_handle(const Control& c) { return SliderHandle(this, lifetime_, c.id); }
Result SliderHandle::set_value(int value) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->slider_set_value(id_, value);
}
Result SliderHandle::focus() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result SliderHandle::enable(bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->enable(id_, enabled); return {};
}
Result SliderHandle::use_latest() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->available(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.connected) return {Status::invalid_value, u"Source is not connected"};
    f.value = f.origin = f.source_value; ui_->set_error(id_, u""); return ui_->notify();
}
Result SliderHandle::connect_source(NumericSource source) const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.connected || f.confirmation_member || f.confirmed || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.immediate = std::move(write); return {};
}
Result SliderHandle::connect_confirmed(std::function<Result(const NumericChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    auto& f = ui_->numerics_.at(id_);
    if (!f.connected || f.confirmation_member || f.immediate || !write) return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    f.confirmed = std::move(write); return {};
}
Result SliderHandle::confirm() const {
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
Result detail::UiState::slider_pointer(const Id& id, int x, Rect track) {
    if (pointer_armed_ != id || track.w <= 0) return {Status::invalid_value, u"No slider drag is active"};
    auto& f = numerics_.at(id);
    double ratio = std::clamp((static_cast<double>(x) - track.x) / track.w, 0.0, 1.0);
    double value = static_cast<double>(f.range.minimum) + ratio * (static_cast<double>(f.range.maximum) - f.range.minimum);
    return slider_set_value(id, static_cast<int>(std::floor(value + 0.5)));
}
Result detail::UiState::slider_key_down(KeyPress key) {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<int> SliderHandle::draft() const {
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->numerics_.at(id_).value};
}
ComponentHandle detail::UiState::layout_handle(std::string_view id) {
    auto control = find(id);
    if (!control) return {};
    return component_type(control->kind).handle(*this, *control);
}
ComponentHandle detail::UiState::text_layout_handle(const Control& control) { return TextHandle(this, lifetime_, control.id); }
ComponentHandle detail::UiState::label_layout_handle(const Control& control) { return LabelHandle(this, lifetime_, control.id); }
ComponentHandle detail::UiState::button_layout_handle(const Control& control) { return ButtonHandle(this, lifetime_, control.id); }

std::optional<Id> detail::UiState::layout_id(const ComponentHandle& handle) const {
    return std::visit([this](const auto& item) -> std::optional<Id> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
        else return item.ui_ == this && !item.lifetime_.expired() ? std::optional<Id>(item.id_) : std::nullopt;
    }, handle);
}
TabsHandle detail::UiState::add_tabs(Control control, std::vector<TabPage> pages) {
    if (control.kind != Kind::tabs || control.id.empty() || find(control.id) || pages.empty() || !pages.front().enabled)
        throw std::invalid_argument("Invalid tabs component");
    std::set<std::string> values;
    std::set<Id> members;
    for (const auto& page : pages) {
        if (page.value.id.empty() || !values.insert(page.value.id).second) throw std::invalid_argument("Duplicate page identity");
        for (const auto& handle : page.members) {
            auto member = layout_id(handle);
            if (!member || page_members_.contains(*member) || !members.insert(*member).second)
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
ComponentHandle detail::UiState::tabs_layout_handle(const Control& c) { return TabsHandle(this, lifetime_, c.id); }
TooltipHandle detail::UiState::add_tooltip(ComponentHandle source, std::u16string text, unsigned delay_ms) {
    auto id = layout_id(source);
    if (!id || !find(*id) || removed(*id) || text.empty() || tooltips_.contains(*id))
        throw std::invalid_argument("Invalid tooltip source or text");
    tooltips_.emplace(*id, TooltipState{std::move(text), delay_ms});
    refresh_tooltips();
    return TooltipHandle(this, lifetime_, *id);
}
void detail::UiState::refresh_tooltips() {
    if (tooltip_clock_) tooltip_now_ = tooltip_clock_();
    bool changed = false;
    for (auto& [id, tip] : tooltips_) {
        bool eligible = window_active_ && !modal_ && ((focus_ == id && !pointer_focus_) || tooltip_hover_ == id) && bool(available(id));
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
    tooltip_now_ = now;
    refresh_tooltips();
}
void detail::UiState::tooltip_hover(std::string_view id) {
    if (tooltip_hover_ == id) return;
    tooltip_hover_ = Id(id);
    if (auto it = tooltips_.find(tooltip_hover_); it != tooltips_.end() && it->second.dismissed) {
        it->second.eligible = false;
        it->second.dismissed = false;
    }
    refresh_tooltips();
}
void detail::UiState::window_active(bool active) {
    if (window_active_ == active) return;
    window_active_ = active;
    if (!active) tooltip_hover_.clear();
    refresh_tooltips();
}
Query<bool> TooltipHandle::visible() const {
    if (lifetime_.expired()) return {Status::not_found, u"UI no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(source_); !r) return {r.status, r.message, std::nullopt};
    auto it = ui_->tooltips_.find(source_);
    if (it == ui_->tooltips_.end()) return {Status::not_found, u"Tooltip no longer exists", std::nullopt};
    return {Status::ok, {}, it->second.visible};
}
DialogHandle detail::UiState::add_dialog(std::vector<ComponentHandle> members, ButtonHandle initial, ButtonHandle fallback, std::function<void(bool)> result) {
    auto first = layout_id(initial), backup = layout_id(fallback);
    if (dialog_.configured || !first || !backup || !find(*first) || !find(*backup) || removed(*first) || removed(*backup) || !result || members.empty()) throw std::invalid_argument("Invalid dialog");
    std::set<Id> unique;
    for (const auto& member : members) {
        auto id = layout_id(member);
        if (!id || !find(*id) || removed(*id) || page_members_.contains(*id) || !unique.insert(*id).second)
            throw std::invalid_argument("Invalid dialog member");
    }
    if (!unique.contains(*first) || unique.contains(*backup)) throw std::invalid_argument("Invalid dialog focus targets");
    dialog_.members.assign(unique.begin(), unique.end());
    dialog_.initial = *first; dialog_.fallback = *backup; dialog_.result = std::move(result); dialog_.configured = true;
    for (const auto& id : dialog_.members) const_cast<Control*>(find(id))->page = "dialog";
    return DialogHandle(this, lifetime_);
}
Result detail::UiState::open_dialog(const ComponentHandle& opener) {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!dialog_.configured || modal_ || dialog_.transitioning) return {Status::busy, u"Dialog already open or transitioning"};
    auto source = layout_id(opener);
    if (!source) return {Status::not_found, u"Invalid opener"};
    if (auto r = available(*source); !r) return r;
    const auto* initial = find(dialog_.initial);
    if (!initial || !initial->enabled || removed(dialog_.initial)) return {Status::disabled, u"Initial focus is unavailable"};
    dialog_.transitioning = true;
    struct Guard { bool& value; ~Guard() { value = false; } } guard{dialog_.transitioning};
    dialog_.opener = *source;
    // Finish the previous edit while background operations are still admitted.
    if (auto r = move_focus(dialog_.initial); !r) return r;
    modal(true);
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::settle_dialog(bool confirmed) {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (dialog_.transitioning) return {Status::busy, u"Dialog is transitioning"};
    if (!modal_ || !dialog_.configured) return {Status::invalid_value, u"Dialog is not open"};
    dialog_.transitioning = true;
    struct Guard { bool& value; ~Guard() { value = false; } } guard{dialog_.transitioning};
    modal(false);
    auto source = std::exchange(dialog_.opener, {});
    auto target = find(source);
    if (!target || !focusable(*target)) target = find(dialog_.fallback);
    if (target && focusable(*target)) move_focus(target->id); else move_focus({});
    try { dialog_.result(confirmed); }
    catch (...) { stopped_ = true; return {Status::failed, u"Dialog result failed"}; }
    if (invalidate_) invalidate_();
    return {};
}
Result DialogHandle::open(ComponentHandle opener) const {
    return lifetime_.expired() ? Result{Status::not_found, u"UI no longer exists"} : ui_->open_dialog(opener);
}
Result DialogHandle::settle(bool confirmed) const {
    return lifetime_.expired() ? Result{Status::not_found, u"UI no longer exists"} : ui_->settle_dialog(confirmed);
}
Query<bool> DialogHandle::is_open() const {
    if (lifetime_.expired()) return {Status::not_found, u"UI no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(ui_->dialog_.initial); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->modal_};
}
Result detail::UiState::activate_tab(const Id& id, OptionValue page) {
    if (auto r = available(id); !r) return r;
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
    if (auto member = page_members_.find(id); member != page_members_.end()) {
        auto& tabs = tabs_.at(member->second.first);
        auto page = tabs.pages[member->second.second].value;
        if (auto result = activate_tab(member->second.first, page); !result) return result;
    }
    return focus(id);
}
Result TabsHandle::activate(OptionValue page) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->activate_tab(id_, std::move(page));
}
Result detail::UiState::add_page_button(const Id& id, OptionValue page, ButtonHandle button) {
    if (auto r = configuration_access(id); !r) return r;
    auto member = layout_id(button);
    if (!member || !find(*member) || removed(*member) || page_members_.contains(*member) ||
        std::find(dialog_.members.begin(), dialog_.members.end(), *member) != dialog_.members.end())
        return {Status::invalid_value, u"Invalid page member"};
    auto& t = tabs_.at(id);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalid_value, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    it->members.push_back(button);
    page_members_.emplace(*member, std::make_pair(id, index));
    if (invalidate_) invalidate_();
    return {};
}
Result TabsHandle::add_button(OptionValue page, ButtonHandle button) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->add_page_button(id_, std::move(page), button);
}
Result TabsHandle::focus() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result TabsHandle::enable_page(OptionValue page, bool enabled) const {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    ui_->tabs_.at(id_).changed = std::move(callback); return {};
}
Query<Semantics> TabsHandle::describe() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto r = ui_->configuration_access(id_); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<OptionValue> TabsHandle::active() const {
    auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
    return {Status::ok, {}, ui_->tabs_.at(id_).pages[ui_->tabs_.at(id_).active].value};
}
Result detail::UiState::tabs_key_down(KeyPress key) {
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
Result LabelHandle::connect_source(std::function<std::u16string()> read) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (!read) return {Status::invalid_value, u"A label source requires a reader"};
    ui_->bind_text(id_, std::move(read));
    return {};
}
Result ButtonHandle::focus() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result ButtonHandle::bind(std::function<Result()> action) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (!action) return {Status::invalid_value, u"An action is required"};
    if (ui_->confirmation_buttons_.contains(id_)) return {Status::invalid_value, u"Confirmation action cannot be rebound"};
    ui_->bind(id_, std::move(action));
    return {};
}
Result ButtonHandle::enable(bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    ui_->enable(id_, enabled);
    return {};
}
Result ButtonHandle::remove() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto r = ui_->configuration_access(id_); !r) return r;
    if (ui_->executing_ == id_) return {Status::busy, u"Button action is executing"};
    if (std::find(ui_->dialog_.members.begin(), ui_->dialog_.members.end(), id_) != ui_->dialog_.members.end() || id_ == ui_->dialog_.fallback)
        return {Status::invalid_value, u"Dialog focus targets must remain alive"};
    ui_->removed_.insert(id_);
    ui_->refresh_tooltips();
    if (ui_->focus_ == id_) ui_->move_focus({});
    ui_->actions_.erase(id_);
    if (ui_->invalidate_) ui_->invalidate_();
    return {};
}
Result TextHandle::focus() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->focus(id_);
}
Result TextHandle::on_blur(std::function<void()> callback) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    if (callback) ui_->blur_callbacks_.insert_or_assign(id_, std::move(callback));
    else ui_->blur_callbacks_.erase(id_);
    return {};
}
Result TextHandle::set_error(std::u16string message) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->set_error(id_, std::move(message));
}
Result TextHandle::enable(bool enabled) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    ui_->enable(id_, enabled);
    return {};
}
Result TextHandle::read_only(bool is_read_only) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->configuration_access(id_); !result) return result;
    ui_->set_read_only(id_, is_read_only);
    return {};
}
Result TextHandle::reset_to_source() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->reset_to_source(id_);
}
Result detail::UiState::reset_to_source(const Id& id) {
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.has_source_value)
        return {Status::invalid_value, u"Text source is not connected"};
    replace_text(id, it->second.source_value);
    it->second.origin = it->second.source_value;
    return set_error(id, u"");
}
Result ButtonHandle::invoke() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->invoke(id_);
}
Query<bool> ButtonHandle::executing() const {
    auto snapshot = describe();
    if (!snapshot) return {snapshot.status, snapshot.message, std::nullopt};
    return {Status::ok, {}, snapshot->executing};
}
Result TextHandle::paste(std::u16string_view text) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->paste(id_, text);
}
Result TextHandle::undo() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->undo(id_);
}
Result TextHandle::redo() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->redo(id_);
}
Result TextHandle::select(unsigned anchor, unsigned caret) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->select(id_, anchor, caret);
}
Result TextHandle::copy() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->copy(id_);
}
Result TextHandle::cut() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->cut(id_);
}
Result detail::UiState::configuration_access(std::string_view id) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!find(id) || removed(id)) return {Status::not_found, u"Component no longer exists"};
    return {};
}
Result detail::UiState::connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
                                std::function<ConfirmationResult(const std::vector<TextChange>&)> submit) {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_write(id_, std::move(write), true);
}
Result TextHandle::connect_confirmed(std::function<Result(const TextChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_write(id_, std::move(write), false);
}
Result detail::UiState::connect_write(const Id& id, std::function<Result(const TextChange&)> write, bool immediate) {
    if (auto result = configuration_access(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.has_source_value || it->second.confirmation_member ||
        (immediate ? bool(it->second.confirmed) : bool(it->second.immediate)) || !write)
        return {Status::invalid_value, u"Write needs an ungrouped connected source"};
    (immediate ? it->second.immediate : it->second.confirmed) = std::move(write);
    return {};
}
Result TextHandle::confirm() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->confirm(id_);
}
Result detail::UiState::confirm(const Id& id) {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_source(id_, std::move(source));
}
Result detail::UiState::connect_source(const Id& id, TextSource source) {
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
    auto snapshot = describe();
    if (!snapshot) return {snapshot.status, snapshot.message, std::nullopt};
    return {Status::ok, {}, snapshot->source_conflict};
}
Result TextHandle::use_latest() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->use_latest(id_);
}
Result detail::UiState::use_latest(const Id& id) {
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
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->set_value(id_, std::move(value));
}
Query<Semantics> TextHandle::describe() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto result = ui_->configuration_access(id_); !result) return {result.status, result.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<std::u16string> TextHandle::draft() const {
    auto snapshot = describe();
    if (!snapshot) return {snapshot.status, snapshot.message, std::nullopt};
    return {Status::ok, {}, snapshot->value};
}
Query<Semantics> ButtonHandle::describe() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto result = ui_->configuration_access(id_); !result) return {result.status, result.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
Query<Semantics> LabelHandle::describe() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists", std::nullopt};
    if (auto result = ui_->configuration_access(id_); !result) return {result.status, result.message, std::nullopt};
    return {Status::ok, {}, ui_->describe(id_)};
}
std::optional<Semantics> detail::UiState::describe(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return std::nullopt;
    Semantics s;
    s.id = control->id; s.kind = control->kind; s.target = control->target; s.role = control->role;
    s.enabled = control->enabled; s.read_only = control->read_only;
    s.focusable = bool(focusable(*control));
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
    if (!s.focusable) return s;
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
    const auto* control = find(id);
    if (!control || control->kind != Kind::label || !provider) throw std::invalid_argument("Text binding requires a label and a provider");
    providers_.insert_or_assign(control->id, std::move(provider));
}
const Control* detail::UiState::find(std::string_view id) const {
    auto it = std::find_if(controls_.begin(), controls_.end(), [&](const auto& control) { return control.id == id; });
    return it == controls_.end() ? nullptr : &*it;
}
EditSession* detail::UiState::edit(std::string_view id) {
    auto it = edits_.find(id); return it == edits_.end() ? nullptr : &it->second.session;
}
Result detail::UiState::available(std::string_view id) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto control = find(id);
    if (!control || removed(id)) return {Status::not_found, u"Component no longer exists"};
    if (auto member = page_members_.find(id); member != page_members_.end() && tabs_.at(member->second.first).active != member->second.second)
        return {Status::hidden, u"Page is hidden"};
    if (!control->enabled) return {Status::disabled, u"目前無法執行"};
    if (control->page == "dialog" && !modal_) return {Status::disabled, u"對話框尚未開啟"};
    if (modal_ && control->page != "dialog") return {Status::blocked_by_modal, u"請先關閉對話框"};
    return {};
}
Result detail::UiState::set_value(std::string_view id, std::u16string value) {
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.text == value) return {};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    return replace_selection(id, *field, value);
}
Result detail::UiState::writable(std::string_view id, Field*& field) {
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (!it->second.has_source_value || (!it->second.confirmation_member && !it->second.immediate && !it->second.confirmed))
        return {Status::invalid_value, u"Connect an explicit write timing before editing"};
    if (find(id)->read_only) return {Status::read_only, u"欄位為唯讀"};
    if (it->second.session.composing || dispatching_) return {Status::busy, u"操作尚未結束"};
    field = &it->second;
    return {};
}
// Every content change funnels through here so history, value mirror and notification agree.
Result detail::UiState::replace_selection(std::string_view id, Field& field, std::u16string_view insertion) {
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
    session.select(caret, caret);
    return finish_edit(id, field);
}
Result detail::UiState::travel(std::string_view id, bool forward) {
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
    if (field.immediate && !field.submitting) return submit_field(id, field, field.immediate, false);
    return notify();
}
Result detail::UiState::undo(std::string_view id) { return travel(id, false); }
Result detail::UiState::redo(std::string_view id) { return travel(id, true); }
Result detail::UiState::notify() {
    dispatching_ = true;
    try { if (changed_) changed_(); }
    catch (...) { stopped_ = true; dispatching_ = false; return {Status::failed, u"資料更新失敗，介面已停止"}; }
    dispatching_ = false;
    return {};
}
Result detail::UiState::text_input(std::u16string_view text) {
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
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    return replace_selection(id, *field, paste_as_single_line(text));
}
Result detail::UiState::copy(std::string_view id) {
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
    if (auto it = edits_.find(id); it != edits_.end()) it->second.offset = std::max(0.f, offset);
}
Result detail::UiState::select(std::string_view id, unsigned anchor, unsigned caret) {
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (it->second.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    it->second.session.select(anchor, caret);
    return {};
}
Result detail::UiState::invoke(std::string_view id) {
    if (auto result = available(id); !result) return result;
    if (dispatching_ || invoking_) return {Status::busy, u"操作尚未結束"};
    for (const auto& [key, field] : edits_)
        if (field.session.composing) return {Status::busy, u"請先完成文字組字"};
    auto it = actions_.find(id);
    if (it == actions_.end()) return {Status::not_found, u"元件沒有此動作"};
    invoking_ = true;
    executing_ = id;
    try { auto result = it->second(); invoking_ = false; executing_.clear(); return result; }
    catch (...) { stopped_ = true; invoking_ = false; executing_.clear(); return {Status::failed, u"操作失敗，介面已停止"}; }
}
void detail::UiState::bind(Id id, Action action) {
    if (!find(id) || !action) throw std::invalid_argument("Action requires a known ID and a callback");
    actions_.insert_or_assign(std::move(id), std::move(action));
}
Result detail::UiState::focusable(const Control& control) const {
    if (!component_type(control.kind).can_focus()) return {Status::not_supported, u"標籤不能取得焦點"};
    return available(control.id);
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
    if (next == focus_) return {};
    if (!composing_.empty() && before_focus_) before_focus_();
    if (!popup_source_.empty() && next != popup_source_) close_popup();
    key_armed_.clear(); pointer_armed_.clear();
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
    if (composing_.empty()) {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        composing_ = focus_;
    }
    edits_.at(composing_).session.update_composition(std::move(preedit), caret);
    return {};
}
Result detail::UiState::composition_commit(std::u16string_view text) {
    auto target = composing_.empty() ? focus_ : composing_;
    composition_cancel();
    if (!edits_.contains(target)) return {Status::invalid_value, u"沒有接收組字的文字欄位"};
    auto committed = without_controls(text);
    Field* field = nullptr;
    if (auto result = writable(target, field); !result) return result;
    if (committed.empty()) return {};
    return replace_selection(target, *field, committed);
}
Result detail::UiState::composition_cancel() {
    if (composing_.empty()) return {};
    edits_.at(composing_).session.cancel_composition();
    composing_.clear();
    return {};
}
Result detail::UiState::focus(std::string_view id) {
    pointer_focus_ = false;
    refresh_tooltips();
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"Component no longer exists"};
    if (auto result = focusable(*control); !result) return result;
    return move_focus(control->id);
}
Result detail::UiState::key_down(KeyPress key) {
    if (key.key == Key::escape && !key.repeat) {
        for (auto& [id, tip] : tooltips_) {
            if (tip.visible) {
                tip.visible = false; tip.dismissed = true;
                if (invalidate_) invalidate_();
                return {};
            }
        }
    }
    if (modal_ && dialog_.configured && key.key == Key::escape) {
        if (key.repeat) return {};
        return settle_dialog(false);
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
    if (!control) return {};
    if (auto result = available(focus_); !result) return result;
    return component_type(control->kind).key_down(*this, key);
}
Result detail::UiState::text_key_down(KeyPress key) { return edit_key(key); }
Result detail::UiState::command_key_down(KeyPress key) {
    if (key.repeat) return {};
    if (key.key == Key::enter) return invoke(focus_);
    if (key.key == Key::space) key_armed_ = focus_;
    return {};
}
void detail::UiState::command_pointer_down() { pointer_armed_ = focus_; }

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
            auto content = clipboard_ ? clipboard_->read() : std::nullopt;
            return content ? paste(focus_, *content) : Result{Status::not_supported, u"剪貼簿沒有文字"};
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
    if (key != Key::space || key_armed_.empty() || key_armed_ != focus_) return {};
    auto target = std::exchange(key_armed_, {});
    return invoke(target);
}
Result detail::UiState::pointer_down(std::string_view id, std::optional<std::size_t> option) {
    if (!popup_source_.empty() && popup_source_ != id) close_popup();
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"Component no longer exists"};
    if (auto result = focusable(*control); !result) return result;
    pointer_focus_ = true;
    if (auto result = move_focus(control->id); !result) return result;
    refresh_tooltips();
    if (control->kind == Kind::dropdown) {
        pointer_armed_ = control->id; pointer_option_ = option; pointer_option_value_.reset();
        auto& f = dropdowns_.at(control->id);
        if (popup_source_ == id && option && *option < f.options.size()) pointer_option_value_ = f.options[*option].value;
    }
    else if (control->kind == Kind::list) {
        pointer_armed_ = control->id; pointer_option_ = option; pointer_option_value_.reset();
        const auto& items = lists_.at(control->id).items;
        if (option && *option < items.size()) pointer_option_value_ = items[*option].value;
    }
    else if (control->kind == Kind::radio_group || control->kind == Kind::slider || control->kind == Kind::tabs) { pointer_armed_ = control->id; pointer_option_ = option; }
    else component_type(control->kind).pointer_down(*this);
    return {};
}
Result detail::UiState::pointer_up(std::string_view id, std::optional<std::size_t> option) {
    auto armed = std::exchange(pointer_armed_, {});
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
void detail::UiState::cancel_input() { key_armed_.clear(); pointer_armed_.clear(); pointer_option_.reset(); pointer_option_value_.reset(); }
Result detail::UiState::set_error(std::string_view id, std::u16string message) {
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"Component no longer exists"};
    if (control->kind == Kind::label) return {Status::not_supported, u"標籤不能帶有錯誤"};
    if (message.empty()) { if (auto it = errors_.find(id); it != errors_.end()) errors_.erase(it); }
    else errors_.insert_or_assign(control->id, std::move(message));
    return {};
}
void detail::UiState::enable(std::string_view id, bool enabled) {
    auto* control = const_cast<Control*>(find(id));
    if (!control) return;
    control->enabled = enabled;
    if (!enabled && popup_source_ == id) close_popup();
    if (!enabled && focus_ == control->id) move_focus({});
    refresh_tooltips();
}
void detail::UiState::set_read_only(std::string_view id, bool read_only) {
    if (auto* control = const_cast<Control*>(find(id))) control->read_only = read_only;
}
void detail::UiState::check(std::string_view id, bool checked) {
    if (auto* control = const_cast<Control*>(find(id))) control->checked = checked;
}
void detail::UiState::replace_text(std::string_view id, std::u16string value) {
    auto* control = const_cast<Control*>(find(id));
    auto it = edits_.find(id);
    if (!control || it == edits_.end() || !valid_single_line(value, control->max_length)) throw std::invalid_argument("Invalid owner refresh");
    if (composing_ == control->id) composition_cancel();
    auto& field = it->second;
    field.session.cancel_composition();
    field.session.text = std::move(value);
    field.session.select(field.session.selection.anchor, field.session.selection.caret);
    field.past.clear(); field.future.clear(); field.offset = 0;
}
Ui::Ui(std::vector<Control> controls) : state_(std::make_unique<detail::UiState>(std::move(controls))) {}
Ui::~Ui() = default;
TextHandle Ui::add_text(Control control) { return state_->add_text(std::move(control)); }
ButtonHandle Ui::add_button(Control control) { return state_->add_button(std::move(control)); }
LabelHandle Ui::add_label(Control control) { return state_->add_label(std::move(control)); }
CheckboxHandle Ui::add_checkbox(Control control) { return state_->add_checkbox(std::move(control)); }
RadioGroupHandle Ui::add_radio_group(Control control, std::vector<RadioOption> options) { return state_->add_radio_group(std::move(control), std::move(options)); }
SliderHandle Ui::add_slider(Control control, SliderRange range) { return state_->add_slider(std::move(control), range); }
ProgressHandle Ui::add_progress(Control control) { return state_->add_progress(std::move(control)); }
TabsHandle Ui::add_tabs(Control control, std::vector<TabPage> pages) { return state_->add_tabs(std::move(control), std::move(pages)); }
DropdownHandle Ui::add_dropdown(Control control, std::vector<RadioOption> options) { return state_->add_dropdown(std::move(control), std::move(options)); }
ListHandle Ui::add_list(Control control, std::vector<RadioOption> items) { return state_->add_list(std::move(control), std::move(items)); }
TooltipHandle Ui::add_tooltip(ComponentHandle source, std::u16string text, unsigned delay_ms) {
    return state_->add_tooltip(std::move(source), std::move(text), delay_ms);
}
DialogHandle Ui::add_dialog(std::vector<ComponentHandle> members, ButtonHandle initial, ButtonHandle fallback, std::function<void(bool)> result) {
    return state_->add_dialog(std::move(members), initial, fallback, std::move(result));
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
detail::UiState& detail::access(Ui& ui) { return *ui.state_; }
const detail::UiState& detail::access(const Ui& ui) { return *ui.state_; }

}
