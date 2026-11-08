#include "desktop_host.hpp"
#include "core_internal.hpp"
#include "edit_form.hpp"
#include "renderer_failure_checks.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <thread>
using namespace lcars;
using namespace lcars::detail;
static void check(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
static void diagnostics(const Appearance& appearance) {
    edit_form::Form form;
    RendererTestHooks hooks; hooks.hide_khr_debug=true;
    auto created=create_desktop_for_test(form.ui,appearance,[&]{return form.page();},form.scroll,{},&hooks);
    check(created.desktop && !created.desktop->capabilities().debug_messages, "DG02 hidden debug");
    edit_form::Form debug_form;
    RendererTestHooks debug_hooks; debug_hooks.misuse_once=true;
    DesktopOptions options; options.debug_context=DebugContext::on;
    std::vector<RenderDiagnostic> events; auto owner=std::this_thread::get_id(); bool right_thread=true;
    options.on_diagnostic=[&](const RenderDiagnostic& event) { right_thread &= std::this_thread::get_id()==owner; events.push_back(event); };
    DesktopHost host(debug_form.ui,appearance,[&]{return debug_form.page();},debug_form.scroll,options,false,true,&debug_hooks);
    check(host.capabilities().debug_messages,"DG01 KHR_debug available");
    host.paint(); host.pump();
    bool found=false;
    for (const auto& event:events) if (event.category==DiagnosticCategory::api_misuse && event.backend==u"OpenGL" && event.native_code && !event.native_message.empty() && !event.source_time && event.received_at.time_since_epoch().count()) found=true;
    check(found && right_thread,"DG01 debug event");
}
static void startup(const Appearance& appearance) {
    diagnostics(appearance);
    for (int mode=0; mode<2; ++mode) {
        edit_form::Form form;
        RendererTestHooks hooks; hooks.hide_khr_debug=true;
        if (mode==0) hooks.fragment_shader_override="invalid shader"; else hooks.fail_context_33=true;
        auto created=create_desktop_for_test(form.ui,appearance,[&]{return form.page();},form.scroll,{},&hooks);
        auto e=created.failure;
        bool rejected = !created.desktop && e && (mode==0 ? e->kind==StartupFailureKind::shader && !e->message.empty()
                : e->kind==StartupFailureKind::context_version && e->required==u"OpenGL 3.3 core" && !e->actual.empty());
        check(rejected,mode==0 ? "RF01" : "RF02");
    }
    edit_form::Form form;
    RendererTestHooks hooks; hooks.hide_robustness=true;
    auto created=create_desktop_for_test(form.ui,appearance,[&]{return form.page();},form.scroll,{},&hooks);
    check(created.desktop && !created.desktop->capabilities().reset_detection, "RF05 unavailable reset detection");
}
static void public_lifecycle(const Appearance& appearance, bool handled) {
    edit_form::Form form;
    edit_form::Form other;
    DesktopHost other_host(other.ui,appearance,[&]{return other.page();},other.scroll,{},false,true);
    check(bool(form.handles.name.set_value(u"workstation A")), "prepare public draft");
    RendererTestHooks hooks; hooks.gl_error_at_frame=1; hooks.fail_rebuild_once=handled;
    DesktopOptions options;
    int callbacks=0;
    if (handled) options.on_renderer_failed=[&](Desktop& desktop, const RendererFailure& failure) {
        ++callbacks;
        check(failure.kind==RendererFailureKind::frame_error, "public failure kind");
        check(renderer_failure_preserves_draft(form.handles.name,u"workstation A"), "public draft protected");
        check(desktop.rebuild_renderer().status==Status::failed && desktop.renderer_failed(), "public rebuild failure");
        hooks.gl_error_at_frame=0;
        check(bool(desktop.rebuild_renderer()), "public rebuild succeeds");
        PostQuitMessage(0);
    };
    auto created=create_desktop_for_test(form.ui,appearance,[&]{return form.page();},form.scroll,options,&hooks);
    check(bool(created.desktop), "public desktop creation");
    auto result=created.desktop->run();
    check(handled ? result.status==Status::ok && callbacks==1 : result.status==Status::renderer_failed && result.failure && result.failure->kind==RendererFailureKind::frame_error, "public run result");
    check(form.handles.name.draft().value==u"workstation A", "public retained draft");
    check(IsWindow(other_host.window()) != FALSE, "other host remains open");
    if (!handled) check(form.handles.name.set_value(u"lost").status==Status::renderer_failed, "closed host preserves failure");
}
static void failure(const Appearance& appearance, bool reset, bool handled, bool modal = false) {
    edit_form::Form form(edit_form::default_ids(), true);
    auto original=form.handles.name.draft().value;
    check(bool(form.handles.name.set_value(u"workstation A")), "prepare draft");
    check(bool(form.handles.name.focus()) && bool(form.handles.name.select(0,11)), "prepare selection");
    if (modal) check(bool(form.handles.dialog.open(form.handles.name)), "prepare modal");
    auto selection=access(form.ui).edit(form.ids().name)->selection;
    RendererTestHooks hooks; hooks.hide_khr_debug=true; hooks.gl_error_at_frame=reset?0:2; hooks.reset_at_frame=reset?2:0; hooks.fail_rebuild_once=handled;
    DesktopHost host(form.ui,appearance,[&]{return form.page();},form.scroll,{},false,false,&hooks);
    auto before=host.compare_gpu(640,480,1.f);
    check(before.passed, "pre-failure comparison");
    int callbacks=0;
    const auto owner=std::this_thread::get_id();
    if (handled) host.on_renderer_failure([&](const RendererFailure& f) {
        ++callbacks;
        check(std::this_thread::get_id()==owner, "notification on UI thread");
        check(f.kind==(reset?RendererFailureKind::device_reset:RendererFailureKind::frame_error), "failure kind");
        check(renderer_failure_preserves_draft(form.handles.name,u"workstation A"), "failed edit preserves draft");
        check(access(form.ui).edit(form.ids().name)->selection.anchor==selection.anchor && access(form.ui).edit(form.ids().name)->selection.caret==selection.caret, "selection preserved");
        check(form.handles.name.describe()->operations.empty(), "failed operations omitted");
        check(form.handles.dialog.open().status==Status::renderer_failed, "dialog blocked");
        check(host.rebuild_renderer().status==Status::failed && host.renderer_failed(), "RF07 failed rebuild");
    });
    host.paint();
    check(host.renderer_failed(), "immediate failure");
    SendMessageW(host.window(),WM_CHAR,L'X',0);
    SendMessageW(host.window(),WM_KEYDOWN,VK_BACK,0);
    SendMessageW(host.window(),WM_KEYDOWN,VK_TAB,0);
    check(form.handles.name.draft().value==u"workstation A", "native input blocked");
    if (handled && std::getenv("CI")) {
        ShowWindow(host.window(),SW_SHOW); SetForegroundWindow(host.window()); SetFocus(host.window());
        check(GetForegroundWindow()==host.window(), "RF03 foreground window");
        INPUT inputs[4]{};
        inputs[0].type=INPUT_KEYBOARD; inputs[0].ki.wVk='X';
        inputs[1]=inputs[0]; inputs[1].ki.dwFlags=KEYEVENTF_KEYUP;
        inputs[2].type=INPUT_KEYBOARD; inputs[2].ki.wVk=VK_BACK;
        inputs[3]=inputs[2]; inputs[3].ki.dwFlags=KEYEVENTF_KEYUP;
        check(SendInput(4,inputs,sizeof(INPUT))==4, "RF03 SendInput");
        host.pump();
        check(form.handles.name.draft().value==u"workstation A", "RF03 SendInput blocked");
    }
    if (handled) {
        host.pump(); check(callbacks==1, "single notification");
        if (!reset && !modal) {
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (std::chrono::steady_clock::now()<deadline) { host.pump(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
            check(hooks.rebuild_attempts==1 && host.renderer_failed(), "RF07 no automatic retry");
        }
        check(bool(host.rebuild_renderer()) && !host.renderer_failed(), "RF07 recovery");
        host.paint(); check(!host.renderer_failed(), "recovered paint");
        auto after=host.compare_gpu(640,480,1.f);
        check(after.passed && compare_images(before.actual,after.actual,{}).different_pixels*200 <= 640u*480u, "recovered image");
        if (!modal) check(bool(form.handles.name.undo()) && form.handles.name.draft().value==original, "undo history preserved");
    }
    else { host.run(); check(host.renderer_failure()->kind==RendererFailureKind::frame_error, "RF04 result"); host.close(); check(form.handles.name.describe()->value==u"workstation A", "RF04 retained draft"); }
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--gdi-version") {
            edit_form::Form form;
            auto result=Desktop::create(form.ui,web_appearance(),[&]{return form.page();},form.scroll);
            check(result.failure && result.failure->kind==StartupFailureKind::context_version &&
                  result.failure->actual.find(u"GDI Generic")!=std::u16string::npos, "RF02 GDI Generic");
            return 0;
        }
        for (auto appearance : {&web_appearance(), &material_appearance()}) {
            startup(*appearance); failure(*appearance,false,true); failure(*appearance,false,true,true); failure(*appearance,true,true); failure(*appearance,false,false);
            public_lifecycle(*appearance,true); public_lifecycle(*appearance,false);
        }
        std::cout << "RF01-RF05, RF07 failure seam checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
