#include "check.hpp"
#include "desktop_linux_test.hpp"
#include "linux_failure_prompt.hpp"
#include "renderer.hpp"

using namespace isoduo;
using test::check;

int main(int argc, char** argv) {
    const bool failSave = argc == 2 && std::string_view(argv[1]) == "save-error";
    try {
        const auto& appearance = webAppearance();
        settingsDemo::Demo demo;
        check(bool(demo.name.focus()) && bool(demo.name.setValue(u"Unsaved draft")) && demo.isDirty(),
              "Create a dirty draft before the renderer fails");
        int saves = 0;
        demo.onSave([&](const auto& document) -> std::u16string {
            check(document.name == u"Unsaved draft", "Recovery save includes the draft");
            std::cout << "Save " << ++saves << std::endl;
            return failSave ? u"Fixture save failure" : u"";
        });
        settingsDemo::LinuxFailurePrompt prompt(demo);
        detail::RendererTestHooks hooks;
        hooks.contextLostAtFrame = 1;
        bool failed = false;
        DesktopConfig config{.title = u"Isoduo Recovery Fixture"};
        config.onCloseRequested = [&](Desktop& desktop) { return prompt.requestClose(desktop); };
        config.onRendererFailed = [&](Desktop& desktop, const RendererFailure& failure) {
            failed = true;
            hooks.contextLostAtFrame = 0;
            std::cout << "Frozen renderer" << std::endl;
            prompt.show(desktop, failure.message);
        };
        auto created = detail::LinuxDesktopTest::create(demo.ui, appearance, [&] { return demo.page(); }, demo.scroll, config, &hooks);
        check(bool(created.desktop), "Open the recovery fixture");
        detail::LinuxDesktopTest::observeFrames(*created.desktop, [&](const auto&, const auto&) {
            if (failed) created.desktop->close();
        });
        auto result = created.desktop->run();
        check(result.status == Status::rendererFailed || result.status == Status::ok, "Close or rebuild the failed host");
        check(demo.name.draft() && *demo.name.draft() == u"Unsaved draft", "Recoverable draft survives the prompt");
        std::cout << (result.status == Status::ok ? "Rebuilt" : "Closed frozen") << " with " << saves << " saves\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
