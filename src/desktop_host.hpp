#pragma once
// Internal host type shared by run_desktop and the native verification driver.
#include "lcars/desktop.hpp"
#include "renderer_opengl.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lcars::detail {
class WindowsClipboard final : public Clipboard {
public:
    explicit WindowsClipboard(HWND window) : window_(window) {}
    bool write(std::u16string_view text) override;
    std::optional<std::u16string> read() override;
private:
    HWND window_;
};

class DesktopHost {
public:
    DesktopHost(Ui& ui, const Appearance& appearance, std::function<layout::Node()> page, ScrollState& scroll,
                const DesktopOptions& options, bool trace = false, bool background = false);
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
    Frame render(int width, int height, float scale, const std::filesystem::path& bmp, Canvas* pixels = nullptr);
    // Arrange only, with the host's real text measurement and an independent scroll state.
    Frame layout(int width, int height, float scale, ScrollState& scroll, std::string_view reveal);
    const Frame& frame() const { return frame_; }
    int frames() const { return frames_; }
    bool failed() const { return failed_; }
    std::vector<std::wstring>& ime_log() { return ime_log_; }
    std::vector<std::wstring>& trace() { return trace_; }  // verification mode: input messages as handled
    LRESULT message(UINT msg, WPARAM w, LPARAM l);

private:
    Ui& ui_;
    const Appearance& appearance_;
    std::function<layout::Node()> page_;
    ScrollState& scroll_;
    HWND window_ = nullptr;
    HIMC input_context_ = nullptr;
    bool ime_associated_ = true;
    Text text_;
    ShapedMeasure measure_{text_};
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<WindowsClipboard> clipboard_;
    Frame frame_;
    float forced_scale_ = 0;
    int frames_ = 0;
    bool failed_ = false;
    std::string reveal_;                        // focus target to scroll into view on the next arrange
    enum class Drag { none, text, thumb, slider } drag_ = Drag::none;
    int drag_origin_ = 0, drag_offset_ = 0;
    ScrollState* drag_scroll_ = nullptr;
    char16_t pending_high_ = 0;
    std::vector<std::wstring> ime_log_, trace_;
    bool tracing_ = false;

    Frame arrange(int width, int height, float scale);
    void release();
    void draw(Canvas& canvas, const Frame& frame, float scale, bool window_focused);
    void finish_composition();         // ask the IME to commit before focus moves
    void sync_ime();
    void follow_focus();               // notice focus moved by application or semantic calls
    std::string synced_focus_;
    void after_input(const std::string& focus_before);
    void log_ime(const wchar_t* event, std::u16string_view detail = {});
};
}
