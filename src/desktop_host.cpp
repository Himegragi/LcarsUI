#include "utf.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "desktop_host.hpp"
#include "uia_provider.hpp"
#include <windowsx.h>
#include <dwmapi.h>
#include <imm.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace isoduo::detail {
namespace {
bool systemKeyboardCues() {
    BOOL on = FALSE;
    return SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &on, 0) && on;
}
const wchar_t* windowClass = L"Isoduo.DesktopHost";
constexpr UINT_PTR tooltipTimer = 1;
constexpr UINT_PTR dragScrollTimer = 2;  // keeps a text drag scrolling while the pointer rests outside the field
constexpr UINT_PTR animationTimer = 3;    // distinct from the other timer ids: they run side by side
constexpr UINT animationFrameMs = 16;
std::uint64_t animationNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
// Windows "Show animations in Windows" (SPI_GETCLIENTAREAANIMATION). When it cannot be read, the
// Windows default (on) applies.
bool systemClientAnimations() {
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled != FALSE;
}
bool cloaked(HWND window) {
    BOOL value = FALSE;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &value, sizeof(value))) && value;
}
// Best effort: one visible, opaque, unshaped window above ours covers all of our client area.
// A window covered by several smaller ones, or by a see-through one, still counts as visible.
bool coveredByAnotherWindow(HWND window) {
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window, &topLeft); ClientToScreen(window, &bottomRight);
    if (bottomRight.x <= topLeft.x || bottomRight.y <= topLeft.y) return false;
    for (HWND above = GetWindow(window, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV)) {
        if (!IsWindowVisible(above) || IsIconic(above)) continue;
        if (GetWindowLongPtrW(above, GWL_EXSTYLE) & (WS_EX_TRANSPARENT | WS_EX_LAYERED)) continue;
        if (cloaked(above)) continue;
        HRGN shape = CreateRectRgn(0, 0, 0, 0);
        const bool shaped = shape && GetWindowRgn(above, shape) != ERROR;
        if (shape) DeleteObject(shape);
        if (shaped) continue;
        RECT bounds{};
        if (FAILED(DwmGetWindowAttribute(above, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) && !GetWindowRect(above, &bounds)) continue;
        if (bounds.left <= topLeft.x && bounds.top <= topLeft.y && bounds.right >= bottomRight.x && bounds.bottom >= bottomRight.y) return true;
    }
    return false;
}

Key mapKey(WPARAM vk) {
    switch (vk) {
    case VK_TAB: return Key::tab;
    case VK_RETURN: return Key::enter;
    case VK_SPACE: return Key::space;
    case VK_ESCAPE: return Key::escape;
    case VK_LEFT: return Key::left;
    case VK_RIGHT: return Key::right;
    case VK_UP: return Key::up;
    case VK_DOWN: return Key::down;
    case VK_HOME: return Key::home;
    case VK_END: return Key::end;
    case VK_PRIOR: return Key::pageUp;
    case VK_NEXT: return Key::pageDown;
    case VK_BACK: return Key::backspace;
    case VK_DELETE: return Key::del;
    case VK_APPS: return Key::menu;
    case VK_F10: return Key::f10;
    case VK_MENU: return Key::alt;
    default: return Key::other;
    }
}
std::u16string composition(HIMC imc, DWORD kind) {
    LONG bytes = ImmGetCompositionStringW(imc, kind, nullptr, 0);
    if (bytes <= 0 || bytes > 8192 || bytes % 2) return {};
    std::u16string value(static_cast<std::size_t>(bytes) / 2, u'\0');
    return ImmGetCompositionStringW(imc, kind, value.data(), bytes) == bytes ? value : std::u16string{};
}
std::wstring wide(std::u16string_view text) { return std::wstring(text.begin(), text.end()); }

LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    auto host = reinterpret_cast<DesktopHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        host = static_cast<DesktopHost*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        host->attach(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (!host) return DefWindowProcW(window, msg, w, l);
    // Extension or backend failure: stop this UI instead of letting C++ unwind into Win32.
    auto stop = [&](const char* what) {
        std::cerr << "Isoduo host: " << what << '\n';
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        PostQuitMessage(1);
        return LRESULT{0};
    };
    try { return host->message(msg, w, l); }
    catch (const std::exception& e) { return stop(e.what()); }
    catch (...) { return stop("unknown exception"); }
}
}

namespace {
// Clipboard managers can hold the global clipboard briefly after a change. Bound the wait
// on the UI thread; report failure rather than waiting indefinitely if another owner stalls.
bool openClipboard(HWND window) {
    constexpr ULONGLONG timeoutMs = 250;
    const auto deadline = GetTickCount64() + timeoutMs;
    do {
        if (OpenClipboard(window)) return true;
        auto now = GetTickCount64();
        if (now >= deadline) break;
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(5, deadline - now)));
    } while (GetTickCount64() < deadline);
    return false;
}
}

namespace {
UINT richFormat() {
    static const UINT format = RegisterClipboardFormatW(L"LcarsUI.RichText.1");
    return format;
}
HGLOBAL globalText(std::u16string_view text) {
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
    if (!memory) return nullptr;
    auto data = static_cast<char16_t*>(GlobalLock(memory));
    if (!data) { GlobalFree(memory); return nullptr; }
    std::memcpy(data, text.data(), text.size() * sizeof(char16_t));
    data[text.size()] = 0;
    GlobalUnlock(memory);
    return memory;
}
HGLOBAL globalBytes(std::string_view bytes) {  // NUL-terminated: a global block may be larger than asked for
    auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size() + 1);
    if (!memory) return nullptr;
    auto data = static_cast<char*>(GlobalLock(memory));
    if (!data) { GlobalFree(memory); return nullptr; }
    std::memcpy(data, bytes.data(), bytes.size());
    data[bytes.size()] = 0;
    GlobalUnlock(memory);
    return memory;
}
}
bool WindowsClipboard::write(std::u16string_view text) { return writeRich(text, {}); }
// One clipboard transaction: the plain text and, for a copy from a rich field, the private payload beside it.
// EmptyClipboard drops every older format, so a payload never outlives the text it belonged to.
bool WindowsClipboard::writeRich(std::u16string_view text, std::string_view payload) {
    if (!openClipboard(window_)) return false;
    auto memory = globalText(text);
    auto rich = payload.empty() ? nullptr : globalBytes(payload);
    bool ok = false;
    if (memory && (payload.empty() || rich)) {
        ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (ok) memory = nullptr;  // the clipboard owns it now
        if (ok && rich) {
            if (SetClipboardData(richFormat(), rich) != nullptr) rich = nullptr;  // plain text still stands if only this fails
        }
    }
    if (memory) GlobalFree(memory);
    if (rich) GlobalFree(rich);
    CloseClipboard();
    return ok;
}
Query<std::string> WindowsClipboard::readRich() {
    if (!openClipboard(window_)) return {Status::failed, u"無法開啟剪貼簿", std::nullopt};
    Query<std::string> result{Status::notSupported, u"No rich text", std::nullopt};
    if (IsClipboardFormatAvailable(richFormat())) {
        result = {Status::failed, u"Cannot read rich text", std::nullopt};
        if (auto memory = GetClipboardData(richFormat())) {
            if (auto data = static_cast<const char*>(GlobalLock(memory))) {
                const auto size = GlobalSize(memory);
                std::size_t n = 0;
                while (n < size && data[n]) ++n;
                if (n < size) result = {Status::ok, {}, std::string(data, n)};
                GlobalUnlock(memory);
            }
        }
    }
    CloseClipboard();
    return result;
}
Query<std::u16string> WindowsClipboard::read() {
    if (!openClipboard(window_)) return {Status::failed, u"\u7121\u6cd5\u958b\u555f\u526a\u8cbc\u7c3f", std::nullopt};
    Query<std::u16string> result{Status::notSupported, u"\u526a\u8cbc\u7c3f\u6c92\u6709\u6587\u5b57", std::nullopt};
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        result = {Status::failed, u"\u7121\u6cd5\u8b80\u53d6\u526a\u8cbc\u7c3f\u6587\u5b57", std::nullopt};
        if (auto memory = GetClipboardData(CF_UNICODETEXT)) {
            if (auto data = static_cast<const char16_t*>(GlobalLock(memory))) {
                auto units = GlobalSize(memory) / sizeof(char16_t);
                std::size_t n = 0;
                while (n < units && data[n]) ++n;
                if (n < units) result = {Status::ok, {}, std::u16string(data, n)};
                GlobalUnlock(memory);
            }
        }
    }
    CloseClipboard();
    return result;
}

