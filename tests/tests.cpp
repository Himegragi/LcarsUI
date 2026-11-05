#include "core_internal.hpp"
#include "settings.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace lcars;
namespace {
int checks=0;
void check(bool condition,const char* name){++checks;if(!condition)throw std::runtime_error(name);}
void core_behavior(){
    Ui ui({{"name",Kind::text,u"名稱","main",u"繁體中文"},
           {"locked",Kind::text,u"唯讀","main",u"keep",true,true},
           {"run",Kind::button,u"執行","main",{}},
           {"cancel",Kind::button,u"取消","dialog",{}}});
    int calls=0;detail::access(ui).bind("run",[&]{++calls;return Result{};});
    check(detail::access(ui).set_value("absent",u"x").status==Status::not_found,"Unknown ID must fail");
    check(detail::access(ui).set_value("locked",u"x").status==Status::read_only,"Read-only write must fail");
    check(detail::access(ui).set_value("name",u"line\nbreak").status==Status::invalid_value,"Multiline must fail");
    check(detail::access(ui).set_value("name",std::u16string(1,0xd800)).status==Status::invalid_value,"Malformed UTF16 must fail");
    check(detail::access(ui).set_value("name",std::u16string(4097,u'x')).status==Status::invalid_value,"Oversize text must fail");
    check(detail::access(ui).find("name")->value==u"繁體中文","Rejected writes must preserve value");
    auto* session=detail::access(ui).edit("name");session->select(1,3);session->start_composition();session->update_composition(u"測",1);
    check(session->visible()==u"繁測文"&&session->text==u"繁體中文","Preedit must not replace committed text");
    check(detail::access(ui).set_value("name",u"x").status==Status::busy,"Semantic write must not interrupt composition");
    session->cancel_composition();
    check(session->text==u"繁體中文"&&session->selection.anchor==1&&session->selection.caret==3,"Cancel must retain committed text and selection");
    detail::access(ui).modal(true);
    check(detail::access(ui).invoke("run").status==Status::blocked_by_modal&&calls==0,"Modal must block commands");
    check(detail::access(ui).set_value("name",u"x").status==Status::blocked_by_modal,"Modal must block writes");
    detail::access(ui).modal(false);detail::access(ui).enable("run",false);
    check(detail::access(ui).invoke("cancel").status==Status::disabled,"Hidden dialog actions must not execute");
    check(detail::access(ui).invoke("run").status==Status::disabled,"Disabled command must fail");
    detail::access(ui).enable("run",true);check(bool(detail::access(ui).invoke("run"))&&calls==1,"Enabled command executes once");
    Status background=Status::ok;
    std::thread worker([&]{background=detail::access(ui).set_value("name",u"wrong").status;});worker.join();
    check(background==Status::wrong_thread&&session->text==u"繁體中文","Wrong-thread mutation must fail before touching state");
    check(bool(detail::access(ui).set_value("name",u"A\U00020000B")),"Valid surrogate pair must succeed");
    session->select(2,2);check(session->selection.caret==1,"Selection cannot split surrogate pair");
    check(detail::access(ui).edit("name")==session,"Persistent edit identity must survive value changes");
    ui.on_change([&]{check(detail::access(ui).set_value("name",u"recursive").status==Status::busy,"Reentrant change must be rejected");});
    check(bool(detail::access(ui).set_value("name",u"new")),"Normal callback must finish");
    detail::access(ui).bind("run",[&]{return detail::access(ui).invoke("run");});
    check(detail::access(ui).invoke("run").status==Status::busy&&!detail::access(ui).stopped(),"Recursive commands must be rejected without stopping UI");
    detail::access(ui).bind("run",[]{throw std::runtime_error("extension");return Result{};});
    check(detail::access(ui).invoke("run").status==Status::failed&&detail::access(ui).stopped(),"Throwing extension stops the affected UI");
    check(detail::access(ui).set_value("name",u"unsafe").status==Status::failed,"Stopped UI rejects new work");
    bool duplicate=false;try{Ui bad({{"x",Kind::button,u"","",{}},{"x",Kind::button,u"","",{}}});}catch(const std::invalid_argument&){duplicate=true;}
    check(duplicate,"Duplicate IDs must be rejected");
}
void settings_workflow(const std::filesystem::path& folder){
    const auto path=folder/u8"設定.conf";settings::App app(path);
    check(!app.dirty()&&detail::access(app.ui).invoke("save").status==Status::disabled,"New app starts clean");
    detail::access(app.ui).set_value("profile.name",u"繁體中文 = Work \U00020000");
    detail::access(app.ui).set_value("profile.workspace",u"我的工作區");
    check(app.dirty()&&detail::access(app.ui).find("save")->enabled,"Editing enables save");
    check(bool(detail::access(app.ui).invoke("save"))&&!app.dirty(),"Save clears dirty only on success");
    settings::App reopened(path);
    check(reopened.values()==app.values(),"Restart must restore UTF8 settings");
    detail::access(app.ui).invoke("preferences.font");detail::access(app.ui).invoke("preferences.larger");
    check(app.values().ming_font&&app.values().font_size==20&&app.dirty(),"Presentation preferences participate in dirty state");
    detail::access(app.ui).invoke("undo");
    check(app.values().font_size==18&&app.values().ming_font,"Undo last font-size change");
    detail::access(app.ui).invoke("undo");check(!app.values().ming_font&&!app.dirty(),"Undo to save point must clear dirty");
    detail::access(app.ui).invoke("redo");check(app.values().ming_font,"Redo font choice");
    detail::access(app.ui).set_value("profile.name",u"Different");check(!detail::access(app.ui).find("redo")->enabled,"New change invalidates redo");
    detail::access(app.ui).invoke("reset");
    check(detail::access(app.ui).modal(),"Reset opens in-window dialog");
    check(detail::access(app.ui).invoke("save").status==Status::blocked_by_modal,"Save is blocked behind dialog");
    detail::access(app.ui).invoke("dialog.cancel");check(app.values().name==u"Different","Cancel preserves draft");
    detail::access(app.ui).invoke("reset");detail::access(app.ui).invoke("dialog.confirm");check(app.values()==settings::Values{},"Confirm defaults updates all fields");
    detail::access(app.ui).invoke("undo");check(app.values().name==u"Different","Reset is one undo transaction");
    detail::access(app.ui).set_value("search",u"字型");check(detail::access(app.ui).find("search")->value==u"字型","Search accepts Chinese");
    auto current=app.values();detail::access(app.ui).invoke("nav.preferences");
    check(app.values()==current&&detail::access(app.ui).find("search")->value.empty(),"Navigation preserves draft and clears search");
    detail::access(app.ui).set_value("profile.name",u"");
    check(detail::access(app.ui).invoke("save").status==Status::invalid_value&&app.dirty(),"Invalid save preserves dirty draft");
    settings::App intact(path);check(intact.values()==reopened.values(),"Invalid save must not overwrite previous file");
    detail::access(app.ui).invoke("discard");detail::access(app.ui).invoke("dialog.confirm");
    check(!app.dirty()&&app.values()==reopened.values(),"Discard restores last successful save");
    detail::access(app.ui).invoke("undo");check(app.values().name.empty(),"Discard can be undone");
}
void persistence_failures(const std::filesystem::path& folder){
    auto path=folder/"io.conf";settings::Values good;check(bool(settings::write_file(path,good)),"Create first save");
    auto temp=path;temp+=L".tmp";{std::ofstream out(temp);out<<"someone else's temporary file";}
    auto change=good;change.name=u"changed";
    check(!settings::write_file(path,change),"Existing temporary file must block save");
    settings::Values loaded;check(bool(settings::read_file(path,loaded))&&loaded==good,"Temp collision preserves old settings");
    std::ifstream temp_input(temp);std::string temp_text;std::getline(temp_input,temp_text);
    check(temp_text=="someone else's temporary file","Failed save must not remove another writer's file");
    temp_input.close();std::filesystem::remove(temp);
    auto broken=folder/"broken.conf";
    for(auto payload:{"LcarsUI settings 1\nname=x\n","LcarsUI settings 2\n","LcarsUI settings 1\nname=x\nname=y\n"}){
        {std::ofstream out(broken,std::ios::binary);out<<payload;}
        loaded=good;loaded.name=u"unchanged";auto before=loaded;
        check(!settings::read_file(broken,loaded)&&loaded==before,"Bad file must fail transactionally");
    }
    auto directory=folder/"target-is-directory";std::filesystem::create_directory(directory);
    check(!settings::write_file(directory,good),"Destination directory is a write error");
    auto directory_temp=directory;directory_temp+=L".tmp";
    check(!std::filesystem::exists(directory_temp),"Owned failed temporary file is cleaned up");
    auto blocker=folder/"not-a-directory";{std::ofstream out(blocker);out<<"data";}
    settings::App failing(blocker/"settings.conf");detail::access(failing.ui).set_value("profile.name",u"dirty");
    check(!detail::access(failing.ui).invoke("save")&&failing.dirty(),"IO failure must keep pending changes");
}
void appearances(){
    auto& a=web_appearance();auto& b=material_appearance();
    for(int size:{16,18,24,26,39}){
        auto web=a.field({20,30,500,140},size),material=b.field({20,30,500,140},size);
        check(web.label.y<web.surface.y,"Web label is outside the field surface");
        check(material.label.y>material.surface.y&&material.underline,"Material label is inside filled field");
        for(auto parts:{web,material}){
            check(parts.editor.x>=parts.surface.x&&parts.editor.x+parts.editor.w<=parts.surface.x+parts.surface.w,"Editor stays in surface horizontally");
            check(parts.editor.y+parts.editor.h<=parts.surface.y+parts.surface.h,"Editor stays in surface vertically");
        }
    }
}
}
int main(){
    const auto folder=std::filesystem::temp_directory_path()/("lcars-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        check(std::filesystem::create_directory(folder),"Test directory must be newly created");
        core_behavior();settings_workflow(folder);persistence_failures(folder);appearances();
        check(std::filesystem::canonical(folder).parent_path()==std::filesystem::canonical(std::filesystem::temp_directory_path()),"Cleanup must stay in the temporary directory");
        std::filesystem::remove_all(folder);
        std::cout<<"PASS "<<checks<<" checks: semantic input, composition, history, modal, persistence, errors, appearances\n";return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\nArtifacts: "<<folder<<'\n';return 1;
    }
}
