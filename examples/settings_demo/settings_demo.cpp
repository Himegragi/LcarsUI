#include "settings_demo.hpp"
#include <algorithm>
#include <cstdio>
using namespace lcars;
namespace settings_demo {
namespace {
std::u16string fold_ascii(std::u16string value) {
    for (auto& c : value) if (c >= u'A' && c <= u'Z') c += u'a' - u'A';
    return value;
}
}
Demo::Demo() {
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
    for (int n=1; n<=20; ++n) {
        char id[8], number[8]; std::snprintf(id,sizeof(id),"T%03d",n); std::snprintf(number,sizeof(number),"%02d",n);
        items_.push_back({{id}, n==5 || n==6 ? u"\u5171\u7528\u7bc4\u672c" : u"\u7bc4\u672c " + std::u16string(number,number+2), {}});
    }
    items_search.connect_source({[this] { return search_value_; }, [this](auto changed) { search_changed_=std::move(changed); return [this] { search_changed_={}; }; }});
    items_search.connect_immediate([this](const TextChange& c) { search_value_=c.value; if (search_changed_) search_changed_(); return refresh_items(); });
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
    name.on_blur([this] { name.confirm(); });
    notices.connect_source({[this] { return notices_value_; }, [this](std::function<void()> notify) { notices_changed_ = std::move(notify); return [this] { notices_changed_ = {}; }; }});
    notices.connect_immediate([this](const BoolChange& c) { notices_value_=c.value; if (notices_changed_) notices_changed_(); dirty(); return Result{}; });
    mode.connect_source({[this] { return mode_value_; }, [this](std::function<void()> notify) { mode_changed_ = std::move(notify); return [this] { mode_changed_ = {}; }; }});
    mode.connect_immediate([this](const OptionChange& c) { mode_value_=c.value; if (mode_changed_) mode_changed_(); dirty(); return Result{}; });
    region.connect_source({[this] { return region_value_; }, [this](std::function<void()> notify) { region_changed_ = std::move(notify); return [this] { region_changed_ = {}; }; }});
    region.connect_immediate([this](const DropdownChange& c) { region_value_=c.value; if (region_changed_) region_changed_(); dirty(); return Result{}; });
    volume.connect_source({[this] { return volume_value_; }, [this](std::function<void()> notify) { volume_changed_ = std::move(notify); return [this] { volume_changed_ = {}; }; }});
    volume.connect_immediate([this](const NumericChange& c) { volume_value_=c.value; if (volume_changed_) volume_changed_(); dirty(); return Result{}; });
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
    nav.focus();
}
Result Demo::refresh_items() {
    auto query = fold_ascii(search_value_);
    std::vector<RadioOption> visible;
    for (const auto& item : items_) if (fold_ascii(item.name).find(query) != std::u16string::npos) visible.push_back({item.id,item.name});
    auto count = visible.size();
    if (auto r=items_list.set_items(std::move(visible)); !r) return r;
    auto number = [](std::size_t n) { auto str=std::to_string(n); return std::u16string(str.begin(),str.end()); };
    auto text = u"\u5171 " + number(items_.size()) + u" \u7b46\uff0c\u986f\u793a " + number(count) + u" \u7b46";
    if (items_.size() >= 1000) text += u"\uff08\u5df2\u9054\u4e0a\u9650\uff09";
    if (auto r=items_count_.connect_source([text] { return text; }); !r) return r;
    return items_add.enable(items_.size()<1000);
}
void Demo::dirty() { undo_.enable(true); }
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
    content.push_back(control(status_));
    content.push_back(row({control(undo_),control(redo_),control(revert_),control(save_)},8));
    auto background=layout::scroll(row({control(nav),column(std::move(content),12,24)},16));
    return layout::modal(std::move(background), {
        {item_dialog,column({control(item_title_,24),control(item_name),control(item_note),row({control(item_cancel_),control(item_ok)},8)},12)},
        {delete_dialog,column({control(delete_title_,24),control(delete_message_),row({control(delete_cancel_),control(delete_confirm_)},8)},12)}
    });
}
}

