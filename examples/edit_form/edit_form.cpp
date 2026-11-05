#include "edit_form.hpp"
#include <algorithm>
#include <string>

namespace edit_form {
using namespace lcars;
namespace {
const std::u16string required_name = u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31";
const std::u16string initial_name = u"測試使用者";
const std::u16string short_intro = u"改名稱只改草稿；按「套用」且名稱有效，才更新上次套用結果並計數一次。資料只在記憶體。";
const std::u16string long_intro = short_intro +
    u"\n這是長說明，用來確認長文字換行後不會覆蓋相鄰欄位，並在小視窗中造成溢出，讓你檢查捲動、焦點揭露與指標命中。"
    u"\n名稱欄按 Enter 不會套用；中文組字確認也不會套用。Ctrl+Z、Ctrl+Y 與 Ctrl+Shift+Z 只作用於名稱欄本身的編輯歷史。"
    u"\n「名稱唯讀」仍可選取與複製；「資料區停用」時名稱與套用都不能操作，但測試入口與重設仍可使用。";

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
    i.apply = "form.apply"; i.state = "result.state"; i.draft = "result.draft"; i.result = "result.applied"; i.count = "result.count"; i.latest = "form.latest";
    return i;
}

Form::Form(Ids ids) : ids_(std::move(ids)), applied_(initial_name), ui({}) {
    const auto& i = ids_;
    handles.title = ui.add_label({.id = i.title, .kind = Kind::label, .value = u"編輯表單"});
    handles.intro = ui.add_label({.id = i.intro, .kind = Kind::label});
    handles.normal = ui.add_button({.id = i.normal, .kind = Kind::button, .label = u"正常"});
    handles.read_only = ui.add_button({.id = i.read_only, .kind = Kind::button, .label = u"名稱唯讀"});
    handles.disabled = ui.add_button({.id = i.disabled, .kind = Kind::button, .label = u"資料區停用"});
    handles.error = ui.add_button({.id = i.error, .kind = Kind::button, .label = u"名稱錯誤"});
    handles.long_text = ui.add_button({.id = i.long_text, .kind = Kind::button, .label = u"長說明"});
    handles.reset = ui.add_button({.id = i.reset, .kind = Kind::button, .label = u"重設"});
    handles.name_label = ui.add_label({.id = i.name_label, .kind = Kind::label, .value = u"顯示名稱", .target = i.name, .role = TextRole::name});
    handles.name = ui.add_text({.id = i.name, .kind = Kind::text, .value = initial_name});
    handles.name_help = ui.add_label({.id = i.name_help, .kind = Kind::label, .value = u"單行文字，可輸入繁體中文或英文。", .target = i.name, .role = TextRole::description});
    handles.name_error = ui.add_label({.id = i.name_error, .kind = Kind::label, .target = i.name, .role = TextRole::error});
    handles.apply = ui.add_button({.id = i.apply, .kind = Kind::button, .label = u"套用"});
    handles.state = ui.add_label({.id = i.state, .kind = Kind::label});
    handles.draft = ui.add_label({.id = i.draft, .kind = Kind::label});
    handles.result = ui.add_label({.id = i.result, .kind = Kind::label});
    handles.count = ui.add_label({.id = i.count, .kind = Kind::label});
    handles.latest = ui.add_button({.id = i.latest, .kind = Kind::button, .label = u"\u6539\u7528\u6700\u65b0\u503c", .enabled = false});

    handles.name.connect_source({[this] { return applied_; }, [this](std::function<void()> changed) {
        source_changed_ = std::move(changed);
        return [this] { source_changed_ = {}; };
    }});
    handles.intro.connect_source([this] { return long_intro_ ? long_intro : short_intro; });
    handles.state.connect_source([this] {
        std::u16string state = mode_ == Mode::read_only ? u"\u540d\u7a31\u552f\u8b80" : mode_ == Mode::disabled ? u"\u8cc7\u6599\u5340\u505c\u7528" : u"\u6b63\u5e38";
        if (handles.name.describe()->invalid) state += u"\u30fb\u540d\u7a31\u6709\u932f\u8aa4";
        if (handles.name.source_conflict().value.value_or(false)) state += u"\u30fb\u4f86\u6e90\u5df2\u66f4\u65b0";
        return u"\u72c0\u614b\uff1a" + state;
    });
    handles.draft.connect_source([this] { return u"\u76ee\u524d\u7de8\u8f2f\u503c\uff1a" + *handles.name.draft(); });
    handles.result.connect_source([this] { return u"\u4e0a\u6b21\u5957\u7528\u7d50\u679c\uff1a" + applied_; });
    handles.count.connect_source([this] { auto n = std::to_string(applies_); return u"\u5957\u7528\u6b21\u6578\uff1a" + std::u16string(n.begin(), n.end()); });
    handles.name.on_blur([this] { validate(); });
    ui.on_change([this] { if (handles.name.describe()->invalid) validate(); refresh_conflict(); });
    ui.connect_confirmation(handles.apply, {handles.name}, [this](const auto& changes) { return apply(changes); });
    handles.normal.bind([this] { set_mode(Mode::normal); return Result{}; });
    handles.read_only.bind([this] { set_mode(Mode::read_only); return Result{}; });
    handles.disabled.bind([this] { set_mode(Mode::disabled); return Result{}; });
    handles.long_text.bind([this] { long_intro_ = !long_intro_; return Result{}; });
    handles.reset.bind([this] { return reset(); });
    handles.latest.bind([this] { auto result = handles.name.use_latest(); refresh_conflict(); return result; });
    handles.error.bind([this] {
        set_mode(Mode::normal);
        handles.name.set_value(u"");
        validate();
        handles.name.focus();
        return Result{};
    });
}

bool Form::validate() {
    bool valid = !blank(*handles.name.draft());
    handles.name.set_error(valid ? u"" : required_name);
    return valid;
}
ConfirmationResult Form::apply(const std::vector<TextChange>& changes) {
    if (blank(changes[0].value)) {
        auto message = required_name;
        return {{Status::invalid_value, message}, {{changes[0].field, message}}};
    }
    applied_ = changes[0].value;
    ++applies_;
    if (source_changed_) source_changed_();
    return {};
}
void Form::update_source(std::u16string value) {
    applied_ = std::move(value);
    if (source_changed_) source_changed_();
    refresh_conflict();
}
void Form::refresh_conflict() {
    handles.latest.enable(mode_ != Mode::disabled && mode_ != Mode::read_only && handles.name.source_conflict().value.value_or(false));
}
void Form::set_mode(Mode mode) {
    mode_ = mode;
    handles.name.read_only(mode == Mode::read_only);
    handles.name.enable(mode != Mode::disabled);
    handles.apply.enable(mode != Mode::disabled);
    refresh_conflict();
}
Result Form::reset() {
    set_mode(Mode::normal);
    long_intro_ = false;
    update_source(initial_name);
    handles.name.reset_to_source();
    refresh_conflict();
    applies_ = 0;
    scroll.offset = 0;
    handles.reset.focus();
    return {};
}

layout::Node Form::page() const {
    using namespace layout;
    return layout::scroll(column({
        control(handles.title, 24),
        control(handles.intro),
        grid({control(handles.normal), control(handles.read_only), control(handles.disabled), control(handles.error), control(handles.long_text), control(handles.reset)}, 120, 8),
        grid({column({control(handles.name), row({control(handles.apply), control(handles.latest), control(handles.state)}, 16)}, 12),
              column({control(handles.draft), control(handles.result), control(handles.count)}, 8)},
             360, 32),
    }, 16, 24));
}
}
