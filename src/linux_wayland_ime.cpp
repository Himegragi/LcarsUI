#include "linux_wayland_ime.hpp"
#include "text-input-v3-client-protocol.h"
#include "utf.hpp"
#include <glib.h>
#include <algorithm>
#include <utility>

namespace isoduo::detail {
WaylandIme::WaylandIme(std::function<void(LinuxImeEdit)> deliver) : deliver_(std::move(deliver)) {}
WaylandIme::~WaylandIme() {
    if (input_) zwp_text_input_v3_destroy(input_);
    if (manager_) zwp_text_input_manager_v3_destroy(manager_);
}
void WaylandIme::bind(wl_registry* registry, std::uint32_t name) {
    if (manager_) return;
    manager_ = static_cast<zwp_text_input_manager_v3*>(
        wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1));
    createInput();
}
void WaylandIme::seat(wl_seat* seat) {
    seat_ = seat;
    createInput();
}
void WaylandIme::createInput() {
    if (!manager_ || !seat_ || input_) return;
    input_ = zwp_text_input_manager_v3_get_text_input(manager_, seat_);
    if (input_) zwp_text_input_v3_add_listener(input_, &listener_, this);
}
void WaylandIme::commit() {
    zwp_text_input_v3_commit(input_);
    ++serial_;
}
bool WaylandIme::refreshCaret() {
    if (!entered_ || !target_ || !caret_) return false;
    caret_.reset();
    return true;
}
void WaylandIme::update(const std::optional<LinuxImeState>& state, Rect surfaceCaret, bool externalChange) {
    if (!entered_) return;
    const auto next = state ? state->target : 0;
    const bool changed = target_ != next;
    if (changed) {
        if (target_) {
            zwp_text_input_v3_disable(input_);
            commit();
        }
        target_ = next;
        pending_ = {};
        caret_.reset();
        surrounding_.reset();
        if (!state) return;
        zwp_text_input_v3_enable(input_);
        zwp_text_input_v3_set_content_type(input_,
            state->multiline ? ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE : ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
            ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    }
    if (!state) return;
    const bool moved = caret_ != surfaceCaret;
    if (moved) {
        zwp_text_input_v3_set_cursor_rectangle(input_, surfaceCaret.x, surfaceCaret.y, surfaceCaret.w, surfaceCaret.h);
        caret_ = surfaceCaret;
    }
    // A protocol message is limited to 4000 bytes. Keep the full selection where it fits.
    const auto text = utf8FromUtf16(state->text);
    const std::u16string_view utf16 = state->text;
    const auto anchor = utf8FromUtf16(utf16.substr(0, state->anchor)).size();
    const auto cursor = utf8FromUtf16(utf16.substr(0, state->cursor)).size();
    const auto first = std::min(anchor, cursor), last = std::max(anchor, cursor);
    constexpr std::size_t limit = 4000;
    bool surroundingChanged = false;
    if (last - first <= limit) {
        auto start = first > (limit - (last - first)) / 2 ? first - (limit - (last - first)) / 2 : 0;
        if (text.size() - start < limit) start = text.size() > limit ? text.size() - limit : 0;
        while (start < first && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) ++start;
        auto end = std::min(text.size(), start + limit);
        while (end > last && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        Surrounding surrounding{text.substr(start, end - start), static_cast<int>(anchor - start),
                                static_cast<int>(cursor - start)};
        surroundingChanged = surrounding_ != surrounding;
        if (surroundingChanged) {
            if (externalChange && !changed)
                zwp_text_input_v3_set_text_change_cause(input_, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
            zwp_text_input_v3_set_surrounding_text(input_, surrounding.text.c_str(), surrounding.cursor, surrounding.anchor);
            surrounding_ = std::move(surrounding);
        }
    } else if (surrounding_) {
        // Clear an earlier snapshot when the complete selection cannot be represented.
        zwp_text_input_v3_set_surrounding_text(input_, "", 0, 0);
        surrounding_.reset();
        surroundingChanged = true;
    }
    if (changed || moved || surroundingChanged) commit();
    if (changed) enabledAt_ = serial_;
}
void WaylandIme::done(std::uint32_t serial) {
    auto edit = std::exchange(pending_, {});
    // Older geometry updates still belong to the same editor. A batch from before
    // enable belongs to a different editor and must never land in the current one.
    if (!entered_ || !target_ || (serial - enabledAt_) >= (std::uint32_t{1} << 31)) return;
    if ((edit.deleteBefore || edit.deleteAfter) && (serial != serial_ || !surrounding_)) return;
    // Mutter also acknowledges client state with a bare done event.
    if (!edit.replacePreedit && edit.commit.empty() && !edit.deleteBefore && !edit.deleteAfter) return;
    edit.target = target_;
    deliver_(std::move(edit));
}
const zwp_text_input_v3_listener WaylandIme::listener_{
    [](void* data, zwp_text_input_v3*, wl_surface* surface) {
        auto& ime = *static_cast<WaylandIme*>(data);
        ime.entered_ = surface == ime.surface_;
    },
    [](void* data, zwp_text_input_v3*, wl_surface*) {
        auto& ime = *static_cast<WaylandIme*>(data);
        ime.entered_ = false;
        ime.target_ = 0;
        ime.pending_ = {};
    },
    [](void* data, zwp_text_input_v3*, const char* text, std::int32_t begin, std::int32_t end) {
        auto& pending = static_cast<WaylandIme*>(data)->pending_;
        const std::string_view value = text ? text : "";
        const auto nativeCursor = end >= 0 ? end : begin;
        if (!g_utf8_validate(value.data(), value.size(), nullptr)) return;
        auto byte = nativeCursor < 0 ? value.size() : std::min(value.size(), static_cast<std::size_t>(nativeCursor));
        while (byte < value.size() && (static_cast<unsigned char>(value[byte]) & 0xc0) == 0x80) --byte;
        pending.preedit = utf16FromUtf8(value);
        pending.cursor = static_cast<unsigned>(utf16FromUtf8(value.substr(0, byte)).size());
        pending.replacePreedit = true;
    },
    [](void* data, zwp_text_input_v3*, const char* text) {
        if (!text || g_utf8_validate(text, -1, nullptr))
            static_cast<WaylandIme*>(data)->pending_.commit = utf16FromUtf8(text ? text : "");
    },
    [](void* data, zwp_text_input_v3*, std::uint32_t before, std::uint32_t after) {
        auto& pending = static_cast<WaylandIme*>(data)->pending_;
        pending.deleteBefore = before;
        pending.deleteAfter = after;
    },
    [](void* data, zwp_text_input_v3*, std::uint32_t serial) { static_cast<WaylandIme*>(data)->done(serial); }
};
}
