#pragma once
#include "linux_ime.hpp"
#include <functional>

struct wl_registry;
struct wl_seat;
struct wl_surface;
struct zwp_text_input_manager_v3;
struct zwp_text_input_v3;
struct zwp_text_input_v3_listener;

namespace isoduo::detail {
class WaylandIme {
public:
    explicit WaylandIme(std::function<void(LinuxImeEdit)> deliver);
    ~WaylandIme();
    void bind(wl_registry*, std::uint32_t name);
    void seat(wl_seat*);
    void surface(wl_surface* surface) { surface_ = surface; }
    void update(const std::optional<LinuxImeState>&, Rect surfaceCaret, bool externalChange);
    bool refreshCaret();

private:
    void createInput();
    void commit();
    void done(std::uint32_t serial);
    static const zwp_text_input_v3_listener listener_;

    struct Surrounding {
        std::string text;
        int anchor, cursor;
        bool operator==(const Surrounding&) const = default;
    };
    std::function<void(LinuxImeEdit)> deliver_;
    zwp_text_input_manager_v3* manager_ = nullptr;
    zwp_text_input_v3* input_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_surface* surface_ = nullptr;
    bool entered_ = false;
    std::uint64_t target_ = 0;
    std::uint32_t serial_ = 0, enabledAt_ = 0, surroundingAt_ = 0;
    std::optional<Surrounding> surrounding_;
    std::optional<Rect> caret_;
    LinuxImeEdit pending_;
};
}
