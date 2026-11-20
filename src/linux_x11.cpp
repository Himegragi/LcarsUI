#include "linux_window.hpp"
#include "renderer_opengl_egl.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include "linux_x11_clipboard.hpp"
#include "linux_x11_ime.hpp"
#include <EGL/eglext.h>
#include <X11/XKBlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <xkbcommon/xkbcommon.h>
#include <poll.h>
#include <cerrno>
#include <climits>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <utility>
#include <cstdlib>

namespace isoduo::detail {
namespace {
class X11Window final : public LinuxWindow {
public:
    explicit X11Window(const DesktopConfig& config) {
        display_ = XOpenDisplay(nullptr);
        if (!display_) throw StartupError{{StartupFailureKind::window, {}, {}, u"Unable to connect to the X11 display"}};
        try {
            // RESOURCE_MANAGER is on screen zero, even when the window uses another X screen.
            resources_ = RootWindow(display_, 0);
            XSelectInput(display_, resources_, PropertyChangeMask);
            XrmInitialize();
            geometry_.scale = desktopScale();
            const double width = std::round(config.width * static_cast<double>(geometry_.scale));
            const double height = std::round(config.height * static_cast<double>(geometry_.scale));
            if (width > USHRT_MAX || height > USHRT_MAX)
                throw StartupError{{StartupFailureKind::window, {}, {}, u"X11 client dimensions exceed the protocol limit"}};
            geometry_.width = std::max(1, static_cast<int>(width));
            geometry_.height = std::max(1, static_cast<int>(height));
            egl_ = std::make_unique<EglWindow>(EGL_PLATFORM_X11_KHR, display_);
            XVisualInfo match{};
            match.visualid = egl_->visualId();
            match.screen = DefaultScreen(display_);
            int count = 0;
            std::unique_ptr<XVisualInfo, decltype(&XFree)> visual(
                XGetVisualInfo(display_, VisualIDMask | VisualScreenMask, &match, &count), XFree);
            if (!visual) throw StartupError{{StartupFailureKind::window, {}, {}, u"EGL visual unavailable on the X11 screen"}};
            const auto root = RootWindow(display_, match.screen);
            auto* connection = XGetXCBConnection(display_);
            const auto colormap = xcb_generate_id(connection);
            checked(connection, xcb_create_colormap_checked(connection, XCB_COLORMAP_ALLOC_NONE, colormap,
                root, visual->visualid), u"Unable to allocate the X11 colormap");
            colormap_ = colormap;
            const uint32_t eventMask = ExposureMask | StructureNotifyMask | FocusChangeMask | VisibilityChangeMask |
                KeyPressMask | KeyReleaseMask | KeymapStateMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                EnterWindowMask | LeaveWindowMask;
            // Checked requests keep server-side failures local to this creation. Xlib retains event queue ownership.
            const auto window = xcb_generate_id(connection);
            const uint32_t values[] = {0, eventMask, colormap};
            checked(connection, xcb_create_window_checked(connection, visual->depth, window, root, 0, 0, geometry_.width,
                geometry_.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual->visualid,
                XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, values), u"Unable to create the X11 window");
            window_ = window;
            clipboard_ = std::make_unique<X11Clipboard>(display_, [this](auto result) { clipboardDelivered(std::move(result)); });
            ime_ = std::make_unique<X11Ime>(display_, window_, eventMask,
                [this](auto edit) { imeEdited(std::move(edit)); });
            protocols_ = XInternAtom(display_, "WM_PROTOCOLS", False);
            close_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
            titleProperty_ = XInternAtom(display_, "_NET_WM_NAME", False);
            utf8_ = XInternAtom(display_, "UTF8_STRING", False);
            XSetWMProtocols(display_, window_, &close_, 1);
            title(utf8FromUtf16(config.title));
            Bool detectable = False;
            XkbSetDetectableAutoRepeat(display_, True, &detectable);
            detectableRepeat_ = detectable;
            XSync(display_, False);
            egl_->nativeWindow(&window_);
            reference_ = {DesktopBackend::x11, display_, nullptr, window_};
        } catch (...) {
            release();
            throw;
        }
    }
    ~X11Window() override { release(); }
    const NativeWindow& reference() const override { return reference_; }
    EglWindow& renderWindow() override { return *egl_; }
    AsyncClipboard& clipboard() override { return *clipboard_; }
    void updateIme(const std::optional<LinuxImeState>& state, bool) override { ime_->update(state); }
    void handoffClipboard() override {
        if (!clipboard_->beginHandoff()) return;
        while (clipboard_->handingOff() && poll(-1)) events.clear();
    }
    void title(std::string_view value) override {
        XChangeProperty(display_, window_, titleProperty_, utf8_, 8, PropModeReplace,
            reinterpret_cast<const unsigned char*>(value.data()), static_cast<int>(value.size()));
        XStoreName(display_, window_, std::string(value).c_str());
        XFlush(display_);
    }
    std::optional<std::pair<int, int>> screenOrigin() const override {
        if (!window_) return {};
        auto* connection = XGetXCBConnection(display_);
        const auto request = xcb_translate_coordinates(connection, window_, DefaultRootWindow(display_), 0, 0);
        std::unique_ptr<xcb_translate_coordinates_reply_t, decltype(&std::free)> reply(
            xcb_translate_coordinates_reply(connection, request, nullptr), std::free);
        if (!reply || !reply->same_screen) return {};
        return std::pair{reply->dst_x, reply->dst_y};
    }
    bool prepareFrame() override {
        if (!mapRequested_) {
            XMapWindow(display_, window_);
            XFlush(display_);
            mapRequested_ = true;
        }
        return mapped_;
    }
    void fullscreenForTest() override {
        const auto state = XInternAtom(display_, "_NET_WM_STATE", False);
        const auto fullscreen = XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(display_, window_, state, XA_ATOM, 32, PropModeReplace,
            reinterpret_cast<const unsigned char*>(&fullscreen), 1);
    }
    bool poll(int timeoutMs) override {
        clipboard_->expire();
        if (!events.empty()) return true;
        clipboard_->limitWait(timeoutMs);
        if (!XPending(display_)) {
            pollfd fd{ConnectionNumber(display_), POLLIN, 0};
            int ready = pollFileDescriptors({&fd, 1}, timeoutMs);
            if (ready < 0) return errno == EINTR;
            if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
            if (!ready) { clipboard_->expire(); return true; }
        }
        for (int count = 0; count < 256 && XPending(display_); ++count) {
            XEvent event{};
            XNextEvent(display_, &event);
            // KeymapNotify has no window in the wire event.
            if (event.type == PropertyNotify && event.xproperty.window == resources_ &&
                event.xproperty.atom == XA_RESOURCE_MANAGER) {
                const auto scale = desktopScale();
                if (scale != geometry_.scale) {
                    geometry_.scale = scale;
                    events.emplace_back(NativeEvent::Kind::redraw);
                }
            } else if (event.type == KeymapNotify ||
                (!clipboard_->dispatch(event) && !XFilterEvent(&event, None) && event.xany.window == window_))
                translate(event);
            // Apply focus and edits before passing the next native event to XIM.
            if (!events.empty()) break;
        }
        clipboard_->expire();
        return true;
    }

private:
    float desktopScale() const {
        // Xfce publishes its desktop-wide DPI here. RandR's physical sizes and
        // output transforms do not specify a per-output application UI scale.
        auto* connection = XGetXCBConnection(display_);
        const auto request = xcb_get_property(connection, false, resources_, XA_RESOURCE_MANAGER, XA_STRING, 0, 65536);
        std::unique_ptr<xcb_get_property_reply_t, decltype(&std::free)> reply(
            xcb_get_property_reply(connection, request, nullptr), std::free);
        if (!reply || reply->format != 8 || reply->bytes_after) return 1.f;
        const auto* data = static_cast<const char*>(xcb_get_property_value(reply.get()));
        const std::string resources(data, xcb_get_property_value_length(reply.get()));
        std::unique_ptr<_XrmHashBucketRec, decltype(&XrmDestroyDatabase)> database(
            XrmGetStringDatabase(resources.c_str()), XrmDestroyDatabase);
        char* type = nullptr;
        XrmValue value{};
        if (!database || !XrmGetResource(database.get(), "Xft.dpi", "Xft.Dpi", &type, &value) || !value.size)
            return 1.f;
        const std::string_view text(value.addr, value.size - 1);
        const auto first = text.find_first_not_of(" \t");
        if (first == text.npos) return 1.f;
        const auto* end = text.data() + text.find_last_not_of(" \t") + 1;
        float dpi = 0;
        const auto parsed = std::from_chars(text.data() + first, end, dpi);
        return parsed.ec == std::errc{} && parsed.ptr == end && dpi >= 24.f && dpi <= 768.f ? dpi / 96.f : 1.f;
    }
    static void checked(xcb_connection_t* connection, xcb_void_cookie_t request, std::u16string message) {
        std::unique_ptr<xcb_generic_error_t, decltype(&std::free)> error(xcb_request_check(connection, request), std::free);
        if (error) message += u" (X11 error " + utf16FromUtf8(std::to_string(error->error_code)) + u")";
        if (error || xcb_connection_has_error(connection))
            throw StartupError{{StartupFailureKind::window, {}, {}, std::move(message)}};
    }
    void release() {
        ime_.reset();
        clipboard_.reset();
        egl_.reset();
        if (window_) XDestroyWindow(display_, window_);
        if (colormap_) XFreeColormap(display_, colormap_);
        XCloseDisplay(display_);
    }
    void translate(XEvent& event) {
        using Kind = NativeEvent::Kind;
        switch (event.type) {
        case Expose: events.emplace_back(Kind::redraw); break;
        case ConfigureNotify:
            ime_->moved();
            geometry_.width = event.xconfigure.width;
            geometry_.height = event.xconfigure.height;
            events.emplace_back(Kind::redraw);
            break;
        case DestroyNotify:
            window_ = 0;
            events.emplace_back(Kind::destroyed);
            break;
        case MapNotify:
            // The window manager can resize the client while handling MapRequest.
            readDrawableSize();
            [[fallthrough]];
        case UnmapNotify:
            mapped_ = event.type == MapNotify;
            events.emplace_back(Kind::visibility).visible = mapped_;
            break;
        case VisibilityNotify:
            events.emplace_back(Kind::visibility).visible = event.xvisibility.state != VisibilityFullyObscured;
            break;
        case ClientMessage:
            if (event.xclient.message_type == protocols_ && event.xclient.format == 32 &&
                static_cast<Atom>(event.xclient.data.l[0]) == close_) events.emplace_back(Kind::close);
            break;
        case FocusIn:
        case FocusOut:
            events.emplace_back(Kind::focus).focused = event.type == FocusIn;
            readFocusKeymap_ = event.type == FocusIn;
            if (!readFocusKeymap_) down_.fill(false);
            break;
        case KeymapNotify:
            // The server sends the key state at FocusIn, before any later key events.
            if (std::exchange(readFocusKeymap_, false)) {
                for (std::size_t code = 8; code < down_.size(); ++code) {
                    const auto bits = static_cast<unsigned char>(event.xkeymap.key_vector[code / 8]);
                    down_[code] = (bits & (1u << (code % 8))) != 0;
                }
            }
            break;
        case LeaveNotify: events.emplace_back(Kind::leave); break;
        case EnterNotify: {
            auto& motion = events.emplace_back(Kind::motion);
            motion.x = event.xcrossing.x;
            motion.y = event.xcrossing.y;
            break;
        }
        case MotionNotify: {
            auto& motion = events.emplace_back(Kind::motion);
            motion.x = event.xmotion.x;
            motion.y = event.xmotion.y;
            break;
        }
        case ButtonPress:
        case ButtonRelease: {
            auto& button = event.xbutton;
            if (button.button == Button1 || button.button == Button3) {
                const bool pressed = event.type == ButtonPress;
                const auto kind = button.button == Button1 ? (pressed ? Kind::press : Kind::release)
                                                          : (pressed ? Kind::rightPress : Kind::rightRelease);
                auto& input = events.emplace_back(kind);
                input.x = button.x;
                input.y = button.y;
                input.shift = (button.state & ShiftMask) != 0;
            } else if (event.type == ButtonPress && button.button >= 4 && button.button <= 7) {
                const bool horizontal = button.button >= 6;
                auto& wheel = events.emplace_back(horizontal ? Kind::horizontalWheel : Kind::wheel);
                wheel.x = button.x;
                wheel.y = button.y;
                const int step = static_cast<int>(std::lround(48 * geometry_.scale));
                wheel.amount = button.button == 4 || button.button == 7 ? step : -step;
                wheel.shift = (button.state & ShiftMask) != 0;
            }
            break;
        }
        case KeyPress:
        case KeyRelease: {
            auto& key = event.xkey;
            const bool pressed = event.type == KeyPress;
            if (!pressed && !detectableRepeat_ && XEventsQueued(display_, QueuedAfterReading)) {
                XEvent next{};
                XPeekEvent(display_, &next);
                if (next.type == KeyPress && next.xkey.window == key.window &&
                    next.xkey.keycode == key.keycode && next.xkey.time == key.time) break;
            }
            std::u16string text;
            if (pressed && ime_->lookup(key, text)) break;
            KeySym symbol = NoSymbol;
            unsigned consumed = 0;
            XkbLookupKeySym(display_, key.keycode, key.state, &consumed, &symbol);
            const bool alt = (key.state & Mod1Mask) != 0;
            if (pressed && !linuxKeyDownAllowed(static_cast<unsigned>(symbol), alt)) break;
            auto& input = events.emplace_back(pressed ? Kind::keyDown : Kind::keyUp);
            input.key = linuxKey(static_cast<unsigned>(symbol), key.state & ShiftMask, key.state & ControlMask);
            if (key.keycode < down_.size()) {
                input.key.repeat = pressed && down_[key.keycode];
                down_[key.keycode] = pressed;
            }
            if (pressed && !input.key.ctrl && !alt) {
                if (!text.empty()) input.text = std::move(text);
                else {
                    char utf8[8]{};
                    if (xkb_keysym_to_utf8(static_cast<xkb_keysym_t>(symbol), utf8, sizeof(utf8)) > 1)
                        input.text = utf16FromUtf8(utf8);
                }
            }
            break;
        }
        }
    }

    void readDrawableSize() {
        auto* connection = XGetXCBConnection(display_);
        const auto request = xcb_get_geometry(connection, window_);
        std::unique_ptr<xcb_get_geometry_reply_t, decltype(&std::free)> geometry(
            xcb_get_geometry_reply(connection, request, nullptr), std::free);
        if (!geometry) return;
        geometry_.width = geometry->width;
        geometry_.height = geometry->height;
    }

    Display* display_ = nullptr;
    Window window_ = 0, resources_ = 0;
    Colormap colormap_ = 0;
    Atom protocols_ = None, close_ = None, titleProperty_ = None, utf8_ = None;
    std::unique_ptr<EglWindow> egl_;
    std::unique_ptr<X11Clipboard> clipboard_;
    std::unique_ptr<X11Ime> ime_;
    NativeWindow reference_{};
    std::array<bool, 256> down_{};
    bool detectableRepeat_ = false;
    bool readFocusKeymap_ = false;
    bool mapRequested_ = false, mapped_ = false;
};
}

std::unique_ptr<LinuxWindow> makeX11Window(const DesktopConfig& config) { return std::make_unique<X11Window>(config); }
}
