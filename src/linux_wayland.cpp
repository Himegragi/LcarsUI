#include "linux_window.hpp"
#include "linux_wayland_clipboard.hpp"
#include "linux_wayland_ime.hpp"
#include "renderer_opengl_egl.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <EGL/eglext.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace isoduo::detail {
namespace {
class WaylandWindow final : public LinuxWindow {
public:
    explicit WaylandWindow(const DesktopConfig& config) {
        display_ = wl_display_connect(nullptr);
        if (!display_) throw StartupError{{StartupFailureKind::window, {}, {}, u"Unable to connect to the Wayland display"}};
        try {
            clipboard_ = std::make_unique<WaylandClipboard>(display_, [this](auto result) { clipboardDelivered(std::move(result)); });
            ime_ = std::make_unique<WaylandIme>([this](auto edit) { imeEdited(std::move(edit)); });
            logicalWidth_ = pendingWidth_ = config.width;
            logicalHeight_ = pendingHeight_ = config.height;
            registry_ = wl_display_get_registry(display_);
            wl_registry_add_listener(registry_, &registryListener_, this);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            bool registryReady = false;
            auto destroy = [](wl_callback* callback) { if (callback) wl_callback_destroy(callback); };
            std::unique_ptr<wl_callback, decltype(destroy)> sync(wl_display_sync(display_), destroy);
            static constexpr wl_callback_listener syncListener{[](void* ready, wl_callback*, uint32_t) {
                *static_cast<bool*>(ready) = true;
            }};
            if (sync) wl_callback_add_listener(sync.get(), &syncListener, &registryReady);
            if (!sync || !waitFor(registryReady, deadline) || !compositor_ || !shell_)
                throw StartupError{{StartupFailureKind::window, {}, {}, u"Wayland compositor or xdg-shell unavailable"}};
            surface_ = wl_compositor_create_surface(compositor_);
            if (!surface_) throw StartupError{{StartupFailureKind::resource, {}, {}, u"Unable to allocate the Wayland surface"}};
            ime_->surface(surface_);
            wl_surface_add_listener(surface_, &surfaceListener_, this);
            if (fractionalManager_ && viewporter_) {
                viewport_ = wp_viewporter_get_viewport(viewporter_, surface_);
                fractional_ = wp_fractional_scale_manager_v1_get_fractional_scale(fractionalManager_, surface_);
                if (!viewport_ || !fractional_)
                    throw StartupError{{StartupFailureKind::resource, {}, {}, u"Unable to allocate Wayland fractional scaling objects"}};
                wp_fractional_scale_v1_add_listener(fractional_, &fractionalListener_, this);
            }
            shellSurface_ = xdg_wm_base_get_xdg_surface(shell_, surface_);
            if (!shellSurface_) throw StartupError{{StartupFailureKind::resource, {}, {}, u"Unable to allocate xdg_surface"}};
            xdg_surface_add_listener(shellSurface_, &shellSurfaceListener_, this);
            toplevel_ = xdg_surface_get_toplevel(shellSurface_);
            if (!toplevel_) throw StartupError{{StartupFailureKind::resource, {}, {}, u"Unable to allocate xdg_toplevel"}};
            xdg_toplevel_add_listener(toplevel_, &toplevelListener_, this);
            xdg_toplevel_set_app_id(toplevel_, "isoduo");
            title(utf8FromUtf16(config.title));
            wl_surface_commit(surface_);
            if (!waitFor(configured_, deadline))
                throw StartupError{{StartupFailureKind::window, {}, {}, u"Wayland did not configure the initial window"}};
            eglWindow_ = wl_egl_window_create(surface_, geometry_.width, geometry_.height);
            if (!eglWindow_) throw StartupError{{StartupFailureKind::resource, {}, {}, u"Unable to allocate the Wayland EGL window"}};
            egl_ = std::make_unique<EglWindow>(EGL_PLATFORM_WAYLAND_KHR, display_);
            egl_->nativeWindow(eglWindow_);
            reference_ = {DesktopBackend::wayland, display_, surface_, 0};
        } catch (...) {
            release();
            throw;
        }
    }
    ~WaylandWindow() override { release(); }
    const NativeWindow& reference() const override { return reference_; }
    EglWindow& renderWindow() override { return *egl_; }
    AsyncClipboard& clipboard() override { return *clipboard_; }
    void clipboardSerial(std::uint32_t serial) override { clipboard_->serial(serial); }
    void updateIme(const std::optional<LinuxImeState>& state, bool externalChange) override {
        Rect surfaceCaret{};
        if (state) {
            const auto rect = state->caret;
            const auto x = static_cast<int>(std::floor(rect.x / scaleX()));
            const auto y = static_cast<int>(std::floor(rect.y / scaleY()));
            surfaceCaret = {x, y, std::max(1, static_cast<int>(std::ceil((rect.x + rect.w) / scaleX())) - x),
                                  std::max(1, static_cast<int>(std::ceil((rect.y + rect.h) / scaleY())) - y)};
        }
        ime_->update(state, surfaceCaret, externalChange);
    }
    void pointerToClient(NativeEvent& event) const override {
        event.x = static_cast<int>(std::floor(wl_fixed_to_double(event.x) * scaleX()));
        event.y = static_cast<int>(std::floor(wl_fixed_to_double(event.y) * scaleY()));
    }
    void title(std::string_view value) override { xdg_toplevel_set_title(toplevel_, std::string(value).c_str()); }
    void fullscreenForTest() override { xdg_toplevel_set_fullscreen(toplevel_, nullptr); }
    bool prepareFrame() override { return !frameCallback_; }
    void requestFrame() override {
        frameCallback_ = wl_surface_frame(surface_);
        if (frameCallback_) {
            frameChangesGeometry_ = std::exchange(geometryChanged_, false);
            wl_callback_add_listener(frameCallback_, &frameListener_, this);
        }
    }
    void discardFrame() override {
        if (frameCallback_) wl_callback_destroy(std::exchange(frameCallback_, nullptr));
        geometryChanged_ |= std::exchange(frameChangesGeometry_, false);
    }
    bool poll(int timeoutMs) override {
        if (wl_display_dispatch_pending(display_) < 0) return false;
        if (!events.empty() || outputLeft_) timeoutMs = 0;
        if (repeat_) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(repeat_->next - Clock::now()).count();
            const int wait = static_cast<int>(std::max<std::int64_t>(0, remaining));
            if (timeoutMs < 0 || wait < timeoutMs) timeoutMs = wait;
        }
        while (wl_display_prepare_read(display_) != 0)
            if (wl_display_dispatch_pending(display_) < 0) return false;
        auto cancel = [this] { wl_display_cancel_read(display_); };
        short flags = POLLIN;
        if (wl_display_flush(display_) < 0) {
            if (errno != EAGAIN) { cancel(); return false; }
            flags |= POLLOUT;
        }
        std::vector<pollfd> fds{{wl_display_get_fd(display_), flags, 0}};
        clipboard_->preparePoll(fds, timeoutMs);
        int ready = pollFileDescriptors(fds, timeoutMs);
        if (ready < 0) { cancel(); return errno == EINTR; }
        if (fds.front().revents & (POLLERR | POLLHUP | POLLNVAL)) { cancel(); return false; }
        if (fds.front().revents & POLLIN) {
            if (wl_display_read_events(display_) < 0) return false;
        } else cancel();
        if (wl_display_dispatch_pending(display_) < 0) return false;
        // A minimize can take the surface off every output before xdg-shell
        // reports suspension. Finish transitions, but keep frame pacing under
        // the compositor's control. A handoff between outputs can send both
        // leave and enter in the same batch.
        if (std::exchange(outputLeft_, false) &&
            std::none_of(outputs_.begin(), outputs_.end(), [](const auto& entry) { return entry.second.entered; }))
            events.emplace_back(NativeEvent::Kind::leftOutputs);
        if (repeat_ && Clock::now() >= repeat_->next) {
            emitKey(repeat_->code, true, std::nullopt);
            repeat_->next = Clock::now() + std::chrono::milliseconds(std::max(1, 1000 / repeatRate_));
        }
        clipboard_->dispatchIo();
        return true;
    }

private:
    bool waitFor(const bool& ready, std::chrono::steady_clock::time_point deadline) {
        while (!ready) {
            events.clear();
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0 || !poll(static_cast<int>(remaining.count()))) return false;
        }
        return true;
    }
    static WaylandWindow& self(void* data) { return *static_cast<WaylandWindow*>(data); }

    void release() {
        ime_.reset();
        clipboard_.reset();
        egl_.reset();
        if (eglWindow_) wl_egl_window_destroy(eglWindow_);
        if (frameCallback_) wl_callback_destroy(frameCallback_);
        if (toplevel_) xdg_toplevel_destroy(toplevel_);
        if (shellSurface_) xdg_surface_destroy(shellSurface_);
        if (fractional_) wp_fractional_scale_v1_destroy(fractional_);
        if (viewport_) wp_viewport_destroy(viewport_);
        if (surface_) wl_surface_destroy(surface_);
        if (pointer_) wl_pointer_release(pointer_);
        if (keyboard_) wl_keyboard_release(keyboard_);
        if (seat_) wl_seat_release(seat_);
        for (auto [id, output] : outputs_) wl_output_destroy(output.handle);
        if (shell_) xdg_wm_base_destroy(shell_);
        if (fractionalManager_) wp_fractional_scale_manager_v1_destroy(fractionalManager_);
        if (viewporter_) wp_viewporter_destroy(viewporter_);
        if (compositor_) wl_compositor_destroy(compositor_);
        if (registry_) wl_registry_destroy(registry_);
        wl_display_flush(display_);
        wl_display_disconnect(display_);
        if (keyState_) xkb_state_unref(keyState_);
        if (keyContext_) xkb_context_unref(keyContext_);
    }
    double scaleX() const { return static_cast<double>(geometry_.width) / logicalWidth_; }
    double scaleY() const { return static_cast<double>(geometry_.height) / logicalHeight_; }
    void updateGeometry() {
        if (!configured_) return;
        const bool supportsBufferScale = wl_surface_get_version(surface_) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION;
        int integerScale = 1;
        if (!fractionalScale_ && supportsBufferScale) {
            integerScale = preferredScale_ ? preferredScale_ : 1;
            if (!preferredScale_)
                for (auto [id, output] : outputs_)
                    if (output.entered) integerScale = std::max(integerScale, output.scale);
        }
        constexpr double scaleUnit = 120;
        const double numerator = fractionalScale_ ? fractionalScale_ : integerScale * scaleUnit;
        const auto pixels = [numerator](int dip) {
            return static_cast<int>(std::clamp(std::round(dip * numerator / scaleUnit), 1.0,
                                               static_cast<double>(std::numeric_limits<int>::max())));
        };
        LinuxClientGeometry next{pixels(logicalWidth_), pixels(logicalHeight_), static_cast<float>(numerator / scaleUnit)};
        if (supportsBufferScale && bufferScale_ != integerScale) {
            wl_surface_set_buffer_scale(surface_, integerScale);
            bufferScale_ = integerScale;
        }
        const std::pair destination{logicalWidth_, logicalHeight_};
        const bool viewportChanged = fractionalScale_ && viewportSize_ != destination;
        if (viewportChanged) {
            // The destination and the resized buffer take effect in the same surface commit.
            wp_viewport_set_destination(viewport_, logicalWidth_, logicalHeight_);
            viewportSize_ = destination;
        }
        if (geometry_ == next && !viewportChanged) return;
        if (eglWindow_ && (geometry_.width != next.width || geometry_.height != next.height))
            wl_egl_window_resize(eglWindow_, next.width, next.height, 0, 0);
        geometry_ = next;
        geometryChanged_ = true;
        events.emplace_back(NativeEvent::Kind::redraw);
    }
    void inputPosition(wl_fixed_t x, wl_fixed_t y) {
        x_ = x;
        y_ = y;
        pointerEvent(NativeEvent::Kind::motion);
    }
    NativeEvent& pointerEvent(NativeEvent::Kind kind) {
        auto& event = events.emplace_back(kind);
        // Preserve surface-local precision until the host consumes the event.
        event.x = x_;
        event.y = y_;
        return event;
    }
    struct PointerAxis {
        double delta = 0, remainder = 0;
        std::optional<int> ticks;
    };
    PointerAxis& axis(uint32_t kind) {
        return kind == WL_POINTER_AXIS_VERTICAL_SCROLL ? verticalAxis_ : horizontalAxis_;
    }
    void emitWheel(PointerAxis& axis, bool horizontal) {
        const double delta = axis.ticks ? *axis.ticks * (48.0 / 120.0) : axis.delta;
        const double pixels = (horizontal ? delta : -delta) * geometry_.scale + axis.remainder;
        const auto amount = static_cast<int>(std::lround(pixels));
        axis = {.delta = 0, .remainder = pixels - amount, .ticks = {}};
        if (!amount) return;
        using Kind = NativeEvent::Kind;
        auto& event = pointerEvent(horizontal ? Kind::horizontalWheel : Kind::wheel);
        event.amount = amount;
        event.shift = modifier(XKB_MOD_NAME_SHIFT);
    }
    bool modifier(const char* name) const {
        return keyState_ && xkb_state_mod_name_is_active(keyState_, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    }
    void keymap(uint32_t format, int fd, uint32_t size) {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size) { ::close(fd); return; }
        auto* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (data == MAP_FAILED) return;
        if (!keyContext_) keyContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        auto* map = keyContext_ ? xkb_keymap_new_from_buffer(keyContext_, static_cast<const char*>(data),
            size - 1, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS) : nullptr;
        munmap(data, size);
        if (!map) return;
        repeat_.reset();
        auto* state = xkb_state_new(map);
        xkb_keymap_unref(map);
        if (keyState_) xkb_state_unref(keyState_);
        keyState_ = state;
    }
    void key(uint32_t serial, uint32_t code, bool pressed) {
        if (!keyState_) return;
        if (!emitKey(code, pressed, serial)) return;
        const auto key = code + 8;
        if (pressed && repeatRate_ > 0 && xkb_keymap_key_repeats(xkb_state_get_keymap(keyState_), key))
            repeat_ = RepeatedKey{code, Clock::now() + std::chrono::milliseconds(repeatDelay_)};
        else if (!pressed && repeat_ && repeat_->code == code)
            repeat_.reset();
    }
    bool emitKey(uint32_t code, bool pressed, std::optional<uint32_t> serial) {
        const auto key = code + 8;
        const auto symbol = xkb_state_key_get_one_sym(keyState_, key);
        const bool alt = modifier(XKB_MOD_NAME_ALT);
        if (pressed && !linuxKeyDownAllowed(symbol, alt)) return false;
        auto& event = events.emplace_back(pressed ? NativeEvent::Kind::keyDown : NativeEvent::Kind::keyUp);
        event.inputSerial = serial;
        event.key = linuxKey(symbol, modifier(XKB_MOD_NAME_SHIFT), modifier(XKB_MOD_NAME_CTRL));
        event.key.repeat = !serial;
        if (pressed && !event.key.ctrl && !alt) {
            char text[16]{};
            int size = xkb_state_key_get_utf8(keyState_, key, text, sizeof(text));
            if (size > 0 && size < static_cast<int>(sizeof(text))) event.text = utf16FromUtf8({text, static_cast<std::size_t>(size)});
        }
        return true;
    }
    void keyboardFocus(bool active) {
        if (!active) repeat_.reset();
        events.emplace_back(NativeEvent::Kind::focus).focused = active;
        clipboard_->focused(active);
    }
    void changeSeat(uint32_t caps) {
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard_) {
            keyboard_ = wl_seat_get_keyboard(seat_);
            wl_keyboard_add_listener(keyboard_, &keyboardListener_, this);
        } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard_) {
            wl_keyboard_release(keyboard_);
            keyboard_ = nullptr;
            keyboardFocus(false);
        }
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer_) {
            pointer_ = wl_seat_get_pointer(seat_);
            wl_pointer_add_listener(pointer_, &pointerListener_, this);
        } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer_) {
            wl_pointer_release(pointer_);
            pointer_ = nullptr;
            verticalAxis_ = horizontalAxis_ = {};
            events.emplace_back(NativeEvent::Kind::captureLost);
        }
    }
    static const wl_registry_listener registryListener_;
    static const xdg_wm_base_listener shellListener_;
    static const xdg_surface_listener shellSurfaceListener_;
    static const xdg_toplevel_listener toplevelListener_;
    static const wl_callback_listener frameListener_;
    static const wl_surface_listener surfaceListener_;
    static const wp_fractional_scale_v1_listener fractionalListener_;
    static const wl_output_listener outputListener_;
    static const wl_seat_listener seatListener_;
    static const wl_keyboard_listener keyboardListener_;
    static const wl_pointer_listener pointerListener_;
    struct Output { wl_output* handle; int scale = 1; bool entered = false; };
    std::map<uint32_t, Output> outputs_;
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* shell_ = nullptr;
    wl_surface* surface_ = nullptr;
    wp_viewporter* viewporter_ = nullptr;
    wp_viewport* viewport_ = nullptr;
    wp_fractional_scale_manager_v1* fractionalManager_ = nullptr;
    wp_fractional_scale_v1* fractional_ = nullptr;
    xdg_surface* shellSurface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    wl_callback* frameCallback_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    xkb_context* keyContext_ = nullptr;
    xkb_state* keyState_ = nullptr;
    using Clock = std::chrono::steady_clock;
    struct RepeatedKey { uint32_t code; Clock::time_point next; };
    std::optional<RepeatedKey> repeat_;
    int repeatRate_ = 0, repeatDelay_ = 0;
    wl_egl_window* eglWindow_ = nullptr;
    std::unique_ptr<EglWindow> egl_;
    std::unique_ptr<WaylandClipboard> clipboard_;
    std::unique_ptr<WaylandIme> ime_;
    NativeWindow reference_{};
    int logicalWidth_ = 0, logicalHeight_ = 0, pendingWidth_ = 0, pendingHeight_ = 0;
    int preferredScale_ = 0, bufferScale_ = 1;
    uint32_t fractionalScale_ = 0;
    std::pair<int, int> viewportSize_;
    wl_fixed_t x_ = 0, y_ = 0;
    PointerAxis verticalAxis_, horizontalAxis_;
    bool configured_ = false, suspended_ = false, pendingSuspended_ = false;
    bool geometryChanged_ = false, frameChangesGeometry_ = false, outputLeft_ = false;
};

