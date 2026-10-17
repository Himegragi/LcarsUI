#include "lcars/core.hpp"
#include <stdexcept>
namespace lcars {
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
Ui::Ui(std::vector<Control> controls) : controls_(std::move(controls)) {
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
std::optional<Semantics> Ui::describe(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return std::nullopt;
    Semantics s;
    s.id = control->id; s.kind = control->kind; s.target = control->target; s.role = control->role;
    s.enabled = control->enabled; s.read_only = control->read_only;
    s.focusable = bool(focusable(*control));
    s.focused = focus_ == control->id;
    s.name = control->label; s.value = text(id);
    if (auto it = errors_.find(id); it != errors_.end()) s.error = it->second;
    s.invalid = !s.error.empty();
    for (const auto& other : controls_) {
        if (other.target != id) continue;
        s.labels.push_back(other.id);
        if (other.role == TextRole::name) s.name = text(other.id);
        else if (other.role == TextRole::description) s.description = text(other.id);
    }
    if (!s.focusable) return s;
    s.operations.push_back(Operation::focus);
    if (control->kind == Kind::button) s.operations.push_back(Operation::activate);
    if (auto it = edits_.find(id); it != edits_.end()) {
        s.operations.insert(s.operations.end(), {Operation::select, Operation::copy});
        const auto& field = it->second;
        if (!control->read_only && !field.session.composing) {
            s.operations.insert(s.operations.end(), {Operation::edit, Operation::cut, Operation::paste});
            if (!field.past.empty()) s.operations.push_back(Operation::undo);
            if (!field.future.empty()) s.operations.push_back(Operation::redo);
        }
    }
    return s;
}
std::u16string Ui::text(std::string_view id) const {
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
void Ui::bind_text(std::string_view id, std::function<std::u16string()> provider) {
    const auto* control = find(id);
    if (!control || control->kind != Kind::label || !provider) throw std::invalid_argument("Text binding requires a label and a provider");
    providers_.insert_or_assign(control->id, std::move(provider));
}
const Control* Ui::find(std::string_view id) const {
    auto it = std::find_if(controls_.begin(), controls_.end(), [&](const auto& control) { return control.id == id; });
    return it == controls_.end() ? nullptr : &*it;
}
EditSession* Ui::edit(std::string_view id) {
    auto it = edits_.find(id); return it == edits_.end() ? nullptr : &it->second.session;
}
Result Ui::available(std::string_view id) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"操作必須在 UI 執行緒"};
    if (stopped_) return {Status::failed, u"介面已停止，請重新啟動"};
    auto control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (!control->enabled) return {Status::disabled, u"目前無法執行"};
    if (control->page == "dialog" && !modal_) return {Status::disabled, u"對話框尚未開啟"};
    if (modal_ && control->page != "dialog") return {Status::blocked_by_modal, u"請先關閉對話框"};
    return {};
}
Result Ui::set_value(std::string_view id, std::u16string value) {
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.text == value) return {};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    return replace_selection(id, *field, value);
}
Result Ui::writable(std::string_view id, Field*& field) {
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (find(id)->read_only) return {Status::read_only, u"欄位為唯讀"};
    if (it->second.session.composing || dispatching_) return {Status::busy, u"操作尚未結束"};
    field = &it->second;
    return {};
}
// Every content change funnels through here so history, value mirror and notification agree.
Result Ui::replace_selection(std::string_view id, Field& field, std::u16string_view insertion) {
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
    field.past.push_back({session.text, session.selection});
    field.future.clear();
    session.text = std::move(next);
    session.select(caret, caret);
    const_cast<Control*>(find(id))->value = session.text;
    return notify();
}
Result Ui::travel(std::string_view id, bool forward) {
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    auto& from = forward ? field->future : field->past;
    auto& to = forward ? field->past : field->future;
    if (from.empty()) return {Status::not_supported, forward ? u"沒有可重做的編輯" : u"沒有可復原的編輯"};
    auto& session = field->session;
    to.push_back({session.text, session.selection});
    auto snapshot = std::move(from.back());
    from.pop_back();
    session.text = std::move(snapshot.text);
    session.select(snapshot.selection.anchor, snapshot.selection.caret);
    const_cast<Control*>(find(id))->value = session.text;
    return notify();
}
Result Ui::undo(std::string_view id) { return travel(id, false); }
Result Ui::redo(std::string_view id) { return travel(id, true); }
Result Ui::notify() {
    dispatching_ = true;
    try { if (changed_) changed_(); }
    catch (...) { stopped_ = true; dispatching_ = false; return {Status::failed, u"資料更新失敗，介面已停止"}; }
    dispatching_ = false;
    return {};
}
Result Ui::text_input(std::u16string_view text) {
    std::u16string committed;
    for (auto ch : text) if (ch >= 0x20 && ch != 0x7f) committed += ch;
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
Result Ui::paste(std::string_view id, std::u16string_view text) {
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    return replace_selection(id, *field, paste_as_single_line(text));
}
Result Ui::copy(std::string_view id) {
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    const auto& session = it->second.session;
    if (session.begin() == session.end()) return {};
    if (!clipboard_ || !clipboard_->write(std::u16string_view(session.text).substr(session.begin(), session.end() - session.begin())))
        return {Status::failed, u"無法寫入剪貼簿"};
    return {};
}
Result Ui::cut(std::string_view id) {
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.begin() == field->session.end()) return {};
    if (auto result = copy(id); !result) return result;
    return replace_selection(id, *field, u"");
}
float Ui::text_offset(std::string_view id) const {
    auto it = edits_.find(id);
    return it == edits_.end() ? 0.f : it->second.offset;
}
void Ui::set_text_offset(std::string_view id, float offset) {
    if (auto it = edits_.find(id); it != edits_.end()) it->second.offset = std::max(0.f, offset);
}
Result Ui::select(std::string_view id, unsigned anchor, unsigned caret) {
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalid_value, u"元件不是文字欄位"};
    if (it->second.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    it->second.session.select(anchor, caret);
    return {};
}
Result Ui::invoke(std::string_view id) {
    if (auto result = available(id); !result) return result;
    if (dispatching_ || invoking_) return {Status::busy, u"操作尚未結束"};
    for (const auto& [key, field] : edits_)
        if (field.session.composing) return {Status::busy, u"請先完成文字組字"};
    auto it = actions_.find(id);
    if (it == actions_.end()) return {Status::not_found, u"元件沒有此動作"};
    invoking_ = true;
    try { auto result = it->second(); invoking_ = false; return result; }
    catch (...) { stopped_ = true; invoking_ = false; return {Status::failed, u"操作失敗，介面已停止"}; }
}
void Ui::bind(Id id, Action action) {
    if (!find(id) || !action) throw std::invalid_argument("Action requires a known ID and a callback");
    actions_.insert_or_assign(std::move(id), std::move(action));
}
Result Ui::focusable(const Control& control) const {
    if (control.kind == Kind::label) return {Status::not_supported, u"標籤不能取得焦點"};
    return available(control.id);
}
std::vector<Id> Ui::focus_order() const {
    std::vector<Id> order;
    for (const auto& control : controls_)
        if (focusable(control)) order.push_back(control.id);
    return order;
}
void Ui::move_focus(Id next) {
    if (next == focus_) return;
    key_armed_.clear(); pointer_armed_.clear();
    composition_cancel();
    auto previous = std::exchange(focus_, std::move(next));
    if (!focus_moved_) return;
    try { focus_moved_(previous, focus_); }
    catch (...) { stopped_ = true; }
}
Result Ui::composition_update(std::u16string preedit, unsigned caret) {
    if (composing_.empty()) {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        composing_ = focus_;
    }
    edits_.at(composing_).session.update_composition(std::move(preedit), caret);
    return {};
}
Result Ui::composition_commit(std::u16string_view text) {
    auto target = composing_.empty() ? focus_ : composing_;
    composition_cancel();
    if (!edits_.contains(target)) return {Status::invalid_value, u"沒有接收組字的文字欄位"};
    std::u16string committed;
    for (auto ch : text) if (ch >= 0x20 && ch != 0x7f) committed += ch;
    Field* field = nullptr;
    if (auto result = writable(target, field); !result) return result;
    if (committed.empty()) return {};
    return replace_selection(target, *field, committed);
}
Result Ui::composition_cancel() {
    if (composing_.empty()) return {};
    edits_.at(composing_).session.cancel_composition();
    composing_.clear();
    return {};
}
Result Ui::focus(std::string_view id) {
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (auto result = focusable(*control); !result) return result;
    move_focus(control->id);
    return {};
}
Result Ui::key_down(KeyPress key) {
    if (key.key == Key::tab) {
        auto order = focus_order();
        if (order.empty()) return {Status::not_found, u"沒有可聚焦的元件"};
        auto it = std::find(order.begin(), order.end(), focus_);
        auto n = static_cast<long>(order.size());
        long index = it == order.end() ? (key.shift ? 0 : -1) : static_cast<long>(it - order.begin());
        index = (index + (key.shift ? -1 : 1) + n) % n;
        move_focus(order[static_cast<std::size_t>(index)]);
        return {};
    }
    const auto* control = find(focus_);
    if (!control) return {};
    if (control->kind == Kind::text) return edit_key(key);
    if (control->kind != Kind::button || key.repeat) return {};
    if (key.key == Key::enter) return invoke(focus_);
    if (key.key == Key::space) {
        if (auto result = available(focus_); !result) return result;
        key_armed_ = focus_;
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
Result Ui::edit_key(KeyPress key) {
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
Result Ui::key_up(Key key) {
    if (key != Key::space || key_armed_.empty() || key_armed_ != focus_) return {};
    auto target = std::exchange(key_armed_, {});
    return invoke(target);
}
Result Ui::pointer_down(std::string_view id) {
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (auto result = focusable(*control); !result) return result;
    move_focus(control->id);
    if (control->kind == Kind::button) pointer_armed_ = control->id;
    return {};
}
Result Ui::pointer_up(std::string_view id) {
    auto armed = std::exchange(pointer_armed_, {});
    if (armed.empty() || armed != id) return {};
    return invoke(armed);
}
void Ui::cancel_input() { key_armed_.clear(); pointer_armed_.clear(); }
Result Ui::set_error(std::string_view id, std::u16string message) {
    const auto* control = find(id);
    if (!control) return {Status::not_found, u"找不到元件"};
    if (control->kind == Kind::label) return {Status::not_supported, u"標籤不能帶有錯誤"};
    if (message.empty()) { if (auto it = errors_.find(id); it != errors_.end()) errors_.erase(it); }
    else errors_.insert_or_assign(control->id, std::move(message));
    return {};
}
void Ui::enable(std::string_view id, bool enabled) {
    auto* control = const_cast<Control*>(find(id));
    if (!control) return;
    control->enabled = enabled;
    if (!enabled && focus_ == control->id) move_focus({});
}
void Ui::set_read_only(std::string_view id, bool read_only) {
    if (auto* control = const_cast<Control*>(find(id))) control->read_only = read_only;
}
void Ui::check(std::string_view id, bool checked) {
    if (auto* control = const_cast<Control*>(find(id))) control->checked = checked;
}
void Ui::replace_text(std::string_view id, std::u16string value) {
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
}
