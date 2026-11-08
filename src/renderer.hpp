#pragma once
#include <windows.h>
#include "render.hpp"
#include <filesystem>
#include <chrono>
#include <memory>
namespace lcars::detail {
class Renderer {
public:
    explicit Renderer(HWND window);
    ~Renderer();
    double draw(const DrawList& list, int width, int height, bool measure = false, bool offscreen = false, double* submission_ms = nullptr, std::chrono::steady_clock::time_point* presented = nullptr);
    Image read_back(const DrawList& list, int width, int height);
    std::string name() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
