#include "linux_window.hpp"
#include "renderer_opengl_egl.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include <EGL/eglext.h>
#include <X11/XKBlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <xkbcommon/xkbcommon.h>
#include <poll.h>
#include <cerrno>
#include <climits>
#include <array>
#include <cstdlib>

namespace isoduo::detail {
namespace {
class X11Window final : public LinuxWindow {
public:
    explicit X11Window(const DesktopConfig& config) {
        display_ = XOpenDisplay(nullptr);
        if (!display_) throw StartupError{{StartupFailureKind::window, {}, {}, u"Unable to connect to the X11 display"}};
        try {
            if (config.width > USHRT_MAX || config.height > USHRT_MAX)
                throw StartupError{{StartupFailureKind::window, {}, {}, u"X11 client dimensions exceed the protocol limit"}};
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
            const uint32_t eventMask = ExposureMask | StructureNotifyMask | FocusChangeMask |
                KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask;
            width_ = config.width;
            height_ = config.height;
            // Checked requests keep server-side failures local to this creation. Xlib retains event queue ownership.
            const auto window = xcb_generate_id(connection);
            const uint32_t values[] = {0, eventMask, colormap};
            checked(connection, xcb_create_window_checked(connection, visual->depth, window, root, 0, 0, width_,
                height_, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual->visualid,
                XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, values), u"Unable to create the X11 window");
            window_ = window;
            protocols_ = XInternAtom(display_, "WM_PROTOCOLS", False);
            close_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
            titleProperty_ = XInternAtom(display_, "_NET_WM_NAME", False);
            utf8_ = XInternAtom(display_, "UTF8_STRING", False);
            XSetWMProtocols(display_, window_, &close_, 1);
            title(utf8FromUtf16(config.title));
            Bool detectable = False;
            XkbSetDetectableAutoRepeat(display_, True, &detectable);
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
    void title(std::string_view value) override {
        XChangeProperty(display_, window_, titleProperty_, utf8_, 8, PropModeReplace,
            reinterpret_cast<const unsigned char*>(value.data()), static_cast<int>(value.size()));
        XStoreName(display_, window_, std::string(value).c_str());
        XFlush(display_);
    }
    bool prepareFrame() override {
        if (!mapped_) {
            XMapWindow(display_, window_);
            XSync(display_, False);
            mapped_ = true;
        }
        return true;
    }
    bool poll(int timeoutMs) override {
        if (!XPending(display_)) {
            pollfd fd{ConnectionNumber(display_), POLLIN, 0};
            int ready = ::poll(&fd, 1, timeoutMs);
            if (ready < 0) return errno == EINTR;
            if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
            if (!ready) return true;
        }
        for (int count = 0; count < 256 && XPending(display_); ++count) {
            XEvent event{};
            XNextEvent(display_, &event);
            translate(event);
            if (!window_) break;
        }
        return true;
    }

private:
    static void checked(xcb_connection_t* connection, xcb_void_cookie_t request, std::u16string message) {
        std::unique_ptr<xcb_generic_error_t, decltype(&std::free)> error(xcb_request_check(connection, request), std::free);
        if (error) message += u" (X11 error " + utf16FromUtf8(std::to_string(error->error_code)) + u")";
        if (error || xcb_connection_has_error(connection))
            throw StartupError{{StartupFailureKind::window, {}, {}, std::move(message)}};
    }
    void release() {
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
            width_ = event.xconfigure.width;
            height_ = event.xconfigure.height;
            events.emplace_back(Kind::redraw);
            break;
        case DestroyNotify:
            window_ = 0;
            events.emplace_back(Kind::destroyed);
            break;
        case ClientMessage:
            if (event.xclient.message_type == protocols_ && event.xclient.format == 32 &&
                static_cast<Atom>(event.xclient.data.l[0]) == close_) events.emplace_back(Kind::close);
            break;
        case FocusIn:
        case FocusOut:
            events.emplace_back(Kind::focus).active = event.type == FocusIn;
            if (event.type == FocusOut) down_.fill(false);
            break;
        case LeaveNotify: events.emplace_back(Kind::leave); break;
        case MotionNotify: {
            auto& motion = events.emplace_back(Kind::motion);
            motion.x = event.xmotion.x;
            motion.y = event.xmotion.y;
            break;
        }
        case ButtonPress:
        case ButtonRelease: {
            auto& button = event.xbutton;
            if (button.button == Button1) {
                auto& input = events.emplace_back(event.type == ButtonPress ? Kind::press : Kind::release);
                input.x = button.x;
                input.y = button.y;
                input.active = (button.state & ShiftMask) != 0;
            } else if (event.type == ButtonPress && (button.button == Button4 || button.button == Button5)) {
                auto& wheel = events.emplace_back(Kind::wheel);
                wheel.x = button.x;
                wheel.y = button.y;
                wheel.amount = button.button == Button4 ? 120 : -120;
                wheel.active = (button.state & ShiftMask) != 0;
            }
            break;
        }
        case KeyPress:
        case KeyRelease: {
            auto& key = event.xkey;
            KeySym symbol = NoSymbol;
            unsigned consumed = 0;
            XkbLookupKeySym(display_, key.keycode, key.state, &consumed, &symbol);
            const bool pressed = event.type == KeyPress;
            auto& input = events.emplace_back(pressed ? Kind::keyDown : Kind::keyUp);
            input.key = linuxKey(static_cast<unsigned>(symbol), key.state & ShiftMask, key.state & ControlMask);
            if (key.keycode < down_.size()) {
                input.key.repeat = pressed && down_[key.keycode];
                down_[key.keycode] = pressed;
            }
            if (pressed && !(key.state & (ControlMask | Mod1Mask))) {
                char utf8[8]{};
                if (xkb_keysym_to_utf8(static_cast<xkb_keysym_t>(symbol), utf8, sizeof(utf8)) > 1)
                    input.text = utf16FromUtf8(utf8);
            }
            break;
        }
        }
    }

    Display* display_ = nullptr;
    Window window_ = 0;
    Colormap colormap_ = 0;
    Atom protocols_ = None, close_ = None, titleProperty_ = None, utf8_ = None;
    std::unique_ptr<EglWindow> egl_;
    NativeWindow reference_{};
    std::array<bool, 256> down_{};
    bool mapped_ = false;
};
}

std::unique_ptr<LinuxWindow> makeX11Window(const DesktopConfig& config) { return std::make_unique<X11Window>(config); }
}
