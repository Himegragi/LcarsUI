#include "close_controller.hpp"
#include "settings_store.hpp"
#include "desktop_host.hpp"
#include "core_internal.hpp"
#include "lcars/semantics.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace lcars;
using namespace settingsDemo;
static void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
static std::function<void()> scheduled;
static std::exception_ptr scriptError;
static Desktop* running;
static void CALLBACK timerAction(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr,id);
    try { auto action=std::move(scheduled); action(); }
    catch (...) { scriptError=std::current_exception(); running->close(); }
}
static void later(std::function<void()> action) {
    scheduled=std::move(action);
    check(SetTimer(nullptr,0,30,timerAction)!=0, "schedule host action");
}
static std::string readBytes(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
struct ScriptPrompt : NativePrompt {
    std::vector<bool> retries;
    std::vector<SaveChoice> choices;
    std::size_t retryIndex=0, choiceIndex=0;
    int errors=0; bool sawBlank=false;
    std::function<void()> whileSave;
    bool retry(std::u16string_view) override { check(retryIndex<retries.size(), "unexpected retry prompt"); return retries[retryIndex++]; }
    SaveChoice save(bool blank) override {
        sawBlank=blank;
        if (whileSave) whileSave();
        check(choiceIndex<choices.size(), "unexpected save prompt"); return choices[choiceIndex++];
    }
    void error(std::u16string_view) override { ++errors; }
};
struct Fixture {
    std::filesystem::path path;
    SettingsStore store;
    Demo demo;
    CloseController controller;
    ScriptPrompt prompt;
    detail::RendererTestHooks hooks;
    std::unique_ptr<Desktop> desktop;
    int requests=0;
    explicit Fixture(const Appearance& appearance, std::string suffix, bool fail=false)
        : path(std::filesystem::temp_directory_path()/("lcars-close-host-"+std::to_string(GetCurrentProcessId())+"-"+suffix)), store(path), controller(demo) {
        std::filesystem::remove_all(path);
        check(store.save(demo.document()).empty(), "write baseline");
        demo.onSave([this](const auto& doc) { return store.save(doc); });
        DesktopConfig options;
        options.onCloseRequested=[this](Desktop& d) { ++requests; return controller.request(d,prompt); };
        if (fail) { hooks.errorAtFrame=1; options.onRendererFailed=[this](Desktop& d, const RendererFailure& f) { controller.failure(d,f,prompt); }; }
        auto created=detail::DesktopHost::createForTest(demo.ui,appearance,[this]{return demo.page();},demo.scroll,options,fail ? &hooks : nullptr);
        check(bool(created.desktop), "create host"); desktop=std::move(created.desktop);
        demo.onClose([this] { desktop->close(); });
    }
    ~Fixture() { desktop.reset(); std::filesystem::remove_all(path); }
    HWND hwnd() { return static_cast<HWND>(desktop->nativeWindow()); }
    RunResult run() { running=desktop.get(); scriptError=nullptr; auto result=desktop->run(); if (scriptError) std::rethrow_exception(scriptError); return result; }
    void postClose(bool system=false) { check(PostMessageW(hwnd(),system ? WM_SYSCOMMAND : WM_CLOSE,system ? SC_CLOSE : 0,0)!=0,"post close"); }
    void invoke(const char* id) { check(bool(demo.ui.semantics().perform({id},Verb::invoke)), "invoke dialog action"); }
};
static void normalClose(const Appearance& appearance, int choice) {
    Fixture f(appearance,"normal-"+std::to_string(choice));
    auto before=readBytes(f.store.path());
    check(bool(f.demo.volume.setValue(65)),"change volume");
    f.postClose(choice==1);
    later([&] {
        check(f.requests==1 && IsWindow(f.hwnd()), "close is vetoed while dialog open");
        check(f.demo.closeDialog.isOpen().value.value_or(false),"close dialog visible");
        if (choice==0) f.invoke("app.close_dialog.save");
        if (choice==1) f.invoke("app.close_dialog.discard");
        if (choice==2) {
            f.invoke("app.close_dialog.cancel");
            check(IsWindow(f.hwnd()) && !f.demo.closeDialog.isOpen().value.value_or(true) && f.demo.isDirty(), "cancel leaves window and changes");
            check(readBytes(f.store.path())==before, "cancel leaves file");
            f.postClose();
            later([&] { check(f.requests==2,"second request"); f.invoke("app.close_dialog.discard"); });
        }
    });
    auto originalWindow=f.hwnd();
    auto result=f.run();
    check(result.status==Status::ok && !IsWindow(originalWindow), "normal close exits host");
    auto loaded=f.store.load(); check(bool(loaded.document), "load after close");
    check(loaded.document->volume==(choice==0 ? 65 : 40), "saved volume on reopen");
    if (choice!=0) check(readBytes(f.store.path())==before, "discard leaves file bytes");
}
static void appearanceDiscard(const Appearance& appearance) {
    Fixture f(appearance,"appearance");
    auto baseline=f.demo.ui.presentation(); baseline.textSize=20;
    check(bool(f.demo.setPresentation(baseline)) && bool(f.demo.save()), "save initial appearance");
    auto before=readBytes(f.store.path());
    check(bool(f.demo.nav.select({"appearance"})), "open appearance page");
    check(bool(f.demo.ui.semantics().perform({"appearance.text_size"},Verb::setValue,28)), "change size");
    f.postClose(true);
    later([&] { check(f.demo.closeDialog.isOpen().value.value_or(false),"appearance close dialog"); f.invoke("app.close_dialog.discard"); });
    check(f.run().status==Status::ok,"appearance discard exits");
    check(readBytes(f.store.path())==before, "appearance discard preserves bytes");
    auto loaded=f.store.load(); check(bool(loaded.document),"reload appearance file");
    Demo reopened; reopened.loadDocument(*loaded.document);
    auto created=Desktop::create(reopened.ui,appearance,[&] { return reopened.page(); },reopened.scroll);
    check(bool(created.desktop),"reopen appearance host");
    check(reopened.ui.presentation().textSize==20, "AP06 reopen saved appearance");
    PostMessageW(static_cast<HWND>(created.desktop->nativeWindow()),WM_CLOSE,0,0);
    check(created.desktop->run().status==Status::ok, "reopened host closes");
}
static void compositionClose(const Appearance& appearance) {
    Fixture f(appearance,"composition");
    std::u16string callbackDraft; bool composing=true;
    DesktopConfig options;
    // Replace the fixture host so the observer runs in the actual WM_CLOSE callback.
    f.desktop.reset();
    options.onCloseRequested=[&](Desktop& d) {
        auto* edit=detail::access(f.demo.ui).edit("general.name"); composing=edit && edit->composing;
        callbackDraft=f.demo.name.draft().value.value_or(u"");
        return f.controller.request(d,f.prompt);
    };
    auto created=Desktop::create(f.demo.ui,appearance,[&]{return f.demo.page();},f.demo.scroll,options);
    check(bool(created.desktop),"composition host"); f.desktop=std::move(created.desktop);
    f.demo.onClose([&] { f.desktop->close(); });
    later([&] {
        check(bool(f.demo.name.focus()),"focus name");
        check(bool(f.demo.name.setValue(u"Before")),"set committed draft");
        check(bool(detail::access(f.demo.ui).compositionUpdate(u"preedit",7)),"start simulated composition");
        f.postClose();
        later([&] { check(!composing && callbackDraft==u"Before", "CR09 composition settled before callback"); f.invoke("app.close_dialog.discard"); });
    });
    check(f.run().status==Status::ok,"composition close exits");
}
static void failureRetry(const Appearance& appearance, bool failOnce) {
    Fixture f(appearance,failOnce ? "retry-fail" : "retry-success",true);
    f.hooks.failRebuildOnce=failOnce;
    f.prompt.retries=failOnce ? std::vector<bool>{true,true} : std::vector<bool>{true};
    // Run the real failure callback, then close once recovery has completed.
    later([&] { check(!f.desktop->rendererFailed(),"renderer rebuilt after retry"); f.desktop->close(); });
    auto result=f.run();
    auto attempts=failOnce ? 2 : 1;
    check(result.status==Status::ok && f.prompt.retryIndex==std::size_t(attempts) && f.hooks.rebuildAttempts==attempts,
          "CR11 rebuilds once per retry choice");
}
static void failureClose(const Appearance& appearance, SaveChoice choice, bool blank) {
    Fixture f(appearance,"failure-"+std::to_string(int(choice))+(blank?"b":""),true);
    check(bool(f.demo.volume.setValue(65)),"dirty before failure");
    check(bool(f.demo.name.setValue(blank ? u"" : u"Retained")),"set retained draft");
    auto before=readBytes(f.store.path());
    f.desktop.reset();
    DesktopConfig options;
    options.onCloseRequested=[&](Desktop& d) { ++f.requests; return f.controller.request(d,f.prompt); };
    options.onRendererFailed=[&](Desktop& d,const RendererFailure& failure) {
        check(PostMessageW(f.hwnd(),WM_CLOSE,0,0)!=0,"post failure close");
        MSG message{};
        check(PeekMessageW(&message,f.hwnd(),WM_CLOSE,WM_CLOSE,PM_REMOVE)!=0,"receive failure close");
        DispatchMessageW(&message);
        if (choice==SaveChoice::cancel) f.controller.failure(d,failure,f.prompt);
    };
    f.hooks.errorAtFrame=1;
    auto created=detail::DesktopHost::createForTest(f.demo.ui,appearance,[&]{return f.demo.page();},f.demo.scroll,options,&f.hooks);
    check(bool(created.desktop),"failure host"); f.desktop=std::move(created.desktop);
    f.prompt.choices=choice==SaveChoice::cancel ? std::vector<SaveChoice>{choice,SaveChoice::discard} : std::vector<SaveChoice>{choice};
    if (choice==SaveChoice::cancel) f.prompt.retries={false};
    bool sentNested=false;
    f.prompt.whileSave=[&] { if (!sentNested) { sentNested=true; SendMessageW(f.hwnd(),WM_CLOSE,0,0); } };
    auto result=f.run();
    check(result.status==Status::rendererFailed && f.requests==1,"CR10 and CR12 close without reentry");
    check(!f.demo.closeDialog.isOpen().value.value_or(true), "failure avoids in-window close dialog");
    check(f.prompt.sawBlank==blank,"blank draft communicated");
    if (choice==SaveChoice::save) {
        auto loaded=f.store.load(); check(loaded.document && loaded.document->volume==65,"failure saves changed settings");
        check(loaded.document->name==(blank ? f.demo.document().name : u"Retained"),"failure handles retained name draft");
    } else check(readBytes(f.store.path())==before,"failure discard leaves file");
    if (choice==SaveChoice::cancel) check(f.prompt.retryIndex==1 && f.prompt.choiceIndex==2,"cancel returns to retry prompt");
}
static void failureSaveError(const Appearance& appearance) {
    Fixture f(appearance,"failure-save-error",true);
    check(bool(f.demo.volume.setValue(65)),"dirty before failed write");
    auto before=readBytes(f.store.path());
    f.demo.onSave([](const auto&) { return std::u16string(u"disk full"); });
    f.prompt.retries={false,false};
    f.prompt.choices={SaveChoice::save,SaveChoice::discard};
    check(f.run().status==Status::rendererFailed,"failure exits after discard");
    check(f.prompt.errors==1 && f.prompt.retryIndex==2 && f.prompt.choiceIndex==2,"failed write returns to retry prompt");
    check(readBytes(f.store.path())==before,"failed write leaves file intact");
}
static void runProcess(const std::wstring& arguments) {
    wchar_t path[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr,path,MAX_PATH)>0,"locate host driver");
    std::wstring command=std::wstring(L"\"")+path+L"\" "+arguments;
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"start demo host process");
    DWORD wait=WaitForSingleObject(process.hProcess,60000);
    if (wait!=WAIT_OBJECT_0) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); }
    DWORD exitCode=1; GetExitCodeProcess(process.hProcess,&exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    check(wait==WAIT_OBJECT_0 && exitCode==0,"demo host process exits successfully");
}
int main(int argc, char** argv) {
    try {
        if (argc==4 && std::string_view(argv[1])=="--normal") {
            auto& appearance=std::string_view(argv[3])=="web" ? webAppearance() : materialAppearance();
            normalClose(appearance,std::stoi(argv[2])); return 0;
        }
        if (argc==3 && std::string_view(argv[1])=="--appearance") {
            auto& appearance=std::string_view(argv[2])=="web" ? webAppearance() : materialAppearance();
            appearanceDiscard(appearance); return 0;
        }
        for (int index=0;index<2;++index) {
            const auto* appearance=index ? &materialAppearance() : &webAppearance();
            std::wstring name=index ? L"material" : L"web";
            for (int choice=0;choice<3;++choice) runProcess(L"--normal "+std::to_wstring(choice)+L" "+name);
            runProcess(L"--appearance "+name);
            compositionClose(*appearance); failureRetry(*appearance,false); failureRetry(*appearance,true);
            failureClose(*appearance,SaveChoice::save,false); failureClose(*appearance,SaveChoice::save,true);
            failureClose(*appearance,SaveChoice::discard,false); failureClose(*appearance,SaveChoice::cancel,false);
            failureSaveError(*appearance);
        }
        std::cout << "settings close host passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