const wl_registry_listener WaylandWindow::registryListener_{
        [](void* data, wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
            auto& w = self(data);
            if (!std::strcmp(interface, wl_compositor_interface.name))
                w.compositor_ = static_cast<wl_compositor*>(wl_registry_bind(registry, id, &wl_compositor_interface, std::min(version, 6u)));
            else if (!std::strcmp(interface, xdg_wm_base_interface.name)) {
                w.shell_ = static_cast<xdg_wm_base*>(wl_registry_bind(registry, id, &xdg_wm_base_interface,
                    std::min({version, static_cast<uint32_t>(xdg_wm_base_interface.version), 6u})));
                xdg_wm_base_add_listener(w.shell_, &shellListener_, &w);
            } else if (!std::strcmp(interface, wl_data_device_manager_interface.name)) {
                w.clipboard_->bind(registry, id, version);
            } else if (!std::strcmp(interface, "zwp_text_input_manager_v3")) {
                w.ime_->bind(registry, id);
            } else if (!std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) && !w.fractionalManager_) {
                w.fractionalManager_ = static_cast<wp_fractional_scale_manager_v1*>(
                    wl_registry_bind(registry, id, &wp_fractional_scale_manager_v1_interface, 1));
            } else if (!std::strcmp(interface, wp_viewporter_interface.name) && !w.viewporter_) {
                w.viewporter_ = static_cast<wp_viewporter*>(wl_registry_bind(registry, id, &wp_viewporter_interface, 1));
            } else if (!std::strcmp(interface, wl_seat_interface.name) && version >= 5 && !w.seat_) {
                w.seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, id, &wl_seat_interface,
                    std::min(version, static_cast<uint32_t>(WL_POINTER_AXIS_VALUE120_SINCE_VERSION))));
                wl_seat_add_listener(w.seat_, &seatListener_, &w);
                w.clipboard_->seat(w.seat_);
                w.ime_->seat(w.seat_);
            } else if (!std::strcmp(interface, wl_output_interface.name) && version >= 2) {
                auto* output = static_cast<wl_output*>(wl_registry_bind(registry, id, &wl_output_interface, 2));
                w.outputs_.emplace(id, Output{output});
                wl_output_add_listener(output, &outputListener_, &w);
            }
        },
        [](void* data, wl_registry*, uint32_t id) {
            auto& w = self(data);
            auto output = w.outputs_.find(id);
            if (output == w.outputs_.end()) return;
            wl_output_destroy(output->second.handle);
            w.outputs_.erase(output);
            w.updateGeometry();
        }
};

