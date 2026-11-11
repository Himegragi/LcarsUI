#include "utf.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "desktop_host.hpp"
#include "uia_provider.hpp"
#include <windowsx.h>
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

namespace lcars::detail {
namespace {
bool system_keyboard_cues() {
    BOOL on = FALSE;
    return SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &on, 0) && on;
}
const wchar_t* window_class = L"LcarsUI.DesktopHost";
constexpr UINT_PTR tooltip_timer = 1;
constexpr UINT_PTR drag_scroll_timer = 2;  // keeps a text drag scrolling while the pointer rests outside the field

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
    case VK_PRIOR: return Key::page_up;
    case VK_NEXT: return Key::page_down;
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

namespace {
UINT rich_format() {
    static const UINT format = RegisterClipboardFormatW(L"LcarsUI.RichText.1");
    return format;
}
HGLOBAL global_text(std::u16string_view text) {
    auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
    if (!memory) return nullptr;
    auto data = static_cast<char16_t*>(GlobalLock(memory));
    if (!data) { GlobalFree(memory); return nullptr; }
    std::memcpy(data, text.data(), text.size() * sizeof(char16_t));
    data[text.size()] = 0;
    GlobalUnlock(memory);
    return memory;
}
HGLOBAL global_bytes(std::string_view bytes) {  // NUL-terminated: a global block may be larger than asked for
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
bool WindowsClipboard::write(std::u16string_view text) { return write_rich(text, {}); }
// One clipboard transaction: the plain text and, for a copy from a rich field, the private payload beside it.
// EmptyClipboard drops every older format, so a payload never outlives the text it belonged to.
bool WindowsClipboard::write_rich(std::u16string_view text, std::string_view payload) {
    if (!open_clipboard(window_)) return false;
    auto memory = global_text(text);
    auto rich = payload.empty() ? nullptr : global_bytes(payload);
    bool ok = false;
    if (memory && (payload.empty() || rich)) {
        ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (ok) memory = nullptr;  // the clipboard owns it now
        if (ok && rich) {
            if (SetClipboardData(rich_format(), rich) != nullptr) rich = nullptr;  // plain text still stands if only this fails
        }
    }
    if (memory) GlobalFree(memory);
    if (rich) GlobalFree(rich);
    CloseClipboard();
    return ok;
}
Query<std::string> WindowsClipboard::read_rich() {
    if (!open_clipboard(window_)) return {Status::failed, u"無法開啟剪貼簿", std::nullopt};
    Query<std::string> result{Status::not_supported, u"No rich text", std::nullopt};
    if (IsClipboardFormatAvailable(rich_format())) {
        result = {Status::failed, u"Cannot read rich text", std::nullopt};
        if (auto memory = GetClipboardData(rich_format())) {
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
                         const DesktopOptions& options, bool trace, bool background, RendererTestHooks* hooks)
    : ui_(ui), appearance_(appearance), page_(std::move(page)), scroll_(scroll), diagnostics_(options.on_diagnostic,&ui),
      text_(system_font_file, [this](const RenderDiagnostic& event) { diagnostics_.push(event); if(window_) PostMessageW(window_,WM_APP+43,0,0); }) {
    hooks_ = hooks; debug_context_ = options.debug_context;
    text_.set_styled_lookup(system_styled_font_file);
    configure_font(ui.presentation().font_family);
    tracing_ = trace;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = window_class;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw StartupError{{StartupFailureKind::window, {}, {}, u"Window registration failed"}};
    auto title = wide(options.title);
    HWND window = CreateWindowExW(background ? WS_EX_NOACTIVATE : 0, window_class, title.c_str(), WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, this);
    if (!window) {
        window_ = nullptr;  // WM_NCCREATE may have attached a window that creation then destroyed
        throw StartupError{{StartupFailureKind::window, {}, {}, u"Window creation failed"}};
    }
    try {
        if (hooks_ && !hooks_->startup_resource_error.empty()) throw std::runtime_error(hooks_->startup_resource_error);
        uia_ = std::make_unique<UiaBridge>(ui_, window_, [this]() -> const Frame& {
            RECT client{}; GetClientRect(window_, &client);
            if (client.right > 0 && client.bottom > 0) frame_ = arrange(client.right, client.bottom, scale());
            return frame_;
        });
        input_context_ = ImmGetContext(window_);
        if (input_context_) ImmReleaseContext(window_, input_context_);
        clipboard_ = std::make_unique<WindowsClipboard>(window_);
        detail::access(ui_).set_clipboard(clipboard_.get());
        detail::access(ui_).bind_appearance(&appearance_);
        detail::access(ui_).tooltip_clock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
        detail::access(ui_).set_system_keyboard_cues(system_keyboard_cues());
        detail::access(ui_).on_invalidate([this] { if (window_) { schedule_tooltip(); InvalidateRect(window_, nullptr, FALSE); } });
        // Cursor keys need the current wrapping; a key can arrive before the paint that would settle it.
        detail::access(ui_).on_relayout([this] { relayout(); });
        detail::access(ui_).on_semantic_pending([this] { if (window_) PostMessageW(window_, WM_APP + 45, 0, 0); });
        detail::access(ui_).on_reveal([this](const Id& id) { reveal_ = id; if (window_) InvalidateRect(window_, nullptr, FALSE); });
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
        renderer_ = std::make_unique<Renderer>(window_, hooks_, debug_context_, [this](RenderDiagnostic event) { diagnostics_.push(std::move(event)); });
        capabilities_ = renderer_->capabilities();
        client_size(options.width, options.height);
        PostMessageW(window_,WM_APP+43,0,0);
    } catch (...) {
        release();  // the destructor will not run for a half-built host
        throw;
    }
}
Result DesktopHost::rebuild_renderer() {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (painting_) return {Status::busy, u"Painting in progress"};
    if (!renderer_failure_) return {Status::invalid_value, u"Renderer is not failed"};
    if (hooks_) ++hooks_->rebuild_attempts;
    renderer_.reset();
    struct ShaderOverride {
        RendererTestHooks* hooks;
        std::string previous;
        explicit ShaderOverride(RendererTestHooks* value): hooks(value) {
            if (hooks) { previous=hooks->fragment_shader_override; if (hooks->fail_rebuild_once) { hooks->fail_rebuild_once=false; hooks->fragment_shader_override="invalid shader"; } }
        }
        ~ShaderOverride() { if (hooks) hooks->fragment_shader_override=std::move(previous); }
    } shader_override(hooks_);
    try {
        renderer_ = std::make_unique<Renderer>(window_, hooks_, debug_context_, [this](RenderDiagnostic event) { diagnostics_.push(std::move(event)); });
        renderer_->restore_atlas(text_.atlas_pages());
        capabilities_=renderer_->capabilities();
    } catch (const StartupError& e) { renderer_.reset(); return {Status::failed,e.failure.message}; }
      catch (const std::exception& e) { renderer_.reset(); std::string text=e.what(); return {Status::failed,utf16_from_utf8(text)}; }
    renderer_failure_.reset(); detail::access(ui_).renderer_failed(false);
    schedule_tooltip();
    InvalidateRect(window_,nullptr,FALSE);
    return {};
}
void DesktopHost::fail_renderer(RendererFailure failure) {
    renderer_failure_=std::move(failure);
    if (window_) KillTimer(window_, tooltip_timer);
    detail::access(ui_).renderer_failed(true);
    if (auto session=detail::access(ui_).edit(detail::access(ui_).focused()); session && session->composing) {
        if (auto imc=ImmGetContext(window_)) { ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0); ImmReleaseContext(window_, imc); }
        detail::access(ui_).composition_cancel();
    }
    PostMessageW(window_,WM_APP+44,0,0);
}
void DesktopHost::configure_font(std::u16string_view family) {
    if(font_configured_ && requested_font_==family) return;
    PresentationSettings settings; settings.font_family=family;
    auto resolution=system_font_catalog().resolve(settings);
    enqueue_font_diagnostics(diagnostics_,font_configured_?0:system_font_catalog_error(),resolution);
    requested_font_=family; font_configured_=true;
    text_.set_primary_family(!family.empty() && resolution.requested_available ? resolution.primary : std::u16string{}, true);
    if(window_) PostMessageW(window_,WM_APP+43,0,0);
}
DesktopHost::~DesktopHost() { release(); }
void DesktopHost::release() {
    if (uia_) { uia_->close(); uia_.reset(); }
    // A host may be destroyed while its Ui remains alive. Discard an unfinished
    // preedit on both sides before detaching the window; closing must not leave
    // an orphaned composition in the retained text session.
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (window_ && session && session->composing && !renderer_failure_) {
        if (auto imc = ImmGetContext(window_)) {
            ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(window_, imc);
        }
        detail::access(ui_).composition_cancel();
    }
    detail::access(ui_).on_presentation({});
    if (detail::access(ui_).bound_appearance() == &appearance_) detail::access(ui_).bind_appearance(nullptr);
    detail::access(ui_).on_invalidate({});
    detail::access(ui_).on_relayout({});
    detail::access(ui_).on_semantic_pending({});
    detail::access(ui_).on_reveal({});
    detail::access(ui_).tooltip_clock({});
    if (window_) { KillTimer(window_, tooltip_timer); KillTimer(window_, drag_scroll_timer); }
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
    BOOL result = 0;
    while (!quit_ && (result = GetMessageW(&msg, nullptr, 0, 0)) > 0) dispatch(msg);
    return result < 0 || failed_ ? 1 : quit_ ? 0 : static_cast<int>(msg.wParam);
}
void DesktopHost::dispatch(MSG& message) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
    follow_focus();
}
void DesktopHost::schedule_tooltip() {
    if (!window_) return;
    KillTimer(window_, tooltip_timer);
    if (renderer_failure_) return;
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
    if (renderer_failure_ || painting_) return;
    struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(painting_);
    auto start=std::chrono::steady_clock::now();
    RECT r{};
    GetClientRect(window_, &r);
    if (!renderer_ || r.right <= 0 || r.bottom <= 0) return;
    float s = scale();
    if(perf_enabled_ && perf_offscreen_) { r.right=3840; r.bottom=2160; s=2.f; }
    frame_ = arrange(r.right, r.bottom, s);
    auto after_layout=std::chrono::steady_clock::now();
    reveal_.clear();
    RecordingCanvas recording(r.right, r.bottom, detail::access(ui_).effective_palette(appearance_).background);
    paint_observations_.clear();
    recording.count_ink = observe_paint_;
    draw(recording, frame_, s, GetFocus() == window_);
    auto after_list=std::chrono::steady_clock::now();
    for (const auto& observation : recording.list.observations) {
        paint_observations_.push_back({static_cast<PaintObservation::Type>(observation.type), observation.owner, observation.text,
            observation.bounds, observation.clip, observation.color,
            command_ink(recording.list, observation.begin, observation.end, observation.clip, r.right, r.bottom),
            observation.popup, observation.tooltip, observation.bold, observation.italic});
    }
    if(perf_enabled_ && perf_delay_) Sleep(static_cast<DWORD>(perf_delay_));
    auto before_gpu=std::chrono::steady_clock::now();
    double submission=0; std::chrono::steady_clock::time_point presented;
    double gpu=0;
    try { gpu=renderer_->draw(recording.list, r.right, r.bottom, perf_enabled_, perf_offscreen_, &submission, &presented); }
    catch (const FrameError& e) {
        for (auto code : e.codes) diagnostics_.push(frame_diagnostic(code));
        PostMessageW(window_,WM_APP+43,0,0);
        fail_renderer(e.failure); return;
    }
    catch (const std::exception& e) {
        std::string message=e.what();
        PostMessageW(window_,WM_APP+43,0,0);
        fail_renderer({RendererFailureKind::resource_failed,0,utf16_from_utf8(message)}); return;
    }
    auto end=std::chrono::steady_clock::now();
    if(perf_enabled_) {
        FILETIME created{},exited{},kernel{},user{},now{}; GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user); GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER c{},n{}; c.LowPart=created.dwLowDateTime; c.HighPart=created.dwHighDateTime; n.LowPart=now.dwLowDateTime; n.HighPart=now.dwHighDateTime;
        double age=double(n.QuadPart-c.QuadPart)/10000. - std::chrono::duration<double,std::milli>(end-presented).count();
        if(first_present_ms_<0) first_present_ms_=age;
        double cpu=std::chrono::duration<double,std::milli>(before_gpu-start).count();
        double sample_age=age-std::chrono::duration<double,std::milli>(presented-start).count();
        perf_samples_.add(sample_age,cpu+submission+gpu);
        perf_breakdown_.add(sample_age,std::chrono::duration<double,std::milli>(after_layout-start).count(),
            std::chrono::duration<double,std::milli>(after_list-after_layout).count(),submission+gpu);
        if(input_time_.time_since_epoch().count()) { input_present_ms_=std::chrono::duration<double,std::milli>(presented-input_time_).count(); input_time_={}; }
    }
    ++frames_;
    sync_ime();
    PostMessageW(window_,WM_APP+43,0,0);
}
Frame DesktopHost::layout(int width, int height, float s, ScrollState& scroll, std::string_view reveal) {
    auto frame=lcars::arrange(page_(), ui_, appearance_, measure_, width, height, s, scroll, reveal);
    diagnostics_.drain(); return frame;
}
Frame DesktopHost::render(int width, int height, float s, const std::filesystem::path& bmp, Image* copy) {
    auto frame = arrange(width, height, s);
    reveal_.clear();
    RecordingCanvas recording(width, height, detail::access(ui_).effective_palette(appearance_).background);
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
    RecordingCanvas recording(width, height, detail::access(ui_).effective_palette(appearance_).background);
    draw(recording, frame, s, true);
    auto reference = render_cpu(recording.list, width, height);
    std::vector<Rect> regions;
    std::vector<GpuRegion> observed;
    std::string owner; bool popup=false;
    unsigned roles=0;
    Rect clip{0,0,width,height};
    for (std::size_t i=0;i<recording.list.commands.size();++i) {
        const auto& command=recording.list.commands[i];
        if(command.type==DrawCommand::Type::clip){clip=intersect(command.rect,{0,0,width,height});continue;}
        if(command.type==DrawCommand::Type::begin_group){owner=command.owner;popup=command.popup;continue;}
        if(command.type==DrawCommand::Type::end_group){owner.clear();popup=false;continue;}
        if(command.role==PaintRole::content)continue;
        auto glyph=command.resolve?command.resolve():ResolvedGlyph{command.rect,command.coverage};
        auto bounds=intersect(glyph.rect,clip);
        if(bounds.w>0 && bounds.h>0){
            bool ink=command_ink(recording.list,i,i+1,clip,width,height);
            regions.push_back(bounds); observed.push_back({bounds,owner,static_cast<unsigned>(command.role),popup,ink,false});
            if(ink)roles |= static_cast<unsigned>(command.role);
        }
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
    for(std::size_t i=0;i<observed.size();++i){
        observed[i].passed=observed[i].ink && result.region_pass[i];
        if(!observed[i].passed)failed_roles |= observed[i].role;
    }
    bool passed=!failed_roles && result.regions_ok && result.different_pixels*200<=static_cast<std::size_t>(width)*height;
    return {std::move(reference),std::move(actual),std::move(result),roles,failed_roles,std::move(observed),passed};
}
std::string DesktopHost::renderer_name() const { return renderer_->name(); }

bool DesktopHost::compare_legacy(int width, int height, float s) {
    auto frame = arrange(width, height, s);
    Canvas old(width, height, detail::access(ui_).effective_palette(appearance_).background);
    draw(old, frame, s, true);
    RecordingCanvas recording(width, height, detail::access(ui_).effective_palette(appearance_).background);
    draw(recording, frame, s, true);
    return old.pixels == render_cpu(recording.list, width, height).pixels;
}

template<class Target> void DesktopHost::draw(Target& canvas, const Frame& frame, float s, bool window_focused) {
    UiState::OperationScope operation(&access(ui_));
    const auto p = detail::access(ui_).effective_palette(appearance_);
    const Rect whole{0, 0, canvas.width, canvas.height};
    struct PaintAdapter final : ComponentPaint {
        Target& canvas;
        Text& renderer;
        std::vector<PaintObservation>* observations;
        PaintAdapter(UiState& ui, const Placement& item, const Semantics& semantics, const Appearance& appearance,
                     float scale, bool window_focused, Target& target, Text& text, std::vector<PaintObservation>* events)
            : ComponentPaint(ui, item, semantics, appearance, scale, window_focused), canvas(target), renderer(text), observations(events) {}
        void role(PaintRole value) override { if constexpr (std::is_same_v<Target, RecordingCanvas>) canvas.current_role=value; }
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
        void clip(Rect rect) override { canvas.clip = intersect(canvas.clip, rect); }
        void text(std::u16string_view value, int pixels, int x, int y, Color color) override { styled_text(value, pixels, {}, x, y, color); }
        void styled_text(std::u16string_view value, int pixels, FontStyle style, int x, int y, Color color) override {
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
        if (item.dropdown_panel) for (const auto& region : frame.regions) {
            if (region.state != &detail::access(ui_).dropdown_scroll(item.id) || !region.thumb.h) continue;
            canvas.rounded(region.track, region.track.w / 2, p.panel);
            canvas.rounded(region.thumb, region.thumb.w / 2, p.border);
        }
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
    const auto anchor = field->caret_rect(session->visible_caret());  // a wrapped line's own row in a multi-line field
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
// Extend the selection of the field being dragged; outside a multi-line field's top or bottom edge it
// scrolls a line, and a timer repeats that while the pointer rests there.
// Scrolling moves a composing caret's line; the input method's window must move with it.
void DesktopHost::follow_composition() {
    const auto* session = detail::access(ui_).edit(detail::access(ui_).focused());
    if (!session || !session->composing) return;
    relayout();
    sync_ime();
}
void DesktopHost::relayout() {
    RECT r{};
    if (window_ && !painting_ && GetClientRect(window_, &r) && r.right > 0 && r.bottom > 0) frame_ = arrange(r.right, r.bottom, scale());
}
void DesktopHost::drag_text(int x, int y) {
    const auto& id = detail::access(ui_).focused();
    auto handle = detail::access(ui_).layout_handle(id);
    if (auto* field = std::get_if<TextHandle>(&handle)) {
        frame_.select_to(ui_, *field, x, y);
        const auto* placed = detail::find_placement(frame_, id);
        const bool outside = placed && placed->multiline && (y < placed->view.text.y || y >= placed->view.text.y + static_cast<int>(placed->view.visible) * placed->view.line_height);
        if (outside) SetTimer(window_, drag_scroll_timer, 60, nullptr);
        else KillTimer(window_, drag_scroll_timer);
    }
    sync_ime();
    InvalidateRect(window_, nullptr, FALSE);
}
void DesktopHost::after_input(const Id& focus_before) {
    if (detail::access(ui_).focused() != focus_before) reveal_ = detail::access(ui_).focused();
    // The input method anchors at the caret's wrapped line, so settle the lines this input changed first.
    if (const auto* focused = detail::access(ui_).find(detail::access(ui_).focused()); focused && focused->multiline) relayout();
    sync_ime();
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT DesktopHost::message(UINT msg, WPARAM w, LPARAM l) {
    const bool close_key = msg == WM_SYSKEYDOWN && w == VK_F4 && (l & (1LL << 29));
    if (renderer_failure_ && !close_key && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
        (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_COMPOSITION) ||
        msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_TIMER || msg == WM_CAPTURECHANGED || msg == WM_CANCELMODE)) return 0;
    if(perf_enabled_ && (msg==WM_KEYDOWN || msg==WM_CHAR)) input_time_=std::chrono::steady_clock::now()-std::chrono::milliseconds(static_cast<DWORD>(GetTickCount()-GetMessageTime()));
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
        if (uia_ && static_cast<LONG>(l) == UiaRootObjectId) return uia_->get_object(w, l);
        break;
    case uia_dispatch_message:
        if (uia_) uia_->dispatch();
        return 0;
    case WM_APP+44:
        if (renderer_failure_) {
            if (failure_callback_) { auto failure = *renderer_failure_; failure_callback_(failure); }
            else { quit_ = true; release(); }
        }
        return 0;
    case WM_APP+43:
        diagnostics_.drain(); return 0;
    case WM_APP+45:
        ui_.semantics().dispatch_events(); return 0;
    case WM_PAINT: {
        ++paint_messages_;
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
    case WM_CLOSE: {
        if (closing_request_) return 0;
        closing_request_ = true;
        if (!renderer_failure_) { log_ime(L"host:close"); finish_composition(); }
        auto decision = close_callback_ ? close_callback_() : CloseDecision::close;
        if (decision == CloseDecision::close) quit_ = true;
        closing_request_ = false;
        return 0;
    }
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
        if (w == drag_scroll_timer) {
            POINT point{};
            if (drag_ != Drag::text || !GetCursorPos(&point) || !ScreenToClient(window_, &point)) { KillTimer(window_, drag_scroll_timer); return 0; }
            drag_text(point.x, point.y);
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
        if (LOWORD(w) == WA_INACTIVE) {
            detail::access(ui_).cancel_input();
            // An interrupted slider drag returns its thumb to the value's tick.
            if (drag_ == Drag::slider) { drag_ = Drag::none; InvalidateRect(window_, nullptr, FALSE); }
        }
        break;
    case WM_CANCELMODE:
        detail::access(ui_).cancel_input();
        drag_ = Drag::none;
        if (GetCapture() == window_) ReleaseCapture();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(l) != window_) { detail::access(ui_).cancel_input(); drag_ = Drag::none; KillTimer(window_, drag_scroll_timer); InvalidateRect(window_, nullptr, FALSE); }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(window_, &point);
        int delta = GET_WHEEL_DELTA_WPARAM(w) * static_cast<int>(48 * scale()) / WHEEL_DELTA;
        frame_.wheel(ui_, point.x, point.y, delta);
        follow_composition();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        // "Always underline access keys and show keyboard focus": takes effect without a restart.
        if (w == SPI_SETKEYBOARDCUES || w == 0) detail::access(ui_).set_system_keyboard_cues(system_keyboard_cues());
        break;
    case WM_LBUTTONDOWN: {
        detail::access(ui_).pointer_pressed();  // any press in the client area hides the indicator
        SetFocus(window_);
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        pending_high_ = 0;
        const auto* hit = frame_.hit(x, y);
        if (!hit || !hit->dropdown_panel) detail::access(ui_).close_popup();
        {
            // An open dialog blocks the page's scrollbars but not its own.
            const bool popup_hit = hit && hit->kind == Kind::dropdown && hit->popup;
            for (auto it = frame_.regions.rbegin(); it != frame_.regions.rend(); ++it) {
                if (frame_.modal && !popup_hit && !it->modal_content) continue;
                if (!it->thumb.h || !it->clip.contains(x, y) || !it->track.contains(x, y)) continue;
                if (hit && hit->dropdown_panel && it->state != &detail::access(ui_).dropdown_scroll(hit->id)) continue;
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
        const bool on_list = hit->kind == Kind::list;  // after_input may arrange again, which replaces the frame hit points into
        finish_composition();
        if (detail::access(ui_).pointer_down(id, frame_.option_at(*hit, x, y))) {
            if (detail::component_type(hit->kind).selects_text()) {
                auto at = detail::placement_text_hit(frame_, id, x, y);
                auto anchor = (w & MK_SHIFT) ? detail::access(ui_).edit(id)->selection.anchor : at.offset;
                detail::access(ui_).select_pointer(id, anchor, at.offset, at.upstream);
                drag_ = Drag::text;
            }
            if (hit->kind == Kind::slider) {
                drag_ = Drag::slider;
                detail::access(ui_).slider_pointer_down(id, x, hit->slider_track);
            }
            SetCapture(window_);
        }
        after_input(before);
        // A pointer already targets a visible row. Revealing the list's entry cursor here
        // would move the row before button-up and cancel or redirect the click.
        if (on_list) reveal_.clear();
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
            drag_text(x, y);
        } else if (drag_ == Drag::slider && (w & MK_LBUTTON)) {
            detail::access(ui_).slider_pointer(detail::access(ui_).focused(), x);
            InvalidateRect(window_, nullptr, FALSE);
        } else if (drag_ == Drag::thumb && (w & MK_LBUTTON)) {
            auto region = std::find_if(frame_.regions.begin(), frame_.regions.end(),
                [this](const ScrollRegion& item) { return item.state == drag_scroll_; });
            if (region != frame_.regions.end()) frame_.scroll_to(ui_, *drag_scroll_, region->offset_for_thumb(drag_offset_ + (y - drag_origin_)));
            follow_composition();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        auto before = detail::access(ui_).focused();
        if (drag_ == Drag::slider) {
            const auto id = detail::access(ui_).focused();
            detail::access(ui_).slider_pointer(id, x);
            detail::access(ui_).pointer_up(id);
        } else if (drag_ == Drag::none) {
            const auto* hit = frame_.hit(x, y);
            // Settle the press before releasing capture; capture loss would cancel it.
            detail::access(ui_).pointer_up(hit ? std::string_view(hit->id) : std::string_view{}, hit ? frame_.option_at(*hit, x, y) : std::nullopt);
        }
        drag_ = Drag::none; drag_scroll_ = nullptr;
        KillTimer(window_, drag_scroll_timer);
        if (GetCapture() == window_) ReleaseCapture();
        after_input(before);
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (w == VK_F4 && (l & (1LL << 29))) { SendMessageW(window_, WM_CLOSE, 0, 0); return 0; }
        break;
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
            const auto anchor = field->caret_rect(at);
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

namespace lcars {
struct Desktop::Impl {
    std::unique_ptr<detail::DesktopHost> host;
    DesktopOptions options;
    std::thread::id owner = std::this_thread::get_id();
};
Desktop::Desktop(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
Desktop::~Desktop() = default;
DesktopCreation Desktop::create(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopOptions options) {
    return detail::DesktopHost::create_for_test(ui,appearance,std::move(page),scroll,std::move(options),nullptr);
}
DesktopCreation detail::DesktopHost::create_for_test(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopOptions options, detail::RendererTestHooks* hooks) {
    try {
        auto impl=std::make_unique<Desktop::Impl>(); impl->options=std::move(options);
        impl->host=std::make_unique<detail::DesktopHost>(ui,appearance,std::move(page),scroll,impl->options,false,false,hooks);
        auto desktop=std::unique_ptr<Desktop>(new Desktop(std::move(impl)));
        if (desktop->impl_->options.on_close_requested) {
            auto ptr = desktop.get();
            ptr->impl_->host->on_close_request([ptr] { return ptr->impl_->options.on_close_requested(*ptr); });
        }
        if (desktop->impl_->options.on_renderer_failed) {
            auto ptr=desktop.get();
            ptr->impl_->host->on_renderer_failure([ptr](const RendererFailure& failure) { ptr->impl_->options.on_renderer_failed(*ptr,failure); });
        }
        return {Status::ok,{},std::move(desktop)};
    } catch (const detail::StartupError& e) { return {Status::failed,e.failure,{}}; }
      catch (const std::exception& e) { std::string message=e.what(); return {Status::failed,StartupFailure{StartupFailureKind::resource,{}, {},detail::utf16_from_utf8(message)},{}}; }
}
RunResult Desktop::run() {
    auto& host=*impl_->host;
    ShowWindow(host.window(),SW_SHOW); SetFocus(host.window()); UpdateWindow(host.window());
    int result=host.run();
    auto failure=host.renderer_failure();
    host.close();
    if (failure) return {Status::renderer_failed,failure,{}};
    return {result ? Status::failed : Status::ok,{}, {}};
}
Result Desktop::set_title(std::u16string title) {
    if (std::this_thread::get_id() != impl_->owner) return {Status::wrong_thread, u"Operation requires the UI thread"};
    if (!valid_single_line(title, title.size())) return {Status::invalid_value, u"Invalid title"};
    if (!SetWindowTextW(impl_->host->window(), detail::wide(title).c_str())) return {Status::failed, u"Unable to set window title"};
    return {};
}
Result Desktop::close() {
    if (std::this_thread::get_id() != impl_->owner) return {Status::wrong_thread, u"Operation requires the UI thread"};
    impl_->host->request_quit(); return {};
}
void* Desktop::native_window() const { return impl_->host->window(); }
Result Desktop::rebuild_renderer() { return impl_->host->rebuild_renderer(); }
bool Desktop::renderer_failed() const { return impl_->host->renderer_failed(); }
RendererCapabilities Desktop::capabilities() const { return impl_->host->capabilities(); }
RunResult run_desktop(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll, DesktopOptions options) {
    auto creation=Desktop::create(ui,appearance,std::move(page),scroll,std::move(options));
    if (!creation.desktop) return {creation.status,{},creation.failure};
    return creation.desktop->run();
}
}
