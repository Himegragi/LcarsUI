#pragma once
// Internal host type shared by runDesktop and the native verification driver.
#include "isoduo/desktop.hpp"
#include "renderer.hpp"
#include "desktop_paint.hpp"
#include "text.hpp"
#include "diagnostic_queue.hpp"
#include "perf.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace isoduo::detail {
class UiaBridge;
class WindowsClipboard final : public Clipboard {
public:
    explicit WindowsClipboard(HWND window) : window_(window) {}
    bool write(std::u16string_view text) override;
    bool writeRich(std::u16string_view text, std::string_view payload) override;
    Query<std::string> readRich() override;
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
    unsigned roles = 0, failedRoles = 0;
    std::vector<GpuRegion> regions;
    bool passed = false;
    explicit operator bool() const { return passed; }
};
class DesktopHost {
public:
    static CreateResult createForTest(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&, DesktopConfig, RendererTestHooks*);
    DesktopHost(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                const DesktopConfig& options, bool trace = false, bool background = false, RendererTestHooks* hooks = nullptr);
    ~DesktopHost();
    DesktopHost(const DesktopHost&) = delete;
    DesktopHost& operator=(const DesktopHost&) = delete;

    HWND window() const { return window_; }
    void attach(HWND window) { window_ = window; }  // from WM_NCCREATE, before CreateWindowExW returns
    int run();                              // GetMessage loop until WM_QUIT
    void dispatch(MSG& message);            // TranslateMessage + DispatchMessage, as run() does
    void pump();                            // dispatch everything already queued
    void clientSize(int widthDip, int heightDip);
    float scale() const;
    void forceScale(float scale) { forcedScale_ = scale; }  // 0 follows the window DPI
    // Mouse messages that touch or pen input synthesizes carry this signature in the message extra info.
    static bool touchSignature(std::uintptr_t extraInfo);
    // Test seam: replaces the extra-info read, which a posted message cannot carry.
    void touchSource(std::function<bool()> source) { touchSource_ = std::move(source); }
    void paint();                           // arrange, draw and present one frame
    // Arrange and raster offscreen at an arbitrary physical size; saves a 32-bit BMP.
    Frame render(int width, int height, float scale, const std::filesystem::path& bmp, Image* pixels = nullptr);
    bool compareLegacy(int width, int height, float scale);
    GpuComparison compareGpu(int width, int height, float scale, int fault = 0);
    std::string rendererName() const;
    // Arrange only, with the host's real text measurement and an independent scroll state.
    Frame layout(int width, int height, float scale, ScrollState& scroll, std::string_view reveal);
    const Frame& frame() const { return frame_; }
    // Verification observer: records only the most recently painted actual frame.
    void observePaint(bool enabled) { observePaint_ = enabled; paintObservations_.clear(); }
    const std::vector<PaintObservation>& paintObservations() const { return paintObservations_; }
    void configureAtlas(int size, int pages) { text_.configureAtlas(size, pages); }
    void clearAtlas() { text_.clearAtlas(); }
    unsigned atlasClears() const { return text_.atlasClears(); }
    unsigned atlasBatches() const { return text_.atlasBatches(); }
    unsigned atlasPeakPages() const { return text_.atlasPeakPages(); }
    unsigned rasterizations() const { return text_.rasterizations(); }
    void perfEnable(int delayMs=0, bool offscreen=false) { perfEnabled_=true; perfDelay_=delayMs; perfOffscreen_=offscreen; }
    void perfReset() { perfSamples_.clear(); perfBreakdown_.clear(); }
    const PerfSamples& perfSamples() const { return perfSamples_; }
    const PerfBreakdown& perfBreakdown() const { return perfBreakdown_; }
    int paintMessages() const { return paintMessages_; }
    double firstPresentMs() const { return firstPresentMs_; }
    double inputPresentMs() const { return inputPresentMs_; }
    int frames() const { return frames_; }
    bool failed() const { return failed_; }
    bool rendererFailed() const { return rendererFailure_.has_value(); }
    const std::optional<RendererFailure>& rendererFailure() const { return rendererFailure_; }
    void onRendererFailure(std::function<void(const RendererFailure&)> callback) { failureCallback_ = std::move(callback); }
    Result rebuildRenderer();
    // #75: the system "Show animations in Windows" preference, re-read on WM_SETTINGCHANGE. Tests may
    // replace the reader to simulate it being on or off (and flip it while running).
    void systemAnimationSource(std::function<bool()> reader) { animationReader_ = std::move(reader); refreshSystemAnimations(); }
    // Tests replace the minimized / not shown / cloaked / covered check so another window on a shared
    // desktop cannot decide the outcome; with no reader the real window state is used.
    void hiddenSource(std::function<bool()> reader) { hiddenReader_ = std::move(reader); refreshVisibility(); }
    bool windowHidden() const { return hidden_; }
    void close() { release(); }
    void requestQuit() { quit_ = true; }
    void onCloseRequest(std::function<CloseDecision()> callback) { closeCallback_ = std::move(callback); }
    RendererCapabilities capabilities() const { return renderer_ ? renderer_->capabilities() : capabilities_; }
    std::vector<std::wstring>& imeLog() { return imeLog_; }
    std::vector<std::wstring>& trace() { return trace_; }  // verification mode: input messages as handled
    LRESULT message(UINT msg, WPARAM w, LPARAM l);

private:
    Ui& ui_;
    std::unique_ptr<UiaBridge> uia_;
    const Appearance& appearance_;
    std::function<layout::Node()> page_;
    ScrollState& scroll_;
    HWND window_ = nullptr;
    HIMC inputContext_ = nullptr;
    bool imeAssociated_ = true;
    DiagnosticQueue diagnostics_;
    Text text_;
    std::u16string requestedFont_;
    bool fontConfigured_ = false;
    ShapedMeasure measure_{text_};
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<WindowsClipboard> clipboard_;
    Frame frame_;
    float forcedScale_ = 0;
    float atlasScale_ = 0;
    int atlasTextSize_ = 0;
    std::u16string atlasFontFamily_;
    bool perfEnabled_=false, perfOffscreen_=false;
    int perfDelay_=0, paintMessages_=0;
    double firstPresentMs_=-1, inputPresentMs_=0;
    std::chrono::steady_clock::time_point inputTime_{};
    PerfSamples perfSamples_;
    PerfBreakdown perfBreakdown_;
    int frames_ = 0;
    bool failed_ = false;
    bool painting_ = false;
    bool quit_ = false;
    bool closingRequest_ = false;
    std::function<CloseDecision()> closeCallback_;
    std::thread::id owner_ = std::this_thread::get_id();
    RendererTestHooks* hooks_ = nullptr;
    DebugContext debugContext_ = DebugContext::buildDefault;
    RendererCapabilities capabilities_;
    std::optional<RendererFailure> rendererFailure_;
    std::function<void(const RendererFailure&)> failureCallback_;
    std::string reveal_;                        // focus target to scroll into view on the next arrange
    enum class Drag { none, text, thumb, slider } drag_ = Drag::none;
    int dragOrigin_ = 0, dragOffset_ = 0;
    ScrollState* dragScroll_ = nullptr;
    ComponentHandle dragComponent_;
    char16_t pendingHigh_ = 0;
    bool swallowLeft_ = false, swallowRight_ = false;  // a press that only closed a menu: its release is dropped too
    bool menuSysSwallow_ = false, dropSpaceChar_ = false;
    void openPointerMenu(int x, int y);
    std::vector<std::wstring> imeLog_, trace_;
    bool tracing_ = false;
    bool observePaint_ = false;
    std::vector<PaintObservation> paintObservations_;

    Frame arrange(int width, int height, float scale);
    void configureFont(std::u16string_view family);
    void release();
    void failRenderer(RendererFailure failure);
    template<class Target> void draw(Target& canvas, const Frame& frame, float scale, bool windowFocused);
    void finishComposition();         // ask the IME to commit before focus moves
    void syncIme();
    void dragText(int x, int y);       // pointer drag over the focused text field
    void followComposition();          // re-anchor the input method after a scroll
    void relayout();                    // arrange again now, outside a paint
    void scheduleTooltip();
    void updateHover(int x, int y, bool touch = false);  // pointer position in client pixels; touch/pen messages are ignored
    bool synthesizedByTouch() const;  // the message being handled came from touch or pen
    std::function<bool()> touchSource_;
    void followFocus();               // notice focus moved by application or semantic calls
    std::string syncedFocus_;
    std::function<bool()> animationReader_, hiddenReader_;
    bool hidden_ = false;
    void refreshSystemAnimations();
    void refreshVisibility();         // minimized / not shown / cloaked / fully covered: motion ends at once
    void scheduleAnimation();         // next frame while a transition runs, nothing otherwise
    void afterInput(const std::string& focusBefore);
    void logIme(const wchar_t* event, std::u16string_view detail = {});
};
}

