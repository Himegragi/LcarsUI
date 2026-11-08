#include "settings_demo.hpp"
#include "close_controller.hpp"
#include "lcars/desktop.hpp"
#include "about_desktop.hpp"
#include "settings_store.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
namespace {
class WindowsPrompt : public settings_demo::NativePrompt {
    HWND owner_;
    static std::wstring wide(std::u16string_view s) { return {s.begin(), s.end()}; }
public:
    explicit WindowsPrompt(void* owner) : owner_(static_cast<HWND>(owner)) {}
    bool retry(std::u16string_view reason) override {
        auto message = L"\u756b\u9762\u7121\u6cd5\u7e6a\u88fd\uff08" + wide(reason) + L"\uff09\u3002\u6309\u300c\u91cd\u8a66\u300d\u91cd\u65b0\u5efa\u7acb\u756b\u9762\uff1b\u6309\u300c\u53d6\u6d88\u300d\u7d50\u675f\u7a0b\u5f0f\uff08\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539\u6642\u6703\u5148\u8a62\u554f\uff09\u3002";
        return MessageBoxW(owner_, message.c_str(), L"\u756b\u9762\u7121\u6cd5\u7e6a\u88fd", MB_RETRYCANCEL | MB_ICONERROR) == IDRETRY;
    }
    settings_demo::SaveChoice save(bool blank) override {
        auto message = blank ? L"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f\u986f\u793a\u540d\u7a31\u7a7a\u767d\uff0c\u672a\u5132\u5b58\u6b64\u6b04\u3002" : L"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f";
        int result = MessageBoxW(owner_, message, L"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f", MB_YESNOCANCEL | MB_ICONWARNING);
        return result == IDYES ? settings_demo::SaveChoice::save : result == IDNO ? settings_demo::SaveChoice::discard : settings_demo::SaveChoice::cancel;
    }
    void error(std::u16string_view message) override { auto text=wide(message); MessageBoxW(owner_, text.c_str(), L"\u7121\u6cd5\u5132\u5b58", MB_OK | MB_ICONERROR); }
};
settings_demo::Demo* pending_demo = nullptr;
std::u16string pending_title, pending_message;
void CALLBACK show_pending_notice(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    auto* demo = pending_demo; pending_demo = nullptr;
    if (demo) demo->show_load_notice(std::move(pending_title), std::move(pending_message));
}
}
int wmain(int argc, wchar_t** argv) {
    try {
#ifdef LCARS_APPEARANCE_material
        const auto& appearance = lcars::material_appearance();
#else
        const auto& appearance = lcars::web_appearance();
#endif
        auto name = appearance.name();
        std::filesystem::path directory;
        if (argc==3 && std::wstring_view(argv[1])==L"--settings-dir") directory=argv[2];
        else if (argc!=1) { std::cerr << "Usage: --settings-dir <directory>\n"; return 1; }
        else {
            PWSTR raw=nullptr; if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&raw))) throw std::runtime_error("Cannot locate Roaming AppData");
            directory=std::filesystem::path(raw)/L"LcarsUI"/L"SettingsDemo"; CoTaskMemFree(raw);
        }
        settings_demo::SettingsStore store(directory);
        auto loaded=store.load();
        auto about=settings_demo::desktop_about_info(appearance);
        auto path=store.path().wstring(); about.settings_path.assign(path.begin(),path.end());
        settings_demo::Demo demo(std::move(about));
        if (loaded.document) demo.load_document(*loaded.document);
        std::u16string path_text(path.begin(),path.end());
        demo.on_save([&store](const auto& document) { return store.save(document); },path_text);
        lcars::DesktopOptions options;
        options.title = u"設定管理示範 — " + std::u16string(name.begin(), name.end());
        settings_demo::CloseController close_controller(demo);
        options.on_close_requested = [&](lcars::Desktop& desktop) {
            WindowsPrompt prompt(desktop.native_window());
            return close_controller.request(desktop, prompt);
        };
        options.on_renderer_failed = [&](lcars::Desktop& desktop, const lcars::RendererFailure& failure) {
            WindowsPrompt prompt(desktop.native_window());
            close_controller.failure(desktop, failure, prompt);
        };
        auto creation = lcars::Desktop::create(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options);
        if (!creation.desktop) return 1;
        demo.on_close([&] { creation.desktop->close(); });
        demo.on_dirty_changed([&](bool dirty) { creation.desktop->set_title((dirty ? u"*" : u"") + options.title); });
        if (loaded.kind==settings_demo::LoadKind::corrupt) {
            auto message=u"無法讀取 " + path_text + u"（" + loaded.reason + u"）。已改用預設值啟動，";
            message += loaded.renamed ? u"原檔已改名為 settings.json.bad。" : u"原檔無法改名，下次儲存會覆蓋它。";
            pending_demo=&demo; pending_title=u"設定檔已損毀"; pending_message=std::move(message);
        } else if (loaded.kind==settings_demo::LoadKind::unreadable) {
            pending_demo=&demo; pending_title=u"無法讀取設定檔"; pending_message=u"無法讀取 " + path_text + u"（" + loaded.reason + u"）。已改用預設值啟動。";
        }
        UINT_PTR notice_timer = pending_demo ? SetTimer(nullptr, 0, 1, show_pending_notice) : 0;
        auto caps = creation.desktop->capabilities();
        demo.set_renderer(caps.version + u" — " + caps.renderer);
        auto result=creation.desktop->run();
        if (notice_timer) KillTimer(nullptr, notice_timer);
        pending_demo=nullptr;
        return result.status == lcars::Status::ok ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
