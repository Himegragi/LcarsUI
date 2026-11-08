#include "settings_demo.hpp"
#include "lcars/desktop.hpp"
#include "about_desktop.hpp"
#include "settings_store.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
namespace {
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
        auto creation = lcars::Desktop::create(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options);
        if (!creation.desktop) return 1;
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
