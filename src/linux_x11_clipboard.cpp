#include "linux_x11_clipboard.hpp"
#include "utf.hpp"
#include <X11/Xatom.h>
#include <X11/Xlib-xcb.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace isoduo::detail {
namespace {
constexpr std::size_t chunkSize = 64 * 1024;
template<class T> using XcbReply = std::unique_ptr<T, decltype(&std::free)>;
}

struct X11Clipboard::State {
    struct Contents {
        std::shared_ptr<const std::string> text, rich, latin;
    };
    struct Read {
        std::uint64_t request;
        Window window, owner;
        bool rich;
        ClipboardClock::time_point deadline;
        Atom target, richTarget = None;
        std::string bytes;
        std::u16string text;
        std::optional<std::size_t> incrementalMinimum = std::nullopt;
        bool fallback = false;
    };
    struct Write { std::uint64_t request; Contents contents; };
    struct Send {
        Atom type;
        std::shared_ptr<const std::string> bytes;
        std::size_t offset = 0;
        ClipboardClock::time_point deadline;
    };
    Display* display;
    xcb_connection_t* connection;
    ClipboardSink sink;
    Atom clipboard, targets, utf8, plain, plainUtf8, rich, legacy, richMime, legacyMime, incr, property, timestamp, multiple, atomPair;
    Atom manager, saveTargets, saveProperty, clockProperty;
    Window controlWindow = None, ownerWindow = None;
    Time changed = CurrentTime;
    Contents owned;
    std::optional<Read> incoming;
    std::optional<Write> outgoing;
    std::map<std::pair<Window, Atom>, Send> sends;
    std::map<Window, uint32_t> masks;
    ClipboardClock::time_point handoff;

