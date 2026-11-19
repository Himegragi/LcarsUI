#include "linux_window.hpp"
#include "linux_preferences.hpp"
#include "desktop_linux_test.hpp"
#include "desktop_paint.hpp"
#include "diagnostic_queue.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <climits>
#include <set>
#include <utility>
#include <thread>

namespace isoduo::detail {
KeyPress linuxKey(unsigned symbol, bool shift, bool ctrl) {
    Key key = Key::other;
    switch (symbol) {
    case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab: key = Key::tab; break;
    case XKB_KEY_Return: case XKB_KEY_KP_Enter: key = Key::enter; break;
    case XKB_KEY_space: key = Key::space; break;
    case XKB_KEY_Escape: key = Key::escape; break;
    case XKB_KEY_Left: case XKB_KEY_KP_Left: key = Key::left; break;
    case XKB_KEY_Right: case XKB_KEY_KP_Right: key = Key::right; break;
    case XKB_KEY_Up: case XKB_KEY_KP_Up: key = Key::up; break;
    case XKB_KEY_Down: case XKB_KEY_KP_Down: key = Key::down; break;
    case XKB_KEY_Home: case XKB_KEY_KP_Home: key = Key::home; break;
    case XKB_KEY_End: case XKB_KEY_KP_End: key = Key::end; break;
    case XKB_KEY_BackSpace: key = Key::backspace; break;
    case XKB_KEY_Delete: case XKB_KEY_KP_Delete: key = Key::del; break;
    case XKB_KEY_Page_Up: case XKB_KEY_KP_Page_Up: key = Key::pageUp; break;
    case XKB_KEY_Page_Down: case XKB_KEY_KP_Page_Down: key = Key::pageDown; break;
    case XKB_KEY_Menu: key = Key::menu; break;
    case XKB_KEY_F10: key = Key::f10; break;
    case XKB_KEY_Alt_L: case XKB_KEY_Alt_R: key = Key::alt; break;
    }
    char16_t letter = 0;
    if (symbol >= 'a' && symbol <= 'z') letter = static_cast<char16_t>(symbol - 'a' + 'A');
    else if (symbol >= 'A' && symbol <= 'Z') letter = static_cast<char16_t>(symbol);
    return {key, shift, ctrl, false, letter};
}
}

namespace isoduo {
namespace {
using namespace detail;
constexpr auto wrongThread = u"Operation requires the UI thread";
std::uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool hasEnv(const char* name) {
    auto value = std::getenv(name);
    return value && *value;
}
DesktopBackend selectedBackend(DesktopBackend requested) {
    if (requested != DesktopBackend::automatic) return requested;
    auto session = std::getenv("XDG_SESSION_TYPE");
    if (session && std::string_view(session) == "wayland") return DesktopBackend::wayland;
    if (session && std::string_view(session) == "x11") return DesktopBackend::x11;
    if (hasEnv("WAYLAND_DISPLAY") || hasEnv("WAYLAND_SOCKET")) return DesktopBackend::wayland;
    if (hasEnv("DISPLAY")) return DesktopBackend::x11;
    return DesktopBackend::automatic;
}
std::unique_ptr<LinuxWindow> openWindow(const DesktopConfig& config) {
    if (config.width <= 0 || config.height <= 0 || !validSingleLine(config.title, config.title.size()))
        throw StartupError{{StartupFailureKind::window, {}, {}, u"Invalid window dimensions or title"}};
    switch (config.backend) {
    case DesktopBackend::wayland: return makeWaylandWindow(config);
    case DesktopBackend::x11: return makeX11Window(config);
    case DesktopBackend::windows:
        throw StartupError{{StartupFailureKind::window, {}, {}, u"Windows desktop backend is unavailable on Linux"}};
    case DesktopBackend::automatic:
        throw StartupError{{StartupFailureKind::window, {}, {}, u"No native desktop session found"}};
    }
    throw StartupError{{StartupFailureKind::window, {}, {}, u"Unknown desktop backend"}};
}
}

struct Desktop::Impl {
    Ui& ui;
    const Appearance& appearance;
    std::function<layout::Node()> page;
    ScrollState& scroll;
    DesktopConfig config;
    RendererTestHooks* hooks;
    std::thread::id owner = std::this_thread::get_id();
    DiagnosticQueue diagnostics{config.onDiagnostic, &ui};
    Text text{systemFontFile, std::function<void(const RenderDiagnostic&)>(
        [this](const auto& event) { diagnostics.push(event); })};
    ShapedMeasure measure{text};
    std::unique_ptr<LinuxWindow> window;
    std::unique_ptr<Renderer> renderer;
    RendererCapabilities caps;
    std::optional<RendererFailure> failure;
    Frame frame;
    std::string reveal;
    std::string textDrag;
    bool focused = false, attached = false, dirty = true, closing = false, running = false;
    bool painting = false, arranging = false;
    struct WorkScope {
        bool& flag;
        explicit WorkScope(bool& active) : flag(active) { flag = true; }
        ~WorkScope() { flag = false; }
    };
    int atlasScale = 0;
    int textSize = 0;
    std::u16string fontFamily;
    std::set<std::u16string> unavailableFamilies;

