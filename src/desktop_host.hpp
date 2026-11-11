#pragma once
// Internal host type shared by run_desktop and the native verification driver.
#include "lcars/desktop.hpp"
#include "renderer.hpp"
#include "text.hpp"
#include "diagnostic_queue.hpp"
#include "perf.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace lcars::detail {
class UiaBridge;
struct PaintObservation {
    enum class Type { text, fill, rounded } type;
    std::string owner;
    std::u16string text;
    Rect bounds, clip;
    Color color;
    bool visible = false, popup = false, tooltip = false;
};
class WindowsClipboard final : public Clipboard {
public:
    explicit WindowsClipboard(HWND window) : window_(window) {}
    bool write(std::u16string_view text) override;
    Query<std::u16string> read() override;
private:
    HWND window_;
};

struct GpuRegion {
    Rect bounds{};
    std::string owner;
    unsigned role = 0;
    bool popup = false, ink = false, passed = false;
};
struct GpuComparison {
    Image reference, actual;
    ImageComparison comparison;
    unsigned roles = 0, failed_roles = 0;
    std::vector<GpuRegion> regions;
    bool passed = false;
    explicit operator bool() const { return passed; }
};
class DesktopHost {
public:
    static DesktopCreation create_for_test(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&, DesktopOptions, RendererTestHooks*);
    DesktopHost(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                const DesktopOptions& options, bool trace = false, bool background = false, RendererTestHooks* hooks = nullptr);
    ~DesktopHost();
    DesktopHost(const DesktopHost&) = delete;
    DesktopHost& operator=(const DesktopHost&) = delete;

    HWND window() const { return window_; }
    void attach(HWND window) { window_ = window; }  // from WM_NCCREATE, before CreateWindowExW returns
    int run();                              // GetMessage loop until WM_QUIT
    void dispatch(MSG& message);            // TranslateMessage + DispatchMessage, as run() does
    void pump();                            // dispatch everything already queued
    void client_size(int width_dip, int height_dip);
    float scale() const;
    void force_scale(float scale) { forced_scale_ = scale; }  // 0 follows the window DPI
    void paint();                           // arrange, draw and present one frame
    // Arrange and raster offscreen at an arbitrary physical size; saves a 32-bit BMP.
    Frame render(int width, int height, float scale, const std::filesystem::path& bmp, Image* pixels = nullptr);
    bool compare_legacy(int width, int height, float scale);
    GpuComparison compare_gpu(int width, int height, float scale, int fault = 0);
    std::string renderer_name() const;
    // Arrange only, with the host's real text measurement and an independent scroll state.
    Frame layout(int width, int height, float scale, ScrollState& scroll, std::string_view reveal);
    const Frame& frame() const { return frame_; }
    // Verification observer: records only the most recently painted actual frame.
    void observe_paint(bool enabled) { observe_paint_ = enabled; paint_observations_.clear(); }
    const std::vector<PaintObservation>& paint_observations() const { return paint_observations_; }
    void configure_atlas(int size, int pages) { text_.configure_atlas(size, pages); }
    void clear_atlas() { text_.clear_atlas(); }
    unsigned atlas_clears() const { return text_.atlas_clears(); }
    unsigned atlas_batches() const { return text_.atlas_batches(); }
    unsigned atlas_peak_pages() const { return text_.atlas_peak_pages(); }
    unsigned rasterizations() const { return text_.rasterizations(); }
    void perf_enable(int delay_ms=0, bool offscreen=false) { perf_enabled_=true; perf_delay_=delay_ms; perf_offscreen_=offscreen; }
    void perf_reset() { perf_samples_.clear(); perf_breakdown_.clear(); }
    const PerfSamples& perf_samples() const { return perf_samples_; }
    const PerfBreakdown& perf_breakdown() const { return perf_breakdown_; }
    int paint_messages() const { return paint_messages_; }
    double first_present_ms() const { return first_present_ms_; }
    double input_present_ms() const { return input_present_ms_; }
    int frames() const { return frames_; }
    bool failed() const { return failed_; }
    bool renderer_failed() const { return renderer_failure_.has_value(); }
    const std::optional<RendererFailure>& renderer_failure() const { return renderer_failure_; }
    void on_renderer_failure(std::function<void(const RendererFailure&)> callback) { failure_callback_ = std::move(callback); }
    Result rebuild_renderer();
    void close() { release(); }
    void request_quit() { quit_ = true; }
    void on_close_request(std::function<CloseDecision()> callback) { close_callback_ = std::move(callback); }
    RendererCapabilities capabilities() const { return renderer_ ? renderer_->capabilities() : capabilities_; }
    std::vector<std::wstring>& ime_log() { return ime_log_; }
    std::vector<std::wstring>& trace() { return trace_; }  // verification mode: input messages as handled
    LRESULT message(UINT msg, WPARAM w, LPARAM l);

private:
    Ui& ui_;
    std::unique_ptr<UiaBridge> uia_;
    const Appearance& appearance_;
    std::function<layout::Node()> page_;
    ScrollState& scroll_;
    HWND window_ = nullptr;
    HIMC input_context_ = nullptr;
    bool ime_associated_ = true;
    DiagnosticQueue diagnostics_;
    Text text_;
    std::u16string requested_font_;
    bool font_configured_ = false;
    ShapedMeasure measure_{text_};
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<WindowsClipboard> clipboard_;
    Frame frame_;
    float forced_scale_ = 0;
    float atlas_scale_ = 0;
    int atlas_text_size_ = 0;
    std::u16string atlas_font_family_;
    bool perf_enabled_=false, perf_offscreen_=false;
    int perf_delay_=0, paint_messages_=0;
    double first_present_ms_=-1, input_present_ms_=0;
    std::chrono::steady_clock::time_point input_time_{};
    PerfSamples perf_samples_;
    PerfBreakdown perf_breakdown_;
    int frames_ = 0;
    bool failed_ = false;
    bool painting_ = false;
    bool quit_ = false;
    bool closing_request_ = false;
    std::function<CloseDecision()> close_callback_;
    std::thread::id owner_ = std::this_thread::get_id();
    RendererTestHooks* hooks_ = nullptr;
    DebugContext debug_context_ = DebugContext::build_default;
    RendererCapabilities capabilities_;
    std::optional<RendererFailure> renderer_failure_;
    std::function<void(const RendererFailure&)> failure_callback_;
    std::string reveal_;                        // focus target to scroll into view on the next arrange
    enum class Drag { none, text, thumb, slider } drag_ = Drag::none;
    int drag_origin_ = 0, drag_offset_ = 0;
    ScrollState* drag_scroll_ = nullptr;
    char16_t pending_high_ = 0;
    std::vector<std::wstring> ime_log_, trace_;
    bool tracing_ = false;
    bool observe_paint_ = false;
    std::vector<PaintObservation> paint_observations_;

    Frame arrange(int width, int height, float scale);
    void configure_font(std::u16string_view family);
    void release();
    void fail_renderer(RendererFailure failure);
    template<class Target> void draw(Target& canvas, const Frame& frame, float scale, bool window_focused);
    void finish_composition();         // ask the IME to commit before focus moves
    void sync_ime();
    void schedule_tooltip();
    void update_hover(int x, int y);
    void follow_focus();               // notice focus moved by application or semantic calls
    std::string synced_focus_;
    void after_input(const std::string& focus_before);
    void log_ime(const wchar_t* event, std::u16string_view detail = {});
};
}

