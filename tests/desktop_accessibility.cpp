#include "desktop_host.hpp"
#include "edit_form.hpp"
#include "lcars/semantics.hpp"
#include "check.hpp"
#include <UIAutomation.h>

void configuration() {
    using namespace lcars;
    test::check(DesktopConfig{}.platformAccessibilityEnabled, "platform accessibility defaults on");
    editForm::Form enabledForm, disabledForm, explicitForm;
    DesktopConfig explicitConfig; explicitConfig.platformAccessibilityEnabled = true;
    DesktopConfig disabled;
    disabled.platformAccessibilityEnabled = false;
    detail::DesktopHost enabledHost(enabledForm.ui, webAppearance(), [&] { return enabledForm.page(); }, enabledForm.scroll, {}, false, true, nullptr);
    detail::DesktopHost disabledHost(disabledForm.ui, webAppearance(), [&] { return disabledForm.page(); }, disabledForm.scroll, disabled, false, true, nullptr);
    detail::DesktopHost explicitHost(explicitForm.ui, webAppearance(), [&] { return explicitForm.page(); }, explicitForm.scroll, explicitConfig, false, true, nullptr);
    explicitHost.paint();
    test::check(SendMessageW(explicitHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) != 0, "explicit true exposes provider");
    enabledHost.paint(); disabledHost.paint();
    test::check(SendMessageW(disabledHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) == 0, "disabled host exposes no LcarsUI provider");
    test::check(SendMessageW(enabledHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) != 0, "default host still exposes its provider");
    int events = 0; bool focus = false, property = false, structure = false;
    auto subscription = disabledForm.ui.semantics().subscribe([&](const SemanticEvent& e) { ++events; focus |= e.kind == SemanticEventKind::focusChanged; property |= e.kind == SemanticEventKind::propertyChanged; structure |= e.kind == SemanticEventKind::structureChanged; });
    test::check(bool(disabledForm.handles.name.setValue(u"disabled host remains operational")), "handle works");
    test::check(bool(disabledForm.ui.semantics().perform({"form.name"}, Verb::focus)), "semantic focus works");
    auto added = disabledForm.ui.addLabel({.id = "acceptance.dynamic", .label = u"Dynamic"});
    test::check(bool(added), "structure mutation works");
    disabledHost.pump();
    test::check(focus && property && structure, "focus property and structure events arrive through host pump");
    test::check(SendMessageW(enabledHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) != 0, "disabled mutations preserve simultaneous enabled provider");
    SendMessageW(disabledHost.window(), WM_CHAR, L'Z', 1);
    disabledHost.pump();
    test::check(disabledForm.ui.semantics().get({"form.name"})->value.find(u'Z') != std::u16string::npos, "keyboard character reaches disabled host");
    test::check(bool(disabledForm.handles.name.setValue(u"disabled host remains operational")), "restore after keyboard input");
    test::check(events > 0, "semantic events arrive through host loop");
    test::check(disabledForm.ui.semantics().get({"form.name"})->value == u"disabled host remains operational", "semantic query works");
}
int main() { const test::Case cases[] = {{"platform accessibility configuration", configuration}}; return test::run("Desktop accessibility", cases); }
