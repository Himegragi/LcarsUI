#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lcars {
using Id = std::string;
enum class Kind { text, toggle, button, navigation };
enum class Status { ok, not_found, disabled, read_only, blocked_by_modal, busy, invalid_value, wrong_thread, failed };
struct Result {
    Status status = Status::ok;
    std::u16string message;
    explicit operator bool() const { return status == Status::ok; }
};
struct Control {
    Id id;
    Kind kind;
    std::u16string label;
    std::string page;
    std::u16string value;
    bool enabled = true;
    bool read_only = false;
    bool checked = false;
};
struct Selection { unsigned anchor = 0, caret = 0; };
// One session owns text and transient composition. Offsets are UTF-16.
// The text backend supplies navigation stops; this is not a Unicode segmenter.
class EditSession {
public:
    std::u16string text;
    Selection selection;
    bool composing = false;
    std::u16string preedit;
    unsigned preedit_caret = 0;
    unsigned begin() const { return std::min(selection.anchor, selection.caret); }
    unsigned end() const { return std::max(selection.anchor, selection.caret); }
    void select(unsigned anchor, unsigned caret);
    void start_composition();
    void update_composition(std::u16string value, unsigned caret);
    void cancel_composition();
    std::u16string visible() const;
    unsigned visible_caret() const { return composing ? begin() + preedit_caret : selection.caret; }
};
bool valid_single_line(std::u16string_view text);
// UI-thread-owned controls addressed by logical IDs, never visual part paths.
class Ui {
public:
    using Action = std::function<Result()>;
    explicit Ui(std::vector<Control> controls);
    Ui(const Ui&)=delete;
    Ui& operator=(const Ui&)=delete;
    const std::vector<Control>& controls() const { return controls_; }
    const Control* find(std::string_view id) const;
    EditSession* edit(std::string_view id);
    Result set_value(std::string_view id, std::u16string value);
    Result invoke(std::string_view id);
    Result available(std::string_view id) const;
    void bind(Id id, Action action);
    void on_change(std::function<void()> callback) { changed_ = std::move(callback); }
    void enable(std::string_view id, bool enabled);
    void check(std::string_view id, bool checked);
    void replace_text(std::string_view id, std::u16string value);
    void modal(bool active) { modal_ = active; }
    bool modal() const { return modal_; }
    bool stopped() const { return stopped_; }
private:
    std::thread::id owner_ = std::this_thread::get_id();
    std::vector<Control> controls_;
    std::map<Id, EditSession, std::less<>> edits_;
    std::map<Id, Action, std::less<>> actions_;
    std::function<void()> changed_;
    bool modal_ = false, stopped_ = false, dispatching_ = false, invoking_ = false;
};
}
