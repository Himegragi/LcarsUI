#include "desktop_host.hpp"
#include "core_internal.hpp"
#include "rich_text_contract.hpp"
#include "check.hpp"
#include "clipboard_text.hpp"
#include <cstring>
#include <system_error>

namespace {
using test::check;

void requireWin32(bool success, const char* operation) {
    if (!success) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

void isolateClipboard() {
    // This executable owns the station and desktop until process exit. Open the
    // clipboard only after both associations are in place.
    const auto station = CreateWindowStationW(nullptr, CWF_CREATE_ONLY, WINSTA_ALL_ACCESS, nullptr);
    requireWin32(station != nullptr, "create private clipboard window station");
    USEROBJECTFLAGS flags{};
    requireWin32(GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), nullptr) != FALSE,
                 "inspect clipboard window station");
    check(!(flags.dwFlags & WSF_VISIBLE), "clipboard station must be noninteractive");
    requireWin32(SetProcessWindowStation(station) != FALSE, "attach private clipboard window station");
    const auto desktop = CreateDesktopW(L"IsoduoClipboardContract", nullptr, nullptr, 0,
                                        DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    requireWin32(desktop != nullptr, "create private clipboard desktop");
    requireWin32(SetThreadDesktop(desktop) != FALSE, "attach private clipboard desktop");
}

std::u16string unicodeText(HWND window) {
    std::u16string text;
    if (!OpenClipboard(window)) return text;
    if (auto memory = GetClipboardData(CF_UNICODETEXT)) {
        if (auto data = static_cast<const char16_t*>(GlobalLock(memory))) {
            text = data;
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    return text;
}

// Core hands the clipboard LF line breaks; the Windows clipboard holds CRLF, as other Windows text does,
// and pasting it back gives LF again.
void multiLineCopy(HWND window, isoduo::detail::WindowsClipboard& clipboard) {
    const auto windowsText = test::copiedClipboardText(u"one\ntwo", test::ClipboardSeam::windows);
    check(windowsText == u"one\r\ntwo", "the shared predicate gives Windows CRLF");
    isoduo::Ui ui;
    auto field = ui.addTextField({.id = "note", .value = u"one\ntwo", .multiline = true, .visibleLines = 2}).value.value();
    check(bool(field.connectSource({[] { return std::u16string(u"one\ntwo"); }, [](std::function<void()>) { return [] {}; }})) &&
          bool(field.connectConfirmed([](const auto&) { return isoduo::Result{}; })), "connect the field");
    auto& host = isoduo::detail::access(ui);
    host.setClipboard(&clipboard);
    host.focus("note");
    host.select("note", 0, 7);
    check(bool(host.copy("note")) && unicodeText(window) == windowsText, "a multi-line copy stores CRLF in CF_UNICODETEXT");
    auto read = clipboard.read();
    check(read && *read == windowsText, "the Windows clipboard reads back CRLF");
    host.select("note", 7, 7);
    check(bool(host.pasteClipboard("note")) && host.text("note") == u"one\ntwoone\ntwo", "pasting the CRLF text inserts LF");
    check(clipboard.write(u"a\r\nb") && unicodeText(window) == u"a\r\nb", "text that already has CRLF keeps it");
    host.setClipboard(nullptr);
}

void clipboardContract() {
    HWND window = CreateWindowExW(0, L"STATIC", L"Isoduo clipboard contract", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, nullptr, nullptr);
    requireWin32(window != nullptr, "create clipboard owner window");
    isoduo::detail::WindowsClipboard clipboard(window);
    const auto canonicalFormat = RegisterClipboardFormatW(L"Isoduo.RichText.1");
    const auto legacyFormat = RegisterClipboardFormatW(L"LcarsUI.RichText.1");
    check(canonicalFormat && legacyFormat && canonicalFormat != legacyFormat, "register both rich-text formats");
    const std::string old = "LcarsUI.RichText.1\n5\nAlpha\n1\n0 5 1 0:\n";
    check(clipboard.write(u"Alpha"), "plain text transaction");
    check(OpenClipboard(window) != FALSE, "open clipboard for legacy owner fixture");
    auto memory = GlobalAlloc(GMEM_MOVEABLE, old.size() + 1);
    check(memory != nullptr, "fixture allocation");
    auto data = GlobalLock(memory); check(data != nullptr, "fixture lock");
    std::memcpy(data, old.c_str(), old.size() + 1); GlobalUnlock(memory);
    check(SetClipboardData(legacyFormat, memory) != nullptr, "publish old registered format");
    CloseClipboard();
    auto input = clipboard.readRich();
    auto rich = input ? isoduo::detail::parseRichPayload(*input) : std::nullopt;
    check(rich && rich->text == u"Alpha" && rich->spans.size() == 1 && rich->spans[0].bold, "real clipboard reads legacy format and preserves style");
    auto output = isoduo::detail::serializeRichPayload(*rich);
    check(clipboard.writeRich(u"Alpha", output), "write migrated value");
    check(IsClipboardFormatAvailable(canonicalFormat) && !IsClipboardFormatAvailable(legacyFormat), "new clipboard transaction publishes only canonical format");
    auto read = clipboard.readRich();
    check(read && test::canonicalRichRoundTrip(*read, *rich), "real clipboard canonical round trip");
    multiLineCopy(window, clipboard);
    DestroyWindow(window);
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--isolated") {
        try { isolateClipboard(); }
        catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
        return test::run("clipboard_contract", {{"private Win32 clipboard", clipboardContract}});
    }
    // An unnamed station is scoped to a logon session. Give the worker a fresh
    // local logon session. These placeholder network credentials are unused.
    wchar_t executable[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length) {
        const auto error = GetLastError();
        std::cerr << "Cannot locate clipboard worker executable: " << error << '\n';
        return 1;
    }
    if (length == MAX_PATH) {
        std::cerr << "Clipboard worker executable path was truncated\n";
        return 1;
    }
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --isolated";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION worker{};
    if (!CreateProcessWithLogonW(L"IsoduoClipboardFixture", L".", L"unused", LOGON_NETCREDENTIALS_ONLY,
                                 executable, command.data(), CREATE_NO_WINDOW, nullptr, nullptr, &startup, &worker)) {
        std::cerr << "Cannot start isolated clipboard worker: " << GetLastError() << '\n';
        return 1;
    }
    CloseHandle(worker.hThread);
    DWORD result = 1;
    const auto wait = WaitForSingleObject(worker.hProcess, 30000);
    if (wait == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(worker.hProcess, &result)) result = 1;
    } else {
        TerminateProcess(worker.hProcess, 1);
        std::cerr << "Could not wait for isolated clipboard worker (wait result " << wait << ")\n";
    }
    CloseHandle(worker.hProcess);
    if (result == 2) std::cerr << "Clipboard worker could not establish private station and desktop\n";
    else if (result) std::cerr << "Isolated clipboard worker exited with code " << result << '\n';
    return result == 0 ? 0 : 1;
}