const xdg_wm_base_listener WaylandWindow::shellListener_{
        [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); }
};

const xdg_surface_listener WaylandWindow::shellSurfaceListener_{
        [](void* data, xdg_surface* surface, uint32_t serial) {
            auto& w = self(data);
            xdg_surface_ack_configure(surface, serial);
            w.configured_ = true;
            w.logicalWidth_ = w.pendingWidth_;
            w.logicalHeight_ = w.pendingHeight_;
            if (w.suspended_ != w.pendingSuspended_) {
                w.suspended_ = w.pendingSuspended_;
                if (w.suspended_) w.discardFrame();
                w.events.emplace_back(NativeEvent::Kind::visibility).visible = !w.suspended_;
            }
            w.updateGeometry();
        }
};

const xdg_toplevel_listener WaylandWindow::toplevelListener_{
        [](void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states) {
            auto& w = self(data);
            w.pendingWidth_ = width > 0 ? width : w.logicalWidth_;
            w.pendingHeight_ = height > 0 ? height : w.logicalHeight_;
#ifdef XDG_TOPLEVEL_STATE_SUSPENDED_SINCE_VERSION
            const auto* begin = static_cast<const uint32_t*>(states->data);
            w.pendingSuspended_ = false;
            for (size_t i = 0; i < states->size / sizeof(*begin); ++i)
                w.pendingSuspended_ |= begin[i] == XDG_TOPLEVEL_STATE_SUSPENDED;
#else
            (void)states;
#endif
        },
        [](void* data, xdg_toplevel*) { self(data).events.emplace_back(NativeEvent::Kind::close); },
        [](void*, xdg_toplevel*, int32_t, int32_t) {},
        [](void*, xdg_toplevel*, wl_array*) {}
};

