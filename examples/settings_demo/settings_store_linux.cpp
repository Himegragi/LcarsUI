#include "settings_store.hpp"
#include "utf.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace settingsDemo {
namespace {
std::u16string errorText(int code) {
    return isoduo::detail::utf16FromUtf8(std::generic_category().message(code));
}

struct File {
    int fd;
    explicit File(int value) : fd(value) {}
    File(const File&) = delete;
    ~File() { if (fd >= 0) ::close(fd); }
    int close() { return ::close(std::exchange(fd, -1)); }
};

struct TemporaryFile {
    std::string name;
    bool owned = false;
    ~TemporaryFile() { if (owned) ::unlink(name.c_str()); }
};

int lockDirectory(int fd) {
    int result;
    do { result = ::flock(fd, LOCK_EX); } while (result < 0 && errno == EINTR);
    return result;
}

bool retryableMoveError(int error) {
    return error == EINTR || error == EBUSY || error == EAGAIN;
}
}

LoadResult SettingsStore::load() const {
    const auto unreadable = [](int error) { return LoadResult{LoadKind::unreadable, {}, errorText(error)}; };
    if (auto error = fault("read")) return unreadable(error);
    auto file = path();
    File input(::open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
    if (input.fd < 0) {
        if (errno == ENOENT) return {LoadKind::missing, {}, {}};
        return unreadable(errno);
    }
    struct stat original{};
    if (::fstat(input.fd, &original) < 0) return unreadable(errno);
    if (!S_ISREG(original.st_mode)) return {LoadKind::unreadable, {}, u"設定路徑不是一般檔案"};

    // Bound the read even if the file grows after it is opened.
    std::string bytes;
    bool tooLarge = original.st_size > static_cast<off_t>(maxSettingsBytes);
    while (!tooLarge) {
        char buffer[8192];
        auto count = ::read(input.fd, buffer, std::min(sizeof buffer, maxSettingsBytes + 1 - bytes.size()));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return unreadable(errno);
        if (count == 0) break;
        bytes.append(buffer, static_cast<std::size_t>(count));
        tooLarge = bytes.size() > maxSettingsBytes;
    }
    if (!tooLarge) {
        if (auto document = parseSettings(bytes)) return {LoadKind::loaded, std::move(document), {}};
    }

    auto reason = std::u16string(u"格式或欄位不合法");
    if (tooLarge) reason += u"（超過 1 MiB）";
    auto quarantine = [&]() -> int {
        if (auto error = fault("rename")) return error;
        File directory(::open(directory_.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
        if (directory.fd < 0 || lockDirectory(directory.fd) < 0) return errno;
        struct stat current{};
        if (::fstatat(directory.fd, "settings.json", &current, AT_SYMLINK_NOFOLLOW) < 0) return errno;
        // A concurrent save may have replaced the file since we opened it for reading.
        if (current.st_dev != original.st_dev || current.st_ino != original.st_ino) return ESTALE;
        if (::renameat(directory.fd, "settings.json", directory.fd, "settings.json.bad") < 0) return errno;
        return 0;
    };
    auto error = quarantine();
    if (error) reason += u"；原檔無法改名（" + errorText(error) + u"）";
    return {LoadKind::corrupt, {}, std::move(reason), error == 0};
}

std::u16string SettingsStore::save(const SettingsDocument& document) const {
    auto bytes = writeSettings(document);
    if (bytes.size() > maxSettingsBytes) return u"\u8a2d\u5b9a\u6a94\u8d85\u904e 1 MiB";
    if (auto error = fault("directory")) return errorText(error);
    std::error_code directoryError;
    std::filesystem::create_directories(directory_, directoryError);
    if (directoryError) return errorText(directoryError.value());

    if (auto error = fault("create")) return errorText(error);
    TemporaryFile temporary{path().string() + ".XXXXXX.tmp"};
    File output(::mkostemps(temporary.name.data(), 4, O_CLOEXEC));
    if (output.fd < 0) return errorText(errno);
    temporary.owned = true;

    int error = fault("write");
    auto remaining = std::string_view(bytes);
    while (!error && !remaining.empty()) {
        auto count = ::write(output.fd, remaining.data(), remaining.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { error = count == 0 ? EIO : errno; break; }
        remaining.remove_prefix(static_cast<std::size_t>(count));
    }
    if (!error) {
        error = fault("flush");
        if (!error && ::fsync(output.fd) < 0) error = errno;
    }
    auto closeError = fault("close");
    if (output.close() < 0 && !error) error = errno;
    if (!error) error = closeError;
    if (error) return errorText(error);

    File directory(::open(directory_.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY));
    if (directory.fd < 0 || lockDirectory(directory.fd) < 0) return errorText(errno);
    auto destination = path();
    for (int attempt = 0; attempt < 4; ++attempt) {
        error = fault("move");
        if (!error && ::rename(temporary.name.c_str(), destination.c_str()) == 0) {
            temporary.owned = false;
            return {};
        }
        if (!error) error = errno;
        if (attempt == 3 || !retryableMoveError(error)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return errorText(error);
}
}
