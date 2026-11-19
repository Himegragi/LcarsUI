#include "presentation_matrix.hpp"
#include "x04_drivers.hpp"
#include "isoduo/semantics.hpp"
#include "core_internal.hpp"
#include "component_type.hpp"
#include "render.hpp"
// Assembled showcase acceptance through public operations and the host input seam.
// Every case runs for both appearances and both the shipped and alternate ID sets (AC02).
#include "check.hpp"
#include "edit_form.hpp"
#include "renderer_failure_checks.hpp"
#include "fake_measure.hpp"
#include "geometry.hpp"
#include <cmath>
#include <thread>
#include <set>
#include <fstream>
#include "utf.hpp"
#include <locale>
#include <stdexcept>
using namespace isoduo;
using test::check;

namespace {
editForm::Ids ids = editForm::defaultIds();
Density behaviorDensity = Density::standard;
const Appearance* appearance = &webAppearance();  // every case is rerun under each appearance
// Focus indicators draw only after keyboard input. Sweeps that inspect them start by forcing the display
// state on through the presentation switch (the same one an application would use).
void forceFocusIndicators(Ui& ui) {
    auto settings = ui.presentation(); settings.focusIndicator = FocusIndicatorMode::always;
    check(bool(ui.setPresentation(settings)), "Focus indicators can be forced on");
}

editForm::Ids alternateIds() {
    editForm::Ids i;
    i.title = "t"; i.intro = "i"; i.normal = "f1"; i.readOnly = "f2"; i.disabled = "f3"; i.error = "f4";
    i.longText = "f5"; i.reset = "f6"; i.nameLabel = "n.l"; i.name = "n"; i.nameHelp = "n.h"; i.nameError = "n.e";
    i.volumeLatest = "volume.latest"; i.volume = "volume"; i.notices = "notices"; i.mode = "mode"; i.offlineDisabled = "offline";
    i.region = "region.alt"; i.regionEmpty = "region.empty.alt";
    i.apply = "go"; i.state = "s"; i.draft = "d"; i.result = "r"; i.count = "c"; i.latest = "latest";
    return i;
}
KeyPress key(Key k, bool shift = false) { return {.key = k, .shift = shift}; }

void themeVariantsHaveAccessiblePalettes() {
    auto luminance = [](Color c) {
        auto channel = [](double v) { v /= 255.; return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
    };
    auto contrast = [&](Color a, Color b) { auto x = luminance(a), y = luminance(b); return (std::max(x,y)+0.05)/(std::min(x,y)+0.05); };
    for (auto variant : {ThemeVariant::light(), ThemeVariant::dark()}) {
        check(appearance->supports(variant), "PV01 official variant supported");
        auto p = appearance->palette(variant);
        for (auto ink : {p.text, p.muted, p.error})
            for (auto surface : {p.background, p.panel, p.surface})
                check(contrast(ink, surface) >= 4.5, "PV01 text contrast");
        check(contrast(p.text, p.selection) >= 4.5 && contrast(p.onAccent, p.accent) >= 4.5, "PV01 selection and accent contrast");
        check(p.scrimAlpha == (variant == ThemeVariant::dark() ? 160 : 110), "Variant scrim opacity");
    }
    check(!appearance->supports(ThemeVariant::highContrast()), "High contrast is unsupported");
}

void themeSwitchPreservesShowcaseGeometry() {
    editForm::Form form(ids, true); test::FakeMeasure measure;
    for (const char* page : {"v01", "v02", "v03"}) {
        form.handles.tabs.activate({page});
        auto settings = form.ui.presentation(); settings.variant = ThemeVariant::light(); form.ui.setPresentation(settings);
        ScrollState a, b;
        auto light = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, a);
        settings.variant = ThemeVariant::dark(); check(bool(form.ui.setPresentation(settings)), "Dark accepted");
        auto dark = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, b);
        check(light.items.size() == dark.items.size() && light.overlays.size() == dark.overlays.size(), "PV02 same placements");
        for (size_t i = 0; i < light.items.size(); ++i) {
            const auto& x = light.items[i]; const auto& y = dark.items[i];
            auto linesEqual = [](const auto& a, const auto& b) {
                if (a.size() != b.size()) return false;
                for (size_t j = 0; j < a.size(); ++j) if (a[j].bounds != b[j].bounds) return false;
                return true;
            };
            check(x.id == y.id && x.bounds == y.bounds && x.clip == y.clip && x.textSize == y.textSize &&
                  x.textField.label == y.textField.label && x.textField.surface == y.textField.surface && x.textField.editor == y.textField.editor &&
                  x.textField.description == y.textField.description && x.textField.error == y.textField.error && x.options == y.options &&
                  x.sliderTrack == y.sliderTrack && x.progressTrack == y.progressTrack && x.progressFill == y.progressFill &&
                  linesEqual(x.lines, y.lines) && linesEqual(x.labelLines, y.labelLines) &&
                  linesEqual(x.descriptionLines, y.descriptionLines) && linesEqual(x.errorLines, y.errorLines), "PV02 geometry unchanged");
        }
    }
    editForm::Form controls(ids, true); controls.handles.tabs.activate({"v04"});
    check(bool(controls.handles.dark.toggle()) && controls.ui.presentation().variant == ThemeVariant::dark(), "V04 switches to dark");
    check(bool(controls.handles.dark.toggle()) && controls.ui.presentation().variant == ThemeVariant::light(), "V04 switches to light");
}

void semanticTreeReadsRegisteredComponents() {
    Ui ui;
    ui.addButton({.id = "save", .label = u"Save"});
    auto children = ui.semantics().children({});
    auto node = ui.semantics().get({"save"});
    check(children && children->size() == 1 && node && node->name == u"Save" && node->role == Role::button,
          "Semantic tree enumerates and reads a registered button");
}

void semanticFormOperations() {
    editForm::Form form(ids, true);
    auto& tree = form.ui.semantics();
    check(bool(tree.perform({ids.name}, Verb::setValue, std::u16string(u"工作站 A"))), "semantic name draft");
    auto nameNode = tree.get({ids.name});
    check(nameNode && std::find(nameNode->verbs.begin(), nameNode->verbs.end(), Verb::undo) != nameNode->verbs.end(), "semantic verbs expose available undo");
    check(bool(tree.perform({ids.notices}, Verb::toggle)), "semantic checkbox");
    check(bool(tree.perform({ids.mode, OptionValue{"focus"}}, Verb::select)), "semantic radio selection");
    check(tree.perform({ids.mode}, Verb::select, OptionValue{"normal"}).status == Status::invalidValue,
          "item selection requires a logical item reference");
    check(bool(tree.perform({ids.region}, Verb::open)), "semantic dropdown open");
    check(bool(tree.perform({ids.region, OptionValue{"south"}}, Verb::select)), "semantic dropdown selection");
    check(bool(tree.perform({ids.volume}, Verb::setValue, 65)), "semantic slider");
    check(form.applies() == 0 && form.applied() == u"測試使用者" && bool(tree.perform({ids.apply}, Verb::invoke)), "semantic apply");
    check(form.applies() == 1 && form.applied() == u"工作站 A" && !form.noticesApplied() &&
          form.modeApplied() == OptionValue{"focus"} && form.regionApplied() == OptionValue{"south"} &&
          form.volumeApplied() == 65, "semantic F01 results");
    check(bool(tree.perform({ids.name}, Verb::setValue, std::u16string(u"  "))) &&
          tree.perform({ids.apply}, Verb::invoke).status == Status::invalidValue &&
          tree.get({ids.name})->error == u"請填寫顯示名稱" && tree.get({ids.name})->focused && form.applies() == 1,
          "semantic apply rejects blank draft and focuses the error");
}

void semanticModalOperations() {
    editForm::Form form(ids, true);
    auto& tree = form.ui.semantics();
    auto prefix = ids.title + ".pages.";
    check(bool(tree.perform({prefix + "tabs", OptionValue{"v03"}}, Verb::select)), "semantic switch page");
    check(bool(tree.perform({prefix + "v03.open"}, Verb::invoke)), "semantic open dialog");
    auto dialog = tree.get({prefix + "dialog"});
    check(dialog && dialog->name == u"重設示範進度" &&
          std::find(dialog->verbs.begin(), dialog->verbs.end(), Verb::cancel) != dialog->verbs.end(), "open dialog exposes its name and cancel action");
    auto half = tree.get({prefix + "v03.half"});
    check(half && half->blockedByModal && half->enabled, "modal background readable");
    auto progress = tree.get({prefix + "v03.progress"});
    check(progress && progress->numericValue == 50 && progress->blockedByModal, "modal background progress remains readable");
    check(tree.perform({prefix + "v03.half"}, Verb::invoke).status == Status::blockedByModal, "modal rejects background");
    check(tree.perform({prefix + "tabs", OptionValue{"v01"}}, Verb::select).status == Status::blockedByModal, "modal rejects tab selection");
    check(bool(tree.perform({prefix + "dialog"}, Verb::cancel)) && form.progress() == 50 && form.dialogConfirmations() == 0,
          "semantic cancel dialog");
    check(tree.perform({prefix + "dialog"}, Verb::cancel).status == Status::hidden &&
          form.handles.dialog.settle(false).status == Status::hidden, "closed dialog reports hidden through either path");
    check(tree.get({prefix + "v03.open"})->focused, "dialog cancel restores opener focus");
    check(bool(tree.perform({prefix + "v03.open"}, Verb::invoke)) &&
          bool(tree.perform({prefix + "dialog.confirm"}, Verb::invoke)) &&
          form.progress() == 0 && form.dialogConfirmations() == 1, "semantic P03 confirms exactly once");
}

void semanticListOperations() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    auto prefix = ids.title + ".pages.";
    check(bool(tree.perform({prefix + "tabs", OptionValue{"v02"}}, Verb::select)), "select list page");
    check(bool(tree.perform({prefix + "v02.twenty"}, Verb::invoke)), "load list sample");
    auto first = tree.get({prefix + "v02.list", OptionValue{"T005"}});
    auto second = tree.get({prefix + "v02.list", OptionValue{"T006"}});
    check(first && second && first->name == second->name && first->name == u"共用範本", "duplicate names retain distinct identities");
    check(bool(tree.perform({prefix + "v02.list", OptionValue{"T006"}}, Verb::select)), "select duplicate-name item");
    std::string reveal;
    detail::access(form.ui).onReveal([&](const std::string& id) { reveal = id; });
    check(form.list->handles.list.focus({"T005"}).status ==
          tree.perform({prefix + "v02.list", OptionValue{"T005"}}, Verb::focus).status &&
          tree.get({prefix + "v02.list", OptionValue{"T005"}})->focused &&
          form.list->selection() == OptionValue{"T006"} && reveal == prefix + "v02.list", "focus item without changing selection and request reveal");
    check(bool(tree.perform({prefix + "v02.list", OptionValue{"T003"}}, Verb::select)) &&
          tree.get({prefix + "v02.list", OptionValue{"T005"}})->focused,
          "selecting an item does not move the focus cursor");
    check(bool(tree.perform({prefix + "v02.list", OptionValue{"T006"}}, Verb::select)), "restore list selection");
    check(bool(tree.perform({prefix + "v02.sort"}, Verb::invoke)) &&
          form.list->selection() == OptionValue{"T006"}, "list selection survives sorting");
    check(bool(tree.perform({prefix + "v02.sort"}, Verb::invoke)) && form.list->selection() == OptionValue{"T006"},
          "list selection survives sorting back");
    check(bool(tree.perform({prefix + "v02.disable"}, Verb::invoke)) &&
          tree.perform({prefix + "v02.list", OptionValue{"T008"}}, Verb::select).status == Status::disabled,
          "disabled list item rejects selection");
}

void semanticTextAndThread() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    check(bool(tree.perform({ids.name}, Verb::setValue, std::u16string(u"A\U0001f600B"))), "semantic UTF-16 value");
    check(tree.perform({ids.name}, Verb::select, Selection{1, 2}).status == Status::invalidValue, "semantic selection rejects surrogate split");
    check(bool(tree.perform({ids.name}, Verb::select, Selection{1, 3})), "semantic selection accepts code point");
    form.handles.name.focus(); detail::access(form.ui).compositionUpdate(u"preedit", 3);
    check(tree.perform({ids.name}, Verb::setValue, std::u16string(u"x")).status == Status::busy &&
          tree.perform({ids.name}, Verb::select, Selection{1, 3}).status == Status::busy &&
          tree.text(ids.name).value == u"A\U0001f600B", "composition blocks edits and keeps preedit out of text");
    detail::access(form.ui).compositionCancel();
    check(bool(tree.perform({ids.name}, Verb::paste, std::u16string(u"x"))) && tree.text(ids.name).value == u"AxB", "semantic paste uses selected range");
    check(bool(tree.perform({ids.name}, Verb::select, Selection{1, 1})) && bool(tree.perform({ids.name}, Verb::copy)), "semantic copy of empty selection succeeds");
    check(bool(tree.perform({ids.apply}, Verb::focus)) && tree.get({ids.apply})->focused, "semantic focus uses handle behavior");
    check(bool(tree.perform({ids.disabled}, Verb::invoke)) && tree.perform({ids.name}, Verb::focus).status == Status::disabled,
          "semantic focus reports disabled before rejecting operation");
    Status read = Status::ok, write = Status::ok;
    std::thread worker([&] { read = tree.get({ids.name}).status; write = tree.perform({ids.name}, Verb::undo).status; });
    worker.join();
    check(read == Status::wrongThread && write == Status::wrongThread, "semantic access requires UI thread");
}

void semanticAdmissionPriority() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    form.handles.name.enable(false);
    check(tree.perform({ids.name}, Verb::setValue, 7).status == Status::disabled, "disabled precedes invalid argument");
    form.handles.tabs.activate({"v03"});
    check(tree.perform({ids.name}, Verb::setValue, std::u16string(u"x")).status == Status::hidden, "hidden precedes disabled");
    form.handles.progressHalf.enable(false); form.handles.dialogOpen.invoke();
    check(tree.perform({ids.title + ".pages.v03.half"}, Verb::invoke).status == Status::disabled, "disabled precedes modal block");
    form.handles.dialog.settle(false); form.handles.tabs.activate({"v01"}); form.handles.name.enable(true);
    form.handles.name.focus(); detail::access(form.ui).compositionUpdate(u"preedit", 3); form.handles.name.readOnly(true);
    check(tree.perform({ids.name}, Verb::setValue, std::u16string(u"x")).status == Status::readOnly, "read-only precedes composition");
    detail::access(form.ui).compositionCancel();
    form.handles.name.readOnly(false); form.handles.tabs.activate({"v02"});
    form.list->handles.twenty.invoke(); form.list->handles.disableEight.invoke(); form.list->handles.list.focus();
    check(bool(form.handles.dialog.open()), "open dialog over list");
    check(tree.perform({ids.title + ".pages.v02.list", OptionValue{"T008"}}, Verb::select).status == Status::disabled,
          "disabled item precedes modal block");
    check(tree.perform({ids.title + ".pages.v02.list", OptionValue{"T008"}}, Verb::focus).status == Status::disabled &&
          form.list->handles.list.focus({"T008"}).status == Status::disabled, "disabled focus precedes modal block through both paths");
    form.handles.dialog.settle(false);
}

void semanticRendererFailure() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    check(bool(tree.perform({ids.name}, Verb::setValue, std::u16string(u"工作站 A"))), "draft before renderer failure");
    detail::access(form.ui).rendererFailed(true);
    check(tree.text(ids.name).value == u"工作站 A" && tree.get({ids.name})->value == u"工作站 A", "renderer failure preserves semantic reads");
    check(rendererFailureRejectsInvoke(tree, {ids.apply}, [&] { return form.applies(); }),
          "renderer failure rejects semantic invoke");
    check(tree.perform({ids.result}, Verb::invoke).status == Status::rendererFailed, "renderer failure precedes unsupported verb");
    detail::access(form.ui).rendererFailed(false);
    check(bool(tree.perform({ids.apply}, Verb::invoke)) && form.applies() == 1, "semantic invoke resumes after renderer recovery");
    form.handles.tabs.activate({"v03"}); form.handles.dialogOpen.invoke();
    detail::access(form.ui).rendererFailed(true);
    auto dialog = tree.get({ids.title + ".pages.dialog"});
    check(dialog && dialog->verbs.empty(), "failed renderer removes dialog actions");
    detail::access(form.ui).rendererFailed(false);
    form.handles.dialog.settle(false);
}

void semanticNotifications() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    std::vector<SemanticEvent> events; bool inside = false, recursed = false, changed = false;
    auto subscription = tree.subscribe([&](const SemanticEvent& event) {
        recursed |= inside; inside = true; events.push_back(event);
        if (!changed && event.kind == SemanticEventKind::textChanged) { changed = true; tree.perform({ids.apply}, Verb::focus); }
        inside = false;
    });
    check(bool(tree.perform({ids.name}, Verb::setValue, std::u16string(u"new name"))), "semantic change accepted");
    check(events.empty(), "notifications wait until operation returns");
    int scheduled = 0;
    detail::access(form.ui).onSemanticPending([&] { ++scheduled; });
    check(scheduled == 1, "new host schedules notifications queued before attachment");
    tree.dispatchEvents();
    check(std::count_if(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::textChanged && e.ref.id == ids.name;
    }) == 1, "semantic text change delivered once");
    check(!recursed && std::any_of(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::focusChanged && e.ref.id == ids.apply;
    }), "nested semantic operation emits focus in a later notification round");
    check(std::any_of(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::propertyChanged && e.ref.id == ids.name && e.property == "value";
    }), "value property notification delivered");
    check(std::any_of(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::textSelectionChanged && e.ref.id == ids.name;
    }), "text selection notification delivered");
    events.clear(); auto prefix = ids.title + ".pages.";
    check(bool(tree.perform({prefix + "tabs", OptionValue{"v02"}}, Verb::select)), "switch for structure notification");
    tree.dispatchEvents();
    check(std::any_of(events.begin(), events.end(), [](const auto& e) { return e.kind == SemanticEventKind::structureChanged && e.ref.id.empty(); }),
          "page switch changes root structure");
    events.clear();
    check(bool(tree.perform({prefix + "v02.twenty"}, Verb::invoke)), "populate list");
    tree.dispatchEvents(); events.clear();
    check(bool(tree.perform({prefix + "v02.sort"}, Verb::invoke)), "reorder list"); tree.dispatchEvents();
    check(std::any_of(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::structureChanged && e.ref.id == prefix + "v02.list";
    }), "reordering notifies list structure");
    events.clear();
    check(bool(tree.perform({prefix + "v02.list", OptionValue{"T005"}}, Verb::focus)), "focus a logical item");
    tree.dispatchEvents();
    check(std::count_if(events.begin(), events.end(), [](const auto& e) { return e.kind == SemanticEventKind::focusChanged; }) == 1 &&
          std::any_of(events.begin(), events.end(), [&](const auto& e) {
              return e.kind == SemanticEventKind::focusChanged && e.ref.id == prefix + "v02.list" && e.ref.item == OptionValue{"T005"};
          }), "focus event names only the focused list item");
    events.clear();
    check(bool(tree.perform({prefix + "v02.search"}, Verb::setValue, std::u16string(u"共用範本"))), "filter list");
    tree.dispatchEvents();
    check(std::any_of(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::structureChanged && e.ref.id == prefix + "v02.list";
    }), "filter notifies list structure");
    events.clear();
    tree.perform({prefix + "v02.search"}, Verb::setValue, std::u16string(u"a"));
    tree.perform({prefix + "v02.search"}, Verb::setValue, std::u16string(u"b"));
    tree.dispatchEvents();
    check(std::count_if(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == SemanticEventKind::textChanged && e.ref.id == prefix + "v02.search";
    }) == 2, "separate operations retain separate notifications");
    events.clear(); test::FakeMeasure measure;
    arrange(form.page(), form.ui, *appearance, measure, 800, 600, 1.f, form.scroll);
    tree.dispatchEvents();
    check(std::count_if(events.begin(), events.end(), [](const auto& e) { return e.kind == SemanticEventKind::layoutChanged; }) == 1,
          "layout changes coalesce per arrangement");
    subscription.reset(); events.clear();
    tree.perform({prefix + "v02.search"}, Verb::setValue, std::u16string(u"c")); tree.dispatchEvents();
    check(events.empty(), "unsubscribed listener receives no events");
}

void semanticFocusDriver() {
    for (int scenario = 0; scenario < 7; ++scenario) {
    editForm::Form form(ids, true); auto& state = detail::access(form.ui);
    auto entries = x04::inventory(form);
    if (scenario == 1) for (const auto& entry : entries) std::visit([](const auto& h) {
        if constexpr (requires { h.enable(false); }) h.enable(false);
    }, entry.handle);
    if (scenario == 2) for (const auto& entry : entries) std::visit([](const auto& h) {
        if constexpr (requires { h.readOnly(true); }) h.readOnly(true);
    }, entry.handle);
    if (scenario == 3) { form.handles.tabs.activate({"v03"}); form.handles.dialogOpen.invoke(); }
    if (scenario == 4) form.handles.tabs.activate({"v03"});
    if (scenario == 5) { form.handles.normal.bind([]() -> Result { throw std::runtime_error("stop fixture"); }); form.handles.normal.invoke(); }
    if (scenario == 6) detail::access(form.ui).rendererFailed(true);
    for (const auto& entry : entries) {
        auto id = state.layoutId(entry.handle); if (!id) continue;
        auto typed = std::visit([](const auto& handle) -> Status {
            if constexpr (requires { handle.focus(); }) return handle.focus().status;
            else return Status::notSupported;
        }, entry.handle);
        if (scenario >= 5 && typed == Status::notSupported) continue;
        auto semantic = form.ui.semantics().perform({*id}, Verb::focus).status;
        check(typed == semantic, "semantic focus matches typed handle");
        if (auto list = std::get_if<ListHandle>(&entry.handle)) {
            auto children = form.ui.semantics().children({*id});
            if (children) for (const auto& child : *children) if (child.item)
                check(list->focus(*child.item).status == form.ui.semantics().perform(child, Verb::focus).status,
                      "logical-item focus matches typed handle");
        }
    }
    }
}

void semanticSubscriberExceptionBoundary() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics(); int calls = 0;
    auto first = tree.subscribe([](const SemanticEvent&) { throw std::runtime_error("subscriber failure"); });
    auto second = tree.subscribe([&](const SemanticEvent&) { ++calls; });
    check(bool(tree.perform({ids.apply}, Verb::focus)), "focus before subscriber failure");
    tree.dispatchEvents();
    check(calls > 0, "a throwing subscriber does not prevent other listeners");
}

void semanticRemovalNotification() {
    Ui ui; auto button = ui.addButton({.id = "removable", .label = u"Removable"}).value.value();
    button.focus(); std::vector<SemanticEvent> events;
    auto subscription = ui.semantics().subscribe([&](const auto& event) { events.push_back(event); });
    check(bool(button.remove()), "remove focused component");
    ui.semantics().dispatchEvents();
    check(std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.kind == SemanticEventKind::structureChanged && event.ref.id.empty();
    }), "removal notifies root structure");
}

