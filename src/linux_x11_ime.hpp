#pragma once
#include "linux_ime.hpp"
#include <X11/Xlib.h>
#include <functional>

namespace isoduo::detail {
class X11Ime {
public:
    X11Ime(Display*, Window, long eventMask, std::function<void(LinuxImeEdit)> deliver);
    ~X11Ime();
    void update(const std::optional<LinuxImeState>&);
    bool lookup(XKeyEvent&, std::u16string& text);
    void moved();

private:
    void destroyContext();
    void createContext();
    void publish(int caret);
    void draw(const XIMPreeditDrawCallbackStruct&);
    static int started(XIC, XPointer, XPointer);
    static void ended(XIM, XPointer, XPointer);
    static void drawn(XIM, XPointer, XPointer);
    static void caretMoved(XIM, XPointer, XPointer);
    static void disconnected(XIM, XPointer, XPointer);

    Display* display_;
    Window window_;
    long eventMask_;
    std::function<void(LinuxImeEdit)> deliver_;
    XIM method_ = nullptr;
    XIC context_ = nullptr;
    XIMStyle style_ = 0;
    std::uint64_t target_ = 0;
    std::u32string preedit_;
    int cursor_ = 0;
    std::optional<Rect> caret_;
};
}
