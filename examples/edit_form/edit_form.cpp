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
std::vector<RadioOption> regions() {
    return {{{"north"}, u"\u5317\u5340"}, {{"central"}, u"\u4e2d\u5340"}, {{"south"}, u"\u5357\u5340"}};
}
constexpr auto required_region = u"\u8acb\u9078\u64c7\u5730\u5340";
}

Ids default_ids() {
    Ids i;
    i.title = "form.title"; i.intro = "form.intro";
    i.normal = "fixture.normal"; i.read_only = "fixture.read_only"; i.disabled = "fixture.disabled";
    i.error = "fixture.error"; i.long_text = "fixture.long"; i.reset = "form.reset";
    i.name_label = "form.name.label"; i.name = "form.name"; i.name_help = "form.name.help"; i.name_error = "form.name.error";
    i.volume = "form.volume"; i.volume_latest = "form.volume.latest"; i.notices = "form.notices"; i.mode = "form.mode"; i.offline_disabled = "fixture.offline_disabled";
    i.region = "form.region"; i.region_empty = "fixture.region_empty";
    i.apply = "form.apply"; i.state = "result.state"; i.draft = "result.draft"; i.result = "result.applied"; i.count = "result.count"; i.latest = "form.latest";
    return i;
}

Form::Form(Ids ids, bool pages) : ids_(std::move(ids)), applied_(initial_name), pages_(pages), ui({}) {
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
    handles.notices = ui.add_checkbox({.id = i.notices, .kind = Kind::checkbox, .label = u"\u555f\u7528\u63d0\u793a"});
    handles.mode = ui.add_radio_group({.id = i.mode, .kind = Kind::radio_group, .label = u"\u6a21\u5f0f"},
        {{{"normal"}, u"\u4e00\u822c"}, {{"focus"}, u"\u5c08\u6ce8"}, {{"offline"}, u"\u96e2\u7dda"}});
    handles.region = ui.add_dropdown({.id = i.region, .kind = Kind::dropdown, .label = u"\u5730\u5340"}, regions());
    handles.volume = ui.add_slider({.id = i.volume, .kind = Kind::slider, .label = u"\u97f3\u91cf"}, {0, 100, 5});
    handles.volume_latest = ui.add_button({.id = i.volume_latest, .kind = Kind::button, .label = u"\u97f3\u91cf\u6539\u7528\u6700\u65b0\u503c", .enabled = false});
    handles.offline_disabled = ui.add_button({.id = i.offline_disabled, .kind = Kind::button, .label = u"\u505c\u7528\u96e2\u7dda"});
    handles.region_empty = ui.add_button({.id = i.region_empty, .kind = Kind::button, .label = u"\u6e05\u7a7a\uff0f\u9084\u539f\u5730\u5340"});
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
    handles.notices.connect_source({[this] { return notices_applied_; }, [this](std::function<void()> changed) {
        notices_changed_ = std::move(changed); return [this] { notices_changed_ = {}; };
    }});
    handles.mode.connect_source({[this] { return mode_applied_; }, [this](std::function<void()> changed) {
        mode_changed_ = std::move(changed); return [this] { mode_changed_ = {}; };
    }});
    handles.region.connect_source({[this] { return region_applied_; }, [this](std::function<void()> changed) {
        region_changed_ = std::move(changed); return [this] { region_changed_ = {}; };
    }});
    handles.volume.connect_source({[this] { return volume_applied_; }, [this](std::function<void()> changed) {
        volume_changed_ = std::move(changed); return [this] { volume_changed_ = {}; };
    }});
    handles.intro.connect_source([this] { return long_intro_ ? long_intro : short_intro; });
    handles.state.connect_source([this] {
        std::u16string state = mode_ == Mode::read_only ? u"\u540d\u7a31\u552f\u8b80" : mode_ == Mode::disabled ? u"\u8cc7\u6599\u5340\u505c\u7528" : u"\u6b63\u5e38";
        if (handles.name.describe()->invalid) state += u"\u30fb\u540d\u7a31\u6709\u932f\u8aa4";
        if (handles.name.source_conflict().value.value_or(false)) state += u"\u30fb\u4f86\u6e90\u5df2\u66f4\u65b0";
        return u"\u72c0\u614b\uff1a" + state;
    });
    auto mode_name = [](const std::optional<OptionValue>& value) -> std::u16string {
        if (!value) return u"\u672a\u9078\u53d6";
        if (value->id == "normal") return u"\u4e00\u822c";
        if (value->id == "focus") return u"\u5c08\u6ce8";
        if (value->id == "offline") return u"\u96e2\u7dda";
        return u"\u672a\u77e5";
    };
    auto number = [](int value) { auto n = std::to_string(value); return std::u16string(n.begin(), n.end()); };
    auto region_name = [](const std::optional<OptionValue>& value) -> std::u16string {
        if (!value) return u"\u672a\u9078\u53d6";
        if (value->id == "north") return u"\u5317\u5340";
        if (value->id == "central") return u"\u4e2d\u5340";
        if (value->id == "south") return u"\u5357\u5340";
        return u"\u672a\u77e5";
    };
    handles.draft.connect_source([this, mode_name, region_name, number] { return u"\u76ee\u524d\u7de8\u8f2f\u503c\uff1a" + *handles.name.draft() +
        u" / \u63d0\u793a\uff1a" + (*handles.notices.draft() ? u"\u555f\u7528" : u"\u505c\u7528") + u" / \u6a21\u5f0f\uff1a" + mode_name(*handles.mode.draft()) + u" / \u5730\u5340\uff1a" + region_name(*handles.region.draft()) + u" / \u97f3\u91cf\uff1a" + number(*handles.volume.draft()); });
    handles.result.connect_source([this, mode_name, region_name, number] { return u"\u4e0a\u6b21\u5957\u7528\u7d50\u679c\uff1a" + applied_ +
        u" / \u63d0\u793a\uff1a" + (notices_applied_ ? u"\u555f\u7528" : u"\u505c\u7528") + u" / \u6a21\u5f0f\uff1a" + mode_name(mode_applied_) + u" / \u5730\u5340\uff1a" + region_name(region_applied_) + u" / \u97f3\u91cf\uff1a" + number(volume_applied_); });
    handles.count.connect_source([this] { auto n = std::to_string(applies_); return u"\u5957\u7528\u6b21\u6578\uff1a" + std::u16string(n.begin(), n.end()); });
    handles.name.on_blur([this] { validate(); });
    ui.on_change([this] { if (handles.name.describe()->invalid) validate(); refresh_conflict(); });
    ui.connect_mixed_confirmation(handles.apply, {handles.name, handles.notices, handles.mode, handles.region, handles.volume}, [this](const auto& changes) { return apply(changes); });
    handles.normal.bind([this] { set_mode(Mode::normal); return Result{}; });
    handles.read_only.bind([this] { set_mode(Mode::read_only); return Result{}; });
    handles.disabled.bind([this] { set_mode(Mode::disabled); return Result{}; });
    handles.offline_disabled.bind([this] {
        offline_disabled_ = !offline_disabled_;
        return handles.mode.enable_option({"offline"}, !offline_disabled_);
    });
    handles.region_empty.bind([this] {
        region_empty_ = !region_empty_;
        return handles.region.set_options(region_empty_ ? std::vector<RadioOption>{} : regions());
    });
    handles.long_text.bind([this] { long_intro_ = !long_intro_; return Result{}; });
    handles.reset.bind([this] { return reset(); });
    handles.volume_latest.bind([this] { auto result = handles.volume.use_latest(); refresh_conflict(); return result; });
    handles.latest.bind([this] { auto result = handles.name.use_latest(); refresh_conflict(); return result; });
    handles.error.bind([this] {
        set_mode(Mode::normal);
        handles.name.set_value(u"");
        validate();
        handles.name.focus();
        return Result{};
    });
    if (pages_) {
        // V02 is deliberately a small state-bearing placeholder for the list from #20.
        const auto prefix = i.title + ".pages.";
        handles.v02_title = ui.add_label({.id = prefix + "v02.title", .kind = Kind::label, .value = u"List page (placeholder)"});
        handles.search = ui.add_text({.id = prefix + "v02.search", .kind = Kind::text, .label = u"Search (placeholder)"});
        handles.choose_sample = ui.add_button({.id = prefix + "v02.choose", .kind = Kind::button, .label = u"Select sample T006"});
        handles.reset_v02 = ui.add_button({.id = prefix + "v02.reset", .kind = Kind::button, .label = u"Reset page"});
        handles.v02_result = ui.add_label({.id = prefix + "v02.result", .kind = Kind::label});
        handles.v03_title = ui.add_label({.id = prefix + "v03.title", .kind = Kind::label, .value = u"Confirmation and help (placeholder)"});
        handles.reset_v03 = ui.add_button({.id = prefix + "v03.reset", .kind = Kind::button, .label = u"Reset page"});
        handles.reset_v03.bind([this] { scroll.offset = 0; return handles.reset_v03.focus(); });
        handles.search.connect_source({[this] { return search_source_; }, [](std::function<void()>) { return [] {}; }});
        handles.search.connect_confirmed([](const TextChange&) { return Result{}; });
        handles.v02_result.connect_source([this] { return sample_selected_ ? u"Sample: T006" : u"Sample: none"; });
        handles.choose_sample.bind([this] { sample_selected_ = true; return Result{}; });
        handles.reset_v02.bind([this] {
            sample_selected_ = false;
            handles.search.reset_to_source();
            scroll.offset = 0;
            return handles.reset_v02.focus();
        });
        handles.tabs = ui.add_tabs({.id = prefix + "tabs", .kind = Kind::tabs, .label = u"Showcase"}, {
            {{"v01"}, u"Edit form", true, {handles.title, handles.intro, handles.normal, handles.read_only, handles.disabled,
                handles.error, handles.long_text, handles.reset, handles.name_label, handles.name, handles.name_help,
                handles.name_error, handles.notices, handles.mode, handles.region, handles.volume, handles.volume_latest,
                handles.offline_disabled, handles.region_empty, handles.apply, handles.state, handles.draft, handles.result,
                handles.count, handles.latest}},
            {{"v02"}, u"List", true, {handles.v02_title, handles.search, handles.choose_sample, handles.reset_v02, handles.v02_result}},
            {{"v03"}, u"Confirm", true, {handles.v03_title, handles.reset_v03}}
        });
        handles.tabs.on_page_change([this](OptionValue from, OptionValue to) {
            auto index = [](const OptionValue& page) { return page.id == "v01" ? 0 : page.id == "v02" ? 1 : 2; };
            page_offsets_[index(from)] = scroll.offset;
            scroll.offset = page_offsets_[index(to)];
        });
    }
}