void semanticVerbDrivers() {
    for (int state = 0; state < 7; ++state) for (auto verb : {Verb::invoke, Verb::toggle, Verb::setValue, Verb::select, Verb::open, Verb::cancel,
                      Verb::undo, Verb::redo, Verb::copy, Verb::cut, Verb::paste, Verb::confirm}) {
        editForm::Form a(ids, true), b(ids, true);
        auto typedEntries = x04::inventory(a); auto semanticEntries = x04::inventory(b);
        auto prepare = [&](editForm::Form& form, const auto& entries) {
            if (state == 1) for (const auto& entry : entries) std::visit([](const auto& h) {
                if constexpr (requires { h.enable(false); }) h.enable(false);
            }, entry.handle);
            if (state == 2) for (const auto& entry : entries) std::visit([](const auto& h) {
                if constexpr (requires { h.readOnly(true); }) h.readOnly(true);
            }, entry.handle);
            if (state == 3) { form.handles.tabs.activate({"v03"}); form.handles.dialogOpen.invoke(); }
            if (state == 4) form.handles.tabs.activate({"v03"});
            if (state == 5) { form.handles.normal.bind([]() -> Result { throw std::runtime_error("stop fixture"); }); form.handles.normal.invoke(); }
            if (state == 6) detail::access(form.ui).rendererFailed(true);
        };
        prepare(a, typedEntries); prepare(b, semanticEntries);
        check(typedEntries.size() == semanticEntries.size(), "same inventory for verb driver");
        for (std::size_t i = 0; i < typedEntries.size() && i < semanticEntries.size(); ++i) {
            auto expected = std::visit([&](const auto& h) -> Status {
                using T = std::decay_t<decltype(h)>;
                if constexpr (std::is_same_v<T, ButtonHandle>) { if (verb == Verb::invoke) return h.invoke().status; }
                if constexpr (std::is_same_v<T, CheckboxHandle>) { if (verb == Verb::toggle) return h.toggle().status; }
                if constexpr (std::is_same_v<T, TextFieldHandle>) {
                    switch (verb) {
                    case Verb::setValue: return h.setValue(u"example").status;
                    case Verb::select: return h.select(0, 0).status;
                    case Verb::undo: return h.undo().status;
                    case Verb::redo: return h.redo().status;
                    case Verb::copy: return h.copy().status;
                    case Verb::cut: return h.cut().status;
                    case Verb::paste: return h.paste(u"x").status;
                    default: break;
                    }
                }
                if constexpr (std::is_same_v<T, SliderHandle>) { if (verb == Verb::setValue) return h.setValue(50).status; }
                if constexpr (std::is_same_v<T, DropdownHandle>) {
                    if (verb == Verb::open) return h.open().status;
                    if (verb == Verb::cancel) return h.cancel().status;
                }
                return Status::notSupported;
            }, typedEntries[i].handle);
            if (state >= 5 && expected == Status::notSupported) continue;
            auto ref = detail::access(b.ui).layoutId(semanticEntries[i].handle);
            if (!ref) { check(expected == Status::notFound, "removed handle is unavailable"); continue; }
            auto snapshot = b.ui.semantics().get({*ref});
            if (snapshot && (state == 6 || snapshot->hidden || !snapshot->enabled || snapshot->blockedByModal))
                check(snapshot->verbs.empty(), "restricted node omits verbs");
            Argument arg;
            if (verb == Verb::setValue) {
                auto n = b.ui.semantics().get({*ref});
                arg = n && n->role == Role::slider ? Argument{50} : Argument{std::u16string(u"example")};
            } else if (verb == Verb::select) arg = Selection{0, 0};
            else if (verb == Verb::paste) arg = std::u16string(u"x");
            auto actual = b.ui.semantics().perform({*ref}, verb, arg).status;
            check(actual == expected, "semantic verb status matches typed handle or is inapplicable");
            {
                auto children = b.ui.semantics().children({*ref});
                if (children) for (const auto& child : *children) if (child.item) {
                    if (state == 6 && verb != Verb::select) continue;
                    if (verb != Verb::select) {
                        check(b.ui.semantics().perform(child, verb, arg).status == Status::notSupported,
                              "inapplicable logical-item verb is not supported");
                        continue;
                    }
                    auto itemStatus = std::visit([&](const auto& h) -> Status {
                        using T = std::decay_t<decltype(h)>;
                        if constexpr (std::is_same_v<T, RadioGroupHandle> || std::is_same_v<T, DropdownHandle> ||
                                      std::is_same_v<T, ListHandle> || std::is_same_v<T, NavigationListHandle>)
                            return h.select(*child.item).status;
                        else if constexpr (std::is_same_v<T, TabsHandle>) return h.activate(*child.item).status;
                        else return Status::notSupported;
                    }, typedEntries[i].handle);
                    auto semanticStatus = b.ui.semantics().perform(child, verb).status;
                    check(itemStatus == semanticStatus, "semantic item selection matches typed handle");
                }
            }
        }
    }
}

void semanticWrongThreadDrivers() {
    editForm::Form form(ids, true); auto entries = x04::inventory(form);
    std::vector<std::pair<NodeRef, ComponentHandle>> targets;
    for (const auto& entry : entries) {
        auto id = detail::access(form.ui).layoutId(entry.handle); if (!id) continue;
        targets.emplace_back(NodeRef{*id}, entry.handle);
        auto children = form.ui.semantics().children({*id});
        if (children) for (const auto& child : *children) if (child.item) targets.emplace_back(child, entry.handle);
    }
    std::vector<bool> results;
    std::thread worker([&] {
        for (auto verb : {Verb::invoke, Verb::toggle, Verb::setValue, Verb::select, Verb::open, Verb::cancel, Verb::focus,
                          Verb::undo, Verb::redo, Verb::copy, Verb::cut, Verb::paste, Verb::confirm}) {
            for (const auto& [ref, handle] : targets) {
                auto status = std::visit([&](const auto& h) -> std::optional<Status> {
                    using T = std::decay_t<decltype(h)>;
                    if (ref.item) {
                        if constexpr (std::is_same_v<T, ListHandle>) if (verb == Verb::focus) return h.focus(*ref.item).status;
                        if (verb != Verb::select) return {};
                        if constexpr (std::is_same_v<T, RadioGroupHandle> || std::is_same_v<T, DropdownHandle> ||
                                      std::is_same_v<T, ListHandle> || std::is_same_v<T, NavigationListHandle>) return h.select(*ref.item).status;
                        else if constexpr (std::is_same_v<T, TabsHandle>) return h.activate(*ref.item).status;
                        return {};
                    }
                    if (verb == Verb::focus) { if constexpr (requires { h.focus(); }) return h.focus().status; }
                    if constexpr (std::is_same_v<T, ButtonHandle>) { if (verb == Verb::invoke) return h.invoke().status; }
                    if constexpr (std::is_same_v<T, CheckboxHandle>) { if (verb == Verb::toggle) return h.toggle().status; }
                    if constexpr (std::is_same_v<T, TextFieldHandle>) switch (verb) {
                        case Verb::setValue: return h.setValue(u"x").status; case Verb::select: return h.select(0, 0).status;
                        case Verb::undo: return h.undo().status; case Verb::redo: return h.redo().status;
                        case Verb::copy: return h.copy().status; case Verb::cut: return h.cut().status;
                        case Verb::paste: return h.paste(u"x").status; default: break;
                    }
                    if constexpr (std::is_same_v<T, SliderHandle>) { if (verb == Verb::setValue) return h.setValue(50).status; }
                    if constexpr (std::is_same_v<T, DropdownHandle>) {
                        if (verb == Verb::open) return h.open().status; if (verb == Verb::cancel) return h.cancel().status;
                    }
                    if (verb == Verb::confirm) { if constexpr (requires { h.confirm(); }) return h.confirm().status; }
                    return {};
                }, handle);
                if (status) results.push_back(*status == Status::wrongThread && form.ui.semantics().perform(ref, verb).status == *status);
            }
        }
    });
    worker.join();
    check(!results.empty() && std::all_of(results.begin(), results.end(), [](bool ok) { return ok; }), "all applicable verbs reject a non-UI thread");
}

void semanticBusyAndRemovedDrivers() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    form.handles.name.focus(); detail::access(form.ui).compositionUpdate(u"preedit", 3);
    for (auto verb : {Verb::setValue, Verb::select, Verb::paste, Verb::cut, Verb::undo, Verb::redo}) {
        Argument arg;
        if (verb == Verb::setValue || verb == Verb::paste) arg = std::u16string(u"x");
        if (verb == Verb::select) arg = Selection{0, 0};
        check(tree.perform({ids.name}, verb, arg).status == Status::busy, "text mutation is busy during composition");
    }
    detail::access(form.ui).compositionCancel();
    form.handles.tabs.activate({"v03"}); auto removed = form.handles.dialogTemporary;
    auto id = ids.title + ".pages.v03.temporary"; check(bool(removed.remove()), "remove temporary button");
    check(removed.invoke().status == Status::notFound && tree.perform({id}, Verb::invoke).status == Status::notFound &&
          removed.focus().status == Status::notFound && tree.perform({id}, Verb::focus).status == Status::notFound,
          "removed component rejects both typed and semantic operations");
}

void writeSemanticTsv(editForm::Form& form, const char* state) {
    const char* roles[] = {"text", "button", "label", "checkbox", "radio_group", "radio_option", "slider", "dropdown", "dropdown_option", "list", "list_item", "tabs", "tab_item", "progress", "dialog", "tooltip"};
    auto filename = std::string("semantic-") + (appearance == &webAppearance() ? "web-" : "material-") + state + ".tsv";
    std::ofstream out(filename, std::ios::binary);
    auto clean = [&](const std::u16string& text) { auto str = detail::utf8FromUtf16(text); for (auto& ch : str) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' '; return str; };
    out << "id\titem\trole\tname\tvalue\thidden\tvisible\tblocked_by_modal\tbounds\n";
    auto visit = [&](auto&& self, const NodeRef& parent) -> void {
        auto children = form.ui.semantics().children(parent);
        check(bool(children), "Tree traversal succeeds while writing TSV");
        for (const auto& ref : *children) {
            auto n = form.ui.semantics().get(ref); check(bool(n), "Enumerated node can be read");
            out << ref.id << '\t' << (ref.item ? ref.item->id : "") << '\t' << roles[static_cast<int>(n->role)] << '\t'
                << clean(n->name) << '\t' << clean(n->value) << '\t' << n->hidden << '\t' << n->visible << '\t' << n->blockedByModal << '\t';
            if (n->bounds) out << n->bounds->x << ',' << n->bounds->y << ',' << n->bounds->w << ',' << n->bounds->h;
            else out << '-';
            out << '\n'; self(self, ref);
        }
    };
    visit(visit, {}); out.flush(); check(bool(out), "Semantic TSV was written");
}

void semanticShowcaseStatesAndIdentity() {
    editForm::Form form(ids, true); auto& tree = form.ui.semantics();
    test::FakeMeasure measure;
    auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll); };
    draw();
    if (ids.name == editForm::defaultIds().name) writeSemanticTsv(form, "v01");
    auto inventory = x04::inventory(form);
    std::set<std::string> unique;
    for (auto& entry : inventory) { auto s = x04::describe(entry.handle); if (s) unique.insert(s->id); }
    std::set<std::string> treeComponents;
    auto visit = [&](auto&& self, const NodeRef& parent) -> void {
        auto children = tree.children(parent); check(bool(children), "Semantic children are readable");
        for (const auto& ref : *children) {
            auto node = tree.get(ref); check(bool(node), "Enumerated semantic node is readable");
            if (!ref.item && node->role != Role::dialog && node->role != Role::tooltip) treeComponents.insert(ref.id);
            if (node->role == Role::dialog) self(self, ref);
        }
    };
    visit(visit, {});
    check(inventory.size() == treeComponents.size() && unique == treeComponents,
          "X04 includes every tree component exactly once");
    for (const auto& handle : {ComponentHandle{form.handles.v04Title}, ComponentHandle{form.handles.font}, ComponentHandle{form.handles.fontStatus}, ComponentHandle{form.handles.resetV04}, ComponentHandle{form.handles.textSize}, ComponentHandle{form.handles.dark}, ComponentHandle{form.handles.compact}, ComponentHandle{form.handles.preview}})
        check(unique.contains(x04::describe(handle)->id), "X04 includes each V04 component");
    auto mode = form.handles.mode.describe()->id;
    auto options = tree.children({mode});
    check(options && options->size() == 3 && (*options)[0].item == OptionValue{"normal"} &&
          (*options)[1].item == OptionValue{"focus"} && (*options)[2].item == OptionValue{"offline"}, "SEM02 radio options are tree children");
    auto name = tree.get({ids.name});
    check(name && !name->hidden && name->name == form.handles.name.describe()->name && name->bounds,
          "SEM02 active field has its current name and clipped bounds");
    auto listId = form.list->handles.list.describe()->id;
    check(tree.get({listId})->hidden, "SEM02 inactive list remains readable and hidden");
    form.handles.tabs.activate({"v02"}); draw();
    if (ids.name == editForm::defaultIds().name) writeSemanticTsv(form, "v02");
    auto a = tree.get({listId, OptionValue{"T005"}}); auto b = tree.get({listId, OptionValue{"T006"}});
    check(a && b && a->name == u"\u5171\u7528\u7bc4\u672c" && b->name == a->name, "SEM03 duplicate names retain distinct identities");
    form.list->handles.list.select({"T006"}); form.list->handles.sort.invoke(); form.list->handles.sort.invoke();
    check(tree.get({listId, OptionValue{"T006"}})->selected && tree.get({listId, OptionValue{"T999"}}).status == Status::notFound,
          "SEM03 selected identity survives sorting and unknown item is absent");
    form.list->handles.list.select({"T003"}); form.list->handles.erase.invoke();
    check(tree.get({listId, OptionValue{"T003"}}).status == Status::notFound, "SEM03 deleted item is absent");
    form.handles.tabs.activate({"v04"}); draw();
    if (ids.name == editForm::defaultIds().name) writeSemanticTsv(form, "v04");
    form.handles.tabs.activate({"v03"}); draw();
    if (ids.name == editForm::defaultIds().name) writeSemanticTsv(form, "v03");
    auto hidden = tree.get({ids.name});
    check(hidden && hidden->hidden && !hidden->visible && hidden->value == form.handles.name.describe()->value &&
          form.handles.name.setValue(u"blocked").status == Status::hidden, "SEM08 hidden field remains readable and rejects edits");
    form.handles.dialogOpen.invoke(); draw();
    auto background = tree.get({form.handles.progressHalf.describe()->id});
    check(background && background->blockedByModal && background->enabled, "Modal blocking is distinct from enabled state");
    std::optional<NodeSnapshot> dialog;
    auto roots = tree.children({}); check(bool(roots), "SEM02 root children are readable with a dialog open");
    for (const auto& ref : *roots) { auto n = tree.get(ref); if (n && n->role == Role::dialog && !n->hidden) dialog = *n; }
    check(bool(dialog), "SEM02 open dialog is a visible root child");
    check(dialog->bounds.has_value(), "SEM02 open dialog has bounds");
    auto members = tree.children(dialog->ref); check(members && !members->empty(), "SEM02 open dialog has members");
    for (const auto& ref : *members) {
        auto n = tree.get(ref);
        check(n && n->visible && n->bounds && test::inside(*n->bounds, *dialog->bounds),
              "SEM02 every open dialog member is visible with bounds inside the dialog");
    }
}

void semanticUnnamedDiagnostic() {
    Ui ui; auto button = ui.addButton({.id = "probe.unnamed"}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    arrange(layout::component(button), ui, *appearance, measure, 400, 200, 1.f, scroll);
    arrange(layout::component(button), ui, *appearance, measure, 400, 200, 1.f, scroll);
    check(ui.semantics().get({"probe.unnamed"})->name.empty(), "SEM11 ID is never substituted for a missing name");
#ifndef NDEBUG
    check(ui.diagnostics().size() == 1, "SEM11 unnamed focusable component emits one diagnostic");
#endif
}

void presentationSettingsAreAtomic() {
    Ui ui;
    int redraws = 0;
    detail::access(ui).onInvalidate([&] { ++redraws; });
    auto initial = ui.presentation();
    check(initial.textSize == 16, "Default text size is 16 DIP");
    check(bool(ui.setPresentation(initial)) && redraws == 0, "Identical settings do not invalidate");
    auto changed = initial; changed.textSize = 28;
    check(bool(ui.setPresentation(changed)) && ui.presentation() == changed && redraws == 1, "Valid settings apply once");
    auto invalid = changed; invalid.textSize = 29;
    check(ui.setPresentation(invalid).status == Status::invalidValue && ui.presentation() == changed && redraws == 1, "Invalid size is atomic");
    invalid = changed; invalid.density = static_cast<Density>(99);
    check(ui.setPresentation(invalid).status == Status::invalidValue && ui.presentation() == changed, "Invalid density is atomic");
    invalid = changed; invalid.variant = ThemeVariant::highContrast();
    check(ui.setPresentation(invalid).status == Status::notSupported && ui.presentation() == changed, "High contrast is reserved");
    Status threadResult = Status::ok;
    std::thread worker([&] { threadResult = ui.setPresentation(initial).status; }); worker.join();
    check(threadResult == Status::wrongThread && ui.presentation() == changed, "Wrong-thread call is rejected");
    invalid.variant.name = "sepia";
    check(ui.setPresentation(invalid).status == Status::notFound && ui.presentation() == changed, "Unknown variant is rejected");
}

void presentationSizeScalesExplicitAndDefaultText() {
    Ui ui;
    auto body = ui.addLabel({.id = "body", .value = u"Body"}).value.value();
    auto title = ui.addLabel({.id = "title", .value = u"Title"}).value.value();
    auto root = layout::column({layout::component(body), layout::component(title, 24)});
    test::FakeMeasure measure; ScrollState scroll;
    auto settings = ui.presentation(); settings.textSize = 28;
    check(bool(ui.setPresentation(settings)), "Size accepted");
    auto frame = arrange(root, ui, *appearance, measure, 640, 480, 1.f, scroll);
    check(frame.find(body)->textSize == 28 && frame.find(title)->textSize == 42, "Body and explicit title scale proportionally");
}

void compactDensityReflowsFields() {
    Ui ui;
    auto field = ui.addTextField({.id = "density.field", .label = u"Name"}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::component(field);
    auto standard = arrange(root, ui, *appearance, measure, 400, 400, 1.f, scroll);
    auto settings = ui.presentation(); settings.density = Density::compact;
    check(bool(ui.setPresentation(settings)), "Compact density accepted");
    auto compact = arrange(root, ui, *appearance, measure, 400, 400, 1.f, scroll);
    check(compact.find(field)->bounds.h < standard.find(field)->bounds.h, "Compact field is shorter");
    check(compact.find(field)->textSize == standard.find(field)->textSize, "Density preserves text size");
}

void compactShowcaseHasSmallerNonoverlappingLayout() {
    editForm::Form form(ids, true);
    test::FakeMeasure measure;
    auto standard = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
    auto settings = form.ui.presentation(); settings.density = Density::compact;
    check(bool(form.ui.setPresentation(settings)), "Compact accepted");
    auto compact = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
    check(compact.extent < standard.extent, "DN01 compact reduces V01 content height");
    for (std::size_t i = 0; i < compact.items.size(); ++i) {
        const auto& item = compact.items[i];
        for (const auto& old : standard.items) if (old.id == item.id)
            check(old.textSize == item.textSize, "DN01 text size is unchanged");
        for (std::size_t j = i + 1; j < compact.items.size(); ++j) {
            auto overlap = intersect(item.bounds, compact.items[j].bounds);
            check(overlap.w == 0 || overlap.h == 0, "DN01 components do not overlap");
        }
    }
}

void compactTargetsMeetMinimumHeight() {
    editForm::Form form(ids, true);
    auto settings = form.ui.presentation(); settings.density = Density::compact; settings.textSize = 12;
    check(bool(form.ui.setPresentation(settings)), "Compact 12 DIP accepted");
    test::FakeMeasure measure;
    for (const char* page : {"v01", "v02", "v03", "v04"}) {
        form.handles.tabs.activate({page});
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
        for (const auto& item : frame.items) {
            if (item.kind == ComponentKind::label || item.kind == ComponentKind::progress) continue;
            check(item.bounds.h >= 24, "Clickable component is at least 24 DIP high");
            for (auto option : item.options) check(option.h >= 24, "Clickable row is at least 24 DIP high");
        }
    }
}

void v04CompactCheckboxAppliesDensity() {
    editForm::Form form(ids, true);
    form.handles.tabs.activate({"v04"});
    check(bool(form.handles.compact.toggle()), "Compact checkbox toggles");
    check(form.ui.presentation().density == Density::compact, "Checkbox applies compact density");
    check(!form.handles.compact.describe()->sourceConflict, "The checkbox's own write does not show an external conflict");
    check(bool(form.handles.compact.toggle()), "Compact checkbox toggles back");
    check(form.ui.presentation().density == Density::standard, "Checkbox restores standard density");
}

// #84: the presentation page compares alignments: one mixed-script paragraph, two dropdowns that drive it,
// and "姓　名" distributed next to the same label left-aligned.
void v04JustifyComparisonFollowsItsDropdowns() {
    editForm::Form form(ids, true);
    auto& h = form.handles; h.tabs.activate({"v04"});
    test::FakeMeasure measure;
    auto lay = [&] { return arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll); };
    auto stretched = [&](const Frame& frame) {
        const auto* sample = frame.find(h.justifySample);
        std::size_t count = 0;
        for (const auto& line : sample->lines) count += line.segments.empty() ? 0 : 1;
        return count;
    };
    {
        auto frame = lay();
        const auto* sample = frame.find(h.justifySample);
        check(sample && sample->lines.size() > 1 && frame.find(h.justifyAlign) && frame.find(h.justifyMethod) && frame.find(h.distributeName) && frame.find(h.distributePlain), "The comparison section is on the page");
        check(*h.justifySample.textAlign() == TextAlign::justify && stretched(frame) == sample->lines.size() - 1, "The paragraph starts justified: every line but the last is stretched");
        check(h.justifyAlign.draft().value.value() == OptionValue{"justify"} && h.justifyMethod.draft().value.value() == OptionValue{"automatic"}, "The dropdowns show the paragraph's settings");
        const auto* name = frame.find(h.distributeName); const auto* plain = frame.find(h.distributePlain);
        check(name->lines.size() == 1 && !name->lines[0].segments.empty() && plain->lines.size() == 1 && plain->lines[0].segments.empty(), "姓　名 is distributed, the plain one is not");
    }
    check(bool(h.justifyAlign.select({"left"})) && *h.justifySample.textAlign() == TextAlign::left && stretched(lay()) == 0, "Left-aligned: nothing stretches");
    check(bool(h.justifyAlign.select({"distribute"})) && *h.justifySample.textAlign() == TextAlign::distribute, "Distributed");
    {
        auto frame = lay();
        check(stretched(frame) == frame.find(h.justifySample)->lines.size(), "Even the last line is stretched");
    }
    check(bool(h.justifyAlign.select({"justify"})) && bool(h.justifyMethod.select({"spaces"})) && *h.justifySample.justifyMethod() == JustifyMethod::spaces, "Pick the spaces-only method");
    check(bool(h.justifyMethod.select({"per_character"})) && *h.justifySample.justifyMethod() == JustifyMethod::perCharacter && stretched(lay()) > 0, "And the per-character one");
    check(h.justifyMethod.select({"nonsense"}).status != Status::ok && *h.justifySample.justifyMethod() == JustifyMethod::perCharacter, "An unknown method is refused");
    check(bool(h.resetV04.invoke()) && form.ui.presentation() == PresentationSettings{}, "Resetting the presentation does not touch the comparison");
    auto settings = form.ui.presentation(); settings.justifyAsLeft = true;
    check(bool(form.ui.setPresentation(settings)) && stretched(lay()) == 0 && *h.justifySample.justifyMethod() == JustifyMethod::perCharacter, "The user setting turns the paragraph left-aligned without changing its properties");
}

void v04SliderUpdatesGlobalTextSize() {
    editForm::Form form(ids, true);
    check(bool(form.handles.tabs.activate({"v04"})), "V04 available");
    check(bool(form.handles.textSize.setValue(28)), "Slider accepts 28");
    check(form.ui.presentation().textSize == 28, "Slider applies size globally");
    test::FakeMeasure measure;
    for (const char* page : {"v01", "v02", "v03"}) {
        check(bool(form.handles.tabs.activate({page})), "Showcase page activates");
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll);
        const Placement* title = std::string_view(page) == "v01" ? frame.find(form.handles.title) :
            std::string_view(page) == "v02" ? frame.find(form.list->handles.title) : frame.find(form.handles.v03Title);
        check(title && title->textSize == 42, "V04 slider reflows each page title");
    }
    form.handles.tabs.activate({"v04"});
    check(form.handles.preview.describe()->value == u"\u7e41\u4e2d English \U0001f600 \U00020000 \u2764\ufe0f", "Preview contains sample text");
}

void v04FontChoiceAppliesAndReportsFallback() {
    editForm::Form form(ids, true, [](std::u16string_view family) {
        isoduo::FontResolution r; r.requested = family; r.requestedAvailable = family != u"Missing test font";
        r.primary = family.empty() || !r.requestedAvailable ? u"Segoe UI" : std::u16string(family); return r;
    });
    auto& h = form.handles; h.tabs.activate({"v04"});
    check(bool(h.font.select({"missing"})), "Select missing test font");
    check(form.ui.presentation().fontFamily == u"Missing test font", "Requested font remains stored");
    check(h.fontStatus.describe()->value.find(u"Segoe UI") != std::u16string::npos &&
          h.fontStatus.describe()->value.find(u"\u9810\u8a2d") != std::u16string::npos, "Fallback is visible");
    check(bool(h.font.select({"mingliu"})), "Select MingLiU");
    check(form.ui.presentation().fontFamily == u"MingLiU" && h.fontStatus.describe()->value.find(u"MingLiU") != std::u16string::npos, "Selected family is visible");
    check(bool(h.textSize.setValue(28)) && bool(h.dark.toggle()) && bool(h.resetV04.invoke()), "Reset presentation");
    check(form.ui.presentation() == PresentationSettings{} && h.font.draft().value.value() == OptionValue{"default"} && h.textSize.describe()->value == u"16" && h.dark.describe()->value != u"true", "Reset updates controls and settings");
}

