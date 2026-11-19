#pragma once
#include "async_clipboard.hpp"
#include "isoduo/desktop.hpp"
#include "linux_ime.hpp"
#include "linux_preferences.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace isoduo::detail {
class EglWindow;
struct NativeEvent {
    enum class Kind {
        redraw, close, destroyed, focus, keyDown, keyUp, motion, leave, press, release,
        rightPress, rightRelease, wheel, horizontalWheel, captureLost, visibility, clipboard, imeEdit
    };
    Kind kind;
    int x = 0, y = 0, amount = 0;
    bool active = false;
    KeyPress key;
    std::u16string text;
    std::optional<std::uint32_t> inputSerial;
    std::optional<ClipboardDelivery> clipboard;
    std::optional<LinuxImeEdit> ime;
    explicit NativeEvent(Kind value) : kind(value) {}
};
class LinuxWindow {
public:
    virtual ~LinuxWindow() = default;
    virtual const NativeWindow& reference() const = 0;
    virtual EglWindow& renderWindow() = 0;
    virtual void title(std::string_view value) = 0;
    virtual bool poll(int timeoutMs) = 0;
    virtual AsyncClipboard& clipboard() = 0;
    virtual void clipboardSerial(std::uint32_t) {}
    virtual void handoffClipboard() {}
    virtual void updateIme(const std::optional<LinuxImeState>&, bool) {}
    virtual bool prepareFrame() { return true; }
    virtual void requestFrame() {}
    virtual void discardFrame() {}
    int width() const { return width_; }
    int height() const { return height_; }
    int scale() const { return scale_; }
    void observePreferences(LinuxPreferenceMonitor& monitor) { preferences_ = &monitor; }
    std::vector<NativeEvent> events;

protected:
    int pollFileDescriptors(std::span<pollfd> descriptors, int timeout);
    void imeEdited(LinuxImeEdit edit) {
        events.emplace_back(NativeEvent::Kind::imeEdit).ime = std::move(edit);
    }
    void clipboardDelivered(ClipboardDelivery delivery) {
        events.emplace_back(NativeEvent::Kind::clipboard).clipboard = std::move(delivery);
    }
    int width_ = 0, height_ = 0, scale_ = 1;

private:
    LinuxPreferenceMonitor* preferences_ = nullptr;
};
std::unique_ptr<LinuxWindow> makeWaylandWindow(const DesktopConfig&);
std::unique_ptr<LinuxWindow> makeX11Window(const DesktopConfig&);
KeyPress linuxKey(unsigned symbol, bool shift, bool ctrl);
bool linuxKeyDownAllowed(unsigned symbol, bool alt);
}
