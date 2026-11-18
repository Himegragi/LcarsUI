#include "desktop_host.hpp"
#include "edit_form.hpp"
#include "isoduo/semantics.hpp"
#include "check.hpp"
#include <UIAutomation.h>

void configuration() {
    using namespace isoduo;
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
    test::check(SendMessageW(disabledHost.window(), WM_GETOBJECT, 0, UiaRootObjectId) == 0, "disabled host exposes no Isoduo provider");
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
void disabledPointerInput() {
    using namespace isoduo;
    editForm::Form form;
    DesktopConfig config;
    config.platformAccessibilityEnabled = false;
    detail::DesktopHost host(form.ui, webAppearance(), [&] { return form.page(); }, form.scroll, config, false, true, nullptr);
    host.paint();
    const auto* field = host.frame().find(form.handles.name);
    test::check(field != nullptr, "pointer target has measured placement");
    if (!field) return;
    // Frame placement is in physical pixels; convert to DIP before host-scale conversion.
    const int x = static_cast<int>((field->bounds.x + field->textField.editor.x + 2) / host.scale() * host.scale());
    const int y = static_cast<int>((field->bounds.y + field->textField.editor.y + field->textField.editor.h / 2) / host.scale() * host.scale());
    SendMessageW(host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    host.pump();
    SendMessageW(host.window(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    host.pump();
    const auto before = form.ui.semantics().get({"form.name"})->value;
    SendMessageW(host.window(), WM_CHAR, L'Z', 1);
    host.pump();
    const auto after = form.ui.semantics().get({"form.name"})->value;
    test::check(after.size() == before.size() + 1 && after.find(u'Z') != std::u16string::npos,
                "pointer focuses field and typing changes its value with platform accessibility disabled");
}
int main() { const test::Case cases[] = {{"platform accessibility configuration", configuration}, {"disabled pointer input", disabledPointerInput}}; return test::run("Desktop accessibility", cases); }