void v04CatalogFontCanBeSelectedAndReset() {
    auto resolve = [](std::u16string_view family) {
        FontResolution result;
        result.requested = family;
        result.requestedAvailable = family.empty() || family == u"Trial Mono";
        result.primary = family == u"Trial Mono" ? u"Trial Mono" : u"Default Sans";
        return result;
    };
    editForm::Form form(ids, true, resolve, std::vector<FontFamily>{{u"Trial Mono", u"Trial Mono Display"}});
    auto& h = form.handles;
    check(bool(h.tabs.activate({"v04"})), "Open the presentation page");
    const auto items = h.font.describe()->items;
    const auto installed = std::find_if(items.begin(), items.end(), [](const auto& item) {
        return item.label == u"Trial Mono Display";
    });
    check(installed != items.end() && installed->enabled, "The installed catalog font is available");
    check(bool(h.font.select(installed->value)), "Choose the installed catalog font");
    check(form.ui.presentation().fontFamily == u"Trial Mono" && h.font.draft().value.value() == installed->value &&
          h.fontStatus.describe()->value.find(u"Trial Mono") != std::u16string::npos,
          "Catalog choice, presentation and font status agree");
    check(bool(h.font.select({"missing"})) && form.ui.presentation().fontFamily == u"Missing test font" &&
          h.fontStatus.describe()->value.find(u"Default Sans") != std::u16string::npos,
          "The missing font remains selectable and reports its fallback");
    check(bool(h.resetV04.invoke()) && form.ui.presentation().fontFamily.empty() &&
          h.font.draft().value.value() == OptionValue{"default"}, "Reset restores the default font");
}

void fontSwitchReflowsActiveComposition() {
    struct ChangingMeasure : TextMeasure {
        test::FakeMeasure base; bool wider = false;
        TextRun measure(std::u16string_view text, int pixels) override { return base.measure(text, wider ? pixels * 2 : pixels); }
        int lineHeight(int pixels) override { return base.lineHeight(pixels); }
    } measure;
    editForm::Form form(ids, true); auto& state = detail::access(form.ui);
    form.handles.name.setValue(u"ABCDE"); form.handles.name.focus(); form.handles.name.select(1, 3);
    check(bool(state.compositionUpdate(u"\u6e2c", 1)), "PS03 composition starts");
    auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, ids.name); };
    auto settings = form.ui.presentation();
    for (int change = 0; change != 4; ++change) {
        auto previous = draw().find(form.handles.name)->caretX(2);
        if (change == 0) settings.textSize = 28;
        if (change == 1) { settings.fontFamily = u"MingLiU"; measure.wider = true; }
        if (change == 2) settings.variant = ThemeVariant::dark();
        if (change == 3) settings.density = Density::compact;
        check(bool(form.ui.setPresentation(settings)), "PS03 presentation change accepted");
        auto frame = draw(); auto* field = frame.find(form.handles.name); auto* edit = state.edit(ids.name);
        check(field && edit && edit->composing && edit->preedit == u"\u6e2c" && edit->preeditCaret == 1 && form.handles.name.describe()->focused, "PS03 composition and focus survive presentation change");
        check(field && edit && frame.textOffset(form.handles.name, field->caretX(edit->begin())) == 1 &&
              frame.textOffset(form.handles.name, field->caretX(edit->visibleCaret())) == 2,
              "PS03 composition start and candidate caret hit their characters");
        if (change < 2) check(field && field->caretX(2) != previous, "PS03 size and font change metrics");
    }
}

void presentationReflowPreservesEditAndPopup() {
    editForm::Form form(ids, true); auto& h = form.handles; auto& state = detail::access(form.ui);
    test::FakeMeasure measure;
    auto draw = [&](std::string_view reveal) { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, reveal); };
    h.name.setValue(u"ABCDE"); h.name.focus(); h.name.select(1, 3); draw(ids.name);
    for (int size : {28, 12}) {
        auto settings = form.ui.presentation(); settings.textSize = size; check(bool(form.ui.setPresentation(settings)), "PS02 size accepted");
        auto frame = draw(ids.name); auto* field = frame.find(h.name); auto* session = state.edit(ids.name);
        check(field && session && session->text == u"ABCDE" && session->selection.anchor == 1 && session->selection.caret == 3 && h.name.describe()->focused, "PS02 selection and focus survive");
        check(frame.textOffset(h.name, field->caretX(2)) == 2, "PS02 new caret hit");
    }
    check(bool(state.compositionUpdate(u"xy", 1)), "PS03 composition starts");
    auto settings = form.ui.presentation(); settings.textSize = 28; form.ui.setPresentation(settings);
    auto frame = draw(ids.name); auto* session = state.edit(ids.name); auto* field = frame.find(h.name);
    check(field && session->composing && session->preedit == u"xy" && session->preeditCaret == 1 && frame.textOffset(h.name, field->caretX(session->visibleCaret())) == 2, "PS03 composition preserved");
    state.compositionCancel(); h.region.focus(); draw(ids.region); h.region.open(); state.keyDown({Key::down}); state.keyDown({Key::down});
    settings.textSize = 12; form.ui.setPresentation(settings); draw(ids.region); settings.textSize = 28; form.ui.setPresentation(settings); frame = draw(ids.region);
    check(h.region.describe()->expanded && h.region.describe()->candidate == OptionValue{"south"} && bool(test::popupInClient(frame, {0,0,640,480}, ids.region)), "PS04 popup follows reflow");
}
void unfocusedFieldPreservesVisibleCharacter() {
    editForm::Form form(ids, true); auto& h = form.handles;
    test::FakeMeasure measure;
    check(bool(h.name.setValue(u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")), "prepare long text");
    check(bool(h.name.focus()) && bool(h.name.select(30, 30)), "place caret inside long text");
    auto draw = [&] { return arrange(layout::component(h.name), form.ui, *appearance, measure, 220, 400, 1.f, form.scroll); };
    auto before = draw();
    auto first = [](const Placement& p) {
        for (const auto& [index, x] : p.run.stops) if (x >= p.offset - 0.01f) return index;
        return p.run.stops.back().first;
    };
    auto anchor = first(*before.find(h.name));
    check(anchor > 0 && bool(h.nameLabel.describe()) && bool(h.notices.focus()), "unfocus scrolled field");
    auto settings = form.ui.presentation(); settings.textSize = 28;
    check(bool(form.ui.setPresentation(settings)), "change text size");
    auto after = draw();
    check(first(*after.find(h.name)) == anchor, "text size keeps the first visible character");
}

void presentationReflowKeepsTooltipAndModal() {
    editForm::Form form(ids, true); auto& h = form.handles; auto& state = detail::access(form.ui);
    test::FakeMeasure measure; h.tabs.activate({"v03"}); state.windowActive(true); forceFocusIndicators(form.ui);
    h.progressHalf.focus(); state.tooltipTime(100000);
    arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.progressHalf.describe()->id);
    auto settings = form.ui.presentation(); settings.textSize = 28; form.ui.setPresentation(settings);
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.progressHalf.describe()->id);
    check(*h.progressTip.visible(), "PS05 tooltip remains visible");
    auto it = std::find_if(frame.overlays.begin(), frame.overlays.end(), [](const Placement& p) { return p.tooltip; });
    check(it != frame.overlays.end() && test::inside(it->bounds, {0,0,640,480}), "PS05 tooltip fits client");
    for (const auto& line : it->lines) check(test::inside(line.bounds, it->bounds), "PS05 tooltip line fits");
    check(frame.hit(it->bounds.x + 1, it->bounds.y + 1) != &*it, "PS05 tooltip is pointer transparent");
    h.dialogOpen.invoke(); settings.textSize = 12; settings.density = Density::compact; form.ui.setPresentation(settings);
    frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll);
    check(*h.dialog.isOpen() && h.dialogCancel.describe()->focused && h.progressFull.invoke().status == Status::blockedByModal, "PS06 modal and focus survive");
    state.keyDown({Key::tab, true}); check(h.dialogConfirm.describe()->focused, "PS06 reverse tab stays inside");
    state.keyDown({Key::tab}); check(h.dialogCancel.describe()->focused, "PS06 tab stays inside");
}

void ps07ListAnchorSurvivesSizeChange() {
    editForm::Form form(ids, true); form.handles.tabs.activate({"v02"});
    auto& page = *form.list; auto& h = page.handles; test::FakeMeasure measure;
    auto settings = form.ui.presentation(); settings.textSize = 12; form.ui.setPresentation(settings);
    h.hundred.invoke(); h.list.focus(); h.list.select({"T050"});
    auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll); };
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id); settings.textSize = 28; form.ui.setPresentation(settings); frame = draw();
    auto* list = frame.find(h.list);
    check(list && test::inside(list->options[49], list->clip) && page.selection() == OptionValue{"T050"}, "PS07 focused item remains visible");
    settings.textSize = 12; form.ui.setPresentation(settings); frame = draw();
    h.list.enable(false); h.list.enable(true);
    check(bool(h.hundred.focus()), "PS07 another component has focus while scrolling the list");
    frame = draw();
    list = frame.find(h.list); auto region = std::find_if(frame.regions.begin(), frame.regions.end(), [&](const ScrollRegion& r) { return r.state == &page.listScroll; });
    page.listScroll.offset += list->options[29].y - region->viewport.y;
    frame = draw(); list = frame.find(h.list);
    check(list->options[29].y == list->clip.y, "PS07 T030 starts at viewport top");
    settings.textSize = 28; form.ui.setPresentation(settings); frame = draw(); list = frame.find(h.list);
    check(list && list->options[29].y == list->clip.y, "PS07 unfocused top item stays anchored");
}

void ps08LargeTextWrapsWithoutOverlap() {
    editForm::Form form(ids, true); test::FakeMeasure measure;
    form.handles.name.setError(u"A long error message that must wrap onto multiple lines at the larger text size without overlapping the next control.");
    auto settings = form.ui.presentation(); settings.textSize = 28; form.ui.setPresentation(settings);
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, ids.name);
    auto* field = frame.find(form.handles.name); auto* next = frame.find(form.handles.notices);
    check(field && next && field->errorLines.size() > 1, "PS08 large error wraps");
    auto editor = field->textField.editor; editor.x += field->bounds.x; editor.y += field->bounds.y;
    for (const auto& line : field->labelLines) check(test::inside(line.bounds, field->bounds) && intersect(line.bounds, editor).h == 0, "PS08 label fits without editor overlap");
    for (const auto& line : field->descriptionLines) check(test::inside(line.bounds, field->bounds) && intersect(line.bounds, editor).h == 0, "PS08 help fits without editor overlap");
    for (const auto& line : field->errorLines) check(test::inside(line.bounds, field->bounds) && intersect(line.bounds, editor).h == 0, "PS08 error fits without editor overlap");
    check(next->bounds.y >= field->bounds.y + field->bounds.h, "PS08 following control does not overlap");

}

void longFieldLabelAndErrorKeepTheirAssociation() {
    Ui ui;
    const std::u16string title = u"這是一個需要在狹窄視窗內換行的非常長的欄位名稱";
    auto label = ui.addLabel({.id = "label", .value = title, .target = "field", .labelKind = LabelKind::name}).value.value();
    auto field = ui.addTextField({.id = "field", .value = u"value"}).value.value();
    auto error = ui.addLabel({.id = "error", .target = "field", .labelKind = LabelKind::error}).value.value();
    auto next = ui.addButton({.id = "next", .label = u"Next"}).value.value();
    auto root = layout::column({layout::component(field), layout::component(next)}, 12);
    test::FakeMeasure measure; ScrollState scroll;
    auto before = arrange(root, ui, *appearance, measure, 240, 600, 1.f, scroll);
    field.setError(u"這是一段很長的錯誤訊息，需要換行而且不能覆蓋後面的按鈕");
    auto after = arrange(root, ui, *appearance, measure, 240, 600, 1.f, scroll);
    auto* p = after.find(field); auto* b = after.find(next);
    check(p && b && p->labelLines.size() > 1 && p->errorLines.size() > 1, "Long field label and error both wrap");
    bool inside = true;
    for (const auto& line : p->labelLines) inside = inside && intersect(line.bounds, p->bounds) == line.bounds;
    for (const auto& line : p->errorLines) inside = inside && intersect(line.bounds, p->bounds) == line.bounds;
    check(inside && b->bounds.y >= p->bounds.y + p->bounds.h && b->bounds.y > before.find(next)->bounds.y,
          "Remeasured field keeps all lines inside and pushes the next control down");
    check(field.describe()->name == title && field.describe()->invalid && label.describe()->target == "field" &&
          error.describe()->target == "field" && error.describe()->value == field.describe()->error,
          "Public field and labels retain their semantic association");
}

void radioBoundariesUnderTheAppearance() {
    Ui ui;
    auto radio = ui.addRadioGroup({.id = "radio", .label = u"Mode"},
        {{{"first"}, u"First", false}, {{"middle"}, u"Middle", true}, {{"last"}, u"Last", false}}).value.value();
    auto next = ui.addButton({.id = "next", .label = u"Next"}).value.value();
    std::optional<OptionValue> selected;
    radio.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    radio.connectImmediate([&](const Change<RadioGroupHandle>& change) { selected = change.value; return Result{}; });
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::column({layout::component(radio), layout::component(next)}, 8);
    auto frame = arrange(root, ui, *appearance, measure, 240, 300, 1.f, scroll);
    check(frame.find(radio) && !radio.draft()->has_value() && radio.select({"first"}).status == Status::disabled &&
          radio.select({"last"}).status == Status::disabled, "Empty radio rejects disabled first and last options");
    radio.focus(); detail::access(ui).keyDown({Key::right}); detail::access(ui).keyDown({Key::right});
    check(selected == OptionValue{"middle"}, "Navigation chooses only the enabled option");
    detail::access(ui).keyDown({Key::tab});
    check(next.describe()->focused, "Tab exits the group");
    Ui allUi;
    auto all = allUi.addRadioGroup({.id = "all"}, {{{"a"}, u"A", false}, {{"b"}, u"B", false}}).value.value();
    auto after = allUi.addButton({.id = "after"}).value.value();
    all.connectSource({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    all.connectImmediate([](const Change<RadioGroupHandle>&) { return Result{}; });
    auto allFrame = arrange(layout::column({layout::component(all), layout::component(after)}), allUi, *appearance, measure, 240, 300, 1.f, scroll);
    all.focus(); detail::access(allUi).keyDown({Key::right}); detail::access(allUi).keyDown({Key::tab});
    check(allFrame.find(all) && !all.draft()->has_value() && after.describe()->focused, "All-disabled radio remains empty and can be exited");
}

void sliderNonDivisibleUnderTheAppearance() {
    Ui ui;
    auto slider = ui.addSlider({.id = "slider"}, {-3, 8, 4}).value.value();
    slider.connectSource({[] { return -3; }, [](std::function<void()>) { return [] {}; }});
    slider.connectConfirmed([](const Change<SliderHandle>&) { return Result{}; });
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::component(slider);
    auto frame = arrange(root, ui, *appearance, measure, 240, 120, 1.f, scroll);
    slider.setValue(3);
    check(frame.find(slider) && *slider.draft() == 5, "Non-divisible slider renders and midpoint rounds upward");
    slider.focus(); detail::access(ui).keyDown({Key::home});
    check(*slider.draft() == -3, "Home reaches minimum");
    detail::access(ui).keyDown({Key::end}); detail::access(ui).keyDown({Key::left});
    check(*slider.draft() == 5, "Left from maximum reaches last regular step");
    detail::access(ui).keyDown({Key::end});
    check(*slider.draft() == 8, "End reaches non-divisible maximum");
    int rejected = 0;
    for (auto range : {SliderRange{1, 1, 1}, SliderRange{2, 1, 1}, SliderRange{0, 1, 0}, SliderRange{0, 1, -1}})
        if (ui.addSlider({.id = "invalid"}, range).status == Status::invalidValue) ++rejected;
    check(rejected == 4, "Invalid ranges and steps are rejected");
}

void disabledTabsUnderTheAppearance() {
    Ui ui;
    auto first = ui.addButton({.id = "first"}).value.value();
    auto hidden = ui.addButton({.id = "hidden"}).value.value();
    auto tabs = ui.addTabs({.id = "tabs"}, {
        {{"one"}, u"One", true, {first}}, {{"blocked"}, u"Blocked", false, {}}, {{"two"}, u"Two", true, {hidden}}}).value.value();
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::column({layout::component(tabs), layout::component(first), layout::component(hidden)}), ui, *appearance, measure, 400, 300, 1.f, scroll);
    check(frame.find(tabs) && !frame.find(hidden) && hidden.invoke().status == Status::hidden &&
          tabs.activate({"blocked"}).status == Status::disabled, "Hidden page is not rendered or actionable and disabled tab rejects activation");
    tabs.focus(); detail::access(ui).keyDown({Key::right});
    check(*tabs.active() == OptionValue{"one"} && tabs.describe()->candidate == OptionValue{"two"}, "Keyboard skips disabled tab without activating it");
    detail::access(ui).keyDown({Key::enter});
    check(*tabs.active() == OptionValue{"two"} && first.invoke().status == Status::hidden, "Keyboard activates the enabled page and hides the previous page");
}

void keyboardCompletesTheThreePageJourney() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    auto tap = [&](Key k) {
        host.keyDown({k}); host.keyUp(k);
        arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, host.focused());
    };
    auto walk = [&](const std::string& target) {
        for (int i = 0; i < 100 && host.focused() != target; ++i) tap(Key::tab);
        return host.focused() == target;
    };
    check(walk(h.apply.describe()->id), "Keyboard reaches V01 Apply");
    tap(Key::enter);
    check(form.applies() == 1, "Keyboard applies the normal form once");
    check(walk(h.disabled.describe()->id), "Keyboard reaches disabled fixture");
    tap(Key::enter);
    check(!h.name.describe()->enabled && walk(h.normal.describe()->id), "Data is disabled and keyboard reaches recovery");
    tap(Key::enter);
    check(walk(h.tabs.describe()->id), "Keyboard reaches page tabs");
    tap(Key::right); tap(Key::enter);
    check(*h.tabs.active() == OptionValue{"v02"}, "Keyboard opens V02");
    auto& list = form.list->handles;
    check(walk(list.zero.describe()->id), "Keyboard reaches empty-list fixture");
    tap(Key::enter);
    check(walk(list.list.describe()->id), "Keyboard reaches empty list");
    tap(Key::down); tap(Key::space);
    check(!form.list->selection() && !list.list.describe()->candidate, "Empty list remains unselected");
    tap(Key::tab);
    check(!list.list.describe()->focused, "Keyboard leaves empty list");
    check(walk(list.twenty.describe()->id), "Keyboard reaches 20-item fixture");
    tap(Key::enter);
    check(walk(list.list.describe()->id), "Keyboard reenters populated list");
    tap(Key::end);
    check(form.list->selection() == OptionValue{"T020"}, "Keyboard selects the last item");
    check(walk(h.tabs.describe()->id), "Keyboard returns to tabs from V02");
    tap(Key::right); tap(Key::enter);
    check(*h.tabs.active() == OptionValue{"v03"} && walk(h.dialogOpen.describe()->id), "Keyboard opens V03 and reaches dialog opener");
    tap(Key::enter);
    check(h.dialogCancel.describe()->focused && *h.dialog.isOpen(), "Dialog starts on Cancel");
    tap(Key::tab); tap(Key::enter);
    check(!*h.dialog.isOpen() && form.progress() == 0 && form.dialogConfirmations() == 1 && h.dialogOpen.describe()->focused,
          "Keyboard confirms once and leaves the modal at its opener");
    check(walk(h.tabs.describe()->id), "Keyboard can leave V03");
}

void resetsReproduceCasesAndPreserveOtherPages() {
    editForm::Form form(ids, true); auto& h = form.handles;
    test::FakeMeasure measure;
    auto frame = [&] { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, detail::access(form.ui).focused()); };
    h.tabs.activate({"v02"}); auto& list = form.list->handles;
    list.search.setValue(u"共用範本"); list.list.select({"T006"});
    h.tabs.activate({"v03"}); h.progressFull.invoke();
    h.tabs.activate({"v01"}); h.name.setValue(u"draft"); h.reset.invoke();
    check(frame().find(h.name) && *h.name.draft() == u"測試使用者" && form.applies() == 0, "V01 reset restores initial form");
    h.name.setValue(u"工作站 A"); h.apply.invoke();
    check(form.applies() == 1 && form.applied() == u"工作站 A", "Representative V01 apply works again after reset");
    h.tabs.activate({"v02"});
    check(*list.search.draft() == u"共用範本" && form.list->selection() == OptionValue{"T006"}, "V01 reset retained V02 state");
    list.reset.invoke();
    check(frame().find(list.list) && list.search.draft()->empty() && !form.list->selection() && list.list.describe()->items.size() == 20, "V02 reset restores initial list");
    list.list.select({"T003"}); list.erase.invoke();
    check(!form.list->selection() && list.list.describe()->candidate == OptionValue{"T004"}, "Representative V02 deletion works again after reset");
    h.tabs.activate({"v03"});
    check(form.progress() == 100, "V02 reset retained V03 progress");
    h.resetV03.invoke();
    check(frame().find(h.progress) && form.progress() == 50 && form.dialogConfirmations() == 0, "V03 reset restores initial progress");
    h.dialogOpen.invoke(); h.dialogConfirm.invoke();
    check(form.progress() == 0 && form.dialogConfirmations() == 1, "Representative V03 confirmation works again after reset");
    h.tabs.activate({"v01"});
    check(form.applied() == u"工作站 A" && form.applies() == 1, "Other-page resets retain V01 applied result");
    h.tabs.activate({"v02"});
    check(list.list.describe()->items.size() == 19 && !form.list->selection(), "V03 reset retained V02 deletion");
}
// Arrange the page with the current appearance and check the role carriers follow the state.
void settle(editForm::Form& form) {
    test::FakeMeasure measure;
    for (int width : {960, 640}) {
        auto frame = arrange(form.page(), form.ui, *appearance, measure, width, 480, 1.f, form.scroll, detail::access(form.ui).focused());
        auto field = frame.find(form.handles.name);
        bool invalid = form.handles.name.describe()->invalid;
        check(field && field->labelLines.size() == 1 && field->errorLines.empty() != invalid,
              "Under this appearance the name carries its label, and an error exactly when invalid");
    }
}

void regionIsInTheFormConfirmationGroup() {
    editForm::Form form(ids);
    check(*form.handles.region.draft() == OptionValue{"north"} && form.regionApplied() == OptionValue{"north"}, "Region begins at North");
    check(bool(form.handles.region.select({"south"})) && form.regionApplied() == OptionValue{"north"}, "Selecting South changes only the draft");
    check(bool(form.handles.apply.invoke()) && form.regionApplied() == OptionValue{"south"} && form.applies() == 1, "Apply commits region with the form");
    form.handles.regionEmpty.invoke();
    check(!*form.handles.region.draft(), "Clearing options clears the draft");
    check(form.handles.apply.invoke().status == Status::invalidValue && form.handles.region.describe()->focused &&
          form.handles.region.describe()->invalid && !form.handles.name.describe()->invalid && form.applies() == 1, "Missing region is rejected and focuses its own error");
    form.handles.regionEmpty.invoke();
    check(!*form.handles.region.draft(), "Restoring options does not resurrect the old selection");
    form.handles.name.setValue(u"");
    form.handles.apply.invoke();
    check(form.handles.name.describe()->focused && form.handles.region.describe()->invalid, "Name error takes focus before region");
    form.handles.name.setValue(u"Valid"); form.handles.region.select({"central"});
    check(bool(form.handles.apply.invoke()) && form.regionApplied() == OptionValue{"central"} && form.applies() == 2, "Corrected region is explicitly selected and applied");
}

void allFiveFieldsApplyOnce() {
    editForm::Form form(ids);
    auto densitySettings = form.ui.presentation(); densitySettings.density = behaviorDensity;
    check(bool(form.ui.setPresentation(densitySettings)), "Behavior density accepted");
    form.handles.name.setValue(u"\u5de5\u4f5c\u7ad9 A");
    form.handles.notices.toggle(); form.handles.mode.select({"focus"});
    form.handles.region.select({"south"}); form.handles.volume.setValue(65);
    check(form.applies() == 0 && form.regionApplied() == OptionValue{"north"} && form.volumeApplied() == 40,
          "Five edits remain drafts before Apply");
    check(bool(form.handles.apply.invoke()) && form.applies() == 1 && form.applied() == u"\u5de5\u4f5c\u7ad9 A" &&
          !form.noticesApplied() && form.modeApplied() == OptionValue{"focus"} &&
          form.regionApplied() == OptionValue{"south"} && form.volumeApplied() == 65,
          "F01 applies all five values in one action");
}

