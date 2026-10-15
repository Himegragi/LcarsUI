#include "lcars/core.hpp"
#include <stdexcept>
namespace lcars {
bool valid_single_line(std::u16string_view text) {
    if (text.size() > 4096) return false;
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
            if (!valid_single_line(control.value)) throw std::invalid_argument("Invalid initial text");
            EditSession session;
            session.text = control.value;
            edits_.emplace(control.id, std::move(session));
        }
    }
}
const Control* Ui::find(std::string_view id) const {
    auto it = std::find_if(controls_.begin(), controls_.end(), [&](const auto& control) { return control.id == id; });
    return it == controls_.end() ? nullptr : &*it;
}
EditSession* Ui::edit(std::string_view id) {
    auto it = edits_.find(id); return it == edits_.end() ? nullptr : &it->second;
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
    if (auto result = available(id); !result) return result;
    const auto* control = find(id);
    if (control->kind != Kind::text) return {Status::invalid_value, u"元件不是文字欄位"};
    if (control->read_only) return {Status::read_only, u"欄位為唯讀"};
    auto& session = edits_.at(control->id);
    if (session.composing || dispatching_) return {Status::busy, u"操作尚未結束"};
    if (!valid_single_line(value)) return {Status::invalid_value, u"請輸入有效單行文字（最多 4096 UTF-16 單位）"};
    if (session.text == value) return {};
    session.text = std::move(value);
    session.select(session.selection.anchor, session.selection.caret);
    const_cast<Control*>(control)->value = session.text;
    dispatching_ = true;
    try { if (changed_) changed_(); }
    catch (...) { stopped_ = true; dispatching_ = false; return {Status::failed, u"資料更新失敗，介面已停止"}; }
    dispatching_ = false;
    return {};
}
Result Ui::invoke(std::string_view id) {
    if (auto result = available(id); !result) return result;
    if (dispatching_ || invoking_) return {Status::busy, u"操作尚未結束"};
    for (const auto& [key, session] : edits_)
        if (session.composing) return {Status::busy, u"請先完成文字組字"};
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
void Ui::enable(std::string_view id, bool enabled) {
    if (auto* control = const_cast<Control*>(find(id))) control->enabled = enabled;
}
void Ui::check(std::string_view id, bool checked) {
    if (auto* control = const_cast<Control*>(find(id))) control->checked = checked;
}
void Ui::replace_text(std::string_view id, std::u16string value) {
    auto* control = const_cast<Control*>(find(id)); auto* session = edit(id);
    if (!control || !session || !valid_single_line(value)) throw std::invalid_argument("Invalid owner refresh");
    session->cancel_composition(); session->text = std::move(value);
    session->select(session->selection.anchor, session->selection.caret);
    control->value = session->text;
}
}