    State(Display* display, ClipboardSink sink) : display(display), connection(XGetXCBConnection(display)),
        sink(std::move(sink)), clipboard(atom("CLIPBOARD")), targets(atom("TARGETS")), utf8(atom(x11Utf8Format)),
        plain(atom(plainClipboardFormat)), plainUtf8(atom(utf8ClipboardFormat)), rich(atom(richClipboardFormat)),
        legacy(atom(legacyClipboardFormat)), richMime(atom(richClipboardMime)), legacyMime(atom(legacyClipboardMime)),
        incr(atom("INCR")), property(atom("_ISODUO_SELECTION")),
        timestamp(atom("TIMESTAMP")), multiple(atom("MULTIPLE")), atomPair(atom("ATOM_PAIR")),
        manager(atom("CLIPBOARD_MANAGER")), saveTargets(atom("SAVE_TARGETS")), saveProperty(atom("_ISODUO_SAVE_TARGETS")),
        clockProperty(atom("_ISODUO_SELECTION_TIME")), controlWindow(helper()) {}
    ~State() {
        for (auto [window, mask] : masks) setMask(window, mask);
        if (incoming) XDestroyWindow(display, incoming->window);
        if (ownerWindow) XDestroyWindow(display, ownerWindow);
        if (controlWindow) XDestroyWindow(display, controlWindow);
    }
    Atom atom(const char* name) const { return XInternAtom(display, name, False); }
    bool checked(xcb_void_cookie_t cookie) const {
        XcbReply<xcb_generic_error_t> error(xcb_request_check(connection, cookie), std::free);
        return !error && !xcb_connection_has_error(connection);
    }
    bool setMask(Window window, uint32_t mask) const {
        return checked(xcb_change_window_attributes_checked(connection, window, XCB_CW_EVENT_MASK, &mask));
    }
    Window helper() const {
        const auto window = xcb_generate_id(connection);
        const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        if (!checked(xcb_create_window_checked(connection, XCB_COPY_FROM_PARENT, window, DefaultRootWindow(display),
            0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask))) return None;
        return window;
    }
    XcbReply<xcb_get_property_reply_t> get(Window window, Atom key, bool remove = false,
                                         std::size_t limit = clipboardLimit) const {
        auto request = xcb_get_property(connection, remove, window, key, XCB_GET_PROPERTY_TYPE_ANY, 0, limit / 4);
        XcbReply<xcb_get_property_reply_t> reply(xcb_get_property_reply(connection, request, nullptr), std::free);
        if (reply && (reply->bytes_after || reply->type == None)) reply.reset();
        return reply;
    }
    bool put(Window window, Atom key, Atom type, int format, std::size_t size, const void* data) const {
        return checked(xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, key, type, format, size, data));
    }
    void convert(Atom target) {
        incoming->target = target;
        incoming->incrementalMinimum.reset();
        incoming->bytes.clear();
        XConvertSelection(display, clipboard, target, property, incoming->window, CurrentTime);
        XFlush(display);
    }
    void completeRead(Result result, ClipboardData data = {}) {
        auto id = incoming->request;
        XDestroyWindow(display, incoming->window);
        incoming.reset();
        sink({id, std::move(result), std::move(data)});
    }
    void finishPart() {
        auto& read = *incoming;
        if (XGetSelectionOwner(display, clipboard) != read.owner) return completeRead(clipboardFailure());
        if (read.target == read.richTarget)
            return completeRead({}, {std::move(read.text), std::move(read.bytes)});
        if (read.target == XA_STRING) {
            for (unsigned char ch : read.bytes) read.text += static_cast<char16_t>(ch);
        } else {
            try { read.text = utf16FromUtf8(read.bytes); }
            catch (const std::range_error&) { return completeRead(clipboardFailure()); }
        }
        if (read.richTarget) return convert(read.richTarget);
        completeRead({}, {std::move(read.text), {}});
    }
    void conversionFailed() {
        if (incoming->richTarget && incoming->target == incoming->richTarget &&
            XGetSelectionOwner(display, clipboard) == incoming->owner)
            return completeRead({}, {std::move(incoming->text), {}});
        completeRead(clipboardFailure());
    }
    void receiveTargets(const xcb_get_property_reply_t& reply) {
        if (reply.type != XA_ATOM || reply.format != 32) return conversionFailed();
        const auto* atoms = static_cast<const xcb_atom_t*>(xcb_get_property_value(&reply));
        const std::span formats(atoms, reply.value_len);
        auto has = [&](Atom atom) { return std::ranges::find(formats, atom) != formats.end(); };
        if (incoming->rich) {
            for (auto candidate : {richMime, rich, legacyMime, legacy}) {
                if (has(candidate)) { incoming->richTarget = candidate; break; }
            }
        }
        for (auto text : {utf8, plainUtf8, plain, static_cast<Atom>(XA_STRING)}) {
            if (has(text)) return convert(text);
        }
        completeRead(noClipboardText());
    }
    void receiveBytes(const xcb_get_property_reply_t& reply) {
        if (reply.type != incoming->target || reply.format != 8 ||
            incoming->bytes.size() + reply.value_len > clipboardLimit) return conversionFailed();
        if (incoming->incrementalMinimum) {
            if (reply.value_len) {
                incoming->bytes.append(static_cast<const char*>(xcb_get_property_value(&reply)), reply.value_len);
                return;
            }
            // INCR announces a lower bound, so the terminating empty property cannot arrive before it.
            if (incoming->bytes.size() < *incoming->incrementalMinimum) return conversionFailed();
        } else if (reply.value_len) {
            incoming->bytes.assign(static_cast<const char*>(xcb_get_property_value(&reply)), reply.value_len);
        }
        finishPart();
    }
    void receiveNotify(const XSelectionEvent& event) {
        if (event.target != incoming->target || incoming->incrementalMinimum) return;
        if (XGetSelectionOwner(display, clipboard) != incoming->owner) return completeRead(clipboardFailure());
        if (event.property != property) {
            // Older owners may implement conversion without TARGETS.
            if (incoming->target == targets) {
                incoming->fallback = true;
                return convert(utf8);
            }
            if (incoming->fallback && incoming->target == utf8) return convert(XA_STRING);
            return conversionFailed();
        }
        auto reply = get(event.requestor, property, true, incoming->target == targets ? chunkSize : clipboardLimit);
        if (!reply) return conversionFailed();
        if (incoming->target == targets) return receiveTargets(*reply);
        if (reply->type == incr && reply->format == 32 && reply->value_len == 1) {
            const auto minimum = *static_cast<const uint32_t*>(xcb_get_property_value(reply.get()));
            if (minimum > clipboardLimit) return conversionFailed();
            incoming->incrementalMinimum = minimum;
            return;
        }
        receiveBytes(*reply);
    }
    void publish(Time time) {
        auto publication = std::move(*outgoing);
        outgoing.reset();
        // Each publication has its own owner even within the same process.
        // A pending read can then detect replacement between format conversions.
        auto window = helper();
        if (window) XSetSelectionOwner(display, clipboard, window, time);
        if (!window || XGetSelectionOwner(display, clipboard) != window) {
            if (window) XDestroyWindow(display, window);
            sink({publication.request, clipboardFailure(), {}});
            return;
        }
        if (ownerWindow) XDestroyWindow(display, ownerWindow);
        ownerWindow = window;
        changed = time;
        owned = std::move(publication.contents);
        sink({publication.request, {}, {}});
    }
    std::vector<xcb_atom_t> formats(bool metadata) const {
        std::vector<xcb_atom_t> result;
        if (metadata) result = {static_cast<xcb_atom_t>(targets), static_cast<xcb_atom_t>(timestamp), static_cast<xcb_atom_t>(multiple)};
        for (auto format : {utf8, plainUtf8, plain}) result.push_back(format);
        if (owned.latin) result.push_back(XA_STRING);
        if (owned.rich) { result.push_back(rich); result.push_back(richMime); }
        return result;
    }
    bool watch(Window window) {
        if (masks.contains(window)) return true;
        auto request = xcb_get_window_attributes(connection, window);
        XcbReply<xcb_get_window_attributes_reply_t> attr(xcb_get_window_attributes_reply(connection, request, nullptr), std::free);
        if (!attr || !setMask(window, attr->your_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE)) return false;
        masks.emplace(window, attr->your_event_mask);
        return true;
    }
    void unwatch(Window window) {
        auto mask = masks.find(window);
        if (mask == masks.end()) return;
        if (std::ranges::any_of(sends, [window](const auto& entry) { return entry.first.first == window; })) return;
        setMask(window, mask->second);
        masks.erase(mask);
    }
    bool supply(Window window, Atom target, Atom key) {
        if (sends.contains({window, key})) return false;
        if (target == targets) {
            auto values = formats(true);
            return put(window, key, XA_ATOM, 32, values.size(), values.data());
        }
        if (target == timestamp) {
            const auto time = static_cast<uint32_t>(changed);
            return put(window, key, XA_INTEGER, 32, 1, &time);
        }
        auto bytes = target == utf8 || target == plainUtf8 || target == plain ? owned.text :
                     target == rich || target == richMime ? owned.rich : target == XA_STRING ? owned.latin : nullptr;
        if (!bytes) return false;
        if (bytes->size() <= chunkSize) return put(window, key, target, 8, bytes->size(), bytes->data());
        if (sends.size() >= 32 || !watch(window)) return false;
        const auto size = static_cast<uint32_t>(bytes->size());
        if (!put(window, key, incr, 32, 1, &size)) { unwatch(window); return false; }
        sends.emplace(std::pair{window, key}, Send{target, std::move(bytes), 0, ClipboardClock::now() + clipboardWait});
        return true;
    }
    bool supplyMultiple(Window window, Atom key) {
        auto reply = get(window, key, false, chunkSize);
        if (!reply || reply->format != 32 || (reply->type != atomPair && reply->type != XA_ATOM) || reply->value_len % 2)
            return false;
        auto* pairs = static_cast<xcb_atom_t*>(xcb_get_property_value(reply.get()));
        for (unsigned i = 0; i < reply->value_len; i += 2) {
            auto target = pairs[i], destination = pairs[i + 1];
            if (!destination || destination == key || !supply(window, target, destination)) pairs[i + 1] = None;
        }
        return put(window, key, atomPair, 32, reply->value_len, pairs);
    }
    void respond(const XSelectionRequestEvent& request) {
        const Atom key = request.property ? request.property : request.target;
        const bool supplied = owned.text && request.selection == clipboard &&
            (request.target == multiple ? request.property && supplyMultiple(request.requestor, key)
                                        : supply(request.requestor, request.target, key));
        xcb_selection_notify_event_t reply{};
        reply.response_type = XCB_SELECTION_NOTIFY;
        reply.time = request.time;
        reply.requestor = request.requestor;
        reply.selection = request.selection;
        reply.target = request.target;
        reply.property = supplied ? key : None;
        // SendEvent reads the full 32-byte wire event.
        char wire[32]{};
        static_assert(sizeof(reply) <= sizeof(wire));
        std::memcpy(wire, &reply, sizeof(reply));
        checked(xcb_send_event_checked(connection, false, request.requestor, XCB_EVENT_MASK_NO_EVENT, wire));
    }
    bool propertyEvent(const XPropertyEvent& event) {
        if (event.window == controlWindow && event.atom == clockProperty) {
            if (outgoing && event.state == PropertyNewValue) publish(event.time);
            return true;
        }
        if (incoming && event.window == incoming->window) {
            if (event.atom == property && event.state == PropertyNewValue && incoming->incrementalMinimum) {
                auto reply = get(event.window, property, true);
                if (reply) receiveBytes(*reply);
                else conversionFailed();
            }
            return true;
        }
        auto transfer = sends.find({event.window, event.atom});
        if (transfer == sends.end() || event.state != PropertyDelete) return false;
        auto& send = transfer->second;
        auto chunk = std::string_view(*send.bytes).substr(send.offset, chunkSize);
        const bool written = put(event.window, event.atom, send.type, 8, chunk.size(), chunk.data());
        if (!written || chunk.empty()) {
            sends.erase(transfer);
            unwatch(event.window);
        } else send.offset += chunk.size();
        return true;
    }
};

