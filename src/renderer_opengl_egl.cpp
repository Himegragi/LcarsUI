#include "renderer_opengl_egl.hpp"
#include "renderer.hpp"
#include "utf.hpp"
#include <charconv>

namespace isoduo::detail {
namespace {
std::u16string eglError(std::u16string message) {
    char code[16]{};
    auto result = std::to_chars(code, code + sizeof(code), eglGetError(), 16);
    return message + u" (EGL 0x" + utf16FromUtf8({code, static_cast<std::size_t>(result.ptr - code)}) + u")";
}
}

EglWindow::EglWindow(EGLenum platform, void* nativeDisplay) {
    display_ = eglGetPlatformDisplay(platform, nativeDisplay, nullptr);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr))
        throw StartupError{{StartupFailureKind::contextCreation, {}, {}, eglError(u"Unable to initialize native EGL display")}};
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display_, attributes, &config_, 1, &count) || !count) {
        auto message = eglError(u"No EGL window configuration for OpenGL");
        eglTerminate(display_);
        throw StartupError{{StartupFailureKind::contextCreation, {}, {}, std::move(message)}};
    }
}
EglWindow::~EglWindow() { eglTerminate(display_); }
int EglWindow::visualId() const {
    EGLint id = 0;
    eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &id);
    return id;
}
EGLSurface EglWindow::createSurface(void* window) const {
    auto surface = eglCreatePlatformWindowSurface(display_, config_, window, nullptr);
    if (surface == EGL_NO_SURFACE)
        throw StartupError{{StartupFailureKind::resource, {}, {}, eglError(u"Unable to create native EGL window surface")}};
    return surface;
}
}
