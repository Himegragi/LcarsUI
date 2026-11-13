// A consumer form unrelated to the shipped edit form: its own IDs, data model, validation
// and callback, composed from C01-C03 and laid out with its own text measurement.
#include "lcars/layout.hpp"
#include "lcars/semantics.hpp"
#include "lcars/version.hpp"
#include "handle_kinds.hpp"
static_assert(lcars::framework_version == LCARS_PACKAGE_VERSION);
static_assert(LCARS_VERSION_MAJOR == 0 && LCARS_VERSION_MINOR == 2 && LCARS_VERSION_PATCH == 0);
static_assert(lcars::framework_version_major == 0 && lcars::framework_version_minor == 2 && lcars::framework_version_patch == 0);
static_assert(!std::is_constructible_v<lcars::SemanticTree, lcars::detail::UiState&>);
#include <concepts>
#include <cstdio>
#include <string>
#include <type_traits>

static_assert(!std::is_convertible_v<lcars::BoolSource, lcars::OptionSource>);
static_assert(!std::is_convertible_v<lcars::BoolSource, lcars::NumericSource>);
static_assert(!std::is_convertible_v<lcars::TextSource, lcars::NumericSource>);
static_assert(!std::is_convertible_v<lcars::TextSource, lcars::BoolSource>);
static_assert(!std::is_convertible_v<bool, lcars::OptionValue>);
// Handles compare within their own kind only (#94): equal when they control the same thing, a compile error across kinds.
static_assert(handle_kinds::equality_holds);
static_assert(handle_kinds::construction_holds);
static_assert(std::tuple_size_v<handle_kinds::All> == 14);
static_assert(std::equality_comparable<lcars::ComponentHandle> && std::equality_comparable<lcars::ConfirmField>);
static_assert(!std::equality_comparable_with<lcars::TextHandle, lcars::CheckboxHandle>);  // one case of the list's rule, spelled out
// Applications use typed handles; string operations belong to the semantic layer, not Ui.
template <class T> concept HasFind = requires(T& ui, std::string_view id) { ui.find(id); };
static_assert(!HasFind<lcars::Ui>);
template <class T> concept HasText = requires(T& ui, std::string_view id) { ui.text(id); };
static_assert(!HasText<lcars::Ui>);
template <class T> concept HasEdit = requires(T& ui, std::string_view id) { ui.edit(id); };
static_assert(!HasEdit<lcars::Ui>);
template <class T> concept HasSetValue = requires(T& ui, std::string_view id) { ui.set_value(id, std::u16string{}); };
static_assert(!HasSetValue<lcars::Ui>);
template <class T> concept HasInvoke = requires(T& ui, std::string_view id) { ui.invoke(id); };
static_assert(!HasInvoke<lcars::Ui>);
template <class T> concept HasBind = requires(T& ui, std::string_view id) { ui.bind(std::string(id), [] { return lcars::Result{}; }); };
static_assert(!HasBind<lcars::Ui>);
template <class T> concept HasBindText = requires(T& ui, std::string_view id) { ui.bind_text(id, [] { return std::u16string{}; }); };
static_assert(!HasBindText<lcars::Ui>);
template <class T> concept HasFocus = requires(T& ui, std::string_view id) { ui.focus(id); };
static_assert(!HasFocus<lcars::Ui>);
template <class T> concept HasPointerDown = requires(T& ui, std::string_view id) { ui.pointer_down(id); };
static_assert(!HasPointerDown<lcars::Ui>);
template <class T> concept HasPointerUp = requires(T& ui, std::string_view id) { ui.pointer_up(id); };
static_assert(!HasPointerUp<lcars::Ui>);
template <class T> concept HasUndo = requires(T& ui, std::string_view id) { ui.undo(id); };
static_assert(!HasUndo<lcars::Ui>);
template <class T> concept HasRedo = requires(T& ui, std::string_view id) { ui.redo(id); };
static_assert(!HasRedo<lcars::Ui>);
template <class T> concept HasPaste = requires(T& ui, std::string_view id) { ui.paste(id, std::u16string_view{}); };
static_assert(!HasPaste<lcars::Ui>);
template <class T> concept HasSelect = requires(T& ui, std::string_view id) { ui.select(id, 0, 0); };
static_assert(!HasSelect<lcars::Ui>);
template <class T> concept HasCopy = requires(T& ui, std::string_view id) { ui.copy(id); };
static_assert(!HasCopy<lcars::Ui>);
template <class T> concept HasCut = requires(T& ui, std::string_view id) { ui.cut(id); };
static_assert(!HasCut<lcars::Ui>);
template <class T> concept HasEnable = requires(T& ui, std::string_view id) { ui.enable(id, true); };
static_assert(!HasEnable<lcars::Ui>);
template <class T> concept HasReadOnly = requires(T& ui, std::string_view id) { ui.set_read_only(id, true); };
static_assert(!HasReadOnly<lcars::Ui>);
template <class T> concept HasSetError = requires(T& ui, std::string_view id) { ui.set_error(id, std::u16string{}); };
static_assert(!HasSetError<lcars::Ui>);
template <class T> concept HasReplaceText = requires(T& ui, std::string_view id) { ui.replace_text(id, std::u16string{}); };
static_assert(!HasReplaceText<lcars::Ui>);
template <class T> concept HasAvailable = requires(T& ui, std::string_view id) { ui.available(id); };
static_assert(!HasAvailable<lcars::Ui>);
template <class T> concept HasInput = requires(T& ui, std::string_view id) { ui.text_input(std::u16string_view{}); };
static_assert(!HasInput<lcars::Ui>);