const wl_callback_listener WaylandWindow::frameListener_{
        [](void* data, wl_callback* callback, uint32_t) {
            wl_callback_destroy(callback);
            auto& w = self(data);
            w.frameCallback_ = nullptr;
            // The compositor can reposition the surface when applying its resized
            // buffer. Re-send the same surface-local caret against that position.
            if (std::exchange(w.frameChangesGeometry_, false) && w.ime_->refreshCaret())
                w.events.emplace_back(NativeEvent::Kind::redraw);
        }
};

const wl_surface_listener WaylandWindow::surfaceListener_{
        [](void* data, wl_surface*, wl_output* output) {
            auto& w = self(data);
            for (auto& [id, item] : w.outputs_) if (item.handle == output) item.entered = true;
            w.updateGeometry();
        },
        [](void* data, wl_surface*, wl_output* output) {
            auto& w = self(data);
            for (auto& [id, item] : w.outputs_) if (item.handle == output && item.entered) {
                item.entered = false;
                w.outputLeft_ = true;
                break;
            }
            w.updateGeometry();
        },
        [](void* data, wl_surface*, int32_t scale) {
            self(data).preferredScale_ = std::max(1, scale);
            self(data).updateGeometry();
        },
        [](void*, wl_surface*, uint32_t) {}
};
const wp_fractional_scale_v1_listener WaylandWindow::fractionalListener_{
        [](void* data, wp_fractional_scale_v1*, uint32_t scale) {
            if (!scale) return;
            auto& w = self(data);
            w.fractionalScale_ = scale;
            w.updateGeometry();
        }
};

