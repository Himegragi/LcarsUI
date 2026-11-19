#pragma once
#include "settings_demo.hpp"
#include "isoduo/desktop.hpp"
#include "../../src/linux_preferences.hpp"
#include "../../src/utf.hpp"
#include <gio/gio.h>
#include <initializer_list>
#include <iostream>
#include <memory>
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
    enum class Step { retry, save, saveError };
    using Lifetime = std::shared_ptr<LinuxFailurePrompt*>;
    struct Response {
        Lifetime alive;
        Step step;
    };

    Demo& demo_;
    isoduo::Desktop* desktop_ = nullptr;
    std::string reason_;
    std::unique_ptr<GSubprocess, decltype(&g_object_unref)> process_{nullptr, g_object_unref};
    Lifetime alive_ = std::make_shared<LinuxFailurePrompt*>(this);

    void launch(Step step, std::initializer_list<std::string> arguments) {
        std::vector<const char*> argv{"zenity", "--no-markup", "--modal"};
        for (const auto& argument : arguments) argv.push_back(argument.c_str());
        argv.push_back(nullptr);
        GError* error = nullptr;
        process_.reset(g_subprocess_newv(argv.data(), G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error));
        if (!process_) {
            std::cerr << "Cannot open renderer recovery dialog: " << error->message << '\n';
            g_error_free(error);
            return;
        }
        auto* context = isoduo::detail::LinuxPreferenceMonitor::context();
        g_main_context_push_thread_default(context);
        g_subprocess_communicate_utf8_async(process_.get(), nullptr, nullptr, complete, new Response{alive_, step});
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
        if (!read || !g_subprocess_get_if_exited(process)) {
            std::cerr << "Renderer recovery dialog failed";
            if (error) std::cerr << ": " << error->message;
            std::cerr << '\n';
            return;
        }
        try {
            prompt->respond(response->step, g_subprocess_get_exit_status(process), output ? g_strchomp(output) : "");
        } catch (const std::exception& e) {
            std::cerr << "Renderer recovery failed: " << e.what() << '\n';
        }
    }

    void askRetry() {
        launch(Step::retry, {"--question", "--title=畫面無法繪製",
            "--text=畫面無法繪製（" + reason_ + "）。要重建畫面或結束程式？",
            "--ok-label=重試", "--cancel-label=結束"});
    }

    void respond(Step step, int status, std::string_view output) {
        if (status != 0 && status != 1) {
            std::cerr << "Renderer recovery dialog exited with status " << status << '\n';
            return;
        }
        switch (step) {
        case Step::retry:
            if (status == 0) {
                auto rebuilt = desktop_->rebuildRenderer();
                if (!rebuilt) show(*desktop_, rebuilt.message);
            } else if (!demo_.isDirty()) {
                desktop_->close();
            } else {
                launch(Step::save, {"--question", "--title=要儲存修改嗎？",
                    demo_.blankNameDraft() ? "--text=顯示名稱空白，將保留已確認的名稱。要儲存其他修改嗎？" : "--text=要儲存修改嗎？",
                    "--ok-label=儲存", "--extra-button=捨棄", "--cancel-label=取消", "--default-cancel"});
            }
            break;
        case Step::save:
            if (output == "捨棄") {
                desktop_->close();
            } else if (status == 0) {
                auto error = demo_.saveFailedRenderer();
                if (error.empty()) desktop_->close();
                else launch(Step::saveError, {"--error", "--title=無法儲存",
                    "--text=" + isoduo::detail::utf8FromUtf16(error), "--ok-label=確定"});
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