    Impl(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
         DesktopConfig config, RendererTestHooks* hooks)
        : ui(ui), appearance(appearance), page(std::move(page)), scroll(scroll), config(std::move(config)), hooks(hooks) {}
    ~Impl() { release(); }
    void createRenderer() {
        renderer = std::make_unique<Renderer>(window->renderWindow(), hooks, config.debugContext,
            [this](auto event) { diagnostics.push(std::move(event)); });
        caps = renderer->capabilities();
    }
    void attach() {
        window = openWindow(config);
        createRenderer();
        text.setStyledLookup(systemStyledFontFile);
        auto& state = access(ui);
        configureFont();
        attached = true;
        state.rendererFailed(false);
        state.bindAppearance(&appearance);
        const auto preferences = readLinuxDesktopPreferences();
        state.setSystemKeyboardCues(preferences.keyboardCues.value_or(false));
        state.animationTime(nowMs);
        state.setSystemAnimations(preferences.animations.value_or(true));
        state.tooltipClock(nowMs);
        state.setClickClock(nowMs);
        state.onInvalidate([this] { dirty = true; });
        state.onRelayout([this] { if (!arranging && !painting && !closing) arrange(); });
        state.onReveal([this](const Id& id) { reveal = id; dirty = true; });
        state.onPresentation([this] {
            configureFont();
            reveal = access(ui).focused();
            dirty = true;
        });
    }
    void release() {
        if (attached) {
            auto& state = access(ui);
            state.onInvalidate({});
            state.onRelayout({});
            state.onReveal({});
            state.onPresentation({});
            state.animationTime({});
            state.tooltipClock({});
            state.setClickClock({});
            state.cancelInput();
            state.bindAppearance(nullptr);
            attached = false;
        }
        renderer.reset();
        window.reset();
    }
    void configureFont() {
        const auto& settings = ui.presentation();
        if (settings.textSize == textSize && settings.fontFamily == fontFamily) return;
        if (!textSize || settings.fontFamily != fontFamily) {
            const auto resolution = systemFontCatalog().resolve(settings);
            if (resolution.requestedAvailable || unavailableFamilies.insert(resolution.requested).second)
                enqueueFontDiagnostics(diagnostics, textSize ? 0 : systemFontCatalogError(), resolution);
            text.setFontResolution(resolution, systemEmojiFamily());
        } else {
            text.clearAtlas();
        }
        textSize = settings.textSize;
        fontFamily = settings.fontFamily;
    }
    void arrange() {
        WorkScope work(arranging);
        auto scale = window->scale();
        if (atlasScale && atlasScale != scale) text.clearAtlas();
        atlasScale = scale;
        frame = isoduo::arrange(page(), ui, appearance, measure, window->width() * scale, window->height() * scale,
            static_cast<float>(scale), scroll, reveal);
    }
    void paint() {
        dirty = false;
        WorkScope work(painting);
        try {
            arrange();
            reveal.clear();
            if (closing) return;
            const int width = window->width() * window->scale(), height = window->height() * window->scale();
            RecordingCanvas canvas(width, height, access(ui).effectivePalette(appearance).background);
            paintDesktop(canvas, frame, static_cast<float>(window->scale()), focused, ui, appearance, text, measure);
            if (closing) return;
            window->requestFrame();
            renderer->draw(canvas.list, width, height);
        } catch (const FrameError& error) {
            for (auto code : error.codes) diagnostics.push(frameDiagnostic(code));
            fail(error.failure);
        } catch (const std::exception& error) {
            fail({RendererFailureKind::resourceFailed, 0, utf16FromUtf8(error.what())});
        }
    }
    void fail(RendererFailure error) {
        failure = std::move(error);
        window->discardFrame();
        access(ui).finishAnimations();
        access(ui).rendererFailed(true);
        if (!config.onRendererFailed) closing = true;
    }
    void pointer(const NativeEvent& event) {
        using Kind = NativeEvent::Kind;
        auto& state = access(ui);
        if (event.kind == Kind::leave) {
            state.pointerLeft();
            return;
        }
        if (dirty) arrange();
        const auto* hit = frame.hit(event.x, event.y);
        const auto option = hit ? frame.optionAt(*hit, event.x, event.y) : std::nullopt;
        const Id id = hit ? hit->id : Id{};
        const bool selectsText = hit && componentType(hit->kind).selectsText();
        state.pointerOver(frame, event.x, event.y);
        if (event.kind == Kind::motion && !textDrag.empty()) {
            state.textPointerDrag(textDrag, placementTextHit(frame, textDrag, event.x, event.y),
                placementTextCell(frame, textDrag, event.x, event.y));
        } else if (event.kind == Kind::press) {
            state.pointerPressed();
            if (!state.pointerDown(id, option) || !selectsText) return;
            auto caret = placementTextHit(frame, id, event.x, event.y);
            auto cell = placementTextCell(frame, id, event.x, event.y);
            if (state.textPointerDown(id, caret, cell, event.x, event.y, event.active))
                textDrag = id;
        } else if (event.kind == Kind::release) {
            if (!textDrag.empty()) state.endTextDrag();
            textDrag.clear();
            state.pointerUp(id, option);
        } else if (event.kind == Kind::wheel) {
            frame.wheel(ui, event.x, event.y, event.amount, event.active);
            dirty = true;
        }
    }
    void dispatch(const NativeEvent& event, Desktop& desktop) {
        using Kind = NativeEvent::Kind;
        auto& state = access(ui);
        switch (event.kind) {
        case Kind::redraw: dirty = true; return;
        case Kind::destroyed: closing = true; return;
        case Kind::close:
            if (!config.onCloseRequested || config.onCloseRequested(desktop) == CloseDecision::close) closing = true;
            return;
        case Kind::focus:
            focused = event.active;
            if (!focused) { state.cancelInput(); textDrag.clear(); }
            dirty = true;
            return;
        default: break;
        }
        if (failure) return;
        if (event.kind == Kind::keyDown) {
            if (dirty) arrange();
            auto previous = state.focused();
            state.keyDown(event.key);
            if (!closing && previous == state.focused() && !event.text.empty() && event.text.front() >= u' ' &&
                event.text.front() != u'\x7f') state.textInput(event.text);
        } else if (event.kind == Kind::keyUp) state.keyUp(event.key.key);
        else pointer(event);
    }
    int delay() {
        if (failure) return -1;
        int timeout = access(ui).animationRunning() ? 16 : -1;
        if (auto deadline = access(ui).tooltipDeadline()) {
            auto now = nowMs();
            int tooltip = *deadline <= now ? 0 : static_cast<int>(std::min<std::uint64_t>(*deadline - now, INT_MAX));
            timeout = timeout < 0 ? tooltip : std::min(timeout, tooltip);
        }
        return timeout;
    }
};

Desktop::Desktop(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Desktop::~Desktop() = default;

CreateResult Desktop::create(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page,
                             ScrollState& scroll, DesktopConfig config) {
    return LinuxDesktopTest::create(ui, appearance, std::move(page), scroll, std::move(config), nullptr);
}
CreateResult LinuxDesktopTest::create(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page,
                                      ScrollState& scroll, DesktopConfig config, RendererTestHooks* hooks) {
    const auto backend = config.backend = selectedBackend(config.backend);
    try {
        auto impl = std::make_unique<Desktop::Impl>(ui, appearance, std::move(page), scroll, std::move(config), hooks);
        impl->attach();
        return {Status::ok, {}, std::unique_ptr<Desktop>(new Desktop(std::move(impl)))};
    } catch (const StartupError& error) {
        auto failure = error.failure;
        failure.backend = backend;
        return {Status::failed, std::move(failure), {}};
    } catch (const std::exception& error) {
        return {Status::failed, StartupFailure{StartupFailureKind::resource, {}, {}, utf16FromUtf8(error.what()), backend}, {}};
    }
}
RunResult Desktop::run() {
    auto& host = *impl_;
    if (host.owner != std::this_thread::get_id()) return {Status::wrongThread, {}, {}};
    if (host.running) return {Status::busy, {}, {}};
    if (!host.window) return {Status::notFound, {}, {}};
    host.running = true;
    struct RunScope {
        Impl& host;
        ~RunScope() { host.release(); host.running = false; }
    } run{host};
    while (!host.closing) {
        if (host.dirty && !host.failure && host.window->prepareFrame()) {
            host.paint();
            if (host.failure && host.config.onRendererFailed) {
                const auto error = *host.failure;
                host.config.onRendererFailed(*this, error);
            }
        }
        host.diagnostics.drain();
        host.ui.semantics().dispatchEvents();
        if (host.closing) break;
        if (host.dirty && !host.failure && host.window->prepareFrame()) {
            if (!host.window->poll(0)) return {Status::failed, {}, {}};
        } else if (!host.window->poll(host.delay())) return {Status::failed, {}, {}};
        auto events = std::exchange(host.window->events, {});
        for (const auto& event : events) {
            host.dispatch(event, *this);
            if (host.closing) break;
        }
        if (!host.failure && !host.closing) {
            auto& state = access(host.ui);
            const auto deadline = state.tooltipDeadline();
            if (deadline && *deadline <= nowMs()) state.refreshTooltips();
            if (state.animationRunning()) host.dirty = true;
        }
    }
    if (host.failure) return {Status::rendererFailed, host.failure, {}};
    return {};
}
Result Desktop::setTitle(std::u16string title) {
    if (impl_->owner != std::this_thread::get_id()) return {Status::wrongThread, wrongThread};
    if (!impl_->window) return {Status::notFound, u"Desktop window is closed"};
    if (!validSingleLine(title, title.size())) return {Status::invalidValue, u"Invalid title"};
    impl_->window->title(utf8FromUtf16(title));
    return {};
}
Result Desktop::close() {
    if (impl_->owner != std::this_thread::get_id()) return {Status::wrongThread, wrongThread};
    impl_->closing = true;
    return {};
}
const NativeWindow* Desktop::nativeWindow() const { return impl_->window ? &impl_->window->reference() : nullptr; }
RendererCapabilities Desktop::capabilities() const { return impl_->caps; }
bool Desktop::rendererFailed() const { return impl_->failure.has_value(); }
Result Desktop::rebuildRenderer() {
    auto& host = *impl_;
    if (host.owner != std::this_thread::get_id()) return {Status::wrongThread, wrongThread};
    if (!host.window) return {Status::notFound, u"Desktop window is closed"};
    if (host.painting) return {Status::busy, u"Painting in progress"};
    if (!host.failure) return {Status::invalidValue, u"Renderer is not failed"};
    host.renderer.reset();
    try {
        host.createRenderer();
        host.renderer->restoreAtlas(host.text.atlasPages());
    } catch (const StartupError& error) {
        host.renderer.reset();
        return {Status::failed, error.failure.message};
    } catch (const std::exception& error) {
        host.renderer.reset();
        return {Status::failed, utf16FromUtf8(error.what())};
    }
    host.failure.reset();
    access(host.ui).rendererFailed(false);
    host.dirty = true;
    return {};
}
RunResult runDesktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page,
                     ScrollState& scroll, DesktopConfig config) {
    auto creation = Desktop::create(ui, appearance, std::move(page), scroll, std::move(config));
    if (!creation.desktop) return {creation.status, {}, std::move(creation.failure)};
    return creation.desktop->run();
}
}
