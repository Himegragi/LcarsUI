#include "check.hpp"
#include "desktop_linux_test.hpp"
#include "isoduo/semantics.hpp"
#include "render_role_checks.hpp"
#include "text_geometry_checks.hpp"
#include "utf.hpp"
#include <array>
#include <iomanip>

using namespace isoduo;
using test::check;

namespace {
constexpr int width = 580, height = 760;
constexpr auto singleText = u"A\U0001f600B";
constexpr auto multiText = u"alpha beta\nsecond\nthird row\nIsoduo 台灣 office i\u0301 fi 一二三四五六七八九十\n"
                          u"first long wrapped line has interleaved 漢字 and office ligatures followed by a second "
                          u"piece with more words and trailing whitespace   \nfifth\nsixth";
constexpr auto richText = u"Styling 中文 office fi i\u0301 日月山河 with bold and italic advances and trailing spaces   "
                         u"more ligatures and mixed script until the line wraps\nSecond paragraph\nThird paragraph";

struct Fixture {
    Ui ui;
    ScrollState scroll;
    TextFieldHandle single = ui.addTextField({.id = "single", .label = u"Single line"}).value.value();
    TextFieldHandle multi = ui.addTextField({.id = "multi", .label = u"Multiple lines",
                                            .multiline = true, .visibleLines = 3}).value.value();
    TextFieldHandle rich = ui.addTextField({.id = "rich", .label = u"Styled text", .multiline = true,
                                            .visibleLines = 3, .rich = true}).value.value();
    LabelHandle horizontal = ui.addLabel({.id = "horizontal", .value = richText}).value.value();
    LabelHandle vertical;
    unsigned reports = 0, shortcuts = 0, saves = 0, phase = 0;
    RichText saved{richText, {{.start = 0, .length = 7, .italic = true, .color = {}}}};

    Fixture() {
        std::u16string sample;
        for (int i = 0; i < 12; ++i) sample += u"臺灣，123。ABC \U0001f600　「直書」\n";
        sample.pop_back();
        vertical = ui.addLabel({.id = "vertical", .value = sample, .writingMode = WritingMode::vertical}).value.value();
        connect(single, singleText);
        connect(multi, multiText);
        check(bool(rich.connectRichSource(Source<RichText>{[&] { return saved; }, subscribe})), "Connect styled source");
        auto save = button("save", u"Save styles (Ctrl+S)");
        check(ui.connectConfirmation(save, {rich}, [&](const auto& changes) {
            saved = std::get<Change<TextFieldHandle, RichText>>(changes.front()).value;
            ++saves;
            return ConfirmationResult{};
        }) && ui.addShortcut({.letter = u'S', .ctrl = true}, save), "Connect styled confirmation");
        bind("report", u"Report (Ctrl+G)", {.ctrl = true, .letter = u'G'}, [&] { report(); });
        bind("shortcut", u"Ctrl+Enter", {.key = Key::enter, .ctrl = true}, [&] { ++shortcuts; });
        bind("words", u"Word mode (Ctrl+W)", {.ctrl = true, .letter = u'W'}, [&] {
            constexpr std::array modes{WordSegmentation::dictionary, WordSegmentation::perCharacter, WordSegmentation::scriptRun};
            auto current = std::find(modes.begin(), modes.end(), *multi.wordSegmentation());
            check(bool(multi.setWordSegmentation(modes[(current - modes.begin() + 1) % modes.size()])), "Change word mode");
        });
        bind("flow", u"Flow (Ctrl+P)", {.ctrl = true, .letter = u'P'}, [&] { flow((phase + 1) % 7); });
        bind("rows", u"Visible lines (Ctrl+L)", {.ctrl = true, .letter = u'L'}, [&] {
            check(bool(multi.setVisibleLines(*multi.visibleLines() == 3 ? 2 : 3)), "Change the viewport size");
        });
    }

