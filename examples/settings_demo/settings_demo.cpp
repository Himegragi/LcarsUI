#include "settings_demo.hpp"
#include "isoduo/version.hpp"
#include <algorithm>
#include <cstdio>
#include "utf.hpp"
#include "core_internal.hpp"
using namespace isoduo;
namespace settingsDemo {
namespace {
auto subscribeTo(std::function<void()>& slot) {
    return [&slot](std::function<void()> changed) {
        slot = std::move(changed);
        return [&slot] { slot = {}; };
    };
}
std::u16string foldAscii(std::u16string value) {
    for (auto& c : value) if (c >= u'A' && c <= u'Z') c += u'a' - u'A';
    return value;
}
}
const char* focusModeId(FocusIndicatorMode mode) {
    return mode == FocusIndicatorMode::never ? "never" : mode == FocusIndicatorMode::always ? "always" : "automatic";
}
std::optional<FocusIndicatorMode> focusModeFromId(std::string_view id) {
    for (auto mode : {FocusIndicatorMode::automatic, FocusIndicatorMode::always, FocusIndicatorMode::never}) if (id == focusModeId(mode)) return mode;
    return std::nullopt;
}
Demo::Demo(AboutInfo about) : about_(std::move(about)) {
    nav = ui.addNavigationList({.id="app.nav", .label=u"設定頁面"},
        {{{"general"},u"一般",true,{}},{{"items"},u"項目清單",true,{}},{{"appearance"},u"外觀與文字",true,{}},{{"about"},u"關於",true,{}}}, {"general"}).value.value();
    title_ = ui.addLabel({.id="app.page_title"}).value.value();
    nameLabel_ = ui.addLabel({.id="general.name.label",.value=u"顯示名稱",.target="general.name",.labelKind=LabelKind::name}).value.value();
    name = ui.addTextField({.id="general.name",.value=nameValue_,.maxLength=64}).value.value();
    nameHelp_ = ui.addLabel({.id="general.name.help",.value=u"單行文字，可輸入繁體中文或英文。",.target="general.name",.labelKind=LabelKind::description}).value.value();
    nameError_ = ui.addLabel({.id="general.name.error",.target="general.name",.labelKind=LabelKind::error}).value.value();
    notices = ui.addCheckbox({.id="general.notices",.label=u"啟用提示"}).value.value();
    mode = ui.addRadioGroup({.id="general.mode",.label=u"模式"}, {{{"normal"},u"一般"},{{"focus"},u"專注"},{{"offline"},u"離線"}}).value.value();
    region = ui.addDropdown({.id="general.region",.label=u"地區"}, {{{"north"},u"北區"},{{"central"},u"中區"},{{"south"},u"南區"}}).value.value();
    volume = ui.addSlider({.id="general.volume",.label=u"音量"},{0,100,5}).value.value();
    itemsSearch = ui.addTextField({.id="items.search",.label=u"\u641c\u5c0b\u540d\u7a31"}).value.value();
    itemsList = ui.addList({.id="items.list",.label=u"\u9805\u76ee"}, {}).value.value();
    itemsCount_ = ui.addLabel({.id="items.count"}).value.value();
    itemsAdd = ui.addButton({.id="items.add",.label=u"\u65b0\u589e\u2026"}).value.value();
    itemsEdit = ui.addButton({.id="items.edit",.label=u"\u7de8\u8f2f\u2026",.enabled=false}).value.value();
    itemsDelete = ui.addButton({.id="items.delete",.label=u"\u522a\u9664\u2026",.enabled=false}).value.value();
    // Right-clicking a row selects it, then offers the same Edit and Delete as the buttons and the
    // Enter / Del shortcuts: each item runs the button's own action.
    itemsMenu = ui.addContextMenu(itemsList, {
        {.id="items.edit", .label=u"\u7de8\u8f2f(E)", .enabled=false, .accessKey=u'E', .shortcutHint=u"Enter", .action=[this](const MenuContext&) { return itemsEdit.invoke(); }},
        {.id="items.delete", .label=u"\u522a\u9664(D)", .enabled=false, .accessKey=u'D', .shortcutHint=u"Del", .action=[this](const MenuContext&) { return itemsDelete.invoke(); }},
    }).value.value();
    font = ui.addDropdown({.id="appearance.font",.label=u"\u5b57\u578b"}, {{{"default"},u"\u9810\u8a2d\uff08Segoe UI\uff0b\u5fae\u8edf\u6b63\u9ed1\u9ad4\uff09"}}).value.value();
    fontStatus_ = ui.addLabel({.id="appearance.font.status"}).value.value();
    nav.addMember({"appearance"}, font); nav.addMember({"appearance"}, fontStatus_);
    refreshFonts();
    font.connectSource({[this]() -> std::optional<OptionValue> { auto value=ui.presentation().fontFamily; return OptionValue{value.empty() ? "default" : "family:" + isoduo::detail::utf8FromUtf16(value)}; }, subscribeTo(fontChanged_)});
    font.connectImmediate([this](const Change<DropdownHandle>& c) { auto settings=ui.presentation(); settings.fontFamily = !c.value || c.value->id == "default" ? u"" : isoduo::detail::utf16FromUtf8(c.value->id.substr(7)); return setPresentation(settings); });
    textSize = ui.addSlider({.id="appearance.text_size",.label=u"\u6587\u5b57\u5927\u5c0f"},{12,28,1}).value.value();
    nav.addMember({"appearance"}, textSize);
    textSize.connectSource({[this] { return ui.presentation().textSize; }, subscribeTo(textSizeChanged_)});
    connectSlider(textSize, "appearance.text_size", [this](const Change<SliderHandle>& change, bool join) {
        auto settings = ui.presentation(); settings.textSize = change.value;
        return setPresentation(settings, join);
    });
    dark = ui.addCheckbox({.id="appearance.dark",.label=u"\u6df1\u8272"}).value.value();
    compact = ui.addCheckbox({.id="appearance.compact",.label=u"\u7dca\u6e4a"}).value.value();
    // WCAG 1.4.8: the user can turn justified text off for the whole window.
    justifyLeft = ui.addCheckbox({.id="appearance.justify_left",.label=u"左右對齊文字改為靠左"}).value.value();
    accent = ui.addDropdown({.id="appearance.accent",.label=u"強調色"}, {{{"default"},u"預設"},{{"green"},u"綠色"},{{"orange"},u"橘色"}}).value.value();
    nav.addMember({"appearance"}, accent);
    accent.connectSource({[this]() -> std::optional<OptionValue> { return OptionValue{accentValue_}; }, subscribeTo(accentChanged_)});
    accent.connectImmediate([this](const Change<DropdownHandle>& c) { return setAccent(c.value ? c.value->id : "default"); });
    // The focus ring (#90): one dropdown for when it shows, a thickness slider, a radius (same as the component, or a slider) and a color.
    focusMode = ui.addDropdown({.id="appearance.focus_mode",.label=u"焦點標示"}, {{{"automatic"},u"自動"},{{"always"},u"一律顯示"},{{"never"},u"關閉"}}).value.value();
    focusWidth = ui.addSlider({.id="appearance.focus_width",.label=u"焦點外框粗細"},{minFocusRingWidth,maxFocusRingWidth,1}).value.value();
    focusRadiusSame = ui.addCheckbox({.id="appearance.focus_radius_same",.label=u"焦點外框圓角跟元件一樣"}).value.value();
    focusRadius = ui.addSlider({.id="appearance.focus_radius",.label=u"焦點外框圓角"},{minFocusRingRadius,maxFocusRingRadius,1}).value.value();
    focusColor = ui.addDropdown({.id="appearance.focus_color",.label=u"焦點外框顏色"}, {{{"default"},u"預設"},{{"green"},u"綠色"},{{"orange"},u"橘色"}}).value.value();
    for (ComponentHandle member : {ComponentHandle{focusMode}, ComponentHandle{focusWidth}, ComponentHandle{focusRadiusSame}, ComponentHandle{focusRadius}, ComponentHandle{focusColor}}) nav.addMember({"appearance"}, member);
    focusMode.connectSource({[this]() -> std::optional<OptionValue> {
        return OptionValue{focusModeId(ui.presentation().focusIndicator)};
    }, subscribeTo(focusModeChanged_)});
    focusMode.connectImmediate([this](const Change<DropdownHandle>& c) {
        auto settings = ui.presentation();
        settings.focusIndicator = (c.value ? focusModeFromId(c.value->id) : std::nullopt).value_or(FocusIndicatorMode::automatic);
        return setPresentation(settings);
    });
    // 3 DIP is the appearances' own ring thickness (ComponentPresentation::focus): what the slider shows while the setting is empty.
    focusWidth.connectSource({[this] { return ui.presentation().focusRingWidth.value_or(3); }, subscribeTo(focusWidthChanged_)});
    connectSlider(focusWidth, "appearance.focus_width", [this](const Change<SliderHandle>& change, bool join) {
        auto settings = ui.presentation(); settings.focusRingWidth = change.value;
        return setPresentation(settings, join);
    });
    focusRadiusSame.connectSource({[this] { return !ui.presentation().focusRingRadius.has_value(); }, subscribeTo(focusRadiusSameChanged_)});
    focusRadiusSame.connectImmediate([this](const Change<CheckboxHandle>& c) {
        auto settings = ui.presentation();
        settings.focusRingRadius = c.value ? std::nullopt : std::optional<int>{focusRadiusStart()};
        return setPresentation(settings);
    });
    focusRadius.connectSource({[this] { return ui.presentation().focusRingRadius.value_or(focusRadiusStart()); }, subscribeTo(focusRadiusChanged_)});
    connectSlider(focusRadius, "appearance.focus_radius", [this](const Change<SliderHandle>& change, bool join) {
        auto settings = ui.presentation(); settings.focusRingRadius = change.value; focusRadiusMemory_ = change.value;
        return setPresentation(settings, join);
    });
    focusColor.connectSource({[this]() -> std::optional<OptionValue> { return OptionValue{focusColorValue_}; }, subscribeTo(focusColorChanged_)});
    focusColor.connectImmediate([this](const Change<DropdownHandle>& c) { return setFocusColor(c.value ? c.value->id : "default"); });
    refreshFocusControls();
    preview_ = ui.addLabel({.id="appearance.preview",.value=u"\u9810\u89bd\uff1a\u7e41\u4e2d English \U0001f600 \U00020000",.textAlign=TextAlign::center}).value.value();
    resetAppearance_ = ui.addButton({.id="appearance.reset",.label=u"\u9084\u539f\u9810\u8a2d\u5916\u89c0"}).value.value();
    for (ComponentHandle member : {ComponentHandle{dark}, ComponentHandle{compact}, ComponentHandle{justifyLeft}, ComponentHandle{preview_}, ComponentHandle{resetAppearance_}}) nav.addMember({"appearance"}, member);
    justifyLeft.connectSource({[this] { return ui.presentation().justifyAsLeft; }, subscribeTo(justifyLeftChanged_)});
    justifyLeft.connectImmediate([this](const Change<CheckboxHandle>& c) { auto settings=ui.presentation(); settings.justifyAsLeft=c.value; return setPresentation(settings); });
    dark.connectSource({[this] { return ui.presentation().variant == ThemeVariant::dark(); }, subscribeTo(darkChanged_)});
    compact.connectSource({[this] { return ui.presentation().density == Density::compact; }, subscribeTo(compactChanged_)});
    dark.connectImmediate([this](const Change<CheckboxHandle>& c) { auto settings=ui.presentation(); settings.variant=c.value ? ThemeVariant::dark() : ThemeVariant::light(); return setPresentation(settings); });
    compact.connectImmediate([this](const Change<CheckboxHandle>& c) { auto settings=ui.presentation(); settings.density=c.value ? Density::compact : Density::standard; return setPresentation(settings); });
    resetAppearance_.bind([this] {
        if (auto r = applyPalette("default", "default"); !r) return r;
        accentValue_ = focusColorValue_ = "default"; if (accentChanged_) accentChanged_(); if (focusColorChanged_) focusColorChanged_();
        return setPresentation(PresentationSettings{});
    });
    for (const char* id : {"about.app", "about.framework", "about.build", "about.appearance", "about.renderer", "about.diagnostics", "about.settings_path"}) {
        aboutLabels_.push_back(ui.addLabel({.id=id}).value.value());
        nav.addMember({"about"}, aboutLabels_.back());
    }
    aboutLabels_[0].connectSource([] { return u"Isoduo 設定管理示範 0.1.0"; });
    aboutLabels_[1].connectSource([] { return u"Isoduo 框架 " + std::u16string(isoduo::frameworkVersion); });
    aboutLabels_[2].connectSource([this] { return u"建置：" + about_.build; });
    aboutLabels_[3].connectSource([this] { return u"外觀：" + about_.appearance; });
    aboutLabels_[4].connectSource([this] { return u"繪製：" + about_.renderer; });
    aboutLabels_[5].connectSource([this] { return about_.diagnostics ? u"診斷：基本診斷（只存記憶體）" : u"診斷：未選用"; });
    aboutLabels_[6].connectSource([this] { return about_.settingsPath; });
    // A label in vertical writing mode: columns run top to bottom, right to left (Taiwan rules).
    aboutVertical_ = ui.addLabel({.id="about.vertical",.value=u"Isoduo 設定管理示範（第 12 版），謹此致謝。",.writingMode=WritingMode::vertical}).value.value();
    nav.addMember({"about"}, aboutVertical_);
    std::vector<OptionItem> licenseOptions;
    for (const auto& license : about_.licenses) licenseOptions.push_back({license.id, license.name});
    licenses = ui.addList({.id="about.licenses",.label=u"授權"}, std::move(licenseOptions)).value.value();
    licenseText_ = ui.addLabel({.id="about.license_text",.lineSpacing=1.4,.paragraphSpacing=8}).value.value();
    licenses.connectSource({[this] { return licenseSelection_; }, subscribeTo(licenseSelectionChanged_)});
    licenses.connectImmediate([this](const Change<ListHandle>& change) { licenseSelection_ = change.value; licenseScroll.offset = 0; if (licenseSelectionChanged_) licenseSelectionChanged_(); return Result{}; });
    licenseText_.connectSource([this] { for (const auto& license : about_.licenses) if (licenseSelection_ && license.id == *licenseSelection_) return license.text; return std::u16string{}; });
    nav.addMember({"about"}, licenses);
    nav.addMember({"about"}, licenseText_);
    for (int n=1; n<=20; ++n) {
        char id[8], number[8]; std::snprintf(id,sizeof(id),"T%03d",n); std::snprintf(number,sizeof(number),"%02d",n);
        items_.push_back({{id}, n==5 || n==6 ? u"\u5171\u7528\u7bc4\u672c" : u"\u7bc4\u672c " + std::u16string(number,number+2), {}});
    }
    itemsSearch.connectSource({[this] { return searchValue_; }, subscribeTo(searchChanged_)});
    itemsSearch.connectImmediate([this](const Change<TextFieldHandle>& c) {
        searchValue_=c.value;
        if (auto r=refreshItems(); !r) return r;
        if (searchChanged_) searchChanged_();
        return Result{};
    });
    itemsList.connectSource({[this] { return selection_; }, subscribeTo(selectionChanged_)});
    itemsList.connectImmediate([this](const Change<ListHandle>& change) { selection_=change.value; itemsEdit.enable(selection_.has_value()); itemsDelete.enable(selection_.has_value()); itemsMenu.enableItem("items.edit", selection_.has_value()); itemsMenu.enableItem("items.delete", selection_.has_value()); if (selectionChanged_) selectionChanged_(); return Result{}; });
    for (ComponentHandle member : {ComponentHandle{itemsSearch}, ComponentHandle{itemsList}, ComponentHandle{itemsCount_}, ComponentHandle{itemsAdd}, ComponentHandle{itemsEdit}, ComponentHandle{itemsDelete}}) nav.addMember({"items"}, member);
    refreshItems();
    status_ = ui.addLabel({.id="app.status",.value=u"使用預設值，尚未建立設定檔"}).value.value();
    undo_ = ui.addButton({.id="app.undo",.label=u"復原",.enabled=false}).value.value();
    redo_ = ui.addButton({.id="app.redo",.label=u"重做",.enabled=false}).value.value();
    revert_ = ui.addButton({.id="app.revert",.label=u"還原為已儲存",.enabled=false}).value.value();
    save_ = ui.addButton({.id="app.save",.label=u"儲存",.enabled=false}).value.value();
    for (ComponentHandle member : {ComponentHandle{nameLabel_}, ComponentHandle{name}, ComponentHandle{nameHelp_}, ComponentHandle{nameError_}, ComponentHandle{notices}, ComponentHandle{mode}, ComponentHandle{region}, ComponentHandle{volume}}) nav.addMember({"general"}, member);
    title_.connectSource([this] { auto id=nav.selected()->id; return id=="general"?u"一般":id=="items"?u"項目清單":id=="appearance"?u"外觀與文字":u"關於"; });
    name.connectSource({[this] { return nameValue_; }, subscribeTo(nameChanged_)});
    name.connectConfirmed([this](const Change<TextFieldHandle>& change) {
        if (std::all_of(change.value.begin(), change.value.end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'　'; })) { name.setError(u"請填寫顯示名稱"); return Result{Status::invalidValue,u"請填寫顯示名稱"}; }
        name.setError(u""); if (nameValue_ != change.value) { nameValue_=change.value; if (nameChanged_) nameChanged_(); dirty(); } return Result{};
    });
    name.onBlur([this] {
        auto draft = name.draft(); if (!draft) return;
        if (std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            name.setError(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"); updateHistoryControls(); return;
        }
        name.setError(u"");
        if (nameValue_ != *draft) { nameValue_=*draft; if (nameChanged_) nameChanged_(); dirty(); }
        name.resetToSource(); updateHistoryControls();
    });
    notices.connectSource({[this] { return noticesValue_; }, subscribeTo(noticesChanged_)});
    notices.connectImmediate([this](const Change<CheckboxHandle>& c) { noticesValue_=c.value; if (noticesChanged_) noticesChanged_(); dirty(); return Result{}; });
    mode.connectSource({[this] { return modeValue_; }, subscribeTo(modeChanged_)});
    mode.connectImmediate([this](const Change<RadioGroupHandle>& c) { modeValue_=c.value; if (modeChanged_) modeChanged_(); dirty(); return Result{}; });
    region.connectSource({[this] { return regionValue_; }, subscribeTo(regionChanged_)});
    region.connectImmediate([this](const Change<DropdownHandle>& c) { regionValue_=c.value; if (regionChanged_) regionChanged_(); dirty(); return Result{}; });
    volume.connectSource({[this] { return volumeValue_; }, subscribeTo(volumeChanged_)});
    connectSlider(volume, "general.volume", [this](const Change<SliderHandle>& c, bool join) { volumeValue_=c.value; if (volumeChanged_) volumeChanged_(); dirty(join); return Result{}; });
    itemTitle_ = ui.addLabel({.id="items.dialog.title"}).value.value();
    itemNameLabel_ = ui.addLabel({.id="items.dialog.name.label",.value=u"\u540d\u7a31",.target="items.dialog.name",.labelKind=LabelKind::name}).value.value();
    itemName = ui.addTextField({.id="items.dialog.name",.maxLength=64}).value.value();
    itemNameError_ = ui.addLabel({.id="items.dialog.name.error",.target="items.dialog.name",.labelKind=LabelKind::error}).value.value();
    itemNoteLabel_ = ui.addLabel({.id="items.dialog.note.label",.value=u"\u8aaa\u660e\uff08\u9078\u586b\uff09",.target="items.dialog.note",.labelKind=LabelKind::name}).value.value();
    itemNoteBold = ui.addButton({.id="items.dialog.note.bold",.label=u"粗體"}).value.value();
    itemNoteItalic = ui.addButton({.id="items.dialog.note.italic",.label=u"斜體"}).value.value();
    itemNoteUnderline = ui.addButton({.id="items.dialog.note.underline",.label=u"底線"}).value.value();
    itemNoteColor = ui.addDropdown({.id="items.dialog.note.color",.label=u"文字顏色"}, {{{"default"},u"預設"},{{"red"},u"紅"},{{"green"},u"綠"},{{"blue"},u"藍"}}).value.value();
    itemNote = ui.addTextField({.id="items.dialog.note",.maxLength=200,.multiline=true,.visibleLines=3,.rich=true}).value.value();
    // The note's named colors, one value per theme, each readable on the surface of both appearances (see the settings demo tests).
    itemNote.setColors({{"red",{176,0,32},{255,138,148}},{"green",{20,96,48},{120,224,150}},{"blue",{28,60,170},{150,180,255}}});
    itemCancel_ = ui.addButton({.id="items.dialog.cancel",.label=u"\u53d6\u6d88"}).value.value();
    itemOk = ui.addButton({.id="items.dialog.ok",.label=u"\u78ba\u5b9a"}).value.value();
    deleteTitle_ = ui.addLabel({.id="items.delete_dialog.title",.value=u"\u522a\u9664\u9805\u76ee"}).value.value();
    deleteMessage_ = ui.addLabel({.id="items.delete_dialog.message"}).value.value();
    deleteCancel_ = ui.addButton({.id="items.delete_dialog.cancel",.label=u"\u53d6\u6d88"}).value.value();
    deleteConfirm_ = ui.addButton({.id="items.delete_dialog.confirm",.label=u"\u522a\u9664"}).value.value();
    itemDialog = ui.addDialog("items.dialog", {itemTitle_,itemNameLabel_,itemName,itemNameError_,itemNoteLabel_,itemNoteBold,itemNoteItalic,itemNoteUnderline,itemNoteColor,itemNote,itemCancel_,itemOk}, itemName, itemsAdd, [](bool) {}).value.value();
    deleteDialog = ui.addDialog("items.delete_dialog", {deleteTitle_,deleteMessage_,deleteCancel_,deleteConfirm_}, deleteCancel_, itemsAdd, [](bool) {}).value.value();
    itemName.connectSource({[this] { return itemNameSource_; }, subscribeTo(itemNameChanged_)});
    itemNote.connectRichSource({[this] { return itemNoteSource_; }, subscribeTo(itemNoteChanged_)});
    // The style buttons act on the note's selection (or the typing style at a caret); the selection stays and the note takes the focus back.
    auto styleButton = [this](isoduo::ButtonHandle& button, isoduo::StyleAttribute attribute) {
        button.bind([this, attribute] { auto result = itemNote.toggleStyle(attribute); itemNote.focus(); return result; });
    };
    styleButton(itemNoteBold, isoduo::StyleAttribute::bold); styleButton(itemNoteItalic, isoduo::StyleAttribute::italic); styleButton(itemNoteUnderline, isoduo::StyleAttribute::underline);
    itemNoteColor.connectSource({[this]() -> std::optional<OptionValue> { return OptionValue{noteColorValue_}; }, subscribeTo(noteColorChanged_)});
    itemNoteColor.connectImmediate([this](const Change<DropdownHandle>& c) {
        noteColorValue_ = c.value ? c.value->id : "default"; if (noteColorChanged_) noteColorChanged_();
        auto result = itemNote.applyStyle({.color = noteColorValue_ == "default" ? std::string{} : noteColorValue_});
        itemNote.focus();
        return result;
    });
    auto openItem = [this](bool edit) -> Result {
        editing_=edit;
        auto found=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
        if (edit && found==items_.end()) return {Status::invalidValue,u"No selection"};
        itemTitle_.connectSource([edit] { return edit ? u"\u7de8\u8f2f\u9805\u76ee" : u"\u65b0\u589e\u9805\u76ee"; });
        auto result=itemDialog.open(); if (!result) return result;
        itemNameSource_=edit ? found->name : u""; itemNoteSource_=edit ? isoduo::RichText{found->note, found->noteSpans} : isoduo::RichText{};
        noteColorValue_="default"; if (noteColorChanged_) noteColorChanged_();
        if (itemNameChanged_) itemNameChanged_(); if (itemNoteChanged_) itemNoteChanged_();
        if (auto r = itemName.resetToSource(); !r) return r;
        if (auto r = itemNote.resetToSource(); !r) return r;
        return itemName.setError(u"");
    };
    itemsAdd.bind([openItem] { return openItem(false); });
    itemsEdit.bind([openItem] { return openItem(true); });
    itemCancel_.bind([this] { return itemDialog.settle(false); });
    ui.connectConfirmation(itemOk, {itemName, itemNote}, [this](const std::vector<ConfirmFieldChange>& changes) -> ConfirmationResult {
        const auto& value=std::get<Change<TextFieldHandle>>(changes[0]).value; const auto& rich=std::get<Change<TextFieldHandle, RichText>>(changes[1]).value;
        if (std::all_of(value.begin(),value.end(),[](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            return {{Status::invalidValue,u"Name required"}, {{itemName,u"\u8acb\u586b\u5beb\u540d\u7a31"}}};
        }
        OptionValue id;
        bool changed=false;
        if (editing_) {
            auto it=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
            if (it==items_.end()) return {{Status::notFound,u"Item not found"}, {}};
            id=it->id; changed=it->name!=value || it->note!=rich.text || it->noteSpans!=rich.spans; it->name=value; it->note=rich.text; it->noteSpans=rich.spans;
        } else {
            char buffer[16]; std::snprintf(buffer,sizeof(buffer),"T%03d",nextItemId_++); id={buffer}; items_.push_back({id,value,rich.text,rich.spans}); changed=true;
        }
        auto result=itemDialog.settle(true); if (!result) return {result, {}};
        if (foldAscii(value).find(foldAscii(searchValue_))==std::u16string::npos) {
            if (auto r=itemsSearch.setValue(u""); !r) return {r, {}};
        }
        if (auto r=refreshItems(); !r) return {r, {}};
        if (auto r=itemsList.select(id); !r) return {r, {}};
        if (changed) dirty(); return {Result{}, {}};
    });
    itemsDelete.bind([this] {
        auto it=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
        if (it==items_.end()) return Result{Status::invalidValue,u"No selection"};
        auto message=u"\u8981\u522a\u9664\u300c" + it->name + u"\u300d\u55ce\uff1f\u522a\u9664\u5f8c\u53ef\u4ee5\u6309\u300c\u5fa9\u539f\u300d\u627e\u56de\u3002";
        deleteMessage_.connectSource([message] { return message; }); return deleteDialog.open();
    });
    deleteCancel_.bind([this] { return deleteDialog.settle(false); });
    deleteConfirm_.bind([this] {
        auto selected=selection_; auto result=deleteDialog.settle(true); if (!result) return result;
        std::erase_if(items_,[&](const Item& item) { return selected && item.id==*selected; });
        if (auto r=refreshItems(); !r) return r;
        dirty(); return Result{};
    });
    closeTitle_ = ui.addLabel({.id="app.close_dialog.title",.value=u"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f"}).value.value();
    closeMessage_ = ui.addLabel({.id="app.close_dialog.message",.value=u"\u6709\u5c1a\u672a\u5132\u5b58\u7684\u4fee\u6539\u3002\u95dc\u9589\u524d\u8981\u5148\u5132\u5b58\u55ce\uff1f"}).value.value();
    closeSave = ui.addButton({.id="app.close_dialog.save",.label=u"\u5132\u5b58"}).value.value();
    closeDiscard = ui.addButton({.id="app.close_dialog.discard",.label=u"\u4e0d\u5132\u5b58"}).value.value();
    closeCancel = ui.addButton({.id="app.close_dialog.cancel",.label=u"\u53d6\u6d88"}).value.value();
    closeDialog = ui.addDialog("app.close_dialog", {closeTitle_,closeMessage_,closeSave,closeDiscard,closeCancel}, closeSave, save_, [](bool) {}).value.value();
    closeCancel.bind([this] { return closeDialog.settle(false); });
    closeDiscard.bind([this] { auto result=closeDialog.settle(true); if (result && closeHandler_) closeHandler_(); return result; });
    closeSave.bind([this] { auto result=closeDialog.settle(true); if (!result) return result; result=saveDocument(); if (result && closeHandler_) closeHandler_(); return result; });
    loadTitle_ = ui.addLabel({.id="app.load_dialog.title"}).value.value();
    loadMessage_ = ui.addLabel({.id="app.load_dialog.message"}).value.value();
    loadOk_ = ui.addButton({.id="app.load_dialog.ok",.label=u"確定"}).value.value();
    loadDialog_ = ui.addDialog("app.load_dialog", {loadTitle_,loadMessage_,loadOk_}, loadOk_, save_, [this](bool) { nav.focus(); }).value.value();
    loadOk_.bind([this] { return loadDialog_.settle(true); });
    errorTitle_ = ui.addLabel({.id="app.error_dialog.title",.value=u"無法儲存"}).value.value();
    errorMessage_ = ui.addLabel({.id="app.error_dialog.message"}).value.value();
    errorOk_ = ui.addButton({.id="app.error_dialog.ok",.label=u"確定"}).value.value();
    errorDialog_ = ui.addDialog("app.error_dialog", {errorTitle_,errorMessage_,errorOk_}, errorOk_, save_, [](bool) {}).value.value();
    errorOk_.bind([this] { return errorDialog_.settle(true); });
    ui.addShortcut({.key=Key::enter},itemsEdit,itemsList);
    ui.addShortcut({.key=Key::del},itemsDelete,itemsList);
    ui.addShortcut({.letter=u'S',.ctrl=true},save_);
    ui.addShortcut({.letter=u'Z',.ctrl=true},undo_);
    ui.addShortcut({.letter=u'Y',.ctrl=true},redo_);
    ui.addShortcut({.letter=u'Z',.ctrl=true,.shift=true},redo_);
    current_ = snapshot(); saved_ = current_;
    undo_.bind([this] {
        auto description = name.describe();
        if (description && !description->error.empty()) {
            auto result=name.resetToSource(); if (!result) return result;
            name.setError(u""); updateHistoryControls(); return Result{};
        }
        if (undoHistory_.empty()) return Result{Status::emptyHistory, {}};
        redoHistory_.push_back(snapshot());
        auto previous = undoHistory_.back(); undoHistory_.pop_back();
        return apply(previous);
    });
    redo_.bind([this] {
        if (redoHistory_.empty()) return Result{Status::emptyHistory, {}};
        undoHistory_.push_back(snapshot());
        auto next = redoHistory_.back(); redoHistory_.pop_back();
        return apply(next);
    });
    save_.bind([this] { return saveDocument(); });
    revert_.bind([this] {
        name.resetToSource();
        Result applied;
        if (saved_ && snapshot() != *saved_) { auto target=*saved_; auto before=snapshot(); applied=apply(target); current_=before; dirty(); }
        updateHistoryControls(); return applied;
    });
    ui.onChange([this] { updateHistoryControls(); });
    nav.focus();
}
Result Demo::refreshItems() {
    auto query = foldAscii(searchValue_);
    std::vector<OptionItem> visible;
    for (const auto& item : items_) if (foldAscii(item.name).find(query) != std::u16string::npos) visible.push_back({item.id,item.name});
    auto count = visible.size();
    auto oldCount = itemsCount_.describe();
    if (!oldCount) return {oldCount.status, oldCount.message};
    auto number = [](std::size_t n) { auto str=std::to_string(n); return std::u16string(str.begin(),str.end()); };
    auto text = u"\u5171 " + number(items_.size()) + u" \u7b46\uff0c\u986f\u793a " + number(count) + u" \u7b46";
    if (items_.size() >= 1000) text += u"\uff08\u5df2\u9054\u4e0a\u9650\uff09";
    if (auto r=itemsCount_.connectSource([text] { return text; }); !r) return r;
    if (auto r=itemsList.setItems(std::move(visible)); !r) {
        auto previous = oldCount->value;
        itemsCount_.connectSource([previous] { return previous; });
        return r;
    }
    return itemsAdd.enable(items_.size()<1000);
}
void Demo::connectSlider(SliderHandle& slider, const char* id, std::function<Result(const Change<SliderHandle>&, bool join)> change) {
    slider.connectImmediate([this, &slider, id, change = std::move(change)](const Change<SliderHandle>& c) {
        const bool focused = slider.describe()->focused;
        auto result = change(c, focused && dragGroup_ == id); if (!result) return result;
        dragGroup_ = focused ? id : "";
        return Result{};
    });
    slider.onBlur([this, id] { if (dragGroup_ == id) dragGroup_.clear(); });
}
int Demo::focusRadiusStart() const {
    if (focusRadiusMemory_) return *focusRadiusMemory_;
    const auto* appearance = isoduo::detail::access(ui).boundAppearance();
    return appearance ? appearance->radius() : 6;
}
void Demo::refreshFonts() {
    std::vector<OptionItem> options{{{"default"},u"\u9810\u8a2d\uff08Segoe UI\uff0b\u5fae\u8edf\u6b63\u9ed1\u9ad4\uff09"}};
    auto key = ui.presentation().fontFamily;
    bool found = key.empty();
    for (const auto& family : about_.fonts) {
        options.push_back({{"family:" + isoduo::detail::utf8FromUtf16(family.key)}, family.displayName});
        found = found || family.key == key;
    }
    if (!found) options.push_back({{"family:" + isoduo::detail::utf8FromUtf16(key)}, key + u"\uff08\u672a\u5b89\u88dd\uff09"});
    font.setItems(std::move(options));
    auto resolution = about_.resolveFont ? about_.resolveFont(ui.presentation()) : FontResolution{key, key.empty() ? u"Segoe UI" : key, found, {}};
    auto status = resolution.requestedAvailable ? u"\u76ee\u524d\u5b57\u578b\uff1a" + resolution.primary : u"\u627e\u4e0d\u5230\u300c" + key + u"\u300d\u5b57\u578b\uff0c\u66ab\u6642\u6539\u7528\u9810\u8a2d\u5b57\u578b\u3002\u8a2d\u5b9a\u4ecd\u6703\u4fdd\u7559\u3002";
    fontStatus_.connectSource([status] { return status; });
}
namespace {
struct AccentColors { Color accent, onAccent, selection; };
struct AccentRow { const char* appearance; const char* variant; AccentColors green, orange; };
// Every row passes checkPaletteContrast on top of that appearance's built-in palette. The ring color choices reuse
// the accent of the same row: those colors are already 3:1 on every surface (the focus rules in checkPaletteContrast).
const AccentRow accentRows[] = {
    {"Minimal Web", "light", {{20,110,50}, {255,255,255}, {205,235,210}}, {{170,70,0}, {255,255,255}, {255,225,195}}},
    {"Minimal Web", "dark", {{110,200,130}, {8,40,16}, {36,92,52}}, {{255,170,90}, {50,24,0}, {120,66,20}}},
    {"Material-inspired", "light", {{28,106,60}, {255,255,255}, {200,235,205}}, {{160,70,0}, {255,255,255}, {255,220,190}}},
    {"Material-inspired", "dark", {{150,220,160}, {10,50,20}, {40,95,55}}, {{255,184,120}, {70,30,0}, {110,62,20}}},
};
}
namespace {
// One entry per role for every linked built-in appearance and variant: the colors of the chosen row ("green" or "orange"), none for "default".
std::vector<PaletteOverride> rowOverrides(const std::string& id, std::vector<std::pair<const char*, Color>> (*roles)(const AccentColors&)) {
    std::vector<PaletteOverride> list;
    if (id != "green" && id != "orange") return list;
    for (const auto& row : accentRows) {
        if (!builtinAppearance(row.appearance)) continue;
        for (const auto& [role, color] : roles(id == "green" ? row.green : row.orange)) list.push_back({row.appearance, ThemeVariant{row.variant}, role, color});
    }
    return list;
}
}
std::vector<PaletteOverride> Demo::focusColorOverrides(const std::string& id) {
    return rowOverrides(id, [](const AccentColors& c) { return std::vector<std::pair<const char*, Color>>{{"focus", c.accent}}; });
}
std::vector<PaletteOverride> Demo::accentOverrides(const std::string& id) {
    return rowOverrides(id, [](const AccentColors& c) { return std::vector<std::pair<const char*, Color>>{{"accent", c.accent}, {"on_accent", c.onAccent}, {"selection", c.selection}}; });
}
Result Demo::applyPalette(const std::string& accentId, const std::string& focusId) {
    auto list = accentOverrides(accentId), focus = focusColorOverrides(focusId);
    list.insert(list.end(), focus.begin(), focus.end());
    return ui.setPaletteOverrides(std::move(list));
}
Result Demo::applyAccent(const std::string& id) {
    auto result = applyPalette(id, focusColorValue_);
    if (result) { accentValue_ = id; if (accentChanged_) accentChanged_(); }
    return result;
}
Result Demo::applyFocusColor(const std::string& id) {
    auto result = applyPalette(accentValue_, id);
    if (result) { focusColorValue_ = id; if (focusColorChanged_) focusColorChanged_(); }
    return result;
}
Result Demo::setAccent(const std::string& id) {
    auto result = applyAccent(id);
    if (result) dirty();
    return result;
}
Result Demo::setFocusColor(const std::string& id) {
    auto result = applyFocusColor(id);
    if (result) dirty();
    return result;
}
Result Demo::presentationChanged() {
    refreshFonts();
    for (auto* slot : {&fontChanged_, &textSizeChanged_, &darkChanged_, &compactChanged_, &justifyLeftChanged_,
                       &focusModeChanged_, &focusWidthChanged_, &focusRadiusSameChanged_, &focusRadiusChanged_})
        if (*slot) (*slot)();
    return refreshFocusControls();
}
Result Demo::refreshFocusControls() {
    // The indicator off leaves nothing to style: the three controls are disabled but keep their values for when it comes back.
    const bool styling = ui.presentation().focusIndicator != FocusIndicatorMode::never;
    for (auto result : {focusWidth.enable(styling), focusRadiusSame.enable(styling), focusRadius.enable(styling), focusColor.enable(styling)})
        if (!result) return result;
    return {};
}
Result Demo::setPresentation(const PresentationSettings& settings, bool coalesce) {
    auto result=ui.setPresentation(settings); if (!result) return result;
    auto refreshed = presentationChanged(); dirty(coalesce); return refreshed;
}
void Demo::onDirtyChanged(std::function<void(bool)> callback) { dirtyChanged_ = std::move(callback); if (dirtyChanged_) dirtyChanged_(dirtyState_); }
SettingsDocument Demo::snapshot() const {
    return {nameValue_, noticesValue_, modeValue_, regionValue_, volumeValue_, items_, nextItemId_, ui.presentation(), accentValue_, focusColorValue_};
}
void Demo::updateHistoryControls() {
    auto description = name.describe();
    undo_.enable(!undoHistory_.empty() || (description && !description->error.empty())); redo_.enable(!redoHistory_.empty());
    auto draft = name.draft();
    bool changed = saved_ && (snapshot() != *saved_ || (draft && *draft != nameValue_));
    save_.enable(changed); revert_.enable(changed);
    if (changed != dirtyState_) { dirtyState_=changed; if (dirtyChanged_) dirtyChanged_(changed); }
    status_.connectSource([changed, this] { return changed && saveFailed_ ? u"有未儲存的修改（上次儲存失敗）" : changed ? u"\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539" : savedOnce_ ? u"\u6240\u6709\u4fee\u6539\u5df2\u5132\u5b58" : u"\u4f7f\u7528\u9810\u8a2d\u503c\uff0c\u5c1a\u672a\u5efa\u7acb\u8a2d\u5b9a\u6a94"; });
}
void Demo::dirty(bool coalesce) {
    if (!coalesce) dragGroup_.clear();
    auto now = snapshot();
    if (current_ && *current_ != now) {
        if (!coalesce) undoHistory_.push_back(*current_);
        if (undoHistory_.size() > 100) undoHistory_.erase(undoHistory_.begin());
        redoHistory_.clear();
    }
    current_ = std::move(now); updateHistoryControls();
}
Result Demo::apply(const SettingsDocument& value) {
    Result outcome;  // the first part that could not be applied; the others are still applied
    auto note = [&outcome](Result result) { if (!result && outcome) outcome = std::move(result); };
    dragGroup_.clear();
    nameValue_=value.name; noticesValue_=value.notices; modeValue_=value.mode; regionValue_=value.region;
    volumeValue_=value.volume; items_=value.items; nextItemId_=value.nextItemId;
    if (nameChanged_) nameChanged_(); if (noticesChanged_) noticesChanged_();
    if (modeChanged_) modeChanged_(); if (regionChanged_) regionChanged_(); if (volumeChanged_) volumeChanged_();
    auto palette = applyPalette(value.accent, value.focusColor);
    if (palette) {
        accentValue_=value.accent; focusColorValue_=value.focusColor;
        if (accentChanged_) accentChanged_(); if (focusColorChanged_) focusColorChanged_();
    }
    note(std::move(palette));
    note(ui.setPresentation(value.presentation)); if (value.presentation.focusRingRadius) focusRadiusMemory_=*value.presentation.focusRingRadius;
    note(presentationChanged()); note(refreshItems()); current_=value; updateHistoryControls();
    return outcome;
}
Result Demo::loadDocument(const SettingsDocument& value) {
    auto result = apply(value); saved_=value; current_=value; savedOnce_=true; undoHistory_.clear(); redoHistory_.clear(); updateHistoryControls();
    if (!result) showLoadNotice(u"設定檔沒有完全套用", u"有一部分設定無法套用：" + result.message);
    return result;
}
void Demo::showLoadNotice(std::u16string title, std::u16string message) {
    loadTitle_.connectSource([title] { return title; }); loadMessage_.connectSource([message] { return message; }); loadDialog_.open();
}
void Demo::setRenderer(std::u16string value) { about_.renderer = std::move(value); }
Result Demo::saveDocument() {
        auto draft = name.draft();
        if (!draft) return Result{draft.status, draft.message};
        if (std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            nav.select({"general"}); name.setError(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"); name.focus();
            return Result{Status::invalidValue, u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"};
        }
        if (nameValue_ != *draft) { nameValue_=*draft; if (nameChanged_) nameChanged_(); dirty(); }
        if (saveHandler_) {
            auto error=saveHandler_(snapshot());
            if (!error.empty()) {
                saveFailed_=true; updateHistoryControls();
                auto message=u"無法寫入 " + settingsPath_ + u"：" + error + u"。修改仍保留，尚未儲存。";
                errorMessage_.connectSource([message] { return message; }); errorDialog_.open();
                return Result{Status::invalidValue, error};
            }
        }
        saveFailed_=false; saved_ = snapshot(); savedOnce_ = true; updateHistoryControls(); return Result{};
}
bool Demo::requestClose() {
    if (closeDialog.isOpen().value.value_or(false)) { closeDialog.settle(false); return false; }
    for (auto dialog : {itemDialog, deleteDialog, loadDialog_, errorDialog_})
        if (dialog.isOpen().value.value_or(false)) dialog.settle(false);
    if (!isDirty()) return true;
    closeDialog.open(); return false;
}
bool Demo::isDirty() const {
    auto draft = name.draft();
    return dirtyState_ || (draft && *draft != nameValue_);
}
bool Demo::blankNameDraft() const {
    auto draft = name.draft();
    return draft && std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; });
}
std::u16string Demo::saveFailedRenderer() {
    auto draft = name.draft();
    auto document = snapshot();
    if (draft && !std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) document.name = *draft;
    if (!saveHandler_) return {};
    return saveHandler_(document);
}
layout::Node Demo::page() const {
    using namespace layout;
    std::vector<Node> content{component(title_,24)};
    if (nav.selected()->id == "general") {
        content.push_back(component(name)); content.push_back(component(notices)); content.push_back(component(mode)); content.push_back(component(region)); content.push_back(component(volume));
    }
    if (nav.selected()->id == "items") {
        content.push_back(component(itemsSearch)); content.push_back(component(itemsList)); content.push_back(component(itemsCount_));
        content.push_back(row({component(itemsAdd),component(itemsEdit),component(itemsDelete)},8));
    }
    if (nav.selected()->id == "appearance") { content.push_back(component(font)); content.push_back(component(fontStatus_)); content.push_back(component(textSize)); content.push_back(component(dark)); content.push_back(component(compact)); content.push_back(component(justifyLeft)); content.push_back(component(accent));
        content.push_back(component(focusMode)); content.push_back(component(focusWidth)); content.push_back(component(focusRadiusSame));
        if (ui.presentation().focusRingRadius) content.push_back(component(focusRadius));  // the slider shows once "same as the component" is unchecked
        content.push_back(component(focusColor)); content.push_back(component(preview_)); content.push_back(component(resetAppearance_)); }
    if (nav.selected()->id == "about") {
        for (auto label : aboutLabels_) content.push_back(component(label));
        content.push_back(verticalText(aboutVertical_, 200));
        content.push_back(component(licenses));
        content.push_back(scrollText(licenseText_, licenseScroll, 200));
    }
    content.push_back(component(status_));
    content.push_back(row({component(undo_),component(redo_),component(revert_),component(save_)},8));
    auto background=layout::scroll(row({component(nav),column(std::move(content),12,24)},16));
    return layout::modal(std::move(background), {
        {itemDialog,column({component(itemTitle_,24),component(itemName),row({component(itemNoteBold),component(itemNoteItalic),component(itemNoteUnderline),component(itemNoteColor)},8),component(itemNote),row({component(itemCancel_),component(itemOk)},8)},12)},
        {deleteDialog,column({component(deleteTitle_,24),component(deleteMessage_),row({component(deleteCancel_),component(deleteConfirm_)},8)},12)},
        {loadDialog_,column({component(loadTitle_,24),component(loadMessage_),component(loadOk_)},12)},
        {closeDialog,column({component(closeTitle_,24),component(closeMessage_),row({component(closeSave),component(closeDiscard),component(closeCancel)},8)},12)},
        {errorDialog_,column({component(errorTitle_,24),component(errorMessage_),component(errorOk_)},12)}
    });
}
}

