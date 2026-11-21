#include "linux_wayland_clipboard.hpp"
#include "utf.hpp"
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <optional>
#include <pthread.h>
#include <set>
#include <signal.h>
#include <unistd.h>
#include <utility>

namespace isoduo::detail {
namespace {
class File {
public:
    File() = default;
    explicit File(int fd) : fd_(fd) {}
    ~File() { if (fd_ >= 0) close(fd_); }
    File(File&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    File& operator=(File&& other) noexcept { std::swap(fd_, other.fd_); return *this; }
    int get() const { return fd_; }
    bool nonblocking() const {
        int flags = fcntl(fd_, F_GETFL);
        return flags >= 0 && fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == 0;
    }
private:
    int fd_ = -1;
};

// Peers can close the pipe independently. Consume only a SIGPIPE generated
// by this write, leaving the process and the application's signal policy alone.
ssize_t writePipe(int fd, std::string_view bytes) {
    sigset_t pipe, previous, pending{};
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    if (const int error = pthread_sigmask(SIG_BLOCK, &pipe, &previous)) { errno = error; return -1; }
    sigpending(&pending);
    const auto count = ::write(fd, bytes.data(), bytes.size());
    const int error = errno;
    if (count < 0 && error == EPIPE && !sigismember(&pending, SIGPIPE)) {
        timespec immediate{};
        while (sigtimedwait(&pipe, nullptr, &immediate) < 0 && errno == EINTR) {}
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = error;
    return count;
}
std::atomic<std::uint64_t> sourceSequence{0};
}

struct WaylandClipboard::State {
    struct Input {
        File file;
        std::string bytes;
        bool failed = false;
        bool active() const { return file.get() >= 0; }
        void readSome() {
            if (!active()) return;
            char chunk[16384];
            const auto count = ::read(file.get(), chunk, sizeof(chunk));
            if (count > 0 && bytes.size() + count <= clipboardLimit) {
                bytes.append(chunk, count);
                return;
            }
            if (count < 0 && (errno == EAGAIN || errno == EINTR)) return;
            failed = count != 0;
            file = {};
        }
    };
    struct Read {
        std::uint64_t request;
        ClipboardClock::time_point deadline;
        Input text, rich;
    };
    struct Source {
        std::string marker;
        std::shared_ptr<const std::string> text, rich;
    };
    struct Write {
        std::uint64_t request;
        wl_data_source* source;
        ClipboardClock::time_point deadline;
    };
    struct Send {
        File file;
        std::shared_ptr<const std::string> bytes;
        std::size_t offset = 0;
        ClipboardClock::time_point deadline;
        bool advance() {
            if (ClipboardClock::now() >= deadline) return true;
            auto remaining = std::string_view(*bytes).substr(offset, 16384);
            if (remaining.empty()) return true;
            const auto count = writePipe(file.get(), remaining);
            if (count < 0) return errno != EAGAIN && errno != EINTR;
            offset += count;
            return offset == bytes->size();
        }
    };
    wl_display* display;
    ClipboardSink sink;
    wl_seat* seat = nullptr;
    wl_data_device_manager* manager = nullptr;
    wl_data_device* device = nullptr;
    wl_data_offer* selection = nullptr;
    wl_data_offer* drag = nullptr;
    std::map<wl_data_offer*, std::set<std::string, std::less<>>> offers;
    std::map<wl_data_source*, Source> sources;
    std::vector<Send> sends;
    std::optional<Read> incoming;
    std::optional<Write> outgoing;
    bool focused = false;
    std::optional<std::uint32_t> serial;

