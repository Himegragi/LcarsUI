#pragma once
#include "isoduo/core.hpp"

namespace isoduo::detail {
struct ClipboardData {
    std::u16string text;  // core writes LF line breaks; a host offers them in its platform's convention
    std::string rich;
};
struct ClipboardDelivery {
    std::uint64_t request;
    Result result;
    ClipboardData data;
};

// UI-thread host seam. A successful request admits work. Delivery is separate
// and must be processed on the UI thread in order with native input.
class AsyncClipboard {
public:
    virtual ~AsyncClipboard() = default;
    virtual bool hasText() const = 0;
    virtual Result read(std::uint64_t request, bool rich) = 0;
    virtual Result write(std::uint64_t request, ClipboardData data) = 0;
};
}
