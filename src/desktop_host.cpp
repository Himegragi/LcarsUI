#include "core_internal.hpp"
#include "component_type.hpp"
#include "desktop_host.hpp"
#include <windowsx.h>
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace lcars::detail {
namespace {
const wchar_t* window_class = L"LcarsUI.DesktopHost";
constexpr UINT_PTR tooltip_timer = 1;

Key map_key(WPARAM vk) {
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

namespace {
// Clipboard managers can hold the global clipboard briefly after a change. Bound the wait
// on the UI thread; report failure rather than waiting indefinitely if another owner stalls.
bool open_clipboard(HWND window) {
    constexpr ULONGLONG timeout_ms = 250;
    const auto deadline = GetTickCount64() + timeout_ms;
    do {
        if (OpenClipboard(window)) return true;
        auto now = GetTickCount64();
        if (now >= deadline) break;
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(5, deadline - now)));
    } while (GetTickCount64() < deadline);
    return false;
}
}

bool WindowsClipboard::write(std::u16string_view text) {
    if (!open_clipboard(window_)) return false;
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
Query<std::u16string> WindowsClipboard::read() {
    if (!open_clipboard(window_)) return {Status::failed, u"\u7121\u6cd5\u958b\u555f\u526a\u8cbc\u7c3f", std::nullopt};
    Query<std::u16string> result{Status::not_supported, u"\u526a\u8cbc\u7c3f\u6c92\u6709\u6587\u5b57", std::nullopt};
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
                         const DesktopOptions& options, bool trace, bool background)
    : ui_(ui), appearance_(appearance), page_(std::move(page)), scroll_(scroll), diagnostics_(options.on_diagnostic,&ui),
      text_(system_font_file, [this](const RenderDiagnostic& event) { diagnostics_.push(event); if(window_) PostMessageW(window_,WM_APP+43,0,0); }) {
    configure_font(ui.presentation().font_family);
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
        detail::access(ui_).tooltip_clock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
        detail::access(ui_).on_invalidate([this] { if (window_) { schedule_tooltip(); InvalidateRect(window_, nullptr, FALSE); } });
        atlas_text_size_ = ui_.presentation().text_size;
        atlas_font_family_ = ui_.presentation().font_family;
        detail::access(ui_).on_presentation([this] {
            const auto& settings = ui_.presentation();
            const auto clears = text_.atlas_clears();
            configure_font(settings.font_family);
            if ((settings.text_size != atlas_text_size_ || settings.font_family != atlas_font_family_) && text_.atlas_clears() == clears)
                text_.clear_atlas();
            atlas_text_size_ = settings.text_size;
            atlas_font_family_ = settings.font_family;
            if (!window_) return;
            RECT client{}; GetClientRect(window_, &client);
            if (client.right <= 0 || client.bottom <= 0) return;
            reveal_ = detail::access(ui_).focused();
            frame_ = arrange(client.right, client.bottom, scale());
            reveal_.clear();
            sync_ime();
        });
        detail::access(ui_).before_focus([this] { finish_composition(); });
        renderer_ = std::make_unique<Renderer>(window_);
        client_size(options.width, options.height);
        PostMessageW(window_,WM_APP+43,0,0);
    } catch (...) {
        release();  // the destructor will not run for a half-built host
        throw;
    }
}
void DesktopHost::configure_font(std::u16string_view family) {
    if(font_configured_ && requested_font_==family) return;
    PresentationSettings settings; settings.font_family=family;
    auto resolution=system_font_catalog().resolve(settings);
    enqueue_font_diagnostics(diagnostics_,font_configured_?0:system_font_catalog_error(),resolution);
    requested_font_=family; font_configured_=true;
    text_.set_primary_family(!family.empty() && resolution.requested_available ? resolution.primary : std::u16string{});
    if(window_) PostMessageW(window_,WM_APP+43,0,0);
}
DesktopHost::~DesktopHost() { release(); }
void DesktopHost::release() {
    // A host may be destroyed while its Ui remains alive. Discard an unfinished
    // preedit on both sides before detaching the window; closing must not leave
    // an orphaned composition in the retained text session.
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (window_ && session && session->composing) {
        if (auto imc = ImmGetContext(window_)) {
            ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(window_, imc);
        }
        detail::access(ui_).composition_cancel();
    }
    detail::access(ui_).on_presentation({});
    detail::access(ui_).on_invalidate({});
    detail::access(ui_).tooltip_clock({});
    if (window_) KillTimer(window_, tooltip_timer);
    detail::access(ui_).before_focus({});
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
void DesktopHost::schedule_tooltip() {
    if (!window_) return;
    KillTimer(window_, tooltip_timer);
    if (auto deadline = detail::access(ui_).tooltip_deadline()) {
        auto now = static_cast<std::uint64_t>(GetTickCount64());
        auto wait = *deadline > now ? *deadline - now : 1;
        SetTimer(window_, tooltip_timer, static_cast<UINT>(std::min<std::uint64_t>(wait, USER_TIMER_MAXIMUM)), nullptr);
    }
}
void DesktopHost::update_hover(int x, int y) {
    const auto* hit = frame_.hit(x, y);
    detail::access(ui_).tooltip_hover(hit && !hit->popup ? hit->id : std::string_view{});
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
    diagnostics_.drain();
}

Frame DesktopHost::arrange(int width, int height, float scale) {
    if (atlas_scale_ && atlas_scale_ != scale) text_.clear_atlas();
    atlas_scale_ = scale;
    return lcars::arrange(page_(), ui_, appearance_, measure_, width, height, scale, scroll_, reveal_);
}
void DesktopHost::paint() {
    RECT r{};
    GetClientRect(window_, &r);
    if (!renderer_ || r.right <= 0 || r.bottom <= 0) return;
    float s = scale();
    frame_ = arrange(r.right, r.bottom, s);
    reveal_.clear();
    RecordingCanvas recording(r.right, r.bottom, appearance_.palette(ui_.presentation().variant).background);
    paint_observations_.clear();
    recording.count_ink = observe_paint_;
    draw(recording, frame_, s, GetFocus() == window_);
    for (const auto& observation : recording.list.observations) {
        paint_observations_.push_back({static_cast<PaintObservation::Type>(observation.type), observation.owner, observation.text,
            observation.bounds, observation.clip, observation.color,
            command_ink(recording.list, observation.begin, observation.end, observation.clip, r.right, r.bottom),
            observation.popup, observation.tooltip});
    }
    renderer_->draw(recording.list, r.right, r.bottom);
    ++frames_;
    sync_ime();
    diagnostics_.drain();
}
Frame DesktopHost::layout(int width, int height, float s, ScrollState& scroll, std::string_view reveal) {
    auto frame=lcars::arrange(page_(), ui_, appearance_, measure_, width, height, s, scroll, reveal);
    diagnostics_.drain(); return frame;
}
Frame DesktopHost::render(int width, int height, float s, const std::filesystem::path& bmp, Image* copy) {
    auto frame = arrange(width, height, s);
    reveal_.clear();
    RecordingCanvas recording(width, height, appearance_.palette(ui_.presentation().variant).background);
    draw(recording, frame, s, true);
    auto image = render_cpu(recording.list, width, height);
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

GpuComparison DesktopHost::compare_gpu(int width, int height, float s, int fault) {
    auto frame = arrange(width, height, s);
    RecordingCanvas recording(width, height, appearance_.palette().background);
    draw(recording, frame, s, true);
    auto reference = render_cpu(recording.list, width, height);
    std::vector<Rect> regions;
    std::vector<unsigned> region_roles;
    unsigned roles=0;
    Rect clip{0,0,width,height};
    for (std::size_t i=0;i<recording.list.commands.size();++i) {
        const auto& command=recording.list.commands[i];
        if(command.type==DrawCommand::Type::clip){clip=intersect(command.rect,{0,0,width,height});continue;}
        if(command.role==PaintRole::content)continue;
        auto glyph=command.resolve?command.resolve():ResolvedGlyph{command.rect,command.coverage};
        auto bounds=intersect(glyph.rect,clip);
        if(bounds.w>0 && bounds.h>0 && command_ink(recording.list,i,i+1,clip,width,height)){regions.push_back(bounds); region_roles.push_back(static_cast<unsigned>(command.role)); roles |= static_cast<unsigned>(command.role);}
    }
    auto gpu_list=recording.list;
    for(auto& command:gpu_list.commands){
        unsigned role=static_cast<unsigned>(command.role);
        if(fault==1 && (role & static_cast<unsigned>(PaintRole::caret))) ++command.rect.x;
        if(fault==2 && (role & static_cast<unsigned>(PaintRole::focus_indicator))) command.color.a=0;
    }
    auto actual = renderer_->read_back(gpu_list, width, height);
    auto result=compare_images(reference,actual,regions);
    unsigned failed_roles=0;
    for(std::size_t i=0;i<region_roles.size();++i) if(!result.region_pass[i]) failed_roles |= region_roles[i];
    bool passed=result.regions_ok && result.different_pixels*200<=static_cast<std::size_t>(width)*height;
    return {std::move(reference),std::move(actual),std::move(result),roles,failed_roles,std::move(region_roles),passed};
}
std::string DesktopHost::renderer_name() const { return renderer_->name(); }

bool DesktopHost::compare_legacy(int width, int height, float s) {
    auto frame = arrange(width, height, s);
    Canvas old(width, height, appearance_.palette(ui_.presentation().variant).background);
    draw(old, frame, s, true);
    RecordingCanvas recording(width, height, appearance_.palette(ui_.presentation().variant).background);
    draw(recording, frame, s, true);
    return old.pixels == render_cpu(recording.list, width, height).pixels;
}

template<class Target> void DesktopHost::draw(Target& canvas, const Frame& frame, float s, bool window_focused) {
    UiState::OperationScope operation(&access(ui_));
    const auto p = appearance_.palette(ui_.presentation().variant);
    const Rect whole{0, 0, canvas.width, canvas.height};
    struct PaintAdapter final : ComponentPaint {
        Target& canvas;
        Text& renderer;
        std::vector<PaintObservation>* observations;
        PaintAdapter(UiState& ui, const Placement& item, const Semantics& semantics, const Appearance& appearance,
                     float scale, bool window_focused, Target& target, Text& text, std::vector<PaintObservation>* events)
            : ComponentPaint(ui, item, semantics, appearance, scale, window_focused), canvas(target), renderer(text), observations(events) {}
        void role(PaintRole value) override { if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.current_role=value; }
        void observe(DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin) {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                if (observations) canvas.list.observations.push_back({type, placement.id, std::move(text), bounds,
                    canvas.clip, color, placement.popup, placement.tooltip, begin, canvas.list.commands.size()});
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
        void clip(Rect rect) override { canvas.clip = intersect(canvas.clip, rect); }
        void text(std::u16string_view value, int pixels, int x, int y, Color color) override {
            auto layout = renderer.shape(value, pixels);
            auto begin=cursor();
            renderer.draw(canvas, layout, x, y + ShapedMeasure::baseline(pixels), color);
            observe(DrawObservation::Type::text, std::u16string(value),
                {x, y, static_cast<int>(std::ceil(layout.width)), (pixels * 3 + 1) / 2}, color, begin);
        }
    };
    for (const auto& item : frame.items) {
        canvas.clip = intersect(item.clip, whole);
        auto semantics = detail::access(ui_).describe(item.id);
        PaintAdapter adapter{detail::access(ui_), item, *semantics, appearance_, s, window_focused, canvas, text_, observe_paint_ && canvas.count_ink ? &paint_observations_ : nullptr};
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.begin_group(item.id,item.popup,item.tooltip);
        detail::component_type(item.kind).paint(adapter);
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.end_group();
    }
    for (const auto& region : frame.regions) {
        canvas.clip = intersect(region.clip, whole);
        if (region.thumb.h) {
            canvas.rounded(region.track, region.track.w / 2, p.panel);
            canvas.rounded(region.thumb, region.thumb.w / 2, p.border);
        }
    }
    if (frame.modal) {
        canvas.clip = whole;
        if constexpr (std::is_same_v<Target, RecordingCanvas>) {
            canvas.list.set_clip(whole); canvas.list.fill_rect(whole, {p.scrim.r, p.scrim.g, p.scrim.b, p.scrim_alpha});
        } else {
            for (int y = 0; y < canvas.height; ++y)
                for (int x = 0; x < canvas.width; ++x) canvas.blend(x, y, p.scrim, p.scrim_alpha);
        }
        canvas.rounded(frame.modal_panel, static_cast<int>(appearance_.radius() * s), p.surface);
    }
    for (const auto& item : frame.overlays) {
        canvas.clip = intersect(item.clip, whole);
        if (item.tooltip) {
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.begin_group(item.id,true,true);
            auto observe_tooltip = [&](DrawObservation::Type type, std::u16string text, Rect bounds, Color color, std::size_t begin) {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) {
                    if (observe_paint_ && canvas.count_ink) canvas.list.observations.push_back({type, item.id, std::move(text), bounds,
                        canvas.clip, color, true, true, begin, canvas.list.commands.size()});
                }
            };
            auto cursor = [&]() -> std::size_t {
                if constexpr (std::is_same_v<Target, RecordingCanvas>) return canvas.list.commands.size();
                return 0;
            };
            auto begin = cursor();
            canvas.rounded(item.bounds, static_cast<int>(appearance_.radius() * s), p.border);
            observe_tooltip(DrawObservation::Type::rounded, {}, item.bounds, p.border, begin);
            int inset = std::max(1, static_cast<int>(std::lround(1 * s)));
            Rect inner{item.bounds.x + inset, item.bounds.y + inset,
                       std::max(0, item.bounds.w - 2 * inset), std::max(0, item.bounds.h - 2 * inset)};
            begin = cursor();
            canvas.rounded(inner, static_cast<int>(appearance_.radius() * s), p.surface);
            observe_tooltip(DrawObservation::Type::rounded, {}, inner, p.surface, begin);
            canvas.clip = intersect(canvas.clip, inner);
            for (const auto& line : item.lines) {
                begin = cursor();
                text_.draw(canvas, text_.shape(line.text, item.text_size), line.bounds.x,
                           line.bounds.y + ShapedMeasure::baseline(item.text_size), p.text);
                observe_tooltip(DrawObservation::Type::text, line.text, line.bounds, p.text, begin);
            }
            if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.end_group();
            continue;
        }
        auto semantics = detail::access(ui_).describe(item.id);
        if (!semantics || (!frame.modal && !semantics->expanded)) continue;
        PaintAdapter adapter{detail::access(ui_), item, *semantics, appearance_, s, window_focused, canvas, text_, observe_paint_ && canvas.count_ink ? &paint_observations_ : nullptr};
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.begin_group(item.id,item.popup,item.tooltip);
        detail::component_type(item.kind).paint(adapter);
        if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.end_group();
    }
    canvas.clip = whole;
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
    bool writable = semantics && detail::access(ui_).edit(id) && semantics->enabled && !semantics->read_only;
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
    const auto* field = writable ? detail::find_placement(frame_, id) : nullptr;
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
    case WM_APP+43:
        diagnostics_.drain(); return 0;
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
    case WM_CLOSE:
        // Use the same IME completion policy as a focus transition before the host is torn down.
        log_ime(L"host:close");
        finish_composition();
        PostQuitMessage(0); return 0;
    case WM_SETFOCUS: {
        detail::access(ui_).window_active(true);
        POINT point{};
        if (GetFocus() == window_ && GetCursorPos(&point) && ScreenToClient(window_, &point)) update_hover(point.x, point.y);
        sync_ime(); InvalidateRect(window_, nullptr, FALSE); return 0;
    }
    case WM_TIMER:
        if (w == tooltip_timer) {
            KillTimer(window_, tooltip_timer);
            detail::access(ui_).tooltip_time(GetTickCount64());
            schedule_tooltip();
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        log_ime(L"wm:killfocus");
        detail::access(ui_).window_active(false);
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
        if (frame_.modal) return 0;
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window_, &point);
        int delta = GET_WHEEL_DELTA_WPARAM(w) * static_cast<int>(48 * scale()) / WHEEL_DELTA;
        frame_.wheel(ui_, point.x, point.y, delta);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(window_);
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        pending_high_ = 0;
        const auto* hit = frame_.hit(x, y);
        if (!hit || !hit->popup) detail::access(ui_).close_popup();
        if (!frame_.modal && (!hit || !hit->popup)) {
            for (auto it = frame_.regions.rbegin(); it != frame_.regions.rend(); ++it) {
                if (!it->thumb.h || !it->clip.contains(x, y) || !it->track.contains(x, y)) continue;
                // Track click centers the thumb on the pointer; dragging then moves its top.
                drag_offset_ = it->thumb.contains(x, y) ? it->thumb.y : y - it->thumb.h / 2;
                drag_scroll_ = it->state;
                frame_.scroll_to(ui_, *drag_scroll_, it->offset_for_thumb(drag_offset_));
                drag_ = Drag::thumb; drag_origin_ = y;
                SetCapture(window_);
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
        }
        if (!hit || !detail::component_type(hit->kind).accepts_pointer()) return 0;
        Id id = hit->id;
        finish_composition();
        if (detail::access(ui_).pointer_down(id, frame_.option_at(*hit, x, y))) {
            if (detail::component_type(hit->kind).selects_text()) {
                auto offset = detail::placement_text_offset(frame_, id, x);
                auto anchor = (w & MK_SHIFT) ? detail::access(ui_).edit(id)->selection.anchor : offset;
                detail::access(ui_).select(id, anchor, offset);
                drag_ = Drag::text;
            }
            if (hit->kind == Kind::slider) {
                drag_ = Drag::slider;
                detail::access(ui_).slider_pointer(id, x, hit->slider_track);
            }
            SetCapture(window_);
        }
        after_input(before);
        // A pointer already targets a visible row. Revealing the list's entry cursor here
        // would move the row before button-up and cancel or redirect the click.
        if (hit->kind == Kind::list) reveal_.clear();
        return 0;
    }
    case WM_MOUSELEAVE:
        detail::access(ui_).tooltip_hover({});
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        TrackMouseEvent(&track);
        update_hover(x, y);
        if (drag_ == Drag::text && (w & MK_LBUTTON)) {
            const auto& id = detail::access(ui_).focused();
            if (auto* session = detail::access(ui_).edit(id)) detail::access(ui_).select(id, session->selection.anchor, detail::placement_text_offset(frame_, id, x));
            InvalidateRect(window_, nullptr, FALSE);
        } else if (drag_ == Drag::slider && (w & MK_LBUTTON)) {
            const auto& id = detail::access(ui_).focused();
            if (const auto* item = detail::find_placement(frame_, id)) detail::access(ui_).slider_pointer(id, x, item->slider_track);
            InvalidateRect(window_, nullptr, FALSE);
        } else if (drag_ == Drag::thumb && (w & MK_LBUTTON)) {
            auto region = std::find_if(frame_.regions.begin(), frame_.regions.end(),
                [this](const ScrollRegion& item) { return item.state == drag_scroll_; });
            if (region != frame_.regions.end()) frame_.scroll_to(ui_, *drag_scroll_, region->offset_for_thumb(drag_offset_ + (y - drag_origin_)));
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        if (drag_ == Drag::slider) {
            const auto& id = detail::access(ui_).focused();
            if (const auto* item = detail::find_placement(frame_, id)) detail::access(ui_).slider_pointer(id, x, item->slider_track);
            detail::access(ui_).pointer_up(id);
        } else if (drag_ == Drag::none) {
            const auto* hit = frame_.hit(x, y);
            // Settle the press before releasing capture; capture loss would cancel it.
            detail::access(ui_).pointer_up(hit ? std::string_view(hit->id) : std::string_view{}, hit ? frame_.option_at(*hit, x, y) : std::nullopt);
        }
        drag_ = Drag::none; drag_scroll_ = nullptr;
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
        if (const auto* focused = detail::access(ui_).find(detail::access(ui_).focused()); focused && focused->kind == Kind::list &&
            (key.key == Key::up || key.key == Key::down || key.key == Key::left || key.key == Key::right || key.key == Key::home || key.key == Key::end))
            reveal_ = focused->id;
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
            const auto* field = detail::find_placement(frame_, detail::access(ui_).focused());
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
