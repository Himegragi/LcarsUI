#include "edit_form.hpp"
#include <algorithm>
#include <string>

namespace edit_form {
using namespace lcars;
namespace {
const std::u16string initial_name = u"測試使用者";
const std::u16string short_intro = u"改名稱只改草稿；按「套用」且名稱有效，才更新上次套用結果並計數一次。資料只在記憶體。";
const std::u16string long_intro = short_intro +
    u"\n這是長說明，用來確認長文字換行後不會覆蓋相鄰欄位，並在小視窗中造成溢出，讓你檢查捲動、焦點揭露與指標命中。"
    u"\n名稱欄按 Enter 不會套用；中文組字確認也不會套用。Ctrl+Z、Ctrl+Y 與 Ctrl+Shift+Z 只作用於名稱欄本身的編輯歷史。"
    u"\n「名稱唯讀」仍可選取與複製；「資料區停用」時名稱與套用都不能操作，但測試入口與重設仍可使用。";

std::vector<Control> controls(const Ids& i) {
    // Declaration order is the Tab order: test entries first, then the form.
    return {
        {.id = i.title, .kind = Kind::label, .value = u"編輯表單"},
        {.id = i.intro, .kind = Kind::label},
        {.id = i.normal, .kind = Kind::button, .label = u"正常"},
        {.id = i.read_only, .kind = Kind::button, .label = u"名稱唯讀"},
        {.id = i.disabled, .kind = Kind::button, .label = u"資料區停用"},
        {.id = i.error, .kind = Kind::button, .label = u"名稱錯誤"},
        {.id = i.long_text, .kind = Kind::button, .label = u"長說明"},
        {.id = i.reset, .kind = Kind::button, .label = u"重設"},
        {.id = i.name_label, .kind = Kind::label, .value = u"顯示名稱", .target = i.name, .role = TextRole::name},
        {.id = i.name, .kind = Kind::text, .value = initial_name},
        {.id = i.name_help, .kind = Kind::label, .value = u"單行文字，可輸入繁體中文或英文。", .target = i.name, .role = TextRole::description},
        {.id = i.name_error, .kind = Kind::label, .target = i.name, .role = TextRole::error},
        {.id = i.apply, .kind = Kind::button, .label = u"套用"},
        {.id = i.state, .kind = Kind::label},
        {.id = i.draft, .kind = Kind::label},
        {.id = i.result, .kind = Kind::label},
        {.id = i.count, .kind = Kind::label},
    };
}
bool blank(std::u16string_view text) {
    return std::all_of(text.begin(), text.end(), [](char16_t c) { return c == u' ' || c == u'\t' || c == u'　'; });
}
}

Ids default_ids() {
    Ids i;
    i.title = "form.title"; i.intro = "form.intro";
    i.normal = "fixture.normal"; i.read_only = "fixture.read_only"; i.disabled = "fixture.disabled";
    i.error = "fixture.error"; i.long_text = "fixture.long"; i.reset = "form.reset";
    i.name_label = "form.name.label"; i.name = "form.name"; i.name_help = "form.name.help"; i.name_error = "form.name.error";
    i.apply = "form.apply"; i.state = "result.state"; i.draft = "result.draft"; i.result = "result.applied"; i.count = "result.count";
    return i;
}

Form::Form(Ids ids) : ui(controls(ids)), ids_(std::move(ids)), applied_(initial_name) {
    const auto& i = ids_;
    ui.bind_text(i.intro, [this] { return long_intro_ ? long_intro : short_intro; });
    ui.bind_text(i.state, [this] {
        std::u16string state = mode_ == Mode::read_only ? u"名稱唯讀" : mode_ == Mode::disabled ? u"資料區停用" : u"正常";
        if (ui.describe(ids_.name)->invalid) state += u"・名稱有錯誤";
        return u"狀態：" + state;
    });
    ui.bind_text(i.draft, [this] { return u"目前編輯值：" + ui.text(ids_.name); });
    ui.bind_text(i.result, [this] { return u"上次套用結果：" + applied_; });
    ui.bind_text(i.count, [this] { auto n = std::to_string(applies_); return u"套用次數：" + std::u16string(n.begin(), n.end()); });
    // Validation policy: on leaving the name, and while an error is shown so fixing clears it.
    ui.on_focus([this](const Id& from, const Id&) { if (from == ids_.name) validate(); });
    ui.on_change([this] { if (ui.describe(ids_.name)->invalid) validate(); });
    ui.bind(i.apply, [this] { return apply(); });
    ui.bind(i.normal, [this] { set_mode(Mode::normal); return Result{}; });
    ui.bind(i.read_only, [this] { set_mode(Mode::read_only); return Result{}; });
    ui.bind(i.disabled, [this] { set_mode(Mode::disabled); return Result{}; });
    ui.bind(i.long_text, [this] { long_intro_ = !long_intro_; return Result{}; });
    ui.bind(i.reset, [this] { return reset(); });
    ui.bind(i.error, [this] {
        // A named fixture: blank the name as an undoable edit, then show its error.
        set_mode(Mode::normal);
        ui.set_value(ids_.name, u"");
        validate();
        ui.focus(ids_.name);
        return Result{};
    });
}

bool Form::validate() {
    bool valid = !blank(ui.text(ids_.name));
    ui.set_error(ids_.name, valid ? u"" : u"請填寫顯示名稱");
    return valid;
}
Result Form::apply() {
    if (!validate()) {
        ui.focus(ids_.name);
        return {Status::invalid_value, u"請填寫顯示名稱"};
    }
    applied_ = ui.text(ids_.name);
    ++applies_;
    return {};
}
void Form::set_mode(Mode mode) {
    mode_ = mode;
    ui.set_read_only(ids_.name, mode == Mode::read_only);
    ui.enable(ids_.name, mode != Mode::disabled);
    ui.enable(ids_.apply, mode != Mode::disabled);
}
Result Form::reset() {
    set_mode(Mode::normal);
    long_intro_ = false;
    ui.replace_text(ids_.name, initial_name);
    ui.set_error(ids_.name, u"");
    applied_ = initial_name;
    applies_ = 0;
    scroll.offset = 0;
    ui.focus(ids_.reset);
    return {};
}

layout::Node Form::page() const {
    using namespace layout;
    const auto& i = ids_;
    return layout::scroll(column({
        control(i.title, 24),
        control(i.intro),
        grid({control(i.normal), control(i.read_only), control(i.disabled), control(i.error), control(i.long_text), control(i.reset)}, 120, 8),
        grid({column({control(i.name), row({control(i.apply), control(i.state)}, 16)}, 12),
              column({control(i.draft), control(i.result), control(i.count)}, 8)},
             360, 32),
    }, 16, 24));
}
}
