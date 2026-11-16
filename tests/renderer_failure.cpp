#include "desktop_host.hpp"
#include "core_internal.hpp"
#include "edit_form.hpp"
#include "lcars/fonts.hpp"
#include "renderer_failure_checks.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <thread>
using namespace lcars;
using namespace lcars::detail;
static void check(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
static void diagnostics(const Appearance& appearance) {
    editForm::Form form;
    RendererTestHooks hooks; hooks.hideKhrDebug=true;
    auto created=DesktopHost::createForTest(form.ui,appearance,[&]{return form.page();},form.scroll,{},&hooks);
    check(created.desktop && !created.desktop->capabilities().debugMessages, "DG02 hidden debug");
    editForm::Form debugForm;
    RendererTestHooks debugHooks; debugHooks.misuseOnce=true;
    DesktopConfig options; options.debugContext=DebugContext::on;
    std::vector<RenderDiagnostic> events; auto owner=std::this_thread::get_id(); bool rightThread=true;
    options.onDiagnostic=[&](const RenderDiagnostic& event) { rightThread &= std::this_thread::get_id()==owner; events.push_back(event); };
    DesktopHost host(debugForm.ui,appearance,[&]{return debugForm.page();},debugForm.scroll,options,false,true,&debugHooks);
    check(host.capabilities().debugMessages,"DG01 KHR_debug available");
    host.paint(); host.pump();
    bool found=false;
    for (const auto& event:events) if (event.kind==DiagnosticKind::apiMisuse && event.backend==u"OpenGL" && event.nativeCode && !event.nativeMessage.empty() && !event.sourceTime && event.receivedAt.time_since_epoch().count()) found=true;
    check(found && rightThread,"DG01 debug event");
}
static void startup(const Appearance& appearance) {
    diagnostics(appearance);
    {
        editForm::Form form;
        RendererTestHooks hooks; hooks.startupResourceError = "resource \xe4\xb8\xad\xe6\x96\x87";
        auto result = DesktopHost::createForTest(form.ui, appearance, [&] { return form.page(); }, form.scroll, {}, &hooks);
        check(!result.desktop && result.failure && result.failure->kind == StartupFailureKind::resource && result.failure->message == u"resource 中文", "startup resource error preserves kind and UTF-8");
    }
    for (int mode=0; mode<2; ++mode) {
        editForm::Form form;
        RendererTestHooks hooks; hooks.hideKhrDebug=true;
        if (mode==0) hooks.fragmentShaderOverride="invalid shader"; else hooks.failContext33=true;
        auto created=DesktopHost::createForTest(form.ui,appearance,[&]{return form.page();},form.scroll,{},&hooks);
        auto e=created.failure;
        bool rejected = !created.desktop && e && (mode==0 ? e->kind==StartupFailureKind::shader && !e->message.empty()
                : e->kind==StartupFailureKind::contextVersion && e->required==u"OpenGL 3.3 core" && !e->actual.empty());
        check(rejected,mode==0 ? "RF01" : "RF02");
    }
    editForm::Form form;
    RendererTestHooks hooks; hooks.hideRobustness=true;
    auto created=DesktopHost::createForTest(form.ui,appearance,[&]{return form.page();},form.scroll,{},&hooks);
    check(created.desktop && !created.desktop->capabilities().resetDetection, "RF05 unavailable reset detection");
}
static void closeRequest(const Appearance& appearance) {
    editForm::Form form;
    int calls = 0;
    DesktopConfig options;
    options.onCloseRequested = [&](Desktop& desktop) {
        ++calls;
        SendMessageW(static_cast<HWND>(desktop.nativeWindow()), WM_CLOSE, 0, 0);
        return CloseDecision::keepOpen;
    };
    auto created = Desktop::create(form.ui, appearance, [&]{ return form.page(); }, form.scroll, options);
    check(bool(created.desktop), "close desktop creation");
    auto hwnd = static_cast<HWND>(created.desktop->nativeWindow());
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    check(calls == 1 && IsWindow(hwnd), "veto and reentrant close");
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    check(calls == 2 && IsWindow(hwnd), "system close goes through same veto");
    SendMessageW(hwnd, WM_SYSKEYDOWN, VK_F4, 1LL << 29);
    check(calls == 3 && IsWindow(hwnd), "Alt F4 goes through same veto");
    Status status = Status::ok;
    std::thread worker([&] { status = created.desktop->close().status; }); worker.join();
    check(status == Status::wrongThread, "close requires UI thread");
    check(bool(created.desktop->close()), "explicit close");
    check(created.desktop->run().status == Status::ok, "explicit close run result");
    editForm::Form defaultForm;
    auto automatic=Desktop::create(defaultForm.ui, appearance, [&]{ return defaultForm.page(); }, defaultForm.scroll);
    check(bool(automatic.desktop), "default close desktop");
    PostMessageW(static_cast<HWND>(automatic.desktop->nativeWindow()), WM_CLOSE, 0, 0);
    check(automatic.desktop->run().status==Status::ok, "default request closes");
    editForm::Form callbackForm; DesktopConfig callbackOptions;
    callbackOptions.onCloseRequested=[](Desktop& desktop) { desktop.close(); return CloseDecision::keepOpen; };
    auto callback=Desktop::create(callbackForm.ui, appearance, [&]{ return callbackForm.page(); }, callbackForm.scroll, callbackOptions);
    check(bool(callback.desktop), "callback close desktop");
    PostMessageW(static_cast<HWND>(callback.desktop->nativeWindow()), WM_CLOSE, 0, 0);
    check(callback.desktop->run().status==Status::ok, "close inside callback takes effect");
}
static void titleUpdates(const Appearance& appearance) {
    editForm::Form form;
    DesktopConfig options; options.title = u"Initial title for test";
    auto created = Desktop::create(form.ui, appearance, [&]{return form.page();}, form.scroll, options);
    check(bool(created.desktop), "title test desktop");
    auto window = FindWindowW(nullptr, L"Initial title for test");
    check(window != nullptr, "find test window");
    check(bool(created.desktop->setTitle(u"Updated title")), "set title");
    wchar_t text[64]{}; GetWindowTextW(window, text, 64);
    check(std::wstring(text) == L"Updated title", "native title updated");
    check(created.desktop->setTitle(u"Bad\nTitle").status == Status::invalidValue, "reject multiline title");
    Status status = Status::ok;
    std::thread worker([&] { status=created.desktop->setTitle(u"Other thread").status; }); worker.join();
    check(status == Status::wrongThread, "title requires UI thread");
}
static void publicLifecycle(const Appearance& appearance, bool handled) {
    editForm::Form form;
    editForm::Form other;
    DesktopHost otherHost(other.ui,appearance,[&]{return other.page();},other.scroll,{},false,true);
    check(bool(form.handles.name.setValue(u"workstation A")), "prepare public draft");
    RendererTestHooks hooks; hooks.errorAtFrame=1; hooks.failRebuildOnce=handled;
    DesktopConfig options;
    int callbacks=0, closeCalls=0;
    options.onCloseRequested=[&](Desktop&) { ++closeCalls; return CloseDecision::keepOpen; };
    if (handled) options.onRendererFailed=[&](Desktop& desktop, const RendererFailure& failure) {
        ++callbacks;
        SendMessageW(static_cast<HWND>(desktop.nativeWindow()), WM_SYSKEYDOWN, VK_F4, 1LL << 29);
        check(closeCalls==1, "Alt F4 requests close during renderer failure");
        check(failure.kind==RendererFailureKind::frameError, "public failure kind");
        check(bool(desktop.setTitle(u"Title during failure")), "title works during renderer failure");
        check(rendererFailurePreservesDraft(form.handles.name,u"workstation A"), "public draft protected");
        check(form.ui.semantics().text(form.ids().name).value == u"workstation A" &&
              rendererFailureRejectsInvoke(form.ui.semantics(), {form.ids().apply}, [&] { return form.applies(); }),
              "SEM12 semantic read and admission during renderer failure");
        check(desktop.rebuildRenderer().status==Status::failed && desktop.rendererFailed(), "public rebuild failure");
        hooks.errorAtFrame=0;
        check(bool(desktop.rebuildRenderer()), "public rebuild succeeds");
        check(bool(form.ui.semantics().perform({form.ids().apply}, Verb::invoke)) && form.applies() == 1,
              "SEM12 semantic invoke resumes after renderer rebuild");
        PostQuitMessage(0);
    };
    auto created=DesktopHost::createForTest(form.ui,appearance,[&]{return form.page();},form.scroll,options,&hooks);
    check(bool(created.desktop), "public desktop creation");
    auto result=created.desktop->run();
    check(handled ? result.status==Status::ok && callbacks==1 : result.status==Status::rendererFailed && result.failure && result.failure->kind==RendererFailureKind::frameError, "public run result");
    check(form.handles.name.draft().value==u"workstation A", "public retained draft");
    check(IsWindow(otherHost.window()) != FALSE, "other host remains open");
    if (!handled) check(form.handles.name.setValue(u"lost").status==Status::rendererFailed, "closed host preserves failure");
}
static void failure(const Appearance& appearance, bool reset, bool handled, bool modal = false) {
    editForm::Form form(editForm::defaultIds(), true);
    auto original=form.handles.name.draft().value;
    check(bool(form.handles.name.setValue(u"workstation A")), "prepare draft");
    check(bool(form.handles.name.focus()) && bool(form.handles.name.select(0,11)), "prepare selection");
    if (modal) check(bool(form.handles.dialog.open(form.handles.name)), "prepare modal");
    auto selection=access(form.ui).edit(form.ids().name)->selection;
    RendererTestHooks hooks; hooks.hideKhrDebug=true; hooks.errorAtFrame=reset?0:2; hooks.resetAtFrame=reset?2:0; hooks.failRebuildOnce=handled;
    DesktopHost host(form.ui,appearance,[&]{return form.page();},form.scroll,{},false,false,&hooks);
    auto before=host.compareGpu(640,480,1.f);
    check(before.passed, "pre-failure comparison");
    int callbacks=0;
    const auto owner=std::this_thread::get_id();
    if (handled) host.onRendererFailure([&](const RendererFailure& f) {
        ++callbacks;
        check(std::this_thread::get_id()==owner, "notification on UI thread");
        check(f.kind==(reset?RendererFailureKind::deviceReset:RendererFailureKind::frameError), "failure kind");
        check(rendererFailurePreservesDraft(form.handles.name,u"workstation A"), "failed edit preserves draft");
        check(access(form.ui).edit(form.ids().name)->selection.anchor==selection.anchor && access(form.ui).edit(form.ids().name)->selection.caret==selection.caret, "selection preserved");
        check(rendererFailureOmitsInteractiveVerbs(*form.handles.name.describe()), "failed focus, editing and selection verbs omitted");
        check(form.handles.dialog.open().status==Status::rendererFailed, "dialog blocked");
        check(host.rebuildRenderer().status==Status::failed && host.rendererFailed(), "RF07 failed rebuild");
    });
    host.paint();
    check(host.rendererFailed(), "immediate failure");
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
            check(hooks.rebuildAttempts==1 && host.rendererFailed(), "RF07 no automatic retry");
        }
        check(bool(host.rebuildRenderer()) && !host.rendererFailed(), "RF07 recovery");
        host.paint(); check(!host.rendererFailed(), "recovered paint");
        auto after=host.compareGpu(640,480,1.f);
        check(after.passed && compareImages(before.actual,after.actual,{}).differentPixels*200 <= 640u*480u, "recovered image");
        if (!modal) check(bool(form.handles.name.undo()) && form.handles.name.draft().value==original, "undo history preserved");
    }
    else { host.run(); check(host.rendererFailure()->kind==RendererFailureKind::frameError, "RF04 result"); host.close(); check(form.handles.name.describe()->value==u"workstation A", "RF04 retained draft"); }
}
static void frozenOverlays(const Appearance& appearance) {
    editForm::Form form(editForm::defaultIds(), true);
    RendererTestHooks hooks; hooks.errorAtFrame = 1;
    DesktopHost host(form.ui, appearance, [&] { return form.page(); }, form.scroll, {}, false, true, &hooks);
    auto& state = access(form.ui);
    std::uint64_t now = 1000;
    state.tooltipClock([&] { return now; });
    state.windowActive(true);
    check(bool(form.handles.tabs.activate({"v03"})), "tooltip page");
    state.setSystemKeyboardCues(true);  // a focus tooltip needs the indicator state to be on
    check(bool(form.handles.progressHalf.focus()), "tooltip source focus");
    now += 10000; state.tooltipTime(now);
    check(*form.handles.progressTip.visible(), "tooltip shown before failure");
    host.paint();
    check(host.rendererFailed(), "overlay test failure");
    SendMessageW(host.window(), WM_TIMER, 1, 0);
    check(*form.handles.progressTip.visible(), "tooltip remains visible during failure");
    auto presentation = form.ui.presentation(); presentation.textSize = 28;
    check(bool(form.ui.setPresentation(presentation)) && *form.handles.progressTip.visible(), "presentation reflow keeps tooltip frozen during failure");
    hooks.errorAtFrame = 0;
    check(bool(host.rebuildRenderer()), "overlay test recovery");
    editForm::Form dropdownForm(editForm::defaultIds(), true);
    RendererTestHooks wheelHooks; wheelHooks.errorAtFrame = 1;
    DesktopHost wheelHost(dropdownForm.ui, appearance, [&] { return layout::component(dropdownForm.handles.region); }, dropdownForm.scroll, {}, false, true, &wheelHooks);
    check(bool(dropdownForm.handles.region.open()), "dropdown opened before failure");
    wheelHost.paint();
    check(wheelHost.rendererFailed(), "wheel test failure");
    check(dropdownForm.handles.region.describe()->expanded, "dropdown remains open immediately after failure");
    SendMessageW(wheelHost.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    check(dropdownForm.handles.region.describe()->expanded, "wheel preserves dropdown during failure");
    Ui pendingUi;
    auto button = pendingUi.addButton({.id="pending", .label=u"Pending"}).value.value();
    auto tip = pendingUi.addTooltip("pending.tip", button, u"Help", 1000).value.value();
    ScrollState pendingScroll; RendererTestHooks pendingHooks; pendingHooks.errorAtFrame = 1;
    DesktopHost pendingHost(pendingUi, appearance, [&] { return layout::component(button); }, pendingScroll, {}, false, true, &pendingHooks);
    auto& pending = access(pendingUi); std::uint64_t pendingNow = 1000;
    pending.tooltipClock([&] { return pendingNow; }); pending.windowActive(true); pending.setSystemKeyboardCues(true);
    check(bool(button.focus()) && pending.tooltipDeadline() == 2000, "pending tooltip starts delay");
    pendingHost.paint(); check(pendingHost.rendererFailed(), "pending tooltip failure");
    pendingNow = 6000; pendingHooks.errorAtFrame = 0;
    check(bool(pendingHost.rebuildRenderer()) && pending.tooltipDeadline() == 7000, "recovery resumes remaining tooltip delay");
    pendingNow = 6999; pending.tooltipTime(pendingNow); check(!*tip.visible(), "tooltip delay remains pending");
    pendingNow = 7000; pending.tooltipTime(pendingNow); check(*tip.visible(), "tooltip appears after remaining delay");
}
static void presentationDuringFailure(const Appearance& appearance) {
    editForm::Form form(editForm::defaultIds(), true);
    check(bool(form.handles.name.setValue(u"Station W")), "PS10 prepare Latin text");
    RendererTestHooks hooks; hooks.errorAtFrame = 1;
    DesktopHost host(form.ui, appearance, [&] { return form.page(); }, form.scroll, {}, false, true, &hooks);
    host.forceScale(1.f); host.clientSize(960, 720);
    host.paint();
    check(host.rendererFailed(), "PS10 simulated failure");
    int frames = host.frames();
    auto settings = form.ui.presentation();
    settings.textSize = 28; settings.variant = ThemeVariant::dark(); settings.density = Density::compact;
    const auto& families = systemFontCatalog().families();
    auto font = std::find_if(families.begin(), families.end(), [](const FontFamily& family) { return family.key != u"Segoe UI"; });
    settings.fontFamily = font == families.end() ? u"LcarsUI missing PS10 font" : font->key;
    check(bool(form.ui.setPresentation(settings)) && form.ui.presentation() == settings, "PS10 settings accepted while failed");
    auto invalid = settings; invalid.textSize = 29;
    check(form.ui.setPresentation(invalid).status == Status::invalidValue && form.ui.presentation() == settings, "PS10 invalid settings remain atomic");
    check(form.handles.dark.toggle().status == Status::rendererFailed && form.ui.presentation() == settings, "PS10 presentation control remains blocked");
    host.paint();
    check(host.frames() == frames && host.rendererFailed(), "PS10 failure suppresses drawing");
    hooks.errorAtFrame = 0;
    check(bool(host.rebuildRenderer()), "PS10 rebuild");
    host.observePaint(true);
    host.paint();
    auto field = host.frame().find(form.handles.name);
    auto dark = appearance.palette(ThemeVariant::dark()).text;
    bool darkText = std::any_of(host.paintObservations().begin(), host.paintObservations().end(), [&](const auto& event) {
        return event.type == PaintObservation::Type::text && event.visible && event.color.r == dark.r && event.color.g == dark.g && event.color.b == dark.b;
    });
    check(host.frames() == frames + 1 && !host.rendererFailed() && field && field->textSize == 28 && darkText && form.ui.presentation() == settings,
          "PS10 first recovered frame uses stored presentation");
    editForm::Form reference(editForm::defaultIds(), true);
    check(bool(reference.handles.name.setValue(u"Station W")), "PS10 reference Latin text");
    check(bool(reference.ui.setPresentation(settings)), "PS10 reference settings");
    DesktopHost referenceHost(reference.ui, appearance, [&] { return reference.page(); }, reference.scroll, {}, false, true);
    referenceHost.forceScale(1.f); referenceHost.clientSize(960, 720); referenceHost.paint();
    auto expected = referenceHost.frame().find(reference.handles.name);
    check(expected && field->bounds.w == expected->bounds.w && field->bounds.h == expected->bounds.h &&
          field->textField.editor == expected->textField.editor && field->run.width == expected->run.width,
          "PS10 first recovered frame matches independently configured font and density");
    auto comparison = host.compareGpu(960, 720, 1.f);
    check(bool(comparison), "PS10 recovered CPU/GPU frame comparison");
}
// A real TDR without a robust context: every error query afterwards answers GL_CONTEXT_LOST.
// The frame must end as a device reset (not spin collecting errors) and the host must recover.
static void contextLost(const Appearance& appearance) {
    editForm::Form form(editForm::defaultIds(), true);
    RendererTestHooks hooks; hooks.hideKhrDebug=true; hooks.hideRobustness=true; hooks.contextLostAtFrame=2;
    DesktopHost host(form.ui,appearance,[&]{return form.page();},form.scroll,{},false,false,&hooks);
    check(host.compareGpu(640,480,1.f).passed, "context-lost pre-failure comparison");
    std::optional<RendererFailure> seen;
    host.onRendererFailure([&](const RendererFailure& f) { seen=f; });
    auto start=std::chrono::steady_clock::now();
    host.paint();
    check(std::chrono::steady_clock::now()-start < std::chrono::seconds(5), "TD01 a lost context ends the frame promptly");
    check(host.rendererFailed() && host.rendererFailure()->kind==RendererFailureKind::deviceReset &&
          host.rendererFailure()->nativeCode==0x0507, "TD01 a lost context is reported as a device reset");
    host.pump(); check(seen && seen->kind==RendererFailureKind::deviceReset, "TD01 the application is told once");
    check(bool(host.rebuildRenderer()) && !host.rendererFailed(), "TD01 rebuild after a lost context");
    host.paint(); check(!host.rendererFailed(), "TD01 recovered paint");
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--ps10") {
            for (auto appearance : {&webAppearance(), &materialAppearance()}) presentationDuringFailure(*appearance);
            std::cout << "PS10 passed for both appearances\n"; return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--gdi-version") {
            editForm::Form form;
            auto result=Desktop::create(form.ui,webAppearance(),[&]{return form.page();},form.scroll);
            check(result.failure && result.failure->kind==StartupFailureKind::contextVersion &&
                  result.failure->actual.find(u"GDI Generic")!=std::u16string::npos, "RF02 GDI Generic");
            return 0;
        }
        for (auto appearance : {&webAppearance(), &materialAppearance()}) {
            closeRequest(*appearance); titleUpdates(*appearance); startup(*appearance); frozenOverlays(*appearance); failure(*appearance,false,true); failure(*appearance,false,true,true); failure(*appearance,true,true); failure(*appearance,false,false); contextLost(*appearance);
            publicLifecycle(*appearance,true); publicLifecycle(*appearance,false);
        }
        std::cout << "RF01-RF05, RF07 failure seam checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
