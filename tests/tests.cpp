#include "core_internal.hpp"
#include "renderer_failure_checks.hpp"
#include "lcars/appearance.hpp"
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
    Ui ui;
    auto name = ui.add_text({.id = "name", .kind = Kind::text, .value = u"\u7e41\u9ad4\u4e2d\u6587"}).value.value();
    auto locked = ui.add_text({.id = "locked", .kind = Kind::text, .value = u"keep", .read_only = true}).value.value();
    auto run = ui.add_button({.id = "run", .kind = Kind::button}).value.value();
    ui.add_button({.id = "cancel", .kind = Kind::button, .page = "dialog"}).value.value();
    name.connect_source({[] { return std::u16string(u"\u7e41\u9ad4\u4e2d\u6587"); }, [](std::function<void()>) { return [] {}; }});
    name.connect_confirmed([](const Change<TextHandle>&) { return Result{}; });
    locked.connect_source({[] { return std::u16string(u"keep"); }, [](std::function<void()>) { return [] {}; }});
    locked.connect_confirmed([](const Change<TextHandle>&) { return Result{}; });
    int calls=0;run.bind([&]{++calls;return Result{};});
    check(detail::access(ui).set_value("absent",u"x").status==Status::not_found,"Unknown ID must fail");
    check(locked.set_value(u"x").status==Status::read_only,"Read-only write must fail");
    check(name.set_value(u"line\nbreak").status==Status::invalid_value,"Multiline must fail");
    check(name.set_value(std::u16string(1,0xd800)).status==Status::invalid_value,"Malformed UTF16 must fail");
    check(name.set_value(std::u16string(4097,u'x')).status==Status::invalid_value,"Oversize text must fail");
    check(name.describe()->value==u"繁體中文","Rejected writes must preserve value");
    auto* session=detail::access(ui).edit("name");session->select(1,3);session->start_composition();session->update_composition(u"測",1);
    check(session->visible()==u"繁測文"&&session->text==u"繁體中文","Preedit must not replace committed text");
    check(name.set_value(u"x").status==Status::busy,"Semantic write must not interrupt composition");
    session->cancel_composition();
    check(session->text==u"繁體中文"&&session->selection.anchor==1&&session->selection.caret==3,"Cancel must retain committed text and selection");
    detail::access(ui).modal(true);
    check(run.invoke().status==Status::blocked_by_modal&&calls==0,"Modal must block commands");
    check(name.set_value(u"x").status==Status::blocked_by_modal,"Modal must block writes");
    detail::access(ui).modal(false);detail::access(ui).enable("run",false);
    check(detail::access(ui).invoke("cancel").status==Status::hidden,"Hidden dialog actions must not execute");
    check(run.invoke().status==Status::disabled,"Disabled command must fail");
    detail::access(ui).enable("run",true);check(bool(run.invoke())&&calls==1,"Enabled command executes once");
    Status background=Status::ok;
    std::thread worker([&]{background=name.set_value(u"wrong").status;});worker.join();
    check(background==Status::wrong_thread&&session->text==u"繁體中文","Wrong-thread mutation must fail before touching state");
    check(bool(name.set_value(u"A\U00020000B")),"Valid surrogate pair must succeed");
    session->select(2,2);check(session->selection.caret==1,"Selection cannot split surrogate pair");
    check(detail::access(ui).edit("name")==session,"Persistent edit identity must survive value changes");
    ui.on_change([&]{check(name.set_value(u"recursive").status==Status::busy,"Reentrant change must be rejected");});
    check(bool(name.set_value(u"new")),"Normal callback must finish");
    run.bind([&]{return run.invoke();});
    check(run.invoke().status==Status::busy&&!detail::access(ui).stopped(),"Recursive commands must be rejected without stopping UI");
    run.bind([]{throw std::runtime_error("extension");return Result{};});
    check(run.invoke().status==Status::failed&&detail::access(ui).stopped(),"Throwing extension stops the affected UI");
    check(name.set_value(u"unsafe").status==Status::failed,"Stopped UI rejects new work");
    Ui bad; bad.add_button({.id="x", .kind=Kind::button}); bool duplicate = bad.add_button({.id="x", .kind=Kind::button}).status == Status::invalid_value;
    check(duplicate,"Duplicate IDs must be rejected");
}
void renderer_failure_admission() {
    Ui ui;
    auto name = ui.add_text({.id="name", .kind=Kind::text, .value=u"draft"}).value.value();
    name.connect_source({[] { return std::u16string(u"draft"); }, [](std::function<void()>) { return [] {}; }});
    name.connect_confirmed([](const Change<TextHandle>&) { return Result{}; });
    check(bool(detail::access(ui).focus("name")), "Initial focus succeeds");
    detail::access(ui).renderer_failed(true);
    check(renderer_failure_preserves_draft(name,u"draft"), "Failure rejects edits and preserves readable draft");
    check(detail::access(ui).set_value("missing", u"x").status == Status::not_found, "Missing ID precedes failure");
    check(detail::access(ui).key_down({.key=Key::tab}).status == Status::renderer_failed, "Failure rejects focus traversal");
    check(name.enable(false).status == Status::renderer_failed, "Failure rejects disabling focused control");
    check(name.reset_to_source().status == Status::renderer_failed, "Failure rejects resetting draft");
    check(detail::access(ui).focused() == "name", "Failure freezes focus");
    check(ui.add_button({.id="new",.kind=Kind::button}).status == Status::renderer_failed, "Failure rejects adding controls");
    Status background=Status::ok;
    std::thread worker([&] { background=name.set_value(u"x").status; }); worker.join();
    check(background == Status::wrong_thread, "Wrong thread precedes renderer failure");
    check(name.describe()->operations.empty(), "Failure advertises no operations");
    auto settings = ui.presentation(); settings.text_size = 28; settings.variant = ThemeVariant::dark(); settings.density = Density::compact;
    check(bool(ui.set_presentation(settings)) && ui.presentation() == settings, "PS10 presentation accepted while failed");
    auto invalid = settings; invalid.text_size = 29;
    check(ui.set_presentation(invalid).status == Status::invalid_value && ui.presentation() == settings, "PS10 invalid presentation stays atomic");
    Status other_thread = Status::ok;
    std::thread presentation_worker([&] { other_thread = ui.set_presentation(settings).status; }); presentation_worker.join();
    check(other_thread == Status::wrong_thread, "PS10 presentation remains UI-thread only");
    detail::access(ui).renderer_failed(false);
    check(bool(name.set_value(u"recovered")), "Recovery permits edits");
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
    try {
        core_behavior(); renderer_failure_admission(); appearances();
        std::cout << "PASS " << checks << " checks: core admission and appearance geometry\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1;
    }
}