template<class T> concept FindsByString = requires(T& frame, std::string_view id) { frame.find(id); };
static_assert(!FindsByString<lcars::Frame>);
template<class T> concept LaysOutString = requires(T id) { lcars::layout::control(id); };
static_assert(!LaysOutString<std::string>);

namespace {
// The application's pre-existing data model; LcarsUI never owns or copies it.
struct Profile {
    std::u16string alias = u"guest";
    int saves = 0;
};
// Monospace stand-in for a real shaping backend.
struct Monospace final : lcars::TextMeasure {
    lcars::TextRun measure(std::u16string_view text, int pixels) override {
        lcars::TextRun run;
        for (unsigned i = 0; i <= text.size(); ++i) run.stops.push_back({i, i * pixels * 0.6f});
        run.width = text.size() * pixels * 0.6f;
        return run;
    }
    int line_height(int pixels) override { return pixels + 4; }
};
int failures = 0;
void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}

int main() {
    using namespace lcars;
    Profile profile;
    std::function<void()> changed;
    Ui ui;
    ui.add_label({.id = "profile.alias.caption", .kind = Kind::label, .value = u"Alias", .target = "profile.alias", .role = TextRole::name}).value.value();
    auto alias = ui.add_text({.id = "profile.alias", .kind = Kind::text, .max_length = 12}).value.value();
    ui.add_label({.id = "profile.alias.problem", .kind = Kind::label, .target = "profile.alias", .role = TextRole::error}).value.value();
    auto save = ui.add_button({.id = "profile.save", .kind = Kind::button, .label = u"Save alias"}).value.value();
    auto summary = ui.add_label({.id = "profile.summary", .kind = Kind::label}).value.value();
    alias.connect_source({[&] { return profile.alias; }, [&](std::function<void()> callback) {
        changed = std::move(callback);
        return [&] { changed = {}; };
    }});
    summary.connect_source([&] { return u"Saved " + profile.alias; });
    ui.connect_confirmation(save, {alias}, [&](const std::vector<TextChange>& changes) {
        if (changes[0].value.size() < 3) {
            return ConfirmationResult{{Status::invalid_value, u"At least three characters"}, {{changes[0].field, u"At least three characters"}}};
        }
        profile.alias = changes[0].value;
        ++profile.saves;
        if (changed) changed();
        return ConfirmationResult{};
    });

    alias.focus();
    alias.set_value(u"ab");
    auto handle_status = save.invoke().status;
    auto semantic_status = ui.semantics().perform({"profile.save"}, Verb::invoke).status;
    expect(handle_status == Status::invalid_value && semantic_status == handle_status && profile.saves == 0, "consumer validation agrees through both interfaces");
    expect(alias.describe()->error == u"At least three characters", "consumer error is attached to its own field");
    expect(ui.semantics().perform({"profile.alias"}, Verb::set_value, std::u16string(u"abcdef")).status == Status::ok &&
           ui.semantics().text("profile.alias").value == u"abcdef", "semantic editing updates the draft");
    expect(ui.semantics().perform({"profile.alias"}, Verb::select, Selection{1, 3}).status == Status::ok &&
           ui.semantics().perform({"profile.alias"}, Verb::undo).status == Status::ok &&
           ui.semantics().text("profile.alias").value == u"ab" &&
           ui.semantics().perform({"profile.alias"}, Verb::redo).status == Status::ok,
           "semantic selection and history use the field behavior");
    save.focus();
    save.invoke();
    expect(profile.alias == u"abcdef" && profile.saves == 1, "consumer callback writes its existing data model once");
    expect(!alias.describe()->focused && alias.describe()->value == u"abcdef",
           "focus moves to the save button and the field is untouched");
    alias.focus();
    expect(alias.paste(u"0123456789").status == Status::invalid_value, "consumer-chosen length limit is enforced");

    Monospace measure;
    ScrollState scroll;
    auto page = layout::column({layout::control(alias), layout::row({layout::control(save), layout::control(summary)})}, 8, 16);
    auto frame = arrange(page, ui, material_appearance(), measure, 480, 320, 1.f, scroll);
    const auto* field = frame.find(alias);
    expect(field && field->label_lines.size() == 1 && field->label_lines[0].text == u"Alias", "framework layout carries the consumer's label");
    const auto* placement = frame.find(save);
    const auto* hit = placement ? frame.hit(placement->bounds.x + 1, placement->bounds.y + 1) : nullptr;
    expect(hit && hit->id == "profile.save", "consumer IDs are hit-testable");
    std::printf("%s\n", failures ? "consumer FAILED" : "consumer OK");
    return failures ? 1 : 0;
}
