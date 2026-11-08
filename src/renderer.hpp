#pragma once
#include <windows.h>
#include "render.hpp"
#include <filesystem>
#include <memory>
namespace lcars::detail {
class Renderer {
public:
    explicit Renderer(HWND window);
    ~Renderer();
    void draw(const Image& canvas);
    void capture(const Image& canvas,const std::filesystem::path& path);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
