#include "settings_demo.hpp"
#include "lcars/version.hpp"
#include <algorithm>
#include <cstdio>
#include <codecvt>
#include <locale>
using namespace lcars;
namespace settings_demo {
namespace {
std::u16string fold_ascii(std::u16string value) {
    for (auto& c : value) if (c >= u'A' && c <= u'Z') c += u'a' - u'A';
    return value;
}
}
Demo::Demo(AboutInfo about) : about_(std::move(about)) {
    nav = ui.add_navigation_list({.id="app.nav", .kind=Kind::navigation_list, .label=u"設定頁面"},
        {{{"general"},u"一般",true,{}},{{"items"},u"項目清單",true,{}},{{"appearance"},u"外觀與文字",true,{}},{{"about"},u"關於",true,{}}}, {"general"}).value.value();
    title_ = ui.add_label({.id="app.page_title",.kind=Kind::label}).value.value();
    name_label_ = ui.add_label({.id="general.name.label",.kind=Kind::label,.value=u"顯示名稱",.target="general.name",.role=TextRole::name}).value.value();
    name = ui.add_text({.id="general.name",.kind=Kind::text,.value=name_value_,.max_length=64}).value.value();
    name_help_ = ui.add_label({.id="general.name.help",.kind=Kind::label,.value=u"單行文字，可輸入繁體中文或英文。",.target="general.name",.role=TextRole::description}).value.value();
    name_error_ = ui.add_label({.id="general.name.error",.kind=Kind::label,.target="general.name",.role=TextRole::error}).value.value();
    notices = ui.add_checkbox({.id="general.notices",.kind=Kind::checkbox,.label=u"啟用提示"}).value.value();
    mode = ui.add_radio_group({.id="general.mode",.kind=Kind::radio_group,.label=u"模式"}, {{{"normal"},u"一般"},{{"focus"},u"專注"},{{"offline"},u"離線"}}).value.value();
    region = ui.add_dropdown({.id="general.region",.kind=Kind::dropdown,.label=u"地區"}, {{{"north"},u"北區"},{{"central"},u"中區"},{{"south"},u"南區"}}).value.value();
    volume = ui.add_slider({.id="general.volume",.kind=Kind::slider,.label=u"音量"},{0,100,5}).value.value();
    items_search = ui.add_text({.id="items.search",.kind=Kind::text,.label=u"\u641c\u5c0b\u540d\u7a31"}).value.value();
    items_list = ui.add_list({.id="items.list",.kind=Kind::list,.label=u"\u9805\u76ee"}, {}).value.value();
    items_count_ = ui.add_label({.id="items.count",.kind=Kind::label}).value.value();
    items_add = ui.add_button({.id="items.add",.kind=Kind::button,.label=u"\u65b0\u589e\u2026"}).value.value();
    items_edit = ui.add_button({.id="items.edit",.kind=Kind::button,.label=u"\u7de8\u8f2f\u2026",.enabled=false}).value.value();
    items_delete = ui.add_button({.id="items.delete",.kind=Kind::button,.label=u"\u522a\u9664\u2026",.enabled=false}).value.value();
    font = ui.add_dropdown({.id="appearance.font",.kind=Kind::dropdown,.label=u"\u5b57\u578b"}, {{{"default"},u"\u9810\u8a2d\uff08Segoe UI\uff0b\u5fae\u8edf\u6b63\u9ed1\u9ad4\uff09"}}).value.value();
    font_status_ = ui.add_label({.id="appearance.font.status",.kind=Kind::label}).value.value();
    nav.add_member({"appearance"}, font); nav.add_member({"appearance"}, font_status_);
    refresh_fonts();
    font.connect_source({[this]() -> std::optional<OptionValue> { auto value=ui.presentation().font_family; return OptionValue{value.empty() ? "default" : "family:" + std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.to_bytes(value)}; }, [this](auto changed) { font_changed_=std::move(changed); return [this] { font_changed_={}; }; }});
    font.connect_immediate([this](const DropdownChange& c) { auto settings=ui.presentation(); settings.font_family = !c.value || c.value->id == "default" ? u"" : std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.from_bytes(c.value->id.substr(7)); return set_presentation(settings); });
    text_size = ui.add_slider({.id="appearance.text_size",.kind=Kind::slider,.label=u"\u6587\u5b57\u5927\u5c0f"},{12,28,1}).value.value();
    nav.add_member({"appearance"}, text_size);
    text_size.connect_source({[this] { return ui.presentation().text_size; }, [this](auto changed) { text_size_changed_ = std::move(changed); return [this] { text_size_changed_ = {}; }; }});
    text_size.connect_immediate([this](const NumericChange& change) {
        auto settings = ui.presentation(); settings.text_size = change.value;
        bool focused = text_size.describe()->focused;
        auto result = set_presentation(settings, focused && text_size_group_); if (!result) return result;
        text_size_group_ = focused;
        return Result{};
    });
    text_size.on_blur([this] { text_size_group_ = false; });
    dark = ui.add_checkbox({.id="appearance.dark",.kind=Kind::checkbox,.label=u"\u6df1\u8272"}).value.value();
    compact = ui.add_checkbox({.id="appearance.compact",.kind=Kind::checkbox,.label=u"\u7dca\u6e4a"}).value.value();
    preview_ = ui.add_label({.id="appearance.preview",.kind=Kind::label,.value=u"\u9810\u89bd\uff1a\u7e41\u4e2d English \U0001f600 \U00020000"}).value.value();
    reset_appearance_ = ui.add_button({.id="appearance.reset",.kind=Kind::button,.label=u"\u9084\u539f\u9810\u8a2d\u5916\u89c0"}).value.value();
    for (ComponentHandle member : {ComponentHandle{dark}, ComponentHandle{compact}, ComponentHandle{preview_}, ComponentHandle{reset_appearance_}}) nav.add_member({"appearance"}, member);
    dark.connect_source({[this] { return ui.presentation().variant == ThemeVariant::dark(); }, [this](auto changed) { dark_changed_=std::move(changed); return [this] { dark_changed_={}; }; }});
    compact.connect_source({[this] { return ui.presentation().density == Density::compact; }, [this](auto changed) { compact_changed_=std::move(changed); return [this] { compact_changed_={}; }; }});
    dark.connect_immediate([this](const BoolChange& c) { auto settings=ui.presentation(); settings.variant=c.value ? ThemeVariant::dark() : ThemeVariant::light(); return set_presentation(settings); });
    compact.connect_immediate([this](const BoolChange& c) { auto settings=ui.presentation(); settings.density=c.value ? Density::compact : Density::standard; return set_presentation(settings); });
    reset_appearance_.bind([this] { return set_presentation(PresentationSettings{}); });
    for (const char* id : {"about.app", "about.framework", "about.build", "about.appearance", "about.renderer", "about.diagnostics", "about.settings_path"}) {
        about_labels_.push_back(ui.add_label({.id=id,.kind=Kind::label}).value.value());
        nav.add_member({"about"}, about_labels_.back());
    }
    about_labels_[0].connect_source([] { return u"LcarsUI 設定管理示範 0.1.0"; });
    about_labels_[1].connect_source([] { return u"LcarsUI 框架 " + std::u16string(lcars::framework_version); });
    about_labels_[2].connect_source([this] { return u"建置：" + about_.build; });
    about_labels_[3].connect_source([this] { return u"外觀：" + about_.appearance; });
    about_labels_[4].connect_source([this] { return u"繪製：" + about_.renderer; });
    about_labels_[5].connect_source([] { return u"診斷：未選用"; });
    about_labels_[6].connect_source([this] { return about_.settings_path; });
    std::vector<RadioOption> license_options;
    for (const auto& license : about_.licenses) license_options.push_back({license.id, license.name});
    licenses = ui.add_list({.id="about.licenses",.kind=Kind::list,.label=u"授權"}, std::move(license_options)).value.value();
    license_text_ = ui.add_label({.id="about.license_text",.kind=Kind::label}).value.value();
    licenses.connect_source({[this] { return license_selection_; }, [this](auto changed) { license_selection_changed_ = std::move(changed); return [this] { license_selection_changed_ = {}; }; }});
    licenses.connect_immediate([this](std::optional<OptionValue> value) { license_selection_ = std::move(value); license_scroll.offset = 0; if (license_selection_changed_) license_selection_changed_(); return Result{}; });
    license_text_.connect_source([this] { for (const auto& license : about_.licenses) if (license_selection_ && license.id == *license_selection_) return license.text; return std::u16string{}; });
    nav.add_member({"about"}, licenses);
    nav.add_member({"about"}, license_text_);
    for (int n=1; n<=20; ++n) {
        char id[8], number[8]; std::snprintf(id,sizeof(id),"T%03d",n); std::snprintf(number,sizeof(number),"%02d",n);
        items_.push_back({{id}, n==5 || n==6 ? u"\u5171\u7528\u7bc4\u672c" : u"\u7bc4\u672c " + std::u16string(number,number+2), {}});
    }
    items_search.connect_source({[this] { return search_value_; }, [this](auto changed) { search_changed_=std::move(changed); return [this] { search_changed_={}; }; }});
    items_search.connect_immediate([this](const TextChange& c) {
        search_value_=c.value;
        if (auto r=refresh_items(); !r) return r;
        if (search_changed_) search_changed_();
        return Result{};
    });
    items_list.connect_source({[this] { return selection_; }, [this](auto changed) { selection_changed_=std::move(changed); return [this] { selection_changed_={}; }; }});
    items_list.connect_immediate([this](std::optional<OptionValue> value) { selection_=std::move(value); items_edit.enable(selection_.has_value()); items_delete.enable(selection_.has_value()); if (selection_changed_) selection_changed_(); return Result{}; });
    for (ComponentHandle member : {ComponentHandle{items_search}, ComponentHandle{items_list}, ComponentHandle{items_count_}, ComponentHandle{items_add}, ComponentHandle{items_edit}, ComponentHandle{items_delete}}) nav.add_member({"items"}, member);
    refresh_items();
    status_ = ui.add_label({.id="app.status",.kind=Kind::label,.value=u"使用預設值，尚未建立設定檔"}).value.value();
    undo_ = ui.add_button({.id="app.undo",.kind=Kind::button,.label=u"復原",.enabled=false}).value.value();
    redo_ = ui.add_button({.id="app.redo",.kind=Kind::button,.label=u"重做",.enabled=false}).value.value();
    revert_ = ui.add_button({.id="app.revert",.kind=Kind::button,.label=u"還原為已儲存",.enabled=false}).value.value();
    save_ = ui.add_button({.id="app.save",.kind=Kind::button,.label=u"儲存",.enabled=false}).value.value();
    for (ComponentHandle member : {ComponentHandle{name_label_}, ComponentHandle{name}, ComponentHandle{name_help_}, ComponentHandle{name_error_}, ComponentHandle{notices}, ComponentHandle{mode}, ComponentHandle{region}, ComponentHandle{volume}}) nav.add_member({"general"}, member);
    title_.connect_source([this] { auto id=nav.selected()->id; return id=="general"?u"一般":id=="items"?u"項目清單":id=="appearance"?u"外觀與文字":u"關於"; });
    name.connect_source({[this] { return name_value_; }, [this](std::function<void()> notify) { name_changed_ = std::move(notify); return [this] { name_changed_ = {}; }; }});
    name.connect_confirmed([this](const TextChange& change) {
        if (std::all_of(change.value.begin(), change.value.end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'　'; })) { name.set_error(u"請填寫顯示名稱"); return Result{Status::invalid_value,u"請填寫顯示名稱"}; }
        name.set_error(u""); if (name_value_ != change.value) { name_value_=change.value; if (name_changed_) name_changed_(); dirty(); } return Result{};
    });
    name.on_blur([this] {
        auto draft = name.draft(); if (!draft) return;
        if (std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            name.set_error(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"); update_history_controls(); return;
        }
        name.set_error(u"");
        if (name_value_ != *draft) { name_value_=*draft; if (name_changed_) name_changed_(); dirty(); }
        name.reset_to_source(); update_history_controls();
    });
    notices.connect_source({[this] { return notices_value_; }, [this](std::function<void()> notify) { notices_changed_ = std::move(notify); return [this] { notices_changed_ = {}; }; }});
    notices.connect_immediate([this](const BoolChange& c) { notices_value_=c.value; if (notices_changed_) notices_changed_(); dirty(); return Result{}; });
    mode.connect_source({[this] { return mode_value_; }, [this](std::function<void()> notify) { mode_changed_ = std::move(notify); return [this] { mode_changed_ = {}; }; }});
    mode.connect_immediate([this](const OptionChange& c) { mode_value_=c.value; if (mode_changed_) mode_changed_(); dirty(); return Result{}; });
    region.connect_source({[this] { return region_value_; }, [this](std::function<void()> notify) { region_changed_ = std::move(notify); return [this] { region_changed_ = {}; }; }});
    region.connect_immediate([this](const DropdownChange& c) { region_value_=c.value; if (region_changed_) region_changed_(); dirty(); return Result{}; });
    volume.connect_source({[this] { return volume_value_; }, [this](std::function<void()> notify) { volume_changed_ = std::move(notify); return [this] { volume_changed_ = {}; }; }});
    volume.connect_immediate([this](const NumericChange& c) { volume_value_=c.value; if (volume_changed_) volume_changed_(); bool focused = volume.describe()->focused; dirty(focused && volume_group_); volume_group_ = focused; return Result{}; });
    volume.on_blur([this] { volume_group_ = false; });
    item_title_ = ui.add_label({.id="items.dialog.title",.kind=Kind::label}).value.value();
    item_name_label_ = ui.add_label({.id="items.dialog.name.label",.kind=Kind::label,.value=u"\u540d\u7a31",.target="items.dialog.name",.role=TextRole::name}).value.value();
    item_name = ui.add_text({.id="items.dialog.name",.kind=Kind::text,.max_length=64}).value.value();
    item_name_error_ = ui.add_label({.id="items.dialog.name.error",.kind=Kind::label,.target="items.dialog.name",.role=TextRole::error}).value.value();
    item_note_label_ = ui.add_label({.id="items.dialog.note.label",.kind=Kind::label,.value=u"\u8aaa\u660e\uff08\u9078\u586b\uff09",.target="items.dialog.note",.role=TextRole::name}).value.value();
    item_note = ui.add_text({.id="items.dialog.note",.kind=Kind::text,.max_length=200}).value.value();
    item_cancel_ = ui.add_button({.id="items.dialog.cancel",.kind=Kind::button,.label=u"\u53d6\u6d88"}).value.value();
    item_ok = ui.add_button({.id="items.dialog.ok",.kind=Kind::button,.label=u"\u78ba\u5b9a"}).value.value();
    delete_title_ = ui.add_label({.id="items.delete_dialog.title",.kind=Kind::label,.value=u"\u522a\u9664\u9805\u76ee"}).value.value();
    delete_message_ = ui.add_label({.id="items.delete_dialog.message",.kind=Kind::label}).value.value();
    delete_cancel_ = ui.add_button({.id="items.delete_dialog.cancel",.kind=Kind::button,.label=u"\u53d6\u6d88"}).value.value();
    delete_confirm_ = ui.add_button({.id="items.delete_dialog.confirm",.kind=Kind::button,.label=u"\u522a\u9664"}).value.value();
    item_dialog = ui.add_dialog("items.dialog", {item_title_,item_name_label_,item_name,item_name_error_,item_note_label_,item_note,item_cancel_,item_ok}, item_name, items_add, [](bool) {}).value.value();
    delete_dialog = ui.add_dialog("items.delete_dialog", {delete_title_,delete_message_,delete_cancel_,delete_confirm_}, delete_cancel_, items_add, [](bool) {}).value.value();
    item_name.connect_source({[this] { return item_name_source_; }, [this](auto changed) { item_name_changed_=std::move(changed); return [this] { item_name_changed_={}; }; }});
    item_note.connect_source({[this] { return item_note_source_; }, [this](auto changed) { item_note_changed_=std::move(changed); return [this] { item_note_changed_={}; }; }});
    auto open_item = [this](bool edit) -> Result {
        editing_=edit;
        auto found=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
        if (edit && found==items_.end()) return {Status::invalid_value,u"No selection"};
        item_title_.connect_source([edit] { return edit ? u"\u7de8\u8f2f\u9805\u76ee" : u"\u65b0\u589e\u9805\u76ee"; });
        auto result=item_dialog.open(); if (!result) return result;
        item_name_source_=edit ? found->name : u""; item_note_source_=edit ? found->note : u"";
        if (item_name_changed_) item_name_changed_(); if (item_note_changed_) item_note_changed_();
        if (auto r = item_name.reset_to_source(); !r) return r;
        if (auto r = item_note.reset_to_source(); !r) return r;
        return item_name.set_error(u"");
    };
    items_add.bind([open_item] { return open_item(false); });
    items_edit.bind([open_item] { return open_item(true); });
    item_cancel_.bind([this] { return item_dialog.settle(false); });
    ui.connect_confirmation(item_ok, {item_name, item_note}, [this](const std::vector<TextChange>& changes) -> ConfirmationResult {
        const auto& value=changes[0].value; const auto& note=changes[1].value;
        if (std::all_of(value.begin(),value.end(),[](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            return {{Status::invalid_value,u"Name required"}, {{item_name,u"\u8acb\u586b\u5beb\u540d\u7a31"}}};
        }
        OptionValue id;
        bool changed=false;
        if (editing_) {
            auto it=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
            if (it==items_.end()) return {{Status::not_found,u"Item not found"}, {}};
            id=it->id; changed=it->name!=value || it->note!=note; it->name=value; it->note=note;
        } else {
            char buffer[16]; std::snprintf(buffer,sizeof(buffer),"T%03d",next_item_id_++); id={buffer}; items_.push_back({id,value,note}); changed=true;
        }
        auto result=item_dialog.settle(true); if (!result) return {result, {}};
        if (fold_ascii(value).find(fold_ascii(search_value_))==std::u16string::npos) {
            if (auto r=items_search.set_value(u""); !r) return {r, {}};
        }
        if (auto r=refresh_items(); !r) return {r, {}};
        if (auto r=items_list.select(id); !r) return {r, {}};
        if (changed) dirty(); return {Result{}, {}};
    });
    items_delete.bind([this] {
        auto it=std::find_if(items_.begin(),items_.end(),[this](const Item& x) { return selection_ && x.id==*selection_; });
        if (it==items_.end()) return Result{Status::invalid_value,u"No selection"};
        auto message=u"\u8981\u522a\u9664\u300c" + it->name + u"\u300d\u55ce\uff1f\u522a\u9664\u5f8c\u53ef\u4ee5\u6309\u300c\u5fa9\u539f\u300d\u627e\u56de\u3002";
        delete_message_.connect_source([message] { return message; }); return delete_dialog.open();
    });
    delete_cancel_.bind([this] { return delete_dialog.settle(false); });
    delete_confirm_.bind([this] {
        auto selected=selection_; auto result=delete_dialog.settle(true); if (!result) return result;
        std::erase_if(items_,[&](const Item& item) { return selected && item.id==*selected; });
        if (auto r=refresh_items(); !r) return r;
        dirty(); return Result{};
    });
    ui.add_shortcut({.key=Key::enter},items_edit,items_list);
    ui.add_shortcut({.key=Key::del},items_delete,items_list);
    ui.add_shortcut({.letter=u'S',.ctrl=true},save_);
    ui.add_shortcut({.letter=u'Z',.ctrl=true},undo_);
    ui.add_shortcut({.letter=u'Y',.ctrl=true},redo_);
    ui.add_shortcut({.letter=u'Z',.ctrl=true,.shift=true},redo_);
    current_ = snapshot(); saved_ = current_;
    undo_.bind([this] {
        auto description = name.describe();
        if (description && !description->error.empty()) {
            auto result=name.reset_to_source(); if (!result) return result;
            name.set_error(u""); update_history_controls(); return Result{};
        }
        if (undo_history_.empty()) return Result{Status::empty_history, {}};
        redo_history_.push_back(snapshot());
        auto previous = undo_history_.back(); undo_history_.pop_back();
        apply(previous); return Result{};
    });
    redo_.bind([this] {
        if (redo_history_.empty()) return Result{Status::empty_history, {}};
        undo_history_.push_back(snapshot());
        auto next = redo_history_.back(); redo_history_.pop_back();
        apply(next); return Result{};
    });
    save_.bind([this] {
        auto draft = name.draft();
        if (!draft) return Result{draft.status, draft.message};
        if (std::all_of(draft->begin(), draft->end(), [](char16_t c) { return c==u' ' || c==u'\t' || c==u'\u3000'; })) {
            nav.select({"general"}); name.set_error(u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"); name.focus();
            return Result{Status::invalid_value, u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31"};
        }
        if (name_value_ != *draft) { name_value_=*draft; if (name_changed_) name_changed_(); dirty(); }
        saved_ = snapshot(); saved_once_ = true; update_history_controls(); return Result{};
    });
    revert_.bind([this] {
        name.reset_to_source();
        if (saved_ && snapshot() != *saved_) { auto target=*saved_; auto before=snapshot(); apply(target); current_=before; dirty(); }
        update_history_controls(); return Result{};
    });
    ui.on_change([this] { update_history_controls(); });
    nav.focus();
}
Result Demo::refresh_items() {
    auto query = fold_ascii(search_value_);
    std::vector<RadioOption> visible;
    for (const auto& item : items_) if (fold_ascii(item.name).find(query) != std::u16string::npos) visible.push_back({item.id,item.name});
    auto count = visible.size();
    auto old_count = items_count_.describe();
    if (!old_count) return {old_count.status, old_count.message};
    auto number = [](std::size_t n) { auto str=std::to_string(n); return std::u16string(str.begin(),str.end()); };
    auto text = u"\u5171 " + number(items_.size()) + u" \u7b46\uff0c\u986f\u793a " + number(count) + u" \u7b46";
    if (items_.size() >= 1000) text += u"\uff08\u5df2\u9054\u4e0a\u9650\uff09";
    if (auto r=items_count_.connect_source([text] { return text; }); !r) return r;
    if (auto r=items_list.set_items(std::move(visible)); !r) {
        auto previous = old_count->value;
        items_count_.connect_source([previous] { return previous; });
        return r;
    }
    return items_add.enable(items_.size()<1000);
}
void Demo::refresh_fonts() {
    std::vector<RadioOption> options{{{"default"},u"\u9810\u8a2d\uff08Segoe UI\uff0b\u5fae\u8edf\u6b63\u9ed1\u9ad4\uff09"}};
    auto key = ui.presentation().font_family;
    bool found = key.empty();
    for (const auto& family : about_.fonts) {
        options.push_back({{"family:" + std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.to_bytes(family.key)}, family.display_name});
        found = found || family.key == key;
    }
    if (!found) options.push_back({{"family:" + std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.to_bytes(key)}, key + u"\uff08\u672a\u5b89\u88dd\uff09"});
    font.set_options(std::move(options));
    auto resolution = about_.resolve_font ? about_.resolve_font(ui.presentation()) : FontResolution{key, key.empty() ? u"Segoe UI" : key, found, {}};
    auto status = resolution.requested_available ? u"\u76ee\u524d\u5b57\u578b\uff1a" + resolution.primary : u"\u627e\u4e0d\u5230\u300c" + key + u"\u300d\u5b57\u578b\uff0c\u66ab\u6642\u6539\u7528\u9810\u8a2d\u5b57\u578b\u3002\u8a2d\u5b9a\u4ecd\u6703\u4fdd\u7559\u3002";
    font_status_.connect_source([status] { return status; });
}
Result Demo::set_presentation(const PresentationSettings& settings, bool coalesce) {
    auto result=ui.set_presentation(settings); if (result) { refresh_fonts(); if (font_changed_) font_changed_(); if (text_size_changed_) text_size_changed_(); if (dark_changed_) dark_changed_(); if (compact_changed_) compact_changed_(); dirty(coalesce); } return result;
}
void Demo::on_dirty_changed(std::function<void(bool)> callback) { dirty_changed_ = std::move(callback); if (dirty_changed_) dirty_changed_(dirty_state_); }
SettingsDocument Demo::snapshot() const {
    return {name_value_, notices_value_, mode_value_, region_value_, volume_value_, items_, next_item_id_, ui.presentation()};
}
void Demo::update_history_controls() {
    auto description = name.describe();
    undo_.enable(!undo_history_.empty() || (description && !description->error.empty())); redo_.enable(!redo_history_.empty());
    auto draft = name.draft();
    bool changed = saved_ && (snapshot() != *saved_ || (draft && *draft != name_value_));
    save_.enable(changed); revert_.enable(changed);
    if (changed != dirty_state_) { dirty_state_=changed; if (dirty_changed_) dirty_changed_(changed); }
    status_.connect_source([changed, this] { return changed ? u"\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539" : saved_once_ ? u"\u6240\u6709\u4fee\u6539\u5df2\u5132\u5b58" : u"\u4f7f\u7528\u9810\u8a2d\u503c\uff0c\u5c1a\u672a\u5efa\u7acb\u8a2d\u5b9a\u6a94"; });
}
void Demo::dirty(bool coalesce) {
    if (!coalesce) { volume_group_=false; text_size_group_=false; }
    auto now = snapshot();
    if (current_ && *current_ != now) {
        if (!coalesce) undo_history_.push_back(*current_);
        if (undo_history_.size() > 100) undo_history_.erase(undo_history_.begin());
        redo_history_.clear();
    }
    current_ = std::move(now); update_history_controls();
}
void Demo::apply(const SettingsDocument& value) {
    volume_group_=false; text_size_group_=false;
    name_value_=value.name; notices_value_=value.notices; mode_value_=value.mode; region_value_=value.region;
    volume_value_=value.volume; items_=value.items; next_item_id_=value.next_item_id;
    if (name_changed_) name_changed_(); if (notices_changed_) notices_changed_();
    if (mode_changed_) mode_changed_(); if (region_changed_) region_changed_(); if (volume_changed_) volume_changed_();
    ui.set_presentation(value.presentation); refresh_fonts(); if (font_changed_) font_changed_(); if (text_size_changed_) text_size_changed_(); if (dark_changed_) dark_changed_(); if (compact_changed_) compact_changed_(); refresh_items(); current_=value; update_history_controls();
}
void Demo::set_renderer(std::u16string value) { about_.renderer = std::move(value); }
layout::Node Demo::page() const {
    using namespace layout;
    std::vector<Node> content{control(title_,24)};
    if (nav.selected()->id == "general") {
        content.push_back(control(name)); content.push_back(control(notices)); content.push_back(control(mode)); content.push_back(control(region)); content.push_back(control(volume));
    }
    if (nav.selected()->id == "items") {
        content.push_back(control(items_search)); content.push_back(control(items_list)); content.push_back(control(items_count_));
        content.push_back(row({control(items_add),control(items_edit),control(items_delete)},8));
    }
    if (nav.selected()->id == "appearance") { content.push_back(control(font)); content.push_back(control(font_status_)); content.push_back(control(text_size)); content.push_back(control(dark)); content.push_back(control(compact)); content.push_back(control(preview_)); content.push_back(control(reset_appearance_)); }
    if (nav.selected()->id == "about") {
        for (auto label : about_labels_) content.push_back(control(label));
        content.push_back(control(licenses));
        content.push_back(scroll_text(license_text_, license_scroll, 200));
    }
    content.push_back(control(status_));
    content.push_back(row({control(undo_),control(redo_),control(revert_),control(save_)},8));
    auto background=layout::scroll(row({control(nav),column(std::move(content),12,24)},16));
    return layout::modal(std::move(background), {
        {item_dialog,column({control(item_title_,24),control(item_name),control(item_note),row({control(item_cancel_),control(item_ok)},8)},12)},
        {delete_dialog,column({control(delete_title_,24),control(delete_message_),row({control(delete_cancel_),control(delete_confirm_)},8)},12)}
    });
}
}

