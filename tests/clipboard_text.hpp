#pragma once
// One predicate for the line breaks a copy leaves on a clipboard, shared by the core seam and the real
// Windows and Linux clipboard tests: core hands every clipboard LF, and each platform's clipboard stores
// the text in its own convention.
#include <string>
#include <string_view>

namespace test {
enum class ClipboardSeam {
    core,     // the Clipboard and AsyncClipboard seams core writes to
    windows,  // CF_UNICODETEXT on the Windows clipboard
    linuxDesktop,  // text/plain;charset=utf-8 on Wayland, UTF8_STRING on X11
};
// The text a seam holds after Isoduo copies `value`, whose line breaks are LF.
inline std::u16string copiedClipboardText(std::u16string_view value, ClipboardSeam seam) {
    if (seam != ClipboardSeam::windows) return std::u16string(value);
    std::u16string result;
    for (auto ch : value) {
        if (ch == u'\n') result += u'\r';
        result += ch;
    }
    return result;
}
}
