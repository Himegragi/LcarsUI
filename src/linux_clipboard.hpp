#pragma once
#include "async_clipboard.hpp"
#include <algorithm>
#include <chrono>
#include <functional>

namespace isoduo::detail {
using ClipboardSink = std::function<void(ClipboardDelivery)>;
using ClipboardClock = std::chrono::steady_clock;
inline constexpr auto clipboardWait = std::chrono::seconds(5);
inline constexpr std::size_t clipboardLimit = 16 * 1024 * 1024;
inline constexpr char richClipboardFormat[] = "Isoduo.RichText.1";
inline constexpr char legacyClipboardFormat[] = "LcarsUI.RichText.1";
inline constexpr char richClipboardMime[] = "application/x-isoduo-rich-text;version=1";
inline constexpr char legacyClipboardMime[] = "application/x-lcarsui-rich-text;version=1";
inline constexpr char utf8ClipboardFormat[] = "text/plain;charset=utf-8";
inline constexpr char plainClipboardFormat[] = "text/plain";
inline constexpr char x11Utf8Format[] = "UTF8_STRING";

inline void limitClipboardWait(int& timeout, ClipboardClock::time_point deadline) {
    if (deadline == ClipboardClock::time_point{}) return;
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - ClipboardClock::now()).count();
    const auto wait = static_cast<int>(std::max<decltype(remaining)>(0, remaining));
    timeout = timeout < 0 ? wait : std::min(timeout, wait);
}
inline Result clipboardFailure() { return {Status::failed, u"Clipboard transfer failed"}; }
inline Result noClipboardText() { return {Status::notSupported, u"Clipboard has no text"}; }
}
