#include "linux_window.hpp"
#include "linux_preferences.hpp"
#include "linux_accessibility.hpp"
#include "desktop_linux_test.hpp"
#include "desktop_paint.hpp"
#include "diagnostic_queue.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <poll.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <climits>
#include <set>
#include <utility>
#include <thread>

namespace isoduo::detail {
int LinuxWindow::pollFileDescriptors(std::span<pollfd> descriptors, int timeout) {
    if (preferences_) return preferences_->poll(descriptors, timeout);
    return ::poll(descriptors.data(), descriptors.size(), timeout);
}

bool linuxKeyDownAllowed(unsigned symbol, bool alt) {
    switch (symbol) {
    case XKB_KEY_Alt_L: case XKB_KEY_Alt_R: return true;
    case XKB_KEY_Control_L: case XKB_KEY_Control_R:
    case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
    case XKB_KEY_Super_L: case XKB_KEY_Super_R:
    case XKB_KEY_ISO_Level3_Shift: return false;
    default: return !alt;
    }
}
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
using PerfClock = std::chrono::steady_clock;
double elapsedMs(PerfClock::time_point begin, PerfClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
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
    std::optional<LinuxPreferenceMonitor> preferences;
    std::unique_ptr<LinuxWindow> window;
    std::unique_ptr<Renderer> renderer;
    RendererCapabilities caps;
    std::optional<RendererFailure> failure;
    Frame frame;
    std::optional<LinuxAccessibility> accessibility;
    Id imeField;
    std::uint64_t imeTarget = 0;
    bool imeComposing = false;
    std::string reveal;
    std::optional<TextFieldHandle> textDrag;
    std::optional<std::uint64_t> textScrollDeadline;
    std::pair<int, int> textDragPoint{};
    Id sliderDrag;
    struct ThumbDrag {
        ScrollState* state;
        ComponentHandle component;
        int grabOffset;
    };
    std::optional<ThumbDrag> thumbDrag;
    Id spaceTextTarget;
    bool focused = false, attached = false, dirty = true, closing = false, running = false;
    bool layoutDirty = true;
    bool leftPressed = false, rightPressed = false, hidden = false;
    bool animationScheduled = false;
    bool painting = false, arranging = false;
    struct WorkScope {
        bool& flag;
        explicit WorkScope(bool& active) : flag(active) { flag = true; }
        ~WorkScope() { flag = false; }
    };
    float atlasScale = 0;
    LinuxClientGeometry layoutGeometry;
    std::function<void(const Frame&, const LinuxClientGeometry&)> observeFrame;
    std::function<void(const LinuxFrameMeasurement&)> measureFrame;
    std::function<void()> observeRedraw;
    std::chrono::milliseconds paintDelay{};
    double pendingLayoutMs = 0;
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
        state.setAsyncClipboard(&window->clipboard());
        state.beforeFocus([this] { finishIme(); });
        state.rendererFailed(false);
        state.bindAppearance(&appearance);
        preferences.emplace();
        window->observePreferences(*preferences);
        refreshPreferences();
        state.animationTime(nowMs);
        state.tooltipClock(nowMs);
        state.setClickClock(nowMs);
        state.onInvalidate([this] { invalidateFrame(); });
        state.onRelayout([this] { if (!arranging && !painting && !closing) arrange(); });
        state.onReveal([this](const Id& id) { reveal = id; invalidateFrame(); });
        state.onPresentation([this] {
            configureFont();
            reveal = access(ui).focused();
            invalidateFrame();
        });
    }
    void refreshPreferences() {
        const auto settings = readLinuxDesktopPreferences();
        auto& state = access(ui);
        state.setSystemKeyboardCues(settings.keyboardCues.value_or(false));
        state.setSystemAnimations(settings.animations.value_or(true));
    }
    void release() {
        accessibility.reset();
        if (attached) {
            auto& state = access(ui);
            state.beforeFocus({});
            stopIme();
            state.compositionCancel();
            state.setAsyncClipboard(nullptr);
            state.onInvalidate({});
            state.onRelayout({});
            state.onReveal({});
            state.onPresentation({});
            state.animationTime({});
            state.tooltipClock({});
            state.setClickClock({});
            state.windowActive(false);
            state.cancelInput();
            state.setWindowHidden(false);
            state.bindAppearance(nullptr);
            attached = false;
        }
        renderer.reset();
        window.reset();
        preferences.reset();
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
    void prepareAtlas(float scale) {
        if (atlasScale && atlasScale != scale) text.clearAtlas();
        atlasScale = scale;
    }
    void invalidateFrame() {
        dirty = layoutDirty = true;
    }
    void arrangeIfNeeded() {
        // Input and IME may already have arranged the state we are about to paint.
        if (layoutDirty || layoutGeometry != window->geometry()) arrange();
    }
    void arrange() {
        const auto start = measureFrame ? PerfClock::now() : PerfClock::time_point{};
        WorkScope work(arranging);
        layoutDirty = false;
        layoutGeometry = window->geometry();
        const auto target = std::exchange(reveal, {});
        frame = isoduo::arrange(page(), ui, appearance, measure, layoutGeometry.width, layoutGeometry.height,
            layoutGeometry.scale, scroll, target);
        if (measureFrame) pendingLayoutMs += elapsedMs(start, PerfClock::now());
    }
    void stopIme() {
        if (imeField.empty()) return;
        imeField.clear();
        imeComposing = false;
        window->updateIme(std::nullopt, false);
    }
    void finishIme() {
        auto& state = access(ui);
        const auto* session = state.edit(state.focused());
        if (!session || !session->composing) return;
        auto text = session->preedit;
        stopIme();
        state.compositionCommit(text);
        invalidateFrame();
    }
    void syncIme(bool externalChange = true) {
        if (failure || closing || !focused) {
            stopIme();
            return;
        }
        auto& state = access(ui);
        const auto& id = state.focused();
        const auto* field = state.find(id);
        const auto* session = state.edit(id);
        const auto* placement = findPlacement(frame, id);
        if (!field || !field->enabled || field->readOnly || !session || !placement || state.menuOpen()) {
            if (session && session->composing) {
                state.compositionCancel();
                invalidateFrame();
            }
            stopIme();
            return;
        }
        if (imeField != id || (imeComposing && !session->composing)) {
            stopIme();
            imeField = id;
            ++imeTarget;
        }
        imeComposing = session->composing;
        window->updateIme(LinuxImeState{imeTarget, placement->caretRect(session->visibleCaret()),
            session->text, session->selection.anchor, session->selection.caret, field->multiline}, externalChange);
    }
    void editIme(LinuxImeEdit& edit) {
        auto& state = access(ui);
        if (imeField.empty() || imeField != state.focused() || edit.target != imeTarget) return;
        const auto* session = state.edit(imeField);
        if (!session || (imeComposing && !session->composing)) return;
        if (!edit.commit.empty() || edit.deleteBefore || edit.deleteAfter) {
            unsigned before = 0, after = 0;
            if (edit.deleteBefore || edit.deleteAfter) {
                const std::u16string_view text = session->text;
                const auto left = utf8FromUtf16(text.substr(0, session->begin()));
                const auto right = utf8FromUtf16(text.substr(session->end()));
                if (edit.deleteBefore > left.size() || edit.deleteAfter > right.size()) return;
                const auto start = left.size() - edit.deleteBefore;
                const auto boundary = [](char byte) { return (static_cast<unsigned char>(byte) & 0xc0) != 0x80; };
                if (!boundary(left[start]) || !boundary(right[edit.deleteAfter])) return;
                before = static_cast<unsigned>(utf16FromUtf8(std::string_view(left).substr(start)).size());
                after = static_cast<unsigned>(utf16FromUtf8(std::string_view(right).substr(0, edit.deleteAfter)).size());
            }
            state.compositionCommit(edit.commit, before, after);
        }
        if (edit.replacePreedit && state.focused() == imeField) {
            if (edit.preedit.empty()) state.compositionCancel();
            else state.compositionUpdate(std::move(edit.preedit), edit.cursor);
        }
        // A native cancel or commit is already reflected by the input method.
        session = state.edit(imeField);
        imeComposing = session && session->composing;
        invalidateFrame();
    }
    void paint() {
        dirty = false;
        WorkScope work(painting);
        try {
            arrangeIfNeeded();
            if (closing) return;
            const auto listStart = measureFrame ? PerfClock::now() : PerfClock::time_point{};
            const auto [width, height, scale] = layoutGeometry;
            prepareAtlas(scale);
            RecordingCanvas canvas(width, height, access(ui).effectivePalette(appearance).background);
            paintDesktop(canvas, frame, scale, focused, ui, appearance, text, measure);
            if (closing) return;
            window->requestFrame();
            if (measureFrame) {
                const auto beforeDelay = PerfClock::now();
                if (paintDelay.count()) std::this_thread::sleep_for(paintDelay);
                const double delayMs = elapsedMs(beforeDelay, PerfClock::now());
                double submissionMs = 0;
                PerfClock::time_point presented;
                const auto gpuMs = renderer->draw(canvas.list, width, height, true, false, &submissionMs, &presented);
                measureFrame({frame, layoutGeometry, presented, std::exchange(pendingLayoutMs, 0),
                    elapsedMs(listStart, beforeDelay), submissionMs, gpuMs, delayMs, focused,
                    access(ui).animationRunning()});
            } else {
                renderer->draw(canvas.list, width, height);
            }
            if (observeFrame) observeFrame(frame, layoutGeometry);
        } catch (const FrameError& error) {
            for (auto code : error.codes) diagnostics.push(frameDiagnostic(code));
            fail(error.failure);
        } catch (const std::exception& error) {
            fail({RendererFailureKind::resourceFailed, 0, utf16FromUtf8(error.what())});
        }
    }
    void fail(RendererFailure error) {
        failure = std::move(error);
        stopIme();
        access(ui).compositionCancel();
        cancelPointer();
        window->discardFrame();
        access(ui).finishAnimations();
        access(ui).rendererFailed(true);
        if (!config.onRendererFailed) closing = true;
    }
    void clearDrag() {
        textDrag.reset();
        textScrollDeadline.reset();
        sliderDrag.clear();
        thumbDrag.reset();
    }
    void cancelPointer() {
        auto& state = access(ui);
        clearDrag();
        leftPressed = rightPressed = false;
        state.cancelPointer();
        state.pointerLeft();
    }
    bool beginThumbDrag(const Placement* hit, int x, int y) {
        auto& state = access(ui);
        for (auto it = frame.regions.rbegin(); it != frame.regions.rend(); ++it) {
            if (!it->state || state.retiredComponent(it->component)) continue;
            const bool popup = hit && hit->dropdownPanel && hit->component == it->component;
            if ((frame.modal && !it->modalContent && !popup) || (hit && hit->dropdownPanel && !popup)) continue;
            if (!it->thumb.h || !it->clip.contains(x, y) || !it->track.contains(x, y)) continue;
            const int along = it->horizontal ? x : y;
            const int thumbStart = it->horizontal ? it->thumb.x : it->thumb.y;
            const int grabOffset = it->thumb.contains(x, y) ? along - thumbStart : (it->horizontal ? it->thumb.w : it->thumb.h) / 2;
            thumbDrag = ThumbDrag{it->state, it->component, grabOffset};
            state.hoverCapture({});
            if (frame.scrollTo(ui, *it, it->offsetForThumb(along - grabOffset))) invalidateFrame();
            return true;
        }
        return false;
    }
    void dragText(int x, int y) {
        textDragPoint = {x, y};
        arrangeIfNeeded();
        const auto* field = textDrag ? frame.find(*textDrag) : nullptr;
        if (!field || access(ui).focused() != field->id) { textScrollDeadline.reset(); return; }
        const auto& view = field->view;
        const bool scrollAbove = y < view.text.y && view.first > 0;
        const bool scrollBelow = y >= view.text.y + view.text.h && view.first + view.visible < view.lines.size();
        if (field->multiline && (scrollAbove || scrollBelow)) textScrollDeadline = nowMs() + 60;
        else textScrollDeadline.reset();
        if (frame.selectTo(ui, *textDrag, x, y)) invalidateFrame();
    }
    void pointer(const NativeEvent& event) {
        using Kind = NativeEvent::Kind;
        auto& state = access(ui);
        if (event.kind == Kind::leave) {
            if (!textDrag && !thumbDrag && sliderDrag.empty()) state.pointerLeft();
            return;
        }
        if (event.kind == Kind::press || event.kind == Kind::rightPress) finishIme();
        arrangeIfNeeded();
        if (event.kind == Kind::wheel || event.kind == Kind::horizontalWheel) {
            const bool scrolled = event.kind == Kind::wheel
                ? frame.wheel(ui, event.x, event.y, event.amount, event.shift)
                : !state.menuOpen() && frame.hwheel(ui, event.x, event.y, event.amount);
            if (scrolled) invalidateFrame();
            return;
        }
        if (event.kind == Kind::rightPress) {
            state.pointerPressed();
            rightPressed = true;
            if (state.menuOpen() && !frame.menuContains(event.x, event.y)) {
                state.closeMenu();
                rightPressed = false;
            }
            return;
        }
        if (event.kind == Kind::rightRelease && !std::exchange(rightPressed, false)) return;
        if (event.kind == Kind::press) {
            leftPressed = true;
            state.pointerPressed();
        }
        if (event.kind == Kind::release && !std::exchange(leftPressed, false)) return;
        if (state.menuOpen()) {
            auto row = frame.menuItemAt(event.x, event.y);
            if (event.kind == Kind::motion && row) state.menuHighlight(*row);
            if (event.kind == Kind::press) {
                if (!frame.menuContains(event.x, event.y)) {
                    leftPressed = false;
                    state.closeMenu();
                } else if (row) state.menuPointerDown(*row);
            }
            if (event.kind == Kind::release) state.menuPointerUp(row ? *row : std::string{});
            if (event.kind == Kind::rightRelease && row) {
                state.menuPointerDown(*row);
                state.menuPointerUp(*row);
            }
            return;
        }
        const auto* hit = frame.hit(event.x, event.y);
        const auto option = hit ? frame.optionAt(*hit, event.x, event.y) : std::nullopt;
        const Id id = hit ? hit->id : Id{};
        const bool selectsText = hit && componentType(hit->kind).selectsText();
        if (event.kind == Kind::motion || event.kind == Kind::press)
            state.pointerOver(frame, event.x, event.y);
        if (event.kind == Kind::rightRelease) {
            if (!hit || !hit->dropdownPanel) state.closePopup();
            if (!hit || hit->dropdownPanel || hit->tooltip) return;
            std::optional<unsigned> offset;
            if (selectsText) offset = placementTextHit(frame, id, event.x, event.y).offset;
            state.menuPointerOpen(id, option, offset, {event.x, event.y});
        } else if (event.kind == Kind::motion) {
            if (textDrag) dragText(event.x, event.y);
            else if (!sliderDrag.empty()) state.sliderPointer(sliderDrag, event.x);
            else if (thumbDrag) {
                auto region = std::find_if(frame.regions.begin(), frame.regions.end(), [&](const ScrollRegion& item) {
                    return item.state == thumbDrag->state && item.component == thumbDrag->component;
                });
                if (region != frame.regions.end()) {
                    const int along = region->horizontal ? event.x : event.y;
                    if (frame.scrollTo(ui, *region, region->offsetForThumb(along - thumbDrag->grabOffset))) invalidateFrame();
                }
            }
        } else if (event.kind == Kind::press) {
            if (!hit || !hit->dropdownPanel) state.closePopup();
            if (beginThumbDrag(hit, event.x, event.y) || !hit) return;
            const auto& type = componentType(hit->kind);
            if (!type.acceptsPointer() || !state.pointerDown(id, option)) return;
            if (type.hasRowCursor()) reveal.clear(); // the pressed row is already visible
            const auto* placed = findPlacement(frame, id);
            if (!placed) return;
            if (selectsText) {
                auto handle = state.layoutHandle(id);
                auto* field = std::get_if<TextFieldHandle>(&handle);
                auto caret = placementTextHit(frame, id, event.x, event.y);
                auto cell = placementTextCell(frame, id, event.x, event.y);
                if (field && state.textPointerDown(id, caret, cell, event.x, event.y, event.shift)) {
                    textDrag = *field;
                    state.hoverCapture(id);
                }
            } else if (type.beginValueDrag(state, *placed, event.x, layoutGeometry.scale)) sliderDrag = id;
        } else if (event.kind == Kind::release) {
            if (!sliderDrag.empty()) {
                state.sliderPointer(sliderDrag, event.x);
                state.pointerUp(sliderDrag);
            } else if (!textDrag && !thumbDrag && !id.empty()) state.pointerUp(id, option);
            state.cancelPointer();
            clearDrag();
            state.hoverRelease();
            state.pointerOver(frame, event.x, event.y);
        }
    }
    void dispatch(NativeEvent& event, Desktop& desktop) {
        using Kind = NativeEvent::Kind;
        auto& state = access(ui);
        switch (event.kind) {
        case Kind::redraw:
            if (observeRedraw) observeRedraw();
            invalidateFrame();
            return;
        case Kind::destroyed: closing = true; return;
        case Kind::close:
            finishIme();
            if (!config.onCloseRequested || config.onCloseRequested(desktop) == CloseDecision::close) closing = true;
            return;
        case Kind::focus:
            if (!event.focused) {
                finishIme();
                stopIme();
            }
            focused = event.focused;
            state.windowActive(focused && !hidden);
            if (!focused) {
                // A recovery prompt can take focus while the last frame is frozen.
                if (!failure) state.closePopup();
                state.cancelInput();
                spaceTextTarget.clear();
                cancelPointer();
            }
            invalidateFrame();
            return;
        case Kind::captureLost:
            cancelPointer();
            return;
        case Kind::visibility:
            hidden = !event.visible;
            state.setWindowHidden(hidden);
            state.windowActive(focused && !hidden);
            if (hidden) cancelPointer();
            invalidateFrame();
            return;
        case Kind::leftOutputs:
            state.finishAnimations();
            return;
        case Kind::clipboard: {
            const bool transferFailed = event.clipboard->result.status == Status::failed;
            const auto result = state.completeClipboard(std::move(*event.clipboard));
            if (transferFailed && result.status == Status::failed) {
                diagnostics.push({.backend = config.backend == DesktopBackend::wayland ? u"Wayland" : u"X11",
                                  .severity = DiagnosticSeverity::error, .code = "clipboard.transfer_failed",
                                  .nativeMessage = utf8FromUtf16(result.message)});
            }
            return;
        }
        default: break;
        }
        if (failure) return;
        if (event.kind == Kind::imeEdit) {
            editIme(*event.ime);
            return;
        }
        if (event.inputSerial) window->clipboardSerial(*event.inputSerial);
        if (event.kind != Kind::keyDown && event.kind != Kind::keyUp) {
            window->pointerToClient(event);
            pointer(event);
            return;
        }
        if (event.kind == Kind::keyDown && event.key.key == Key::escape) {
            if (const auto* session = state.edit(state.focused()); session && session->composing) {
                stopIme();
                state.compositionCancel();
                invalidateFrame();
                return;
            }
        }
        if (event.kind == Kind::keyDown) arrangeIfNeeded();
        const auto previous = state.focused();
        if (event.kind == Kind::keyDown) {
            const bool menu = state.menuOpen();
            if (event.key.key == Key::space && !event.key.repeat) {
                const auto* component = state.find(previous);
                spaceTextTarget = !menu && component && componentType(component->kind).selectsText() ? previous : Id{};
            }
            state.keyDown(event.key);
            // Keyboard navigation can change retained viewport offsets or the
            // selection without an invalidation from the component.
            invalidateFrame();
            if (const auto* component = state.find(state.focused());
                component && revealsRowCursor(component->kind, event.key.key)) reveal = component->id;
            const bool typingSpace = event.key.key != Key::space || (!spaceTextTarget.empty() && spaceTextTarget == previous);
            if (!closing && typingSpace && previous == state.focused() && !event.text.empty() && event.text.front() >= u' ' &&
                event.text.front() != u'\x7f') state.textInput(event.text);
        } else {
            if (event.key.key == Key::space) spaceTextTarget.clear();
            state.keyUp(event.key.key);
        }
        if (previous != state.focused()) {
            reveal = state.focused();
            invalidateFrame();
        }
    }
    int delay() {
        if (failure) return -1;
        int timeout = animationScheduled || access(ui).animationRunning() ? 16 : -1;
        const auto now = nowMs();
        for (auto deadline : {access(ui).tooltipDeadline(), textScrollDeadline}) if (deadline) {
            int wait = *deadline <= now ? 0 : static_cast<int>(std::min<std::uint64_t>(*deadline - now, INT_MAX));
            timeout = timeout < 0 ? wait : std::min(timeout, wait);
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
GpuComparison LinuxDesktopTest::compareDrawList(Desktop& desktop, const DrawList& list, int width, int height) {
    return detail::compareDrawList(*desktop.impl_->renderer, list, width, height);
}
Frame LinuxDesktopTest::layoutFrame(Desktop& desktop, int width, int height, float scale, std::string_view reveal) {
    auto& host = *desktop.impl_;
    Desktop::Impl::WorkScope work(host.arranging);
    return isoduo::arrange(host.page(), host.ui, host.appearance, host.measure, width, height, scale,
                           host.scroll, reveal.empty() ? std::string_view(host.reveal) : reveal);
}
GpuComparison LinuxDesktopTest::compareFrame(Desktop& desktop, int width, int height, float scale, Frame& frame,
                                             int fault, std::string_view reveal) {
    auto& host = *desktop.impl_;
    frame = layoutFrame(desktop, width, height, scale, reveal);
    host.prepareAtlas(scale);
    RecordingCanvas canvas(width, height, access(host.ui).effectivePalette(host.appearance).background);
    paintDesktop(canvas, frame, scale, true, host.ui, host.appearance, host.text, host.measure);
    return detail::compareDrawList(*host.renderer, canvas.list, width, height, fault);
}
void LinuxDesktopTest::observeFrames(Desktop& desktop, std::function<void(const Frame&, const LinuxClientGeometry&)> observer) {
    desktop.impl_->observeFrame = std::move(observer);
}
void LinuxDesktopTest::measureFrames(Desktop& desktop, std::chrono::milliseconds delay,
                                    std::function<void(const LinuxFrameMeasurement&)> observer,
                                    std::function<void()> redraw) {
    auto& host = *desktop.impl_;
    host.paintDelay = delay;
    host.measureFrame = std::move(observer);
    host.observeRedraw = std::move(redraw);
    host.window->fullscreenForTest();
}
void LinuxDesktopTest::configureAtlas(Desktop& desktop, int size, int pages) {
    desktop.impl_->text.configureAtlas(size, pages);
}
LinuxAtlasStats LinuxDesktopTest::atlasStats(const Desktop& desktop) {
    const auto& text = desktop.impl_->text;
    return {text.rasterizations(), text.atlasClears(), text.atlasBatches(), text.atlasPeakPages()};
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
    if (!host.closing && host.config.platformAccessibilityEnabled)
        host.accessibility.emplace(host.preferences->context(), host.ui, *host.window, host.frame, host.config.title, host.closing);
    while (!host.closing) {
        if (host.dirty && !host.hidden && !host.failure && host.window->prepareFrame()) {
            host.paint();
            if (host.failure && host.config.onRendererFailed) {
                const auto error = *host.failure;
                host.config.onRendererFailed(*this, error);
            }
        }
        host.diagnostics.drain();
        host.ui.semantics().dispatchEvents();
        if (host.accessibility) host.accessibility->flushLayout();
        if (host.closing) break;
        host.syncIme();
        if (host.dirty && !host.hidden && !host.failure && host.window->prepareFrame()) {
            if (!host.window->poll(0)) return {Status::failed, {}, {}};
        } else if (!host.window->poll(host.delay())) return {Status::failed, {}, {}};
        if (host.layoutGeometry != host.window->geometry()) host.invalidateFrame();
        auto events = std::exchange(host.window->events, {});
        for (auto& event : events) {
            host.dispatch(event, *this);
            if (host.closing) break;
            if (host.dirty && !host.failure) host.arrangeIfNeeded();
            host.syncIme(event.kind != NativeEvent::Kind::imeEdit);
        }
        if (!host.closing && host.preferences->changed()) host.refreshPreferences();
        if (!host.failure && !host.closing) {
            auto& state = access(host.ui);
            const auto deadline = state.tooltipDeadline();
            if (deadline && *deadline <= nowMs()) state.refreshTooltips();
            if (host.textScrollDeadline && *host.textScrollDeadline <= nowMs())
                host.dragText(host.textDragPoint.first, host.textDragPoint.second);
            const bool animating = state.animationRunning();
            // A timer that outlives the transition still paints the terminal frame.
            if (host.animationScheduled || animating) host.invalidateFrame();
            host.animationScheduled = animating;
        }
    }
    host.window->handoffClipboard();
    if (host.failure) return {Status::rendererFailed, host.failure, {}};
    return {};
}
Result Desktop::setTitle(std::u16string title) {
    if (impl_->owner != std::this_thread::get_id()) return {Status::wrongThread, wrongThread};
    if (!impl_->window) return {Status::notFound, u"Desktop window is closed"};
    if (!validSingleLine(title, title.size())) return {Status::invalidValue, u"Invalid title"};
    impl_->window->title(utf8FromUtf16(title));
    if (impl_->accessibility) impl_->accessibility->title(title);
    impl_->config.title = std::move(title);
    return {};
}
Result Desktop::close() {
    if (impl_->owner != std::this_thread::get_id()) return {Status::wrongThread, wrongThread};
    if (impl_->attached && !impl_->failure) impl_->finishIme();
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
    host.invalidateFrame();
    return {};
}
RunResult runDesktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page,
                     ScrollState& scroll, DesktopConfig config) {
    auto creation = Desktop::create(ui, appearance, std::move(page), scroll, std::move(config));
    if (!creation.desktop) return {creation.status, {}, std::move(creation.failure)};
    return creation.desktop->run();
}
}