const wl_output_listener WaylandWindow::outputListener_{
        [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
        [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {},
        [](void* data, wl_output*) { self(data).updateGeometry(); },
        [](void* data, wl_output* output, int32_t scale) {
            for (auto& [id, item] : self(data).outputs_) if (item.handle == output) item.scale = std::max(1, scale);
        },
        nullptr, nullptr
};

const wl_seat_listener WaylandWindow::seatListener_{
        [](void* data, wl_seat*, uint32_t caps) { self(data).changeSeat(caps); },
        [](void*, wl_seat*, const char*) {}
};

const wl_keyboard_listener WaylandWindow::keyboardListener_{
        [](void* data, wl_keyboard*, uint32_t format, int fd, uint32_t size) { self(data).keymap(format, fd, size); },
        [](void* data, wl_keyboard*, uint32_t, wl_surface*, wl_array*) { self(data).keyboardFocus(true); },
        [](void* data, wl_keyboard*, uint32_t, wl_surface*) { self(data).keyboardFocus(false); },
        [](void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t code, uint32_t state) {
            self(data).key(serial, code, state == WL_KEYBOARD_KEY_STATE_PRESSED);
        },
        [](void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
            if (auto* state = self(data).keyState_) xkb_state_update_mask(state, depressed, latched, locked, 0, 0, group);
        },
        [](void* data, wl_keyboard*, int32_t rate, int32_t delay) {
            auto& w = self(data);
            w.repeat_.reset();
            w.repeatRate_ = rate;
            w.repeatDelay_ = std::max(0, delay);
        }
};

const wl_pointer_listener WaylandWindow::pointerListener_{
        [](void* data, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t x, wl_fixed_t y) { self(data).inputPosition(x, y); },
        [](void* data, wl_pointer*, uint32_t, wl_surface*) {
            auto& w = self(data);
            w.verticalAxis_ = w.horizontalAxis_ = {};
            w.events.emplace_back(NativeEvent::Kind::leave);
        },
        [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { self(data).inputPosition(x, y); },
        [](void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
            if (button != BTN_LEFT && button != BTN_RIGHT) return;
            auto& w = self(data);
            using Kind = NativeEvent::Kind;
            const bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
            const auto kind = button == BTN_LEFT ? (pressed ? Kind::press : Kind::release)
                                                 : (pressed ? Kind::rightPress : Kind::rightRelease);
            auto& event = w.pointerEvent(kind);
            event.shift = w.modifier(XKB_MOD_NAME_SHIFT);
            event.inputSerial = serial;
        },
        [](void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
            self(data).axis(axis).delta += wl_fixed_to_double(value);
        },
        [](void* data, wl_pointer*) {
            auto& w = self(data);
            w.emitWheel(w.verticalAxis_, false);
            w.emitWheel(w.horizontalAxis_, true);
        },
        [](void*, wl_pointer*, uint32_t) {},
        [](void*, wl_pointer*, uint32_t, uint32_t) {},
        [](void* data, wl_pointer*, uint32_t axis, int32_t steps) {
            auto& input = self(data).axis(axis);
            input.ticks = input.ticks.value_or(0) + steps * 120;
        },
        [](void* data, wl_pointer*, uint32_t axis, int32_t ticks) {
            auto& input = self(data).axis(axis);
            input.ticks = input.ticks.value_or(0) + ticks;
        },
        nullptr
};
}

std::unique_ptr<LinuxWindow> makeWaylandWindow(const DesktopConfig& config) { return std::make_unique<WaylandWindow>(config); }
}