DesktopHost::DesktopHost(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                         const DesktopConfig& options, bool trace, bool background, RendererTestHooks* hooks)
    : ui_(ui), appearance_(appearance), page_(std::move(page)), scroll_(scroll), diagnostics_(options.onDiagnostic,&ui),
      text_(systemFontFile, [this](const RenderDiagnostic& event) { diagnostics_.push(event); if(window_) PostMessageW(window_,WM_APP+43,0,0); }) {
    hooks_ = hooks; debugContext_ = options.debugContext;
    text_.setStyledLookup(systemStyledFontFile);
    configureFont(ui.presentation().fontFamily);
    tracing_ = trace;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = windowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw StartupError{{StartupFailureKind::window, {}, {}, u"Window registration failed"}};
    auto title = wide(options.title);
    HWND window = CreateWindowExW(background ? WS_EX_NOACTIVATE : 0, windowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, this);
    if (!window) {
        window_ = nullptr;  // WM_NCCREATE may have attached a window that creation then destroyed
        throw StartupError{{StartupFailureKind::window, {}, {}, u"Window creation failed"}};
    }
    try {
        if (hooks_ && !hooks_->startupResourceError.empty()) throw std::runtime_error(hooks_->startupResourceError);
        uia_ = std::make_unique<UiaBridge>(ui_, window_, [this]() -> const Frame& {
            RECT client{}; GetClientRect(window_, &client);
            if (client.right > 0 && client.bottom > 0) frame_ = arrange(client.right, client.bottom, scale());
            return frame_;
        });
        inputContext_ = ImmGetContext(window_);
        if (inputContext_) ImmReleaseContext(window_, inputContext_);
        clipboard_ = std::make_unique<WindowsClipboard>(window_);
        detail::access(ui_).setClipboard(clipboard_.get());
        detail::access(ui_).bindAppearance(&appearance_);
        detail::access(ui_).tooltipClock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
        detail::access(ui_).setSystemKeyboardCues(systemKeyboardCues());
        detail::access(ui_).animationTime(animationNowMs);
        refreshSystemAnimations();
        detail::access(ui_).setClickClock([] { return static_cast<std::uint64_t>(GetTickCount64()); });  // multi-click counting
        refreshVisibility();
        detail::access(ui_).onInvalidate([this] { if (window_) { scheduleTooltip(); InvalidateRect(window_, nullptr, FALSE); } });
        // Cursor keys need the current wrapping; a key can arrive before the paint that would settle it.
        detail::access(ui_).onRelayout([this] { relayout(); });
        detail::access(ui_).onSemanticPending([this] { if (window_) PostMessageW(window_, WM_APP + 45, 0, 0); });
        detail::access(ui_).onReveal([this](const Id& id) { reveal_ = id; if (window_) InvalidateRect(window_, nullptr, FALSE); });
        atlasTextSize_ = ui_.presentation().textSize;
        atlasFontFamily_ = ui_.presentation().fontFamily;
        detail::access(ui_).onPresentation([this] {
            const auto& settings = ui_.presentation();
            const auto clears = text_.atlasClears();
            configureFont(settings.fontFamily);
            if ((settings.textSize != atlasTextSize_ || settings.fontFamily != atlasFontFamily_) && text_.atlasClears() == clears)
                text_.clearAtlas();
            atlasTextSize_ = settings.textSize;
            atlasFontFamily_ = settings.fontFamily;
            if (!window_) return;
            RECT client{}; GetClientRect(window_, &client);
            if (client.right <= 0 || client.bottom <= 0) return;
            reveal_ = detail::access(ui_).focused();
            frame_ = arrange(client.right, client.bottom, scale());
            reveal_.clear();
            syncIme();
        });
        detail::access(ui_).beforeFocus([this] { finishComposition(); });
        renderer_ = std::make_unique<Renderer>(window_, hooks_, debugContext_, [this](RenderDiagnostic event) { diagnostics_.push(std::move(event)); });
        capabilities_ = renderer_->capabilities();
        clientSize(options.width, options.height);
        PostMessageW(window_,WM_APP+43,0,0);
    } catch (...) {
        release();  // the destructor will not run for a half-built host
        throw;
    }
}
Result DesktopHost::rebuildRenderer() {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (painting_) return {Status::busy, u"Painting in progress"};
    if (!rendererFailure_) return {Status::invalidValue, u"Renderer is not failed"};
    if (hooks_) ++hooks_->rebuildAttempts;
    renderer_.reset();
    struct ShaderOverride {
        RendererTestHooks* hooks;
        std::string previous;
        explicit ShaderOverride(RendererTestHooks* value): hooks(value) {
            if (hooks) { previous=hooks->fragmentShaderOverride; if (hooks->failRebuildOnce) { hooks->failRebuildOnce=false; hooks->fragmentShaderOverride="invalid shader"; } }
        }
        ~ShaderOverride() { if (hooks) hooks->fragmentShaderOverride=std::move(previous); }
    } shaderOverride(hooks_);
    try {
        renderer_ = std::make_unique<Renderer>(window_, hooks_, debugContext_, [this](RenderDiagnostic event) { diagnostics_.push(std::move(event)); });
        renderer_->restoreAtlas(text_.atlasPages());
        capabilities_=renderer_->capabilities();
    } catch (const StartupError& e) { renderer_.reset(); return {Status::failed,e.failure.message}; }
      catch (const std::exception& e) { renderer_.reset(); std::string text=e.what(); return {Status::failed,utf16FromUtf8(text)}; }
    rendererFailure_.reset(); detail::access(ui_).rendererFailed(false);
    scheduleTooltip();
    InvalidateRect(window_,nullptr,FALSE);
    return {};
}
void DesktopHost::failRenderer(RendererFailure failure) {
    rendererFailure_=std::move(failure);
    if (window_) { KillTimer(window_, tooltipTimer); KillTimer(window_, animationTimer); }
    detail::access(ui_).finishAnimations();
    detail::access(ui_).rendererFailed(true);
    if (auto session=detail::access(ui_).edit(detail::access(ui_).focused()); session && session->composing) {
        if (auto imc=ImmGetContext(window_)) { ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0); ImmReleaseContext(window_, imc); }
        detail::access(ui_).compositionCancel();
    }
    PostMessageW(window_,WM_APP+44,0,0);
}
void DesktopHost::configureFont(std::u16string_view family) {
    if(fontConfigured_ && requestedFont_==family) return;
    PresentationSettings settings; settings.fontFamily=family;
    auto resolution=systemFontCatalog().resolve(settings);
    enqueueFontDiagnostics(diagnostics_,fontConfigured_?0:systemFontCatalogError(),resolution);
    requestedFont_=family; fontConfigured_=true;
    text_.setPrimaryFamily(!family.empty() && resolution.requestedAvailable ? resolution.primary : std::u16string{}, true);
    if(window_) PostMessageW(window_,WM_APP+43,0,0);
}
DesktopHost::~DesktopHost() { release(); }
void DesktopHost::release() {
    if (uia_) { uia_->close(); uia_.reset(); }
    // A host may be destroyed while its Ui remains alive. Discard an unfinished
    // preedit on both sides before detaching the window; closing must not leave
    // an orphaned composition in the retained text session.
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (window_ && session && session->composing && !rendererFailure_) {
        if (auto imc = ImmGetContext(window_)) {
            ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(window_, imc);
        }
        detail::access(ui_).compositionCancel();
    }
    detail::access(ui_).onPresentation({});
    if (detail::access(ui_).boundAppearance() == &appearance_) detail::access(ui_).bindAppearance(nullptr);
    detail::access(ui_).onInvalidate({});
    detail::access(ui_).onRelayout({});
    detail::access(ui_).onSemanticPending({});
    detail::access(ui_).onReveal({});
    detail::access(ui_).tooltipClock({});
    detail::access(ui_).animationTime({});
    detail::access(ui_).setClickClock({});
    if (window_) { KillTimer(window_, tooltipTimer); KillTimer(window_, dragScrollTimer); KillTimer(window_, animationTimer); }
    detail::access(ui_).beforeFocus({});
    detail::access(ui_).setClipboard(nullptr);
    if (!window_) return;
    ImmAssociateContext(window_, inputContext_);
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    renderer_.reset();
    DestroyWindow(window_);
    window_ = nullptr;
}
float DesktopHost::scale() const { return forcedScale_ > 0 ? forcedScale_ : GetDpiForWindow(window_) / 96.f; }
void DesktopHost::clientSize(int widthDip, int heightDip) {
    float s = scale();
    RECT wanted{0, 0, static_cast<LONG>(std::lround(widthDip * s)), static_cast<LONG>(std::lround(heightDip * s))};
    AdjustWindowRectExForDpi(&wanted, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, wanted.right - wanted.left, wanted.bottom - wanted.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
int DesktopHost::run() {
    MSG msg{};
    BOOL result = 0;
    while (!quit_ && (result = GetMessageW(&msg, nullptr, 0, 0)) > 0) dispatch(msg);
    return result < 0 || failed_ ? 1 : quit_ ? 0 : static_cast<int>(msg.wParam);
}
void DesktopHost::dispatch(MSG& message) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
    followFocus();
}
void DesktopHost::scheduleTooltip() {
    if (!window_) return;
    KillTimer(window_, tooltipTimer);
    if (rendererFailure_) return;
    if (auto deadline = detail::access(ui_).tooltipDeadline()) {
        auto now = static_cast<std::uint64_t>(GetTickCount64());
        auto wait = *deadline > now ? *deadline - now : 1;
        SetTimer(window_, tooltipTimer, static_cast<UINT>(std::min<std::uint64_t>(wait, USER_TIMER_MAXIMUM)), nullptr);
    }
}
void DesktopHost::refreshSystemAnimations() {
    detail::access(ui_).setSystemAnimations(animationReader_ ? animationReader_() : systemClientAnimations());
}
void DesktopHost::refreshVisibility() {
    const bool hidden = hiddenReader_ ? hiddenReader_() : !window_ || IsIconic(window_) || !IsWindowVisible(window_) || cloaked(window_) || coveredByAnotherWindow(window_);
    if (hidden == hidden_) return;
    hidden_ = hidden;
    detail::access(ui_).setWindowHidden(hidden);  // hiding lands every running transition at once
}
void DesktopHost::scheduleAnimation() {
    if (!window_) return;
    if (!rendererFailure_ && !hidden_ && detail::access(ui_).animationRunning()) SetTimer(window_, animationTimer, animationFrameMs, nullptr);
    else KillTimer(window_, animationTimer);
}
// Windows tags mouse messages that touch or pen input synthesizes in the message extra info.
bool DesktopHost::touchSignature(std::uintptr_t extraInfo) { return (extraInfo & 0xFFFFFF00) == 0xFF515700; }
bool DesktopHost::synthesizedByTouch() const { return touchSource_ ? touchSource_() : touchSignature(static_cast<std::uintptr_t>(GetMessageExtraInfo())); }
void DesktopHost::updateHover(int x, int y, bool touch) {
    detail::access(ui_).pointerOver(frame_, x, y, touch);
}
void DesktopHost::followFocus() {
    if (detail::access(ui_).focused() == syncedFocus_) return;
    reveal_ = detail::access(ui_).focused();
    syncIme();
    InvalidateRect(window_, nullptr, FALSE);
}
void DesktopHost::pump() {
    followFocus();
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { failed_ = true; break; }
        dispatch(msg);
    }
    diagnostics_.drain();
}

