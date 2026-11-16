#include "settings_demo.hpp"
#include "lcars/version.hpp"
#include <algorithm>
#include <cstdio>
#include "utf.hpp"
#include "core_internal.hpp"
using namespace lcars;
namespace settings_demo {
namespace {
auto subscribe_to(std::function<void()>& slot) {
    return [&slot](std::function<void()> changed) {
        slot = std::move(changed);
        return [&slot] { slot = {}; };
    };
}
std::u16string fold_ascii(std::u16string value) {
    for (auto& c : value) if (c >= u'A' && c <= u'Z') c += u'a' - u'A';
    return value;
}
}
const char* focus_mode_id(FocusIndicatorMode mode) {
    return mode == FocusIndicatorMode::never ? "never" : mode == FocusIndicatorMode::always ? "always" : "automatic";
}
std::optional<FocusIndicatorMode> focus_mode_from_id(std::string_view id) {
    for (auto mode : {FocusIndicatorMode::automatic, FocusIndicatorMode::always, FocusIndicatorMode::never}) if (id == focus_mode_id(mode)) return mode;
    return std::nullopt;
}
Demo::Demo(AboutInfo about) : about_(std::move(about)) {
    nav = ui.addNavigationList({.id="app.nav", .label=u"設定頁面"},
        {{{"general"},u"一般",true,{}},{{"items"},u"項目清單",true,{}},{{"appearance"},u"外觀與文字",true,{}},{{"about"},u"關於",true,{}}}, {"general"}).value.value();
    title_ = ui.addLabel({.id="app.page_title"}).value.value();
    name_label_ = ui.addLabel({.id="general.name.label",.value=u"顯示名稱",.target="general.name",.labelKind=LabelKind::name}).value.value();
    name = ui.addTextField({.id="general.name",.value=name_value_,.maxLength=64}).value.value();
    name_help_ = ui.addLabel({.id="general.name.help",.value=u"單行文字，可輸入繁體中文或英文。",.target="general.name",.labelKind=LabelKind::description}).value.value();
    name_error_ = ui.addLabel({.id="general.name.error",.target="general.name",.labelKind=LabelKind::error}).value.value();
    notices = ui.addCheckbox({.id="general.notices",.label=u"啟用提示"}).value.value();
    mode = ui.addRadioGroup({.id="general.mode",.label=u"模式"}, {{{"normal"},u"一般"},{{"focus"},u"專注"},{{"offline"},u"離線"}}).value.value();
    region = ui.addDropdown({.id="general.region",.label=u"地區"}, {{{"north"},u"北區"},{{"central"},u"中區"},{{"south"},u"南區"}}).value.value();
    volume = ui.addSlider({.id="general.volume",.label=u"音量"},{0,100,5}).value.value();
    items_search = ui.addTextField({.id="items.search",.label=u"\u641c\u5c0b\u540d\u7a31"}).value.value();
    items_list = ui.addList({.id="items.list",.label=u"\u9805\u76ee"}, {}).value.value();
    items_count_ = ui.addLabel({.id="items.count"}).value.value();
    items_add = ui.addButton({.id="items.add",.label=u"\u65b0\u589e\u2026"}).value.value();
    items_edit = ui.addButton({.id="items.edit",.label=u"\u7de8\u8f2f\u2026",.enabled=false}).value.value();
    items_delete = ui.addButton({.id="items.delete",.label=u"\u522a\u9664\u2026",.enabled=false}).value.value();
    // Right-clicking a row selects it, then offers the same Edit and Delete as the buttons and the
    // Enter / Del shortcuts: each item runs the button's own action.
    items_menu = ui.addContextMenu(items_list, {
        {.id="items.edit", .label=u"\u7de8\u8f2f(E)", .enabled=false, .accessKey=u'E', .shortcutHint=u"Enter", .action=[this](const MenuContext&) { return items_edit.invoke(); }},
        {.id="items.delete", .label=u"\u522a\u9664(D)", .enabled=false, .accessKey=u'D', .shortcutHint=u"Del", .action=[this](const MenuContext&) { return items_delete.invoke(); }},
    }).value.value();
    font = ui.addDropdown({.id="appearance.font",.label=u"\u5b57\u578b"}, {{{"default"},u"\u9810\u8a2d\uff08Segoe UI\uff0b\u5fae\u8edf\u6b63\u9ed1\u9ad4\uff09"}}).value.value();
    font_status_ = ui.addLabel({.id="appearance.font.status"}).value.value();
    nav.addMember({"appearance"}, font); nav.addMember({"appearance"}, font_status_);
    refresh_fonts();
    font.connectSource({[this]() -> std::optional<OptionValue> { auto value=ui.presentation().fontFamily; return OptionValue{value.empty() ? "default" : "family:" + lcars::detail::utf8_from_utf16(value)}; }, subscribe_to(font_changed_)});
    font.connectImmediate([this](const Change<DropdownHandle>& c) { auto settings=ui.presentation(); settings.fontFamily = !c.value || c.value->id == "default" ? u"" : lcars::detail::utf16_from_utf8(c.value->id.substr(7)); return setPresentation(settings); });
    textSize = ui.addSlider({.id="appearance.text_size",.label=u"\u6587\u5b57\u5927\u5c0f"},{12,28,1}).value.value();
    nav.addMember({"appearance"}, textSize);
    textSize.connectSource({[this] { return ui.presentation().textSize; }, subscribe_to(text_size_changed_)});
    connect_slider(textSize, "appearance.text_size", [this](const Change<SliderHandle>& change, bool join) {
        auto settings = ui.presentation(); settings.textSize = change.value;
        return setPresentation(settings, join);
    });
    dark = ui.addCheckbox({.id="appearance.dark",.label=u"\u6df1\u8272"}).value.value();
    compact = ui.addCheckbox({.id="appearance.compact",.label=u"\u7dca\u6e4a"}).value.value();
    // WCAG 1.4.8: the user can turn justified text off for the whole window.
    justify_left = ui.addCheckbox({.id="appearance.justify_left",.label=u"左右對齊文字改為靠左"}).value.value();
    accent = ui.addDropdown({.id="appearance.accent",.label=u"強調色"}, {{{"default"},u"預設"},{{"green"},u"綠色"},{{"orange"},u"橘色"}}).value.value();
    nav.addMember({"appearance"}, accent);
    accent.connectSource({[this]() -> std::optional<OptionValue> { return OptionValue{accent_value_}; }, subscribe_to(accent_changed_)});
    accent.connectImmediate([this](const Change<DropdownHandle>& c) { return set_accent(c.value ? c.value->id : "default"); });
    // The focus ring (#90): one dropdown for when it shows, a thickness slider, a radius (same as the component, or a slider) and a color.
    focus_mode = ui.addDropdown({.id="appearance.focus_mode",.label=u"焦點標示"}, {{{"automatic"},u"自動"},{{"always"},u"一律顯示"},{{"never"},u"關閉"}}).value.value();
    focus_width = ui.addSlider({.id="appearance.focus_width",.label=u"焦點外框粗細"},{minFocusRingWidth,maxFocusRingWidth,1}).value.value();
    focus_radius_same = ui.addCheckbox({.id="appearance.focus_radius_same",.label=u"焦點外框圓角跟元件一樣"}).value.value();
    focus_radius = ui.addSlider({.id="appearance.focus_radius",.label=u"焦點外框圓角"},{minFocusRingRadius,maxFocusRingRadius,1}).value.value();
    focusColor = ui.addDropdown({.id="appearance.focus_color",.label=u"焦點外框顏色"}, {{{"default"},u"預設"},{{"green"},u"綠色"},{{"orange"},u"橘色"}}).value.value();
    for (ComponentHandle member : {ComponentHandle{focus_mode}, ComponentHandle{focus_width}, ComponentHandle{focus_radius_same}, ComponentHandle{focus_radius}, ComponentHandle{focusColor}}) nav.addMember({"appearance"}, member);
    focus_mode.connectSource({[this]() -> std::optional<OptionValue> {
        return OptionValue{focus_mode_id(ui.presentation().focusIndicator)};
    }, subscribe_to(focus_mode_changed_)});
    focus_mode.connectImmediate([this](const Change<DropdownHandle>& c) {
        auto settings = ui.presentation();
        settings.focusIndicator = (c.value ? focus_mode_from_id(c.value->id) : std::nullopt).value_or(FocusIndicatorMode::automatic);
        return setPresentation(settings);
    });
    // 3 DIP is the appearances' own ring thickness (ComponentPresentation::focus): what the slider shows while the setting is empty.
    focus_width.connectSource({[this] { return ui.presentation().focusRingWidth.value_or(3); }, subscribe_to(focus_width_changed_)});
    connect_slider(focus_width, "appearance.focus_width", [this](const Change<SliderHandle>& change, bool join) {
        auto settings = ui.presentation(); settings.focusRingWidth = change.value;
        return setPresentation(settings, join);
    });
    focus_radius_same.connectSource({[this] { return !ui.presentation().focusRingRadius.has_value(); }, subscribe_to(focus_radius_same_changed_)});
    focus_radius_same.connectImmediate([this](const Change<CheckboxHandle>& c) {
        auto settings = ui.presentation();
        settings.focusRingRadius = c.value ? std::nullopt : std::optional<int>{focus_radius_start()};
        return setPresentation(settings);
    });
    focus_radius.connectSource({[this] { return ui.presentation().focusRingRadius.value_or(focus_radius_start()); }, subscribe_to(focus_radius_changed_)});
    connect_slider(focus_radius, "appearance.focus_radius", [this](const Change<SliderHandle>& change, bool join) {
        auto settings = ui.presentation(); settings.focusRingRadius = change.value; focus_radius_memory_ = change.value;
        return setPresentation(settings, join);
    });
    focusColor.connectSource({[this]() -> std::optional<OptionValue> { return OptionValue{focus_color_value_}; }, subscribe_to(focus_color_changed_)});
    focusColor.connectImmediate([this](const Change<DropdownHandle>& c) { return set_focus_color(c.value ? c.value->id : "default"); });
    refresh_focus_controls();
    preview_ = ui.addLabel({.id="appearance.preview",.value=u"\u9810\u89bd\uff1a\u7e41\u4e2d English \U0001f600 \U00020000",.textAlign=TextAlign::center}).value.value();
    reset_appearance_ = ui.addButton({.id="appearance.reset",.label=u"\u9084\u539f\u9810\u8a2d\u5916\u89c0"}).value.value();
    for (ComponentHandle member : {ComponentHandle{dark}, ComponentHandle{compact}, ComponentHandle{justify_left}, ComponentHandle{preview_}, ComponentHandle{reset_appearance_}}) nav.addMember({"appearance"}, member);
    justify_left.connectSource({[this] { return ui.presentation().justifyAsLeft; }, subscribe_to(justify_left_changed_)});
    justify_left.connectImmediate([this](const Change<CheckboxHandle>& c) { auto settings=ui.presentation(); settings.justifyAsLeft=c.value; return setPresentation(settings); });
    dark.connectSource({[this] { return ui.presentation().variant == ThemeVariant::dark(); }, subscribe_to(dark_changed_)});
    compact.connectSource({[this] { return ui.presentation().density == Density::compact; }, subscribe_to(compact_changed_)});
    dark.connectImmediate([this](const Change<CheckboxHandle>& c) { auto settings=ui.presentation(); settings.variant=c.value ? ThemeVariant::dark() : ThemeVariant::light(); return setPresentation(settings); });
    compact.connectImmediate([this](const Change<CheckboxHandle>& c) { auto settings=ui.presentation(); settings.density=c.value ? Density::compact : Density::standard; return setPresentation(settings); });
    reset_appearance_.bind([this] {
        if (auto r = apply_palette("default", "default"); !r) return r;
        accent_value_ = focus_color_value_ = "default"; if (accent_changed_) accent_changed_(); if (focus_color_changed_) focus_color_changed_();
        return setPresentation(PresentationSettings{});
    });
    for (const char* id : {"about.app", "about.framework", "about.build", "about.appearance", "about.renderer", "about.diagnostics", "about.settings_path"}) {
        about_labels_.push_back(ui.addLabel({.id=id}).value.value());
        nav.addMember({"about"}, about_labels_.back());
    }
    about_labels_[0].connectSource([] { return u"LcarsUI 設定管理示範 0.1.0"; });
    about_labels_[1].connectSource([] { return u"LcarsUI 框架 " + std::u16string(lcars::frameworkVersion); });
    about_labels_[2].connectSource([this] { return u"建置：" + about_.build; });
    about_labels_[3].connectSource([this] { return u"外觀：" + about_.appearance; });
    about_labels_[4].connectSource([this] { return u"繪製：" + about_.renderer; });
    about_labels_[5].connectSource([this] { return about_.diagnostics ? u"診斷：基本診斷（只存記憶體）" : u"診斷：未選用"; });
    about_labels_[6].connectSource([this] { return about_.settings_path; });
    // A label in vertical writing mode: columns run top to bottom, right to left (Taiwan rules).
    about_vertical_ = ui.addLabel({.id="about.vertical",.value=u"LcarsUI 設定管理示範（第 12 版），謹此致謝。",.writingMode=WritingMode::vertical}).value.value();
    nav.addMember({"about"}, about_vertical_);
    std::vector<OptionItem> license_options;
    for (const auto& license : about_.licenses) license_options.push_back({license.id, license.name});
    licenses = ui.addList({.id="about.licenses",.label=u"授權"}, std::move(license_options)).value.value();
    license_text_ = ui.addLabel({.id="about.license_text",.lineSpacing=1.4,.paragraphSpacing=8}).value.value();
    licenses.connectSource({[this] { return license_selection_; }, subscribe_to(license_selection_changed_)});
    licenses.connectImmediate([this](const Change<ListHandle>& change) { license_selection_ = change.value; license_scroll.offset = 0; if (license_selection_changed_) license_selection_changed_(); return Result{}; });
    license_text_.connectSource([this] { for (const auto& license : about_.licenses) if (license_selection_ && license.id == *license_selection_) return license.text; return std::u16string{}; });
    nav.addMember({"about"}, licenses);
    nav.addMember({"about"}, license_text_);
    for (int n=1; n<=20; ++n) {
        char id[8], number[8]; std::snprintf(id,sizeof(id),"T%03d",n); std::snprintf(number,sizeof(number),"%02d",n);
        items_.push_back({{id}, n==5 || n==6 ? u"\u5171\u7528\u7bc4\u672c" : u"\u7bc4\u672c " + std::u16string(number,number+2), {}});
    }
    items_search.connectSource({[this] { return search_value_; }, subscribe_to(search_changed_)});
    items_search.connectImmediate([this](const Change<TextFieldHandle>& c) {
        search_value_=c.value;
        if (auto r=refresh_items(); !r) return r;
        if (search_changed_) search_changed_();
        return Result{};
    });
    items_list.connectSource({[this] { return selection_; }, subscribe_to(selection_changed_)});
    items_list.connectImmediate([this](const Change<ListHandle>& change) { selection_=change.value; items_edit.enable(selection_.has_value()); items_delete.enable(selection_.has_value()); items_menu.enableItem("items.edit", selection_.has_value()); items_menu.enableItem("items.delete", selection_.has_value()); if (selection_changed_) selection_changed_(); return Result{}; });
    for (ComponentHandle member : {ComponentHandle{items_search}, ComponentHandle{items_list}, ComponentHandle{items_count_}, ComponentHandle{items_add}, ComponentHandle{items_edit}, ComponentHandle{items_delete}}) nav.addMember({"items"}, member);
    refresh_items();
    status_ = ui.addLabel({.id="app.status",.value=u"使用預設值，尚未建立設定檔"}).value.value();
    undo_ = ui.addButton({.id="app.undo",.label=u"復原",.enabled=false}).value.value();
    redo_ = ui.addButton({.id="app.redo",.label=u"重做",.enabled=false}).value.value();
    revert_ = ui.addButton({.id="app.revert",.label=u"還原為已儲存",.enabled=false}).value.value();
    save_ = ui.addButton({.id="app.save",.label=u"儲存",.enabled=false}).value.value();
    for (ComponentHandle member : {ComponentHandle{name_label_}, ComponentHandle{name}, ComponentHandle{name_help_}, ComponentHandle{name_error_}, ComponentHandle{notices}, ComponentHandle{mode}, ComponentHandle{region}, ComponentHandle{volume}}) nav.addMember({"general"}, member);
    title_.connectSource([this] { auto id=nav.selected()->id; return id=="general"?u"一般":id=="items"?u"項目清單":id=="appearance"?u"外觀與文字":u"關於"; });
    name.connectSource({[this] { return name_value_; }, subscribe_to(name_changed_)});
    name.connectConfirmed([this](const Change<TextFieldHandle>& change) {
        if (std::all_of(change.value.begin(), change.value.end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'　'; })) { name.setError(u"請填寫顯示名稱"); return Result{Status::invalidValue,u"請填寫顯示名稱"}; }
        name.setError(u""); if (name_value_ != change.value) { name_value_=change.value; if (name_changed_) name_changed_(); dirty(); } return Result{};
    });
    name.onBlur([this] {
        auto draft = name.draft(); if (!draft) return;
        if (std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            name.setError(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"); update_history_controls(); return;
        }
        name.setError(u"");
        if (name_value_ != *draft) { name_value_=*draft; if (name_changed_) name_changed_(); dirty(); }
        name.resetToSource(); update_history_controls();
    });
    notices.connectSource({[this] { return notices_value_; }, subscribe_to(notices_changed_)});
    notices.connectImmediate([this](const Change<CheckboxHandle>& c) { notices_value_=c.value; if (notices_changed_) notices_changed_(); dirty(); return Result{}; });
    mode.connectSource({[this] { return mode_value_; }, subscribe_to(mode_changed_)});
    mode.connectImmediate([this](const Change<RadioGroupHandle>& c) { mode_value_=c.value; if (mode_changed_) mode_changed_(); dirty(); return Result{}; });
    region.connectSource({[this] { return region_value_; }, subscribe_to(region_changed_)});
    region.connectImmediate([this](const Change<DropdownHandle>& c) { region_value_=c.value; if (region_changed_) region_changed_(); dirty(); return Result{}; });
    volume.connectSource({[this] { return volume_value_; }, subscribe_to(volume_changed_)});
    connect_slider(volume, "general.volume", [this](const Change<SliderHandle>& c, bool join) { volume_value_=c.value; if (volume_changed_) volume_changed_(); dirty(join); return Result{}; });
    item_title_ = ui.addLabel({.id="items.dialog.title"}).value.value();
    item_name_label_ = ui.addLabel({.id="items.dialog.name.label",.value=u"\u540d\u7a31",.target="items.dialog.name",.labelKind=LabelKind::name}).value.value();
    item_name = ui.addTextField({.id="items.dialog.name",.maxLength=64}).value.value();
    item_name_error_ = ui.addLabel({.id="items.dialog.name.error",.target="items.dialog.name",.labelKind=LabelKind::error}).value.value();
    item_note_label_ = ui.addLabel({.id="items.dialog.note.label",.value=u"\u8aaa\u660e\uff08\u9078\u586b\uff09",.target="items.dialog.note",.labelKind=LabelKind::name}).value.value();
    item_note_bold = ui.addButton({.id="items.dialog.note.bold",.label=u"粗體"}).value.value();
    item_note_italic = ui.addButton({.id="items.dialog.note.italic",.label=u"斜體"}).value.value();
    item_note_underline = ui.addButton({.id="items.dialog.note.underline",.label=u"底線"}).value.value();
    item_note_color = ui.addDropdown({.id="items.dialog.note.color",.label=u"文字顏色"}, {{{"default"},u"預設"},{{"red"},u"紅"},{{"green"},u"綠"},{{"blue"},u"藍"}}).value.value();
    item_note = ui.addTextField({.id="items.dialog.note",.maxLength=200,.multiline=true,.visibleLines=3,.rich=true}).value.value();
    // The note's named colors, one value per theme, each readable on the surface of both appearances (see the settings demo tests).
    item_note.setColors({{"red",{176,0,32},{255,138,148}},{"green",{20,96,48},{120,224,150}},{"blue",{28,60,170},{150,180,255}}});
    item_cancel_ = ui.addButton({.id="items.dialog.cancel",.label=u"\u53d6\u6d88"}).value.value();
    item_ok = ui.addButton({.id="items.dialog.ok",.label=u"\u78ba\u5b9a"}).value.value();
    delete_title_ = ui.addLabel({.id="items.delete_dialog.title",.value=u"\u522a\u9664\u9805\u76ee"}).value.value();
    delete_message_ = ui.addLabel({.id="items.delete_dialog.message"}).value.value();
    delete_cancel_ = ui.addButton({.id="items.delete_dialog.cancel",.label=u"\u53d6\u6d88"}).value.value();
    delete_confirm_ = ui.addButton({.id="items.delete_dialog.confirm",.label=u"\u522a\u9664"}).value.value();
    item_dialog = ui.addDialog("items.dialog", {item_title_,item_name_label_,item_name,item_name_error_,item_note_label_,item_note_bold,item_note_italic,item_note_underline,item_note_color,item_note,item_cancel_,item_ok}, item_name, items_add, [](bool) {}).value.value();
    delete_dialog = ui.addDialog("items.delete_dialog", {delete_title_,delete_message_,delete_cancel_,delete_confirm_}, delete_cancel_, items_add, [](bool) {}).value.value();
    item_name.connectSource({[this] { return item_name_source_; }, subscribe_to(item_name_changed_)});
    item_note.connectRichSource({[this] { return item_note_source_; }, subscribe_to(item_note_changed_)});
    // The style buttons act on the note's selection (or the typing style at a caret); the selection stays and the note takes the focus back.
    auto style_button = [this](lcars::ButtonHandle& button, lcars::StyleAttribute attribute) {
        button.bind([this, attribute] { auto result = item_note.toggleStyle(attribute); item_note.focus(); return result; });
    };
    style_button(item_note_bold, lcars::StyleAttribute::bold); style_button(item_note_italic, lcars::StyleAttribute::italic); style_button(item_note_underline, lcars::StyleAttribute::underline);
    item_note_color.connectSource({[this]() -> std::optional<OptionValue> { return OptionValue{note_color_value_}; }, subscribe_to(note_color_changed_)});
    item_note_color.connectImmediate([this](const Change<DropdownHandle>& c) {
        note_color_value_ = c.value ? c.value->id : "default"; if (note_color_changed_) note_color_changed_();
        auto result = item_note.applyStyle({.color = note_color_value_ == "default" ? std::string{} : note_color_value_});
        item_note.focus();
        return result;
    });
    auto open_item = [this](bool edit) -> Result {
        editing_=edit;
        auto found=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
        if (edit && found==items_.end()) return {Status::invalidValue,u"No selection"};
        item_title_.connectSource([edit] { return edit ? u"\u7de8\u8f2f\u9805\u76ee" : u"\u65b0\u589e\u9805\u76ee"; });
        auto result=item_dialog.open(); if (!result) return result;
        item_name_source_=edit ? found->name : u""; item_note_source_=edit ? lcars::RichText{found->note, found->note_spans} : lcars::RichText{};
        note_color_value_="default"; if (note_color_changed_) note_color_changed_();
        if (item_name_changed_) item_name_changed_(); if (item_note_changed_) item_note_changed_();
        if (auto r = item_name.resetToSource(); !r) return r;
        if (auto r = item_note.resetToSource(); !r) return r;
        return item_name.setError(u"");
    };
    items_add.bind([open_item] { return open_item(false); });
    items_edit.bind([open_item] { return open_item(true); });
    item_cancel_.bind([this] { return item_dialog.settle(false); });
    ui.connectConfirmation(item_ok, {item_name, item_note}, [this](const std::vector<ConfirmFieldChange>& changes) -> ConfirmationResult {
        const auto& value=std::get<Change<TextFieldHandle>>(changes[0]).value; const auto& rich=std::get<Change<TextFieldHandle, RichText>>(changes[1]).value;
        if (std::all_of(value.begin(),value.end(),[](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            return {{Status::invalidValue,u"Name required"}, {{item_name,u"\u8acb\u586b\u5beb\u540d\u7a31"}}};
        }
        OptionValue id;
        bool changed=false;
        if (editing_) {
            auto it=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
            if (it==items_.end()) return {{Status::notFound,u"Item not found"}, {}};
            id=it->id; changed=it->name!=value || it->note!=rich.text || it->note_spans!=rich.spans; it->name=value; it->note=rich.text; it->note_spans=rich.spans;
        } else {
            char buffer[16]; std::snprintf(buffer,sizeof(buffer),"T%03d",next_item_id_++); id={buffer}; items_.push_back({id,value,rich.text,rich.spans}); changed=true;
        }
        auto result=item_dialog.settle(true); if (!result) return {result, {}};
        if (fold_ascii(value).find(fold_ascii(search_value_))==std::u16string::npos) {
            if (auto r=items_search.setValue(u""); !r) return {r, {}};
        }
        if (auto r=refresh_items(); !r) return {r, {}};
        if (auto r=items_list.select(id); !r) return {r, {}};
        if (changed) dirty(); return {Result{}, {}};
    });
    items_delete.bind([this] {
        auto it=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
        if (it==items_.end()) return Result{Status::invalidValue,u"No selection"};
        auto message=u"\u8981\u522a\u9664\u300c" + it->name + u"\u300d\u55ce\uff1f\u522a\u9664\u5f8c\u53ef\u4ee5\u6309\u300c\u5fa9\u539f\u300d\u627e\u56de\u3002";
        delete_message_.connectSource([message] { return message; }); return delete_dialog.open();
    });
    delete_cancel_.bind([this] { return delete_dialog.settle(false); });
    delete_confirm_.bind([this] {
        auto selected=selection_; auto result=delete_dialog.settle(true); if (!result) return result;
        std::erase_if(items_,[&](const Item& item) { return selected && item.id==*selected; });
        if (auto r=refresh_items(); !r) return r;
        dirty(); return Result{};
    });
    close_title_ = ui.addLabel({.id="app.close_dialog.title",.value=u"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f"}).value.value();
    close_message_ = ui.addLabel({.id="app.close_dialog.message",.value=u"\u6709\u5c1a\u672a\u5132\u5b58\u7684\u4fee\u6539\u3002\u95dc\u9589\u524d\u8981\u5148\u5132\u5b58\u55ce\uff1f"}).value.value();
    close_save = ui.addButton({.id="app.close_dialog.save",.label=u"\u5132\u5b58"}).value.value();
    close_discard = ui.addButton({.id="app.close_dialog.discard",.label=u"\u4e0d\u5132\u5b58"}).value.value();
    close_cancel = ui.addButton({.id="app.close_dialog.cancel",.label=u"\u53d6\u6d88"}).value.value();
    close_dialog = ui.addDialog("app.close_dialog", {close_title_,close_message_,close_save,close_discard,close_cancel}, close_save, save_, [](bool) {}).value.value();
    close_cancel.bind([this] { return close_dialog.settle(false); });
    close_discard.bind([this] { auto result=close_dialog.settle(true); if (result && close_handler_) close_handler_(); return result; });
    close_save.bind([this] { auto result=close_dialog.settle(true); if (!result) return result; result=save_document(); if (result && close_handler_) close_handler_(); return result; });
    load_title_ = ui.addLabel({.id="app.load_dialog.title"}).value.value();
    load_message_ = ui.addLabel({.id="app.load_dialog.message"}).value.value();
    load_ok_ = ui.addButton({.id="app.load_dialog.ok",.label=u"確定"}).value.value();
    load_dialog_ = ui.addDialog("app.load_dialog", {load_title_,load_message_,load_ok_}, load_ok_, save_, [this](bool) { nav.focus(); }).value.value();
    load_ok_.bind([this] { return load_dialog_.settle(true); });
    error_title_ = ui.addLabel({.id="app.error_dialog.title",.value=u"無法儲存"}).value.value();
    error_message_ = ui.addLabel({.id="app.error_dialog.message"}).value.value();
    error_ok_ = ui.addButton({.id="app.error_dialog.ok",.label=u"確定"}).value.value();
    error_dialog_ = ui.addDialog("app.error_dialog", {error_title_,error_message_,error_ok_}, error_ok_, save_, [](bool) {}).value.value();
    error_ok_.bind([this] { return error_dialog_.settle(true); });
    ui.addShortcut({.key=Key::enter},items_edit,items_list);
    ui.addShortcut({.key=Key::del},items_delete,items_list);
    ui.addShortcut({.letter=u'S',.ctrl=true},save_);
    ui.addShortcut({.letter=u'Z',.ctrl=true},undo_);
    ui.addShortcut({.letter=u'Y',.ctrl=true},redo_);
    ui.addShortcut({.letter=u'Z',.ctrl=true,.shift=true},redo_);
    current_ = snapshot(); saved_ = current_;
    undo_.bind([this] {
        auto description = name.describe();
        if (description && !description->error.empty()) {
            auto result=name.resetToSource(); if (!result) return result;
            name.setError(u""); update_history_controls(); return Result{};
        }
        if (undo_history_.empty()) return Result{Status::emptyHistory, {}};
        redo_history_.push_back(snapshot());
        auto previous = undo_history_.back(); undo_history_.pop_back();
        return apply(previous);
    });
    redo_.bind([this] {
        if (redo_history_.empty()) return Result{Status::emptyHistory, {}};
        undo_history_.push_back(snapshot());
        auto next = redo_history_.back(); redo_history_.pop_back();
        return apply(next);
    });
    save_.bind([this] { return save_document(); });
    revert_.bind([this] {
        name.resetToSource();
        Result applied;
        if (saved_ && snapshot() != *saved_) { auto target=*saved_; auto before=snapshot(); applied=apply(target); current_=before; dirty(); }
        update_history_controls(); return applied;
    });
    ui.onChange([this] { update_history_controls(); });
    nav.focus();
}
Result Demo::refresh_items() {
    auto query = fold_ascii(search_value_);
    std::vector<OptionItem> visible;
    for (const auto& item : items_) if (fold_ascii(item.name).find(query) != std::u16string::npos) visible.push_back({item.id,item.name});
    auto count = visible.size();
    auto old_count = items_count_.describe();
    if (!old_count) return {old_count.status, old_count.message};
    auto number = [](std::size_t n) { auto str=std::to_string(n); return std::u16string(str.begin(),str.end()); };
    auto text = u"\u5171 " + number(items_.size()) + u" \u7b46\uff0c\u986f\u793a " + number(count) + u" \u7b46";
    if (items_.size() >= 1000) text += u"\uff08\u5df2\u9054\u4e0a\u9650\uff09";
    if (auto r=items_count_.connectSource([text] { return text; }); !r) return r;
    if (auto r=items_list.setItems(std::move(visible)); !r) {
        auto previous = old_count->value;
        items_count_.connectSource([previous] { return previous; });
        return r;
    }
    return items_add.enable(items_.size()<1000);
}
void Demo::connect_slider(SliderHandle& slider, const char* id, std::function<Result(const Change<SliderHandle>&, bool join)> change) {
    slider.connectImmediate([this, &slider, id, change = std::move(change)](const Change<SliderHandle>& c) {
        const bool focused = slider.describe()->focused;
        auto result = change(c, focused && drag_group_ == id); if (!result) return result;
        drag_group_ = focused ? id : "";
        return Result{};
    });
    slider.onBlur([this, id] { if (drag_group_ == id) drag_group_.clear(); });
}
int Demo::focus_radius_start() const {
    if (focus_radius_memory_) return *focus_radius_memory_;
    const auto* appearance = lcars::detail::access(ui).bound_appearance();
    return appearance ? appearance->radius() : 6;
}
void Demo::refresh_fonts() {
    std::vector<OptionItem> options{{{"default"},u"\u9810\u8a2d\uff08Segoe UI\uff0b\u5fae\u8edf\u6b63\u9ed1\u9ad4\uff09"}};
    auto key = ui.presentation().fontFamily;
    bool found = key.empty();
    for (const auto& family : about_.fonts) {
        options.push_back({{"family:" + lcars::detail::utf8_from_utf16(family.key)}, family.displayName});
        found = found || family.key == key;
    }
    if (!found) options.push_back({{"family:" + lcars::detail::utf8_from_utf16(key)}, key + u"\uff08\u672a\u5b89\u88dd\uff09"});
    font.setItems(std::move(options));
    auto resolution = about_.resolve_font ? about_.resolve_font(ui.presentation()) : FontResolution{key, key.empty() ? u"Segoe UI" : key, found, {}};
    auto status = resolution.requestedAvailable ? u"\u76ee\u524d\u5b57\u578b\uff1a" + resolution.primary : u"\u627e\u4e0d\u5230\u300c" + key + u"\u300d\u5b57\u578b\uff0c\u66ab\u6642\u6539\u7528\u9810\u8a2d\u5b57\u578b\u3002\u8a2d\u5b9a\u4ecd\u6703\u4fdd\u7559\u3002";
    font_status_.connectSource([status] { return status; });
}
namespace {
struct AccentColors { Color accent, onAccent, selection; };
struct AccentRow { const char* appearance; const char* variant; AccentColors green, orange; };
// Every row passes checkPaletteContrast on top of that appearance's built-in palette. The ring color choices reuse
// the accent of the same row: those colors are already 3:1 on every surface (the focus rules in checkPaletteContrast).
const AccentRow accent_rows[] = {
    {"Minimal Web", "light", {{20,110,50}, {255,255,255}, {205,235,210}}, {{170,70,0}, {255,255,255}, {255,225,195}}},
    {"Minimal Web", "dark", {{110,200,130}, {8,40,16}, {36,92,52}}, {{255,170,90}, {50,24,0}, {120,66,20}}},
    {"Material-inspired", "light", {{28,106,60}, {255,255,255}, {200,235,205}}, {{160,70,0}, {255,255,255}, {255,220,190}}},
    {"Material-inspired", "dark", {{150,220,160}, {10,50,20}, {40,95,55}}, {{255,184,120}, {70,30,0}, {110,62,20}}},
};
}
namespace {
// One entry per role for every linked built-in appearance and variant: the colors of the chosen row ("green" or "orange"), none for "default".
std::vector<PaletteOverride> row_overrides(const std::string& id, std::vector<std::pair<const char*, Color>> (*roles)(const AccentColors&)) {
    std::vector<PaletteOverride> list;
    if (id != "green" && id != "orange") return list;
    for (const auto& row : accent_rows) {
        if (!builtinAppearance(row.appearance)) continue;
        for (const auto& [role, color] : roles(id == "green" ? row.green : row.orange)) list.push_back({row.appearance, ThemeVariant{row.variant}, role, color});
    }
    return list;
}
}
std::vector<PaletteOverride> Demo::focus_color_overrides(const std::string& id) {
    return row_overrides(id, [](const AccentColors& c) { return std::vector<std::pair<const char*, Color>>{{"focus", c.accent}}; });
}
std::vector<PaletteOverride> Demo::accent_overrides(const std::string& id) {
    return row_overrides(id, [](const AccentColors& c) { return std::vector<std::pair<const char*, Color>>{{"accent", c.accent}, {"on_accent", c.onAccent}, {"selection", c.selection}}; });
}
Result Demo::apply_palette(const std::string& accent_id, const std::string& focus_id) {
    auto list = accent_overrides(accent_id), focus = focus_color_overrides(focus_id);
    list.insert(list.end(), focus.begin(), focus.end());
    return ui.setPaletteOverrides(std::move(list));
}
Result Demo::apply_accent(const std::string& id) {
    auto result = apply_palette(id, focus_color_value_);
    if (result) { accent_value_ = id; if (accent_changed_) accent_changed_(); }
    return result;
}
Result Demo::apply_focus_color(const std::string& id) {
    auto result = apply_palette(accent_value_, id);
    if (result) { focus_color_value_ = id; if (focus_color_changed_) focus_color_changed_(); }
    return result;
}
Result Demo::set_accent(const std::string& id) {
    auto result = apply_accent(id);
    if (result) dirty();
    return result;
}
Result Demo::set_focus_color(const std::string& id) {
    auto result = apply_focus_color(id);
    if (result) dirty();
    return result;
}
Result Demo::presentation_changed() {
    refresh_fonts();
    for (auto* slot : {&font_changed_, &text_size_changed_, &dark_changed_, &compact_changed_, &justify_left_changed_,
                       &focus_mode_changed_, &focus_width_changed_, &focus_radius_same_changed_, &focus_radius_changed_})
        if (*slot) (*slot)();
    return refresh_focus_controls();
}
Result Demo::refresh_focus_controls() {
    // The indicator off leaves nothing to style: the three controls are disabled but keep their values for when it comes back.
    const bool styling = ui.presentation().focusIndicator != FocusIndicatorMode::never;
    for (auto result : {focus_width.enable(styling), focus_radius_same.enable(styling), focus_radius.enable(styling), focusColor.enable(styling)})
        if (!result) return result;
    return {};
}
Result Demo::setPresentation(const PresentationSettings& settings, bool coalesce) {
    auto result=ui.setPresentation(settings); if (!result) return result;
    auto refreshed = presentation_changed(); dirty(coalesce); return refreshed;
}
void Demo::on_dirty_changed(std::function<void(bool)> callback) { dirty_changed_ = std::move(callback); if (dirty_changed_) dirty_changed_(dirty_state_); }
SettingsDocument Demo::snapshot() const {
    return {name_value_, notices_value_, mode_value_, region_value_, volume_value_, items_, next_item_id_, ui.presentation(), accent_value_, focus_color_value_};
}
void Demo::update_history_controls() {
    auto description = name.describe();
    undo_.enable(!undo_history_.empty() || (description && !description->error.empty())); redo_.enable(!redo_history_.empty());
    auto draft = name.draft();
    bool changed = saved_ && (snapshot() != *saved_ || (draft && *draft != name_value_));
    save_.enable(changed); revert_.enable(changed);
    if (changed != dirty_state_) { dirty_state_=changed; if (dirty_changed_) dirty_changed_(changed); }
    status_.connectSource([changed, this] { return changed && save_failed_ ? u"有未儲存的修改（上次儲存失敗）" : changed ? u"\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539" : saved_once_ ? u"\u6240\u6709\u4fee\u6539\u5df2\u5132\u5b58" : u"\u4f7f\u7528\u9810\u8a2d\u503c\uff0c\u5c1a\u672a\u5efa\u7acb\u8a2d\u5b9a\u6a94"; });
}
void Demo::dirty(bool coalesce) {
    if (!coalesce) drag_group_.clear();
    auto now = snapshot();
    if (current_ && *current_ != now) {
        if (!coalesce) undo_history_.push_back(*current_);
        if (undo_history_.size() > 100) undo_history_.erase(undo_history_.begin());
        redo_history_.clear();
    }
    current_ = std::move(now); update_history_controls();
}
Result Demo::apply(const SettingsDocument& value) {
    Result outcome;  // the first part that could not be applied; the others are still applied
    auto note = [&outcome](Result result) { if (!result && outcome) outcome = std::move(result); };
    drag_group_.clear();
    name_value_=value.name; notices_value_=value.notices; mode_value_=value.mode; region_value_=value.region;
    volume_value_=value.volume; items_=value.items; next_item_id_=value.next_item_id;
    if (name_changed_) name_changed_(); if (notices_changed_) notices_changed_();
    if (mode_changed_) mode_changed_(); if (region_changed_) region_changed_(); if (volume_changed_) volume_changed_();
    auto palette = apply_palette(value.accent, value.focusColor);
    if (palette) {
        accent_value_=value.accent; focus_color_value_=value.focusColor;
        if (accent_changed_) accent_changed_(); if (focus_color_changed_) focus_color_changed_();
    }
    note(std::move(palette));
    note(ui.setPresentation(value.presentation)); if (value.presentation.focusRingRadius) focus_radius_memory_=*value.presentation.focusRingRadius;
    note(presentation_changed()); note(refresh_items()); current_=value; update_history_controls();
    return outcome;
}
Result Demo::load_document(const SettingsDocument& value) {
    auto result = apply(value); saved_=value; current_=value; saved_once_=true; undo_history_.clear(); redo_history_.clear(); update_history_controls();
    if (!result) show_load_notice(u"設定檔沒有完全套用", u"有一部分設定無法套用：" + result.message);
    return result;
}
void Demo::show_load_notice(std::u16string title, std::u16string message) {
    load_title_.connectSource([title] { return title; }); load_message_.connectSource([message] { return message; }); load_dialog_.open();
}
void Demo::set_renderer(std::u16string value) { about_.renderer = std::move(value); }
Result Demo::save_document() {
        auto draft = name.draft();
        if (!draft) return Result{draft.status, draft.message};
        if (std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            nav.select({"general"}); name.setError(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"); name.focus();
            return Result{Status::invalidValue, u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"};
        }
        if (name_value_ != *draft) { name_value_=*draft; if (name_changed_) name_changed_(); dirty(); }
        if (save_handler_) {
            auto error=save_handler_(snapshot());
            if (!error.empty()) {
                save_failed_=true; update_history_controls();
                auto message=u"無法寫入 " + settings_path_ + u"：" + error + u"。修改仍保留，尚未儲存。";
                error_message_.connectSource([message] { return message; }); error_dialog_.open();
                return Result{Status::invalidValue, error};
            }
        }
        save_failed_=false; saved_ = snapshot(); saved_once_ = true; update_history_controls(); return Result{};
}
bool Demo::request_close() {
    if (close_dialog.isOpen().value.value_or(false)) { close_dialog.settle(false); return false; }
    for (auto dialog : {item_dialog, delete_dialog, load_dialog_, error_dialog_})
        if (dialog.isOpen().value.value_or(false)) dialog.settle(false);
    if (!is_dirty()) return true;
    close_dialog.open(); return false;
}
bool Demo::is_dirty() const {
    auto draft = name.draft();
    return dirty_state_ || (draft && *draft != name_value_);
}
bool Demo::blank_name_draft() const {
    auto draft = name.draft();
    return draft && std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; });
}
std::u16string Demo::save_failed_renderer() {
    auto draft = name.draft();
    auto document = snapshot();
    if (draft && !std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) document.name = *draft;
    if (!save_handler_) return {};
    return save_handler_(document);
}
layout::Node Demo::page() const {
    using namespace layout;
    std::vector<Node> content{component(title_,24)};
    if (nav.selected()->id == "general") {
        content.push_back(component(name)); content.push_back(component(notices)); content.push_back(component(mode)); content.push_back(component(region)); content.push_back(component(volume));
    }
    if (nav.selected()->id == "items") {
        content.push_back(component(items_search)); content.push_back(component(items_list)); content.push_back(component(items_count_));
        content.push_back(row({component(items_add),component(items_edit),component(items_delete)},8));
    }
    if (nav.selected()->id == "appearance") { content.push_back(component(font)); content.push_back(component(font_status_)); content.push_back(component(textSize)); content.push_back(component(dark)); content.push_back(component(compact)); content.push_back(component(justify_left)); content.push_back(component(accent));
        content.push_back(component(focus_mode)); content.push_back(component(focus_width)); content.push_back(component(focus_radius_same));
        if (ui.presentation().focusRingRadius) content.push_back(component(focus_radius));  // the slider shows once "same as the component" is unchecked
        content.push_back(component(focusColor)); content.push_back(component(preview_)); content.push_back(component(reset_appearance_)); }
    if (nav.selected()->id == "about") {
        for (auto label : about_labels_) content.push_back(component(label));
        content.push_back(verticalText(about_vertical_, 200));
        content.push_back(component(licenses));
        content.push_back(scrollText(license_text_, license_scroll, 200));
    }
    content.push_back(component(status_));
    content.push_back(row({component(undo_),component(redo_),component(revert_),component(save_)},8));
    auto background=layout::scroll(row({component(nav),column(std::move(content),12,24)},16));
    return layout::modal(std::move(background), {
        {item_dialog,column({component(item_title_,24),component(item_name),row({component(item_note_bold),component(item_note_italic),component(item_note_underline),component(item_note_color)},8),component(item_note),row({component(item_cancel_),component(item_ok)},8)},12)},
        {delete_dialog,column({component(delete_title_,24),component(delete_message_),row({component(delete_cancel_),component(delete_confirm_)},8)},12)},
        {load_dialog_,column({component(load_title_,24),component(load_message_),component(load_ok_)},12)},
        {close_dialog,column({component(close_title_,24),component(close_message_),row({component(close_save),component(close_discard),component(close_cancel)},8)},12)},
        {error_dialog_,column({component(error_title_,24),component(error_message_),component(error_ok_)},12)}
    });
}
}

