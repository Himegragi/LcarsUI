#include "core_internal.hpp"
#include "desktop_host.hpp"
#include <windowsx.h>
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace lcars::detail {
namespace {
const wchar_t* window_class = L"LcarsUI.DesktopHost";

Key map_key(WPARAM vk) {
    switch (vk) {
    case VK_TAB: return Key::tab;
    case VK_RETURN: return Key::enter;
    case VK_SPACE: return Key::space;
    case VK_ESCAPE: return Key::escape;
    case VK_LEFT: return Key::left;
    case VK_RIGHT: return Key::right;
    case VK_HOME: return Key::home;
    case VK_END: return Key::end;
    case VK_BACK: return Key::backspace;
    case VK_DELETE: return Key::del;
    default: return Key::other;
    }
}
std::u16string composition(HIMC imc, DWORD kind) {
    LONG bytes = ImmGetCompositionStringW(imc, kind, nullptr, 0);
    if (bytes <= 0 || bytes > 8192 || bytes % 2) return {};
    std::u16string value(static_cast<std::size_t>(bytes) / 2, u'\0');
    return ImmGetCompositionStringW(imc, kind, value.data(), bytes) == bytes ? value : std::u16string{};
}
Rect offset(Rect r, Rect origin) { return {r.x + origin.x, r.y + origin.y, r.w, r.h}; }
std::wstring wide(std::u16string_view text) { return std::wstring(text.begin(), text.end()); }

LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    auto host = reinterpret_cast<DesktopHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        host = static_cast<DesktopHost*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        host->attach(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (!host) return DefWindowProcW(window, msg, w, l);
    // Extension or backend failure: stop this UI instead of letting C++ unwind into Win32.
    auto stop = [&](const char* what) {
        std::cerr << "LcarsUI host: " << what << '\n';
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        PostQuitMessage(1);
        return LRESULT{0};
    };
    try { return host->message(msg, w, l); }
    catch (const std::exception& e) { return stop(e.what()); }
    catch (...) { return stop("unknown exception"); }
}
}

bool WindowsClipboard::write(std::u16string_view text) {
    if (!OpenClipboard(window_)) return false;
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
    bool ok = false;
    if (memory) {
        if (auto data = static_cast<char16_t*>(GlobalLock(memory))) {
            std::memcpy(data, text.data(), text.size() * sizeof(char16_t));
            data[text.size()] = 0;
            GlobalUnlock(memory);
            ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!ok) GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}
std::optional<std::u16string> WindowsClipboard::read() {
    if (!OpenClipboard(window_)) return std::nullopt;
    std::optional<std::u16string> result;
    if (auto memory = GetClipboardData(CF_UNICODETEXT)) {
        if (auto data = static_cast<const char16_t*>(GlobalLock(memory))) {
            auto units = GlobalSize(memory) / sizeof(char16_t);
            std::size_t n = 0;
            while (n < units && data[n]) ++n;
            if (n < units) result = std::u16string(data, n);
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    return result;
}

DesktopHost::DesktopHost(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                         const DesktopOptions& options, bool trace, bool background)
    : ui_(ui), appearance_(appearance), page_(std::move(page)), scroll_(scroll) {
    tracing_ = trace;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = window_class;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Window registration failed");
    auto title = wide(options.title);
    HWND window = CreateWindowExW(background ? WS_EX_NOACTIVATE : 0, window_class, title.c_str(), WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, this);
    if (!window) {
        window_ = nullptr;  // WM_NCCREATE may have attached a window that creation then destroyed
        throw std::runtime_error("Window creation failed");
    }
    try {
        input_context_ = ImmGetContext(window_);
        if (input_context_) ImmReleaseContext(window_, input_context_);
        clipboard_ = std::make_unique<WindowsClipboard>(window_);
        detail::access(ui_).set_clipboard(clipboard_.get());
        detail::access(ui_).on_invalidate([this] { if (window_) InvalidateRect(window_, nullptr, FALSE); });
        renderer_ = std::make_unique<Renderer>(window_);
        client_size(options.width, options.height);
    } catch (...) {
        release();  // the destructor will not run for a half-built host
        throw;
    }
}
DesktopHost::~DesktopHost() { release(); }
void DesktopHost::release() {
    detail::access(ui_).on_invalidate({});
    detail::access(ui_).set_clipboard(nullptr);
    if (!window_) return;
    ImmAssociateContext(window_, input_context_);
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    renderer_.reset();
    DestroyWindow(window_);
    window_ = nullptr;
}
float DesktopHost::scale() const { return forced_scale_ > 0 ? forced_scale_ : GetDpiForWindow(window_) / 96.f; }
void DesktopHost::client_size(int width_dip, int height_dip) {
    float s = scale();
    RECT wanted{0, 0, static_cast<LONG>(std::lround(width_dip * s)), static_cast<LONG>(std::lround(height_dip * s))};
    AdjustWindowRectExForDpi(&wanted, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, wanted.right - wanted.left, wanted.bottom - wanted.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
int DesktopHost::run() {
    MSG msg{};
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) dispatch(msg);
    return result < 0 || failed_ ? 1 : static_cast<int>(msg.wParam);
}
void DesktopHost::dispatch(MSG& message) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
    follow_focus();
}
void DesktopHost::follow_focus() {
    if (detail::access(ui_).focused() == synced_focus_) return;
    reveal_ = detail::access(ui_).focused();
    sync_ime();
    InvalidateRect(window_, nullptr, FALSE);
}
void DesktopHost::pump() {
    follow_focus();
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { failed_ = true; break; }
        dispatch(msg);
    }
}

Frame DesktopHost::arrange(int width, int height, float scale) {
    return lcars::arrange(page_(), ui_, appearance_, measure_, width, height, scale, scroll_, reveal_);
}
void DesktopHost::paint() {
    RECT r{};
    GetClientRect(window_, &r);
    if (!renderer_ || r.right <= 0 || r.bottom <= 0) return;
    float s = scale();
    frame_ = arrange(r.right, r.bottom, s);
    reveal_.clear();
    Canvas canvas(r.right, r.bottom, appearance_.palette().background);
    draw(canvas, frame_, s, GetFocus() == window_);
    renderer_->draw(canvas);
    ++frames_;
    sync_ime();
}
Frame DesktopHost::layout(int width, int height, float s, ScrollState& scroll, std::string_view reveal) {
    return lcars::arrange(page_(), ui_, appearance_, measure_, width, height, s, scroll, reveal);
}
Frame DesktopHost::render(int width, int height, float s, const std::filesystem::path& bmp, Canvas* copy) {
    auto frame = arrange(width, height, s);
    reveal_.clear();
    Canvas canvas(width, height, appearance_.palette().background);
    draw(canvas, frame, s, true);
    if (copy) *copy = canvas;
    std::vector<unsigned char> pixels(canvas.pixels);
    for (std::size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = height; info.biPlanes = 1; info.biBitCount = 32;
    info.biSizeImage = static_cast<DWORD>(pixels.size());
    std::ofstream out(bmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!out) throw std::runtime_error("Screenshot write failed");
    return frame;
}

void DesktopHost::draw(Canvas& canvas, const Frame& frame, float s, bool window_focused) {
    const auto p = appearance_.palette();
    const Rect whole{0, 0, canvas.width, canvas.height};
    auto d = [&](int v) { return std::max(1, static_cast<int>(std::lround(v * s))); };
    auto lines = [&](const std::vector<Line>& block, int pixels, Color color) {
        for (const auto& line : block)
            text_.draw(canvas, text_.shape(line.text, pixels), line.bounds.x, line.bounds.y + ShapedMeasure::baseline(pixels), color);
    };
    for (const auto& item : frame.items) {
        canvas.clip = intersect(item.clip, whole);
        auto semantics = detail::access(ui_).describe(item.id);
        bool focused = semantics->focused, enabled = semantics->enabled;
        if (item.kind == Kind::label) {
            lines(item.lines, item.text_size, p.text);
        } else if (item.kind != Kind::text) {  // button, toggle, navigation
            int ring = focused ? d(3) : d(1);
            canvas.rounded(item.bounds, d(appearance_.radius()), focused ? p.accent : enabled ? p.border : p.background);
            Rect inner{item.bounds.x + ring, item.bounds.y + ring, item.bounds.w - 2 * ring, item.bounds.h - 2 * ring};
            canvas.rounded(inner, std::max(0, d(appearance_.radius()) - ring), enabled ? p.surface : p.background);
            lines(item.lines, item.text_size, enabled ? p.text : p.muted);
        } else {
            const auto& f = item.field;
            auto surface = offset(f.surface, item.bounds), editor = offset(f.editor, item.bounds);
            int ring = focused ? d(2) : d(1);
            Color edge = semantics->invalid ? p.error : focused ? p.accent : p.border;
            canvas.rounded(surface, d(appearance_.radius()), f.underline ? (enabled ? p.surface : p.background) : edge);
            if (!f.underline) {
                Rect inner{surface.x + ring, surface.y + ring, surface.w - 2 * ring, surface.h - 2 * ring};
                canvas.rounded(inner, std::max(0, d(appearance_.radius()) - ring), enabled ? p.surface : p.background);
            } else {
                canvas.fill({surface.x, surface.y + surface.h - 2 * ring, surface.w, 2 * ring}, edge);
            }
            int caption = item.caption_size;
            lines(item.label_lines, caption, semantics->invalid ? p.error : p.muted);
            lines(item.description_lines, caption, p.muted);
            lines(item.error_lines, caption, p.error);
            const auto* session = detail::access(ui_).edit(item.id);
            canvas.clip = intersect(canvas.clip, editor);
            auto x0 = item.caret_x(0);
            auto at = [&](unsigned offset) { return item.caret_x(offset); };
            if (focused) {
                unsigned first = session->begin();
                unsigned last = session->composing ? first + static_cast<unsigned>(session->preedit.size()) : session->end();
                if (last > first) {
                    if (session->composing) canvas.fill({at(first), editor.y + editor.h - d(2), at(last) - at(first), d(2)}, p.accent);
                    else canvas.fill({at(first), editor.y, at(last) - at(first), editor.h}, p.selection);
                }
            }
            text_.draw(canvas, text_.shape(session->visible(), item.text_size), x0, editor.y + ShapedMeasure::baseline(item.text_size),
                       enabled ? p.text : p.muted);
            if (focused && window_focused && enabled) canvas.fill({at(session->visible_caret()), editor.y, d(1) + 1, editor.h}, p.accent);
        }
    }
    canvas.clip = whole;
    if (frame.thumb.h) {
        canvas.rounded(frame.track, frame.track.w / 2, p.panel);
        canvas.rounded(frame.thumb, frame.thumb.w / 2, p.border);
    }
}

void DesktopHost::log_ime(const wchar_t* event, std::u16string_view detail) {
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    std::wstring line = event;
    line += L"\t" + wide(detail) + L"\tfocus=" + std::wstring(detail::access(ui_).focused().begin(), detail::access(ui_).focused().end());
    if (session) line += L"\ttext=" + wide(session->text) + L"\tcomposing=" + (session->composing ? L"1" : L"0") +
                         L"\tpreedit=" + wide(session->preedit);
    if (tracing_) ime_log_.push_back(std::move(line));  // bounded by the verification run
}
void DesktopHost::finish_composition() {
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (!session || !session->composing) return;
    log_ime(L"host:complete-request");
    if (auto imc = ImmGetContext(window_)) {
        ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        session = detail::access(ui_).edit(detail::access(ui_).focused());
        if (session && session->composing) {
            // The IME did not deliver a result synchronously: discard on both sides.
            ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            detail::access(ui_).composition_cancel();
            log_ime(L"host:complete-ignored-canceled");
        }
        ImmReleaseContext(window_, imc);
    }
}
void DesktopHost::sync_ime() {
    const auto& id = detail::access(ui_).focused();
    synced_focus_ = id;
    auto semantics = id.empty() ? std::nullopt : detail::access(ui_).describe(id);
    bool writable = semantics && semantics->kind == Kind::text && semantics->enabled && !semantics->read_only;
    const auto* focused_session = detail::access(ui_).edit(id);
    if (input_context_ && ImmGetCompositionStringW(input_context_, GCS_COMPSTR, nullptr, 0) > 0 &&
        !(focused_session && focused_session->composing)) {
        // Core dropped or never took this composition (focus moved, owner reset): keep the IME in step.
        ImmNotifyIME(input_context_, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        log_ime(L"host:ime-cancel-stale");
    }
    if (writable != ime_associated_) {
        ImmAssociateContext(window_, writable ? input_context_ : nullptr);
        ime_associated_ = writable;
    }
    const auto* field = writable ? frame_.find(id) : nullptr;
    if (!field || GetFocus() != window_) return;
    const auto* session = detail::access(ui_).edit(id);
    int x = field->caret_x(session->visible_caret());
    int y = field->bounds.y + field->field.editor.y, h = field->field.editor.h;
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
void DesktopHost::after_input(const Id& focus_before) {
    if (detail::access(ui_).focused() != focus_before) reveal_ = detail::access(ui_).focused();
    sync_ime();
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT DesktopHost::message(UINT msg, WPARAM w, LPARAM l) {
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
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(window_, &ps);
        try { paint(); } catch (...) { EndPaint(window_, &ps); throw; }
        EndPaint(window_, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(window_, nullptr, FALSE); return 0;
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
    case WM_CLOSE: PostQuitMessage(0); return 0;
    case WM_SETFOCUS: sync_ime(); InvalidateRect(window_, nullptr, FALSE); return 0;
    case WM_KILLFOCUS:
        log_ime(L"wm:killfocus");
        detail::access(ui_).cancel_input();
        drag_ = Drag::none;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) detail::access(ui_).cancel_input();
        break;
    case WM_CANCELMODE:
        detail::access(ui_).cancel_input();
        drag_ = Drag::none;
        if (GetCapture() == window_) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(l) != window_) { detail::access(ui_).cancel_input(); drag_ = Drag::none; }
        return 0;
    case WM_MOUSEWHEEL: {
        scroll_.offset -= GET_WHEEL_DELTA_WPARAM(w) * static_cast<int>(48 * scale()) / WHEEL_DELTA;
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(window_);
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        pending_high_ = 0;
        if (frame_.thumb.h && frame_.track.contains(x, y)) {
            // Track click centers the thumb on the pointer; dragging then moves the thumb top.
            drag_offset_ = frame_.thumb.contains(x, y) ? frame_.thumb.y : y - frame_.thumb.h / 2;
            scroll_.offset = frame_.offset_for_thumb(drag_offset_);
            drag_ = Drag::thumb;
            drag_origin_ = y;
            SetCapture(window_);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        const auto* hit = frame_.hit(x, y);
        if (!hit || hit->kind == Kind::label) return 0;
        Id id = hit->id;
        finish_composition();
        if (detail::access(ui_).pointer_down(id)) {
            if (hit->kind == Kind::text) {
                auto offset = frame_.text_offset(id, x);
                auto anchor = (w & MK_SHIFT) ? detail::access(ui_).edit(id)->selection.anchor : offset;
                detail::access(ui_).select(id, anchor, offset);
                drag_ = Drag::text;
            }
            SetCapture(window_);
        }
        after_input(before);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (drag_ == Drag::text && (w & MK_LBUTTON)) {
            const auto& id = detail::access(ui_).focused();
            if (auto* session = detail::access(ui_).edit(id)) detail::access(ui_).select(id, session->selection.anchor, frame_.text_offset(id, x));
            InvalidateRect(window_, nullptr, FALSE);
        } else if (drag_ == Drag::thumb && (w & MK_LBUTTON)) {
            scroll_.offset = frame_.offset_for_thumb(drag_offset_ + (y - drag_origin_));
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        if (drag_ == Drag::none) {
            const auto* hit = frame_.hit(x, y);
            // Settle the press before releasing capture; capture loss would cancel it.
            detail::access(ui_).pointer_up(hit ? std::string_view(hit->id) : std::string_view{});
        }
        drag_ = Drag::none;
        if (GetCapture() == window_) ReleaseCapture();
        after_input(before);
        return 0;
    }
    case WM_KEYDOWN: {
        if (w == VK_PROCESSKEY || w == VK_PACKET) break;  // owned by the IME / injected text
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        KeyPress key{map_key(w), shift, ctrl, (l & (1LL << 30)) != 0, 0};
        if (ctrl && w >= 'A' && w <= 'Z') key.letter = static_cast<char16_t>(w);
        if (key.key == Key::other && !key.letter) break;
        auto before = detail::access(ui_).focused();
        if (key.key == Key::tab) finish_composition();
        detail::access(ui_).key_down(key);
        after_input(before);
        return 0;
    }
    case WM_KEYUP: {
        auto before = detail::access(ui_).focused();
        detail::access(ui_).key_up(map_key(w));
        after_input(before);
        return 0;
    }
    case WM_CHAR: {
        auto ch = static_cast<char16_t>(w);
        if (ch >= 0xd800 && ch <= 0xdbff) { pending_high_ = ch; return 0; }
        std::u16string text;
        if (ch >= 0xdc00 && ch <= 0xdfff) {
            if (!pending_high_) return 0;
            text = {pending_high_, ch};
        } else text = std::u16string(1, ch);
        pending_high_ = 0;
        auto before = detail::access(ui_).focused();
        detail::access(ui_).text_input(text);  // a focused button simply refuses committed characters
        after_input(before);
        return 0;
    }
    case WM_IME_SETCONTEXT: return DefWindowProcW(window_, msg, w, l & ~ISC_SHOWUICOMPOSITIONWINDOW);
    case WM_IME_STARTCOMPOSITION: log_ime(L"wm:ime-start"); return 0;
    case WM_IME_COMPOSITION: {
        auto imc = ImmGetContext(window_);
        if (!imc) return 0;
        if (l & GCS_RESULTSTR) {
            auto result = composition(imc, GCS_RESULTSTR);
            detail::access(ui_).composition_commit(result);
            log_ime(L"wm:ime-result", result);
        }
        if (l & GCS_COMPSTR) {
            auto preedit = composition(imc, GCS_COMPSTR);
            auto caret = ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0);
            detail::access(ui_).composition_update(preedit, static_cast<unsigned>(std::max(0L, caret)));
            log_ime(L"wm:ime-compose", preedit);
        }
        ImmReleaseContext(window_, imc);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_IME_ENDCOMPOSITION:
        detail::access(ui_).composition_cancel();  // no-op after a result; discards a canceled preedit
        log_ime(L"wm:ime-end");
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_IME_CHAR: return 0;  // results arrive through GCS_RESULTSTR; avoid double insertion
    case WM_IME_REQUEST:
        if (w == IMR_QUERYCHARPOSITION && l) {
            auto info = reinterpret_cast<IMECHARPOSITION*>(l);
            const auto* field = frame_.find(detail::access(ui_).focused());
            const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
            if (!field || !session || info->dwSize < sizeof(IMECHARPOSITION)) return 0;
            auto at = session->composing ? session->begin() + std::min<unsigned>(info->dwCharPos, static_cast<unsigned>(session->preedit.size()))
                                         : session->selection.caret;
            info->pt = {field->caret_x(at),
                        field->bounds.y + field->field.editor.y};
            ClientToScreen(window_, &info->pt);
            info->cLineHeight = static_cast<UINT>(field->field.editor.h);
            GetWindowRect(window_, &info->rcDocument);
            return 1;
        }
        break;
    }
    return DefWindowProcW(window_, msg, w, l);
}
}

namespace lcars {
int run_desktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopOptions options) {
    detail::DesktopHost host(ui, appearance, std::move(page), scroll, options);
    ShowWindow(host.window(), SW_SHOW);
    SetFocus(host.window());
    UpdateWindow(host.window());
    return host.run();
}
}
