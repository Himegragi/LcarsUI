#pragma once
#include "isoduo/desktop.hpp"
#include <memory>
#include <string>
#include <vector>

namespace isoduo::detail {
class EglWindow;
struct NativeEvent {
    enum class Kind { redraw, close, destroyed, focus, keyDown, keyUp, motion, leave, press, release, wheel };
    Kind kind;
    int x = 0, y = 0, amount = 0;
    bool active = false;
    KeyPress key;
    std::u16string text;
    explicit NativeEvent(Kind value) : kind(value) {}
};
class LinuxWindow {
public:
    virtual ~LinuxWindow() = default;
    virtual const NativeWindow& reference() const = 0;
    virtual EglWindow& renderWindow() = 0;
    virtual void title(std::string_view value) = 0;
    virtual bool poll(int timeoutMs) = 0;
    virtual bool prepareFrame() { return true; }
    virtual void requestFrame() {}
    virtual void discardFrame() {}
    int width() const { return width_; }
    int height() const { return height_; }
    int scale() const { return scale_; }
    std::vector<NativeEvent> events;

protected:
    int width_ = 0, height_ = 0, scale_ = 1;
};
std::unique_ptr<LinuxWindow> makeWaylandWindow(const DesktopConfig&);
std::unique_ptr<LinuxWindow> makeX11Window(const DesktopConfig&);
KeyPress linuxKey(unsigned symbol, bool shift, bool ctrl);
}
