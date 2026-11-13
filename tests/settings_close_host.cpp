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
using namespace settings_demo;
static void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
static std::function<void()> scheduled;
static std::exception_ptr script_error;
static Desktop* running;
static void CALLBACK timer_action(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr,id);
    try { auto action=std::move(scheduled); action(); }
    catch (...) { script_error=std::current_exception(); running->close(); }
}
static void later(std::function<void()> action) {
    scheduled=std::move(action);
    check(SetTimer(nullptr,0,30,timer_action)!=0, "schedule host action");
}
static std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
struct ScriptPrompt : NativePrompt {
    std::vector<bool> retries;
    std::vector<SaveChoice> choices;
    std::size_t retry_index=0, choice_index=0;
    int errors=0; bool saw_blank=false;
    std::function<void()> while_save;
    bool retry(std::u16string_view) override { check(retry_index<retries.size(), "unexpected retry prompt"); return retries[retry_index++]; }
    SaveChoice save(bool blank) override {
        saw_blank=blank;
        if (while_save) while_save();
        check(choice_index<choices.size(), "unexpected save prompt"); return choices[choice_index++];
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
        demo.on_save([this](const auto& doc) { return store.save(doc); });
        DesktopConfig options;
        options.on_close_requested=[this](Desktop& d) { ++requests; return controller.request(d,prompt); };
        if (fail) { hooks.gl_error_at_frame=1; options.on_renderer_failed=[this](Desktop& d, const RendererFailure& f) { controller.failure(d,f,prompt); }; }
        auto created=detail::DesktopHost::create_for_test(demo.ui,appearance,[this]{return demo.page();},demo.scroll,options,fail ? &hooks : nullptr);
        check(bool(created.desktop), "create host"); desktop=std::move(created.desktop);
        demo.on_close([this] { desktop->close(); });
    }
    ~Fixture() { desktop.reset(); std::filesystem::remove_all(path); }
    HWND hwnd() { return static_cast<HWND>(desktop->native_window()); }
    RunResult run() { running=desktop.get(); script_error=nullptr; auto result=desktop->run(); if (script_error) std::rethrow_exception(script_error); return result; }
    void post_close(bool system=false) { check(PostMessageW(hwnd(),system ? WM_SYSCOMMAND : WM_CLOSE,system ? SC_CLOSE : 0,0)!=0,"post close"); }
    void invoke(const char* id) { check(bool(demo.ui.semantics().perform({id},Verb::invoke)), "invoke dialog action"); }
};
static void normal_close(const Appearance& appearance, int choice) {
    Fixture f(appearance,"normal-"+std::to_string(choice));
    auto before=read_bytes(f.store.path());
    check(bool(f.demo.volume.set_value(65)),"change volume");
    f.post_close(choice==1);
    later([&] {
        check(f.requests==1 && IsWindow(f.hwnd()), "close is vetoed while dialog open");
        check(f.demo.close_dialog.is_open().value.value_or(false),"close dialog visible");
        if (choice==0) f.invoke("app.close_dialog.save");
        if (choice==1) f.invoke("app.close_dialog.discard");
        if (choice==2) {
            f.invoke("app.close_dialog.cancel");
            check(IsWindow(f.hwnd()) && !f.demo.close_dialog.is_open().value.value_or(true) && f.demo.is_dirty(), "cancel leaves window and changes");
            check(read_bytes(f.store.path())==before, "cancel leaves file");
            f.post_close();
            later([&] { check(f.requests==2,"second request"); f.invoke("app.close_dialog.discard"); });
        }
    });
    auto original_window=f.hwnd();
    auto result=f.run();
    check(result.status==Status::ok && !IsWindow(original_window), "normal close exits host");
    auto loaded=f.store.load(); check(bool(loaded.document), "load after close");
    check(loaded.document->volume==(choice==0 ? 65 : 40), "saved volume on reopen");
    if (choice!=0) check(read_bytes(f.store.path())==before, "discard leaves file bytes");
}
static void appearance_discard(const Appearance& appearance) {
    Fixture f(appearance,"appearance");
    auto baseline=f.demo.ui.presentation(); baseline.text_size=20;
    check(bool(f.demo.set_presentation(baseline)) && bool(f.demo.save()), "save initial appearance");
    auto before=read_bytes(f.store.path());
    check(bool(f.demo.nav.select({"appearance"})), "open appearance page");
    check(bool(f.demo.ui.semantics().perform({"appearance.text_size"},Verb::set_value,28)), "change size");
    f.post_close(true);
    later([&] { check(f.demo.close_dialog.is_open().value.value_or(false),"appearance close dialog"); f.invoke("app.close_dialog.discard"); });
    check(f.run().status==Status::ok,"appearance discard exits");
    check(read_bytes(f.store.path())==before, "appearance discard preserves bytes");
    auto loaded=f.store.load(); check(bool(loaded.document),"reload appearance file");
    Demo reopened; reopened.load_document(*loaded.document);
    auto created=Desktop::create(reopened.ui,appearance,[&] { return reopened.page(); },reopened.scroll);
    check(bool(created.desktop),"reopen appearance host");
    check(reopened.ui.presentation().text_size==20, "AP06 reopen saved appearance");
    PostMessageW(static_cast<HWND>(created.desktop->native_window()),WM_CLOSE,0,0);
    check(created.desktop->run().status==Status::ok, "reopened host closes");
}
static void composition_close(const Appearance& appearance) {
    Fixture f(appearance,"composition");
    std::u16string callback_draft; bool composing=true;
    DesktopConfig options;
    // Replace the fixture host so the observer runs in the actual WM_CLOSE callback.
    f.desktop.reset();
    options.on_close_requested=[&](Desktop& d) {
        auto* edit=detail::access(f.demo.ui).edit("general.name"); composing=edit && edit->composing;
        callback_draft=f.demo.name.draft().value.value_or(u"");
        return f.controller.request(d,f.prompt);
    };
    auto created=Desktop::create(f.demo.ui,appearance,[&]{return f.demo.page();},f.demo.scroll,options);
    check(bool(created.desktop),"composition host"); f.desktop=std::move(created.desktop);
    f.demo.on_close([&] { f.desktop->close(); });
    later([&] {
        check(bool(f.demo.name.focus()),"focus name");
        check(bool(f.demo.name.set_value(u"Before")),"set committed draft");
        check(bool(detail::access(f.demo.ui).composition_update(u"preedit",7)),"start simulated composition");
        f.post_close();
        later([&] { check(!composing && callback_draft==u"Before", "CR09 composition settled before callback"); f.invoke("app.close_dialog.discard"); });
    });
    check(f.run().status==Status::ok,"composition close exits");
}
static void failure_retry(const Appearance& appearance, bool fail_once) {
    Fixture f(appearance,fail_once ? "retry-fail" : "retry-success",true);
    f.hooks.fail_rebuild_once=fail_once;
    f.prompt.retries=fail_once ? std::vector<bool>{true,true} : std::vector<bool>{true};
    // Run the real failure callback, then close once recovery has completed.
    later([&] { check(!f.desktop->renderer_failed(),"renderer rebuilt after retry"); f.desktop->close(); });
    auto result=f.run();
    auto attempts=fail_once ? 2 : 1;
    check(result.status==Status::ok && f.prompt.retry_index==std::size_t(attempts) && f.hooks.rebuild_attempts==attempts,
          "CR11 rebuilds once per retry choice");
}
static void failure_close(const Appearance& appearance, SaveChoice choice, bool blank) {
    Fixture f(appearance,"failure-"+std::to_string(int(choice))+(blank?"b":""),true);
    check(bool(f.demo.volume.set_value(65)),"dirty before failure");
    check(bool(f.demo.name.set_value(blank ? u"" : u"Retained")),"set retained draft");
    auto before=read_bytes(f.store.path());
    f.desktop.reset();
    DesktopConfig options;
    options.on_close_requested=[&](Desktop& d) { ++f.requests; return f.controller.request(d,f.prompt); };
    options.on_renderer_failed=[&](Desktop& d,const RendererFailure& failure) {
        check(PostMessageW(f.hwnd(),WM_CLOSE,0,0)!=0,"post failure close");
        MSG message{};
        check(PeekMessageW(&message,f.hwnd(),WM_CLOSE,WM_CLOSE,PM_REMOVE)!=0,"receive failure close");
        DispatchMessageW(&message);
        if (choice==SaveChoice::cancel) f.controller.failure(d,failure,f.prompt);
    };
    f.hooks.gl_error_at_frame=1;
    auto created=detail::DesktopHost::create_for_test(f.demo.ui,appearance,[&]{return f.demo.page();},f.demo.scroll,options,&f.hooks);
    check(bool(created.desktop),"failure host"); f.desktop=std::move(created.desktop);
    f.prompt.choices=choice==SaveChoice::cancel ? std::vector<SaveChoice>{choice,SaveChoice::discard} : std::vector<SaveChoice>{choice};
    if (choice==SaveChoice::cancel) f.prompt.retries={false};
    bool sent_nested=false;
    f.prompt.while_save=[&] { if (!sent_nested) { sent_nested=true; SendMessageW(f.hwnd(),WM_CLOSE,0,0); } };
    auto result=f.run();
    check(result.status==Status::renderer_failed && f.requests==1,"CR10 and CR12 close without reentry");
    check(!f.demo.close_dialog.is_open().value.value_or(true), "failure avoids in-window close dialog");
    check(f.prompt.saw_blank==blank,"blank draft communicated");
    if (choice==SaveChoice::save) {
        auto loaded=f.store.load(); check(loaded.document && loaded.document->volume==65,"failure saves changed settings");
        check(loaded.document->name==(blank ? f.demo.document().name : u"Retained"),"failure handles retained name draft");
    } else check(read_bytes(f.store.path())==before,"failure discard leaves file");
    if (choice==SaveChoice::cancel) check(f.prompt.retry_index==1 && f.prompt.choice_index==2,"cancel returns to retry prompt");
}
static void failure_save_error(const Appearance& appearance) {
    Fixture f(appearance,"failure-save-error",true);
    check(bool(f.demo.volume.set_value(65)),"dirty before failed write");
    auto before=read_bytes(f.store.path());
    f.demo.on_save([](const auto&) { return std::u16string(u"disk full"); });
    f.prompt.retries={false,false};
    f.prompt.choices={SaveChoice::save,SaveChoice::discard};
    check(f.run().status==Status::renderer_failed,"failure exits after discard");
    check(f.prompt.errors==1 && f.prompt.retry_index==2 && f.prompt.choice_index==2,"failed write returns to retry prompt");
    check(read_bytes(f.store.path())==before,"failed write leaves file intact");
}
static void run_process(const std::wstring& arguments) {
    wchar_t path[MAX_PATH]{};
    check(GetModuleFileNameW(nullptr,path,MAX_PATH)>0,"locate host driver");
    std::wstring command=std::wstring(L"\"")+path+L"\" "+arguments;
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"start demo host process");
    DWORD wait=WaitForSingleObject(process.hProcess,60000);
    if (wait!=WAIT_OBJECT_0) { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); }
    DWORD exit_code=1; GetExitCodeProcess(process.hProcess,&exit_code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    check(wait==WAIT_OBJECT_0 && exit_code==0,"demo host process exits successfully");
}
int main(int argc, char** argv) {
    try {
        if (argc==4 && std::string_view(argv[1])=="--normal") {
            auto& appearance=std::string_view(argv[3])=="web" ? web_appearance() : material_appearance();
            normal_close(appearance,std::stoi(argv[2])); return 0;
        }
        if (argc==3 && std::string_view(argv[1])=="--appearance") {
            auto& appearance=std::string_view(argv[2])=="web" ? web_appearance() : material_appearance();
            appearance_discard(appearance); return 0;
        }
        for (int index=0;index<2;++index) {
            const auto* appearance=index ? &material_appearance() : &web_appearance();
            std::wstring name=index ? L"material" : L"web";
            for (int choice=0;choice<3;++choice) run_process(L"--normal "+std::to_wstring(choice)+L" "+name);
            run_process(L"--appearance "+name);
            composition_close(*appearance); failure_retry(*appearance,false); failure_retry(*appearance,true);
            failure_close(*appearance,SaveChoice::save,false); failure_close(*appearance,SaveChoice::save,true);
            failure_close(*appearance,SaveChoice::discard,false); failure_close(*appearance,SaveChoice::cancel,false);
            failure_save_error(*appearance);
        }
        std::cout << "settings close host passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

