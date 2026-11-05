#include "core_internal.hpp"
#include <stdexcept>
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
        if (!control.target.empty() && (control.kind != Kind::label || !ids.contains(control.target)))
            throw std::invalid_argument("Only labels may target, and only known controls");
}
detail::UiState::~UiState() {
    lifetime_.reset();
    for (auto& [id, field] : edits_) if (field.disconnect) field.disconnect();
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
    if (auto result = ui_->configuration_access(id_); !result) return result;
    auto it = ui_->edits_.find(id_);
    if (it == ui_->edits_.end() || !it->second.has_source_value)
        return {Status::invalid_value, u"Text source is not connected"};
    ui_->replace_text(id_, it->second.source_value);
    it->second.origin = it->second.source_value;
    return ui_->set_error(id_, u"");
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
    if (!find(id)) return {Status::not_found, u"Component no longer exists"};
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
                if (error.index >= fields.size()) continue;
                set_error(fields[error.index].id_, error.message);
                if (!error.message.empty() && (!first || error.index < *first)) first = error.index;
            }
            if (first) focus(fields[*first].id_);
        }
        auto notification = notify();
        return notification ? outcome.result : notification;
    });
    return {};
}
Result TextHandle::connect_immediate(std::function<Result(const TextChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (std::this_thread::get_id() != ui_->owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    auto it = ui_->edits_.find(id_);
    if (it == ui_->edits_.end() || !it->second.has_source_value || it->second.confirmation_member || it->second.confirmed || !write)
        return {Status::invalid_value, u"Immediate write needs an ungrouped connected source"};
    it->second.immediate = std::move(write);
    return {};
}
Result TextHandle::connect_confirmed(std::function<Result(const TextChange&)> write) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (std::this_thread::get_id() != ui_->owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    auto it = ui_->edits_.find(id_);
    if (it == ui_->edits_.end() || !it->second.has_source_value || it->second.confirmation_member || it->second.immediate || !write)
        return {Status::invalid_value, u"Confirmed write needs an ungrouped connected source"};
    it->second.confirmed = std::move(write);
    return {};
}
Result TextHandle::confirm() const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    if (auto result = ui_->available(id_); !result) return result;
    auto it = ui_->edits_.find(id_);
    if (it == ui_->edits_.end() || !it->second.confirmed)
        return {Status::invalid_value, u"No confirmed write is connected"};
    auto& field = it->second;
    if (field.submitting || field.session.composing || ui_->dispatching_ || ui_->invoking_)
        return {Status::busy, u"Operation is already executing"};
    TextChange change{*this, field.session.text, field.origin};
    field.submitting = true;
    Result result;
    try { result = field.confirmed(change); }
    catch (...) { field.submitting = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
    field.submitting = false;
    if (result) {
        field.origin = change.value;
        ui_->set_error(id_, u"");
    } else {
        ui_->set_error(id_, result.message);
        ui_->focus(id_);
    }
    auto notification = ui_->notify();
    return notification ? result : notification;
}
Result TextHandle::connect_source(TextSource source) const {
    if (lifetime_.expired()) return {Status::not_found, u"Component no longer exists"};
    return ui_->connect_source(id_, std::move(source));
}
Result detail::UiState::connect_source(const Id& id, TextSource source) {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
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
    auto it = ui_->edits_.find(id_);
    if (it == ui_->edits_.end() || !it->second.has_source_value) return {Status::invalid_value, u"Text source is not connected"};
    auto latest = it->second.source_value;
    const bool was_submitting = it->second.submitting;
    it->second.submitting = true;
    auto result = ui_->set_value(id_, latest);
    it->second.submitting = was_submitting;
    if (result) {
        it->second.origin = std::move(latest);
        ui_->set_error(id_, u"");
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
    if (auto it = errors_.find(id); it != errors_.end()) s.error = it->second;
    s.invalid = !s.error.empty();
    for (const auto& other : controls_) {
        if (other.target != id) continue;
        s.labels.push_back(other.id);
        if (other.role == TextRole::name) s.name = text(other.id);
        else if (other.role == TextRole::description) s.description = text(other.id);
    }
    if (auto it = edits_.find(id); it != edits_.end()) {
        const auto& field = it->second;
        s.source_conflict = field.has_source_value && field.origin != field.source_value && field.session.text != field.source_value;
    }
    if (!s.focusable) return s;
    s.operations.push_back(Operation::focus);
    if (control->kind == Kind::button && !s.executing) s.operations.push_back(Operation::activate);
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
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"操作必須在 UI 執行緒"};
    if (stopped_) return {Status::failed, u"介面已停止，請重新啟動"};
    auto control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
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
    const_cast<Control*>(find(id))->value = session.text;
    return finish_edit(id, field);
}
Result detail::UiState::travel(std::string_view id, bool forward) {
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    auto& from = forward ? field->future : field->past;
    auto& to = forward ? field->past : field->future;
    if (from.empty()) return {Status::not_supported, forward ? u"沒有可重做的編輯" : u"沒有可復原的編輯"};
    auto& session = field->session;
    if (field->has_source_value && !field->submitting && session.text == field->source_value)
        field->origin = field->source_value;
    to.push_back({session.text, session.selection});
    auto snapshot = std::move(from.back());
    from.pop_back();
    session.text = std::move(snapshot.text);
    session.select(snapshot.selection.anchor, snapshot.selection.caret);
    const_cast<Control*>(find(id))->value = session.text;
    return finish_edit(id, *field);
}
Result detail::UiState::finish_edit(std::string_view id, Field& field) {
    Result result;
    if (field.immediate && !field.submitting) {
        auto change = TextChange{TextHandle(this, lifetime_, Id(id)), field.session.text, field.origin};
        field.submitting = true;
        try { result = field.immediate(change); }
        catch (...) { field.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
        field.submitting = false;
        if (result) {
            field.origin = change.value;
            set_error(id, u"");
        } else set_error(id, result.message);
    }
    auto notification = notify();
    return notification ? result : notification;
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
    if (control.kind == Kind::label) return {Status::not_supported, u"標籤不能取得焦點"};
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
    key_armed_.clear(); pointer_armed_.clear();
    if (!composing_.empty()) {
        // E01 default, matching the native IME on blur: leftover preedit is committed once.
        auto preedit = edits_.at(composing_).session.preedit;
        composition_commit(preedit);
    }
    auto previous = std::exchange(focus_, std::move(next));
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
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (auto result = focusable(*control); !result) return result;
    return move_focus(control->id);
}
Result detail::UiState::key_down(KeyPress key) {
    if (key.key == Key::tab) {
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
    if (control->kind == Kind::text) return edit_key(key);
    if (control->kind != Kind::button || key.repeat) return {};
    if (key.key == Key::enter) return invoke(focus_);
    if (key.key == Key::space) key_armed_ = focus_;
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
Result detail::UiState::pointer_down(std::string_view id) {
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (auto result = focusable(*control); !result) return result;
    if (auto result = move_focus(control->id); !result) return result;
    if (control->kind == Kind::button) pointer_armed_ = control->id;
    return {};
}
Result detail::UiState::pointer_up(std::string_view id) {
    auto armed = std::exchange(pointer_armed_, {});
    if (armed.empty() || armed != id) return {};
    return invoke(armed);
}
void detail::UiState::cancel_input() { key_armed_.clear(); pointer_armed_.clear(); }
Result detail::UiState::set_error(std::string_view id, std::u16string message) {
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (control->kind == Kind::label) return {Status::not_supported, u"標籤不能帶有錯誤"};
    if (message.empty()) { if (auto it = errors_.find(id); it != errors_.end()) errors_.erase(it); }
    else errors_.insert_or_assign(control->id, std::move(message));
    return {};
}
void detail::UiState::enable(std::string_view id, bool enabled) {
    auto* control = const_cast<Control*>(find(id));
    if (!control) return;
    control->enabled = enabled;
    if (!enabled && focus_ == control->id) move_focus({});
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
    control->value = field.session.text;
}
Ui::Ui(std::vector<Control> controls) : state_(std::make_unique<detail::UiState>(std::move(controls))) {}
Ui::~Ui() = default;
TextHandle Ui::add_text(Control control) { return state_->add_text(std::move(control)); }
ButtonHandle Ui::add_button(Control control) { return state_->add_button(std::move(control)); }
LabelHandle Ui::add_label(Control control) { return state_->add_label(std::move(control)); }
Result Ui::connect_confirmation(ButtonHandle button, std::vector<TextHandle> fields,
        std::function<ConfirmationResult(const std::vector<TextChange>&)> submit) {
    return state_->connect_confirmation(std::move(button), std::move(fields), std::move(submit));
}
void Ui::on_change(std::function<void()> callback) { state_->on_change(std::move(callback)); }
detail::UiState& detail::access(Ui& ui) { return *ui.state_; }
const detail::UiState& detail::access(const Ui& ui) { return *ui.state_; }

}
