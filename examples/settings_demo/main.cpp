#include "../startup_report.hpp"
#include "settings_demo.hpp"
#include "isoduo/desktop.hpp"
#include "about_desktop.hpp"
#include "settings_store.hpp"
#include "failure_diagnostic.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#include "close_controller.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#else
#include "linux_failure_prompt.hpp"
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#endif
namespace {
#ifdef _WIN32
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
#else
std::filesystem::path defaultDirectory() {
    if (auto config = std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute())
        return std::filesystem::path(config) / "Isoduo" / "SettingsDemo";
    std::filesystem::path home;
    if (auto value = std::getenv("HOME")) home = value;
    if (!home.is_absolute()) {
        if (auto account = ::getpwuid(::getuid())) home = account->pw_dir;
    }
    if (!home.is_absolute()) throw std::runtime_error("Cannot locate the user's configuration directory");
    return home / ".config" / "Isoduo" / "SettingsDemo";
}
#endif
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
#ifdef ISODUO_APPEARANCE_material
        const auto& appearance = isoduo::materialAppearance();
#else
        const auto& appearance = isoduo::webAppearance();
#endif
        auto name = appearance.name();
        std::filesystem::path directory, diagnosticsOut;
#ifdef _WIN32
        constexpr std::wstring_view settingsFlag = L"--settings-dir", diagnosticsFlag = L"--diagnostics-out";
#else
        constexpr std::string_view settingsFlag = "--settings-dir", diagnosticsFlag = "--diagnostics-out";
#endif
        for (int i = 1; i < argc; i += 2) {
            std::basic_string_view flag(argv[i]);
            bool hasValue = i + 1 < argc;
#ifndef _WIN32
            hasValue = hasValue && argv[i + 1][0] && !std::string_view(argv[i + 1]).starts_with("--");
#endif
            if (hasValue && flag == settingsFlag && directory.empty()) directory = argv[i + 1];
            else if (hasValue && flag == diagnosticsFlag && diagnosticsOut.empty()) diagnosticsOut = argv[i + 1];
            else { std::cerr << "Usage: [--settings-dir <directory>] [--diagnostics-out <file>]\n"; return 1; }
        }
#ifndef ISODUO_DEMO_DIAGNOSTICS
        if (!diagnosticsOut.empty()) { std::cerr << "--diagnostics-out needs a build with Isoduo::Diagnostics\n"; return 1; }
#endif
#ifdef _WIN32
        if (directory.empty()) {
            PWSTR raw=nullptr; if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&raw))) throw std::runtime_error("Cannot locate Roaming AppData");
            directory=std::filesystem::path(raw)/L"Isoduo"/L"SettingsDemo"; CoTaskMemFree(raw);
        }
#else
        directory = directory.empty() ? defaultDirectory() : std::filesystem::absolute(directory);
#endif
        settingsDemo::SettingsStore store(directory);
        auto loaded=store.load();
        auto about=settingsDemo::desktopAboutInfo(appearance);
#ifdef ISODUO_DEMO_DIAGNOSTICS
        isoduo::Diagnostics diagnostics;
        about.diagnostics = true;
#endif
        auto pathText = store.path().u16string();
        about.settingsPath = pathText;
        settingsDemo::Demo demo(std::move(about));
        if (loaded.document) demo.loadDocument(*loaded.document);
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
#ifdef _WIN32
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
#else
        settingsDemo::LinuxFailurePrompt prompt(demo);
        options.onCloseRequested = [&](isoduo::Desktop& desktop) { return prompt.requestClose(desktop); };
        options.onRendererFailed = [&](isoduo::Desktop& desktop, const isoduo::RendererFailure& failure) {
#ifdef ISODUO_DEMO_DIAGNOSTICS
            diagnostics.recordFailure(settingsDemo::failureDiagnostic(failure));
            exportDiagnostics();
#endif
            prompt.show(desktop, failure.message);
        };
#endif
        auto creation = isoduo::Desktop::create(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, options);
        if (!creation.desktop) { reportStartupFailure(creation.failure); return 1; }
        demo.onClose([&] { creation.desktop->close(); });
        demo.onDirtyChanged([&](bool dirty) { creation.desktop->setTitle((dirty ? u"*" : u"") + options.title); });
        if (loaded.kind == settingsDemo::LoadKind::corrupt || loaded.kind == settingsDemo::LoadKind::unreadable) {
            bool corrupt = loaded.kind == settingsDemo::LoadKind::corrupt;
            std::u16string title = corrupt ? u"設定檔已損毀" : u"無法讀取設定檔";
            auto message = u"無法讀取 " + pathText + u"（" + loaded.reason + u"）。已改用預設值啟動";
            if (corrupt)
                message += loaded.renamed ? u"，原檔已改名為 settings.json.bad。" : u"，原檔無法改名，下次儲存會覆蓋它。";
            else message += u"。";
#ifdef _WIN32
            pendingDemo = &demo;
            pendingTitle = std::move(title);
            pendingMessage = std::move(message);
#else
            demo.showLoadNotice(std::move(title), std::move(message));
#endif
        }
#ifdef _WIN32
        UINT_PTR noticeTimer = pendingDemo ? SetTimer(nullptr, 0, 1, showPendingNotice) : 0;
#endif
        auto caps = creation.desktop->capabilities();
        demo.setRenderer(caps.version + u" — " + caps.renderer);
        auto result=creation.desktop->run();
#ifdef _WIN32
        if (noticeTimer) KillTimer(nullptr, noticeTimer);
        pendingDemo=nullptr;
#else
        prompt.stop();  // an answer must not reach the Desktop after it is destroyed
        if (result.failure) {
            std::cerr << "Renderer failed: " << settingsDemo::promptUtf8(result.failure->message) << '\n';
        }
#endif
#ifdef ISODUO_DEMO_DIAGNOSTICS
        exportDiagnostics();
#endif
        return result.status == isoduo::Status::ok ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
