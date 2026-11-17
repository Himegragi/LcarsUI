#include "desktop_host.hpp"
#include "rich_text.hpp"
#include "check.hpp"
#include <cstring>
using test::check;
int main() {
    HWND window = CreateWindowExW(0, L"STATIC", L"Isoduo clipboard contract", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, nullptr, nullptr);
    check(window != nullptr, "clipboard owner window created");
    isoduo::detail::WindowsClipboard clipboard(window);
    auto previous = clipboard.read();
    auto previousRich = clipboard.readRich();
    check(previous.status == isoduo::Status::ok || previous.status == isoduo::Status::notSupported, "clipboard can be saved");
    const auto canonicalFormat = RegisterClipboardFormatW(L"Isoduo.RichText.1");
    const auto legacyFormat = RegisterClipboardFormatW(L"LcarsUI.RichText.1");
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
    check(read && read->starts_with("Isoduo.RichText.1\n") && isoduo::detail::parseRichPayload(*read) == rich, "real clipboard canonical round trip");
    if (previous) check(clipboard.writeRich(*previous, previousRich ? *previousRich : std::string{}), "restore previous text and rich data");
    else check(clipboard.write(u""), "clear fixture data");
    DestroyWindow(window);
}