Frame DesktopHost::arrange(int width, int height, float scale) {
    if (atlasScale_ && atlasScale_ != scale) text_.clearAtlas();
    atlasScale_ = scale;
    return isoduo::arrange(page_(), ui_, appearance_, measure_, width, height, scale, scroll_, reveal_);
}
void DesktopHost::paint() {
    if (rendererFailure_ || painting_) return;
    struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(painting_);
    auto start=std::chrono::steady_clock::now();
    RECT r{};
    GetClientRect(window_, &r);
    if (!renderer_ || r.right <= 0 || r.bottom <= 0) return;
    float s = scale();
    if(perfEnabled_ && perfOffscreen_) { r.right=3840; r.bottom=2160; s=2.f; }
    frame_ = arrange(r.right, r.bottom, s);
    auto afterLayout=std::chrono::steady_clock::now();
    reveal_.clear();
    RecordingCanvas recording(r.right, r.bottom, detail::access(ui_).effectivePalette(appearance_).background);
    paintObservations_.clear();
    recording.countInk = observePaint_;
    draw(recording, frame_, s, GetFocus() == window_);
    auto afterList=std::chrono::steady_clock::now();
    for (const auto& observation : recording.list.observations) {
        paintObservations_.push_back({static_cast<PaintObservation::Type>(observation.type), observation.owner, observation.text,
            observation.bounds, observation.clip, observation.color,
            commandInk(recording.list, observation.begin, observation.end, observation.clip, r.right, r.bottom),
            observation.popup, observation.tooltip, observation.bold, observation.italic});
    }
    if(perfEnabled_ && perfDelay_) Sleep(static_cast<DWORD>(perfDelay_));
    auto beforeGpu=std::chrono::steady_clock::now();
    double submission=0; std::chrono::steady_clock::time_point presented;
    double gpu=0;
    try { gpu=renderer_->draw(recording.list, r.right, r.bottom, perfEnabled_, perfOffscreen_, &submission, &presented); }
    catch (const FrameError& e) {
        for (auto code : e.codes) diagnostics_.push(frameDiagnostic(code));
        PostMessageW(window_,WM_APP+43,0,0);
        failRenderer(e.failure); return;
    }
    catch (const std::exception& e) {
        std::string message=e.what();
        PostMessageW(window_,WM_APP+43,0,0);
        failRenderer({RendererFailureKind::resourceFailed,0,utf16FromUtf8(message)}); return;
    }
    auto end=std::chrono::steady_clock::now();
    if(perfEnabled_) {
        FILETIME created{},exited{},kernel{},user{},now{}; GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user); GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER c{},n{}; c.LowPart=created.dwLowDateTime; c.HighPart=created.dwHighDateTime; n.LowPart=now.dwLowDateTime; n.HighPart=now.dwHighDateTime;
        double age=double(n.QuadPart-c.QuadPart)/10000. - std::chrono::duration<double,std::milli>(end-presented).count();
        if(firstPresentMs_<0) firstPresentMs_=age;
        double cpu=std::chrono::duration<double,std::milli>(beforeGpu-start).count();
        double sampleAge=age-std::chrono::duration<double,std::milli>(presented-start).count();
        perfSamples_.add(sampleAge,cpu+submission+gpu);
        perfBreakdown_.add(sampleAge,std::chrono::duration<double,std::milli>(afterLayout-start).count(),
            std::chrono::duration<double,std::milli>(afterList-afterLayout).count(),submission+gpu);
        if(inputTime_.time_since_epoch().count()) { inputPresentMs_=std::chrono::duration<double,std::milli>(presented-inputTime_).count(); inputTime_={}; }
    }
    ++frames_;
    syncIme();
    scheduleAnimation();
    PostMessageW(window_,WM_APP+43,0,0);
}
Frame DesktopHost::layout(int width, int height, float s, ScrollState& scroll, std::string_view reveal) {
    auto frame=isoduo::arrange(page_(), ui_, appearance_, measure_, width, height, s, scroll, reveal);
    diagnostics_.drain(); return frame;
}
Frame DesktopHost::render(int width, int height, float s, const std::filesystem::path& bmp, Image* copy) {
    auto frame = arrange(width, height, s);
    reveal_.clear();
    RecordingCanvas recording(width, height, detail::access(ui_).effectivePalette(appearance_).background);
    draw(recording, frame, s, true);
    auto image = renderCpu(recording.list, width, height);
    if (copy) *copy = image;
    std::vector<unsigned char> pixels(image.pixels);
    for (std::size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height; info.biPlanes = 1; info.biBitCount = 32;
    info.biSizeImage = static_cast<DWORD>(pixels.size());
    std::ofstream out(bmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!out) throw std::runtime_error("Screenshot write failed");
    diagnostics_.drain(); return frame;
}

GpuComparison DesktopHost::compareGpu(int width, int height, float s, int fault) {
    auto frame = arrange(width, height, s);
    RecordingCanvas recording(width, height, detail::access(ui_).effectivePalette(appearance_).background);
    draw(recording, frame, s, true);
    auto reference = renderCpu(recording.list, width, height);
    std::vector<Rect> regions;
    std::vector<GpuRegion> observed;
    std::string owner; bool popup=false;
    unsigned roles=0;
    Rect clip{0,0,width,height};
    for (std::size_t i=0;i<recording.list.commands.size();++i) {
        const auto& command=recording.list.commands[i];
        if(command.type==DrawCommand::Type::clip){clip=intersect(command.rect,{0,0,width,height});continue;}
        if(command.type==DrawCommand::Type::beginGroup){owner=command.owner;popup=command.popup;continue;}
        if(command.type==DrawCommand::Type::endGroup){owner.clear();popup=false;continue;}
        if(command.role==PaintRole::content)continue;
        auto glyph=command.resolve?command.resolve():ResolvedGlyph{command.rect,command.coverage};
        auto bounds=intersect(glyph.rect,clip);
        if(bounds.w>0 && bounds.h>0){
            bool ink=commandInk(recording.list,i,i+1,clip,width,height);
            regions.push_back(bounds); observed.push_back({bounds,owner,static_cast<unsigned>(command.role),popup,ink,false});
            if(ink)roles |= static_cast<unsigned>(command.role);
        }
    }
    auto gpuList=recording.list;
    for(auto& command:gpuList.commands){
        unsigned role=static_cast<unsigned>(command.role);
        if(fault==1 && (role & static_cast<unsigned>(PaintRole::caret))) ++command.rect.x;
        if(fault==2 && (role & static_cast<unsigned>(PaintRole::focusIndicator))) command.color.a=0;
    }
    auto actual = renderer_->readBack(gpuList, width, height);
    auto result=compareImages(reference,actual,regions);
    unsigned failedRoles=0;
    for(std::size_t i=0;i<observed.size();++i){
        observed[i].passed=observed[i].ink && result.regionPass[i];
        if(!observed[i].passed)failedRoles |= observed[i].role;
    }
    bool passed=!failedRoles && result.regionsOk && result.differentPixels*200<=static_cast<std::size_t>(width)*height;
    return {std::move(reference),std::move(actual),std::move(result),roles,failedRoles,std::move(observed),passed};
}
std::string DesktopHost::rendererName() const { return renderer_->name(); }

bool DesktopHost::compareLegacy(int width, int height, float s) {
    auto frame = arrange(width, height, s);
    Canvas old(width, height, detail::access(ui_).effectivePalette(appearance_).background);
    draw(old, frame, s, true);
    RecordingCanvas recording(width, height, detail::access(ui_).effectivePalette(appearance_).background);
    draw(recording, frame, s, true);
    return old.pixels == renderCpu(recording.list, width, height).pixels;
}

template<class Target> void DesktopHost::draw(Target& canvas, const Frame& frame, float s, bool windowFocused) {
    UiState::OperationScope operation(&access(ui_));
    const auto p = detail::access(ui_).effectivePalette(appearance_);
    const Rect whole{0, 0, canvas.width, canvas.height};
    struct PaintAdapter final : ComponentPaint {
        Target& canvas;
        Text& renderer;
        std::vector<PaintObservation>* observations;
        PaintAdapter(UiState& ui, const Placement& item, const ComponentSnapshot& semantics, const Appearance& appearance,
                     float scale, bool windowFocused, Target& target, Text& text, std::vector<PaintObservation>* events)
            : ComponentPaint(ui, item, semantics, appearance, scale, windowFocused), canvas(target), renderer(text), observations(events) {}
        void role(PaintRole value) override { if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.currentRole=value; }
        void observe(DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin, bool bold = false, bool italic = false) {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                if (observations) canvas.list.observations.push_back({type, placement.id, std::move(text), bounds,
                    canvas.clip, color, placement.popup, placement.tooltip, begin, canvas.list.commands.size(), bold, italic});
            }
        }
        std::size_t cursor() const {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
            return 0;
        }
        void fill(Rect rect, Color color) override {
            auto begin=cursor(); canvas.fill(rect, color);
            observe(DrawObservation::Type::fill, {}, rect, color, begin);
        }
        void rounded(Rect rect, int radius, Color color) override {
            auto begin=cursor(); canvas.rounded(rect, radius, color);
            observe(DrawObservation::Type::rounded, {}, rect, color, begin);
        }
        void overlay(Rect rect, int radius, Color color, unsigned char alpha) override {
            auto begin=cursor();
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.overlay(rect, radius, color, alpha);
            else if (radius > 0) canvas.rounded(rect, radius, color, alpha);
            else canvas.fill(rect, color, alpha);
            observe(radius > 0 ? DrawObservation::Type::rounded : DrawObservation::Type::fill, {}, rect, color, begin);
        }
        void clip(Rect rect) override { canvas.clip = intersect(canvas.clip, rect); }
        void verticalText(std::u16string_view value, VerticalForm form, int pixels, Rect cell, Color color) override {
            auto begin=cursor();
            renderer.drawVertical(canvas, value, form, pixels, cell, color);
            observe(DrawObservation::Type::text, std::u16string(value), cell, color, begin);
        }
        void text(std::u16string_view value, int pixels, int x, int y, Color color) override { styledText(value, pixels, {}, x, y, color); }
        void styledText(std::u16string_view value, int pixels, FontStyle style, int x, int y, Color color) override {
            auto layout = renderer.shape(value, pixels, style);
            auto begin=cursor();
            renderer.draw(canvas, layout, x, y + ShapedMeasure::baseline(pixels), color);
            observe(DrawObservation::Type::text, std::u16string(value),
                {x, y, static_cast<int>(std::ceil(layout.width)), (pixels * 3 + 1) / 2}, color, begin, style.bold, style.italic);
        }
    };
    for (const auto& item : frame.items) {
        canvas.clip = intersect(item.clip, whole);
        auto semantics = detail::access(ui_).describe(item.id);
        PaintAdapter adapter{detail::access(ui_), item, *semantics, appearance_, s, windowFocused, canvas, text_, observePaint_ && canvas.countInk ? &paintObservations_ : nullptr};
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id,item.popup,item.tooltip);
        detail::componentType(item.kind).paint(adapter);
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
    }
    for (const auto& region : frame.regions) {
        canvas.clip = intersect(region.clip, whole);
        if (region.thumb.h) {
            canvas.rounded(region.track, region.track.w / 2, p.panel);
            canvas.rounded(region.thumb, region.thumb.w / 2, p.outline);
        }
    }
    if (frame.modal) {
        canvas.clip = whole;
        if constexpr (std::is_same_v<Target, RecordingCanvas>) {
            canvas.list.setClip(whole); canvas.list.fillRect(whole, {p.scrim.r, p.scrim.g, p.scrim.b, p.scrimAlpha});
        } else {
            for (int y = 0; y < canvas.height; ++y)
                for (int x = 0; x < canvas.width; ++x) canvas.blend(x, y, p.scrim, p.scrimAlpha);
        }
        canvas.rounded(frame.modalPanel, static_cast<int>(appearance_.radius() * s), p.surface);
    }
    for (const auto& item : frame.overlays) {
        canvas.clip = intersect(item.clip, whole);
        if (item.menu) {
            // The open context menu: a bordered panel, one row per item, a scrollbar when it overflows.
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id, true, false);
            auto cursor = [&]() -> std::size_t {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
                return 0;
            };
            auto observeMenu = [&](DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin) {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                    if (observePaint_ && canvas.countInk) canvas.list.observations.push_back({type, item.id, std::move(text), bounds,
                        canvas.clip, color, true, false, begin, canvas.list.commands.size()});
                }
            };
            const int radius = static_cast<int>(appearance_.radius() * s);
            const int border = std::max(1, static_cast<int>(std::lround(appearance_.componentPresentation(ui_.presentation().density).thin * s)));
            auto begin = cursor();
            canvas.rounded(item.bounds, radius, p.border);
            observeMenu(DrawObservation::Type::rounded, {}, item.bounds, p.border, begin);
            Rect panel{item.bounds.x + border, item.bounds.y + border, std::max(0, item.bounds.w - 2 * border), std::max(0, item.bounds.h - 2 * border)};
            begin = cursor();
            canvas.rounded(panel, radius, p.surface);
            observeMenu(DrawObservation::Type::rounded, {}, panel, p.surface, begin);
            canvas.clip = intersect(canvas.clip, item.menuInner);
            const int line = measure_.lineHeight(item.textSize);
            for (const auto& row : item.menuRows) {
                if (row.bounds.y + row.bounds.h <= item.menuInner.y || row.bounds.y >= item.menuInner.y + item.menuInner.h) continue;
                if (row.separator) {
                    Rect rule{row.bounds.x + item.menuPad, row.bounds.y + row.bounds.h / 2, std::max(0, row.bounds.w - 2 * item.menuPad), std::max(1, static_cast<int>(std::lround(s)))};
                    begin = cursor(); canvas.fill(rule, p.border); observeMenu(DrawObservation::Type::fill, {}, rule, p.border, begin);
                    continue;
                }
                if (row.highlighted) {
                    const Color fill = row.enabled ? p.selection : p.panel;
                    begin = cursor(); canvas.fill(row.bounds, fill); observeMenu(DrawObservation::Type::fill, {}, row.bounds, fill, begin);
                }
                const Color color = row.enabled ? p.text : p.muted;
                const int top = row.bounds.y + (row.bounds.h - line) / 2;
                auto drawText = [&](const std::u16string& value, int x, Color paintColor) {
                    auto layout = text_.shape(value, item.textSize);
                    begin = cursor();
                    text_.draw(canvas, layout, x, top + ShapedMeasure::baseline(item.textSize), paintColor);
                    observeMenu(DrawObservation::Type::text, value, {x, top, static_cast<int>(std::ceil(layout.width)), line}, paintColor, begin);
                    return static_cast<int>(std::ceil(layout.width));
                };
                drawText(row.label, row.bounds.x + item.menuPad, color);
                if (!row.hint.empty()) {
                    const int width = static_cast<int>(std::ceil(text_.shape(row.hint, item.textSize).width));
                    drawText(row.hint, row.bounds.x + row.bounds.w - item.menuPad - width, p.muted);
                }
            }
            if (item.menuThumb.h) {
                canvas.rounded(item.menuTrack, item.menuTrack.w / 2, p.panel);
                canvas.rounded(item.menuThumb, item.menuThumb.w / 2, p.outline);
            }
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
            continue;
        }
        if (item.tooltip) {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id,true,true);
            auto observeTooltip = [&](DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin) {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                    if (observePaint_ && canvas.countInk) canvas.list.observations.push_back({type, item.id, std::move(text), bounds,
                        canvas.clip, color, true, true, begin, canvas.list.commands.size()});
                }
            };
            auto cursor = [&]() -> std::size_t {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
                return 0;
            };
            auto begin = cursor();
            canvas.rounded(item.bounds, static_cast<int>(appearance_.radius() * s), p.border);
            observeTooltip(DrawObservation::Type::rounded, {}, item.bounds, p.border, begin);
            int inset = std::max(1, static_cast<int>(std::lround(1 * s)));
            Rect inner{item.bounds.x + inset, item.bounds.y + inset,
                       std::max(0, item.bounds.w - 2 * inset), std::max(0, item.bounds.h - 2 * inset)};
            begin = cursor();
            canvas.rounded(inner, static_cast<int>(appearance_.radius() * s), p.surface);
            observeTooltip(DrawObservation::Type::rounded, {}, inner, p.surface, begin);
            canvas.clip = intersect(canvas.clip, inner);
            for (const auto& line : item.lines) {
                begin = cursor();
                text_.draw(canvas, text_.shape(line.text, item.textSize), line.bounds.x,
                           line.bounds.y + ShapedMeasure::baseline(item.textSize), p.text);
                observeTooltip(DrawObservation::Type::text, line.text, line.bounds, p.text, begin);
            }
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
            continue;
        }
        auto semantics = detail::access(ui_).describe(item.id);
        if (!semantics || (!frame.modal && !semantics->expanded)) continue;
        PaintAdapter adapter{detail::access(ui_), item, *semantics, appearance_, s, windowFocused, canvas, text_, observePaint_ && canvas.countInk ? &paintObservations_ : nullptr};
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.beginGroup(item.id,item.popup,item.tooltip);
        detail::componentType(item.kind).paint(adapter);
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.endGroup();
        if (item.dropdownPanel) for (const auto& region : frame.regions) {
            if (region.state != &detail::access(ui_).dropdownScroll(item.id) || !region.thumb.h) continue;
            canvas.rounded(region.track, region.track.w / 2, p.panel);
            canvas.rounded(region.thumb, region.thumb.w / 2, p.outline);
        }
    }
    canvas.clip = whole;
}

