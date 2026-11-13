#include "edit_form.hpp"
#include <algorithm>
#include <string>
#include <utility>

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

// #84: Chinese, English words, digits and punctuation in one paragraph, long enough to wrap in a normal window.
const std::u16string justify_sample_text =
    u"LcarsUI 是面向一般桌面應用的 UI 框架，並以讓 AI 操作應用程式作為未來整合方向。這一段中英數混排的文字，用來比較靠左、置中、靠右、"
    u"左右對齊與分散對齊：Windows 11 的 2026 年版本、Direct2D 與 OpenGL 渲染，以及 12345 這類數字，都可以在不同視窗寬度下，觀察每一行的兩端是否切齊。";

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

Form::Form(Ids ids, bool pages, std::function<FontResolution(std::u16string_view)> resolve_font) : ids_(std::move(ids)), applied_(initial_name), resolve_font_(std::move(resolve_font)), pages_(pages), ui() {
    const auto& i = ids_;
    handles.title = ui.add_label({.id = i.title, .kind = Kind::label, .value = u"編輯表單"}).value.value();
    handles.intro = ui.add_label({.id = i.intro, .kind = Kind::label}).value.value();
    handles.normal = ui.add_button({.id = i.normal, .kind = Kind::button, .label = u"正常"}).value.value();
    handles.read_only = ui.add_button({.id = i.read_only, .kind = Kind::button, .label = u"名稱唯讀"}).value.value();
    handles.disabled = ui.add_button({.id = i.disabled, .kind = Kind::button, .label = u"資料區停用"}).value.value();
    handles.error = ui.add_button({.id = i.error, .kind = Kind::button, .label = u"名稱錯誤"}).value.value();
    handles.long_text = ui.add_button({.id = i.long_text, .kind = Kind::button, .label = u"長說明"}).value.value();
    handles.reset = ui.add_button({.id = i.reset, .kind = Kind::button, .label = u"重設"}).value.value();
    handles.name_label = ui.add_label({.id = i.name_label, .kind = Kind::label, .value = u"顯示名稱", .target = i.name, .role = TextRole::name}).value.value();
    handles.name = ui.add_text({.id = i.name, .kind = Kind::text, .value = initial_name}).value.value();
    handles.name_help = ui.add_label({.id = i.name_help, .kind = Kind::label, .value = u"單行文字，可輸入繁體中文或英文。", .target = i.name, .role = TextRole::description}).value.value();
    handles.name_error = ui.add_label({.id = i.name_error, .kind = Kind::label, .target = i.name, .role = TextRole::error}).value.value();
    handles.notices = ui.add_checkbox({.id = i.notices, .kind = Kind::checkbox, .label = u"\u555f\u7528\u63d0\u793a"}).value.value();
    handles.mode = ui.add_radio_group({.id = i.mode, .kind = Kind::radio_group, .label = u"\u6a21\u5f0f"},
        {{{"normal"}, u"\u4e00\u822c"}, {{"focus"}, u"\u5c08\u6ce8"}, {{"offline"}, u"\u96e2\u7dda"}}).value.value();
    handles.region = ui.add_dropdown({.id = i.region, .kind = Kind::dropdown, .label = u"\u5730\u5340"}, regions()).value.value();
    handles.volume = ui.add_slider({.id = i.volume, .kind = Kind::slider, .label = u"\u97f3\u91cf"}, {0, 100, 5}).value.value();
    handles.volume_latest = ui.add_button({.id = i.volume_latest, .kind = Kind::button, .label = u"\u97f3\u91cf\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.notices_latest = ui.add_button({.id = i.notices + ".latest", .kind = Kind::button, .label = u"\u63d0\u793a\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.mode_latest = ui.add_button({.id = i.mode + ".latest", .kind = Kind::button, .label = u"\u6a21\u5f0f\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.region_latest = ui.add_button({.id = i.region + ".latest", .kind = Kind::button, .label = u"\u5730\u5340\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.offline_disabled = ui.add_button({.id = i.offline_disabled, .kind = Kind::button, .label = u"\u505c\u7528\u96e2\u7dda"}).value.value();
    handles.region_empty = ui.add_button({.id = i.region_empty, .kind = Kind::button, .label = u"\u6e05\u7a7a\uff0f\u9084\u539f\u5730\u5340"}).value.value();
    handles.apply = ui.add_button({.id = i.apply, .kind = Kind::button, .label = u"套用"}).value.value();
    handles.state = ui.add_label({.id = i.state, .kind = Kind::label}).value.value();
    handles.draft = ui.add_label({.id = i.draft, .kind = Kind::label}).value.value();
    handles.result = ui.add_label({.id = i.result, .kind = Kind::label}).value.value();
    handles.count = ui.add_label({.id = i.count, .kind = Kind::label}).value.value();
    handles.latest = ui.add_button({.id = i.latest, .kind = Kind::button, .label = u"\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();

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
    auto title_id = handles.title.describe()->id;
    auto title_text = handles.title.describe()->value;
    handles.title.connect_source([this, title_id, title_text] {
        return clip_padding_id_ == title_id ? clip_padding_text_ : title_text;
    });
    handles.intro.connect_source([this] {
        return clip_padding_id_ == ids_.intro ? clip_padding_text_ : (long_intro_ ? long_intro : short_intro);
    });
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
    bind_action(handles.normal, [this] { set_mode(Mode::normal); return Result{}; });
    bind_action(handles.read_only, [this] { set_mode(Mode::read_only); return Result{}; });
    bind_action(handles.disabled, [this] { set_mode(Mode::disabled); return Result{}; });
    bind_action(handles.offline_disabled, [this] {
        offline_disabled_ = !offline_disabled_;
        return handles.mode.enable_option({"offline"}, !offline_disabled_);
    });
    bind_action(handles.region_empty, [this] {
        region_empty_ = !region_empty_;
        return handles.region.set_options(region_empty_ ? std::vector<RadioOption>{} : regions());
    });
    bind_action(handles.long_text, [this] { long_intro_ = !long_intro_; return Result{}; });
    bind_action(handles.reset, [this] { return reset(); });
    bind_action(handles.notices_latest, [this] { auto result = handles.notices.use_latest(); refresh_conflict(); return result; });
    bind_action(handles.mode_latest, [this] { auto result = handles.mode.use_latest(); refresh_conflict(); return result; });
    bind_action(handles.region_latest, [this] { auto result = handles.region.use_latest(); refresh_conflict(); return result; });
    bind_action(handles.volume_latest, [this] { auto result = handles.volume.use_latest(); refresh_conflict(); return result; });
    bind_action(handles.latest, [this] { auto result = handles.name.use_latest(); refresh_conflict(); return result; });
    bind_action(handles.error, [this] {
        set_mode(Mode::normal);
        handles.name.set_value(u"");
        validate();
        handles.name.focus();
        return Result{};
    });
    if (pages_) {
        const auto prefix = i.title + ".pages.";
        list = std::make_unique<list_page::Page>(&ui, &scroll, prefix + "v02.");
        handles.v03_title = ui.add_label({.id = prefix + "v03.title", .kind = Kind::label, .value = u"確認與說明"}).value.value();
        auto v03_title_id = handles.v03_title.describe()->id;
        auto v03_title_text = handles.v03_title.describe()->value;
        handles.v03_title.connect_source([this, v03_title_id, v03_title_text] {
            return clip_padding_id_ == v03_title_id ? clip_padding_text_ : v03_title_text;
        });
        handles.progress = ui.add_progress({.id = prefix + "v03.progress", .kind = Kind::progress, .label = u"示範進度"}).value.value();
        handles.progress_source = ui.add_label({.id = prefix + "v03.source", .kind = Kind::label}).value.value();
        handles.progress_zero = ui.add_button({.id = prefix + "v03.zero", .kind = Kind::button, .label = u"0%"}).value.value();
        handles.progress_half = ui.add_button({.id = prefix + "v03.half", .kind = Kind::button, .label = u"50%"}).value.value();
        handles.progress_tip = ui.add_tooltip(prefix + "v03.half.tip", handles.progress_half, u"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002").value.value();
        handles.progress_full = ui.add_button({.id = prefix + "v03.full", .kind = Kind::button, .label = u"100%"}).value.value();
        handles.progress_below = ui.add_button({.id = prefix + "v03.below", .kind = Kind::button, .label = u"-10"}).value.value();
        handles.progress_above = ui.add_button({.id = prefix + "v03.above", .kind = Kind::button, .label = u"110"}).value.value();
        handles.progress.connect_source({[this] { return progress_; }, [this](std::function<void()> changed) {
            progress_changed_ = std::move(changed); return [this] { progress_changed_ = {}; };
        }});
        handles.progress_source.connect_source([this] {
            auto number = std::to_string(progress_);
            return u"來源值：" + std::u16string(number.begin(), number.end());
        });
        bind_action(handles.progress_zero, [this] { update_progress(0); return Result{}; });
        bind_action(handles.progress_half, [this] { update_progress(50); return Result{}; });
        bind_action(handles.progress_full, [this] { update_progress(100); return Result{}; });
        bind_action(handles.progress_below, [this] { update_progress(-10); return Result{}; });
        bind_action(handles.progress_above, [this] { update_progress(110); return Result{}; });
        handles.reset_v03 = ui.add_button({.id = prefix + "v03.reset", .kind = Kind::button, .label = u"Reset page"}).value.value();
        bind_action(handles.reset_v03, [this] {
            if (!handles.dialog_temporary.describe()) {
                auto id = ids_.title + ".pages.v03.temporary";
                auto replacement = ui.add_button({.id = std::move(id), .kind = Kind::button, .label = u"Temporary opener..."}).value.value();
                if (auto result = handles.tabs.add_member({"v03"}, replacement); !result) return result;
                handles.dialog_temporary = replacement;
                bind_action(handles.dialog_temporary, [this] { return handles.dialog.open(handles.dialog_temporary); });
            }
            update_progress(50); dialog_confirmations_ = 0; dialog_result_ = u"No result"; scroll.offset = 0; return handles.reset_v03.focus();
        });
        handles.dialog_open = ui.add_button({.id = prefix + "v03.open", .kind = Kind::button, .label = u"Reset progress..."}).value.value();
        handles.dialog_temporary = ui.add_button({.id = prefix + "v03.temporary", .kind = Kind::button, .label = u"Temporary opener..."}).value.value();
        handles.dialog_result = ui.add_label({.id = prefix + "v03.result", .kind = Kind::label}).value.value();
        auto dialog_result_id = handles.dialog_result.describe()->id;
        handles.dialog_result.connect_source([this, dialog_result_id] {
            if (clip_padding_id_ == dialog_result_id) return clip_padding_text_;
            auto count = std::to_string(dialog_confirmations_);
            return u"Result: " + dialog_result_ + u"; confirmations: " + std::u16string(count.begin(), count.end());
        });
        handles.dialog_title = ui.add_label({.id = prefix + "dialog.title", .kind = Kind::label, .value = u"\u91cd\u8a2d\u793a\u7bc4\u9032\u5ea6"}).value.value();
        handles.dialog_body = ui.add_label({.id = prefix + "dialog.body", .kind = Kind::label, .value = u"\u5c07\u9032\u5ea6\u6539\u70ba 0%\u3002\u5176\u4ed6\u9801\u9762\u8cc7\u6599\u6703\u4fdd\u7559\u3002"}).value.value();
        handles.dialog_cancel = ui.add_button({.id = prefix + "dialog.cancel", .kind = Kind::button, .label = u"\u53d6\u6d88"}).value.value();
        handles.dialog_confirm = ui.add_button({.id = prefix + "dialog.confirm", .kind = Kind::button, .label = u"\u91cd\u8a2d\u9032\u5ea6"}).value.value();
        handles.dialog = ui.add_dialog(prefix + "dialog", {handles.dialog_title, handles.dialog_body, handles.dialog_cancel, handles.dialog_confirm}, handles.dialog_cancel, handles.dialog_open,
            [this](bool confirmed) { dialog_result_ = confirmed ? u"Confirmed" : u"Cancelled"; if (confirmed) { update_progress(0); ++dialog_confirmations_; } }, handles.dialog_title).value.value();
        bind_action(handles.dialog_open, [this] { return handles.dialog.open(handles.dialog_open); });
        bind_action(handles.dialog_temporary, [this] { return handles.dialog.open(handles.dialog_temporary); });
        bind_action(handles.dialog_cancel, [this] { return handles.dialog.settle(false); });
        bind_action(handles.dialog_confirm, [this] { return handles.dialog.settle(true); });
        handles.v04_title = ui.add_label({.id = prefix + "v04.title", .kind = Kind::label, .value = u"\u5448\u73fe\u8a2d\u5b9a"}).value.value();
        auto resolve = [this](std::u16string_view family) {
            if (resolve_font_) return resolve_font_(family);
            FontResolution result; result.requested = family; result.primary = family.empty() ? u"Segoe UI" : std::u16string(family); return result;
        };
        std::vector<RadioOption> fonts = {{{"default"}, u"\u9810\u8a2d"}};
        for (auto [id, family] : {std::pair{"jhenghei", u"Microsoft JhengHei"}, {"segoe", u"Segoe UI"}, {"mingliu", u"MingLiU"}, {"consolas", u"Consolas"}}) {
            bool available = resolve(family).requested_available;
            fonts.push_back({{id}, std::u16string(family) + (available ? u"" : u"\uff08\u672a\u5b89\u88dd\uff09"), available});
        }
        fonts.push_back({{"missing"}, u"\u4e0d\u5b58\u5728\u7684\u5b57\u578b\uff08\u6e2c\u8a66\uff09"});
        handles.font = ui.add_dropdown({.id = prefix + "v04.font", .kind = Kind::dropdown, .label = u"\u5b57\u578b"}, std::move(fonts)).value.value();
        handles.font.connect_source({[this] {
            const auto& f = ui.presentation().font_family;
            return std::optional<OptionValue>{{f.empty() ? "default" : f == u"Microsoft JhengHei" ? "jhenghei" : f == u"Segoe UI" ? "segoe" : f == u"MingLiU" ? "mingliu" : f == u"Consolas" ? "consolas" : "missing"}};
        }, [this](std::function<void()> changed) { font_changed_ = std::move(changed); return [this] { font_changed_ = {}; }; }});
        handles.font.connect_immediate([this](const Change<DropdownHandle>& change) {
            if (!change.value) return Result{Status::invalid_value, u"Font choice required"};
            const auto& id = change.value->id;
            auto settings = ui.presentation();
            settings.font_family = id == "default" ? u"" : id == "jhenghei" ? u"Microsoft JhengHei" : id == "segoe" ? u"Segoe UI" : id == "mingliu" ? u"MingLiU" : id == "consolas" ? u"Consolas" : u"Missing test font";
            auto result = ui.set_presentation(settings); if (result && font_changed_) font_changed_(); return result;
        });
        handles.font_status = ui.add_label({.id = prefix + "v04.status", .kind = Kind::label}).value.value();
        handles.font_status.connect_source([this, resolve] {
            auto result = resolve(ui.presentation().font_family);
            auto size = std::to_string(ui.presentation().text_size);
            return u"\u76ee\u524d\u72c0\u614b\uff1a" + result.primary + (result.requested_available ? u"" : u"\uff08\u5df2\u6539\u7528\u9810\u8a2d\uff09") + u"\uff0c\u6587\u5b57\u5927\u5c0f " + std::u16string(size.begin(), size.end()) +
                (ui.presentation().variant == ThemeVariant::dark() ? u"\uff0c\u6df1\u8272" : u"\uff0c\u6dfa\u8272") +
                (ui.presentation().density == Density::compact ? u"\uff0c\u7dca\u6e4a" : u"\uff0c\u6a19\u6e96");
        });
        handles.reset_v04 = ui.add_button({.id = prefix + "v04.reset", .kind = Kind::button, .label = u"\u91cd\u8a2d V04"}).value.value();
        bind_action(handles.reset_v04, [this] {
            auto result = ui.set_presentation({}); if (!result) return result;
            if (font_changed_) font_changed_(); if (text_size_changed_) text_size_changed_(); if (dark_changed_) dark_changed_();
            handles.font.use_latest(); handles.text_size.use_latest(); handles.dark.use_latest(); handles.compact.use_latest(); return Result{};
        });
        handles.text_size = ui.add_slider({.id = prefix + "v04.text_size", .kind = Kind::slider, .label = u"\u6587\u5b57\u5927\u5c0f"}, {12, 28, 1}).value.value();
        handles.dark = ui.add_checkbox({.id = prefix + "v04.dark", .kind = Kind::checkbox, .label = u"\u6df1\u8272"}).value.value();
        handles.dark.connect_source({[this] { return ui.presentation().variant == ThemeVariant::dark(); }, [this](std::function<void()> changed) { dark_changed_ = std::move(changed); return [this] { dark_changed_ = {}; }; }});
        handles.dark.connect_immediate([this](const Change<CheckboxHandle>& change) {
            auto settings = ui.presentation(); settings.variant = change.value ? ThemeVariant::dark() : ThemeVariant::light();
            auto result = ui.set_presentation(settings); if (result && dark_changed_) dark_changed_(); return result;
        });
        handles.compact = ui.add_checkbox({.id = prefix + "v04.compact", .kind = Kind::checkbox, .label = u"\u7dfb\u6e4a"}).value.value();
        handles.compact.connect_source({[this] { return ui.presentation().density == Density::compact; }, [](std::function<void()>) { return [] {}; }});
        handles.compact.connect_immediate([this](const Change<CheckboxHandle>& change) {
            auto settings = ui.presentation(); settings.density = change.value ? Density::compact : Density::standard;
            return ui.set_presentation(settings);
        });
        handles.preview = ui.add_text({.id = prefix + "v04.preview", .kind = Kind::text, .label = u"\u9810\u89bd", .value = u"\u7e41\u4e2d English \U0001f600 \U00020000 \u2764\ufe0f"}).value.value();
        handles.preview.connect_source({[this] { return preview_; }, [](std::function<void()>) { return [] {}; }});
        handles.preview.connect_immediate([this](const Change<TextHandle>& change) { preview_ = change.value; return Result{}; });
        // #84: one mixed-script paragraph (Chinese, English and digits) whose alignment and justification method
        // the two dropdowns switch live, and short labels to compare distribute with left alignment.
        handles.justify_sample = ui.add_label({.id = prefix + "v04.justify.sample", .kind = Kind::label, .value = justify_sample_text, .text_align = TextAlign::justify}).value.value();
        handles.distribute_name = ui.add_label({.id = prefix + "v04.justify.name", .kind = Kind::label, .value = u"姓　名", .text_align = TextAlign::distribute}).value.value();
        handles.distribute_plain = ui.add_label({.id = prefix + "v04.justify.plain", .kind = Kind::label, .value = u"姓　名"}).value.value();
        handles.justify_align = ui.add_dropdown({.id = prefix + "v04.justify.align", .kind = Kind::dropdown, .label = u"對齊"},
            {{{"left"}, u"靠左"}, {{"center"}, u"置中"}, {{"right"}, u"靠右"}, {{"justify"}, u"左右對齊"}, {{"distribute"}, u"分散對齊"}}).value.value();
        handles.justify_method = ui.add_dropdown({.id = prefix + "v04.justify.method", .kind = Kind::dropdown, .label = u"對齊方法"},
            {{{"automatic"}, u"自動"}, {{"spaces"}, u"只拉空格"}, {{"per_character"}, u"每字元"}}).value.value();
        handles.justify_align.connect_source({[this] {
            const auto align = handles.justify_sample.text_align().value.value_or(TextAlign::left);
            return std::optional<OptionValue>{{align == TextAlign::center ? "center" : align == TextAlign::right ? "right" : align == TextAlign::justify ? "justify" : align == TextAlign::distribute ? "distribute" : "left"}};
        }, [this](std::function<void()> changed) { justify_align_changed_ = std::move(changed); return [this] { justify_align_changed_ = {}; }; }});
        handles.justify_align.connect_immediate([this](const Change<DropdownHandle>& change) {
            if (!change.value) return Result{Status::invalid_value, u"Alignment choice required"};
            const auto& id = change.value->id;
            auto result = handles.justify_sample.set_text_align(id == "center" ? TextAlign::center : id == "right" ? TextAlign::right : id == "justify" ? TextAlign::justify : id == "distribute" ? TextAlign::distribute : TextAlign::left);
            if (result && justify_align_changed_) justify_align_changed_();
            return result;
        });
        handles.justify_method.connect_source({[this] {
            const auto method = handles.justify_sample.justify_method().value.value_or(JustifyMethod::automatic);
            return std::optional<OptionValue>{{method == JustifyMethod::spaces ? "spaces" : method == JustifyMethod::per_character ? "per_character" : "automatic"}};
        }, [this](std::function<void()> changed) { justify_method_changed_ = std::move(changed); return [this] { justify_method_changed_ = {}; }; }});
        handles.justify_method.connect_immediate([this](const Change<DropdownHandle>& change) {
            if (!change.value) return Result{Status::invalid_value, u"Method choice required"};
            const auto& id = change.value->id;
            auto result = handles.justify_sample.set_justify_method(id == "spaces" ? JustifyMethod::spaces : id == "per_character" ? JustifyMethod::per_character : JustifyMethod::automatic);
            if (result && justify_method_changed_) justify_method_changed_();
            return result;
        });
        handles.text_size.connect_source({[this] { return ui.presentation().text_size; }, [this](std::function<void()> changed) { text_size_changed_ = std::move(changed); return [this] { text_size_changed_ = {}; }; }});
        handles.text_size.connect_immediate([this](const Change<SliderHandle>& change) {
            auto settings = ui.presentation(); settings.text_size = change.value;
            auto result = ui.set_presentation(settings); if (result && text_size_changed_) text_size_changed_(); return result;
        });
        handles.tabs = ui.add_tabs({.id = prefix + "tabs", .kind = Kind::tabs, .label = u"Showcase"}, {
            {{"v01"}, u"Edit form", true, {handles.title, handles.intro, handles.normal, handles.read_only, handles.disabled,
                handles.error, handles.long_text, handles.reset, handles.name_label, handles.name, handles.name_help,
                handles.name_error, handles.notices, handles.notices_latest, handles.mode, handles.mode_latest, handles.region, handles.region_latest, handles.volume, handles.volume_latest,
                handles.offline_disabled, handles.region_empty, handles.apply, handles.state, handles.draft, handles.result,
                handles.count, handles.latest}},
            {{"v02"}, u"List", true, {list->handles.title, list->handles.zero, list->handles.one, list->handles.twenty,
                list->handles.hundred, list->handles.sort, list->handles.disable_eight, list->handles.reset,
                list->handles.search, list->handles.list, list->handles.erase, list->handles.detail}},
            {{"v03"}, u"Confirm", true, {handles.v03_title, handles.progress, handles.progress_source, handles.dialog_result,
                handles.progress_zero, handles.progress_half, handles.progress_full, handles.progress_below, handles.progress_above, handles.reset_v03, handles.dialog_open, handles.dialog_temporary}},
            {{"v04"}, u"\u5448\u73fe\u8a2d\u5b9a", true, {handles.v04_title, handles.font, handles.text_size, handles.dark, handles.compact, handles.preview, handles.font_status, handles.reset_v04,
                handles.justify_sample, handles.justify_align, handles.justify_method, handles.distribute_name, handles.distribute_plain}}
        }).value.value();
        handles.tabs.on_page_change([this](OptionValue from, OptionValue to) {
            auto index = [](const OptionValue& page) { return page.id == "v01" ? 0 : page.id == "v02" ? 1 : page.id == "v03" ? 2 : 3; };
            page_offsets_[index(from)] = scroll.offset;
            scroll.offset = page_offsets_[index(to)];
        });
    }
}

Result Form::bind_action(ButtonHandle button, std::function<Result()> action) {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto result = button.bind(action);
    if (result) fixture_actions_.insert_or_assign(semantic->id, std::move(action));
    return result;
}
Result Form::restore_fixture_action(ButtonHandle button) const {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto found = fixture_actions_.find(semantic->id);
    if (found != fixture_actions_.end()) return button.bind(found->second);
    if (list) return list->restore_fixture_action(button);
    return {Status::not_found, u"No original fixture action"};
}
Result Form::set_clip_padding(LabelHandle label, std::u16string text) {
    auto semantic = label.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto id = semantic->id;
    if (list && (id == list->handles.title.describe()->id || id == list->handles.detail.describe()->id))
        return list->set_clip_padding(label, std::move(text));
    if (id != handles.title.describe()->id && id != handles.intro.describe()->id &&
        (!pages_ || (id != handles.v03_title.describe()->id && id != handles.dialog_result.describe()->id && id != handles.v04_title.describe()->id)))
        return {Status::invalid_value, u"Unknown clipping fixture label"};
    clip_padding_id_ = std::move(id); clip_padding_text_ = std::move(text); return {};
}
void Form::clear_clip_padding() {
    clip_padding_id_.clear(); clip_padding_text_.clear();
    if (list) list->clear_clip_padding();
}

bool Form::validate() {
    bool valid = !blank(*handles.name.draft());
    handles.name.set_error(valid ? u"" : required_name);
    return valid;
}
MixedConfirmationResult Form::apply(const std::vector<FieldChange>& changes) {
    if (auto observer = std::exchange(next_apply_observer_, {})) observer();
    const auto& name = std::get<Change<TextHandle>>(changes[0]);
    const auto& notices = std::get<Change<CheckboxHandle>>(changes[1]);
    const auto& mode = std::get<Change<RadioGroupHandle>>(changes[2]);
    const auto& region = std::get<Change<DropdownHandle>>(changes[3]);
    const auto& volume = std::get<Change<SliderHandle>>(changes[4]);
    std::vector<MixedFieldError> errors;
    if (blank(name.value)) errors.push_back({name.field, required_name});
    if (!region.value || (region.value->id != "north" && region.value->id != "central" && region.value->id != "south")) errors.push_back({region.field, required_region});
    if (!errors.empty()) return {{Status::invalid_value, u"\u8acb\u4fee\u6b63\u6b04\u4f4d"}, std::move(errors)};
    if (next_field_error_) {
        auto error = std::move(*next_field_error_); next_field_error_.reset();
        return {{Status::invalid_value, u"Fixture rejected apply"}, {std::move(error)}};
    }
    if (reject_notices_conflict_) {
        reject_notices_conflict_ = false;
        handles.notices.toggle();
        notices_applied_ = notices.value;
        if (notices_changed_) notices_changed_();
        refresh_conflict();
        return {{Status::invalid_value, u"Source changed during apply"}, {}};
    }
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
void Form::update_mode_source(OptionValue value) {
    mode_applied_ = std::move(value); if (mode_changed_) mode_changed_(); refresh_conflict();
}
void Form::update_region_source(OptionValue value) {
    region_applied_ = std::move(value); if (region_changed_) region_changed_(); refresh_conflict();
}
void Form::update_progress(int value) {
    progress_ = value;
    if (progress_changed_) progress_changed_();
}
void Form::refresh_conflict() {
    handles.notices_latest.enable(mode_ != Mode::disabled && handles.notices.describe()->source_conflict);
    handles.mode_latest.enable(mode_ != Mode::disabled && handles.mode.describe()->source_conflict);
    handles.region_latest.enable(mode_ != Mode::disabled && handles.region.describe()->source_conflict);
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
    std::vector<Node> fields{control(handles.name)};
    fields.push_back(control(handles.notices)); fields.push_back(control(handles.notices_latest));
    fields.push_back(control(handles.mode)); fields.push_back(control(handles.mode_latest));
    fields.push_back(control(handles.region)); fields.push_back(control(handles.region_latest));
    fields.push_back(control(handles.volume)); fields.push_back(control(handles.volume_latest));
    fields.push_back(row({control(handles.apply), control(handles.latest), control(handles.state)}, 16));
    auto form = layout::scroll(column({
        control(handles.title, 24),
        control(handles.intro),
        grid({control(handles.normal), control(handles.read_only), control(handles.disabled), control(handles.error), control(handles.long_text), control(handles.offline_disabled), control(handles.region_empty), control(handles.reset)}, 120, 8),
        grid({column(std::move(fields), 12),
              column({control(handles.draft), control(handles.result), control(handles.count)}, 8)},
             360, 32),
    }, 16, 24));
    if (!pages_) return form;
    auto active = *handles.tabs.active();
    layout::Node content;
    if (active.id == "v01") content = std::move(form);
    else if (active.id == "v02") content = list->page();
    else if (active.id == "v04") {
        std::vector<Node> items;
        bool clipping_fixture = clip_padding_id_ == handles.v04_title.describe()->id;
        if (clipping_fixture) items.push_back(column({}, 0, 700));
        items.push_back(control(handles.v04_title, 24));
        items.push_back(control(handles.font));
        items.push_back(control(handles.text_size));
        items.push_back(control(handles.dark));
        items.push_back(control(handles.compact));
        items.push_back(control(handles.preview));
        items.push_back(control(handles.font_status));
        items.push_back(control(handles.reset_v04));
        // The alignment comparison: the paragraph with its two dropdowns, then "姓　名" distributed next to the same label left-aligned.
        items.push_back(control(handles.justify_sample));
        items.push_back(grid({control(handles.justify_align), control(handles.justify_method)}, 200, 16));
        items.push_back(grid({control(handles.distribute_name), control(handles.distribute_plain)}, 120, 16));
        if (clipping_fixture) items.push_back(column({}, 0, 700));
        content = layout::scroll(column(std::move(items), 16, 24));
    }

    else content = layout::scroll(column({control(handles.v03_title, 24), control(handles.progress), control(handles.progress_source), control(handles.dialog_result), control(handles.dialog_open), control(handles.dialog_temporary),
        grid({control(handles.progress_zero), control(handles.progress_half), control(handles.progress_full),
              control(handles.progress_below), control(handles.progress_above), control(handles.reset_v03)}, 120, 8)}, 16, 24));
    return modal(pinned(control(handles.tabs), std::move(content)),
        column({control(handles.dialog_title, 20), control(handles.dialog_body), row({control(handles.dialog_cancel), control(handles.dialog_confirm)})}, 16), handles.dialog);
}
}