void volumeIsAConfirmedFormField() {
    editForm::Form form(ids);
    check(*form.handles.volume.draft() == 40 && form.volumeApplied() == 40 && form.handles.volume.describe()->name == u"\u97f3\u91cf", "Volume starts at 40 with its accessible name");
    form.handles.volume.setValue(75);
    check(*form.handles.volume.draft() == 75 && form.volumeApplied() == 40 &&
          form.handles.draft.describe()->value.find(u"75") != std::u16string::npos, "Volume is shown as an unapplied draft");
    check(bool(form.handles.apply.invoke()) && form.volumeApplied() == 75 && form.applies() == 1 &&
          form.handles.result.describe()->value.find(u"75") != std::u16string::npos, "Apply commits and displays volume");
    form.handles.reset.invoke();
    check(*form.handles.volume.draft() == 40 && form.volumeApplied() == 40 && form.applies() == 0, "Reset restores volume");
}

void volumeConflictOffersLatest() {
    editForm::Form form(ids);
    form.handles.volume.setValue(55);
    form.updateVolumeSource(70);
    check(*form.handles.volume.draft() == 55 && form.handles.volume.describe()->sourceConflict &&
          form.handles.volumeLatest.describe()->enabled, "Volume conflict preserves the draft and enables use latest");
    check(bool(form.handles.volumeLatest.invoke()) && *form.handles.volume.draft() == 70 &&
          !form.handles.volume.describe()->sourceConflict, "Use latest discards the volume draft");
}

void checkboxConflictIsVisibleAndCanUseLatest() {
    bool source = true; std::function<void()> changed;
    Ui ui;
    auto box = ui.addCheckbox({.id = "check", .label = u"Check"}).value.value();
    box.connectSource({[&] { return source; }, [&](std::function<void()> cb) { changed = std::move(cb); return [] {}; }});
    box.connectConfirmed([&](const Change<CheckboxHandle>&) {
        box.toggle(); source = false; changed();
        return Result{Status::invalidValue, u"Rejected"};
    });
    box.toggle(); box.confirm();
    test::FakeMeasure measure; ScrollState scroll;
    auto frame = arrange(layout::component(box), ui, *appearance, measure, 400, 400, 1.f, scroll);
    const auto* placed = frame.find(box);
    check(box.describe()->sourceConflict && *box.draft() && placed && !placed->descriptionLines.empty(),
          "A source update during a rejected confirmation keeps the draft and displays a conflict");
    check(box.useLatest() && !*box.draft() && !box.describe()->sourceConflict,
          "Use latest adopts the checkbox source");
}

void assembledCheckboxConflictOffersLatest() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    form.rejectNextApplyWithNoticesSourceChange();
    check(bool(h.notices.toggle()), "X04 prepare checkbox draft");
    check(h.apply.invoke().status == Status::invalidValue && h.notices.describe()->sourceConflict &&
          *h.notices.draft() && !form.noticesApplied() && h.noticesLatest.describe()->enabled,
          "X04 assembled rejected apply exposes the checkbox conflict and latest action");
    test::FakeMeasure measure;
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll, h.notices.describe()->id);
    auto* box = frame.find(h.notices);
    check(box && !box->descriptionLines.empty(), "X04 assembled checkbox draws conflict wording");
    check(bool(h.noticesLatest.invoke()) && !*h.notices.draft() && !h.notices.describe()->sourceConflict,
          "X04 assembled latest action adopts the external checkbox value");
}

void optionConflictsAreVisibleAndCanUseLatest() {
    editForm::Form form(ids);
    auto& h = form.handles;
    h.mode.select({"focus"}); h.region.select({"central"});
    form.updateModeSource({"offline"}); form.updateRegionSource({"south"});
    test::FakeMeasure measure; ScrollState scroll;
    auto modeFrame = arrange(layout::component(h.mode), form.ui, *appearance, measure, 400, 400, 1.f, scroll);
    auto regionFrame = arrange(layout::component(h.region), form.ui, *appearance, measure, 400, 400, 1.f, scroll);
    auto mode = modeFrame.find(h.mode); auto region = regionFrame.find(h.region);
    check(h.mode.describe()->sourceConflict && h.region.describe()->sourceConflict && mode && region &&
          !mode->descriptionLines.empty() && !region->descriptionLines.empty() &&
          h.modeLatest.describe()->enabled && h.regionLatest.describe()->enabled,
          "Conflicts preserve drafts, show notices and enable both latest actions");
    check(h.modeLatest.invoke() && h.regionLatest.invoke() && *h.mode.draft() == OptionValue{"offline"} &&
          *h.region.draft() == OptionValue{"south"} && !h.mode.describe()->sourceConflict && !h.region.describe()->sourceConflict,
          "Each latest action adopts its own source value");
}

void initialValuesApplyAndReset() {
    editForm::Form form(ids);
    auto& ui = form.ui;
    check(form.handles.name.describe()->value == u"測試使用者" && form.applied() == u"測試使用者" && form.applies() == 0, "Initial draft, result and count");
    form.handles.name.setValue( u"工作站 A");
    check(form.applied() == u"測試使用者" && form.handles.result.describe()->value.find(u"測試使用者") != std::u16string::npos,
          "Editing changes only the draft");
    check(form.handles.draft.describe()->value.find(u"工作站 A") != std::u16string::npos, "Current draft is displayed");
    check(bool(form.handles.apply.invoke()) && form.applied() == u"工作站 A" && form.applies() == 1, "Valid apply updates result once");
    form.handles.name.focus();
    form.handles.name.select( 6, 6);
    form.handles.name.paste(u"，這是一段會超出欄位寬度的很長很長的名稱");
    test::FakeMeasure measure;
    arrange(form.page(), ui, *appearance, measure, 360, 240, 1.f, form.scroll, ids.apply);
    check(detail::access(ui).textOffset(ids.name) > 0 && form.scroll.offset > 0, "Long draft and small window scroll the field and the page");
    form.handles.disabled.invoke();
    check(bool(form.handles.reset.invoke()), "Reset works while the data area is disabled");
    check(form.handles.name.describe()->value == u"測試使用者" && form.applied() == u"測試使用者" && form.applies() == 0, "Reset restores initial values");
    check(!form.handles.name.undo() && detail::access(ui).textOffset(ids.name) == 0 && form.scroll.offset == 0, "Reset clears history and display offsets");
    check(form.handles.reset.describe()->focused && form.handles.name.describe()->enabled, "Focus stays on reset; the data area is enabled again");
    settle(form);
}

void blankNameErrorFollowsBlurAndApply() {
    editForm::Form form(ids);
    auto& ui = form.ui;
    form.handles.name.focus();
    form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size()));
    form.handles.name.paste(u"　 ");
    detail::access(ui).keyDown(key(Key::tab));
    auto name = form.handles.name.describe();
    check(name->invalid && name->error == u"請填寫顯示名稱" && form.handles.nameError.describe()->value == name->error, "Blur on a blank name shows its error");
    check(form.handles.nameError.describe()->target == ids.name, "The error is associated with the name field");
    auto result = form.handles.apply.invoke();
    check(result.status == Status::invalidValue && form.handles.name.describe()->focused, "Invalid apply is rejected and focuses the name");
    check(form.applied() == u"測試使用者" && form.applies() == 0, "Invalid apply keeps the old result and count");
    form.handles.name.paste(u"有效名稱");
    check(!form.handles.name.describe()->invalid, "Fixing the name clears the error");
    form.handles.apply.invoke();
    check(form.applies() == 1 && form.applied() == u"　 有效名稱", "Successful apply adds exactly one");
    form.handles.name.focus();
    detail::access(ui).keyDown(key(Key::enter));
    check(form.applies() == 1, "Enter in the name field does not apply");
    settle(form);
}

void applyButtonStartsOnce() {
    editForm::Form form(ids);
    auto& ui = form.ui;
    detail::access(ui).pointerDown(ids.apply);
    detail::access(ui).pointerUp({});
    check(form.applies() == 0, "Pressing apply then releasing outside cancels");
    detail::access(ui).pointerDown(ids.apply);
    detail::access(ui).pointerUp(ids.apply);
    detail::access(ui).keyDown(key(Key::space));
    detail::access(ui).keyDown({.key = Key::space, .repeat = true});
    detail::access(ui).keyUp(Key::space);
    detail::access(ui).keyDown(key(Key::enter));
    detail::access(ui).keyDown({.key = Key::enter, .repeat = true});
    check(form.applies() == 3, "Click, Space and Enter each apply once; holding a key does not repeat");
    settle(form);
}

void pasteUndoRedoNeverTouchesApplied() {
    editForm::Form form(ids);
    form.handles.name.focus();
    form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size()));
    form.handles.name.paste( u"Alpha\nBeta");
    check(form.handles.name.describe()->value == u"Alpha Beta", "Pasted line break becomes a space");
    form.handles.name.undo();
    check(form.handles.name.describe()->value == u"測試使用者", "Undo restores the original name");
    form.handles.name.redo();
    check(form.handles.name.describe()->value == u"Alpha Beta" && form.applied() == u"測試使用者" && form.applies() == 0,
          "Redo restores the paste; the applied result never changes");
    settle(form);
}

void readOnlyAndDisabledFixturesRestrictAndRecover() {
    editForm::Form form(ids);
    auto& ui = form.ui;
    form.handles.name.focus();
    form.handles.name.paste(u"!");
    form.handles.readOnly.invoke();
    check(form.handles.name.describe()->readOnly && form.handles.state.describe()->value.find(u"唯讀") != std::u16string::npos,
          "Read-only state has readable text, not just color");
    form.handles.name.focus();
    check(form.handles.name.paste(u"x").status == Status::readOnly && form.handles.name.undo().status == Status::readOnly,
          "Read-only name refuses input and undo");
    form.handles.disabled.invoke();
    check(form.handles.state.describe()->value.find(u"停用") != std::u16string::npos, "Disabled state has readable text");
    std::vector<Status> refused{form.handles.name.focus().status, detail::access(ui).pointerDown(ids.apply).status,
                                form.handles.name.setValue( u"x").status, form.handles.apply.invoke().status};
    check(refused == std::vector<Status>(4, Status::disabled) && form.applies() == 0, "Pointer, keyboard and semantic retries are refused");
    auto order = detail::access(ui).focusOrder();
    check(std::find(order.begin(), order.end(), ids.normal) != order.end() && std::find(order.begin(), order.end(), ids.name) == order.end(),
          "Recovery entry stays reachable while the name leaves the Tab order");
    form.handles.normal.invoke();
    check(form.handles.name.describe()->value == u"!測試使用者" && bool(form.handles.name.undo()), "Recovery keeps the draft and its history");
    settle(form);
}

void keyboardReachesEveryEntryAndSemanticsAnswer() {
    editForm::Form form(ids);
    auto& ui = form.ui;
    std::vector<Id> seen;
    for (int i = 0; i < 15; ++i) { detail::access(ui).keyDown(key(Key::tab)); seen.push_back(detail::access(ui).focused()); }
    std::vector<Id> expected{ids.normal, ids.readOnly, ids.disabled, ids.error, ids.longText, ids.reset, ids.name, ids.notices, ids.mode, ids.region, ids.volume, ids.offlineDisabled, ids.regionEmpty, ids.apply, ids.normal};
    check(seen == expected, "Tab reaches fixtures, name and apply, then wraps");
    detail::access(ui).keyDown(key(Key::tab, true));
    check(form.handles.apply.describe()->focused, "Shift+Tab walks back");
    auto name = form.handles.name.describe();
    check(name->kind == ComponentKind::textField && name->name == u"顯示名稱" && name->value == u"測試使用者" && !name->focused,
          "Name field answers identity, type, name and value");
    auto apply = form.handles.apply.describe();
    check(apply->kind == ComponentKind::button && apply->name == u"套用" && apply->focused, "Apply answers its name and focus");
    check(!form.handles.title.describe()->focusable && !form.handles.result.describe()->focusable, "Display text stays out of the Tab order");
    form.handles.error.invoke();
    check(form.handles.name.describe()->invalid && form.handles.name.describe()->focused, "Error fixture shows the name error and focuses it");
    settle(form);
}

void sourceUpdatesOfferTheLatestValue() {
    editForm::Form form(ids);
    form.handles.name.setValue( u"my draft");
    form.updateSource(u"remote value");
    check(form.handles.name.describe()->value == u"my draft" && form.handles.state.describe()->value.find(u"\u4f86\u6e90\u5df2\u66f4\u65b0") != std::u16string::npos,
          "An external change retains the draft and displays a conflict");
    check(bool(form.handles.latest.invoke()) && form.handles.name.describe()->value == u"remote value",
          "The user can discard the draft in favor of the latest source value");
}

void checkboxAndRadioAreAppliedTogether() {
    editForm::Form form(ids);
    check(form.handles.notices.describe()->name == u"\u555f\u7528\u63d0\u793a" &&
          form.handles.mode.describe()->items[0].label == u"\u4e00\u822c", "New controls retain their Traditional Chinese labels");
    check(*form.handles.notices.draft() && *form.handles.mode.draft() == OptionValue{"normal"},
          "Notices and normal mode are initially selected");
    form.handles.notices.focus();
    detail::access(form.ui).keyDown({.key = Key::space});
    detail::access(form.ui).keyUp(Key::space);
    form.handles.mode.focus();
    detail::access(form.ui).keyDown({.key = Key::right});
    check(!*form.handles.notices.draft() && *form.handles.mode.draft() == OptionValue{"focus"} && form.noticesApplied(),
          "Keyboard changes drafts while the applied result stays unchanged");
    check(bool(form.handles.apply.invoke()) && !form.noticesApplied() && form.modeApplied() == OptionValue{"focus"},
          "Apply commits the boolean and radio drafts together");
    form.handles.offlineDisabled.invoke();
    check(form.handles.mode.select({"offline"}).status == Status::disabled, "Offline can be disabled");
    form.handles.reset.invoke();
    check(form.noticesApplied() && form.modeApplied() == OptionValue{"normal"} && *form.handles.notices.draft(),
          "Reset restores the initial checkbox and radio values");
}

void v02EmptySingleAndNavigation() {
    editForm::Form form(ids, true);
    auto densitySettings = form.ui.presentation(); densitySettings.density = behaviorDensity;
    check(bool(form.ui.setPresentation(densitySettings)), "Behavior density accepted");
    form.handles.tabs.activate({"v02"});
    auto& page = *form.list;
    auto& h = page.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    h.zero.invoke(); h.list.focus();
    host.keyDown({Key::down}); host.keyDown({Key::space});
    auto empty = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    check(empty.find(h.list) && h.list.describe()->focused && !h.list.describe()->candidate && !page.selection(),
          "V02 empty list is rendered, focusable and has no selection");
    host.keyDown({Key::tab});
    check(!h.list.describe()->focused, "Tab leaves an empty V02 list");
    h.one.invoke(); h.list.focus(); host.keyDown({Key::space}); host.keyDown({Key::end});
    check(page.selection() == OptionValue{"T001"}, "Single-item endpoints stay on T001");
    h.twenty.invoke(); h.list.focus();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    const auto* item = frame.find(h.list);
    check(item && item->options.size() == 20, "Twenty V02 rows are arranged");
    auto row = item->options[2];
    check(frame.optionAt(*item, row.x + 3, row.y + row.h / 2) == 2, "T003 has a visible pointer target");
    host.pointerDown(item->id, 2); host.pointerUp(item->id, 2);
    check(page.selection() == OptionValue{"T003"}, "Pointer selects T003");
    auto visible = [&](std::size_t index) {
        auto next = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
        auto* list = next.find(h.list);
        return list && index < list->options.size() && test::inside(list->options[index], list->clip);
    };
    host.keyDown({Key::down});
    check(page.selection() == OptionValue{"T004"} && visible(3), "Down selects and reveals T004");
    host.keyDown({Key::end});
    check(page.selection() == OptionValue{"T020"} && visible(19), "End selects and reveals T020");
    host.keyDown({Key::home});
    check(page.selection() == OptionValue{"T001"} && visible(0), "Home selects and reveals T001");
}

void v02IdentityFilterAndDelete() {
    editForm::Form form(ids, true);
    form.handles.tabs.activate({"v02"});
    auto& page = *form.list; auto& h = page.handles;
    test::FakeMeasure measure;
    h.list.focus(); h.list.select({"T006"}); h.sort.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    check(frame.find(h.list) && page.selection() == OptionValue{"T006"} && h.detail.describe()->value.find(u"T006") != std::u16string::npos,
          "Reverse sorting retains the selected duplicate identity in the detail");
    h.sort.invoke();
    check(page.selection() == OptionValue{"T006"}, "Restoring sort preserves T006");
    h.list.select({"T003"}); h.search.setValue(u"範本 20");
    check(!page.selection() && h.list.describe()->items.size() == 1 && h.list.describe()->items[0].value == OptionValue{"T020"},
          "Filtering T003 out clears selection and leaves T020");
    h.search.setValue(u"");
    check(!page.selection() && h.list.describe()->items.size() == 20, "Clearing the filter does not restore selection");
    h.list.focus(); h.list.select({"T003"}); h.erase.invoke();
    const auto remaining = h.list.describe()->items;
    bool identities = remaining.size() == 19;
    for (int n = 1; n <= 20 && identities; ++n) {
        if (n == 3) continue;
        auto expected = "T" + std::string(n < 10 ? "00" : "0") + std::to_string(n);
        auto index = static_cast<std::size_t>(n < 3 ? n - 1 : n - 2);
        identities = remaining[index].value == OptionValue{expected};
    }
    check(!page.selection() && h.list.describe()->candidate == OptionValue{"T004"} && h.list.describe()->items.size() == 19 &&
          identities && h.erase.invoke().status == Status::disabled, "Deleting T003 keeps all other identities, clears selection and disables deletion");
    h.search.focus(); detail::access(form.ui).keyDown({Key::tab});
    check(h.list.describe()->focused && !page.selection() && h.list.describe()->candidate == OptionValue{"T001"},
          "Tab reentry starts at T001 without selecting it");
}

void v02DisabledAndFilteredNeighbors() {
    editForm::Form form(ids, true);
    form.handles.tabs.activate({"v02"});
    auto& page = *form.list; auto& h = page.handles;
    test::FakeMeasure measure;
    h.list.focus(); h.list.select({"T008"}); h.disableEight.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll, h.list.describe()->id);
    auto options = h.list.describe()->items;
    bool identities = options.size() == 20;
    for (int n = 1; n <= 20 && identities; ++n) {
        auto expected = "T" + std::string(n < 10 ? "00" : "0") + std::to_string(n);
        identities = options[static_cast<std::size_t>(n - 1)].value == OptionValue{expected};
    }
    check(frame.find(h.list) && !page.selection() && h.list.describe()->candidate == OptionValue{"T009"} &&
          h.list.select({"T008"}).status == Status::disabled && identities,
          "Disabling selected T008 clears it, moves to T009 and keeps every identity");
    h.reset.invoke(); h.hundred.invoke(); h.list.focus(); h.list.select({"T009"}); h.search.setValue(u"範本 1");
    check(!page.selection() && h.list.describe()->candidate == OptionValue{"T010"}, "Filtering recovers the next surviving identity T010");
}

void pagesKeepIndependentDraftsHistoryAndScroll() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    test::FakeMeasure measure;
    h.name.setValue(u"a very long draft that needs horizontal scrolling in a narrow field");
    h.name.focus(); h.name.select(66, 66);
    h.longText.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll, ids.name);
    auto offset = frame.find(h.name)->offset;
    form.scroll.offset = frame.maximum;
    int savedScroll = form.scroll.offset;
    check(offset > 0 && savedScroll > 0, "V01 has horizontal and vertical offsets to preserve");
    check(bool(h.tabs.activate({"v02"})), "Open V02");
    check(h.name.setValue(u"blocked").status == Status::hidden, "Hidden V01 cannot be edited");
    auto& list = form.list->handles;
    list.search.setValue(u"\u5171\u7528\u7bc4\u672c"); list.list.select({"T006"});
    check(bool(h.tabs.activate({"v01"})), "Return to V01");
    check(*h.name.draft() == u"a very long draft that needs horizontal scrolling in a narrow field", "V01 draft survives switch");
    check(form.applied() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "V01 applied value survives switch");
    check(form.scroll.offset == savedScroll, "V01 vertical offset survives switch");
    frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll);
    check(frame.find(h.name)->offset == offset, "V01 horizontal offset survives switch");
    h.tabs.activate({"v02"});
    check(*list.search.draft() == u"\u5171\u7528\u7bc4\u672c" && form.list->selection() == OptionValue{"T006"}, "V02 search and T006 survive a switch");
    list.reset.invoke();
    check(list.reset.describe()->focused && list.search.draft()->empty() && !form.list->selection() && list.list.describe()->items.size() == 20, "V02 reset restores the real list and keeps focus");
    list.hundred.invoke(); list.list.focus(); list.list.select({"T100"});
    auto listFrame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll, list.list.describe()->id);
    auto row = listFrame.find(list.list)->options.back();
    auto clip = listFrame.find(list.list)->clip;
    int listOuter = form.scroll.offset, listInner = form.list->listScroll.offset;
    check(listOuter > 0 && listInner > 0 && row.y >= clip.y && row.y + row.h <= clip.y + clip.h,
          "V02 target is fully revealed by both viewports");
    h.tabs.activate({"v01"}); h.tabs.activate({"v02"});
    check(form.scroll.offset == listOuter && form.list->listScroll.offset == listInner &&
          form.list->selection() == OptionValue{"T100"}, "Both V02 offsets and selection survive switching");
    list.reset.invoke();
    check(form.scroll.offset == 0 && form.list->listScroll.offset == 0 && !form.list->selection(),
          "Reset restores both V02 offsets and clears selection");
    h.tabs.activate({"v01"});
    frame = arrange(form.page(), form.ui, *appearance, measure, 360, 180, 1.f, form.scroll);
    check(form.scroll.offset == savedScroll && frame.find(h.name)->offset == offset && bool(h.name.undo()) &&
          *h.name.draft() == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "V02 reset preserves V01 offsets and undo history");
}

void tooltipDelayWaitsForFocusAfterRendererRecovery() {
    Ui ui;
    auto button = ui.addButton({.id = "pending", .label = u"Pending"}).value.value();
    auto tip = ui.addTooltip("pending.tip", button, u"Help", 1000).value.value();
    auto& state = detail::access(ui);
    std::uint64_t now = 1000;
    state.tooltipClock([&] { return now; });
    state.windowActive(true);
    state.setSystemKeyboardCues(true);
    check(bool(button.focus()) && state.tooltipDeadline() == 2000, "Focus starts the tooltip delay");
    now = 1400;
    state.rendererFailed(true);
    state.windowActive(false);
    now = 6000;
    state.rendererFailed(false);
    state.frameVisibility({{"pending", true}});
    check(!state.tooltipDeadline() && !*tip.visible(), "An inactive rebuilt frame does not restart the delay");
    now = 6200;
    state.windowActive(true);
    check(state.tooltipDeadline() == 6800, "Focus return resumes the remaining 600 ms");
    state.tooltipTime(now = 6799);
    check(!*tip.visible(), "Tooltip remains pending until its restored deadline");
    state.tooltipTime(now = 6800);
    check(*tip.visible(), "Tooltip appears after the remaining delay");
}

void tooltipKeyboardDelayAndEscape() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"50%"}).value.value();
    auto other = ui.addButton({.id = "other", .label = u"Other"}).value.value();
    auto tip = ui.addTooltip("form.title.pages.v03.half.tip", source, u"Set progress to 50%", 100).value.value();
    check(source.describe()->description == u"Set progress to 50%", "The source exposes the tooltip as its semantic description");
    auto& host = detail::access(ui);
    host.tooltipTime(1000);
    host.windowActive(true);
    host.keyDown({Key::tab});
    host.tooltipTime(1099);
    check(tip.visible() && !*tip.visible(), "Focus waits for the configured delay");
    host.tooltipTime(1100);
    check(tip.visible() && *tip.visible() && source.describe()->focused, "Tooltip appears without taking focus");
    host.keyDown({Key::escape});
    host.tooltipTime(1300);
    check(!*tip.visible() && source.describe()->focused, "Escape hides for the current focus visit");
    other.focus(); source.focus();
    host.tooltipTime(1400);
    check(*tip.visible(), "A new focus visit can show the tooltip again");
}

void tooltipPointerVisitCanRestartAfterEscape() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"50%"}).value.value();
    auto tip = ui.addTooltip("form.title.pages.v03.half.tip", source, u"Set progress to 50%", 100).value.value();
    auto& host = detail::access(ui);
    host.tooltipTime(0); host.windowActive(true);
    host.tooltipHover("source"); host.tooltipTime(100);
    check(*tip.visible(), "Pointer dwell displays the tooltip");
    host.keyDown({Key::escape}); host.tooltipTime(500);
    check(!*tip.visible(), "Escape suppresses the same pointer visit");
    host.tooltipHover(""); host.tooltipHover("source"); host.tooltipTime(599);
    check(!*tip.visible(), "A fresh pointer visit waits again");
    host.tooltipTime(600);
    check(*tip.visible(), "A fresh pointer visit may show again");
    host.tooltipHover("");
    check(!*tip.visible(), "Leaving the source closes the tooltip");
}

