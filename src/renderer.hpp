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
    void draw(const DrawList& list, int width, int height);
    Image read_back(const DrawList& list, int width, int height);
    std::string name() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
