// A consumer form unrelated to the shipped edit form: its own IDs, data model, validation
// and callback, composed from C01-C03 and laid out with its own text measurement.
#include "lcars/layout.hpp"
#include <cstdio>
#include <string>

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
    Ui ui({});
    ui.add_label({.id = "profile.alias.caption", .kind = Kind::label, .value = u"Alias", .target = "profile.alias", .role = TextRole::name});
    auto alias = ui.add_text({.id = "profile.alias", .kind = Kind::text, .max_length = 12});
    ui.add_label({.id = "profile.alias.problem", .kind = Kind::label, .target = "profile.alias", .role = TextRole::error});
    auto save = ui.add_button({.id = "profile.save", .kind = Kind::button, .label = u"Save alias"});
    auto summary = ui.add_label({.id = "profile.summary", .kind = Kind::label});
    alias.connect_source({[&] { return profile.alias; }, [&](std::function<void()> callback) {
        changed = std::move(callback);
        return [&] { changed = {}; };
    }});
    summary.connect_source([&] { return u"Saved " + profile.alias; });
    ui.connect_confirmation(save, {alias}, [&](const std::vector<TextChange>& changes) {
        if (changes[0].value.size() < 3) {
            return ConfirmationResult{{Status::invalid_value, u"At least three characters"}, {{0, u"At least three characters"}}};
        }
        profile.alias = changes[0].value;
        ++profile.saves;
        if (changed) changed();
        return ConfirmationResult{};
    });

    alias.focus();
    ui.key_down({.ctrl = true, .letter = u'A'});
    ui.text_input(u"ab");
    expect(save.invoke().status == Status::invalid_value && profile.saves == 0, "consumer validation rejects a short alias");
    expect(alias.describe()->error == u"At least three characters", "consumer error is attached to its own field");
    ui.text_input(u"cdef");
    save.focus();
    ui.key_down({.key = Key::enter});
    expect(profile.alias == u"abcdef" && profile.saves == 1, "consumer callback writes its existing data model once");
    expect(ui.focused() == "profile.save" && ui.text_input(u"x").status == Status::invalid_value && alias.describe()->value == u"abcdef",
           "a focused button refuses typed text and the field is untouched");
    alias.focus();
    expect(alias.paste(u"0123456789").status == Status::invalid_value, "consumer-chosen length limit is enforced");

    Monospace measure;
    ScrollState scroll;
    auto page = layout::column({layout::control("profile.alias"), layout::row({layout::control("profile.save"), layout::control("profile.summary")})}, 8, 16);
    auto frame = arrange(page, ui, material_appearance(), measure, 480, 320, 1.f, scroll);
    const auto* field = frame.find("profile.alias");
    expect(field && field->label_lines.size() == 1 && field->label_lines[0].text == u"Alias", "framework layout carries the consumer's label");
    const auto* placement = frame.find("profile.save");
    const auto* hit = placement ? frame.hit(placement->bounds.x + 1, placement->bounds.y + 1) : nullptr;
    expect(hit && hit->id == "profile.save", "consumer IDs are hit-testable");
    std::printf("%s\n", failures ? "consumer FAILED" : "consumer OK");
    return failures ? 1 : 0;
}
