// Interactive clipboard probe. Run on a native desktop alongside a clipboard
// peer. The application logs observable field changes and style spans.
#include "check.hpp"
#include "clipboard_text.hpp"
#include "isoduo/desktop.hpp"
#include "isoduo/semantics.hpp"
#include "utf.hpp"

using namespace isoduo;
using isoduo::detail::utf8FromUtf16;
using test::check;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode = argv[1];
    const bool plain = mode == "plain";
    const bool large = mode == "large";
    const bool source = large || mode == "source" || mode == "readonly";
    Ui ui;
    ScrollState scroll;
    ComponentConfig config{.id = "clipboard", .label = u"Clipboard", .value = u"replace me"};
    config.multiline = config.rich = !plain;
    config.visibleLines = 4;
    config.maxLength = 120000;
    auto field = ui.addTextField(config).value.value();
    RichText initial{large ? std::u16string(24000, u'漢') : source ? u"繁體中文\nIsoduo" : u"replace me", {}};
    if (source && !large) initial.spans = {{.start = 0, .length = 4, .bold = true, .color = "teal"}};
    auto subscribe = [](std::function<void()>) { return [] {}; };
    if (plain) {
        check(bool(field.connectSource({[&] { return initial.text; }, subscribe})), "plain source");
        check(bool(field.connectConfirmed([](auto&) { return Result{}; })), "plain write timing");
    } else {
        check(bool(field.connectRichSource({[&] { return initial; }, subscribe})), "rich source");
        check(bool(field.connectRichConfirmed([](auto&) { return Result{}; })), "rich write timing");
    }
    if (mode == "readonly") check(bool(field.readOnly(true)), "read-only field");
    check(bool(field.focus()), "initial focus");
    auto log = [&] {
        const auto value = plain ? RichText{*field.draft(), {}} : *field.richValue();
        std::cout << "Value size " << value.text.size() << " [";
        for (char ch : utf8FromUtf16(value.text.substr(0, 40))) {
            if (ch == '\n') std::cout << "\\n";
            else std::cout << ch;
        }
        std::cout << ']';
        for (const auto& span : value.spans)
            std::cout << " span " << span.start << ' ' << span.length << ' ' << span.bold << span.italic << span.underline << ' ' << span.color;
        std::cout << std::endl;
    };
    auto events = ui.semantics().subscribe([&](const SemanticEvent& event) {
        if (event.ref.id == "clipboard" && (event.kind == SemanticEventKind::textChanged ||
            (event.kind == SemanticEventKind::propertyChanged && event.property == "styles"))) log();
        if (event.kind == SemanticEventKind::menuOpened) std::cout << "Menu opened" << std::endl;
    });
    DesktopConfig desktop{.title = u"Isoduo Clipboard", .width = 480, .height = 320};
    desktop.onDiagnostic = [](const RenderDiagnostic& event) {
        if (event.code == "clipboard.transfer_failed")
            std::cout << event.code << ": " << event.nativeMessage << std::endl;
    };
    auto created = Desktop::create(ui, webAppearance(), [&] { return layout::column({layout::component(field)}); }, scroll, desktop);
    if (!created.desktop) {
        if (created.failure) std::cerr << utf8FromUtf16(created.failure->message) << '\n';
        return 1;
    }
    const auto& caps = created.desktop->capabilities();
    auto* window = created.desktop->nativeWindow();
    std::cout << "Window " << window->windowId << " backend " << static_cast<int>(window->backend) << ' '
              << utf8FromUtf16(caps.version) << ' ' << utf8FromUtf16(caps.renderer) << std::endl;
    if (mode == "blocked-cut") {
        check(window->backend == DesktopBackendKind::wayland && bool(field.select(0, 10)), "Wayland selection before native input");
        check(field.cut().status == Status::failed && *field.richValue() == initial,
              "a Wayland cut without native input fails with its selection intact");
        std::cout << "PASS Wayland cut rejected before native input\n";
        return 0;
    }
    log();
    if (source && !large) {
        // A copy of the whole source value: the peer must read exactly these UTF-8 bytes (LF line breaks).
        std::cout << "Expected offer";
        for (unsigned char ch : utf8FromUtf16(test::copiedClipboardText(initial.text, test::ClipboardSeam::linuxDesktop)))
            std::cout << ' ' << "0123456789abcdef"[ch >> 4] << "0123456789abcdef"[ch & 15];
        std::cout << std::endl;
    }
    auto result = created.desktop->run();
    std::cout << "Final ";
    log();
    return result.status == Status::ok ? 0 : 1;
}
