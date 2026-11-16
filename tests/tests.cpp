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
void coreBehavior(){
    Ui ui;
    auto name = ui.addTextField({.id = "name", .value = u"\u7e41\u9ad4\u4e2d\u6587"}).value.value();
    auto locked = ui.addTextField({.id = "locked", .value = u"keep", .readOnly = true}).value.value();
    auto run = ui.addButton({.id = "run"}).value.value();
    ui.addButton({.id = "cancel", .page = "dialog"}).value.value();
    name.connectSource({[] { return std::u16string(u"\u7e41\u9ad4\u4e2d\u6587"); }, [](std::function<void()>) { return [] {}; }});
    name.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    locked.connectSource({[] { return std::u16string(u"keep"); }, [](std::function<void()>) { return [] {}; }});
    locked.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    int calls=0;run.bind([&]{++calls;return Result{};});
    check(detail::access(ui).setValue("absent",u"x").status==Status::notFound,"Unknown ID must fail");
    check(locked.setValue(u"x").status==Status::readOnly,"Read-only write must fail");
    check(name.setValue(u"line\nbreak").status==Status::invalidValue,"Multiline must fail");
    check(name.setValue(std::u16string(1,0xd800)).status==Status::invalidValue,"Malformed UTF16 must fail");
    check(name.setValue(std::u16string(4097,u'x')).status==Status::invalidValue,"Oversize text must fail");
    check(name.describe()->value==u"繁體中文","Rejected writes must preserve value");
    auto* session=detail::access(ui).edit("name");session->select(1,3);session->startComposition();session->updateComposition(u"測",1);
    check(session->visible()==u"繁測文"&&session->text==u"繁體中文","Preedit must not replace committed text");
    check(name.setValue(u"x").status==Status::busy,"Semantic write must not interrupt composition");
    session->cancelComposition();
    check(session->text==u"繁體中文"&&session->selection.anchor==1&&session->selection.caret==3,"Cancel must retain committed text and selection");
    detail::access(ui).modal(true);
    check(run.invoke().status==Status::blockedByModal&&calls==0,"Modal must block commands");
    check(name.setValue(u"x").status==Status::blockedByModal,"Modal must block writes");
    detail::access(ui).modal(false);detail::access(ui).enable("run",false);
    check(detail::access(ui).invoke("cancel").status==Status::hidden,"Hidden dialog actions must not execute");
    check(run.invoke().status==Status::disabled,"Disabled command must fail");
    detail::access(ui).enable("run",true);check(bool(run.invoke())&&calls==1,"Enabled command executes once");
    Status background=Status::ok;
    std::thread worker([&]{background=name.setValue(u"wrong").status;});worker.join();
    check(background==Status::wrongThread&&session->text==u"繁體中文","Wrong-thread mutation must fail before touching state");
    check(bool(name.setValue(u"A\U00020000B")),"Valid surrogate pair must succeed");
    session->select(2,2);check(session->selection.caret==1,"Selection cannot split surrogate pair");
    check(detail::access(ui).edit("name")==session,"Persistent edit identity must survive value changes");
    ui.onChange([&]{check(name.setValue(u"recursive").status==Status::busy,"Reentrant change must be rejected");});
    check(bool(name.setValue(u"new")),"Normal callback must finish");
    run.bind([&]{return run.invoke();});
    check(run.invoke().status==Status::busy&&!detail::access(ui).stopped(),"Recursive commands must be rejected without stopping UI");
    run.bind([]{throw std::runtime_error("extension");return Result{};});
    check(run.invoke().status==Status::failed&&detail::access(ui).stopped(),"Throwing extension stops the affected UI");
    check(name.setValue(u"unsafe").status==Status::failed,"Stopped UI rejects new work");
    Ui bad; bad.addButton({.id="x"}); bool duplicate = bad.addButton({.id="x"}).status == Status::invalidValue;
    check(duplicate,"Duplicate IDs must be rejected");
}
void rendererFailureAdmission() {
    Ui ui;
    auto name = ui.addTextField({.id="name", .value=u"draft"}).value.value();
    name.connectSource({[] { return std::u16string(u"draft"); }, [](std::function<void()>) { return [] {}; }});
    name.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    check(bool(detail::access(ui).focus("name")), "Initial focus succeeds");
    detail::access(ui).rendererFailed(true);
    check(rendererFailurePreservesDraft(name,u"draft"), "Failure rejects edits and preserves readable draft");
    check(detail::access(ui).setValue("missing", u"x").status == Status::notFound, "Missing ID precedes failure");
    check(detail::access(ui).keyDown({.key=Key::tab}).status == Status::rendererFailed, "Failure rejects focus traversal");
    check(name.enable(false).status == Status::rendererFailed, "Failure rejects disabling focused control");
    check(name.resetToSource().status == Status::rendererFailed, "Failure rejects resetting draft");
    check(detail::access(ui).focused() == "name", "Failure freezes focus");
    check(ui.addButton({.id="new"}).status == Status::rendererFailed, "Failure rejects adding controls");
    Status background=Status::ok;
    std::thread worker([&] { background=name.setValue(u"x").status; }); worker.join();
    check(background == Status::wrongThread, "Wrong thread precedes renderer failure");
    check(rendererFailureOmitsInteractiveVerbs(*name.describe()), "Failure omits focus, editing and selection verbs");
    auto settings = ui.presentation(); settings.textSize = 28; settings.variant = ThemeVariant::dark(); settings.density = Density::compact;
    check(bool(ui.setPresentation(settings)) && ui.presentation() == settings, "PS10 presentation accepted while failed");
    auto invalid = settings; invalid.textSize = 29;
    check(ui.setPresentation(invalid).status == Status::invalidValue && ui.presentation() == settings, "PS10 invalid presentation stays atomic");
    Status otherThread = Status::ok;
    std::thread presentationWorker([&] { otherThread = ui.setPresentation(settings).status; }); presentationWorker.join();
    check(otherThread == Status::wrongThread, "PS10 presentation remains UI-thread only");
    detail::access(ui).rendererFailed(false);
    check(bool(name.setValue(u"recovered")), "Recovery permits edits");
}
void appearances(){
    auto& a=webAppearance();auto& b=materialAppearance();
    for(int size:{16,18,24,26,39}){
        TextFieldHeights heights{size + 8, 0, 0, size + 4};
        auto web=a.arrangeTextField(500,heights,1.f),material=b.arrangeTextField(500,heights,1.f);
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
        coreBehavior(); rendererFailureAdmission(); appearances();
        std::cout << "PASS " << checks << " checks: core admission and appearance geometry\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1;
    }
}
