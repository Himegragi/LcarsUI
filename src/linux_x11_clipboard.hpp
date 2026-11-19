#pragma once
#include "linux_clipboard.hpp"
#include <X11/Xlib.h>
#include <memory>

namespace isoduo::detail {
class X11Clipboard final : public AsyncClipboard {
public:
    X11Clipboard(Display*, ClipboardSink);
    ~X11Clipboard() override;
    bool dispatch(const XEvent&);
    void limitWait(int&) const;
    void expire();
    bool beginHandoff();
    bool handingOff() const;
    bool hasText() const override;
    Result read(std::uint64_t request, bool rich) override;
    Result write(std::uint64_t request, ClipboardData data) override;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
