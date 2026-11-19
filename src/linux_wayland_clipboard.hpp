#pragma once
#include "linux_clipboard.hpp"
#include <wayland-client.h>
#include <poll.h>
#include <memory>
#include <vector>

namespace isoduo::detail {
class WaylandClipboard final : public AsyncClipboard {
public:
    WaylandClipboard(wl_display*, ClipboardSink);
    ~WaylandClipboard() override;
    void bind(wl_registry*, std::uint32_t name, std::uint32_t version);
    void seat(wl_seat*);
    void focused(bool);
    void serial(std::uint32_t);
    void preparePoll(std::vector<pollfd>&, int& timeout) const;
    void dispatchIo();
    bool hasText() const override;
    Result read(std::uint64_t request, bool rich) override;
    Result write(std::uint64_t request, ClipboardData data) override;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