    static std::function<void()> subscribe(std::function<void()>) { return [] {}; }
    static void connect(TextFieldHandle field, const char16_t* initial) {
        check(field.connectSource(Source<std::u16string>{[initial] { return std::u16string(initial); }, subscribe}) &&
              field.connectConfirmed([](const auto&) { return Result{}; }), "Connect text field");
    }
    ButtonHandle button(std::string id, std::u16string label) {
        return ui.addButton({.id = std::move(id), .label = std::move(label)}).value.value();
    }
    void bind(std::string id, std::u16string label, Shortcut key, std::function<void()> action) {
        auto target = button(std::move(id), std::move(label));
        check(target.bind([action = std::move(action)] { action(); return Result{}; }) &&
              ui.addShortcut(key, target), "Bind fixture shortcut");
    }
    auto page() {
        using namespace layout;
        return column({component(single), component(multi), component(rich), component(horizontal), verticalText(vertical, 96)}, 12, 16);
    }
    void flow(unsigned next) {
        phase = next;
        constexpr std::array alignments{TextAlign::left, TextAlign::center, TextAlign::right};
        constexpr std::array methods{JustifyMethod::automatic, JustifyMethod::spaces, JustifyMethod::perCharacter};
        const auto align = next < alignments.size() ? alignments[next] : TextAlign::justify;
        const auto method = next < alignments.size() ? JustifyMethod::automatic : methods[(next - 3) % methods.size()];
        for (auto field : {multi, rich})
            check(field.setTextAlign(align) && field.setJustifyMethod(method) &&
                  field.setLineSpacing(next ? 1.25 : 1.0) && field.setParagraphSpacing(next ? 8 : 0), "Change field flow");
        check(horizontal.setTextAlign(next < 3 ? align : TextAlign::distribute) && horizontal.setJustifyMethod(method) &&
              horizontal.setLineSpacing(next ? 1.25 : 1.0) && horizontal.setParagraphSpacing(next ? 8 : 0),
              "Change horizontal label flow");
        check(vertical.setVerticalDigits(static_cast<int>(next % 4)) && vertical.setLineSpacing(next ? 1.25 : 1.0),
              "Change vertical layout");
        auto settings = ui.presentation();
        settings.justifyAsLeft = next == 6;
        settings.fontFamily = next == 6 ? u"DejaVu Sans" : u"";
        check(bool(ui.setPresentation(settings)), "Change layout preference");
    }
    void report() {
        const auto tree = ui.semantics();
        ++reports;
        for (const char* id : {"single", "multi", "rich", "vertical"}) {
            const auto node = tree.get({id}).value.value();
            std::cout << "Node " << reports << ' ' << id << ' ' << node.focused;
            if (node.bounds)
                std::cout << ' ' << node.bounds->x << ' ' << node.bounds->y << ' ' << node.bounds->w << ' ' << node.bounds->h;
            if (node.textSelection)
                std::cout << ' ' << node.textSelection->anchor << ' ' << node.textSelection->caret;
            if (node.scroll)
                std::cout << ' ' << node.scroll->visibleLines << ' ' << node.scroll->firstLine;
            std::cout << '\n';
            if (node.textSelection) {
                auto value = detail::utf8FromUtf16(tree.text(id).value.value());
                for (std::size_t pos = 0; (pos = value.find('\n', pos)) != std::string::npos; pos += 2)
                    value.replace(pos, 1, "\\n");
                std::cout << "Text " << reports << ' ' << id << ' ' << std::quoted(value) << '\n';
            }
        }
        auto words = tree.get({"multi"}).value.value();
        auto word = tree.textUnit("multi", TextUnitKind::word, 1).value.value();
        check(multi.wordAt(1).value == word && multi.wordSegmentationInUse().value == words.wordSegmentationInUse,
              "Typed and semantic word queries agree");
        check(tree.get({"rich"})->styles == rich.richValue()->spans, "Typed and semantic styles agree");
        check(words.scroll->visibleLines == multi.visibleLines().value && words.scroll->firstLine == multi.scrollLine().value,
              "Typed and semantic viewports agree");
        std::cout << "Report " << reports << ' ' << shortcuts << ' ' << saves << ' ' << phase << ' '
                  << static_cast<int>(*words.wordSegmentation) << ' ' << static_cast<int>(*words.wordSegmentationInUse)
                  << ' ' << word.begin << ' ' << word.end << ' ' << rich.richValue()->spans.size()
                  << ' ' << saved.spans.size() << std::endl;
    }
};

void compare(Fixture& fixture, Desktop& desktop) {
    check(fixture.rich.focus() && fixture.rich.select(0, 2), "Select two UTF-16 units in the styled run");
    for (unsigned phase = 0; phase < 7; ++phase) {
        fixture.flow(phase);
        const float scale = phase == 6 ? 1.25f : 1.f;
        Frame frame;
        const auto result = detail::LinuxDesktopTest::compareFrame(
            desktop, static_cast<int>(width * scale), static_cast<int>(height * scale), scale, frame);
        const auto* placement = frame.find(fixture.rich);
        check(placement && test::caretMatchesHit(frame, fixture.rich, 0, 2), "The styled caret round-trips through hit testing");
        check(result && test::hasPaintRole(result, "rich", detail::PaintRole::caret) &&
              test::hasPaintRole(result, "rich", detail::PaintRole::selection),
              "The CPU and OpenGL images agree on the selection and caret");
        check(test::matchesVerticalLabel(result, frame.find(fixture.vertical)), "The vertical glyphs render in the same area");
        const auto caret = placement->caretRect(2);
        const auto region = std::find_if(result.regions.begin(), result.regions.end(), [](const auto& item) {
            return item.owner == "rich" && item.role == static_cast<unsigned>(detail::PaintRole::caret);
        });
        check(region != result.regions.end() && region->bounds.x == caret.x && region->bounds.y == caret.y,
              "The painted caret starts at the position used for hit testing");
        std::cout << "PASS flow " << phase << " scale " << scale << " caret " << caret.x << ' ' << caret.y << " different pixels "
                  << result.comparison.differentPixels << std::endl;
    }
    fixture.flow(0);
    check(bool(fixture.rich.select(0, 0)), "Start a drag from the first UTF-16 offset");
    auto frame = detail::LinuxDesktopTest::layoutFrame(desktop, width, height, 1.f);
    const auto& view = frame.find(fixture.rich)->view;
    check(view.lines.front().soft, "The first display line ends at a soft wrap");
    const unsigned wrapPoint = view.lines.front().end;
    check(frame.selectTo(fixture.ui, fixture.rich, view.text.x + view.text.w - 1, view.text.y + 1),
          "Extend the selection to the end of the first line");
    frame = detail::LinuxDesktopTest::layoutFrame(desktop, width, height, 1.f);
    check(test::caretMatchesHit(frame, fixture.rich, 0, wrapPoint), "The upstream caret stays on the first row");
    check(fixture.rich.select(wrapPoint, wrapPoint) && fixture.rich.setScrollLine(1), "Scroll the downstream row to the top");
    frame = detail::LinuxDesktopTest::layoutFrame(desktop, width, height, 1.f);
    check(frame.find(fixture.rich)->view.first == 1 && test::caretMatchesHit(frame, fixture.rich, 1, wrapPoint),
          "The downstream caret is hit at the start of the scrolled row");
    std::cout << "PASS upstream and scrolled downstream wrap boundary at UTF-16 offset " << wrapPoint << std::endl;
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "Usage: isoduo_linux_text_geometry <wayland|x11> <input|compare>");
        const std::string_view backend = argv[1], mode = argv[2];
        check(backend == "wayland" || backend == "x11", "Choose a native backend");
        check(mode == "input" || mode == "compare", "Choose real input or CPU/OpenGL comparison");
        Fixture fixture;
        DesktopConfig config;
        config.backend = backend == "wayland" ? DesktopBackend::wayland : DesktopBackend::x11;
        config.title = u"Isoduo text geometry";
        config.width = width;
        config.height = height;
        auto opened = Desktop::create(fixture.ui, webAppearance(), [&] { return fixture.page(); }, fixture.scroll, config);
        if (opened.failure) std::cerr << detail::utf8FromUtf16(opened.failure->message) << '\n';
        check(opened.desktop && opened.desktop->nativeWindow()->backend == config.backend, "Open the requested native window");
        const auto& desktop = *opened.desktop;
        const auto caps = desktop.capabilities();
        std::cout << "Window " << desktop.nativeWindow()->windowId << ' ' << backend << ' '
                  << detail::utf8FromUtf16(caps.version) << ' ' << detail::utf8FromUtf16(caps.renderer) << std::endl;
        if (mode == "compare") compare(fixture, *opened.desktop);
        else check(opened.desktop->run().status == Status::ok, "Finish the native input trial");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
