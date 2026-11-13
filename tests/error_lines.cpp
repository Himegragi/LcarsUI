// Error and conflict lines under a component (#92): the layout of the lines that sit below a
// text field, a checkbox, a radio group, a dropdown and a slider when the component carries an
// error message or a source conflict. The four non-field types share one arrange step for them;
// this suite pins every Placement rectangle so that sharing it changes nothing.
//
// Seams under test (confirmed by the ticket's Agent Brief):
//   - lcars::arrange(layout::column(...), ui, appearance, measure, ...) -> Placement rectangles
//   - Ui::add_* handles, connect_source (a conflict) and detail::UiState::set_error (an error)
//
// The expected values are not computed here. They are the rectangles the layout produced before
// the refactor, recorded by `lcars_error_lines_tests --dump` into error_lines_expected.hpp.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "core_internal.hpp"
#include "check.hpp"
#include "fake_measure.hpp"
#include "error_lines_expected.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using namespace lcars;
using test::check;
namespace {
// One long message: it wraps to several lines in the narrow column, so line stacking shows.
const std::u16string long_error = u"This value is not accepted because it is much too long for the field";

// A component with its error and/or conflict state set up. A checkbox has two values, so its draft can
// never differ from both the confirmed and the latest source value: it has no conflict state.
struct Subject {
    const char* name;
    bool has_conflict;
    std::function<layout::Node(Ui&, const std::string& id, bool conflict, bool error)> add;
};
// A source the test can move: `model` is what the source reads, `notify` tells the component it changed.
struct Source {
    std::function<std::function<void()>(std::function<void()>)> subscribe() {
        return [this](std::function<void()> callback) { notify = std::move(callback); return [this] { notify = {}; }; };
    }
    std::function<void()> notify;
};
std::vector<std::unique_ptr<Source>>& sources() { static std::vector<std::unique_ptr<Source>> all; return all; }
Source& new_source() { sources().push_back(std::make_unique<Source>()); return *sources().back(); }

const std::vector<OptionItem> options = {{{"a"}, u"Alpha"}, {{"b"}, u"Beta"}, {{"c"}, u"Gamma"}};
ComponentConfig control(const std::string& id, const char16_t* label) { ComponentConfig c; c.id = id; c.label = label; return c; }

void put_error(Ui& ui, const std::string& id, bool error) { if (error) check(bool(detail::access(ui).set_error(id, long_error)), "an error is set"); }

const Subject subjects[] = {
    {"text", true, [](Ui& ui, const std::string& id, bool conflict, bool error) -> layout::Node {
        auto field = ui.add_text(control(id, u"Name")).value.value();
        auto& source = new_source(); auto model = std::make_shared<std::u16string>(u"original");
        if (conflict) {
            field.connect_source({[model] { return *model; }, source.subscribe()});
            // A text field keeps a draft against a changed source only while a confirmation holds it.
            auto confirm = ui.add_button(control(id + ".apply", u"Apply")).value.value();
            check(bool(ui.connect_mixed_confirmation(confirm, {field}, [](const std::vector<FieldChange>&) { return MixedConfirmationResult{}; })), "the text field is confirmed");
            field.set_value(u"draft"); *model = u"newest"; source.notify();
            check(field.describe()->source_conflict, "the text field reports a conflict");
        }
        put_error(ui, id, error);
        return layout::control(field);
    }},
    {"checkbox", false, [](Ui& ui, const std::string& id, bool, bool error) -> layout::Node {
        auto checkbox = ui.add_checkbox(control(id, u"Remember me")).value.value();
        put_error(ui, id, error);
        return layout::control(checkbox);
    }},
    {"radio", true, [](Ui& ui, const std::string& id, bool conflict, bool error) -> layout::Node {
        auto group = ui.add_radio_group(control(id, u"Mode"), options).value.value();
        auto& source = new_source(); auto model = std::make_shared<std::optional<OptionValue>>(OptionValue{"a"});
        if (conflict) {
            group.connect_source({[model] { return *model; }, source.subscribe()});
            check(bool(group.connect_confirmed([](const Change<RadioGroupHandle>&) { return Result{}; })), "the radio group holds a draft");
            group.select({"b"}); *model = OptionValue{"c"}; source.notify();
            check(group.describe()->source_conflict, "the radio group reports a conflict");
        }
        put_error(ui, id, error);
        return layout::control(group);
    }},
    {"dropdown", true, [](Ui& ui, const std::string& id, bool conflict, bool error) -> layout::Node {
        auto box = ui.add_dropdown(control(id, u"Region"), options).value.value();
        auto& source = new_source(); auto model = std::make_shared<std::optional<OptionValue>>(OptionValue{"a"});
        if (conflict) {
            box.connect_source({[model] { return *model; }, source.subscribe()});
            check(bool(box.connect_confirmed([](const Change<DropdownHandle>&) { return Result{}; })), "the dropdown holds a draft");
            box.select({"b"}); *model = OptionValue{"c"}; source.notify();
            check(box.describe()->source_conflict, "the dropdown reports a conflict");
        }
        put_error(ui, id, error);
        return layout::control(box);
    }},
    {"slider", true, [](Ui& ui, const std::string& id, bool conflict, bool error) -> layout::Node {
        auto slider = ui.add_slider(control(id, u"Volume"), {0, 100, 5}).value.value();
        auto& source = new_source(); auto model = std::make_shared<int>(0);
        if (conflict) {
            slider.connect_source({[model] { return *model; }, source.subscribe()});
            check(bool(slider.connect_confirmed([](const Change<SliderHandle>&) { return Result{}; })), "the slider holds a draft");
            slider.set_value(10); *model = 20; source.notify();
            check(slider.describe()->source_conflict, "the slider reports a conflict");
        }
        put_error(ui, id, error);
        return layout::control(slider);
    }},
};
struct State { const char* name; bool conflict, error; };
const State states[] = {{"error", false, true}, {"conflict", true, false}, {"both", true, true}};

struct Run { const Appearance& appearance; const char* appearance_name; Density density; const char* density_name; float scale; const char* scale_name; };
std::vector<Run> runs() {
    std::vector<Run> out;
    for (const auto& [appearance, name] : {std::pair<const Appearance*, const char*>{&web_appearance(), "web"}, {&material_appearance(), "material"}})
        for (const auto& [density, density_name] : {std::pair<Density, const char*>{Density::standard, "standard"}, {Density::compact, "compact"}})
            for (const auto& [scale, scale_name] : {std::pair<float, const char*>{1.f, "1"}, {1.5f, "1.5"}})
                out.push_back({*appearance, name, density, density_name, scale, scale_name});
    return out;
}

std::string rect(Rect r) { return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h); }
std::string lines(const std::vector<Line>& all) {
    std::string out;
    for (const auto& l : all) out += "(" + rect(l.bounds) + "|" + std::to_string(l.text.size()) + ")";
    return out;
}
// Every rectangle of a placement the error lines can move: the box, the clip, each group of lines, the options, the
// slider track, and the text field's rectangles. Text is recorded by its length (the sources are ASCII-free of surprises).
std::string describe(const Placement& p) {
    std::string out = "bounds=" + rect(p.bounds) + " clip=" + rect(p.clip) + " lines=" + lines(p.lines) + " label=" + lines(p.label_lines) +
                      " description=" + lines(p.description_lines) + " error=" + lines(p.error_lines);
    out += " options=";
    for (const auto& o : p.options) out += "(" + rect(o) + ")";
    out += " track=" + rect(p.slider_track);
    out += " field=" + rect(p.field.label) + ";" + rect(p.field.surface) + ";" + rect(p.field.editor) + ";" + rect(p.field.description) + ";" + rect(p.field.error) + ";" + std::to_string(p.field.height);
    return out;
}