void tooltipPointerFocusEndsOnLeave() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"50%"}).value.value();
    auto tip = ui.addTooltip("form.title.pages.v03.half.tip", source, u"Help", 100).value.value();
    auto& host = detail::access(ui);
    host.tooltipTime(0); host.windowActive(true);
    host.tooltipHover("source");
    host.pointerDown("source"); host.pointerUp("source");
    host.tooltipTime(100);
    check(*tip.visible() && source.describe()->focused, "Pointer dwell shows the focused source tooltip");
    host.tooltipHover("");
    check(!*tip.visible() && source.describe()->focused, "Leaving after a pointer click hides the tooltip without changing focus");
    host.keyDown({Key::tab});
    host.tooltipTime(200);
    check(*tip.visible(), "Keyboard focus makes the source eligible again");
}

void tooltipWindowModalAndSourceLifetime() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"Source"}).value.value();
    auto tip = ui.addTooltip("form.title.pages.v03.half.tip", source, u"Help", 100).value.value();
    auto& host = detail::access(ui);
    host.tooltipTime(0); host.windowActive(true); forceFocusIndicators(ui); source.focus(); host.tooltipTime(100);
    check(*tip.visible(), "Tooltip becomes visible");
    host.windowActive(false); host.tooltipTime(1000);
    check(!*tip.visible(), "Window deactivation cancels the tooltip");
    host.windowActive(true); host.tooltipTime(1099);
    check(!*tip.visible(), "Window reactivation starts a fresh delay");
    host.tooltipTime(1100);
    check(*tip.visible() && source.describe()->focused, "Tooltip returns without moving focus");
    host.modal(true);
    check(!*tip.visible(), "Opening a modal closes the tooltip");
    host.modal(false); host.tooltipTime(1200);
    check(*tip.visible(), "Eligibility is recalculated after a modal closes");
    source.enable(false); host.tooltipTime(1400);
    check(!*tip.visible(), "Disabling the source cancels pending and visible tooltips");
    source.enable(true); source.focus(); host.tooltipTime(1450); source.remove(); host.tooltipTime(2000);
    check(tip.visible().status == Status::notFound, "Removal invalidates the tooltip and its pending timer");
}

void tooltipLayoutIsReadableAndPointerTransparent() {
    Ui ui;
    auto source = ui.addButton({.id = "source", .label = u"Source"}).value.value();
    auto tip = ui.addTooltip("form.title.pages.v03.half.tip", source, u"An explanation that wraps near a window edge", 0).value.value();
    auto& host = detail::access(ui);
    host.windowActive(true); forceFocusIndicators(ui); source.focus();
    test::FakeMeasure measure; ScrollState scroll;
    auto root = layout::column({layout::component(source)}, 0, 8);
    auto frame = arrange(root, ui, *appearance, measure, 160, 110, 1.f, scroll);
    check(*tip.visible() && frame.overlays.size() == 1, "A visible tooltip produces an overlay");
    auto& popup = frame.overlays.front();
    check(test::inside(popup.bounds, {0, 0, 160, 110}) && popup.lines.size() > 1,
          "Tooltip wraps and stays inside a narrow window");
    for (const auto& line : popup.lines)
        check(test::inside(line.bounds, popup.bounds), "Each line remains within the tooltip");
    check(frame.hit(popup.bounds.x + 1, popup.bounds.y + 1) != &popup, "Tooltip does not intercept pointer input");
}

struct PaintRoleProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    detail::RecordingCanvas recording{960, 4000, {0, 0, 0}};
    std::vector<std::pair<detail::PaintRole,Rect>> emitted;
    void role(detail::PaintRole value) override { recording.currentRole=value; }
    void fill(Rect r, Color color) override { emitted.emplace_back(recording.currentRole,r); recording.fill(r,color); }
    void rounded(Rect r, int radius, Color color) override { emitted.emplace_back(recording.currentRole,r); recording.rounded(r,radius,color); }
    void clip(Rect r) override { recording.clip=intersect(recording.clip,r); }
    void text(std::u16string_view value, int size, int x, int y, Color color) override {
        if(value.empty()) return;
        emitted.emplace_back(recording.currentRole,Rect{x,y,1,size});
        auto page = std::make_shared<detail::AtlasPage>(); page->width = page->height = 1; page->pixels = {255};
        recording.glyph({x,y,1,1},color,page,{0,0,1,1});
    }
    int count(detail::PaintRole role) const { return static_cast<int>(std::count_if(emitted.begin(),emitted.end(),[&](const auto& item){return (static_cast<unsigned>(item.first) & static_cast<unsigned>(role)) != 0;})); }
};
#include "focus_visibility.hpp"
void x04ScrollableTextInventory() {
    Ui ui; forceFocusIndicators(ui);
    auto longText = ui.addLabel({.id="long", .value=std::u16string(4000, u'x')}).value.value();
    auto shortText = ui.addLabel({.id="short", .value=u"short"}).value.value();
    auto action = ui.addButton({.id="action", .label=u"Action"}).value.value();
    ScrollState outer, longScroll, shortScroll;
    auto page = layout::scroll(layout::column({layout::scrollText(longText, longScroll, 120),
        layout::scrollText(shortText, shortScroll, 120), layout::component(action)}));
    test::FakeMeasure measure;
    auto frame = arrange(page, ui, *appearance, measure, 640, 480, 1.f, outer);
    auto fits = layout::scroll(layout::column({layout::scrollText(longText, longScroll, 10000)}));
    arrange(fits, ui, *appearance, measure, 640, 480, 1.f, outer);
    check(!x04::focusApplicable(*longText.describe()), "X04 excludes the same text when it fits");
    frame = arrange(page, ui, *appearance, measure, 640, 480, 1.f, outer);
    std::vector<std::string> visited;
    for (const auto& entry : x04::inventory(ui)) {
        auto semantic = x04::describe(entry.handle);
        if (!semantic || !x04::focusApplicable(*semantic)) continue;
        check(x04::focus(ui, entry.handle), "X04 generic focus driver accepts focusable component");
        semantic = x04::describe(entry.handle);
        auto item = x04::placement(frame, entry.handle);
        check(item && semantic->focused, "X04 focus sweep finds the focused placement");
        PaintRoleProbe paint(detail::access(ui), *item, *semantic, *appearance, 1.f, true);
        detail::componentType(item->kind).paint(paint);
        check(paint.count(detail::PaintRole::focusIndicator) > 0, "X04 focus sweep observes the focus cue");
        visited.push_back(semantic->id);
    }
    check(std::find(visited.begin(), visited.end(), "long") != visited.end(), "X04 includes overflowing text");
    check(std::find(visited.begin(), visited.end(), "short") == visited.end(), "X04 excludes fitting text");
    check(std::find(visited.begin(), visited.end(), "action") != visited.end(), "X04 includes ordinary focusable controls");
}
void drawCommandStateMatrix() {
    editForm::Form inventoryForm(ids,true);
    const auto count=x04::inventory(inventoryForm).size();
    for(std::size_t index=0; index<count; ++index) for(int stateIndex=0; stateIndex<6; ++stateIndex) {
        editForm::Form form(ids,true); auto entry=x04::inventory(form)[index]; auto& ui=detail::access(form.ui);
        if(entry.page=="modal") { if(!form.handles.tabs.activate({"v03"}) || !form.handles.dialogOpen.invoke()) continue; }
        else if(entry.page!="shared" && !form.handles.tabs.activate({std::string(entry.page)})) continue;
        test::FakeMeasure measure;
        arrange(form.page(),form.ui,*appearance,measure,960,4000,1.f,form.scroll);
        auto semantic=x04::describe(entry.handle); if(!semantic) continue;
        bool applicable=true;
        if(stateIndex==1) {
            applicable=std::visit([](const auto& handle) { return requires { handle.enable(false); }; }, entry.handle);
            if(applicable) check(x04::setEnabled(entry.handle,false), "Supported disable setup succeeds");
        }
        if(stateIndex==2) {
            applicable=x04::focusApplicable(*semantic);
            if(applicable) { forceFocusIndicators(form.ui); check(x04::focus(form.ui, entry.handle), "Supported focus setup succeeds"); }
        }
        if(stateIndex==3) {
            applicable=entry.page!="modal" && x04::errorSupported(*semantic);
            if(applicable) check(x04::driveError(form,entry), "Supported error setup succeeds");
        }
        if(stateIndex==4) {
            applicable=semantic->enabled && (semantic->kind==ComponentKind::checkbox || semantic->kind==ComponentKind::radioGroup || semantic->kind==ComponentKind::list || semantic->kind==ComponentKind::tabs);
            if(applicable) check(x04::driveSelected(entry), "Supported selection setup succeeds");
        }
        if(stateIndex==5) {
            applicable=semantic->kind==ComponentKind::textField && semantic->enabled && !semantic->readOnly;
            if(applicable) {
                check(x04::focus(form.ui, entry.handle), "Composition target can focus");
                check(bool(ui.compositionUpdate(u"preedit",3)), "Composition setup succeeds");
            }
        }
        if(!applicable) continue;
        auto frame=arrange(form.page(),form.ui,*appearance,measure,960,4000,1.f,form.scroll);
        auto item=x04::placement(frame,entry.handle); semantic=x04::describe(entry.handle);
        if(!item || !semantic) continue;
        PaintRoleProbe probe(ui,*item,*semantic,*appearance,1.f,true); detail::componentType(item->kind).paint(probe);
        for(const auto& command : probe.recording.list.commands) {
            using Type=detail::DrawCommand::Type;
            check(command.type==Type::clear || command.type==Type::clip || command.type==Type::fill ||
                  command.type==Type::rounded || command.type==Type::glyph || command.type==Type::beginGroup ||
                  command.type==Type::endGroup, "DL02 command belongs to the supported set");
        }
        if(stateIndex==2) {
            check(probe.count(detail::PaintRole::focusIndicator)>0,"DL03 focus cue");
            bool within=std::any_of(probe.emitted.begin(),probe.emitted.end(),[&](const auto& mark) {
                auto hit=intersect(mark.second,item->bounds);
                return (static_cast<unsigned>(mark.first) & static_cast<unsigned>(detail::PaintRole::focusIndicator)) && hit.w>0 && hit.h>0;
            });
            check(within,"DL03 focus cue intersects component bounds");
        }
        if(stateIndex==2 && semantic->kind==ComponentKind::textField && !semantic->readOnly) check(probe.count(detail::PaintRole::caret)==1,"DL03 one caret");
        if(stateIndex==3 && semantic->invalid && !semantic->error.empty()) check(probe.count(detail::PaintRole::errorText)>0,"DL03 error text cue");
        if(stateIndex==5) check(probe.count(detail::PaintRole::composition)>0,"DL03 composition cue");
    }
}
void darkFocusInventory() {
    editForm::Form inventory(ids, true);
    auto entries = x04::inventory(inventory);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        editForm::Form form(ids, true); auto entry = x04::inventory(form)[index];
        auto semantic = x04::describe(entry.handle);
        auto settings = form.ui.presentation(); settings.variant = ThemeVariant::dark();
        check(bool(form.ui.setPresentation(settings)), "Dark theme accepted");
        forceFocusIndicators(form.ui);
        if (entry.page == "modal") {
            check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialogOpen.invoke()), "Focus modal opened");
        } else if (entry.page != "shared") check(bool(form.handles.tabs.activate({entry.page})), "Focus page activated");
        if (!semantic->enabled) check(x04::setEnabled(entry.handle, true), "Focus target enabled");
        test::FakeMeasure measure;
        arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
        semantic = x04::describe(entry.handle);
        if (!semantic || !x04::focusApplicable(*semantic)) continue;
        check(x04::focus(form.ui, entry.handle), ("Dark focus setup " + semantic->id).c_str());
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll, semantic->id);
        semantic = x04::describe(entry.handle); auto* item = x04::placement(frame, entry.handle);
        check(item && semantic->focused && semantic->visible, ("Dark focus visible " + semantic->id).c_str());
        PaintRoleProbe paint(detail::access(form.ui), *item, *semantic, *appearance, 1.f, true);
        detail::componentType(item->kind).paint(paint);
        auto accent = appearance->palette(ThemeVariant::dark()).accent;
        bool cue = std::any_of(paint.recording.list.commands.begin(), paint.recording.list.commands.end(), [&](const auto& c) {
            auto r = intersect(c.rect, item->clip);
            return (static_cast<unsigned>(c.role) & static_cast<unsigned>(detail::PaintRole::focusIndicator)) && r.w > 0 && r.h > 0 &&
                c.color.r == accent.r && c.color.g == accent.g && c.color.b == accent.b;
        });
        check(cue, ("Dark focus indicator " + semantic->id).c_str());
    }
}
// Record the text sent to the component paint adapter, independently of layout's line lists.
struct PaintTextProbe : detail::ComponentPaint {
    using ComponentPaint::ComponentPaint;
    std::u16string drawn;
    int textCalls = 0;
    std::vector<Color> colors;
    void fill(Rect, Color color) override { colors.push_back(color); }
    void rounded(Rect, int, Color color) override { colors.push_back(color); }
    void clip(Rect) override {}
    void text(std::u16string_view value, int, int, int, Color color) override { ++textCalls; drawn += value; colors.push_back(color); }
};
void listPaintsOnlyVisibleRows() {
    editForm::Form form(ids, true); auto& state=detail::access(form.ui);
    form.handles.tabs.activate({"v02"}); form.list->handles.hundred.invoke();
    test::FakeMeasure measure;
    auto frame=arrange(form.page(), form.ui, *appearance, measure, 960, 4000, 1.f, form.scroll);
    auto original=frame.find(form.list->handles.list);
    check(original && original->options.size()==100, "100 list rows are arranged");
    auto placement=*original; auto first=placement.options[0]; auto second=placement.options[1];
    placement.clip={first.x, first.y, first.w, first.h+second.h/2};
    auto semantic=form.list->handles.list.describe();
    PaintTextProbe paint(state,placement,*semantic,*appearance,1.f,true);
    detail::componentType(placement.kind).paint(paint);
    check(paint.textCalls <= 3 && paint.drawn.find(semantic->items[0].label)!=std::u16string::npos &&
          paint.drawn.find(semantic->items[1].label)!=std::u16string::npos &&
          paint.drawn.find(semantic->items[99].label)==std::u16string::npos,
          "Only the visible and partially visible rows produce text work");
}
// Model the core paint adapter's clipping, including associated-label text that is
// emitted by the field rather than by a separate placement.
struct ClippedPaintProbe : detail::ComponentPaint {
    TextMeasure& measure;
    Rect current;
    const std::vector<Line>* associated;
    bool visible = false;
    ClippedPaintProbe(detail::UiState& state, const Placement& item, const ComponentSnapshot& owner,
                      const Appearance& look, float scale, TextMeasure& metrics, const ComponentSnapshot& target)
        : ComponentPaint(state, item, owner, look, scale, true), measure(metrics), current(item.clip), associated(nullptr) {
        if (!target.target.empty()) {
            if (target.labelKind == LabelKind::name) associated = &item.labelLines;
            else if (target.labelKind == LabelKind::description) associated = &item.descriptionLines;
            else if (target.labelKind == LabelKind::error) associated = &item.errorLines;
        }
    }
    void mark(Rect r) { auto v = intersect(r, current); visible = visible || (v.w > 0 && v.h > 0); }
    void fill(Rect r, Color) override { if (!associated) mark(r); }
    void rounded(Rect r, int, Color) override { if (!associated) mark(r); }
    void clip(Rect r) override { current = intersect(current, r); }
    void text(std::u16string_view value, int pixels, int x, int y, Color) override {
        if (value.empty()) return;
        if (associated && std::none_of(associated->begin(), associated->end(), [&](const Line& line) { return line.text == value; })) return;
        mark({x, y, static_cast<int>(std::ceil(measure.measure(value, pixels).width)), measure.lineHeight(pixels)});
    }
};
void paintedStatesAgreeWithSemantics() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    struct Observation { std::u16string text; std::vector<Color> colors; };
    auto observe = [&](const auto& handle) {
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 4000, 1.f, form.scroll);
        auto* item = frame.find(handle);
        check(item != nullptr, "X04 state target is arranged");
        auto semantic = handle.describe();
        check(bool(semantic), "X04 state target has semantics");
        auto allRows = *item;
        if (allRows.kind == ComponentKind::list) allRows.clip = allRows.bounds;
        PaintTextProbe paint(host, allRows, *semantic, *appearance, 1.f, true);
        detail::componentType(item->kind).paint(paint);
        return Observation{paint.drawn, paint.colors};
    };
    auto hasColor = [](const Observation& o, Color expected) {
        return std::any_of(o.colors.begin(), o.colors.end(), [&](Color c) {
            return c.r == expected.r && c.g == expected.g && c.b == expected.b;
        });
    };
    auto contains = [](const Observation& o, const std::u16string& text) { return o.text.find(text) != std::u16string::npos; };
    const auto& wording = appearance->componentPresentation();
    const auto palette = appearance->palette();

    h.name.focus(); h.name.select(0, 2);
    check(hasColor(observe(h.name), palette.selection), "X04 selected text has a selection highlight");
    h.readOnly.invoke();
    auto readOnly = observe(h.name);
    check(h.name.describe()->readOnly && contains(readOnly, wording.readOnlySuffix) &&
          contains(readOnly, h.name.describe()->value), "X04 read-only field wording and value agree");
    h.disabled.invoke();
    auto disabledName = observe(h.name);
    check(!h.name.describe()->enabled && contains(disabledName, wording.disabledSuffix) && hasColor(disabledName, palette.muted),
          "X04 disabled field is identified and muted");
    auto disabledButton = observe(h.apply);
    auto disabledBox = observe(h.notices);
    auto disabledRadio = observe(h.mode);
    auto disabledRegion = observe(h.region);
    auto disabledVolume = observe(h.volume);
    check(!h.apply.describe()->enabled && hasColor(disabledButton, palette.muted) &&
          !h.notices.describe()->enabled && hasColor(disabledBox, palette.muted) &&
          !h.mode.describe()->enabled && hasColor(disabledRadio, palette.muted) &&
          !h.region.describe()->enabled && contains(disabledRegion, wording.disabledSuffix) && hasColor(disabledRegion, palette.muted) &&
          !h.volume.describe()->enabled && contains(disabledVolume, wording.disabledSuffix) && hasColor(disabledVolume, palette.muted),
          "X04 disabled controls use their restricted presentation");

    h.normal.invoke(); h.error.invoke();
    auto error = observe(h.name);
    check(h.name.describe()->invalid && contains(error, h.name.describe()->error) && hasColor(error, palette.error),
          "X04 field error text and error color agree with its semantics");
    h.name.setValue(u"draft"); form.updateSource(u"new source");
    h.mode.select({"focus"}); form.updateModeSource({"offline"});
    h.region.select({"central"}); form.updateRegionSource({"south"});
    h.volume.setValue(55); form.updateVolumeSource(70);
    check(h.name.describe()->sourceConflict && contains(observe(h.state), u"來源已更新") &&
          h.latest.describe()->enabled && contains(observe(h.latest), u"改用最新值"),
          "X04 field conflict notice and use-latest action are drawn");
    check(h.mode.describe()->sourceConflict && contains(observe(h.mode), wording.conflictSuffix) &&
          h.modeLatest.describe()->enabled && contains(observe(h.modeLatest), u"改用最新值") &&
          h.region.describe()->sourceConflict && contains(observe(h.region), wording.conflictSuffix) &&
          h.regionLatest.describe()->enabled && contains(observe(h.regionLatest), u"改用最新值") &&
          h.volume.describe()->sourceConflict && contains(observe(h.volume), wording.conflictSuffix) &&
          h.volumeLatest.describe()->enabled && contains(observe(h.volumeLatest), u"改用最新值"),
          "X04 option and slider conflicts expose their notices and actions");

    check(h.notices.describe()->checked == true && hasColor(observe(h.notices), palette.accent),
          "X04 checked checkbox has an accent indicator");
    h.notices.toggle();
    check(h.notices.describe()->checked == false && !hasColor(observe(h.notices), palette.accent),
          "X04 unchecked checkbox has no checked indicator");
    auto mode = h.mode.describe();
    check(std::any_of(mode->items.begin(), mode->items.end(), [](const auto& o) { return o.selected; }) &&
          hasColor(observe(h.mode), palette.accent), "X04 radio selection has an accent indicator");
    h.offlineDisabled.invoke();
    check(!h.mode.describe()->items[2].enabled && contains(observe(h.mode), wording.disabledSuffix),
          "X04 disabled radio option is identified in the paint text");
    h.reset.invoke(); h.regionEmpty.invoke(); h.apply.invoke();
    check(h.region.describe()->invalid && !h.region.describe()->error.empty() &&
          contains(observe(h.region), h.region.describe()->error) && hasColor(observe(h.region), palette.error),
          "X04 empty dropdown error is painted with the semantic error and error color");

    h.tabs.activate({"v02"});
    auto& list = form.list->handles.list;
    check(!form.list->handles.erase.describe()->enabled && hasColor(observe(form.list->handles.erase), palette.muted),
          "X04 disabled list delete button is muted");
    form.list->handles.zero.invoke();
    check(list.describe()->items.empty() && contains(observe(list), wording.noItems), "X04 empty list has visible empty wording");
    form.list->handles.twenty.invoke(); form.list->handles.disableEight.invoke();
    check(!list.describe()->items[7].enabled && contains(observe(list), wording.disabledSuffix),
          "X04 disabled list row has disabled wording");
    list.select({"T003"});
    check(list.describe()->items[2].selected && hasColor(observe(list), palette.selection) &&
          contains(observe(list), wording.selectedMarker + list.describe()->items[2].label),
          "X04 selected list row has a selection highlight");
    check(h.tabs.describe()->items[1].selected && hasColor(observe(h.tabs), palette.selection),
          "X04 active tab has a selection highlight");
    h.tabs.activate({"v03"}); h.dialogOpen.invoke();
    check(contains(observe(h.dialogTitle), h.dialogTitle.describe()->value) &&
          contains(observe(h.dialogBody), h.dialogBody.describe()->value) &&
          contains(observe(h.dialogCancel), h.dialogCancel.describe()->name) &&
          contains(observe(h.dialogConfirm), h.dialogConfirm.describe()->name),
          "X04 open dialog text and button names are painted");
}
void hiddenAndClippedComponentsAreNotVisibleOrAdmitted() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    test::FakeMeasure measure;
    auto frame = [&] { return arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll); };
    auto first = frame();
    check(!first.find(form.list->handles.list) && !first.find(h.progress) &&
          form.list->handles.list.select({"T001"}).status == Status::hidden && h.dialogOpen.invoke().status == Status::hidden,
          "X04 other pages are absent from paint placements and reject operations");
    const auto* apply = first.find(h.apply);
    check(apply != nullptr, "X04 visible page contains the apply button");
    auto area = intersect(apply->bounds, apply->clip);
    check(area.w == 0 || area.h == 0, "X04 apply starts fully clipped in this viewport");
    check(!h.apply.describe()->visible && !form.list->handles.list.describe()->visible && h.tabs.describe()->visible,
          "X04 semantics distinguish a clipped component, a hidden page and visible tabs");
    auto* hit = first.hit(std::max(0, apply->bounds.x), std::max(0, apply->bounds.y));
    check(!hit || hit->id != apply->id, "X04 a clipped button is not a hit target");
    h.tabs.activate({"v02"});
    auto second = frame();
    check(!h.name.describe()->visible && second.find(h.tabs) && h.tabs.describe()->visible,
          "X04 semantic visibility follows the active page");
    check(!second.find(h.name) && !second.find(h.progress) && h.name.setValue(u"hidden").status == Status::hidden &&
          h.dialogOpen.invoke().status == Status::hidden, "X04 V01 and V03 are absent and reject actions on V02");
    h.tabs.activate({"v03"});
    auto third = frame();
    check(!third.find(h.name) && !third.find(form.list->handles.list) &&
          h.name.setValue(u"hidden").status == Status::hidden && form.list->handles.list.select({"T001"}).status == Status::hidden,
          "X04 V01 and V02 are absent and reject actions on V03");
    auto& state = detail::access(form.ui);
    std::vector<std::vector<Id>> pageItems;
    for (auto page : {"v01", "v02", "v03"}) {
        h.tabs.activate({page});
        auto current = frame();
        std::vector<Id> idsOnPage;
        for (const auto& item : current.items) if (item.id != h.tabs.describe()->id) idsOnPage.push_back(item.id);
        pageItems.push_back(std::move(idsOnPage));
    }
    int pageIndex = 0;
    for (auto page : {"v01", "v02", "v03"}) {
        h.tabs.activate({page});
        auto current = frame();
        for (int other = 0; other < 3; ++other) if (other != pageIndex) for (const auto& id : pageItems[other]) {
            check(!detail::findPlacement(current, id) && state.available(id).status == Status::hidden,
                  "X04 each other-page placement is absent and not admitted");
        }
        ++pageIndex;
    }
}
// Every fixed-page member is checked after switching to each other page.
void x04ExecutingInventoryAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure;
        for (const auto& entry : x04::inventory(form)) {
            auto button = std::get_if<ButtonHandle>(&entry.handle);
            if (!button || !detail::componentType(button->describe()->kind).supportsState(detail::ComponentState::executing) ||
                !x04::executionSupported(form, *button)) continue;
            check(x04::baselineContext(form), "X04 executing baseline");
            auto baseline = x04::snapshot(form);
            auto before = button->describe(); auto message = std::string("X04 executing ") + before->id;
            check(x04::prepareExecution(form, entry), (message + " prepare").c_str());
            bool observed = false;
            check(x04::observeExecution(form, *button, [&] {
                auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                     int(size.second * scale), scale, form.scroll, before->id);
                auto semantic = button->describe(); auto* item = frame.find(*button);
                if (!item) return Result{Status::failed, u"Executing button was not arranged"};
                PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
                detail::componentType(item->kind).paint(paint);
                observed = semantic->visible && semantic->executing &&
                    std::find(semantic->verbs.begin(), semantic->verbs.end(), Verb::invoke) == semantic->verbs.end() &&
                    button->invoke().status == Status::busy && paint.drawn.find(semantic->name) != std::u16string::npos &&
                    paint.drawn.find(appearance->componentPresentation().executingSuffix) != std::u16string::npos;
                return Result{};
            }), (message + " bind or confirmation observer").c_str());
            check(bool(button->invoke()) && observed && !*button->executing(), (message + " callback").c_str());
            check(x04::restoreExecution(form, entry, before->enabled) && x04::sameModel(baseline, x04::snapshot(form)),
                  (message + " restore").c_str());
        }
        check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.progressZero.invoke()) && form.progress() == 0,
              "X04 restored action still invokes the application model");
    }
}