    State(wl_display* display, ClipboardSink sink) : display(display), sink(std::move(sink)) {}
    ~State() {
        for (auto& [source, data] : sources) wl_data_source_destroy(source);
        for (auto& [offer, formats] : offers) wl_data_offer_destroy(offer);
        if (device && wl_data_device_get_version(device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
            wl_data_device_release(device);
        else if (device) wl_data_device_destroy(device);
        if (manager) wl_data_device_manager_destroy(manager);
    }
    const char* textFormat() const {
        for (auto* format : {utf8ClipboardFormat, x11Utf8Format, plainClipboardFormat})
            if (has(format)) return format;
        return nullptr;
    }
    bool has(std::string_view format) const {
        auto offer = offers.find(selection);
        return offer != offers.end() && offer->second.contains(format);
    }
    void dropOffer(wl_data_offer*& offer) {
        if (!offer) return;
        offers.erase(offer);
        wl_data_offer_destroy(std::exchange(offer, nullptr));
    }
    Input receive(const char* format) const {
        if (!format) return {};
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) < 0) return {{}, {}, true};
        File reader(fds[0]), writer(fds[1]);
        if (!reader.nonblocking()) return {{}, {}, true};
        wl_data_offer_receive(selection, format, writer.get());
        return {std::move(reader), {}, false};
    }
    void completeRead(Result result, ClipboardData data = {}) {
        const auto request = incoming->request;
        incoming.reset();
        sink({request, std::move(result), std::move(data)});
    }
    void completeWrite(Result result) {
        const auto request = outgoing->request;
        outgoing.reset();
        sink({request, std::move(result), {}});
    }
    void updateSelection(wl_data_offer* offer) {
        if (selection != offer) {
            dropOffer(selection);
            selection = offer;
            if (incoming) completeRead(clipboardFailure());
        }
        // set_selection has no reply. The focused client's selection offer
        // carries this source's marker only after the compositor accepts it.
        if (outgoing && has(sources.at(outgoing->source).marker)) completeWrite({});
    }
    void dispatchIo() {
        std::erase_if(sends, [](auto& send) { return send.advance(); });
        const auto now = ClipboardClock::now();
        if (outgoing && now >= outgoing->deadline) {
            auto* source = outgoing->source;
            completeWrite(clipboardFailure());
            wl_data_source_destroy(source);
            sources.erase(source);
        }
        if (!incoming) return;
        incoming->text.readSome();
        incoming->rich.readSome();
        if (incoming->text.failed || (incoming->text.active() && now >= incoming->deadline))
            return completeRead(clipboardFailure());
        if (incoming->text.active() || (incoming->rich.active() && now < incoming->deadline)) return;
        std::u16string text;
        try { text = utf16FromUtf8(incoming->text.bytes); }
        catch (const std::range_error&) { return completeRead(clipboardFailure()); }
        std::string rich;
        if (!incoming->rich.active() && !incoming->rich.failed) rich = std::move(incoming->rich.bytes);
        completeRead({}, {std::move(text), std::move(rich)});
    }
    void attach() {
        if (device || !manager || !seat) return;
        device = wl_data_device_manager_get_data_device(manager, seat);
        if (device) wl_data_device_add_listener(device, &deviceListener, this);
    }
    static State& self(void* data) { return *static_cast<State*>(data); }
    static const wl_data_device_listener deviceListener;
    static const wl_data_offer_listener offerListener;
    static const wl_data_source_listener sourceListener;
};

const wl_data_offer_listener WaylandClipboard::State::offerListener{
    [](void* data, wl_data_offer* offer, const char* type) { self(data).offers.at(offer).emplace(type); },
    [](void*, wl_data_offer*, std::uint32_t) {},
    [](void*, wl_data_offer*, std::uint32_t) {}
};
const wl_data_device_listener WaylandClipboard::State::deviceListener{
    [](void* data, wl_data_device*, wl_data_offer* offer) {
        auto& state = self(data);
        state.offers.try_emplace(offer);
        wl_data_offer_add_listener(offer, &offerListener, data);
    },
    [](void* data, wl_data_device*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
        auto& state = self(data);
        state.dropOffer(state.drag);
        state.drag = offer;
    },
    [](void* data, wl_data_device*) { auto& state = self(data); state.dropOffer(state.drag); },
    [](void*, wl_data_device*, std::uint32_t, wl_fixed_t, wl_fixed_t) {},
    [](void* data, wl_data_device*) { auto& state = self(data); state.dropOffer(state.drag); },
    [](void* data, wl_data_device*, wl_data_offer* offer) { self(data).updateSelection(offer); }
};
const wl_data_source_listener WaylandClipboard::State::sourceListener{
    [](void*, wl_data_source*, const char*) {},
    [](void* data, wl_data_source* source, const char* mime, int fd) {
        auto& state = self(data);
        auto& contents = state.sources.at(source);
        File file(fd);
        const std::string_view type(mime);
        auto bytes = type == richClipboardFormat || type == richClipboardMime ? contents.rich :
            type == utf8ClipboardFormat || type == plainClipboardFormat || type == x11Utf8Format ? contents.text : nullptr;
        if (!bytes || state.sends.size() >= 32 || !file.nonblocking()) return;
        state.sends.push_back({std::move(file), std::move(bytes), 0, ClipboardClock::now() + clipboardWait});
    },
    [](void* data, wl_data_source* source) {
        auto& state = self(data);
        if (state.outgoing && state.outgoing->source == source) state.completeWrite(clipboardFailure());
        wl_data_source_destroy(source);
        state.sources.erase(source);
    },
    [](void*, wl_data_source*) {},
    [](void*, wl_data_source*) {},
    [](void*, wl_data_source*, std::uint32_t) {}
};

WaylandClipboard::WaylandClipboard(wl_display* display, ClipboardSink sink)
    : state_(std::make_unique<State>(display, std::move(sink))) {}
WaylandClipboard::~WaylandClipboard() = default;
void WaylandClipboard::bind(wl_registry* registry, std::uint32_t name, std::uint32_t version) {
    if (state_->manager) return;
    state_->manager = static_cast<wl_data_device_manager*>(wl_registry_bind(
        registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
    state_->attach();
}
void WaylandClipboard::seat(wl_seat* seat) { state_->seat = seat; state_->attach(); }
void WaylandClipboard::focused(bool focused) {
    state_->focused = focused;
    if (!focused) {
        state_->serial.reset();
        state_->updateSelection(nullptr);
    }
}
void WaylandClipboard::serial(std::uint32_t serial) { state_->serial = serial; }
void WaylandClipboard::preparePoll(std::vector<pollfd>& fds, int& timeout) const {
    auto& state = *state_;
    if (state.incoming) {
        fds.push_back({state.incoming->text.file.get(), POLLIN, 0});
        fds.push_back({state.incoming->rich.file.get(), POLLIN, 0});
        limitClipboardWait(timeout, state.incoming->deadline);
    }
    if (state.outgoing) limitClipboardWait(timeout, state.outgoing->deadline);
    for (const auto& send : state.sends) {
        fds.push_back({send.file.get(), POLLOUT, 0});
        limitClipboardWait(timeout, send.deadline);
    }
}
void WaylandClipboard::dispatchIo() { state_->dispatchIo(); }
bool WaylandClipboard::hasText() const { return state_->textFormat() != nullptr; }
Result WaylandClipboard::read(std::uint64_t request, bool rich) {
    auto& state = *state_;
    if (!state.device) return {Status::notSupported, u"Wayland clipboard unavailable"};
    auto* format = state.textFormat();
    if (!format) return noClipboardText();
    const char* privateType = nullptr;
    if (rich) {
        for (auto* candidate : {richClipboardMime, richClipboardFormat, legacyClipboardMime, legacyClipboardFormat}) {
            if (state.has(candidate)) { privateType = candidate; break; }
        }
    }
    state.incoming = State::Read{request, ClipboardClock::now() + clipboardWait,
                                state.receive(format), state.receive(privateType)};
    return {};
}
Result WaylandClipboard::write(std::uint64_t request, ClipboardData data) {
    auto& state = *state_;
    if (!state.device) return {Status::notSupported, u"Wayland clipboard unavailable"};
    if (!state.focused || !state.serial || wl_display_get_error(state.display)) return clipboardFailure();
    auto text = utf8FromUtf16(data.text);
    if (text.size() > clipboardLimit || data.rich.size() > clipboardLimit || state.sources.size() >= 32)
        return clipboardFailure();
    auto* source = wl_data_device_manager_create_data_source(state.manager);
    if (!source) return clipboardFailure();
    const std::string marker = "application/x-isoduo-selection-" + std::to_string(getpid()) + "-" + std::to_string(++sourceSequence);
    const bool rich = !data.rich.empty();
    state.sources.emplace(source, State::Source{marker, std::make_shared<std::string>(std::move(text)),
        rich ? std::make_shared<std::string>(std::move(data.rich)) : nullptr});
    wl_data_source_add_listener(source, &State::sourceListener, &state);
    for (auto* format : {utf8ClipboardFormat, plainClipboardFormat, x11Utf8Format}) wl_data_source_offer(source, format);
    if (rich) {
        wl_data_source_offer(source, richClipboardFormat);
        wl_data_source_offer(source, richClipboardMime);
    }
    wl_data_source_offer(source, marker.c_str());
    wl_data_device_set_selection(state.device, source, *state.serial);
    if (state.outgoing) {
        auto* superseded = state.outgoing->source;
        wl_data_source_destroy(superseded);
        state.sources.erase(superseded);
    }
    state.outgoing = State::Write{request, source, ClipboardClock::now() + clipboardWait};
    return {};
}
}