bool Form::validate() {
    bool valid = !blank(*handles.name.draft());
    handles.name.set_error(valid ? u"" : required_name);
    return valid;
}
MixedConfirmationResult Form::apply(const std::vector<FieldChange>& changes) {
    const auto& name = std::get<TextChange>(changes[0]);
    const auto& notices = std::get<BoolChange>(changes[1]);
    const auto& mode = std::get<OptionChange>(changes[2]);
    const auto& region = std::get<DropdownChange>(changes[3]);
    const auto& volume = std::get<NumericChange>(changes[4]);
    std::vector<MixedFieldError> errors;
    if (blank(name.value)) errors.push_back({name.field, required_name});
    if (!region.value || (region.value->id != "north" && region.value->id != "central" && region.value->id != "south")) errors.push_back({region.field, required_region});
    if (!errors.empty()) return {{Status::invalid_value, u"\u8acb\u4fee\u6b63\u6b04\u4f4d"}, std::move(errors)};
    applied_ = name.value;
    notices_applied_ = notices.value;
    mode_applied_ = mode.value;
    region_applied_ = region.value;
    volume_applied_ = volume.value;
    ++applies_;
    if (source_changed_) source_changed_();
    if (notices_changed_) notices_changed_();
    if (mode_changed_) mode_changed_();
    if (region_changed_) region_changed_();
    if (volume_changed_) volume_changed_();
    return {};
}
void Form::update_source(std::u16string value) {
    applied_ = std::move(value);
    if (source_changed_) source_changed_();
    refresh_conflict();
}
void Form::update_volume_source(int value) {
    volume_applied_ = value;
    if (volume_changed_) volume_changed_();
    refresh_conflict();
}
void Form::refresh_conflict() {
    handles.volume_latest.enable(mode_ != Mode::disabled && handles.volume.describe()->source_conflict);
    handles.latest.enable(mode_ != Mode::disabled && mode_ != Mode::read_only && handles.name.source_conflict().value.value_or(false));
}
void Form::set_mode(Mode mode) {
    mode_ = mode;
    handles.name.read_only(mode == Mode::read_only);
    handles.name.enable(mode != Mode::disabled);
    handles.apply.enable(mode != Mode::disabled);
    handles.notices.enable(mode != Mode::disabled);
    handles.mode.enable(mode != Mode::disabled);
    handles.region.enable(mode != Mode::disabled);
    handles.volume.enable(mode != Mode::disabled);
    refresh_conflict();
}
Result Form::reset() {
    set_mode(Mode::normal);
    long_intro_ = false;
    update_source(initial_name);
    notices_applied_ = true; mode_applied_ = OptionValue{"normal"}; region_applied_ = OptionValue{"north"}; volume_applied_ = 40;
    region_empty_ = false; handles.region.set_options(regions());
    if (notices_changed_) notices_changed_();
    if (mode_changed_) mode_changed_();
    if (region_changed_) region_changed_();
    if (volume_changed_) volume_changed_();
    handles.notices.use_latest(); handles.mode.use_latest(); handles.region.use_latest(); handles.volume.use_latest();
    offline_disabled_ = false; handles.mode.enable_option({"offline"}, true);
    handles.name.reset_to_source();
    refresh_conflict();
    applies_ = 0;
    scroll.offset = 0;
    handles.reset.focus();
    return {};
}

layout::Node Form::page() const {
    using namespace layout;
    auto form = layout::scroll(column({
        control(handles.title, 24),
        control(handles.intro),
        grid({control(handles.normal), control(handles.read_only), control(handles.disabled), control(handles.error), control(handles.long_text), control(handles.offline_disabled), control(handles.region_empty), control(handles.reset)}, 120, 8),
        grid({column({control(handles.name), control(handles.notices), control(handles.mode), control(handles.region), control(handles.volume), control(handles.volume_latest), row({control(handles.apply), control(handles.latest), control(handles.state)}, 16)}, 12),
              column({control(handles.draft), control(handles.result), control(handles.count)}, 8)},
             360, 32),
    }, 16, 24));
    if (!pages_) return form;
    auto active = *handles.tabs.active();
    layout::Node content;
    if (active.id == "v01") content = std::move(form);
    else if (active.id == "v02") content = layout::scroll(column({control(handles.v02_title, 24), control(handles.search),
        row({control(handles.choose_sample), control(handles.reset_v02)}), control(handles.v02_result)}, 16, 24));
    else content = layout::scroll(column({control(handles.v03_title, 24), control(handles.reset_v03)}, 16, 24));
    return pinned(control(handles.tabs), std::move(content));
}
}