void x04FixedPlacementsFitEachClient() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure;
        check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialogOpen.invoke()), "X04 open fixed overlay");
        int width = int(std::lround(size.first * scale)), height = int(std::lround(size.second * scale));
        auto frame = arrange(form.page(), form.ui, *appearance, measure, width, height, scale, form.scroll);
        for (const auto& entry : x04::inventory(form)) {
            auto semantic = x04::describe(entry.handle); auto scroll = x04::enclosingScroll(form, *semantic);
            if (!scroll || *scroll) continue;
            auto* item = x04::placement(frame, entry.handle);
            auto message = std::string("X04 fixed placement fits client ") + semantic->id;
            check(item && x04::insideClient(*item, width, height), message.c_str());
        }
    }
}

void x04ClippedInventoryAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); auto entries = x04::inventory(form);
        for (const auto& entry : entries) {
            if (entry.page == "shared" || entry.page == "modal") continue;
            auto baseline = x04::snapshot(form);
            auto semantic = x04::describe(entry.handle); auto message = std::string("X04 clipped ") + semantic->id;
            check(x04::prepareClipped(form, entry), (message + " fixture").c_str());
            test::FakeMeasure measure;
            auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                int(size.second * scale), scale, form.scroll); };
            form.scroll.offset = 0; auto first = draw();
            auto rect = x04::clippedRect(first, *x04::describe(entry.handle));
            check(rect && first.maximum > 0, (message + " reachable scroll region").c_str());
            bool fully = false, partial = false;
            for (int offset : x04::clippedOffsets(first, *rect)) {
                form.scroll.offset = offset; auto frame = draw(); semantic = x04::describe(entry.handle);
                auto geometry = x04::clippedRect(frame, *semantic); if (!geometry) continue;
                auto* owner = x04::clippedOwner(frame, *semantic);
                auto intersection = intersect(*geometry, owner->clip);
                auto ownerSemantic = detail::access(form.ui).describe(owner->id);
                ClippedPaintProbe paint(detail::access(form.ui), *owner, *ownerSemantic, *appearance, scale, measure, *semantic);
                detail::componentType(owner->kind).paint(paint);
                if (intersection.w == 0 || intersection.h == 0) {
                    auto* hit = frame.hit(geometry->x + std::max(0, geometry->w / 2), geometry->y + std::max(0, geometry->h / 2));
                    check(!semantic->visible && !paint.visible && (!hit || hit->id != (semantic->target.empty() ? semantic->id : semantic->target)),
                          (message + " fully clipped semantics, paint and hit").c_str());
                    fully = true;
                } else if (intersection != *geometry) {
                    check(semantic->visible && paint.visible, (message + " partial semantics and paint").c_str()); partial = true;
                }
            }
            check(fully && partial, (message + " fully and partially clipped positions").c_str());
            check(x04::restoreClipped(form, entry) && x04::sameModel(baseline, x04::snapshot(form)),
                  (message + " model restoration").c_str());
        }
    }
}

void x04ErrorInventoryAtEachSizeAndScale(bool dark = false) {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form inventoryForm(ids, true);
        auto entries = x04::inventory(inventoryForm);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            editForm::Form form(ids, true); if (dark) { auto settings = form.ui.presentation(); settings.variant = ThemeVariant::dark(); check(bool(form.ui.setPresentation(settings)), "Dark theme accepted"); } auto entry = x04::inventory(form)[index];
            auto semantic = x04::describe(entry.handle);
            if (!detail::componentType(semantic->kind).supportsState(detail::ComponentState::error)) continue;
            auto message = std::string("X04 error ") + semantic->id;
            check(x04::driveError(form, entry), (message + " drive").c_str());
            test::FakeMeasure measure;
            auto owner = semantic->target.empty() ? semantic->id : semantic->target;
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, owner);
            semantic = x04::describe(entry.handle); auto* item = detail::findPlacement(frame, owner);
            check(item && semantic->visible && x04::errorSemantics(form, *semantic), (message + " semantics").c_str());
            auto ownerSemantic = detail::access(form.ui).describe(owner);
            PaintTextProbe paint(detail::access(form.ui), *item, *ownerSemantic, *appearance, scale, true);
            detail::componentType(item->kind).paint(paint);
            if (semantic->kind == ComponentKind::tabs) {
                check(paint.drawn.find(appearance->componentPresentation().invalidTabMarker) != std::u16string::npos &&
                      std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { auto e = appearance->palette(form.ui.presentation().variant).text; return c.r == e.r && c.g == e.g && c.b == e.b; }),
                      (message + " tab cue").c_str());
            } else {
                auto color = appearance->palette(form.ui.presentation().variant).error;
                check(paint.drawn.find(x04::errorMessage) != std::u16string::npos &&
                      std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { return c.r == color.r && c.g == color.g && c.b == color.b; }),
                      (message + " error text and cue").c_str());
            }
            check(x04::restoreError(form, entry) && !x04::errorSemantics(form, *x04::describe(entry.handle)), (message + " restore").c_str());
        }
    }
}

void x04CandidateInventoryAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baselineContext(form), "X04 restore baseline page and focus");
            auto baseline = x04::snapshot(form);
            auto before = x04::describe(entry.handle);
            if (!detail::componentType(before->kind).supportsState(detail::ComponentState::candidate)) continue;
            auto message = std::string("X04 candidate ") + before->id;
            check(x04::prepareCandidate(form, entry), (message + " prepare").c_str());
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, before->id);
            check(x04::openCandidate(entry), (message + " open").c_str());
            state.keyDown({before->kind == ComponentKind::tabs ? Key::right : Key::down});
            frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                            int(size.second * scale), scale, form.scroll, before->id);
            auto semantic = x04::describe(entry.handle);
            auto* item = x04::candidatePlacement(frame, *semantic);
            check(item && semantic->visible && semantic->candidate && x04::candidateLabel(*semantic).has_value(),
                  (message + " semantics").c_str());
            PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
            detail::componentType(item->kind).paint(paint);
            auto cue = semantic->kind == ComponentKind::dropdown ? appearance->palette().selection : appearance->palette().accent;
            check(paint.drawn.find(*x04::candidateLabel(*semantic)) != std::u16string::npos &&
                  std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { return c.r == cue.r && c.g == cue.g && c.b == cue.b; }),
                  (message + " paint").c_str());
            if (semantic->kind == ComponentKind::tabs || semantic->kind == ComponentKind::dropdown)
                check(semantic->value == before->value, (message + " candidate preserves selection").c_str());
            check(x04::restoreCandidate(form, entry) && x04::sameModel(baseline, x04::snapshot(form)),
                  (message + " restore").c_str());
        }
    }
}

void x04SearchReadOnlyAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure; auto& search = form.list->handles.search;
        check(x04::driveSearchReadOnly(form) && bool(search.focus()), "X04 drive search read-only");
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                             int(size.second * scale), scale, form.scroll, search.describe()->id);
        auto semantic = search.describe(); auto* item = frame.find(search);
        check(item && semantic->visible && semantic->readOnly && semantic->enabled &&
              search.setValue(u"blocked").status == Status::readOnly &&
              detail::access(form.ui).textInput(u"x").status == Status::readOnly, "X04 search read-only restriction");
        PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
        detail::componentType(item->kind).paint(paint);
        check(paint.drawn.find(semantic->name) != std::u16string::npos &&
              paint.drawn.find(semantic->value) != std::u16string::npos &&
              paint.drawn.find(appearance->componentPresentation().readOnlySuffix) != std::u16string::npos,
              "X04 search read-only paint");
        check(x04::restoreSearchReadOnly(form) && !search.describe()->readOnly, "X04 search read-only recovery");
    }
}

void x04ModalInventoryAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        check(x04::openModal(form), "X04 open modal");
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                             int(size.second * scale), scale, form.scroll);
        check(frame.modal && form.handles.progressZero.invoke().status == Status::blockedByModal, "X04 modal blocks background");
        for (const auto& entry : x04::inventory(form)) {
            if (entry.page != "modal") continue;
            auto semantic = x04::describe(entry.handle); auto* item = x04::placement(frame, entry.handle);
            auto message = std::string("X04 modal ") + semantic->id;
            check(item && semantic->visible && x04::normalSemantics(*semantic), (message + " semantics").c_str());
            PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
            detail::componentType(item->kind).paint(paint);
            for (const auto& expected : x04::normalText(*semantic))
                check(paint.drawn.find(expected) != std::u16string::npos, (message + " paint").c_str());
        }
        check(x04::closeModal(form) && !*form.handles.dialog.isOpen(), "X04 close modal");
    }
}

void x04TooltipAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        std::uint64_t now = 100; state.tooltipClock([&] { return now; }); state.windowActive(true); forceFocusIndicators(form.ui);
        check(x04::prepareTip(form), "X04 prepare tooltip");
        auto draw = [&] { return arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
            int(size.second * scale), scale, form.scroll, form.handles.progressHalf.describe()->id); };
        draw(); now += 600; state.tooltipTime(now); auto frame = draw();
        auto tip = std::find_if(frame.overlays.begin(), frame.overlays.end(), [](const auto& p) { return p.tooltip; });
        check(*form.handles.progressTip.visible() && tip != frame.overlays.end(), "X04 tooltip visible");
        std::u16string text; for (const auto& line : tip->lines) text += line.text;
        check(text.find(form.handles.progressHalf.describe()->description) != std::u16string::npos &&
              (!frame.hit(tip->bounds.x + 1, tip->bounds.y + 1) || !frame.hit(tip->bounds.x + 1, tip->bounds.y + 1)->tooltip),
              "X04 tooltip text and pointer transparency");
        state.keyDown({Key::escape});
        check(!*form.handles.progressTip.visible() && form.handles.progressHalf.describe()->focused, "X04 tooltip Escape");
        state.tooltipClock({});
    }
}

void x04NormalInventoryAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure; auto& state = detail::access(form.ui);
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baselineContext(form), "X04 restore baseline page and focus");
            bool modal = entry.page == "modal";
            if (modal) check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialogOpen.invoke()), "X04 open normal modal");
            else if (entry.page != "shared") check(bool(form.handles.tabs.activate({std::string(entry.page)})), "X04 normal page");
            auto semantic = x04::describe(entry.handle); auto message = std::string("X04 normal ") + semantic->id;
            const auto& owner = semantic->target.empty() ? semantic->id : semantic->target;
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, owner);
            semantic = x04::describe(entry.handle);
            auto* item = detail::findPlacement(frame, owner);
            if (!semantic->visible && semantic->kind == ComponentKind::label && semantic->value.empty()) {
                check(item || semantic->target.empty(), (message + " empty label").c_str());
            } else {
                check(item && semantic->visible && x04::normalSemantics(*semantic), (message + " semantics").c_str());
                auto ownerSemantic = state.describe(owner);
                PaintTextProbe paint(state, *item, *ownerSemantic, *appearance, scale, true);
                detail::componentType(item->kind).paint(paint);
                for (const auto& expected : x04::normalText(*semantic))
                    check(paint.drawn.find(expected) != std::u16string::npos, (message + " paint").c_str());
            }
            if (modal) check(bool(form.handles.dialog.settle(false)) && bool(form.handles.resetV03.invoke()), "X04 close and reset normal modal");
            check(x04::baselineContext(form), "X04 restore normal page and focus");
        }
    }
}

void x04DisabledInventoryAtEachSizeAndScale(bool dark = false) {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); if (dark) { auto settings = form.ui.presentation(); settings.variant = ThemeVariant::dark(); check(bool(form.ui.setPresentation(settings)), "Dark theme accepted"); } test::FakeMeasure measure; auto& state = detail::access(form.ui);
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baselineContext(form), "X04 restore baseline page and focus");
            auto baseline = x04::snapshot(form);
            auto initial = x04::describe(entry.handle);
            if (!detail::componentType(initial->kind).supportsState(detail::ComponentState::disabled)) continue;
            if (initial->kind == ComponentKind::tabs) {
                auto tabs = std::get<TabsHandle>(entry.handle);
                check(bool(tabs.activate({"v01"})) && bool(tabs.enablePage({"v02"}, false)) && bool(tabs.focus()), "X04 disable tab page");
                auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                     int(size.second * scale), scale, form.scroll);
                auto semantic = tabs.describe(); auto* item = frame.find(tabs);
                check(item && !semantic->items[1].enabled && tabs.activate({"v02"}).status == Status::disabled,
                      "X04 disabled tab restriction");
                PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
                detail::componentType(item->kind).paint(paint);
                check(paint.drawn.find(appearance->componentPresentation().disabledTabMarker) != std::u16string::npos,
                      "X04 disabled tab cue");
                state.pointerDown(semantic->id, 1);
                check(state.pointerUp(semantic->id, 1).status == Status::disabled && *tabs.active() == OptionValue{"v01"},
                      "X04 disabled tab rejects pointer");
                state.keyDown({Key::right});
                check(tabs.describe()->candidate == OptionValue{"v03"} && *tabs.active() == OptionValue{"v01"},
                      "X04 keyboard skips disabled tab");
                check(bool(tabs.enablePage({"v02"}, true)) && bool(tabs.activate({"v02"})) && bool(tabs.activate({"v01"})),
                      "X04 disabled tab recovers");
                check(x04::baselineContext(form) && x04::sameModel(baseline, x04::snapshot(form)), "X04 disabled tab restores model");
                continue;
            }
            if (entry.page == "modal") {
                check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialogOpen.invoke()), "X04 open modal");
            } else check(bool(form.handles.tabs.activate({std::string(entry.page)})), "X04 activate page");
            auto message = std::string("X04 disabled ") + initial->id;
            check(x04::setEnabled(entry.handle, true) && x04::focus(form.ui, entry.handle), (message + " prepare").c_str());
            check(x04::setEnabled(entry.handle, false), (message + " drive").c_str());
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, initial->id);
            auto semantic = x04::describe(entry.handle);
            auto* item = x04::placement(frame, entry.handle);
            check(item && semantic->visible && !semantic->enabled && !semantic->focused && x04::disabledRetry(entry.handle),
                  (message + " restriction").c_str());
            PaintTextProbe paint(state, *item, *semantic, *appearance, scale, true);
            detail::componentType(item->kind).paint(paint);
            check(paint.drawn.find(appearance->componentPresentation().disabledSuffix) != std::u16string::npos &&
                  std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { auto m = appearance->palette(form.ui.presentation().variant).muted; return c.r == m.r && c.g == m.g && c.b == m.b; }),
                  (message + " non-color cue").c_str());
            auto snapshot = x04::snapshot(form);
            check(state.pointerDown(semantic->id).status == Status::disabled, (message + " pointer").c_str());
            state.keyDown({Key::space}); state.keyUp(Key::space);
            auto after = x04::describe(entry.handle);
            check(after->value == semantic->value && x04::sameModel(snapshot, x04::snapshot(form)), (message + " keyboard").c_str());
            check(x04::setEnabled(entry.handle, true) && x04::focus(form.ui, entry.handle) &&
                  x04::setEnabled(entry.handle, initial->enabled), (message + " restore").c_str());
            if (semantic->kind == ComponentKind::radioGroup && initial->enabled) {
                auto radio = std::get<RadioGroupHandle>(entry.handle); auto previous = *radio.draft();
                check(previous && bool(radio.select({"focus"})) && bool(radio.enableOption({"offline"}, false)) && bool(radio.focus()),
                      "X04 prepare disabled radio option");
                auto optionFrame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                            int(size.second * scale), scale, form.scroll, initial->id);
                auto optionSemantic = radio.describe(); auto* optionItem = optionFrame.find(radio);
                check(optionItem && !optionSemantic->items[2].enabled && radio.select({"offline"}).status == Status::disabled,
                      "X04 disabled radio option rejects public selection");
                PaintTextProbe optionPaint(state, *optionItem, *optionSemantic, *appearance, scale, true);
                detail::componentType(optionItem->kind).paint(optionPaint);
                check(optionPaint.drawn.find(optionSemantic->items[2].label + appearance->componentPresentation().disabledSuffix) != std::u16string::npos,
                      "X04 disabled radio option has a text cue");
                state.pointerDown(optionSemantic->id, 2);
                check(state.pointerUp(optionSemantic->id, 2).status == Status::disabled, "X04 disabled radio option rejects pointer");
                state.keyDown({Key::right});
                check(*radio.draft() == OptionValue{"focus"}, "X04 keyboard skips disabled radio option");
                check(bool(radio.enableOption({"offline"}, true)) && bool(radio.select({"offline"})) && bool(radio.select(*previous)),
                      "X04 radio option recovers");
            }
            if (entry.page == "modal")
                check(bool(form.handles.dialog.settle(false)) && bool(form.handles.resetV03.invoke()), "X04 close and reset modal");
            if (semantic->kind == ComponentKind::list)
                check(x04::baselineContext(form) && bool(form.handles.tabs.activate({"v02"})) &&
                      bool(form.list->handles.zero.invoke()) && bool(form.list->handles.twenty.invoke()),
                      "X04 restore initial list candidate");
            check(x04::baselineContext(form) && x04::sameModel(baseline, x04::snapshot(form)), (message + " baseline model").c_str());
        }
    }
}

void x04GroupConflictsAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); test::FakeMeasure measure;
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baselineContext(form), "X04 restore baseline page and focus");
            auto semantic = x04::describe(entry.handle);
            if (!detail::componentType(semantic->kind).supportsState(detail::ComponentState::conflict) ||
                !x04::groupConflictSupported(form, *semantic)) continue;
            auto message = std::string("X04 conflict ") + semantic->id;
            check(x04::driveGroupConflict(form, entry), (message + " drive").c_str());
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, semantic->id);
            auto* item = x04::placement(frame, entry.handle);
            semantic = x04::describe(entry.handle);
            check(item && semantic->visible && semantic->sourceConflict, (message + " field visibility").c_str());
            PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
            detail::componentType(item->kind).paint(paint);
            check(paint.drawn.find(appearance->componentPresentation().conflictSuffix) != std::u16string::npos &&
                  paint.drawn.find(semantic->name) != std::u16string::npos &&
                  (semantic->kind == ComponentKind::checkbox || semantic->value.empty() || paint.drawn.find(semantic->value) != std::u16string::npos) &&
                  (semantic->kind != ComponentKind::checkbox || (semantic->checked == true &&
                   std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) { auto a = appearance->palette().accent; return c.r == a.r && c.g == a.g && c.b == a.b; }))),
                  (message + " field paint").c_str());
            auto latest = x04::latestFor(form, entry);
            check(latest.describe()->enabled, (message + " latest enabled").c_str());
            frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                            int(size.second * scale), scale, form.scroll, latest.describe()->id);
            auto* action = frame.find(latest); auto actionSemantic = latest.describe();
            check(action && actionSemantic->visible, (message + " latest visibility").c_str());
            PaintTextProbe actionPaint(detail::access(form.ui), *action, *actionSemantic, *appearance, scale, true);
            detail::componentType(action->kind).paint(actionPaint);
            check(actionPaint.drawn.find(actionSemantic->name) != std::u16string::npos, (message + " latest paint").c_str());
            check(bool(latest.invoke()) && !x04::describe(entry.handle)->sourceConflict &&
                  bool(form.handles.reset.invoke()) && x04::baselineContext(form), (message + " restore").c_str());
        }
    }
}

void x04SelectedInventoryAtEachSizeAndScale(bool dark = false) {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); if (dark) { auto settings = form.ui.presentation(); settings.variant = ThemeVariant::dark(); check(bool(form.ui.setPresentation(settings)), "Dark theme accepted"); }
        test::FakeMeasure measure;
        for (const auto& entry : x04::inventory(form)) {
            check(x04::baselineContext(form), "X04 restore baseline page and focus");
            auto baseline = x04::snapshot(form);
            auto presentation = form.ui.presentation();
            auto before = x04::describe(entry.handle);
            if (!detail::componentType(before->kind).supportsState(detail::ComponentState::selected)) continue;
            if (entry.page != "shared") check(bool(form.handles.tabs.activate({std::string(entry.page)})), "X04 select page");
            auto message = std::string("X04 selected ") + before->id;
            check(x04::driveSelected(entry), message.c_str());
            auto semantic = x04::describe(entry.handle);
            auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale),
                                 int(size.second * scale), scale, form.scroll, semantic->id);
            const auto* item = x04::placement(frame, entry.handle);
            semantic = x04::describe(entry.handle);
            check(item && semantic->visible && x04::selectedSemantics(*semantic), message.c_str());
            PaintTextProbe paint(detail::access(form.ui), *item, *semantic, *appearance, scale, true);
            detail::componentType(item->kind).paint(paint);
            const auto cue = semantic->kind == ComponentKind::checkbox || semantic->kind == ComponentKind::radioGroup
                ? appearance->palette(form.ui.presentation().variant).accent : appearance->palette(form.ui.presentation().variant).selection;
            check(std::any_of(paint.colors.begin(), paint.colors.end(), [&](Color c) {
                return c.r == cue.r && c.g == cue.g && c.b == cue.b;
            }) && paint.drawn.find(semantic->kind == ComponentKind::checkbox ? semantic->name : semantic->value) != std::u16string::npos,
                message.c_str());
            check(x04::restoreSelected(form, entry, *before, presentation) && form.ui.presentation() == presentation && x04::sameModel(baseline, x04::snapshot(form)), message.c_str());
        }
    }
}

void x04HiddenInventoryAtEachSizeAndScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true);
        test::FakeMeasure measure;
        check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialogOpen.invoke()) &&
              bool(form.handles.dialogCancel.invoke()), "X04 close dialog for hidden sweep");
        auto closed = arrange(form.page(), form.ui, *appearance, measure,
                              int(size.first * scale), int(size.second * scale), scale, form.scroll);
        for (const auto& entry : x04::inventory(form)) if (entry.page == "modal") {
            auto semantic = x04::describe(entry.handle);
            auto message = std::string("X04 closed dialog hidden ") + semantic->id;
            check(!semantic->visible && !x04::placement(closed, entry.handle) &&
                  detail::access(form.ui).available(semantic->id).status == Status::hidden &&
                  detail::access(form.ui).pointerDown(semantic->id).status == Status::hidden &&
                  x04::hiddenRetry(entry.handle), message.c_str());
        }
        for (auto page : {"v01", "v02", "v03"}) {
            check(bool(form.handles.tabs.activate({page})), "X04 activate page");
            auto frame = arrange(form.page(), form.ui, *appearance, measure,
                                 int(size.first * scale), int(size.second * scale), scale, form.scroll);
            for (const auto& entry : x04::inventory(form)) {
                if (entry.page == "shared" || entry.page == "modal" || entry.page == page) continue;
                auto semantic = x04::describe(entry.handle);
                auto message = std::string("X04 hidden ") + (semantic ? semantic->id : "missing") + " on " + page;
                check(semantic && !semantic->visible && !x04::placement(frame, entry.handle) &&
                      detail::access(form.ui).available(semantic->id).status == Status::hidden &&
                      x04::hiddenRetry(entry.handle), message.c_str());
            }
        }
    }
}

