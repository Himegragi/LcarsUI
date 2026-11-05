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
        {.id = i.latest, .kind = Kind::button, .label = u"\u6539\u7528\u6700\u65b0\u503c", .enabled = false},
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
    i.apply = "form.apply"; i.state = "result.state"; i.draft = "result.draft"; i.result = "result.applied"; i.count = "result.count"; i.latest = "form.latest";
    return i;
}

Form::Form(Ids ids) : ids_(std::move(ids)), applied_(initial_name), ui({}) {
    auto declarations = controls(ids_);
    ui.add_label(std::move(declarations[0]));
    auto intro = ui.add_label(std::move(declarations[1]));
    auto normal = ui.add_button(std::move(declarations[2]));
    auto read_only = ui.add_button(std::move(declarations[3]));
    auto disabled = ui.add_button(std::move(declarations[4]));
    auto error = ui.add_button(std::move(declarations[5]));
    auto long_text = ui.add_button(std::move(declarations[6]));
    reset_ = ui.add_button(std::move(declarations[7]));
    ui.add_label(std::move(declarations[8]));
    name_ = ui.add_text(std::move(declarations[9]));
    ui.add_label(std::move(declarations[10]));
    ui.add_label(std::move(declarations[11]));
    apply_ = ui.add_button(std::move(declarations[12]));
    auto state = ui.add_label(std::move(declarations[13]));
    auto draft = ui.add_label(std::move(declarations[14]));
    auto result = ui.add_label(std::move(declarations[15]));
    auto count = ui.add_label(std::move(declarations[16]));
    latest_ = ui.add_button(std::move(declarations[17]));

    name_.connect_source({[this] { return applied_; }, [this](std::function<void()> changed) {
        source_changed_ = std::move(changed);
        return [this] { source_changed_ = {}; };
    }});
    intro.connect_source([this] { return long_intro_ ? long_intro : short_intro; });
    state.connect_source([this] {
        std::u16string state = mode_ == Mode::read_only ? u"\u540d\u7a31\u552f\u8b80" : mode_ == Mode::disabled ? u"\u8cc7\u6599\u5340\u505c\u7528" : u"\u6b63\u5e38";
        if (name_.describe()->invalid) state += u"\u30fb\u540d\u7a31\u6709\u932f\u8aa4";
        if (name_.source_conflict()) state += u"\u30fb\u4f86\u6e90\u5df2\u66f4\u65b0";
        return u"\u72c0\u614b\uff1a" + state;
    });
    draft.connect_source([this] { return u"\u76ee\u524d\u7de8\u8f2f\u503c\uff1a" + name_.describe()->value; });
    result.connect_source([this] { return u"\u4e0a\u6b21\u5957\u7528\u7d50\u679c\uff1a" + applied_; });
    count.connect_source([this] { auto n = std::to_string(applies_); return u"\u5957\u7528\u6b21\u6578\uff1a" + std::u16string(n.begin(), n.end()); });
    ui.on_focus([this](const Id& from, const Id&) { if (from == ids_.name) validate(); });
    ui.on_change([this] { if (name_.describe()->invalid) validate(); refresh_conflict(); });
    ui.connect_confirmation(apply_, {name_}, [this](const auto& changes) { return apply(changes); });
    normal.bind([this] { set_mode(Mode::normal); return Result{}; });
    read_only.bind([this] { set_mode(Mode::read_only); return Result{}; });
    disabled.bind([this] { set_mode(Mode::disabled); return Result{}; });
    long_text.bind([this] { long_intro_ = !long_intro_; return Result{}; });
    reset_.bind([this] { return reset(); });
    latest_.bind([this] { auto result = name_.use_latest(); refresh_conflict(); return result; });
    error.bind([this] {
        set_mode(Mode::normal);
        name_.set_value(u"");
        validate();
        name_.focus();
        return Result{};
    });
}

bool Form::validate() {
    bool valid = !blank(name_.describe()->value);
    name_.set_error(valid ? u"" : u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31");
    return valid;
}
ConfirmationResult Form::apply(const std::vector<TextChange>& changes) {
    if (blank(changes[0].value)) {
        auto message = std::u16string(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31");
        return {{Status::invalid_value, message}, {{0, message}}};
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
    latest_.enable(mode_ != Mode::disabled && mode_ != Mode::read_only && name_.source_conflict());
}
void Form::set_mode(Mode mode) {
    mode_ = mode;
    name_.read_only(mode == Mode::read_only);
    name_.enable(mode != Mode::disabled);
    apply_.enable(mode != Mode::disabled);
    refresh_conflict();
}
Result Form::reset() {
    set_mode(Mode::normal);
    long_intro_ = false;
    update_source(initial_name);
    name_.reset_to_source();
    refresh_conflict();
    applies_ = 0;
    scroll.offset = 0;
    reset_.focus();
    return {};
}

layout::Node Form::page() const {
    using namespace layout;
    const auto& i = ids_;
    return layout::scroll(column({
        control(i.title, 24),
        control(i.intro),
        grid({control(i.normal), control(i.read_only), control(i.disabled), control(i.error), control(i.long_text), control(i.reset)}, 120, 8),
        grid({column({control(i.name), row({control(i.apply), control(i.latest), control(i.state)}, 16)}, 12),
              column({control(i.draft), control(i.result), control(i.count)}, 8)},
             360, 32),
    }, 16, 24));
}
}
