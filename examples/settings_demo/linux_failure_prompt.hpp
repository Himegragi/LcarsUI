#pragma once
#include "settings_demo.hpp"
#include "linux_failure_dialog.hpp"
#include "isoduo/desktop.hpp"
#include "../../src/linux_preferences.hpp"
#include "../../src/utf.hpp"
#include <gio/gio.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace settingsDemo {
// Run the system prompt independently of the demo's OpenGL context. Its response
// runs on the desktop thread while the host continues to serve read-only requests.
class LinuxFailurePrompt {
public:
    explicit LinuxFailurePrompt(Demo& demo) : demo_(demo) {}
    ~LinuxFailurePrompt() {
        *alive_ = nullptr;
        if (process_) g_subprocess_force_exit(process_.get());
    }
    LinuxFailurePrompt(const LinuxFailurePrompt&) = delete;
    LinuxFailurePrompt& operator=(const LinuxFailurePrompt&) = delete;

    isoduo::CloseDecision requestClose(isoduo::Desktop& desktop) {
        if (desktop.rendererFailed()) {
            if (fallbackUnavailable_ && !demo_.isDirty()) return isoduo::CloseDecision::close;
            if (!process_) askRetry();
            return isoduo::CloseDecision::keepOpen;
        }
        return demo_.requestClose() ? isoduo::CloseDecision::close : isoduo::CloseDecision::keepOpen;
    }

    void show(isoduo::Desktop& desktop, std::u16string_view reason) {
        desktop_ = &desktop;
        reason_ = isoduo::detail::utf8FromUtf16(reason);
        askRetry();
    }

private:
    using Step = recovery::Step;
    using Choice = recovery::Choice;
    struct Dialog {
        Step step;
        std::string title, message;
    };
    using Lifetime = std::shared_ptr<LinuxFailurePrompt*>;
    struct Response {
        Lifetime alive;
        Dialog dialog;
    };

    Demo& demo_;
    isoduo::Desktop* desktop_ = nullptr;
    std::string reason_;
    bool useFallback_ = false, fallbackUnavailable_ = false;
    std::unique_ptr<GSubprocess, decltype(&g_object_unref)> process_{nullptr, g_object_unref};
    Lifetime alive_ = std::make_shared<LinuxFailurePrompt*>(this);

    void launch(Dialog dialog) {
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
        } else {
            // The built-in buttons report exit status alone. Extra buttons print
            // their labels, giving an explicit response even if startup can return 0 or 1.
            arguments = {"zenity", "--question", "--switch", "--no-markup", "--modal",
                         "--title=" + dialog.title, "--text=" + dialog.message};
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
        auto* context = isoduo::detail::LinuxPreferenceMonitor::context();
        g_main_context_push_thread_default(context);
        g_subprocess_communicate_async(process_.get(), nullptr, nullptr, complete, new Response{alive_, std::move(dialog)});
        g_main_context_pop_thread_default(context);
    }

    static void complete(GObject* source, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Response> response(static_cast<Response*>(data));
        auto* process = G_SUBPROCESS(source);
        GError* error = nullptr;
        GBytes* output = nullptr;
        const bool read = g_subprocess_communicate_finish(process, result, &output, nullptr, &error);
        std::unique_ptr<GBytes, decltype(&g_bytes_unref)> text(output, g_bytes_unref);
        std::unique_ptr<GError, decltype(&g_error_free)> failure(error, g_error_free);
        auto* prompt = *response->alive;
        if (!prompt) return;
        prompt->process_.reset();
        try {
            const auto status = read && g_subprocess_get_if_exited(process) ? g_subprocess_get_exit_status(process) : -1;
            const auto step = response->dialog.step;
            auto choice = status == 0 ? readChoice(step, output, prompt->useFallback_) : std::nullopt;
            if (choice) {
                prompt->fallbackUnavailable_ = false;
                prompt->respond(step, *choice);
            } else {
                std::cerr << "Renderer recovery dialog failed";
                if (error) std::cerr << ": " << error->message;
                else if (status == 0) std::cerr << ": no recognized button response";
                else if (status >= 0) std::cerr << " with status " << status;
                std::cerr << '\n';
                prompt->recover(std::move(response->dialog));
            }
        } catch (const std::exception& e) {
            std::cerr << "Renderer recovery failed: " << e.what() << '\n';
        }
    }

    static std::optional<Choice> readChoice(Step step, GBytes* output, bool useFallback) {
        gsize size = 0;
        const auto* data = output ? static_cast<const char*>(g_bytes_get_data(output, &size)) : nullptr;
        if (!size || data[size - 1] != '\n') return {};
        const std::string_view label(data, size - 1);
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
            else launch({Step::saveError, "無法儲存", isoduo::detail::utf8FromUtf16(error)});
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