void visibleTextAgreesWithSemanticsOnEachPage() {
    editForm::Form form(ids, true);
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    for (auto page : {"v01", "v02", "v03"}) {
        form.handles.tabs.activate({page});
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
        for (const auto& item : frame.items) {
            auto semantic = host.describe(item.id);
            check(bool(semantic), "X04 every arranged control has semantics");
            auto allRows = item;
            if (allRows.kind == ComponentKind::list) allRows.clip = allRows.bounds;
            PaintTextProbe paint(host, allRows, *semantic, *appearance, 1.f, true);
            detail::componentType(item.kind).paint(paint);
            auto contains = [&](std::u16string value) {
                value.erase(std::remove(value.begin(), value.end(), u'\n'), value.end());
                return value.empty() || paint.drawn.find(value) != std::u16string::npos;
            };
            if (item.kind == ComponentKind::label || item.kind == ComponentKind::textField)
                check(contains(semantic->value), "X04 label or field value is sent to paint");
            if (item.kind == ComponentKind::textField || item.kind == ComponentKind::button || item.kind == ComponentKind::checkbox ||
                item.kind == ComponentKind::radioGroup || item.kind == ComponentKind::dropdown || item.kind == ComponentKind::slider || item.kind == ComponentKind::progress)
                check(contains(semantic->name), "X04 component name is sent to paint");
            if (item.kind == ComponentKind::dropdown || item.kind == ComponentKind::slider || item.kind == ComponentKind::progress)
                check(contains(semantic->value), "X04 value is sent to paint");
            if (item.kind == ComponentKind::radioGroup || item.kind == ComponentKind::list || item.kind == ComponentKind::tabs)
                for (const auto& option : semantic->items)
                    check(contains(option.label), "X04 option label is sent to paint");
        }
    }
}

void d01TextSurvivesScaleChange() {
    for (auto scales : {std::pair{1.f, 1.5f}, std::pair{1.5f, 1.f},
                        std::pair{1.25f, 1.5f}, std::pair{1.5f, 1.25f}}) {
        editForm::Form form(ids, true);
        auto& h = form.handles;
        auto& host = detail::access(form.ui);
        test::FakeMeasure measure;
        auto draw = [&](float scale) {
            return arrange(form.page(), form.ui, *appearance, measure, int(640 * scale), int(480 * scale),
                           scale, form.scroll, ids.name);
        };
        h.name.setValue(u"ABCDE"); h.name.focus(); h.name.select(1, 3);
        auto initial = draw(scales.first);
        check(initial.find(h.name), "D01 initial field is laid out");
        auto moved = draw(scales.second);
        auto* field = moved.find(h.name);
        auto* session = host.edit(ids.name);
        check(field && session && session->text == u"ABCDE" && session->selection.anchor == 1 &&
              session->selection.caret == 3 && h.name.describe()->focused, "D01 scale change preserves text, selection and focus");
        int x = field->caretX(2), y = field->bounds.y + field->textField.editor.y + field->textField.editor.h / 2;
        auto* hit = moved.hit(x, y);
        check(hit && hit->id == ids.name && moved.textOffset(h.name, x) == 2, "D01 scaled field hits the expected caret position");
        check(bool(host.pointerDown(hit->id)) && bool(host.select(hit->id, moved.textOffset(h.name, x), moved.textOffset(h.name, x))) &&
              bool(host.pointerUp(hit->id)) && session->selection.anchor == 2 && session->selection.caret == 2,
              "D01 pointer placement selects the expected caret");

        h.name.select(1, 3);
        check(bool(host.compositionUpdate(u"xy", 1)), "D01 composition starts");
        draw(scales.first);
        moved = draw(scales.second);
        field = moved.find(h.name); session = host.edit(ids.name);
        check(field && session && session->composing && session->preedit == u"xy" && session->preeditCaret == 1 &&
              session->selection.anchor == 1 && session->selection.caret == 3 && h.name.describe()->focused,
              "D01 composition and focus survive scale change");
        int start = field->caretX(session->begin());
        int candidate = field->caretX(session->visibleCaret());
        check(moved.textOffset(h.name, start) == 1 && moved.textOffset(h.name, candidate) == 2 && candidate > start &&
              field->textField.editor.h > 0 && field->bounds.y + field->textField.editor.y >= 0,
              "D01 scaled frame exposes composition and candidate caret geometry");
    }
}

void d01PopupSurvivesScaleChange() {
    for (auto scales : {std::pair{1.f, 1.5f}, std::pair{1.5f, 1.f},
                        std::pair{1.25f, 1.5f}, std::pair{1.5f, 1.25f}}) {
        editForm::Form form(ids, true);
        auto& h = form.handles;
        auto& host = detail::access(form.ui);
        test::FakeMeasure measure;
        auto draw = [&](float scale) {
            return arrange(form.page(), form.ui, *appearance, measure, int(640 * scale), int(480 * scale),
                           scale, form.scroll, ids.region);
        };
        h.region.focus();
        auto initial = draw(scales.first);
        check(initial.maximum > 0 && bool(h.region.open()), "D01 popup setup has scroll and opens");
        host.keyDown({Key::down}); host.keyDown({Key::down});
        form.scroll.offset = 0; // force the next reveal to move the viewport
        auto moved = draw(scales.second);
        check(h.region.describe()->focused && h.region.describe()->expanded && h.region.describe()->candidate == OptionValue{"south"},
              "D01 scale change retains the focused open popup and candidate");
        check(bool(test::popupInClient(moved, {0, 0, int(640 * scales.second), int(480 * scales.second)}, ids.region)),
              "D01 scaled popup stays inside the window and receives center hits");
    }
}

void popupAndScrollMatrix() {
    struct Size { int w, h; };
    for (auto size : {Size{960, 720}, Size{640, 480}}) for (float scale : {1.f, 1.5f}) {
        const int w = int(size.w * scale), height = int(size.h * scale);
        auto inside = [&](Rect r) { return test::inside(r, {0, 0, w, height}); };
        editForm::Form form(ids, true);
        auto& h = form.handles;
        auto& host = detail::access(form.ui);
        test::FakeMeasure measure;
        auto draw = [&](std::string_view reveal = {}) {
            return arrange(form.page(), form.ui, *appearance, measure, w, height, scale, form.scroll, reveal);
        };
        auto dropdownId = h.region.describe()->id;
        h.region.focus(); draw(dropdownId);
        check(bool(h.region.open()), "X02 open dropdown after scroll reveal");
        auto dropdown = draw(dropdownId);
        auto pop = std::find_if(dropdown.overlays.begin(), dropdown.overlays.end(), [](const Placement& p) { return p.popup && !p.tooltip; });
        check(bool(test::popupInClient(dropdown, {0, 0, w, height}, dropdownId)) && !pop->options.empty(), "X02 dropdown stays in window and receives center hits after reveal");
        auto row = pop->options.front();
        auto hit = dropdown.hit(row.x + row.w / 2, row.y + row.h / 2);
        check(hit && hit->popup && hit->id == dropdownId, "X02 dropdown receives the topmost hit");
        int wheelBefore = form.scroll.offset;
        check(!dropdown.wheel(form.ui, row.x + row.w / 2, row.y + row.h / 2, -48) &&
              form.scroll.offset == wheelBefore && h.region.describe()->expanded,
              "X02 wheel input inside the dropdown does not move the background or dismiss it");
        host.keyDown({Key::down}); host.keyDown({Key::down}); host.keyDown({Key::escape});
        check(!h.region.describe()->expanded && h.region.describe()->focused && *h.region.draft() == OptionValue{"north"},
              "X02 Escape cancels the candidate and retains focus and value");
        h.region.open(); host.keyDown({Key::down}); host.keyDown({Key::down}); host.keyDown({Key::tab});
        check(!h.region.describe()->expanded && h.volume.describe()->focused && *h.region.draft() == OptionValue{"north"},
              "X02 Tab cancels the candidate and advances focus");
        h.region.focus(); h.region.open(); host.keyDown({Key::down}); host.keyDown({Key::down});
        auto outsideFrame = draw(dropdownId);
        const auto* tabs = outsideFrame.find(h.tabs);
        auto tab = tabs->options.front();
        auto* outsideHit = outsideFrame.hit(tab.x + tab.w / 2, tab.y + tab.h / 2);
        check(outsideHit && outsideHit->id == h.tabs.describe()->id && !outsideHit->popup, "X02 outside hit reaches the page tab");
        host.closePopup(); host.pointerDown(outsideHit->id, outsideFrame.optionAt(*outsideHit, tab.x + tab.w / 2, tab.y + tab.h / 2));
        host.pointerUp(outsideHit->id, outsideFrame.optionAt(*outsideHit, tab.x + tab.w / 2, tab.y + tab.h / 2));
        check(!h.region.describe()->expanded && h.tabs.describe()->focused && *h.region.draft() == OptionValue{"north"} &&
              *h.tabs.active() == OptionValue{"v01"}, "X02 outside click cancels and still activates its target");

        // A relayout that re-reveals the focused source (resize, DPI change) is not scroll input:
        // the open dropdown keeps its candidate and follows its anchor (B04, D01).
        h.region.focus(); auto revealed = draw(dropdownId);
        if (revealed.maximum > 0) {
            h.region.open(); host.keyDown({Key::down}); host.keyDown({Key::down});
            form.scroll.offset = form.scroll.offset == 0 ? revealed.maximum : 0;
            auto relaid = draw(dropdownId);
            auto follow = std::find_if(relaid.overlays.begin(), relaid.overlays.end(), [](const Placement& p) { return p.popup && !p.tooltip; });
            check(h.region.describe()->expanded && h.region.describe()->candidate == OptionValue{"south"} &&
                  follow != relaid.overlays.end() && bool(test::popupInClient(relaid, {0, 0, w, height}, dropdownId)),
                  "X02 relayout reveal keeps the open dropdown and its candidate");
            host.keyDown({Key::escape});
        }

        h.region.focus(); auto scrollFrame = draw(dropdownId);
        h.region.open(); host.keyDown({Key::down}); host.keyDown({Key::down});
        check(h.region.describe()->candidate == OptionValue{"south"}, "X02 scroll starts with an unconfirmed candidate");
        int before = form.scroll.offset;
        int destination = before < scrollFrame.maximum ? before + 1 : before - 1;
        check(!scrollFrame.scrollTo(form.ui, form.scroll, before) && h.region.describe()->expanded,
              "X02 a stationary viewport does not cancel the popup");
        check(scrollFrame.scrollTo(form.ui, form.scroll, destination) && !h.region.describe()->expanded &&
              !h.region.describe()->candidate && h.region.describe()->focused && *h.region.draft() == OptionValue{"north"},
              "X02 a moving viewport cancels the dropdown without changing focus or value");
        h.region.open(); host.keyDown({Key::down}); host.keyDown({Key::down});
        auto wheelFrame = draw();
        auto viewport = wheelFrame.regions.front().clip;
        int wheelStep = form.scroll.offset + 3 <= wheelFrame.maximum ? -1 : 1;
        check(wheelFrame.wheel(form.ui, viewport.x + 1, viewport.y + 1, wheelStep) &&
              !h.region.describe()->expanded && !h.region.describe()->candidate && h.region.describe()->focused &&
              *h.region.draft() == OptionValue{"north"}, "X02 wheel outside the popup cancels through the shared frame path");
        int afterFirstWheel = form.scroll.offset;
        check(wheelFrame.wheel(form.ui, viewport.x + 1, viewport.y + 1, wheelStep) &&
              form.scroll.offset == afterFirstWheel - wheelStep,
              "X02 consecutive wheel messages accumulate before the next frame");
        auto stalePopup = std::find_if(wheelFrame.overlays.begin(), wheelFrame.overlays.end(),
                                        [](const Placement& p) { return p.popup && !p.tooltip; });
        if (stalePopup != wheelFrame.overlays.end()) {
            int x = stalePopup->bounds.x + stalePopup->bounds.w / 2;
            int y = stalePopup->bounds.y + stalePopup->bounds.h / 2;
            if (viewport.contains(x, y))
                check(wheelFrame.wheel(form.ui, x, y, wheelStep),
                      "X02 a dismissed popup no longer intercepts wheel input before repaint");
        }

        // Choose real offsets from the assembled page's own geometry. If the page cannot
        // expose an edge at this size, don't invent extra overflow to manufacture a sample.
        h.longText.invoke();
        for (bool partial : {true, false}) {
            auto geometry = draw();
            const auto* anchor = geometry.find(h.region);
            int originalTop = anchor->bounds.y + form.scroll.offset;
            int half = std::max(1, anchor->bounds.h / 2);
            int topEdge = originalTop + anchor->bounds.h - anchor->clip.y;
            int bottomEdge = originalTop - (anchor->clip.y + anchor->clip.h);
            struct Movement { int before, after; };
            std::optional<Movement> movement;
            for (auto candidate : {Movement{topEdge - (partial ? half : 0) - 1, topEdge - (partial ? half : 0)},
                                   Movement{bottomEdge + (partial ? half : 0) + 1, bottomEdge + (partial ? half : 0)}}) {
                if (candidate.before < 0 || candidate.after < 0 || candidate.before > geometry.maximum || candidate.after > geometry.maximum) continue;
                int y = originalTop - candidate.before;
                if (y < anchor->clip.y + anchor->clip.h && y + anchor->bounds.h > anchor->clip.y) { movement = candidate; break; }
            }
            if (!movement) {
                std::cout << "X02 dropdown " << (partial ? "partial" : "clipped") << " anchor unreachable at "
                          << size.w << 'x' << size.h << ' ' << int(scale * 100) << "% with this assembled page\n";
                continue;
            }
            geometry.scrollTo(form.ui, form.scroll, movement->before); geometry = draw();
            h.region.open(); host.keyDown({Key::down}); host.keyDown({Key::down});
            check(h.region.describe()->expanded && h.region.describe()->candidate == OptionValue{"south"},
                  "X02 edge sample opens with an unconfirmed dropdown candidate");
            check(geometry.scrollTo(form.ui, form.scroll, movement->after), "X02 edge sample moves the viewport");
            auto after = draw(); anchor = after.find(h.region);
            auto visibleArea = intersect(anchor->bounds, anchor->clip);
            bool correctEdge = partial ? visibleArea.h > 0 && visibleArea.h < anchor->bounds.h : visibleArea.h == 0;
            check(correctEdge && !h.region.describe()->expanded && !h.region.describe()->candidate &&
                  h.region.describe()->focused && *h.region.draft() == OptionValue{"north"},
                  "X02 edge scroll cancels the dropdown and preserves value and focus");
        }
        // A focused source scrolled entirely out cannot show a tooltip while it is clipped.
        h.region.focus(); auto visibleFrame = draw(dropdownId);
        std::uint64_t tipNow = 0;
        host.tooltipClock([&] { return tipNow; }); host.windowActive(true);
        auto regionTip = form.ui.addTooltip("region.tip", h.region, u"Region help", 100).value.value();
        tipNow = 100; host.tooltipTime(tipNow);
        auto* tipAnchor = visibleFrame.find(h.region);
        int topCutoff = tipAnchor->bounds.y + form.scroll.offset + tipAnchor->bounds.h - tipAnchor->clip.y;
        int bottomCutoff = tipAnchor->bounds.y + form.scroll.offset - tipAnchor->clip.y - tipAnchor->clip.h;
        int clippedOffset = topCutoff >= 0 && topCutoff <= visibleFrame.maximum ? topCutoff :
                             bottomCutoff >= 0 && bottomCutoff <= visibleFrame.maximum ? bottomCutoff : -1;
        if (clippedOffset >= 0 && clippedOffset != form.scroll.offset) {
            int previous = form.scroll.offset;
            check(*regionTip.visible() && visibleFrame.scrollTo(form.ui, form.scroll, clippedOffset),
                  "X02 start with a visible tooltip and scroll its source completely out");
            auto clippedFrame = draw(); tipNow += 200; host.tooltipTime(tipNow);
            check(!*regionTip.visible() && h.region.describe()->focused,
                  "X02 tooltip stays hidden while its focused source is fully clipped");
            clippedFrame.scrollTo(form.ui, form.scroll, previous); draw();
            tipNow += 99; host.tooltipTime(tipNow);
            check(!*regionTip.visible(), "X02 returning a clipped source starts a new dwell");
            ++tipNow; host.tooltipTime(tipNow);
            check(*regionTip.visible(), "X02 tooltip returns only after the new dwell");
        }
        host.tooltipClock({});
        h.longText.invoke();

        check(bool(h.tabs.activate({"v02"})), "X02 open list page");
        auto& list = form.list->handles.list;
        check(bool(list.focus()), "X02 focus list");
        host.keyDown({Key::end});
        auto listFrame = draw(list.describe()->id);
        auto* listItem = listFrame.find(list);
        check(listItem && !listItem->options.empty() && form.list->listScroll.offset > 0 &&
              test::inside(listItem->options.back(), listItem->clip),
              "X02 End scrolls nested list to the last visible row");

        check(bool(h.tabs.activate({"v03"})), "X02 open popup page");
        std::uint64_t now = 0;
        host.tooltipClock([&] { return now; }); host.windowActive(true);
        auto sourceId = h.progressHalf.describe()->id;
        draw(sourceId); h.progressHalf.focus(); now = 600; host.tooltipTime(now);
        auto tooltip = draw(sourceId);
        auto tip = std::find_if(tooltip.overlays.begin(), tooltip.overlays.end(), [](const Placement& p) { return p.tooltip; });
        check(tip != tooltip.overlays.end() && inside(tip->bounds) && *h.progressTip.visible(), "X02 tooltip appears inside the window after scroll reveal");
        if (tooltip.maximum > 0) {
            int current = form.scroll.offset;
            int next = current < tooltip.maximum ? current + 1 : current - 1;
            check(tooltip.scrollTo(form.ui, form.scroll, next) && !*h.progressTip.visible() && h.progressHalf.describe()->focused,
                  "X02 viewport movement hides the tooltip without moving focus");
            now += 499; host.tooltipTime(now);
            check(!*h.progressTip.visible(), "X02 tooltip needs a fresh full dwell after scrolling");
            now += 1; host.tooltipTime(now);
            check(*h.progressTip.visible(), "X02 tooltip may reappear after the fresh dwell");
        }
        host.keyDown({Key::escape});
        check(!*h.progressTip.visible() && h.progressHalf.describe()->focused && form.progress() == 50,
              "X02 tooltip Escape preserves focus and value");
        h.dialogOpen.focus(); h.progressHalf.focus(); now += 600; host.tooltipTime(now);
        check(*h.progressTip.visible(), "X02 tooltip can return on a new focus visit");
        h.dialogOpen.focus();
        check(!*h.progressTip.visible() && h.dialogOpen.describe()->focused && form.progress() == 50,
              "X02 tooltip closes on focus leave");
        host.tooltipHover(sourceId); host.pointerDown(sourceId); host.pointerUp(sourceId);
        now += 600; host.tooltipTime(now);
        check(*h.progressTip.visible(), "X02 tooltip appears on a pointer visit");
        host.tooltipHover({});
        check(!*h.progressTip.visible() && h.progressHalf.describe()->focused && form.progress() == 50,
              "X02 tooltip pointer leave preserves focus and value");
        auto beforeModal = draw();
        check(bool(h.dialogOpen.invoke()), "X02 open modal dialog");
        if (beforeModal.maximum > 0) {
            int current = form.scroll.offset;
            int next = current < beforeModal.maximum ? current + 1 : current - 1;
            auto viewport = beforeModal.regions.front().clip;
            int delta = next > current ? -1 : 1;
            check(!beforeModal.scrollTo(form.ui, form.scroll, next) &&
                  !beforeModal.wheel(form.ui, viewport.x + 1, viewport.y + 1, delta) && form.scroll.offset == current,
                  "X02 a newly opened modal blocks scrolling through the preceding frame");
        }
        auto dialog = draw();
        auto* cancel = dialog.find(h.dialogCancel);
        auto* dialogHit = cancel ? dialog.hit(cancel->bounds.x + cancel->bounds.w / 2, cancel->bounds.y + cancel->bounds.h / 2) : nullptr;
        check(dialog.modal && inside(dialog.modalPanel) && cancel && dialogHit && dialogHit->id == cancel->id && !*h.progressTip.visible(),
              "X02 dialog bounds and hit are valid and tooltip closes");
        if (dialog.maximum > 0) {
            int current = form.scroll.offset;
            int next = current < dialog.maximum ? current + 1 : current - 1;
            check(!dialog.scrollTo(form.ui, form.scroll, next) && form.scroll.offset == current,
                  "X02 a modal blocks a movable background viewport");
        } else {
            std::cout << "X02 modal background scroll N/A: no overflow at " << size.w << 'x' << size.h << ' ' << int(scale * 100) << "%\n";
        }
        host.keyDown({Key::escape});
        check(!*h.dialog.isOpen() && h.dialogOpen.describe()->focused && form.progress() == 50 && form.dialogConfirmations() == 0,
              "X02 Escape cancels the dialog and restores focus");
        h.dialogOpen.invoke(); h.dialogCancel.invoke();
        check(!*h.dialog.isOpen() && h.dialogOpen.describe()->focused && form.progress() == 50 && form.dialogConfirmations() == 0,
              "X02 Cancel restores focus and preserves progress");
        h.dialogOpen.invoke(); h.dialogConfirm.invoke();
        check(!*h.dialog.isOpen() && h.dialogOpen.describe()->focused && form.progress() == 0 && form.dialogConfirmations() == 1,
              "X02 Confirm restores focus and changes progress once");
    }
}

void repeatedV03RemoveResetKeepsStorageBounded() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    h.tabs.activate({"v03"});
    const auto baseline = host.storageCounts();
    auto old = h.dialogTemporary;
    for (int i = 0; i < 1000; ++i) {
        check(bool(h.dialogTemporary.remove()) && bool(h.resetV03.invoke()), "Remove and reset the V03 opener");
        check(host.storageCounts() == baseline && bool(h.dialogTemporary.describe()), "Reset has bounded component and page storage");
        check(old.describe().status == Status::notFound, "Original opener remains stale");
    }
}

void v03ResetRestoresTheTemporarySourceAndKeepsOtherPages() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    h.name.setValue(u"Retained V01 draft");
    h.tabs.activate({"v03"});
    auto oldSource = h.dialogTemporary;
    h.dialogTemporary.invoke(); h.dialogTemporary.remove(); h.dialogConfirm.invoke();
    h.progressFull.invoke();
    check(form.progress() == 100 && form.dialogConfirmations() == 1 && !oldSource.describe(),
          "The confirmed result and removed temporary source precede reset");
    h.resetV03.invoke();
    check(form.progress() == 50 && form.dialogConfirmations() == 0 && h.dialogResult.describe()->value.find(u"No result") != std::u16string::npos &&
          h.dialogTemporary.describe() && !oldSource.describe(), "Reset restores the initial model and creates a fresh temporary source");
    h.tabs.activate({"v01"});
    check(*h.name.draft() == u"Retained V01 draft", "Resetting V03 preserves the V01 draft");
}

void closedDialogMembersAreHiddenAndRejectInput() {
    editForm::Form form(ids, true); auto& h = form.handles;
    test::FakeMeasure measure; auto& host = detail::access(form.ui);
    check(bool(h.tabs.activate({"v03"})) && bool(h.dialogOpen.invoke()), "Open dialog for hidden regression");
    check(bool(h.dialogCancel.invoke()), "Close dialog for hidden regression");
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 960, 720, 1.f, form.scroll);
    check(!h.dialogTitle.describe()->visible && !h.dialogBody.describe()->visible &&
          !h.dialogCancel.describe()->visible && !h.dialogConfirm.describe()->visible &&
          !frame.find(h.dialogTitle) && !frame.find(h.dialogBody) &&
          !frame.find(h.dialogCancel) && !frame.find(h.dialogConfirm), "Closed dialog has no visible members or placements");
    check(h.dialogCancel.focus().status == Status::hidden && h.dialogCancel.invoke().status == Status::hidden &&
          h.dialogConfirm.invoke().status == Status::hidden &&
          host.pointerDown(h.dialogConfirm.describe()->id).status == Status::hidden &&
          host.pointerDown(h.dialogTitle.describe()->id).status == Status::hidden &&
          host.pointerDown(h.dialogBody.describe()->id).status == Status::hidden,
          "Closed dialog rejects focus, public operation and pointer as hidden");
    check(bool(h.progressHalf.focus()), "Focus visible background after closing");
    host.keyDown({Key::enter}); host.keyUp(Key::enter);
    check(form.dialogConfirmations() == 0 && !*h.dialog.isOpen(), "Keyboard does not activate closed dialog");
    check(bool(h.dialogConfirm.enable(false)) && h.dialogConfirm.invoke().status == Status::hidden,
          "Hidden takes precedence over a disabled dialog member");
}

