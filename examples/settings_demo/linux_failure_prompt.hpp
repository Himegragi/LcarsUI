#pragma once
// Uses only Isoduo's public interfaces: the native window reference and Desktop::post.
#include "settings_demo.hpp"
#include "linux_failure_dialog.hpp"
#include "linux_prompt_parent.hpp"
#include "isoduo/desktop.hpp"
#include <gio/gio.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace settingsDemo {
// UTF-8 for the prompt's command line; an unpaired surrogate becomes U+FFFD.
inline std::string promptUtf8(std::u16string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t code = text[i];
        if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff)
            code = 0x10000 + ((code - 0xd800) << 10) + (text[++i] - 0xdc00);
        else if (code >= 0xd800 && code <= 0xdfff) code = 0xfffd;
        if (code < 0x80) result += static_cast<char>(code);
        else if (code < 0x800) {
            result += static_cast<char>(0xc0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            result += static_cast<char>(0xe0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            result += static_cast<char>(0xf0 | (code >> 18));
            result += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return result;
}

// Run the system prompt independently of the demo's OpenGL context. A worker thread waits for the prompt
// process; Desktop::post brings its answer back to the desktop thread, which keeps serving read-only
// requests meanwhile.
//
// On X11 the prompt is attached to the main window through its XID: Zenity gets --attach (Zenity 4 ignores it,
// so the demo also sets WM_TRANSIENT_FOR on Zenity's window) and the GTK helper makes itself transient for it.
// On Wayland an external prompt process cannot be made a child of another client's surface (xdg-foreign is not
// available on every compositor, and neither Zenity nor the GTK 3 helper can import a parent handle), so it
// opens unparented: a known platform limit.
class LinuxFailurePrompt {
public:
    explicit LinuxFailurePrompt(Demo& demo) : demo_(demo) {}
    ~LinuxFailurePrompt() {
        *alive_ = nullptr;
        stop();
    }
    LinuxFailurePrompt(const LinuxFailurePrompt&) = delete;
    LinuxFailurePrompt& operator=(const LinuxFailurePrompt&) = delete;

    isoduo::CloseDecision requestClose(isoduo::Desktop& desktop) {
        if (desktop.rendererFailed()) {
            desktop_ = &desktop;
            if (fallbackUnavailable_ && !demo_.isDirty()) return isoduo::CloseDecision::close;
            if (!process_) askRetry();
            return isoduo::CloseDecision::keepOpen;
        }
        return demo_.requestClose() ? isoduo::CloseDecision::close : isoduo::CloseDecision::keepOpen;
    }

    void show(isoduo::Desktop& desktop, std::u16string_view reason) {
        desktop_ = &desktop;
        reason_ = promptUtf8(reason);
        askRetry();
    }

    // Ends an open prompt and waits for its worker. Call it once run() has returned and before the Desktop is
    // destroyed; an answer that arrives after that is dropped.
    void stop() {
        if (process_) g_subprocess_force_exit(process_.get());
        if (waiter_.joinable()) waiter_.join();
        process_.reset();
    }

private:
    using Step = recovery::Step;
    using Choice = recovery::Choice;
    struct Dialog {
        Step step;
        std::string title, message;
    };
    using Lifetime = std::shared_ptr<LinuxFailurePrompt*>;
    using Process = std::unique_ptr<GSubprocess, decltype(&g_object_unref)>;
    struct Outcome {
        bool read = false;
        int status = -1;
        std::optional<std::string> output;
        std::string error;
    };

    Demo& demo_;
    isoduo::Desktop* desktop_ = nullptr;
    std::string reason_;
    bool useFallback_ = false, fallbackUnavailable_ = false;
    Process process_{nullptr, g_object_unref};
    std::thread waiter_;
    Lifetime alive_ = std::make_shared<LinuxFailurePrompt*>(this);

    // The main window's XID on X11; none on Wayland.
    std::optional<unsigned long> parentWindow() const {
        const auto* window = desktop_ ? desktop_->nativeWindow() : nullptr;
        if (!window || window->backend != isoduo::DesktopBackendKind::x11 || !window->windowId) return std::nullopt;
        return window->windowId;
    }

    void launch(Dialog dialog) {
        if (waiter_.joinable()) waiter_.join();
        const auto parent = parentWindow();
        std::vector<std::string> arguments;
        if (useFallback_) {
            std::error_code error;
            auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
            if (error) {
                std::cerr << "Cannot locate renderer recovery helper: " << error.message() << '\n';
                recover(std::move(dialog));
                return;
            }
            auto helper = executable.parent_path() / recovery::helperName;
            arguments = {helper.string(), std::to_string(static_cast<int>(dialog.step)), dialog.title, dialog.message};
            if (parent) arguments.push_back(std::to_string(*parent));
        } else {
            // The built-in buttons report exit status alone. Extra buttons print
            // their labels, giving an explicit response even if startup can return 0 or 1.
            arguments = {"zenity", "--question", "--switch", "--no-markup", "--modal",
                         "--title=" + dialog.title, "--text=" + dialog.message};
            if (parent) arguments.push_back("--attach=" + std::to_string(*parent));
            for (auto button : recovery::buttons(dialog.step))
                arguments.push_back(std::string("--extra-button=") + button.label);
        }
        std::vector<const char*> argv;
        for (const auto& argument : arguments) argv.push_back(argument.c_str());
        argv.push_back(nullptr);
        GError* error = nullptr;
        std::unique_ptr<GSubprocessLauncher, decltype(&g_object_unref)> launcher(
            g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE), g_object_unref);
        if (!useFallback_) {
            // Zenity 3 uses ESC for extra buttons. Zenity 4 can use EXTRA.
            g_subprocess_launcher_setenv(launcher.get(), "ZENITY_ESC", "0", TRUE);
            g_subprocess_launcher_setenv(launcher.get(), "ZENITY_EXTRA", "0", TRUE);
        }
        process_.reset(g_subprocess_launcher_spawnv(launcher.get(), argv.data(), &error));
        if (!process_) {
            std::cerr << "Cannot open renderer recovery dialog: " << error->message << '\n';
            g_error_free(error);
            recover(std::move(dialog));
            return;
        }
        auto* process = G_SUBPROCESS(g_object_ref(process_.get()));
        const auto attach = useFallback_ ? std::optional<unsigned long>{} : parent;
        waiter_ = std::thread([process, attach, desktop = desktop_, alive = alive_, dialog = std::move(dialog)]() mutable {
            Process owned(process, g_object_unref);
            if (attach) {
                const char* child = g_subprocess_get_identifier(process);
                if (child && !attachPromptToParent(static_cast<pid_t>(std::atoi(child)), *attach, 5000))
                    std::cerr << "Renderer recovery dialog was not attached to the main window\n";
            }
            GBytes* bytes = nullptr;
            GError* failure = nullptr;
            Outcome outcome;
            outcome.read = g_subprocess_communicate(process, nullptr, nullptr, &bytes, nullptr, &failure);
            if (outcome.read && g_subprocess_get_if_exited(process)) outcome.status = g_subprocess_get_exit_status(process);
            if (bytes) {
                gsize size = 0;
                const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
                outcome.output = std::string(data ? data : "", size);
                g_bytes_unref(bytes);
            }
            if (failure) {
                outcome.error = failure->message;
                g_error_free(failure);
            }
            // The answer runs on the desktop thread; a prompt destroyed meanwhile drops it.
            desktop->post([alive, outcome = std::move(outcome), dialog = std::move(dialog)]() mutable {
                if (auto* prompt = *alive) prompt->complete(std::move(dialog), std::move(outcome));
            });
        });
    }

    void complete(Dialog dialog, Outcome outcome) {
        if (waiter_.joinable()) waiter_.join();
        process_.reset();
        try {
            const auto step = dialog.step;
            auto choice = outcome.status == 0 ? readChoice(step, outcome.output, useFallback_) : std::nullopt;
            if (choice) {
                fallbackUnavailable_ = false;
                respond(step, *choice);
            } else {
                std::cerr << "Renderer recovery dialog failed";
                if (!outcome.error.empty()) std::cerr << ": " << outcome.error;
                else if (outcome.status == 0) std::cerr << ": no recognized button response";
                else if (outcome.status >= 0) std::cerr << " with status " << outcome.status;
                std::cerr << '\n';
                recover(std::move(dialog));
            }
        } catch (const std::exception& e) {
            std::cerr << "Renderer recovery failed: " << e.what() << '\n';
        }
    }

    static std::optional<Choice> readChoice(Step step, const std::optional<std::string>& output, bool useFallback) {
        if (!output || output->empty() || output->back() != '\n') return {};
        const std::string_view label(output->data(), output->size() - 1);
        for (auto button : recovery::buttons(step)) {
            if (label == button.label) return button.choice;
        }
        if (useFallback && label == recovery::dismissedToken) return Choice::dismissed;
        return {};
    }

    void recover(Dialog dialog) {
        if (!useFallback_) {
            useFallback_ = true;
            launch(std::move(dialog));
        } else {
            // A window-manager close may retry the prompt for a dirty document.
            // Only a clean document can close without prompting.
            fallbackUnavailable_ = true;
            useFallback_ = false;
        }
    }

    void askRetry() {
        launch({Step::retry, "畫面無法繪製", "畫面無法繪製（" + reason_ + "）。要重建畫面或結束程式？"});
    }

    void respond(Step step, Choice choice) {
        switch (choice) {
        case Choice::retry: {
            auto rebuilt = desktop_->rebuildRenderer();
            if (!rebuilt) show(*desktop_, rebuilt.message);
            break;
        }
        case Choice::quit:
            if (!demo_.isDirty()) desktop_->close();
            else launch({Step::save, "要儲存修改嗎？", demo_.blankNameDraft()
                ? "顯示名稱空白，將保留已確認的名稱。要儲存其他修改嗎？" : "要儲存修改嗎？"});
            break;
        case Choice::discard:
            desktop_->close();
            break;
        case Choice::save: {
            auto error = demo_.saveFailedRenderer();
            if (error.empty()) desktop_->close();
            else launch({Step::saveError, "無法儲存", promptUtf8(error)});
            break;
        }
        case Choice::cancel:
        case Choice::acknowledge:
            askRetry();
            break;
        case Choice::dismissed:
            if (step != Step::retry) askRetry();
            break;
        }
    }
};
}
