#include "../startup_report.hpp"
#include "settings_demo.hpp"
#include "close_controller.hpp"
#include "isoduo/desktop.hpp"
#include "about_desktop.hpp"
#include "settings_store.hpp"
#include "failure_diagnostic.hpp"
#include <fstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
namespace {
class WindowsPrompt : public settingsDemo::NativePrompt {
    HWND owner_;
    static std::wstring wide(std::u16string_view s) { return {s.begin(), s.end()}; }
public:
    explicit WindowsPrompt(void* owner) : owner_(static_cast<HWND>(owner)) {}
    bool retry(std::u16string_view reason) override {
        auto message = L"\u756b\u9762\u7121\u6cd5\u7e6a\u88fd\uff08" + wide(reason) + L"\uff09\u3002\u6309\u300c\u91cd\u8a66\u300d\u91cd\u65b0\u5efa\u7acb\u756b\u9762\uff1b\u6309\u300c\u53d6\u6d88\u300d\u7d50\u675f\u7a0b\u5f0f\uff08\u6709\u672a\u5132\u5b58\u7684\u4fee\u6539\u6642\u6703\u5148\u8a62\u554f\uff09\u3002";
        return MessageBoxW(owner_, message.c_str(), L"\u756b\u9762\u7121\u6cd5\u7e6a\u88fd", MB_RETRYCANCEL | MB_ICONERROR) == IDRETRY;
    }
    settingsDemo::SaveChoice save(bool blank) override {
        auto message = blank ? L"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f\u986f\u793a\u540d\u7a31\u7a7a\u767d\uff0c\u672a\u5132\u5b58\u6b64\u6b04\u3002" : L"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f";
        int result = MessageBoxW(owner_, message, L"\u8981\u5132\u5b58\u4fee\u6539\u55ce\uff1f", MB_YESNOCANCEL | MB_ICONWARNING);
        return result == IDYES ? settingsDemo::SaveChoice::save : result == IDNO ? settingsDemo::SaveChoice::discard : settingsDemo::SaveChoice::cancel;
    }
    void error(std::u16string_view message) override { auto text=wide(message); MessageBoxW(owner_, text.c_str(), L"\u7121\u6cd5\u5132\u5b58", MB_OK | MB_ICONERROR); }
};
settingsDemo::Demo* pendingDemo = nullptr;
std::u16string pendingTitle, pendingMessage;
void CALLBACK showPendingNotice(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    auto* demo = pendingDemo; pendingDemo = nullptr;
    if (demo) demo->showLoadNotice(std::move(pendingTitle), std::move(pendingMessage));
}
}
int wmain(int argc, wchar_t** argv) {
    try {
#ifdef ISODUO_APPEARANCE_material
        const auto& appearance = isoduo::materialAppearance();
#else
        const auto& appearance = isoduo::webAppearance();
#endif
        auto name = appearance.name();
        std::filesystem::path directory, diagnosticsOut;
        for (int i = 1; i < argc; i += 2) {
            std::wstring_view flag(argv[i]);
            if (i + 1 < argc && flag == L"--settings-dir" && directory.empty()) directory = argv[i + 1];
            else if (i + 1 < argc && flag == L"--diagnostics-out" && diagnosticsOut.empty()) diagnosticsOut = argv[i + 1];
            else { std::cerr << "Usage: [--settings-dir <directory>] [--diagnostics-out <file>]\n"; return 1; }
        }
#ifndef ISODUO_DEMO_DIAGNOSTICS
        if (!diagnosticsOut.empty()) { std::cerr << "--diagnostics-out needs a build with Isoduo::Diagnostics\n"; return 1; }
#endif
        if (directory.empty()) {
            PWSTR raw=nullptr; if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&raw))) throw std::runtime_error("Cannot locate Roaming AppData");
            directory=std::filesystem::path(raw)/L"Isoduo"/L"SettingsDemo"; CoTaskMemFree(raw);
        }
        settingsDemo::SettingsStore store(directory);
        auto loaded=store.load();
        auto about=settingsDemo::desktopAboutInfo(appearance);
#ifdef ISODUO_DEMO_DIAGNOSTICS
        isoduo::Diagnostics diagnostics;
        about.diagnostics = true;
#endif
        auto path=store.path().wstring(); about.settingsPath.assign(path.begin(),path.end());
        settingsDemo::Demo demo(std::move(about));
        if (loaded.document) demo.loadDocument(*loaded.document);
        std::u16string pathText(path.begin(),path.end());
        demo.onSave([&store](const auto& document) { return store.save(document); },pathText);
        isoduo::DesktopConfig options;
#ifdef ISODUO_DEMO_DIAGNOSTICS
        options.onDiagnostic = [&](const isoduo::RenderDiagnostic& event) { diagnostics.record(event); };
        // Evidence runs (TD01) save the in-memory record; normal runs never write it.
        auto exportDiagnostics = [&] {
            if (diagnosticsOut.empty()) return;
            std::ofstream out(diagnosticsOut, std::ios::binary | std::ios::trunc);
            if (!out || !diagnostics.save(out)) std::cerr << "Cannot write diagnostics export\n";
        };
#endif
        options.title = u"設定管理示範 — " + std::u16string(name.begin(), name.end());
        settingsDemo::CloseController closeController(demo);
        options.onCloseRequested = [&](isoduo::Desktop& desktop) {
            WindowsPrompt prompt(desktop.nativeWindow());
            return closeController.request(desktop, prompt);
        };
        options.onRendererFailed = [&](isoduo::Desktop& desktop, const isoduo::RendererFailure& failure) {
#ifdef ISODUO_DEMO_DIAGNOSTICS
            diagnostics.recordFailure(settingsDemo::failureDiagnostic(failure));
            exportDiagnostics();
#endif
            WindowsPrompt prompt(desktop.nativeWindow());
            closeController.failure(desktop, failure, prompt);
        };
        auto creation = isoduo::Desktop::create(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options);
        if (!creation.desktop) { reportStartupFailure(creation.failure); return 1; }
        demo.onClose([&] { creation.desktop->close(); });
        demo.onDirtyChanged([&](bool dirty) { creation.desktop->setTitle((dirty ? u"*" : u"") + options.title); });
        if (loaded.kind==settingsDemo::LoadKind::corrupt) {
            auto message=u"無法讀取 " + pathText + u"（" + loaded.reason + u"）。已改用預設值啟動，";
            message += loaded.renamed ? u"原檔已改名為 settings.json.bad。" : u"原檔無法改名，下次儲存會覆蓋它。";
            pendingDemo=&demo; pendingTitle=u"設定檔已損毀"; pendingMessage=std::move(message);
        } else if (loaded.kind==settingsDemo::LoadKind::unreadable) {
            pendingDemo=&demo; pendingTitle=u"無法讀取設定檔"; pendingMessage=u"無法讀取 " + pathText + u"（" + loaded.reason + u"）。已改用預設值啟動。";
        }
        UINT_PTR noticeTimer = pendingDemo ? SetTimer(nullptr, 0, 1, showPendingNotice) : 0;
        auto caps = creation.desktop->capabilities();
        demo.setRenderer(caps.version + u" — " + caps.renderer);
        auto result=creation.desktop->run();
        if (noticeTimer) KillTimer(nullptr, noticeTimer);
        pendingDemo=nullptr;
#ifdef ISODUO_DEMO_DIAGNOSTICS
        exportDiagnostics();
#endif
        return result.status == isoduo::Status::ok ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
