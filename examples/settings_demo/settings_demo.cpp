#include "settings_demo.hpp"
#include <algorithm>
using namespace lcars;
namespace settings_demo {
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
    nav.focus();
}
void Demo::dirty() { undo_.enable(true); }
layout::Node Demo::page() const {
    using namespace layout;
    std::vector<Node> content{control(title_,24)};
    if (nav.selected()->id == "general") {
        content.push_back(control(name)); content.push_back(control(notices)); content.push_back(control(mode)); content.push_back(control(region)); content.push_back(control(volume));
    }
    content.push_back(control(status_));
    content.push_back(row({control(undo_),control(redo_),control(revert_),control(save_)},8));
    return layout::scroll(row({control(nav),column(std::move(content),12,24)},16));
}
}