void dialogCancelPreservesProgress() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    h.tabs.activate({"v03"});
    check(h.progressHalf.describe()->description == u"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002", "V03 tooltip uses the specified text");
    check(bool(h.dialogOpen.invoke()), "Open dialog");
    check(h.dialogTitle.describe()->value == u"\u91cd\u8a2d\u793a\u7bc4\u9032\u5ea6" && h.dialogBody.describe()->value == u"\u5c07\u9032\u5ea6\u6539\u70ba 0%\u3002\u5176\u4ed6\u9801\u9762\u8cc7\u6599\u6703\u4fdd\u7559\u3002" &&
          h.dialogCancel.describe()->name == u"\u53d6\u6d88" && h.dialogConfirm.describe()->name == u"\u91cd\u8a2d\u9032\u5ea6",
          "Dialog uses the specified title and button text");
    check(h.dialogCancel.describe()->focused, "Dialog starts on cancel");
    check(bool(h.dialogCancel.invoke()), "Cancel dialog");
    check(form.progress() == 50 && form.dialogConfirmations() == 0 && h.dialogOpen.describe()->focused,
          "Cancel preserves progress and returns focus to its opener");
}

void dialogConfinesInputAndConfirmsOnce() {
    editForm::Form form(ids, true);
    auto densitySettings = form.ui.presentation(); densitySettings.density = behaviorDensity;
    check(bool(form.ui.setPresentation(densitySettings)), "Behavior density accepted");
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    test::FakeMeasure measure;
    h.tabs.activate({"v03"});
    h.dialogOpen.invoke();
    auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll);
    const auto* cancel = frame.find(h.dialogCancel);
    check(cancel && frame.hit(cancel->bounds.x + 2, cancel->bounds.y + 2) == cancel && !frame.hit(1, 1),
          "Dialog controls are above an input-blocking mask");
    check(h.progressFull.invoke().status == Status::blockedByModal &&
          h.tabs.activate({"v01"}).status == Status::blockedByModal && form.progress() == 50,
          "Background operations and page switching are blocked");
    check(host.pointerDown(h.progressFull.describe()->id).status == Status::blockedByModal,
          "Background pointer input is blocked");
    host.keyDown({Key::tab, true});
    check(h.dialogConfirm.describe()->focused, "Shift+Tab wraps within the dialog");
    host.keyDown({Key::tab});
    check(h.dialogCancel.describe()->focused, "Tab wraps back to cancel");
    host.keyDown({Key::tab});
    check(h.dialogConfirm.describe()->focused, "Tab enters confirm");
    host.keyDown({Key::enter}); host.keyDown({Key::enter, false, false, true}); host.keyUp(Key::enter);
    check(form.progress() == 0 && form.dialogConfirmations() == 1 && h.dialogOpen.describe()->focused,
          "Confirm settles once and restores opener focus");
}

void dialogEscapeAndRemovedOpener() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    auto& host = detail::access(form.ui);
    h.tabs.activate({"v03"});
    h.dialogTemporary.invoke();
    check(bool(h.dialogTemporary.remove()), "Remove the temporary opener with the dialog fixture");
    host.keyDown({Key::escape});
    check(form.progress() == 50 && form.dialogConfirmations() == 0 && h.dialogOpen.describe()->focused &&
          h.dialogTemporary.invoke().status == Status::notFound,
          "Escape cancels once and returns focus to the permanent fallback");
    check(bool(h.resetV03.invoke()) && bool(h.dialogTemporary.invoke()) && h.dialogCancel.describe()->focused,
          "Reset recreates the removed temporary opener");
}

void dialogRemovedOpenerCanConfirm() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    h.tabs.activate({"v03"}); h.dialogTemporary.invoke(); h.dialogTemporary.remove();
    check(bool(h.dialogConfirm.invoke()) && form.progress() == 0 && form.dialogConfirmations() == 1 &&
          h.dialogOpen.describe()->focused && h.dialogConfirm.invoke().status != Status::ok,
          "Confirm settles once and restores the permanent fallback after removal");
}

void progressDisplaysClampedValuesWithoutChangingTheModel() {
    editForm::Form form(ids, true);
    auto& h = form.handles;
    test::FakeMeasure measure;
    h.name.setValue(u"kept draft");
    check(bool(h.tabs.activate({"v03"})), "Open V03");
    check(form.progress() == 50 && h.progress.describe()->value == u"50%", "Progress starts at 50%");
    const struct { ButtonHandle button; int source; int shown; std::u16string text; } cases[] = {
        {h.progressZero, 0, 0, u"0%"}, {h.progressHalf, 50, 50, u"50%"},
        {h.progressFull, 100, 100, u"100%"}, {h.progressBelow, -10, 0, u"0%"},
        {h.progressAbove, 110, 100, u"100%"}
    };
    for (const auto& c : cases) {
        check(bool(c.button.invoke()), "Progress fixture is available");
        auto semantic = h.progress.describe();
        auto frame = arrange(form.page(), form.ui, *appearance, measure, 640, 480, 1.f, form.scroll);
        const auto* p = frame.find(h.progress);
        check(form.progress() == c.source && semantic->numericValue == c.shown && semantic->value == c.text,
              "Source stays unchanged while semantic value is clamped");
        check(p && !p->lines.empty() && p->lines.front().text.find(c.text) != std::u16string::npos &&
              p->progressFill.w == p->progressTrack.w * c.shown / 100,
              "Rendered text and filled track agree");
    }
    h.tabs.activate({"v02"});
    form.list->handles.search.setValue(u"\u5171\u7528\u7bc4\u672c");
    form.list->handles.list.select({"T006"});
    h.tabs.activate({"v03"});
    check(form.progress() == 110 && h.progress.describe()->value == u"100%", "Progress survives page switches");
    check(bool(h.resetV03.invoke()) && form.progress() == 50 && h.progress.describe()->value == u"50%",
          "Reset restores V03 progress to 50%");
    h.tabs.activate({"v02"});
    check(*form.list->handles.search.draft() == u"\u5171\u7528\u7bc4\u672c" && form.list->selection() == OptionValue{"T006"},
          "Resetting V03 preserves V02 search and selection");
    h.tabs.activate({"v01"});
    check(*h.name.draft() == u"kept draft", "Resetting V03 preserves the V01 draft");
}

// The same assembled state driver is exercised by the real-host observation.
// A changed source must be identifiable at the field as well as recoverable.
void x04NameConflictIsDrawnAtField() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); auto& h = form.handles;
        check(bool(x04::nameConflict.drive(form)) && h.name.describe()->sourceConflict && h.latest.describe()->enabled,
              "An external source change retains a conflicting name draft and offers latest");
        test::FakeMeasure measure;
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale), int(size.second * scale), scale, form.scroll, h.name.describe()->id);
        auto* field = frame.find(h.name);
        std::u16string description, caption;
        if (field) {
            for (const auto& line : field->descriptionLines) description += line.text;
            for (const auto& line : field->labelLines) caption += line.text;
        }
        auto semantic = h.name.describe();
        auto cue = appearance->componentPresentation().conflictSuffix;
        bool associated = h.nameLabel.describe()->target == semantic->id && h.nameLabel.describe()->labelKind == LabelKind::name &&
            h.nameHelp.describe()->target == semantic->id && h.nameHelp.describe()->labelKind == LabelKind::description &&
            h.nameError.describe()->target == semantic->id && h.nameError.describe()->labelKind == LabelKind::error && semantic->labels.size() == 3;
        check(field && semantic->visible && semantic->enabled && !semantic->readOnly && !semantic->invalid && associated &&
              semantic->value == u"Draft" && caption.find(semantic->name) != std::u16string::npos &&
              description.find(semantic->description) != std::u16string::npos && !cue.empty() && description.find(cue) != std::u16string::npos,
              "The conflicting name field retains its value and associations and lays out the source-change cue");
        check(bool(x04::nameConflict.restore(form)) && !h.name.describe()->sourceConflict && *h.name.draft() == u"Latest",
              "The latest action restores the field to the current source");
    }
}

void x04NameReadOnlySizeScale() {
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        editForm::Form form(ids, true); auto& name = form.handles.name;
        check(bool(x04::nameReadOnly.drive(form)), "X04 drive name read-only through application fixture");
        check(bool(name.focus()), "Read-only name remains focusable");
        test::FakeMeasure measure;
        auto frame = arrange(form.page(), form.ui, *appearance, measure, int(size.first * scale), int(size.second * scale), scale, form.scroll, name.describe()->id);
        auto* field = frame.find(name); auto semantic = name.describe();
        std::u16string caption, description;
        if (field) {
            for (const auto& line : field->labelLines) caption += line.text;
            for (const auto& line : field->descriptionLines) description += line.text;
        }
        auto suffix = appearance->componentPresentation().readOnlySuffix;
        auto& h = form.handles;
        bool associated = h.nameLabel.describe()->target == semantic->id && h.nameLabel.describe()->labelKind == LabelKind::name &&
            h.nameHelp.describe()->target == semantic->id && h.nameHelp.describe()->labelKind == LabelKind::description &&
            h.nameError.describe()->target == semantic->id && h.nameError.describe()->labelKind == LabelKind::error && semantic->labels.size() == 3;
        check(field && semantic->visible && semantic->enabled && semantic->readOnly && !semantic->invalid && !semantic->sourceConflict && associated &&
              semantic->value == *name.draft() && !suffix.empty() && description.find(semantic->description) != std::u16string::npos &&
              caption.find(semantic->name) != std::u16string::npos && caption.find(suffix) != std::u16string::npos &&
              name.setValue(u"rejected").status == Status::readOnly,
              "X04 read-only semantic restriction and label cue agree at each core size and scale");
        check(bool(x04::nameReadOnly.restore(form)) && !name.describe()->readOnly,
              "X04 restore name after read-only state");
    }
}

void bothAppearancesGiveSameResults() {
    std::vector<std::vector<ComponentSnapshot>> runs;
    std::vector<std::vector<int>> outcomes;
    for (const auto* each : {&webAppearance(), &materialAppearance()}) {
        editForm::Form form(ids);
        auto& ui = form.ui;
        test::FakeMeasure measure;
        std::vector<int> statuses;
        auto step = [&](Result r) { statuses.push_back(static_cast<int>(r.status)); arrange(form.page(), ui, *each, measure, 640, 480, 1.f, form.scroll, detail::access(ui).focused()); };
        step(form.handles.name.focus());
        step(form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size())));
        step(form.handles.name.paste( u"Alpha\r\nBeta"));
        step(form.handles.apply.invoke());
        step(form.handles.readOnly.invoke());
        step(form.handles.name.paste( u"x"));
        step(form.handles.disabled.invoke());
        step(form.handles.apply.invoke());
        step(form.handles.normal.invoke());
        step(form.handles.name.focus());
        step(form.handles.name.select(0, static_cast<unsigned>(form.handles.name.describe()->value.size())));
        step(form.handles.name.paste(u" "));
        step(form.handles.apply.invoke());
        auto frame = arrange(form.page(), ui, *each, measure, 960, 720, 1.f, form.scroll);
        auto field = frame.find(form.handles.name);
        check(field && !field->errorLines.empty() && field->labelLines.front().text == u"顯示名稱",
              "Each appearance carries the name label and error with the field");
        std::vector<ComponentSnapshot> snapshot;
        const auto& h = form.handles;
        for (auto label : {h.title, h.intro, h.nameLabel, h.nameHelp, h.nameError, h.state, h.draft, h.result, h.count})
            snapshot.push_back(*label.describe());
        snapshot.push_back(*h.name.describe());
        for (auto button : {h.normal, h.readOnly, h.disabled, h.error, h.longText, h.reset, h.apply, h.latest})
            snapshot.push_back(*button.describe());
        ComponentSnapshot applied;
        applied.id = "applied";
        applied.value = form.applied() + u"/" + std::u16string(1, char16_t(u'0' + form.applies()));
        snapshot.push_back(applied);
        runs.push_back(std::move(snapshot));
        int depth = 0;  // measured last: walking history re-runs the form's fix-clears-error policy
        while (form.handles.name.undo()) ++depth;
        statuses.push_back(depth);
        outcomes.push_back(statuses);
    }
    check(outcomes[0] == outcomes[1], "Every operation result and the undo depth agree across appearances");
    check(runs[0].size() == runs[1].size(), "Same controls under both appearances");
    for (std::size_t i = 0; i < runs[0].size(); ++i) {
        const auto &a = runs[0][i], &b = runs[1][i];
        check(a.id == b.id && a.value == b.value && a.error == b.error && a.focused == b.focused && a.enabled == b.enabled &&
              a.readOnly == b.readOnly && a.labels == b.labels && a.verbs == b.verbs,
              "Draft, result, errors, focus and history agree across appearances");
    }
}

void presentationCombinationMatrix() {
    presentationMatrix::run(ids, *appearance, [&](editForm::Form& form) {
        return presentationMatrix::Driver{[&](int width, int height, float scale, std::string_view reveal) {
            test::FakeMeasure measure;
            return arrange(form.page(), form.ui, *appearance, measure, width, height, scale, form.scroll, reveal);
        }, {}};
    }, [](bool ok, const std::string& message) { check(ok, message.c_str()); });
}

}

int main() {
    const test::Case cases[] = {
        {"MX01 presentation matrix", presentationCombinationMatrix},
        {"PS01 presentation settings", presentationSettingsAreAtomic},
        {"PS size scaling", presentationSizeScalesExplicitAndDefaultText},
        {"PS02-04 reflow", presentationReflowPreservesEditAndPopup},
        {"PS unfocused field anchor", unfocusedFieldPreservesVisibleCharacter},
        {"PS03 font reflow", fontSwitchReflowsActiveComposition},
        {"PS05-06 reflow", presentationReflowKeepsTooltipAndModal},
        {"PS07 scroll anchor", ps07ListAnchorSurvivesSizeChange},
        {"PS08 large text", ps08LargeTextWrapsWithoutOverlap},
        {"PV02 theme geometry", themeSwitchPreservesShowcaseGeometry},
        {"PV01 palette contrast", themeVariantsHaveAccessiblePalettes},
        {"DN01 V01 layout", compactShowcaseHasSmallerNonoverlappingLayout},
        {"DN02 compact targets", compactTargetsMeetMinimumHeight},
        {"DN01 compact field reflows", compactDensityReflowsFields},
        {"V04 compact checkbox", v04CompactCheckboxAppliesDensity},

        {"V04 justify comparison", v04JustifyComparisonFollowsItsDropdowns},
        {"V04 size slider", v04SliderUpdatesGlobalTextSize},
        {"V04 font selection", v04FontChoiceAppliesAndReportsFallback},
        {"V04 installed catalog and fallback", v04CatalogFontCanBeSelectedAndReset},
        {"N01 long field label and error", longFieldLabelAndErrorKeepTheirAssociation},
        {"N03 radio boundaries in each appearance", radioBoundariesUnderTheAppearance},
        {"N04 slider boundaries in each appearance", sliderNonDivisibleUnderTheAppearance},
        {"N05 disabled tabs in each appearance", disabledTabsUnderTheAppearance},
        {"X01 keyboard journey", keyboardCompletesTheThreePageJourney},
        {"X05 page reset and rerun", resetsReproduceCasesAndPreserveOtherPages},
        {"X04 name conflict has a field cue", x04NameConflictIsDrawnAtField},
        {"X04 name read-only at each size and scale", x04NameReadOnlySizeScale},
        {"X04 executing inventory at each size and scale", x04ExecutingInventoryAtEachSizeAndScale},
        {"X04 fixed placements fit each client", x04FixedPlacementsFitEachClient},
        {"X04 clipped inventory at each size and scale", x04ClippedInventoryAtEachSizeAndScale},
        {"X04 error inventory at each size and scale", [] { x04ErrorInventoryAtEachSizeAndScale(); }},
        {"X04 scrollable text inventory", x04ScrollableTextInventory},
        {"Dark focus inventory", darkFocusInventory},
        {"F72 nothing drawn before input", focusVisibility::nothingIsDrawnBeforeAnyInput},
        {"F72 tab after pointer draws", focusVisibility::tabAfterPointerDrawsIt},
        {"F72 keys open, chords do not", focusVisibility::everyRealKeyOpensButModifierChordsDoNot},
        {"F72 lone Alt and a menu-closing F10 do not show it", focusVisibility::aLoneAltOrAMenuClosingF10DoesNotShowTheIndicator},
        {"F72 pointer hides, wheel does not",focusVisibility::pointerHidesEverywhereButWheelDoesNot},
        {"F72 programmatic focus keeps state", focusVisibility::programmaticFocusKeepsTheCurrentState},
        {"F72 text field exception", focusVisibility::editableTextAlwaysDrawsButReadOnlyFollowsTheRule},
        {"F72 multi-line text field", focusVisibility::multilineTextFollowsTheTextFieldRule},
        {"F72 system keyboard cues", focusVisibility::systemSettingForcesDisplayAndFollowsChanges},
        {"F72 always mode", focusVisibility::alwaysModeForcesDisplayAndRevertsLive},
        {"F72 tooltip after click", focusVisibility::tooltipWaitsForKeyboardAfterAClick},
        {"F72 slider ring", focusVisibility::sliderFocusIsARingAroundTheTitleAndTheTrack},
        {"F72 slider thumb after pointer", focusVisibility::sliderThumbStaysNormalAfterPointerUse},
        {"Dark error inventory", [] { x04ErrorInventoryAtEachSizeAndScale(true); }},
        {"Dark disabled inventory", [] { x04DisabledInventoryAtEachSizeAndScale(true); }},
        {"Dark selected inventory", [] { x04SelectedInventoryAtEachSizeAndScale(true); }},
        {"X04 candidate inventory at each size and scale", x04CandidateInventoryAtEachSizeAndScale},
        {"X04 search read-only at each size and scale", x04SearchReadOnlyAtEachSizeAndScale},
        {"X04 modal inventory at each size and scale", x04ModalInventoryAtEachSizeAndScale},
        {"X04 tooltip at each size and scale", x04TooltipAtEachSizeAndScale},
        {"X04 normal inventory at each size and scale", x04NormalInventoryAtEachSizeAndScale},
        {"X04 disabled inventory at each size and scale", [] { x04DisabledInventoryAtEachSizeAndScale(); }},
        {"X04 group conflicts at each size and scale", x04GroupConflictsAtEachSizeAndScale},
        {"X04 selected inventory at each size and scale", [] { x04SelectedInventoryAtEachSizeAndScale(); }},
        {"X04 hidden inventory at each size and scale", x04HiddenInventoryAtEachSizeAndScale},
        {"DL02 DL03 state matrix", drawCommandStateMatrix},
        {"X04 painted text and semantic baseline", visibleTextAgreesWithSemanticsOnEachPage},
        {"X04 painted state and modal text", paintedStatesAgreeWithSemantics},
        {"Clipped list bounds paint work", listPaintsOnlyVisibleRows},
        {"X04 hidden pages and clipped targets", hiddenAndClippedComponentsAreNotVisibleOrAdmitted},
        {"D01 text survives scale change", d01TextSurvivesScaleChange},
        {"D01 popup survives scale change", d01PopupSurvivesScaleChange},
        {"X02 popup and scroll size/scale matrix", popupAndScrollMatrix},
        {"P08 repeated removal and reset storage", repeatedV03RemoveResetKeepsStorageBounded},
        {"P08 reset and page isolation", v03ResetRestoresTheTemporarySourceAndKeepsOtherPages},
        {"P06 tooltip layout", tooltipLayoutIsReadableAndPointerTransparent},
        {"C11 tooltip lifetime and window focus", tooltipWindowModalAndSourceLifetime},
        {"P05 tooltip pointer visit", tooltipPointerVisitCanRestartAfterEscape},
        {"P05 tooltip keyboard delay and Escape", tooltipKeyboardDelayAndEscape},
        {"R24 tooltip resumes after focus returns", tooltipDelayWaitsForFocusAfterRendererRecovery},
        {"Closed dialog members are hidden", closedDialogMembersAreHiddenAndRejectInput},
        {"P01 cancel dialog", dialogCancelPreservesProgress},
        {"P02 P03 modal input and confirm", dialogConfinesInputAndConfirmsOnce},
        {"P05 pointer focus tooltip", tooltipPointerFocusEndsOnLeave},
        {"P04 removed opener and Escape", dialogEscapeAndRemovedOpener},
        {"P04 removed opener and confirm", dialogRemovedOpenerCanConfirm},
        {"P07 progress display and source", progressDisplaysClampedValuesWithoutChangingTheModel},
        {"L07 L08 R07 page scopes", pagesKeepIndependentDraftsHistoryAndScroll},
        {"L01 L02 V02 navigation in each appearance", v02EmptySingleAndNavigation},
        {"L03 L04 L05 R03 R10 V02 identities", v02IdentityFilterAndDelete},
        {"N08 R11 V02 neighbors", v02DisabledAndFilteredNeighbors},
        {"F08 region confirmation and error order", regionIsInTheFormConfirmationGroup},
        {"F01 five fields apply once", allFiveFieldsApplyOnce},
        {"G11 checkbox conflict is visible and recoverable", checkboxConflictIsVisibleAndCanUseLatest},
        {"X04 assembled checkbox conflict and latest action", assembledCheckboxConflictOffersLatest},
        {"G11 option conflicts are visible and recoverable", optionConflictsAreVisibleAndCanUseLatest},
        {"G11 volume conflict", volumeConflictOffersLatest},
        {"F09 volume confirmation", volumeIsAConfirmedFormField},
        {"F09 checkbox and radio apply together", checkboxAndRadioAreAppliedTogether},
        {"G11 source updates offer the latest value", sourceUpdatesOfferTheLatestValue},
        {"SEM02 SEM03 SEM08 showcase tree", semanticShowcaseStatesAndIdentity},
        {"SEM11 unnamed diagnostic", semanticUnnamedDiagnostic},
        {"Semantic tree reads registered components", semanticTreeReadsRegisteredComponents},
        {"Semantic form operations", semanticFormOperations},
        {"Semantic modal operations", semanticModalOperations},
        {"Semantic list operations", semanticListOperations},
        {"Semantic text and thread", semanticTextAndThread},
        {"Semantic admission priority", semanticAdmissionPriority},
        {"Semantic renderer failure", semanticRendererFailure},
        {"Semantic notifications", semanticNotifications},
        {"Semantic focus driver", semanticFocusDriver},
        {"Semantic subscriber exception boundary", semanticSubscriberExceptionBoundary},
        {"Semantic removal notification", semanticRemovalNotification},
        {"Semantic verb drivers", semanticVerbDrivers},
        {"Semantic wrong-thread drivers", semanticWrongThreadDrivers},
        {"Semantic busy and removed drivers", semanticBusyAndRemovedDrivers},
        {"AC04 initial values, apply and reset", initialValuesApplyAndReset},
        {"AC05 blank name error follows blur and apply", blankNameErrorFollowsBlurAndApply},
        {"AC06 apply button starts once", applyButtonStartsOnce},
        {"AC09 paste, undo and redo never touch the applied result", pasteUndoRedoNeverTouchesApplied},
        {"AC10 read-only and disabled fixtures restrict and recover", readOnlyAndDisabledFixturesRestrictAndRecover},
        {"AC11 keyboard reaches every entry and semantics answer", keyboardReachesEveryEntryAndSemanticsAnswer},
        {"AC12 both appearances give same results", bothAppearancesGiveSameResults},
    };
    int failed = 0;
    for (const auto* each : {&webAppearance(), &materialAppearance()}) {
        appearance = each;
        ids = editForm::defaultIds();
        failed |= test::run(each == &webAppearance() ? "edit_form (web, shipped IDs)" : "edit_form (material, shipped IDs)", cases);
        ids = alternateIds();
        failed |= test::run(each == &webAppearance() ? "edit_form (web, alternate IDs)" : "edit_form (material, alternate IDs)", cases);
    }
    behaviorDensity = Density::compact;
    for (const auto* each : {&webAppearance(), &materialAppearance()}) {
        appearance = each; ids = editForm::defaultIds();
        const test::Case compactCases[] = {
            {"DN03 F01", allFiveFieldsApplyOnce},
            {"DN03 L02", v02EmptySingleAndNavigation},
            {"DN03 P02", dialogConfinesInputAndConfirmsOnce},
        };
        failed |= test::run("compact behavior", compactCases);
    }
    return failed;
}