void DesktopHost::logIme(const wchar_t* event, std::u16string_view detail) {
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    std::wstring line = event;
    line += L"\t" + wide(detail) + L"\tfocus=" + std::wstring(detail::access(ui_).focused().begin(), detail::access(ui_).focused().end());
    if (session) line += L"\ttext=" + wide(session->text) + L"\tcomposing=" + (session->composing ? L"1" : L"0") +
                         L"\tpreedit=" + wide(session->preedit);
    if (tracing_) imeLog_.push_back(std::move(line));  // bounded by the verification run
}
void DesktopHost::finishComposition() {
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (!session || !session->composing) return;
    logIme(L"host:complete-request");
    if (auto imc = ImmGetContext(window_)) {
        ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        session = detail::access(ui_).edit(detail::access(ui_).focused());
        if (session && session->composing) {
            // The IME did not deliver a result synchronously: discard on both sides.
            ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            detail::access(ui_).compositionCancel();
            logIme(L"host:complete-ignored-canceled");
        }
        ImmReleaseContext(window_, imc);
    }
}
void DesktopHost::syncIme() {
    const auto& id = detail::access(ui_).focused();
    syncedFocus_ = id;
    auto semantics = id.empty() ? std::nullopt : detail::access(ui_).describe(id);
    bool writable = semantics && detail::access(ui_).edit(id) && semantics->enabled && !semantics->readOnly;
    const auto* focusedSession = detail::access(ui_).edit(id);
    if (inputContext_ && ImmGetCompositionStringW(inputContext_, GCS_COMPSTR, nullptr, 0) > 0 &&
        !(focusedSession && focusedSession->composing)) {
        // Core dropped or never took this composition (focus moved, owner reset): keep the IME in step.
        ImmNotifyIME(inputContext_, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        logIme(L"host:ime-cancel-stale");
    }
    if (writable != imeAssociated_) {
        ImmAssociateContext(window_, writable ? inputContext_ : nullptr);
        imeAssociated_ = writable;
    }
    const auto* field = writable ? detail::findPlacement(frame_, id) : nullptr;
    if (!field || GetFocus() != window_) return;
    const auto* session = detail::access(ui_).edit(id);
    const auto anchor = field->caretRect(session->visibleCaret());  // a wrapped line's own row in a multi-line field
    int x = anchor.x, y = anchor.y, h = anchor.h;
    if (auto imc = ImmGetContext(window_)) {
        CANDIDATEFORM candidate{};
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = {x, y + h};
        candidate.rcArea = {x, y, x + 2, y + h};
        COMPOSITIONFORM form{};
        form.dwStyle = CFS_POINT;
        form.ptCurrentPos = {x, y};
        ImmSetCandidateWindow(imc, &candidate);
        ImmSetCompositionWindow(imc, &form);
        ImmReleaseContext(window_, imc);
    }
}
// Right button released over a component: focus it, place the caret or select the row, open its menu.
void DesktopHost::openPointerMenu(int x, int y) {
    auto before = detail::access(ui_).focused();
    const auto* hit = frame_.hit(x, y);
    if (!hit || !hit->dropdownPanel) detail::access(ui_).closePopup();
    if (!hit || hit->dropdownPanel || hit->tooltip) return;
    finishComposition();
    std::optional<unsigned> offset;
    if (detail::componentType(hit->kind).selectsText()) offset = detail::placementTextOffset(frame_, hit->id, x);
    detail::access(ui_).menuPointerOpen(hit->id, frame_.optionAt(*hit, x, y), offset, {x, y});
    afterInput(before);
    if (detail::componentType(hit->kind).hasRowCursor()) reveal_.clear();  // the pressed row is already visible
}
// Extend the selection of the field being dragged; outside a multi-line field's top or bottom edge it
// scrolls a line, and a timer repeats that while the pointer rests there.
// Scrolling moves a composing caret's line; the input method's window must move with it.
void DesktopHost::followComposition() {
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (!session || !session->composing) return;
    relayout();
    syncIme();
}
void DesktopHost::relayout() {
    RECT r{};
    if (window_ && !painting_ && GetClientRect(window_, &r) && r.right > 0 && r.bottom > 0) frame_ = arrange(r.right, r.bottom, scale());
}
void DesktopHost::dragText(int x, int y) {
    const auto& id = detail::access(ui_).focused();
    auto handle = detail::access(ui_).layoutHandle(id);
    if (auto* field = std::get_if<TextFieldHandle>(&handle)) {
        frame_.selectTo(ui_, *field, x, y);
        const auto* placed = detail::findPlacement(frame_, id);
        const bool outside = placed && placed->multiline && (y < placed->view.text.y || y >= placed->view.text.y + placed->view.text.h);
        if (outside) SetTimer(window_, dragScrollTimer, 60, nullptr);
        else KillTimer(window_, dragScrollTimer);
    }
    syncIme();
    InvalidateRect(window_, nullptr, FALSE);
}
void DesktopHost::afterInput(const Id& focusBefore) {
    if (detail::access(ui_).focused() != focusBefore) reveal_ = detail::access(ui_).focused();
    // The input method anchors at the caret's wrapped line, so settle the lines this input changed first.
    if (const auto* focused = detail::access(ui_).find(detail::access(ui_).focused()); focused && focused->multiline) relayout();
    syncIme();
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT DesktopHost::message(UINT msg, WPARAM w, LPARAM l) {
    const bool closeKey = msg == WM_SYSKEYDOWN && w == VK_F4 && (l & (1LL << 29));
    if (rendererFailure_ && !closeKey && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
        (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_COMPOSITION) ||
        msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_TIMER || msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE)) return 0;
    // Whether motion may play is decided before the input that could start it (a window that was
    // covered and is visible again sends us nothing).
    if (msg == WM_KEYDOWN || msg == WM_LBUTTONDOWN) refreshVisibility();
    if(perfEnabled_ && (msg==WM_KEYDOWN || msg==WM_CHAR)) inputTime_=std::chrono::steady_clock::now()-std::chrono::milliseconds(static_cast<DWORD>(GetTickCount()-GetMessageTime()));
    if (tracing_) {
        const wchar_t* name = msg == WM_KEYDOWN ? L"WM_KEYDOWN" : msg == WM_KEYUP ? L"WM_KEYUP" : msg == WM_CHAR ? L"WM_CHAR"
                            : msg == WM_LBUTTONDOWN ? L"WM_LBUTTONDOWN" : msg == WM_LBUTTONUP ? L"WM_LBUTTONUP" : nullptr;
        if (name) {
            const auto& focus = detail::access(ui_).focused();
            trace_.push_back(std::wstring(name) + L"\t" + std::to_wstring(w) + L"\t" + ((l >> 30) & 1 ? L"repeat" : L"first") +
                             L"\tfocus=" + std::wstring(focus.begin(), focus.end()));
        }
    }
    switch (msg) {
    case WM_GETOBJECT:
        if (uia_ && static_cast<LONG>(l) == UiaRootObjectId) return uia_->getObject(w, l);
        break;
    case uiaDispatchMessage:
        if (uia_) uia_->dispatch();
        return 0;
    case WM_APP+44:
        if (rendererFailure_) {
            if (failureCallback_) { auto failure = *rendererFailure_; failureCallback_(failure); }
            else { quit_ = true; release(); }
        }
        return 0;
    case WM_APP+43:
        diagnostics_.drain(); return 0;
    case WM_APP+45:
        ui_.semantics().dispatchEvents(); return 0;
    case WM_PAINT: {
        ++paintMessages_;
        PAINTSTRUCT ps{};
        BeginPaint(window_, &ps);
        try { paint(); } catch (...) { EndPaint(window_, &ps); throw; }
        EndPaint(window_, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: refreshVisibility(); InvalidateRect(window_, nullptr, FALSE); return 0;
    case WM_SHOWWINDOW: case WM_WINDOWPOSCHANGED: refreshVisibility(); break;
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT*>(l);
        SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize = {static_cast<LONG>(400 * scale()), static_cast<LONG>(300 * scale())};
        return 0;
    }
    case WM_CLOSE: {
        if (closingRequest_) return 0;
        closingRequest_ = true;
        if (!rendererFailure_) { logIme(L"host:close"); finishComposition(); }
        auto decision = closeCallback_ ? closeCallback_() : CloseDecision::close;
        if (decision == CloseDecision::close) quit_ = true;
        closingRequest_ = false;
        return 0;
    }
    case WM_SETFOCUS: {
        detail::access(ui_).windowActive(true);
        POINT point{};
        if (GetFocus() == window_ && GetCursorPos(&point) && ScreenToClient(window_, &point)) updateHover(point.x, point.y);
        syncIme(); InvalidateRect(window_, nullptr, FALSE); return 0;
    }
    case WM_TIMER:
        if (w == animationTimer) {
            KillTimer(window_, animationTimer);
            refreshVisibility();  // covered windows are only noticed by looking
            if (!hidden_ && detail::access(ui_).animationRunning()) InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        if (w == tooltipTimer) {
            KillTimer(window_, tooltipTimer);
            detail::access(ui_).tooltipTime(GetTickCount64());
            scheduleTooltip();
            return 0;
        }
        if (w == dragScrollTimer) {
            POINT point{};
            if (drag_ != Drag::text || !GetCursorPos(&point) || !ScreenToClient(window_, &point)) { KillTimer(window_, dragScrollTimer); return 0; }
            dragText(point.x, point.y);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        logIme(L"wm:killfocus");
        detail::access(ui_).windowActive(false);
        detail::access(ui_).cancelInput();
        drag_ = Drag::none;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) {
            detail::access(ui_).cancelInput();
            // An interrupted slider drag returns its thumb to the value's tick.
            if (drag_ == Drag::slider) { drag_ = Drag::none; InvalidateRect(window_, nullptr, FALSE); }
            detail::access(ui_).hoverRelease();
        }
        break;
    case WM_CANCELMODE:
        detail::access(ui_).cancelInput();
        drag_ = Drag::none;
        detail::access(ui_).hoverRelease();
        if (GetCapture() == window_) ReleaseCapture();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(l) != window_) { detail::access(ui_).cancelInput(); detail::access(ui_).hoverRelease(); drag_ = Drag::none; KillTimer(window_, dragScrollTimer); InvalidateRect(window_, nullptr, FALSE); }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window_, &point);
        int delta = GET_WHEEL_DELTA_WPARAM(w) * static_cast<int>(48 * scale()) / WHEEL_DELTA;
        frame_.wheel(ui_, point.x, point.y, delta, (GET_KEYSTATE_WPARAM(w) & MK_SHIFT) != 0);
        followComposition();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window_, &point);
        int delta = GET_WHEEL_DELTA_WPARAM(w) * static_cast<int>(48 * scale()) / WHEEL_DELTA;
        frame_.hwheel(ui_, point.x, point.y, delta);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_SETTINGCHANGE:  // one case for every system setting we follow (a second label would not compile)
        refreshSystemAnimations();  // "Show animations in Windows"
        // "Always underline access keys and show keyboard focus": takes effect without a restart.
        if (w == SPI_SETKEYBOARDCUES || w == 0) detail::access(ui_).setSystemKeyboardCues(systemKeyboardCues());
        break;
    case WM_RBUTTONDOWN: {
        detail::access(ui_).pointerPressed();  // the right button is a press in the client area too (code review 2026-11-12, item 1)
        SetFocus(window_);
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (detail::access(ui_).menuOpen() && !frame_.menuContains(x, y)) {
            // A press outside only dismisses the menu; the release is swallowed with it.
            auto before = detail::access(ui_).focused();
            detail::access(ui_).closeMenu();
            swallowRight_ = true;
            afterInput(before);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (std::exchange(swallowRight_, false)) return 0;
        auto before = detail::access(ui_).focused();
        if (detail::access(ui_).menuOpen()) {
            if (auto row = frame_.menuItemAt(x, y)) { detail::access(ui_).menuPointerDown(*row); detail::access(ui_).menuPointerUp(*row); }
            afterInput(before);
            return 0;
        }
        openPointerMenu(x, y);
        return 0;
    }
    case WM_CONTEXTMENU: {
        auto before = detail::access(ui_).focused();
        if (detail::access(ui_).menuOpen()) return 0;
        if (l == -1) {
            finishComposition();
            detail::access(ui_).keyDown({Key::menu, false, false, false, 0});
            afterInput(before);
        } else {
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(window_, &point);
            openPointerMenu(point.x, point.y);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        detail::access(ui_).pointerPressed();  // any press in the client area hides the indicator
        SetFocus(window_);
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        const bool touch = synthesizedByTouch();
        if (detail::access(ui_).menuOpen()) {
            auto before = detail::access(ui_).focused();
            if (!frame_.menuContains(x, y)) {
                // Outside the menu the press only closes it: it reaches nothing underneath.
                detail::access(ui_).closeMenu();
                swallowLeft_ = true;
            } else if (auto row = frame_.menuItemAt(x, y)) detail::access(ui_).menuPointerDown(*row);
            afterInput(before);
            return 0;
        }
        auto before = detail::access(ui_).focused();
        pendingHigh_ = 0;
        const auto* hit = frame_.hit(x, y);
        if (!hit || !hit->dropdownPanel) detail::access(ui_).closePopup();
        {
            // An open dialog blocks the page's scrollbars but not its own.
            const bool popupHit = hit && detail::componentType(hit->kind).opensPopup() && hit->popup;
            for (auto it = frame_.regions.rbegin(); it != frame_.regions.rend(); ++it) {
                if (detail::access(ui_).retiredComponent(it->component)) continue;
                if (frame_.modal && !popupHit && !it->modalContent) continue;
                if (!it->thumb.h || !it->clip.contains(x, y) || !it->track.contains(x, y)) continue;
                if (hit && hit->dropdownPanel && it->component != hit->component) continue;
                // Track click centers the thumb on the pointer; dragging then moves its top.
                // A sideways-scrolling label has a horizontal bar: its thumb is dragged by x.
                int along = it->horizontal ? x : y;
                dragOffset_ = it->thumb.contains(x, y) ? (it->horizontal ? it->thumb.x : it->thumb.y)
                                                        : along - (it->horizontal ? it->thumb.w : it->thumb.h) / 2;
                dragScroll_ = it->state;
                dragComponent_ = it->component;
                frame_.scrollTo(ui_, *it, it->offsetForThumb(dragOffset_));
                drag_ = Drag::thumb; dragOrigin_ = along;
                if (!touch) detail::access(ui_).hoverCapture({});  // the thumb is not a component: keep what is hovered
                SetCapture(window_);
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
        }
        if (!hit || !detail::componentType(hit->kind).acceptsPointer()) return 0;
        Id id = hit->id;
        const auto& type = detail::componentType(hit->kind);
        const bool onList = type.hasRowCursor();  // afterInput may arrange again, which replaces the frame hit points into
        finishComposition();
        if (detail::access(ui_).pointerDown(id, frame_.optionAt(*hit, x, y))) {
            if (type.selectsText()) {
                // Core counts the clicks (double click time and box come from the current Windows settings) and
                // picks a caret, a word or a paragraph; a drag then grows the selection by the same unit.
                auto at = detail::placementTextHit(frame_, id, x, y);
                auto cell = detail::placementTextCell(frame_, id, x, y);
                detail::access(ui_).setClickSettings({GetDoubleClickTime(), GetSystemMetrics(SM_CXDOUBLECLK), GetSystemMetrics(SM_CYDOUBLECLK)});
                detail::access(ui_).textPointerDown(id, at, cell, x, y, (w & MK_SHIFT) != 0);
                drag_ = Drag::text;
                if (!touch) detail::access(ui_).hoverCapture(id);
            }
            if (type.beginValueDrag(detail::access(ui_), *hit, x, scale())) drag_ = Drag::slider;
            SetCapture(window_);
        }
        afterInput(before);
        // A pointer already targets a visible row. Revealing the list's entry cursor here
        // would move the row before button-up and cancel or redirect the click.
        if (onList) reveal_.clear();
        return 0;
    }
    case WM_MOUSELEAVE:
        detail::access(ui_).pointerLeft();
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        const bool touch = synthesizedByTouch();
        if (!touch) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
            TrackMouseEvent(&track);
        }
        updateHover(x, y, touch);
        if (detail::access(ui_).menuOpen()) {
            // A menu row's hover is its own highlight (lines are ignored); touch moves do not hover.
            if (!touch) if (auto row = frame_.menuItemAt(x, y)) detail::access(ui_).menuHighlight(*row);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        if (drag_ == Drag::text && (w & MK_LBUTTON)) {
            dragText(x, y);
        } else if (drag_ == Drag::slider && (w & MK_LBUTTON)) {
            detail::access(ui_).sliderPointer(detail::access(ui_).focused(), x);
            InvalidateRect(window_, nullptr, FALSE);
        } else if (drag_ == Drag::thumb && (w & MK_LBUTTON)) {
            auto region = std::find_if(frame_.regions.begin(), frame_.regions.end(),
                [this](const ScrollRegion& item) { return item.state == dragScroll_ && item.component == dragComponent_; });
            if (region != frame_.regions.end()) frame_.scrollTo(ui_, *region, region->offsetForThumb(dragOffset_ + ((region->horizontal ? x : y) - dragOrigin_)));
            followComposition();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        if (std::exchange(swallowLeft_, false)) return 0;
        if (detail::access(ui_).menuOpen()) {
            // Releasing over the row that was pressed runs it; lines and disabled items do nothing.
            auto row = frame_.menuItemAt(x, y);
            detail::access(ui_).menuPointerUp(row ? *row : std::string{});
            afterInput(before);
            return 0;
        }
        if (drag_ == Drag::slider) {
            const auto id = detail::access(ui_).focused();
            detail::access(ui_).sliderPointer(id, x);
            detail::access(ui_).pointerUp(id);
        } else if (drag_ == Drag::none) {
            const auto* hit = frame_.hit(x, y);
            // Settle the press before releasing capture; capture loss would cancel it.
            detail::access(ui_).pointerUp(hit ? std::string_view(hit->id) : std::string_view{}, hit ? frame_.optionAt(*hit, x, y) : std::nullopt);
        }
        if (drag_ == Drag::text) detail::access(ui_).endTextDrag();
        drag_ = Drag::none; dragScroll_ = nullptr; dragComponent_ = {};
        KillTimer(window_, dragScrollTimer);
        if (GetCapture() == window_) ReleaseCapture();
        detail::access(ui_).hoverRelease();
        updateHover(x, y, synthesizedByTouch());  // the pointer may have left the dragged component
        afterInput(before);
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (w == VK_F4 && (l & (1LL << 29))) { SendMessageW(window_, WM_CLOSE, 0, 0); return 0; }
        if (w == VK_F10 || w == VK_MENU) {
            // Alt and F10 close an open menu; Shift+F10 opens one. Their release must not reach the
            // system menu, which would take focus away from the window.
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (detail::access(ui_).menuOpen() || (w == VK_F10 && shift)) {
                auto before = detail::access(ui_).focused();
                finishComposition();
                detail::access(ui_).keyDown({mapKey(w), shift, false, (l & (1LL << 30)) != 0, 0});
                afterInput(before);
                menuSysSwallow_ = true;
                return 0;
            }
        }
        break;
    case WM_SYSKEYUP:
        if ((w == VK_F10 || w == VK_MENU) && std::exchange(menuSysSwallow_, false)) return 0;
        break;
    case WM_KEYDOWN: {
        if (w == VK_PROCESSKEY || w == VK_PACKET) break;  // owned by the IME / injected text
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        KeyPress key{mapKey(w), shift, ctrl, (l & (1LL << 30)) != 0, 0};
        if (ctrl && w >= 'A' && w <= 'Z') key.letter = static_cast<char16_t>(w);
        if (key.key == Key::other && !key.letter) break;
        auto before = detail::access(ui_).focused();
        if (key.key == Key::tab) finishComposition();
        // Space runs a menu item; the character it also produces must not then type into the field.
        if (key.key == Key::space && detail::access(ui_).menuOpen()) dropSpaceChar_ = true;
        detail::access(ui_).keyDown(key);
        if (const auto* focused = detail::access(ui_).find(detail::access(ui_).focused()); focused && detail::componentType(focused->kind).hasRowCursor() &&
            (key.key == Key::up || key.key == Key::down || key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end))
            reveal_ = focused->id;
        afterInput(before);
        return 0;
    }
    case WM_KEYUP: {
        auto before = detail::access(ui_).focused();
        dropSpaceChar_ = false;
        detail::access(ui_).keyUp(mapKey(w));
        afterInput(before);
        return 0;
    }
    case WM_CHAR: {
        auto ch = static_cast<char16_t>(w);
        if (ch == u' ' && std::exchange(dropSpaceChar_, false)) return 0;
        if (ch >= 0xd800 && ch <= 0xdbff) { pendingHigh_ = ch; return 0; }
        std::u16string text;
        if (ch >= 0xdc00 && ch <= 0xdfff) {
            if (!pendingHigh_) return 0;
            text = {pendingHigh_, ch};
        } else text = std::u16string(1, ch);
        pendingHigh_ = 0;
        auto before = detail::access(ui_).focused();
        detail::access(ui_).textInput(text);  // a focused button simply refuses committed characters
        afterInput(before);
        return 0;
    }
    case WM_IME_SETCONTEXT: return DefWindowProcW(window_, msg, w, l & ~ISC_SHOWUICOMPOSITIONWINDOW);
    case WM_IME_STARTCOMPOSITION: logIme(L"wm:ime-start"); return 0;
    case WM_IME_COMPOSITION: {
        auto imc = ImmGetContext(window_);
        if (!imc) return 0;
        if (l & GCS_RESULTSTR) {
            auto result = composition(imc, GCS_RESULTSTR);
            detail::access(ui_).compositionCommit(result);
            logIme(L"wm:ime-result", result);
        }
        if (l & GCS_COMPSTR) {
            auto preedit = composition(imc, GCS_COMPSTR);
            auto caret = ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0);
            detail::access(ui_).compositionUpdate(preedit, static_cast<unsigned>(std::max(0L, caret)));
            logIme(L"wm:ime-compose", preedit);
        }
        ImmReleaseContext(window_, imc);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_IME_ENDCOMPOSITION:
        detail::access(ui_).compositionCancel();  // no-op after a result; discards a canceled preedit
        logIme(L"wm:ime-end");
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_IME_CHAR: return 0;  // results arrive through GCS_RESULTSTR; avoid double insertion
    case WM_IME_REQUEST:
        if (w == IMR_QUERYCHARPOSITION && l) {
            auto info = reinterpret_cast<IMECHARPOSITION*>(l);
            const auto* field = detail::findPlacement(frame_, detail::access(ui_).focused());
            const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
            if (!field || !session || info->dwSize < sizeof(IMECHARPOSITION)) return 0;
            auto at = session->composing ? session->begin() + std::min<unsigned>(info->dwCharPos, static_cast<unsigned>(session->preedit.size()))
                                         : session->selection.caret;
            const auto anchor = field->caretRect(at);
            info->pt = {anchor.x, anchor.y};
            ClientToScreen(window_, &info->pt);
            info->cLineHeight = static_cast<UINT>(anchor.h);
            GetWindowRect(window_, &info->rcDocument);
            return 1;
        }
        break;
    }
    return DefWindowProcW(window_, msg, w, l);
}
}

namespace isoduo {
struct Desktop::Impl {
    std::unique_ptr<detail::DesktopHost> host;
    DesktopConfig options;
    std::thread::id owner = std::this_thread::get_id();
};
Desktop::Desktop(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
Desktop::~Desktop() = default;
CreateResult Desktop::create(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopConfig options) {
    return detail::DesktopHost::createForTest(ui,appearance,std::move(page),scroll,std::move(options),nullptr);
}
CreateResult detail::DesktopHost::createForTest(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopConfig options, detail::RendererTestHooks* hooks) {
    try {
        auto impl=std::make_unique<Desktop::Impl>(); impl->options=std::move(options);
        impl->host=std::make_unique<detail::DesktopHost>(ui,appearance,std::move(page),scroll,impl->options,false,false,hooks);
        auto desktop=std::unique_ptr<Desktop>(new Desktop(std::move(impl)));
        if (desktop->impl_->options.onCloseRequested) {
            auto ptr = desktop.get();
            ptr->impl_->host->onCloseRequest([ptr] { return ptr->impl_->options.onCloseRequested(*ptr); });
        }
        if (desktop->impl_->options.onRendererFailed) {
            auto ptr=desktop.get();
            ptr->impl_->host->onRendererFailure([ptr](const RendererFailure& failure) { ptr->impl_->options.onRendererFailed(*ptr,failure); });
        }
        return {Status::ok,{},std::move(desktop)};
    } catch (const detail::StartupError& e) { return {Status::failed,e.failure,{}}; }
      catch (const std::exception& e) { std::string message=e.what(); return {Status::failed,StartupFailure{StartupFailureKind::resource,{}, {},detail::utf16FromUtf8(message)},{}}; }
}
RunResult Desktop::run() {
    auto& host=*impl_->host;
    ShowWindow(host.window(),SW_SHOW); SetFocus(host.window()); UpdateWindow(host.window());
    int result=host.run();
    auto failure=host.rendererFailure();
    host.close();
    if (failure) return {Status::rendererFailed,failure,{}};
    return {result ? Status::failed : Status::ok,{}, {}};
}
Result Desktop::setTitle(std::u16string title) {
    if (std::this_thread::get_id() != impl_->owner) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (!validSingleLine(title, title.size())) return {Status::invalidValue, u"Invalid title"};
    if (!SetWindowTextW(impl_->host->window(), detail::wide(title).c_str())) return {Status::failed, u"Unable to set window title"};
    return {};
}
Result Desktop::close() {
    if (std::this_thread::get_id() != impl_->owner) return {Status::wrongThread, u"Operation requires the UI thread"};
    impl_->host->requestQuit(); return {};
}
void* Desktop::nativeWindow() const { return impl_->host->window(); }
Result Desktop::rebuildRenderer() { return impl_->host->rebuildRenderer(); }
bool Desktop::rendererFailed() const { return impl_->host->rendererFailed(); }
RendererCapabilities Desktop::capabilities() const { return impl_->host->capabilities(); }
RunResult runDesktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopConfig config) {
    auto creation=Desktop::create(ui,appearance,std::move(page),scroll,std::move(config));
    if (!creation.desktop) return {creation.status,{},creation.failure};
    return creation.desktop->run();
}
}