X11Clipboard::X11Clipboard(Display* display, ClipboardSink sink) : state_(std::make_unique<State>(display, std::move(sink))) {}
X11Clipboard::~X11Clipboard() = default;
bool X11Clipboard::hasText() const { return XGetSelectionOwner(state_->display, state_->clipboard) != None; }
Result X11Clipboard::read(std::uint64_t request, bool rich) {
    auto& s = *state_;
    auto owner = XGetSelectionOwner(s.display, s.clipboard);
    if (!owner) return noClipboardText();
    if (s.incoming) {
        XDestroyWindow(s.display, s.incoming->window);
        s.incoming.reset();
    }
    auto window = s.helper();
    if (!window) return clipboardFailure();
    s.incoming = State::Read{.request = request, .window = window, .owner = owner, .rich = rich,
                             .deadline = ClipboardClock::now() + clipboardWait, .target = s.targets, .bytes = {}, .text = {}};
    s.convert(s.targets);
    return {};
}
Result X11Clipboard::write(std::uint64_t request, ClipboardData data) {
    auto& s = *state_;
    if (!s.controlWindow) return clipboardFailure();
    auto text = utf8FromUtf16(data.text);
    if (text.size() > clipboardLimit || data.rich.size() > clipboardLimit) return clipboardFailure();
    State::Contents contents{std::make_shared<std::string>(std::move(text)), {}, {}};
    if (!data.rich.empty()) contents.rich = std::make_shared<std::string>(std::move(data.rich));
    if (std::ranges::all_of(data.text, [](char16_t c) { return c <= 0xff; }))
        contents.latin = std::make_shared<std::string>(data.text.begin(), data.text.end());
    // A property event supplies fresh server time even for menu or application commands.
    if (!s.put(s.controlWindow, s.clockProperty, XA_INTEGER, 8, 0, nullptr)) return clipboardFailure();
    s.outgoing = State::Write{request, std::move(contents)};
    return {};
}
bool X11Clipboard::dispatch(const XEvent& event) {
    auto& s = *state_;
    switch (event.type) {
    case PropertyNotify: return s.propertyEvent(event.xproperty);
    case SelectionNotify:
        if (s.incoming && event.xselection.requestor == s.incoming->window && event.xselection.selection == s.clipboard)
            s.receiveNotify(event.xselection);
        if (event.xselection.requestor == s.controlWindow && event.xselection.selection == s.manager &&
            event.xselection.target == s.saveTargets) s.handoff = {};
        return true;
    case SelectionRequest:
        if (event.xselectionrequest.owner == s.ownerWindow) s.respond(event.xselectionrequest);
        return true;
    case SelectionClear:
        if (event.xselectionclear.window == s.ownerWindow && event.xselectionclear.selection == s.clipboard &&
            XGetSelectionOwner(s.display, s.clipboard) != s.ownerWindow) s.owned = {};
        return true;
    default: return false;
    }
}
void X11Clipboard::limitWait(int& timeout) const {
    if (state_->incoming) limitClipboardWait(timeout, state_->incoming->deadline);
    for (const auto& [key, send] : state_->sends) limitClipboardWait(timeout, send.deadline);
    limitClipboardWait(timeout, state_->handoff);
}
void X11Clipboard::expire() {
    auto& s = *state_;
    const auto now = ClipboardClock::now();
    if (s.incoming && now >= s.incoming->deadline) s.conversionFailed();
    if (now >= s.handoff) s.handoff = {};
    std::erase_if(s.sends, [now](const auto& entry) { return now >= entry.second.deadline; });
    for (auto it = s.masks.begin(); it != s.masks.end();) s.unwatch((it++)->first);
}
bool X11Clipboard::beginHandoff() {
    auto& s = *state_;
    s.outgoing.reset();
    if (!s.owned.text || XGetSelectionOwner(s.display, s.clipboard) != s.ownerWindow ||
        XGetSelectionOwner(s.display, s.manager) == None) return false;
    auto values = s.formats(false);
    if (!s.put(s.controlWindow, s.saveProperty, XA_ATOM, 32, values.size(), values.data())) return false;
    s.handoff = ClipboardClock::now() + std::chrono::seconds(1);
    XConvertSelection(s.display, s.manager, s.saveTargets, s.saveProperty, s.controlWindow, CurrentTime);
    XFlush(s.display);
    return true;
}
bool X11Clipboard::handingOff() const { return state_->handoff != ClipboardClock::time_point{}; }
}
