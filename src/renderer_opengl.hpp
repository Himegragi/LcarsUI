#pragma once
#include <windows.h>
#include "text.hpp"
#include <filesystem>
#include <memory>
namespace lcars::detail {
class Renderer {
public:
    explicit Renderer(HWND window);
    ~Renderer();
    void draw(const Canvas& canvas);
    void capture(const Canvas& canvas,const std::filesystem::path& path);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
