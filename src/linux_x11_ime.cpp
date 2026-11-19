#include "linux_x11_ime.hpp"
#include "utf.hpp"
#include <algorithm>
#include <bit>
#include <climits>
#include <clocale>
#include <vector>

namespace isoduo::detail {
namespace {
using Utf32 = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>;
XPoint spot(Rect rect) {
    return {static_cast<short>(std::clamp(rect.x, int(SHRT_MIN), int(SHRT_MAX))),
            static_cast<short>(std::clamp(rect.y + rect.h, int(SHRT_MIN), int(SHRT_MAX)))};
}
}
X11Ime::X11Ime(Display* display, Window window, long eventMask, std::function<void(LinuxImeEdit)> deliver)
    : display_(display), window_(window), eventMask_(eventMask), deliver_(std::move(deliver)) {
    // XOpenIM binds the context to the current Xlib locale. Honor the application's
    // locale when configured, or load the session locale just for opening XIM.
    const std::string locale = std::setlocale(LC_CTYPE, nullptr);
    if (locale == "C") std::setlocale(LC_CTYPE, "");
    if (XSupportsLocale() && XSetLocaleModifiers("")) method_ = XOpenIM(display_, nullptr, nullptr, nullptr);
    if (locale == "C") std::setlocale(LC_CTYPE, locale.c_str());
    if (!method_) return;
    XIMCallback destroy{reinterpret_cast<XPointer>(this), &disconnected};
    XSetIMValues(method_, XNDestroyCallback, &destroy, nullptr);
    XIMStyles* styles = nullptr;
    if (XGetIMValues(method_, XNQueryInputStyle, &styles, nullptr) || !styles) return;
    for (XIMStyle preferred : {XIMPreeditCallbacks | XIMStatusNothing, XIMPreeditCallbacks | XIMStatusNone,
                               XIMPreeditNothing | XIMStatusNothing, XIMPreeditNone | XIMStatusNone}) {
        if (std::find(styles->supported_styles, styles->supported_styles + styles->count_styles, preferred) !=
            styles->supported_styles + styles->count_styles) {
            style_ = preferred;
            break;
        }
    }
    XFree(styles);
}
X11Ime::~X11Ime() {
    destroyContext();
    if (method_) XCloseIM(method_);
}
void X11Ime::createContext() {
    if (!method_ || !style_) return;
    if (style_ & XIMPreeditCallbacks) {
        auto* data = reinterpret_cast<XPointer>(this);
        XIMCallback start{data, std::bit_cast<XIMProc>(&started)};
        XIMCallback done{data, &ended}, draw{data, &drawn}, caret{data, &caretMoved};
        XPoint location = spot(*caret_);
        auto attrs = XVaCreateNestedList(0, XNPreeditStartCallback, &start, XNPreeditDoneCallback, &done,
            XNPreeditDrawCallback, &draw, XNPreeditCaretCallback, &caret, XNSpotLocation, &location, nullptr);
        if (attrs) {
            context_ = XCreateIC(method_, XNInputStyle, style_, XNClientWindow, window_, XNFocusWindow, window_,
                XNPreeditAttributes, attrs, nullptr);
            XFree(attrs);
        }
    } else {
        context_ = XCreateIC(method_, XNInputStyle, style_, XNClientWindow, window_, XNFocusWindow, window_, nullptr);
    }
    if (!context_) return;
    long filterMask = 0;
    XGetICValues(context_, XNFilterEvents, &filterMask, nullptr);
    XSelectInput(display_, window_, eventMask_ | filterMask);
    XSetICFocus(context_);
    moved();
}
void X11Ime::destroyContext() {
    target_ = 0;
    preedit_.clear();
    cursor_ = 0;
    if (!context_) return;
    XUnsetICFocus(context_);
    XDestroyIC(context_);
    context_ = nullptr;
}
void X11Ime::update(const std::optional<LinuxImeState>& state) {
    const auto next = state ? state->target : 0;
    if (target_ != next) {
        // Each focused editor gets its own XIC. A late callback from the old
        // editor cannot be interpreted as input for the newly focused one.
        destroyContext();
        caret_.reset();
        target_ = next;
        if (!state) return;
        caret_ = state->caret;
        createContext();
    } else if (state && caret_ != state->caret) {
        caret_ = state->caret;
        moved();
    }
}
void X11Ime::moved() {
    if (!context_ || !caret_) return;
    XPoint location = spot(*caret_);
    if (auto attrs = XVaCreateNestedList(0, XNSpotLocation, &location, nullptr)) {
        XSetICValues(context_, XNPreeditAttributes, attrs, nullptr);
        XFree(attrs);
    }
}
bool X11Ime::lookup(XKeyEvent& key, std::u16string& text) {
    // Zero keycode is the synthetic XIM commit event, never a shortcut.
    if (!context_) return key.keycode == 0;
    char buffer[64];
    KeySym symbol = NoSymbol;
    Status status;
    int size = Xutf8LookupString(context_, &key, buffer, sizeof(buffer), &symbol, &status);
    std::vector<char> overflow;
    if (status == XBufferOverflow && size > 0) {
        overflow.resize(size);
        size = Xutf8LookupString(context_, &key, overflow.data(), size, &symbol, &status);
    }
    if (size > 0 && (status == XLookupChars || status == XLookupBoth))
        text = utf16FromUtf8(std::string_view(overflow.empty() ? buffer : overflow.data(), size));
    // A forwarded Ctrl chord can carry an ASCII control character as well
    // as its physical keycode. Preserve the ordinary shortcut dispatch.
    if (key.keycode != 0 && (status != XLookupChars || (key.state & (ControlMask | Mod1Mask)))) return false;
    if (!text.empty()) {
        LinuxImeEdit edit;
        edit.target = target_;
        edit.commit = std::move(text);
        deliver_(std::move(edit));
    }
    return true;
}
int X11Ime::started(XIC, XPointer data, XPointer) {
    auto& ime = *reinterpret_cast<X11Ime*>(data);
    ime.preedit_.clear();
    ime.cursor_ = 0;
    return -1;
}
void X11Ime::ended(XIM, XPointer data, XPointer) {
    auto& ime = *reinterpret_cast<X11Ime*>(data);
    ime.preedit_.clear();
    ime.publish(0);
}
void X11Ime::drawn(XIM, XPointer data, XPointer call) {
    reinterpret_cast<X11Ime*>(data)->draw(*reinterpret_cast<XIMPreeditDrawCallbackStruct*>(call));
}
void X11Ime::caretMoved(XIM, XPointer data, XPointer call) {
    auto& ime = *reinterpret_cast<X11Ime*>(data);
    auto& caret = *reinterpret_cast<XIMPreeditCaretCallbackStruct*>(call);
    if (caret.direction == XIMAbsolutePosition) ime.publish(caret.position);
    caret.position = ime.cursor_;
}
void X11Ime::disconnected(XIM, XPointer data, XPointer) {
    auto& ime = *reinterpret_cast<X11Ime*>(data);
    ime.method_ = nullptr;
    ime.context_ = nullptr;
    ended(nullptr, data, nullptr);
}
void X11Ime::draw(const XIMPreeditDrawCallbackStruct& change) {
    const auto first = static_cast<std::size_t>(std::max(0, change.chg_first));
    if (first > preedit_.size()) return;
    std::u32string replacement;
    if (const auto* text = change.text; text && text->length) {
        // XIM splice positions and caret count characters. Convert to the
        // shared editor's UTF-16 only after assembling the character buffer.
        if (text->encoding_is_wchar && text->string.wide_char) {
            const auto* wide = text->string.wide_char;
            replacement.assign(wide, wide + text->length);
        } else if (!text->encoding_is_wchar && text->string.multi_byte) {
            try {
                replacement = Utf32{}.from_bytes(text->string.multi_byte);
            } catch (const std::range_error&) {
                return;
            }
        } else if (text->feedback) {
            publish(change.caret);
            return;
        }
    }
    preedit_.replace(first, static_cast<std::size_t>(std::max(0, change.chg_length)), replacement);
    publish(change.caret);
}
void X11Ime::publish(int caret) {
    if (!target_) return;
    cursor_ = std::clamp(caret, 0, static_cast<int>(preedit_.size()));
    Utf32 utf;
    LinuxImeEdit edit;
    edit.target = target_;
    edit.preedit = utf16FromUtf8(utf.to_bytes(preedit_));
    edit.cursor = static_cast<unsigned>(utf16FromUtf8(utf.to_bytes(preedit_.data(), preedit_.data() + cursor_)).size());
    edit.replacePreedit = true;
    deliver_(std::move(edit));
}
}