// Every subject in every state in one column, arranged under each appearance x density x scale.
std::map<std::string, std::string> record() {
    std::map<std::string, std::string> all;
    // 240: lines wrap to several rows. 40: nearly every word is wider than the column. 0: a column with no width at all.
    for (const auto& run : runs()) for (int width : {240, 40, 0}) {
        Ui ui; ScrollState scroll; test::FakeMeasure measure;
        std::vector<layout::Node> nodes; std::vector<std::string> ids;
        for (const auto& subject : subjects)
            for (const auto& state : states) {
                if (state.conflict && !subject.has_conflict) continue;
                const std::string id = std::string(subject.name) + "." + state.name;
                nodes.push_back(subject.add(ui, id, state.conflict, state.error));
                auto handle = detail::access(ui).describe(id);
                check(handle.has_value() && handle->invalid == state.error && handle->source_conflict == state.conflict, ("state set up: " + id).c_str());
                ids.push_back(id);
            }
        auto settings = ui.presentation(); settings.density = run.density;
        check(bool(ui.set_presentation(settings)), "presentation accepted");
        auto frame = arrange(layout::column(nodes), ui, run.appearance, measure, width, 8000, run.scale, scroll);
        for (const auto& id : ids) {
            auto found = std::find_if(frame.items.begin(), frame.items.end(), [&](const Placement& p) { return p.id == id; });
            check(found != frame.items.end(), ("arranged: " + id).c_str());
            all[std::string(run.appearance_name) + " " + run.density_name + " " + run.scale_name + " w" + std::to_string(width) + " " + id] = describe(*found);
        }
    }
    return all;
}

void every_error_and_conflict_layout_is_unchanged() {
    const auto actual = record();
    check(actual.size() == expected_count, "the recorded set has the expected number of entries");
    std::string failures;
    for (const auto& entry : expected) {
        auto it = actual.find(entry.key);
        if (it == actual.end()) { failures += std::string("missing ") + entry.key + "\n"; continue; }
        if (it->second != entry.layout) failures += std::string("changed ") + entry.key + "\n  was " + entry.layout + "\n  now " + it->second + "\n";
    }
    if (!failures.empty()) { std::fputs(failures.c_str(), stderr); }
    check(failures.empty(), "no recorded layout changed");
}
}
int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        std::puts("// Recorded by `lcars_error_lines_tests --dump` before the refactor of #92; see error_lines.cpp.");
        std::puts("#pragma once");
        std::puts("#include <cstddef>");
        std::puts("namespace {");
        std::puts("struct Recorded { const char* key; const char* layout; };");
        std::map<std::string, std::string> all;
        try { all = record(); } catch (const std::exception& e) { std::fprintf(stderr, "dump failed: %s\n", e.what()); return 1; }
        std::printf("constexpr std::size_t expected_count = %zu;\n", all.size());
        std::puts("const Recorded expected[] = {");
        for (const auto& [key, layout] : all) std::printf("    {\"%s\",\n     \"%s\"},\n", key.c_str(), layout.c_str());
        std::puts("};");
        std::puts("}");
        return 0;
    }
    static const test::Case cases[] = {
        {"every_error_and_conflict_layout_is_unchanged", every_error_and_conflict_layout_is_unchanged},
    };
    return test::run("error_lines", cases);
}
