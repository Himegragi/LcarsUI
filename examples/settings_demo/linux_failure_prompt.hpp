#pragma once
#include "settings_demo.hpp"
#include "isoduo/desktop.hpp"
#include "../../src/linux_preferences.hpp"
#include "../../src/utf.hpp"
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace settingsDemo {
namespace recovery {
enum class Step { retry, save, saveError };
constexpr auto fallbackFlag = "--renderer-recovery-dialog";
constexpr auto discardLabel = "捨棄";

struct Dialog {
    Step step;
    std::string title, message;
};
}

// Run GTK on its own UI thread and event loop in a child of the demo process.
// It has no dependency on Zenity or the failed OpenGL context.
inline std::optional<int> runLinuxFailurePromptFallback(int argc, char** argv) {
    using recovery::Step;
    if (argc < 2 || std::string_view(argv[1]) != recovery::fallbackFlag) return {};
    if (argc != 5 || argv[2][0] < '0' || argv[2][0] > '2' || argv[2][1] != '\0' ||
        !gtk_init_check(nullptr, nullptr)) return 2;
    const auto step = static_cast<Step>(argv[2][0] - '0');
    auto* dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), argv[3]);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    // Client-side decorations can load theme images through external helpers.
    // Keep recovery usable even when the PATH that broke Zenity is incomplete.
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 8);
    auto* title = gtk_label_new(argv[3]);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(content), title);
    auto* message = gtk_label_new(argv[4]);
    gtk_label_set_line_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(message), 48);
    gtk_container_add(GTK_CONTAINER(content), message);
    gtk_widget_show_all(content);
    switch (step) {
    case Step::retry:
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "結束", GTK_RESPONSE_CANCEL, "重試", GTK_RESPONSE_OK, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        break;
    case Step::save:
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "取消", GTK_RESPONSE_CANCEL, recovery::discardLabel, GTK_RESPONSE_REJECT,
                               "儲存", GTK_RESPONSE_OK, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        break;
    case Step::saveError:
        gtk_dialog_add_button(GTK_DIALOG(dialog), "確定", GTK_RESPONSE_OK);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        break;
    }
    const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_REJECT) std::cout << recovery::discardLabel << '\n';
    return response == GTK_RESPONSE_OK ? 0 : 1;
}

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
    using Dialog = recovery::Dialog;
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
            arguments = {"/proc/self/exe", recovery::fallbackFlag, std::to_string(static_cast<int>(dialog.step)),
                         dialog.title, dialog.message};
        } else {
            arguments = {"zenity", "--no-markup", "--modal", "--title=" + dialog.title, "--text=" + dialog.message};
            switch (dialog.step) {
            case Step::retry:
                arguments.insert(arguments.end(), {"--question", "--ok-label=重試", "--cancel-label=結束"});
                break;
            case Step::save:
                arguments.insert(arguments.end(), {"--question", "--ok-label=儲存",
                    std::string("--extra-button=") + recovery::discardLabel, "--cancel-label=取消", "--default-cancel"});
                break;
            case Step::saveError:
                arguments.insert(arguments.end(), {"--error", "--ok-label=確定"});
                break;
            }
        }
        std::vector<const char*> argv;
        for (const auto& argument : arguments) argv.push_back(argument.c_str());
        argv.push_back(nullptr);
        GError* error = nullptr;
        process_.reset(g_subprocess_newv(argv.data(), G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error));
        if (!process_) {
            std::cerr << "Cannot open renderer recovery dialog: " << error->message << '\n';
            g_error_free(error);
            recover(std::move(dialog));
            return;
        }
        auto* context = isoduo::detail::LinuxPreferenceMonitor::context();
        g_main_context_push_thread_default(context);
        g_subprocess_communicate_utf8_async(process_.get(), nullptr, nullptr, complete, new Response{alive_, std::move(dialog)});
        g_main_context_pop_thread_default(context);
    }

    static void complete(GObject* source, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Response> response(static_cast<Response*>(data));
        auto* process = G_SUBPROCESS(source);
        GError* error = nullptr;
        char* output = nullptr;
        const bool read = g_subprocess_communicate_utf8_finish(process, result, &output, nullptr, &error);
        std::unique_ptr<char, decltype(&g_free)> text(output, g_free);
        std::unique_ptr<GError, decltype(&g_error_free)> failure(error, g_error_free);
        auto* prompt = *response->alive;
        if (!prompt) return;
        prompt->process_.reset();
        try {
            const auto status = read && g_subprocess_get_if_exited(process) ? g_subprocess_get_exit_status(process) : -1;
            if (status == 0 || status == 1) {
                prompt->fallbackUnavailable_ = false;
                prompt->respond(response->dialog.step, status, output ? g_strchomp(output) : "");
            } else {
                std::cerr << "Renderer recovery dialog failed";
                if (error) std::cerr << ": " << error->message;
                else if (status >= 0) std::cerr << " with status " << status;
                std::cerr << '\n';
                prompt->recover(std::move(response->dialog));
            }
        } catch (const std::exception& e) {
            std::cerr << "Renderer recovery failed: " << e.what() << '\n';
        }
    }

    void recover(Dialog dialog) {
        if (!useFallback_) {
            useFallback_ = true;
            launch(std::move(dialog));
        } else {
            // A window-manager close may retry the prompt for a dirty document.
            // Only a clean document can close without prompting.
            fallbackUnavailable_ = true;
        }
    }

    void askRetry() {
        launch({Step::retry, "畫面無法繪製", "畫面無法繪製（" + reason_ + "）。要重建畫面或結束程式？"});
    }

    void respond(Step step, int status, std::string_view output) {
        switch (step) {
        case Step::retry:
            if (status == 0) {
                auto rebuilt = desktop_->rebuildRenderer();
                if (!rebuilt) show(*desktop_, rebuilt.message);
            } else if (!demo_.isDirty()) {
                desktop_->close();
            } else {
                launch({Step::save, "要儲存修改嗎？", demo_.blankNameDraft()
                    ? "顯示名稱空白，將保留已確認的名稱。要儲存其他修改嗎？" : "要儲存修改嗎？"});
            }
            break;
        case Step::save:
            if (output == recovery::discardLabel) {
                desktop_->close();
            } else if (status == 0) {
                auto error = demo_.saveFailedRenderer();
                if (error.empty()) desktop_->close();
                else launch({Step::saveError, "無法儲存", isoduo::detail::utf8FromUtf16(error)});
            } else {
                askRetry();
            }
            break;
        case Step::saveError:
            askRetry();
            break;
        }
    }
};
}
