#include "desktop_host.hpp"
#include "edit_form.hpp"
#include "lcars/semantics.hpp"
#include "check.hpp"
#include <UIAutomation.h>

void configuration() {
    using namespace lcars;
    test::check(DesktopConfig{}.platformAccessibilityEnabled, "platform accessibility defaults on");
    editForm::Form enabledForm, disabledForm;
    DesktopConfig disabled;
    disabled.platformAccessibilityEnabled = false;
    detail::DesktopHost enabledHost(enabledForm.ui, webAppearance(), [&] { return enabledForm.page(); }, enabledForm.scroll, {}, false, true, nullptr);
    detail::DesktopHost disabledHost(disabledForm.ui, webAppearance(), [&] { return disabledForm.page(); }, disabledForm.scroll, disabled, false, true, nullptr);
    enabledHost.paint(); disabledHost.paint();
    test::check(SendMessageW(disabledHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) == 0, "disabled host exposes no LcarsUI provider");
    test::check(SendMessageW(enabledHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) != 0, "default host still exposes its provider");
    int events = 0;
    auto subscription = disabledForm.ui.semantics().subscribe([&](const SemanticEvent&) { ++events; });
    test::check(bool(disabledForm.handles.name.setValue(u"disabled host remains operational")), "handle works");
    disabledHost.pump();
    test::check(events > 0, "semantic events arrive through host loop");
    test::check(disabledForm.ui.semantics().get({"form.name"})->value == u"disabled host remains operational", "semantic query works");
}
int main() { const test::Case cases[] = {{"platform accessibility configuration", configuration}}; return test::run("Desktop accessibility", cases); }
