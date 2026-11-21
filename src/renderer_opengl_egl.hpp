#pragma once
#include <EGL/egl.h>
#include "isoduo/desktop.hpp"

namespace isoduo::detail {
// The native window outlives Renderer. This object selects its EGL visual before
// the platform creates the window, and keeps the display alive across rebuilds.
class EglWindow {
public:
    EglWindow(EGLenum platform, void* nativeDisplay);
    ~EglWindow();
    EglWindow(const EglWindow&) = delete;
    EglWindow& operator=(const EglWindow&) = delete;

    int visualId() const;
    EGLDisplay display() const { return display_; }
    EGLConfig config() const { return config_; }
    EGLSurface createSurface(void* window) const;
    void nativeWindow(void* window) { window_ = window; }
    EGLSurface createSurface() const { return createSurface(window_); }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    void* window_ = nullptr;
};
}
