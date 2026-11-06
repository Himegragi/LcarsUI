#include "core_internal.hpp"
// Native verification of the V01 form in a real Win32 window for both appearances.
// Keyboard and mouse cases are posted to the window queue and pumped through the host's own
// TranslateMessage/DispatchMessage path; layout evidence is rasterized offscreen.
// --ime additionally drives the real Microsoft Bopomofo IME with SendInput (foreground needed).
#include "desktop_host.hpp"
#include "test_component.hpp"
#include "edit_form.hpp"
#include "x04_drivers.hpp"
#include "list_page.hpp"
#include <imm.h>
#include <msctf.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

using namespace lcars;
using lcars::detail::DesktopHost;
using lcars::detail::Canvas;

namespace {
edit_form::Ids alternate_ids() {
    edit_form::Ids i;
    i.title = "t"; i.intro = "i"; i.normal = "f1"; i.read_only = "f2"; i.disabled = "f3"; i.error = "f4";
    i.long_text = "f5"; i.reset = "f6"; i.name_label = "n.l"; i.name = "n"; i.name_help = "n.h"; i.name_error = "n.e";
    i.volume_latest = "volume.latest"; i.volume = "volume"; i.notices = "notices"; i.mode = "mode"; i.offline_disabled = "offline";
    i.region = "region.alt"; i.region_empty = "region.empty.alt";
    i.apply = "go"; i.state = "s"; i.draft = "d"; i.result = "r"; i.count = "c"; i.latest = "latest";
    return i;
}
struct Report {
    std::ofstream out;
    int failures = 0, checks = 0;
    std::string appearance;
    explicit Report(const std::filesystem::path& file) : out(file) { out << "appearance\tcase\tstatus\tevidence\n"; }
    void check(const char* id, bool pass, const std::string& evidence) {
        ++checks;
        if (!pass) ++failures;
        out << appearance << '\t' << id << '\t' << (pass ? "PASS" : "FAIL") << '\t' << evidence << '\n';
        std::cout << (pass ? "PASS " : "FAIL ") << appearance << ' ' << id << ' ' << evidence << '\n';
    }
    void na(const char* id, const std::string& evidence) {
        out << appearance << '\t' << id << "\tN/A\t" << evidence << '\n';
        std::cout << "N/A " << appearance << ' ' << id << ' ' << evidence << '\n';
    }
};
std::string narrow(std::u16string_view text) {
    std::string out;
    for (char16_t c : text) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3f)); }
        else { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3f)); out += static_cast<char>(0x80 | (c & 0x3f)); }
    }
    return out;
}
std::string narrow(const std::wstring& text) { return narrow(std::u16string(text.begin(), text.end())); }
std::string str(Rect r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h);
}
LPARAM key_lparam(WPARAM vk, bool repeat, bool up) {
    LPARAM l = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) << 16);
    if (repeat || up) l |= 1LL << 30;
    if (up) l |= 1LL << 31;
    return l;
}
// One fresh form plus host per scenario, so cases never depend on each other's state.
struct Fixture {
    edit_form::Form form;
    DesktopHost host;
    const edit_form::Ids& ids;
    explicit Fixture(const Appearance& appearance, bool pages = false, bool alternate = false)
        : form(alternate ? alternate_ids() : edit_form::default_ids(), pages), host(form.ui, appearance, [this] { return form.page(); }, form.scroll, options(), true, true), ids(form.ids()) {
        ShowWindow(host.window(), SW_SHOWNOACTIVATE);
        host.force_scale(1.f);
        host.client_size(900, 700);
        settle();
    }
    static DesktopOptions options() { DesktopOptions o; o.title = u"LcarsUI host verification"; return o; }
    void settle() { host.pump(); host.paint(); }
    void post(UINT msg, WPARAM w, LPARAM l) { PostMessageW(host.window(), msg, w, l); }
    void key(WPARAM vk, bool repeat = false) { post(WM_KEYDOWN, vk, key_lparam(vk, repeat, false)); host.pump(); }
    void key_up(WPARAM vk) { post(WM_KEYUP, vk, key_lparam(vk, false, true)); host.pump(); }
    void tap(WPARAM vk) { key(vk); key_up(vk); }
    // Ctrl state for posted keys lives in the thread key state table, as real input would set it.
    void chord(WPARAM letter) {
        BYTE state[256]{};
        GetKeyboardState(state);
        state[VK_CONTROL] = 0x80;
        SetKeyboardState(state);
        tap(letter);
        state[VK_CONTROL] = 0;
        SetKeyboardState(state);
    }
    POINT center(std::string_view id) {
        host.paint();
        auto r = detail::find_placement(host.frame(), id)->bounds;
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
    void mouse(UINT msg, POINT p, WPARAM w = 0) { post(msg, w, MAKELPARAM(p.x, p.y)); host.pump(); }
    int history() {  // undo depth, restored afterwards
        int n = 0;
        while (detail::access(form.ui).undo(ids.name)) ++n;
        for (int i = 0; i < n; ++i) detail::access(form.ui).redo(ids.name);
        return n;
    }
};

void tabs_native(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.name.set_value(u"draft");
    h.tabs.focus(); f.settle();
    f.tap(VK_RIGHT);
    bool candidate = *h.tabs.active() == OptionValue{"v01"} && h.tabs.describe()->candidate == OptionValue{"v02"};
    f.tap(VK_RETURN); f.settle();
    bool activated = *h.tabs.active() == OptionValue{"v02"} && !f.host.frame().find(h.name) && f.host.frame().find(f.form.list->handles.search);
    f.form.list->handles.search.set_value(u"\u5171\u7528\u7bc4\u672c"); f.form.list->handles.list.select({"T006"});
    f.host.paint(); auto tab = f.host.frame().find(h.tabs);
    auto first = tab->options[0]; POINT point{first.x + first.w / 2, first.y + first.h / 2};
    f.mouse(WM_LBUTTONDOWN, point, MK_LBUTTON); f.mouse(WM_LBUTTONUP, point); f.settle();
    bool pointer = *h.tabs.active() == OptionValue{"v01"} && *h.name.draft() == u"draft" && f.form.list->handles.search.set_value(u"blocked").status == Status::hidden;
    h.tabs.activate({"v02"});
    bool retained = *f.form.list->handles.search.draft() == u"\u5171\u7528\u7bc4\u672c" && f.form.list->selection() == OptionValue{"T006"};
    f.form.list->handles.reset.invoke();
    f.host.render(960, 720, 1.f, folder / (report.appearance + "-v02.bmp"));
    report.check("C09-native", candidate && activated && pointer && retained,
        "Arrow only moves tab focus; Enter and pointer activate; hidden content is absent and both drafts survive");
}

void progress_native(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v03"});
    f.settle();
    bool passed = true;
    const struct { ButtonHandle button; int source; int displayed; } cases[] = {
        {h.progress_zero, 0, 0}, {h.progress_half, 50, 50}, {h.progress_full, 100, 100},
        {h.progress_below, -10, 0}, {h.progress_above, 110, 100}
    };
    for (const auto& c : cases) {
        c.button.focus(); f.tap(VK_RETURN); f.settle();
        Canvas pixels(1, 1, {0, 0, 0});
        auto path = folder / (report.appearance + (c.source == 50 ? "-v03-50.bmp" : "-v03-p07.bmp"));
        auto frame = f.host.render(960, 720, 1.f, path, &pixels);
        const auto* item = frame.find(h.progress);
        if (!item || item->progress_track.w < 4) { passed = false; continue; }
        auto track = item->progress_track;
        auto at = [&](int x) {
            auto i = (static_cast<std::size_t>(pixels.height - 1 - (track.y + track.h / 2)) * pixels.width + x) * 4;
            return Color{pixels.pixels[i], pixels.pixels[i + 1], pixels.pixels[i + 2]};
        };
        auto matches = [](Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
        auto palette = appearance.palette();
        auto quarter = at(track.x + track.w / 4), three_quarters = at(track.x + 3 * track.w / 4);
        auto expected_left = c.displayed >= 50 ? palette.accent : palette.border;
        auto expected_right = c.displayed == 100 ? palette.accent : palette.border;
        auto number = std::to_string(c.source);
        passed = passed && f.form.progress() == c.source && h.progress.describe()->numeric_value == c.displayed &&
            h.progress_source.describe()->value.find(std::u16string(number.begin(), number.end())) != std::u16string::npos &&
            matches(quarter, expected_left) && matches(three_quarters, expected_right);
    }
    int frames = f.host.frames();
    f.form.update_progress(50); f.host.pump();
    passed = passed && f.host.frames() > frames && h.progress.describe()->value == u"50%";
    report.check("P07-progress", passed, "Keyboard fixtures drive 0/50/100/-10/110; source and clamped semantics agree with track pixels; external update repaints");
}

void tooltip_native(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v03"});
    SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
    h.progress_half.focus(); f.settle();
    bool initially_hidden = h.progress_tip.visible() && !*h.progress_tip.visible();
    Sleep(550); f.host.pump(); f.host.paint();
    bool shown = *h.progress_tip.visible() && h.progress_half.describe()->focused &&
        std::any_of(f.host.frame().overlays.begin(), f.host.frame().overlays.end(), [](const auto& p) { return p.tooltip; });
    f.tap(VK_ESCAPE); Sleep(550); f.host.pump();
    bool escaped = !*h.progress_tip.visible() && h.progress_half.describe()->focused;
    SendMessageW(f.host.window(), WM_KILLFOCUS, 0, 0);
    SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
    bool refocus_waits = !*h.progress_tip.visible();
    Sleep(550); f.host.pump(); f.host.paint();
    bool returned = *h.progress_tip.visible() && h.progress_half.describe()->focused;
    bool geometry = true;
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
        int width = static_cast<int>(size.first * scale), height = static_cast<int>(size.second * scale);
        auto frame = f.host.render(width, height, scale, folder / (report.appearance + "-tooltip-" +
            std::to_string(size.first) + "-" + std::to_string(static_cast<int>(scale * 100)) + ".bmp"));
        auto tip = std::find_if(frame.overlays.begin(), frame.overlays.end(), [](const auto& p) { return p.tooltip; });
        geometry = geometry && tip != frame.overlays.end();
        if (tip != frame.overlays.end()) {
            geometry = geometry && tip->bounds.x >= 0 && tip->bounds.y >= 0 && tip->bounds.x + tip->bounds.w <= width &&
                tip->bounds.y + tip->bounds.h <= height && !tip->lines.empty();
            for (const auto& line : tip->lines) geometry = geometry && intersect(line.bounds, tip->bounds) == line.bounds;
        }
    }
    f.form.update_progress(0); f.tap(VK_SPACE);
    bool space = f.form.progress() == 50;
    report.check("P05-P06-R08-tooltip-messages", initially_hidden && shown && escaped && refocus_waits && returned && geometry && space,
        "initial=" + std::to_string(initially_hidden) + ", focus=" + std::to_string(shown) + ", escape=" + std::to_string(escaped) +
        ", refocus-delay=" + std::to_string(refocus_waits) + ", returned=" + std::to_string(returned) + ", geometry=" + std::to_string(geometry) +
        ", space=" + std::to_string(space));
}

void dialog_native(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v03"}); h.dialog_open.focus(); f.settle();
    f.tap(VK_RETURN); f.settle();
    auto frame = f.host.render(960, 720, 1.f, folder / (report.appearance + "-dialog.bmp"));
    bool initial = frame.modal && frame.find(h.dialog_cancel) && frame.find(h.dialog_confirm) && h.dialog_cancel.describe()->focused;
    bool geometry = true;
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) {
        for (float scale : {1.f, 1.5f}) {
            int width = static_cast<int>(size.first * scale), height = static_cast<int>(size.second * scale);
            auto sample = f.host.render(width, height, scale, folder / (report.appearance + "-dialog-" + std::to_string(size.first) + "-" + std::to_string(static_cast<int>(scale * 100)) + ".bmp"));
            for (auto button : {h.dialog_cancel, h.dialog_confirm}) {
                auto p = sample.find(button);
                if (!p) { geometry = false; continue; }
                auto b = p->bounds;
                geometry = geometry && intersect(b, p->clip) == b && b.x >= 0 && b.y >= 0 && b.x + b.w <= width && b.y + b.h <= height &&
                    sample.hit(b.x + b.w / 2, b.y + b.h / 2) == p;
            }
            geometry = geometry && !sample.hit(1, 1);
        }
    }
    POINT mask{1, 1};
    f.mouse(WM_LBUTTONDOWN, mask, MK_LBUTTON); f.mouse(WM_LBUTTONUP, mask); f.settle();
    bool shield = *h.dialog.is_open() && h.dialog_cancel.describe()->focused && f.form.progress() == 50;
    f.tap(VK_TAB);
    bool tab = h.dialog_confirm.describe()->focused;
    f.key(VK_RETURN); f.key(VK_RETURN, true); f.key_up(VK_RETURN); f.settle();
    bool once = f.form.progress() == 0 && f.form.dialog_confirmations() == 1 && h.dialog_open.describe()->focused && !*h.dialog.is_open();
    report.check("P02-P03-dialog", initial && shield && tab && once,
        "Dialog overlays the window, mask blocks pointer, Tab reaches confirm, held Enter settles once and restores focus");
    report.check("P03-dialog-layout", geometry, "Dialog buttons are visible and hit at 960x720 and 640x480 DIP, at forced 100% and 150%");
    h.reset_v03.invoke(); h.dialog_open.invoke(); f.tap(VK_ESCAPE); f.settle();
    report.check("P01-dialog-escape", f.form.progress() == 50 && f.form.dialog_confirmations() == 0 && h.dialog_open.describe()->focused && !*h.dialog.is_open(),
        "Escape cancels and returns focus without changing progress");
}

void registered_component_uses_native_host(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    constexpr auto test_kind = static_cast<Kind>(100);
    TestComponent component;
    detail::ScopedComponentType registration(test_kind, component);
    Ui ui({{.id = "probe", .kind = test_kind, .value = u"Test component"}});
    auto& state = detail::access(ui);
    int activations = 0;
    state.bind("probe", [&] { ++activations; return Result{}; });
    ScrollState scroll;
    DesktopOptions options;
    DesktopHost host(ui, appearance, [&] { return layout::control(state.layout_handle("probe")); }, scroll, options, false, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.force_scale(1.f);
    host.client_size(200, 100);
    host.pump();
    host.paint();
    Canvas pixels(1, 1, {0, 0, 0});
    host.render(200, 100, 1.f, folder / (report.appearance + "-registered-component.bmp"), &pixels);
    const std::size_t sample = (static_cast<std::size_t>(100 - 1 - 20) * 200 + 70) * 4;
    bool painted = pixels.pixels[sample] == 17 && pixels.pixels[sample + 1] == 91 && pixels.pixels[sample + 2] == 173;
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(host.window(), message, w, l); host.pump(); };
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(60, 4));
    post(WM_LBUTTONUP, 0, MAKELPARAM(60, 4));
    bool outside = activations == 0;
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4));
    post(WM_LBUTTONUP, 0, MAKELPARAM(4, 4));
    bool pointer = activations == 1;
    post(WM_KEYDOWN, VK_RETURN, key_lparam(VK_RETURN, false, false));
    bool keyboard = activations == 2;
    report.check("registered-component", painted && outside && pointer && keyboard,
                 "custom color and hit region render; inside click and Enter each activate once");
}

void list_native(const Appearance& appearance, Report& report) {
    list_page::Page page;
    DesktopOptions options;
    DesktopHost host(page.ui, appearance, [&] { return page.page(); }, page.scroll, options, false, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE); host.force_scale(1.f); host.client_size(640, 480);
    page.handles.hundred.invoke(); page.handles.search.focus(); host.pump(); host.paint();
    auto post = [&](UINT msg, WPARAM w, LPARAM l) { PostMessageW(host.window(), msg, w, l); host.pump(); };
    page.handles.list.focus(); host.pump();
    post(WM_KEYDOWN, VK_END, key_lparam(VK_END, false, false));
    post(WM_KEYUP, VK_END, key_lparam(VK_END, false, true));
    host.paint();
    auto item = host.frame().find(page.handles.list);
    auto visible = [&](Rect r) { auto v = host.frame().find(page.handles.list)->clip; return r.y >= v.y && r.y + r.h <= v.y + v.h; };
    bool end = item && item->options.size() == 100 && visible(item->options.back()) && page.selection() == OptionValue{"T100"};
    report.check("R02-native-end", end, "End selects T100 and reveals its entire row at 640x480");

    page.handles.hundred.invoke(); page.handles.search.focus(); host.pump(); host.paint();
    auto frame = host.frame();
    auto region = frame.regions.back();
    POINT wheelpoint{region.clip.x + 10, region.clip.y + 10}; ClientToScreen(host.window(), &wheelpoint);
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(wheelpoint.x, wheelpoint.y)); host.paint();
    bool wheel = page.list_scroll.offset > 0;
    frame = host.frame(); region = frame.regions.back();
    POINT start{region.thumb.x + region.thumb.w / 2, region.thumb.y + region.thumb.h / 2};
    POINT finish{start.x, region.track.y + region.track.h - region.thumb.h / 2};
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(finish.x, finish.y));
    post(WM_LBUTTONUP, 0, MAKELPARAM(finish.x, finish.y)); host.paint();
    item = host.frame().find(page.handles.list);
    bool dragged = page.list_scroll.offset == host.frame().regions.back().maximum && item && visible(item->options.back());
    bool clicked = false;
    if (dragged) {
        auto r = item->options.back(); POINT point{r.x + 8, r.y + r.h / 2};
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        post(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        clicked = page.selection() == OptionValue{"T100"};
    }
    page.handles.one.invoke(); host.pump(); host.paint();
    bool clamped = page.list_scroll.offset <= host.frame().regions.back().maximum;
    report.check("L06-N07-native-scroll", wheel && dragged && clicked && clamped,
        "wheel=" + std::to_string(wheel) + ", thumb drag=" + std::to_string(dragged) + ", click T100=" + std::to_string(clicked) + ", shrink clamped=" + std::to_string(clamped));

    page.handles.twenty.invoke(); page.handles.disable_eight.invoke(); page.handles.list.focus();
    host.pump();
    post(WM_KEYDOWN, VK_HOME, key_lparam(VK_HOME, false, false));
    for (int i = 0; i < 6; ++i) post(WM_KEYDOWN, VK_DOWN, key_lparam(VK_DOWN, false, false));
    host.paint();
    region = host.frame().regions.back();
    wheelpoint = {region.clip.x + 10, region.clip.y + 10}; ClientToScreen(host.window(), &wheelpoint);
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(wheelpoint.x, wheelpoint.y)); host.paint();
    item = host.frame().find(page.handles.list);
    bool disabled_click = false;
    if (item && item->options.size() > 7 && visible(item->options[7])) {
        auto r = item->options[7]; POINT point{r.x + 8, r.y + r.h / 2};
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        post(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        disabled_click = page.selection() == OptionValue{"T007"};
    }
    post(WM_KEYDOWN, VK_DOWN, key_lparam(VK_DOWN, false, false));
    report.check("R09-native-disabled-row", disabled_click && page.selection() == OptionValue{"T009"},
        "disabled T008 click ignored=" + std::to_string(disabled_click) + ", Down from T007 skips to T009=" + std::to_string(page.selection() == OptionValue{"T009"}));
}

void nested_list_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v02"});
    auto& page = *f.form.list;
    page.handles.hundred.invoke();
    f.host.client_size(640, 300); f.settle();
    page.handles.list.focus(); f.settle();
    f.tap(VK_END); f.settle();
    auto item = f.host.frame().find(page.handles.list);
    auto row = item->options.back(); auto clip = item->clip;
    bool revealed = page.selection() == OptionValue{"T100"} && page.list_scroll.offset > 0 &&
        f.form.scroll.offset > 0 && row.y >= clip.y && row.y + row.h <= clip.y + clip.h;
    int inner = page.list_scroll.offset;
    f.form.scroll.offset = f.host.frame().maximum / 2; f.settle();
    const auto& region = f.host.frame().regions.back();
    POINT point{region.clip.x + 5, region.clip.y + region.clip.h / 2};
    ClientToScreen(f.host.window(), &point);
    int outer = f.form.scroll.offset;
    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(point.x, point.y));
    f.settle();
    bool bubbles = page.list_scroll.offset == inner && f.form.scroll.offset > outer;
    report.check("B03-native-nested", revealed && bubbles,
        "End reveals T100 through both viewports=" + std::to_string(revealed) +
        ", wheel at inner limit moves outer=" + std::to_string(bubbles));
}

void space_activation_does_not_leak(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    detail::access(ui).focus(f.ids.error);
    f.host.trace().clear();
    f.key(VK_SPACE);
    f.key(VK_SPACE, true);
    f.key(VK_SPACE, true);
    f.key_up(VK_SPACE);
    bool moved = detail::access(ui).focused() == f.ids.name && detail::access(ui).text(f.ids.name).empty();
    int chars = 0;
    for (const auto& line : f.host.trace()) if (line.starts_with(L"WM_CHAR\t32\t")) ++chars;
    std::string trace;
    for (const auto& line : f.host.trace()) trace += narrow(line) + " | ";
    int history_after_activation = f.history();
    f.tap(VK_SPACE);
    bool typed = detail::access(ui).text(f.ids.name) == u" " && f.history() == history_after_activation + 1;
    report.check("AC07-R01", moved && chars == 3 && history_after_activation == 1 && typed,
                 "translated WM_CHAR spaces=" + std::to_string(chars) + " delivered while the button held focus; name length after activation=" +
                 std::to_string(detail::access(ui).text(f.ids.name).size() - 1) + "; history entries after activation=" + std::to_string(history_after_activation) +
                 " (the error fixture's blanking only); typing space afterwards=" + (typed ? "inserted once" : "WRONG") + "; trace: " + trace);
}

void enter_activates_once(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    detail::access(f.form.ui).focus(f.ids.apply);
    f.key(VK_RETURN);
    f.key(VK_RETURN, true);
    f.key_up(VK_RETURN);
    detail::access(f.form.ui).focus(f.ids.name);
    f.tap(VK_RETURN);
    report.check("AC06-enter", f.form.applies() == 1 && detail::access(f.form.ui).text(f.ids.name) == u"測試使用者",
                 "Enter press + auto-repeat on apply -> applies=" + std::to_string(f.form.applies()) + "; Enter in the name field adds nothing");
}

void pointer_activation_rules(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.handles.apply.focus(); f.settle();
    auto apply = f.center(f.ids.apply);
    POINT outside{2, 2};
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, outside, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, outside);
    int moved_out = f.form.applies();
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, apply);
    int clicked = f.form.applies();
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    SendMessageW(f.host.window(), WM_CANCELMODE, 0, 0);
    f.mouse(WM_LBUTTONUP, apply);
    int canceled = f.form.applies();
    detail::access(f.form.ui).focus(f.ids.apply);
    f.key(VK_SPACE);
    SendMessageW(f.host.window(), WM_KILLFOCUS, 0, 0);
    f.key_up(VK_SPACE);
    int deactivated = f.form.applies();
    detail::access(f.form.ui).invoke(f.ids.disabled);
    f.settle();
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, apply);
    report.check("AC06-F10-N02", moved_out == 0 && clicked == 1 && canceled == 1 && deactivated == 1 && f.form.applies() == 1,
                 "press+move out+release=" + std::to_string(moved_out) + ", click=" + std::to_string(clicked) +
                 ", press+WM_CANCELMODE+release=" + std::to_string(canceled) + ", Space+WM_KILLFOCUS+release=" + std::to_string(deactivated) +
                 ", disabled click total=" + std::to_string(f.form.applies()));
    // AC10: with the data area disabled, the native pointer and keyboard cannot reach the name either.
    auto before = detail::access(f.form.ui).text(f.ids.name);
    f.mouse(WM_LBUTTONDOWN, f.center(f.ids.name), MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, f.center(f.ids.name));
    f.tap('X');
    report.check("AC10-native-disabled", detail::access(f.form.ui).focused() != f.ids.name && detail::access(f.form.ui).text(f.ids.name) == before,
                 "click on the disabled name then typing: focus=" + detail::access(f.form.ui).focused() + ", text unchanged=" +
                     (detail::access(f.form.ui).text(f.ids.name) == before ? "yes" : "NO"));
}

void pointer_selection_and_clipboard(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    detail::access(ui).set_value(f.ids.name, u"Alpha 中文");
    f.host.paint();
    const Placement field = *detail::find_placement(f.host.frame(), f.ids.name);  // a copy: repaints replace the frame
    auto x_of = [&](unsigned offset) {
        return field.bounds.x + field.field.editor.x + static_cast<int>(field.run.x(offset) - field.offset);
    };
    int y = field.bounds.y + field.field.editor.y + field.field.editor.h / 2;
    f.mouse(WM_LBUTTONDOWN, {x_of(5), y}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {x_of(5), y});
    auto placed = detail::access(ui).edit(f.ids.name)->selection;
    f.mouse(WM_LBUTTONDOWN, {x_of(8), y}, MK_LBUTTON | MK_SHIFT);
    f.mouse(WM_LBUTTONUP, {x_of(8), y});
    auto extended = detail::access(ui).edit(f.ids.name)->selection;
    f.mouse(WM_LBUTTONDOWN, {x_of(0), y}, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, {x_of(5), y}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {x_of(5), y});
    auto dragged = detail::access(ui).edit(f.ids.name)->selection;
    detail::WindowsClipboard clipboard(f.host.window());
    auto saved = clipboard.read();
    if (saved.status != Status::ok && saved.status != Status::not_supported) {
        report.check("AC08-pointer-clipboard", false, "Could not save clipboard text; test did not overwrite it");
        return;
    }
    f.chord('C');
    auto copied = clipboard.read();
    bool supplied = clipboard.write(u"X\r\nY");
    f.chord('A');
    f.chord('V');
    auto pasted = detail::access(ui).text(f.ids.name);
    f.chord('Z');
    auto undone = detail::access(ui).text(f.ids.name);
    bool restored = saved.status == Status::not_supported || (saved && clipboard.write(*saved));
    report.check("AC08-pointer-clipboard",
                 placed.anchor == 5 && placed.caret == 5 && extended.anchor == 5 && extended.caret == 8 && dragged.anchor == 0 &&
                     dragged.caret == 5 && (copied && *copied == u"Alpha") && supplied && pasted == u"X Y" && undone == u"Alpha \u4e2d\u6587" && restored,
                 "click caret=" + std::to_string(placed.caret) + ", shift-click=" + std::to_string(extended.anchor) + ".." +
                     std::to_string(extended.caret) + ", drag=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) +
                     ", Ctrl+C=" + ((copied && *copied == u"Alpha") ? "ok" : copied ? "unexpected" : "unavailable") +
                     ", fixture write=" + (supplied ? "ok" : "failed") + ", Ctrl+V=" + (pasted == u"X Y" ? "ok" : "unexpected") +
                     ", Ctrl+Z=" + (undone == u"Alpha \u4e2d\u6587" ? "ok" : "unexpected") +
                     "; user clipboard text restored=" + (saved.status == Status::not_supported ? "no text saved" : restored ? "yes" : "FAILED"));
}

// Reproduce a clipboard manager briefly holding the OS clipboard during a real key chord.
void clipboard_contention(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = detail::access(f.form.ui);
    detail::WindowsClipboard clipboard(f.host.window());
    auto saved = clipboard.read();
    if (saved.status != Status::ok && saved.status != Status::not_supported) {
        report.check("AC08-clipboard-contention", false, "Could not save clipboard text; test did not overwrite it");
        return;
    }
    ui.set_value(f.ids.name, u"Alpha");
    ui.focus(f.ids.name);
    ui.select(f.ids.name, 0, 5);

    auto held_during = [&](const std::function<void()>& action) {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready) return false;
        std::atomic<bool> acquired = false;
        std::thread holder([&] {
            for (int i = 0; i < 200; ++i) {
                if (OpenClipboard(nullptr)) { acquired = true; break; }
                Sleep(5);
            }
            SetEvent(ready);
            if (acquired) { Sleep(60); CloseClipboard(); }
        });
        bool signaled = WaitForSingleObject(ready, 3000) == WAIT_OBJECT_0;
        if (signaled && acquired) action();
        holder.join();
        CloseHandle(ready);
        return signaled && acquired;
    };
    bool held_copy = held_during([&] { f.chord('C'); });
    auto copied = clipboard.read();
    bool supplied = clipboard.write(u"X\r\nY");
    f.chord('A');
    bool held_paste = held_during([&] { f.chord('V'); });
    auto pasted = ui.text(f.ids.name);
    bool restored = saved.status == Status::not_supported || (saved && clipboard.write(*saved));
    report.check("AC08-clipboard-contention", held_copy && (copied && *copied == u"Alpha") && supplied && held_paste && pasted == u"X Y" && restored,
                 std::string("60ms clipboard hold: copy=") + ((copied && *copied == u"Alpha") ? "ok" : "failed") + ", paste=" + (pasted == u"X Y" ? "ok" : "failed") +
                     ", fixture write=" + (supplied ? "ok" : "failed") + ", clipboard text restore=" + (saved.status == Status::not_supported ? "no text saved" : restored ? "ok" : "failed"));
}

void checkbox_and_radio_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.handles.notices.focus(); f.host.pump();
    f.tap(VK_SPACE);
    bool toggled = !*f.form.handles.notices.draft();
    f.host.paint();
    auto box = f.host.frame().find(f.form.handles.notices);
    bool clicked_box = false;
    if (box) {
        auto r = box->bounds; POINT p{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
        clicked_box = *f.form.handles.notices.draft();
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
        clicked_box = clicked_box && !*f.form.handles.notices.draft();
    }
    f.form.handles.mode.focus(); f.host.pump();
    f.tap(VK_RIGHT);
    bool arrow = *f.form.handles.mode.draft() == OptionValue{"focus"};
    f.form.handles.offline_disabled.invoke();
    f.host.paint();
    auto item = f.host.frame().find(f.form.handles.mode);
    bool had_option = item && item->options.size() == 3;
    if (had_option) {
        auto r = item->options[2]; POINT p{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
    }
    bool disabled = *f.form.handles.mode.draft() == OptionValue{"focus"};
    f.form.handles.mode.enable_option({"offline"}, true);
    f.host.paint(); item = f.host.frame().find(f.form.handles.mode);
    if (item && item->options.size() == 3) {
        auto r = item->options[2]; POINT p{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
    }
    bool clicked = *f.form.handles.mode.draft() == OptionValue{"offline"};
    bool applied = bool(f.form.handles.apply.invoke()) && !f.form.notices_applied() && f.form.mode_applied() == OptionValue{"offline"};
    report.check("F09-checkbox-radio", toggled && clicked_box && arrow && had_option && disabled && clicked && applied,
        "Space toggled=" + std::to_string(toggled) + ", checkbox clicks=" + std::to_string(clicked_box) + ", arrow=" + std::to_string(arrow) + ", disabled click ignored=" + std::to_string(disabled) +
        ", enabled click=" + std::to_string(clicked) + ", apply=" + std::to_string(applied));
}

void slider_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.handles.volume.focus(); f.settle();
    f.tap(VK_RIGHT);
    bool step = *f.form.handles.volume.draft() == 45 && f.form.volume_applied() == 40;
    f.tap(VK_HOME); bool home = *f.form.handles.volume.draft() == 0;
    f.tap(VK_END); bool end = *f.form.handles.volume.draft() == 100;
    f.host.paint();
    const auto* item = f.host.frame().find(f.form.handles.volume);
    bool dragged = false;
    if (item) {
        auto r = item->slider_track;
        POINT start{r.x, r.y + r.h / 2}, middle{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON);
        bool at_start = *f.form.handles.volume.draft() == 0;
        f.mouse(WM_MOUSEMOVE, middle, MK_LBUTTON);
        f.mouse(WM_LBUTTONUP, middle);
        dragged = at_start && *f.form.handles.volume.draft() == 50;
    }
    bool applied = f.form.volume_applied() == 40 && bool(f.form.handles.apply.invoke()) && f.form.volume_applied() == 50;
    f.form.handles.disabled.invoke();
    bool disabled = f.form.handles.volume.set_value(75).status == Status::disabled && *f.form.handles.volume.draft() == 50;
    report.check("F09-slider", step && home && end && dragged && applied && disabled,
        "arrow=" + std::to_string(step) + ", Home=" + std::to_string(home) + ", End=" + std::to_string(end) +
        ", native drag=" + std::to_string(dragged) + ", apply=" + std::to_string(applied) + ", disabled=" + std::to_string(disabled));
}

void dropdown_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.host.client_size(900, 1000); f.settle();
    auto& region = f.form.handles.region;
    region.focus(); f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN);
    bool candidate = region.describe()->expanded && region.describe()->candidate == OptionValue{"south"} && *region.draft() == OptionValue{"north"};
    f.tap(VK_ESCAPE);
    bool escape = !region.describe()->expanded && region.describe()->focused && *region.draft() == OptionValue{"north"};
    f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN); f.tap(VK_TAB);
    bool tab = !region.describe()->expanded && f.form.handles.volume.describe()->focused && *region.draft() == OptionValue{"north"};
    region.focus(); f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN);
    auto old_intro = f.form.handles.intro.describe()->value;
    auto target = f.center(f.ids.long_text);
    f.mouse(WM_LBUTTONDOWN, target, MK_LBUTTON); f.mouse(WM_LBUTTONUP, target);
    bool outside = !region.describe()->expanded && *region.draft() == OptionValue{"north"} &&
        f.form.handles.long_text.describe()->focused && f.form.handles.intro.describe()->value != old_intro;
    region.focus(); f.tap(VK_RETURN); f.host.paint();
    bool pointer = false;
    if (!f.host.frame().overlays.empty() && f.host.frame().overlays[0].options.size() == 3) {
        auto rect = f.host.frame().overlays[0].options[2];
        POINT pt{rect.x + rect.w / 2, rect.y + rect.h / 2};
        f.mouse(WM_LBUTTONDOWN, pt, MK_LBUTTON); f.mouse(WM_LBUTTONUP, pt);
        pointer = *region.draft() == OptionValue{"south"} && !region.describe()->expanded && f.form.region_applied() == OptionValue{"north"};
    }
    report.check("F07-dropdown-native", candidate && escape && tab && outside && pointer,
        "candidate=" + std::to_string(candidate) + ", Escape=" + std::to_string(escape) + ", Tab=" + std::to_string(tab) +
        ", outside action=" + std::to_string(outside) + ", popup click=" + std::to_string(pointer));
}

void tab_walk_and_visible_focus(const Appearance& appearance, Report& report, const std::filesystem::path& folder, const std::string& tag) {
    Fixture f(appearance);
    std::vector<Id> seen;
    for (int i = 0; i < 15; ++i) { f.tap(VK_TAB); seen.push_back(detail::access(f.form.ui).focused()); }
    BYTE state[256]{};
    GetKeyboardState(state);
    state[VK_SHIFT] = 0x80;
    SetKeyboardState(state);
    f.tap(VK_TAB);
    state[VK_SHIFT] = 0;
    SetKeyboardState(state);
    bool back = detail::access(f.form.ui).focused() == f.ids.apply;
    Canvas pixels(1, 1, {0, 0, 0});
    auto frame = f.host.render(900, 700, 1.f, folder / (tag + "-focus-apply.bmp"), &pixels);
    auto r = detail::find_placement(frame, f.ids.apply)->bounds;
    auto color_at = [&](int x, int y) {
        auto i = (static_cast<std::size_t>(pixels.height - 1 - y) * pixels.width + x) * 4;
        return Color{pixels.pixels[i], pixels.pixels[i + 1], pixels.pixels[i + 2]};
    };
    auto accent = appearance.palette().accent, ring = color_at(r.x + 1, r.y + r.h / 2);
    bool visible = ring.r == accent.r && ring.g == accent.g && ring.b == accent.b;
    const Placement* other = nullptr;
    for (const auto& item : frame.items) {
        auto visible = intersect(item.bounds, item.clip);
        if (item.kind == Kind::button && item.id != f.ids.apply && visible.w > 2 && visible.h == item.bounds.h &&
            item.bounds.x >= 0 && item.bounds.y >= 0 && item.bounds.y + item.bounds.h <= pixels.height) { other = &item; break; }
    }
    bool distinct = false;
    if (other) {
        auto plain = color_at(other->bounds.x + 1, other->bounds.y + other->bounds.h / 2);
        distinct = !(plain.r == accent.r && plain.g == accent.g && plain.b == accent.b);
    }
    std::string order;
    for (const auto& id : seen) order += id + " ";
    bool wraps = seen.size() == 15 && seen[6] == f.ids.name && seen[7] == f.ids.notices && seen[8] == f.ids.mode && seen[9] == f.ids.region && seen[10] == f.ids.volume && seen[11] == f.ids.offline_disabled && seen[12] == f.ids.region_empty && seen[13] == f.ids.apply && seen[14] == seen[0];
    report.check("AC11-tab", wraps && back && visible && distinct,
                 "Tab order: " + order + "; Shift+Tab back to apply=" + (back ? "yes" : "no") + "; focused apply ring pixel is accent=" +
                     (visible ? "yes" : "NO") + ", unfocused reset ring is not=" + (distinct ? "yes" : "NO") + " (" + tag + "-focus-apply.bmp)");
}

void resize_first_frame_matches_next(const Appearance& appearance, Report& report, float scale) {
    Fixture f(appearance);
    f.host.force_scale(scale);
    detail::access(f.form.ui).invoke(f.ids.long_text);
    f.host.client_size(640, 480);
    f.host.pump();
    UpdateWindow(f.host.window());
    POINT wheelpoint{20, 20}; ClientToScreen(f.host.window(), &wheelpoint);
    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120 * 40)), MAKELPARAM(wheelpoint.x, wheelpoint.y));
    f.host.pump();
    UpdateWindow(f.host.window());
    int deep = f.host.frame().offset, deep_max = f.host.frame().maximum;
    int before = f.host.frames();
    f.host.client_size(720, 540);
    f.host.pump();
    UpdateWindow(f.host.window());
    Frame first = f.host.frame();
    int painted = f.host.frames() - before;
    InvalidateRect(f.host.window(), nullptr, FALSE);
    UpdateWindow(f.host.window());
    const Frame& second = f.host.frame();
    bool same = first.items.size() == second.items.size() && first.offset == second.offset;
    for (std::size_t i = 0; same && i < first.items.size(); ++i) same = first.items[i].bounds == second.items[i].bounds;
    auto target = detail::find_placement(first, f.ids.apply)->bounds;
    const auto* hit = second.hit(target.x + target.w / 2, target.y + target.h / 2);
    bool visible = target.y >= 0 && target.y + target.h <= static_cast<int>(540 * scale);
    report.check(scale > 1 ? "R06-native-150" : "R06-native-100", deep > 0 && deep == deep_max && painted == 1 && first.viewport.w == static_cast<int>(720 * scale) && first.maximum > 0 && first.offset == first.maximum && first.offset < deep && same && visible && hit && hit->id == f.ids.apply,
                 "640x480 wheel to offset " + std::to_string(deep) + "/" + std::to_string(deep_max) + "; SetWindowPos 720x540 -> " +
                     std::to_string(painted) + " WM_PAINT, first frame offset " + std::to_string(first.offset) + " max " +
                     std::to_string(first.maximum) + ", identical to the next frame=" + (same ? "yes" : "NO") + ", apply at " + str(target) +
                     " hit=" + (hit ? hit->id : std::string("none")));
}

bool overlaps(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

void layout_matrix(const Appearance& appearance, Report& report, const std::filesystem::path& folder, const std::string& tag) {
    struct Size { int w, h; };
    for (auto size : {Size{960, 720}, Size{640, 480}}) {
        for (float scale : {1.f, 1.5f}) {
            for (int variant = 0; variant < 2; ++variant) {
                Fixture f(appearance);
                auto& ui = f.form.ui;
                if (variant == 1) {
                    detail::access(ui).invoke(f.ids.long_text);
                    detail::access(ui).invoke(f.ids.error);
                }
                int w = static_cast<int>(size.w * scale), h = static_cast<int>(size.h * scale);
                std::string name = tag + "-" + std::to_string(size.w) + "x" + std::to_string(size.h) + "-" +
                                   std::to_string(static_cast<int>(scale * 100)) + (variant ? "-long-error" : "-initial");
                auto frame = f.host.render(w, h, scale, folder / (name + ".bmp"));
                bool clean = true;
                for (std::size_t i = 0; i < frame.items.size(); ++i)
                    for (std::size_t j = i + 1; j < frame.items.size(); ++j)
                        if (overlaps(frame.items[i].bounds, frame.items[j].bounds)) clean = false;
                bool inside = std::all_of(frame.items.begin(), frame.items.end(),
                                          [&](const Placement& p) { return p.bounds.x >= 0 && p.bounds.x + p.bounds.w <= w; });
                auto field = detail::find_placement(frame, f.ids.name)->bounds, apply = detail::find_placement(frame, f.ids.apply)->bounds;
                auto draft = detail::find_placement(frame, f.ids.draft)->bounds;
                bool arrangement = size.w == 960 ? draft.x > field.x && draft.y < apply.y + apply.h : draft.x == field.x && draft.y > apply.y;
                // Every keyboard entry can be scrolled fully into the viewport.
                int reachable = 0, entries = 0;
                for (const auto& id : detail::access(ui).focus_order()) {
                    ScrollState probe;
                    auto revealed = f.host.layout(w, h, scale, probe, id);
                    auto b = detail::find_placement(revealed, id)->bounds;
                    ++entries;
                    if (b.y >= 0 && b.y + b.h <= h && revealed.hit(b.x + b.w / 2, b.y + b.h / 2)->id == id) ++reachable;
                }
                report.check("AC13-layout", clean && inside && arrangement && reachable == entries,
                             name + ": " + std::to_string(frame.items.size()) + " placements, no overlap=" + (clean ? "yes" : "NO") +
                                 ", inside width=" + (inside ? "yes" : "NO") + ", results " + (size.w == 960 ? "beside" : "below") +
                                 " form=" + (arrangement ? "yes" : "NO") + ", entries revealed and hit=" + std::to_string(reachable) + "/" +
                                 std::to_string(entries) + ", scroll max=" + std::to_string(frame.maximum) + ", name " + str(field) +
                                 ", draft " + str(draft));
            }
        }
    }
}
void page_size_scale_matrix(const Appearance& appearance, Report& report) {
    struct Size { int w, h; };
    for (auto page : {"v01", "v02", "v03"}) for (auto size : {Size{960, 720}, Size{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true);
            auto& h = f.form.handles;
            h.tabs.activate({page});
            int w = static_cast<int>(size.w * scale), ht = static_cast<int>(size.h * scale);
            ScrollState initial;
            auto frame = f.host.layout(w, ht, scale, initial, {});
            bool within = true, clean = true;
            for (std::size_t i = 0; i < frame.items.size(); ++i) {
                auto a = intersect(frame.items[i].bounds, frame.items[i].clip);
                if (a.w && (a.x < 0 || a.x + a.w > w)) within = false;
                for (std::size_t j = i + 1; j < frame.items.size(); ++j) {
                    auto b = intersect(frame.items[j].bounds, frame.items[j].clip);
                    if (a.w && a.h && b.w && b.h && overlaps(a, b)) clean = false;
                }
            }
            int reached = 0, entries = 0; std::string missing;
            for (const auto& id : detail::access(f.form.ui).focus_order()) {
                ScrollState probe;
                auto revealed = f.host.layout(w, ht, scale, probe, id);
                auto* item = detail::find_placement(revealed, id);
                ++entries;
                if (!item) continue;
                auto area = intersect(item->bounds, item->clip);
                auto* hit = area.w && area.h ? revealed.hit(area.x + area.w / 2, area.y + area.h / 2) : nullptr;
                if ((area == item->bounds || item->kind == Kind::list) && hit && hit->id == id) ++reached;
                else missing += id + " ";
            }
            bool expected = std::string_view(page) == "v01" ? frame.find(h.name) != nullptr :
                            std::string_view(page) == "v02" ? frame.find(f.form.list->handles.list) != nullptr : frame.find(h.progress) != nullptr;
            std::string detail = std::string(page) + " " + std::to_string(size.w) + "x" + std::to_string(size.h) +
                " " + std::to_string(static_cast<int>(scale * 100)) + "%: visible overlap=" + std::to_string(!clean) +
                ", width=" + std::to_string(within) + ", entries revealed/hit=" + std::to_string(reached) + "/" + std::to_string(entries) + "; missing=" + missing;
            report.check("X02-page-matrix", expected && within && clean && reached == entries, detail);
        }
}

// Each sample uses the actual window paint/measurement and posted Win32 input path.
// Popup-on-scroll policy for the dropdown and tooltip is deliberately not inferred here.
void popup_dismissal_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            auto& h = f.form.handles;
            f.host.force_scale(scale); f.host.client_size(size.first, size.second); f.settle();
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(static_cast<int>(scale * 100)) + "%";
            auto anchor_visibility = [&](const auto& handle) {
                const auto* item = f.host.frame().find(handle);
                if (!item) return std::string("absent");
                auto part = intersect(item->bounds, item->clip);
                if (part.w <= 0 || part.h <= 0) return std::string("clipped");
                return part == item->bounds ? std::string("visible") : std::string("partial");
            };
            auto& region = h.region;
            auto prepare_dropdown = [&] {
                region.focus(); f.settle(); f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN); f.settle();
                return region.describe()->expanded && region.describe()->candidate == OptionValue{"south"} &&
                    *region.draft() == OptionValue{"north"};
            };
            bool prepared = prepare_dropdown();
            f.tap(VK_ESCAPE); f.settle();
            bool escaped = !region.describe()->expanded && region.describe()->focused && *region.draft() == OptionValue{"north"} &&
                f.host.frame().overlays.empty();
            prepared = prepare_dropdown() && prepared;
            f.tap(VK_TAB); f.settle();
            bool tab = !region.describe()->expanded && h.volume.describe()->focused && *region.draft() == OptionValue{"north"} &&
                f.host.frame().overlays.empty();
            prepared = prepare_dropdown() && prepared;
            auto* tabs = f.host.frame().find(h.tabs);
            bool outside = false;
            if (tabs && !tabs->options.empty()) {
                auto rect = tabs->options.front(); POINT p{rect.x + rect.w / 2, rect.y + rect.h / 2};
                auto* hit = f.host.frame().hit(p.x, p.y);
                bool targets_tabs = hit && hit->id == h.tabs.describe()->id && !hit->popup;
                f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p); f.settle();
                hit = f.host.frame().hit(p.x, p.y);
                outside = targets_tabs && hit && hit->id == h.tabs.describe()->id && !hit->popup &&
                    !region.describe()->expanded && h.tabs.describe()->focused && *region.draft() == OptionValue{"north"} &&
                    *h.tabs.active() == OptionValue{"v01"};
            }
            report.check("X02-v01-dismiss", prepared && escaped && tab && outside,
                sample + ": candidate=" + std::to_string(prepared) + ", Esc=" + std::to_string(escaped) +
                ", Tab=" + std::to_string(tab) + ", outside=" + std::to_string(outside));

            prepared = prepare_dropdown();
            auto scroll_region = f.host.frame().regions.front();
            int before_scroll = f.form.scroll.offset;
            POINT wheel_point{scroll_region.clip.x + 2, scroll_region.clip.y + 2};
            ClientToScreen(f.host.window(), &wheel_point);
            // Choose a direction with room to move, away from the dropdown panel.
            int wheel_delta = before_scroll < scroll_region.maximum ? -120 : 120;
            f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(wheel_delta)), MAKELPARAM(wheel_point.x, wheel_point.y));
            f.settle();
            bool wheel_cancel = f.form.scroll.offset != before_scroll && !region.describe()->expanded &&
                !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
            report.check("X02-v01-wheel-cancel", prepared && wheel_cancel,
                sample + ": moved and canceled=" + std::to_string(wheel_cancel) + ", anchor=" + anchor_visibility(region));

            prepared = prepare_dropdown();
            auto popup = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(),
                                      [](const Placement& p) { return p.popup && !p.tooltip; });
            bool popup_wheel = false;
            if (popup != f.host.frame().overlays.end()) {
                POINT p{popup->bounds.x + popup->bounds.w / 2, popup->bounds.y + popup->bounds.h / 2};
                ClientToScreen(f.host.window(), &p);
                int old = f.form.scroll.offset;
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(p.x, p.y)); f.settle();
                popup_wheel = f.form.scroll.offset == old && region.describe()->expanded &&
                    region.describe()->candidate == OptionValue{"south"};
            }
            report.check("X02-v01-popup-wheel", prepared && popup_wheel,
                sample + ": wheel over panel does not scroll background or cancel=" + std::to_string(popup_wheel));
            popup = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(),
                                 [](const Placement& p) { return p.popup && !p.tooltip; });
            // The assembled dropdown has three fixed rows. Record whether the panel can
            // overflow at this actual size instead of treating a missing list driver as a pass.
            if (popup != f.host.frame().overlays.end()) {
                auto option_count = popup->options.size();
                int rows_height = option_count ? popup->options.back().y + popup->options.back().h - popup->bounds.y : 0;
                if (rows_height <= popup->bounds.h)
                    report.na("X02-v01-panel-list-scroll", sample + ": " + std::to_string(option_count) +
                        " options; rows=" + std::to_string(rows_height) + "px, panel=" + std::to_string(popup->bounds.h) + "px; no overflow");
                else
                    report.check("X02-v01-panel-list-scroll", false, sample + ": panel overflows; independent list scroll must be tested");
            } else report.check("X02-v01-panel-list-scroll", false, sample + ": open panel missing");
            // WM_KEYDOWN dispatches to the focused component; only list navigation schedules
            // reveal. A dropdown owns the keyboard while open and is not a scrollable region.
            report.na("X02-popup-keyboard-region-scroll", sample + ": host offers no keyboard scrolling of a region while a popup is open");
            region.cancel();
            prepared = prepare_dropdown();
            scroll_region = f.host.frame().regions.front();
            before_scroll = f.form.scroll.offset;
            POINT track_point{scroll_region.track.x + scroll_region.track.w / 2,
                before_scroll < scroll_region.maximum ? scroll_region.track.y + scroll_region.track.h - 1 : scroll_region.track.y};
            f.mouse(WM_LBUTTONDOWN, track_point, MK_LBUTTON); f.mouse(WM_LBUTTONUP, track_point); f.settle();
            bool track_cancel = f.form.scroll.offset != before_scroll && !region.describe()->expanded &&
                !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
            report.check("X02-v01-track-cancel", prepared && track_cancel,
                sample + ": track moved and canceled=" + std::to_string(track_cancel) + ", anchor=" + anchor_visibility(region));

            prepared = prepare_dropdown();
            scroll_region = f.host.frame().regions.front();
            before_scroll = f.form.scroll.offset;
            POINT thumb_point{scroll_region.thumb.x + scroll_region.thumb.w / 2, scroll_region.thumb.y + scroll_region.thumb.h / 2};
            POINT drag_point{thumb_point.x, before_scroll < scroll_region.maximum ? scroll_region.track.y + scroll_region.track.h - 1 : scroll_region.track.y};
            f.mouse(WM_LBUTTONDOWN, thumb_point, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, drag_point, MK_LBUTTON);
            f.mouse(WM_LBUTTONUP, drag_point); f.settle();
            bool thumb_cancel = f.form.scroll.offset != before_scroll && !region.describe()->expanded &&
                !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
            report.check("X02-v01-thumb-cancel", prepared && thumb_cancel,
                sample + ": thumb moved and canceled=" + std::to_string(thumb_cancel) + ", anchor=" + anchor_visibility(region));

            // Exercise each input at each reachable clipped endpoint, using only actual page
            // geometry. A one-pixel wheel step models a high-resolution wheel message.
            h.long_text.invoke(); f.host.paint();
            for (int input = 0; input < 3; ++input) for (bool partial : {true, false}) {
                auto geometry = f.host.frame();
                auto* anchor = geometry.find(region);
                auto bar = geometry.regions.front();
                int original_top = anchor->bounds.y + f.form.scroll.offset;
                auto height_at = [&](int offset) {
                    int top = original_top - offset;
                    return std::max(0, std::min(top + anchor->bounds.h, anchor->clip.y + anchor->clip.h) - std::max(top, anchor->clip.y));
                };
                auto desired = [&](int offset) { int visible = height_at(offset); return partial ? visible > 0 && visible < anchor->bounds.h : visible == 0; };
                int target = -1, before = -1, pointer_y = 0;
                if (input == 0) {
                    for (int n = 0; n <= bar.maximum && target < 0; ++n) if (desired(n)) {
                        for (int pre : {n - 1, n + 1}) if (pre >= 0 && pre <= bar.maximum && height_at(pre) > 0) {
                            target = n; before = pre; break;
                        }
                    }
                } else {
                    for (int y = bar.track.y; y < bar.track.y + bar.track.h && target < 0; ++y) {
                        int top = y - bar.thumb.h / 2;
                        int n = bar.offset_for_thumb(top);
                        if (!desired(n)) continue;
                        for (int start = 0; start <= bar.maximum; ++start) {
                            if (n == start || height_at(start) <= 0) continue;
                            int thumb_top = bar.track.y + (bar.track.h - bar.thumb.h) * start / std::max(1, bar.maximum);
                            if (input == 1 && y >= thumb_top && y < thumb_top + bar.thumb.h) continue;
                            target = n; before = start; pointer_y = y; break;
                        }
                    }
                }
                const char* case_id = input == 0 ? "X02-v01-edge-wheel" : input == 1 ? "X02-v01-edge-track" : "X02-v01-edge-thumb";
                auto detail = sample + (partial ? " partial" : " clipped");
                if (target < 0) { report.na(case_id, detail + ": no visible starting offset and reachable endpoint; anchor top=" +
                    std::to_string(original_top) + ", height=" + std::to_string(anchor->bounds.h) + ", clip=" + str(anchor->clip) +
                    ", maximum=" + std::to_string(bar.maximum) + ", track=" + str(bar.track) + ", thumb height=" + std::to_string(bar.thumb.h)); continue; }
                geometry.scroll_to(f.form.ui, f.form.scroll, before); f.host.paint();
                region.focus(); region.open(); f.tap(VK_DOWN); f.tap(VK_DOWN); f.settle();
                bool candidate = region.describe()->expanded && region.describe()->candidate == OptionValue{"south"};
                if (input == 0) {
                    POINT p{bar.clip.x + 2, bar.clip.y + 2}; ClientToScreen(f.host.window(), &p);
                    int unit = int(std::ceil(120.0 / (48 * scale)));
                    int delta = target > before ? -unit : unit;
                    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(p.x, p.y));
                } else if (input == 1) {
                    POINT p{bar.track.x + bar.track.w / 2, pointer_y};
                    f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
                } else {
                    auto thumb = f.host.frame().regions.front().thumb;
                    POINT start{thumb.x + thumb.w / 2, thumb.y + thumb.h / 2};
                    POINT end{start.x, pointer_y};
                    f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, end, MK_LBUTTON); f.mouse(WM_LBUTTONUP, end);
                }
                f.settle();
                auto actual = anchor_visibility(region);
                bool pass = candidate && f.form.scroll.offset != before && actual == (partial ? "partial" : "clipped") &&
                    !region.describe()->expanded && !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
                report.check(case_id, pass, detail + ": anchor=" + actual + ", canceled=" + std::to_string(pass));
            }
            h.long_text.invoke(); f.host.paint();

            h.tabs.activate({"v02"});
            auto& list_page = *f.form.list;
            list_page.handles.hundred.invoke(); list_page.handles.list.focus();
            f.tap(VK_END); f.settle();
            auto* list = f.host.frame().find(list_page.handles.list);
            bool end = list && list->options.size() == 100 && intersect(list->options.back(), list->clip) == list->options.back() &&
                list_page.selection() == OptionValue{"T100"} && list_page.list_scroll.offset > 0;
            bool wheel = false, hit = false;
            if (list) {
                auto visible = intersect(list->bounds, list->clip);
                POINT p{visible.x + visible.w / 2, visible.y + visible.h / 2};
                auto* target = f.host.frame().hit(p.x, p.y);
                hit = target && target->id == list_page.handles.list.describe()->id;
                int old_offset = list_page.list_scroll.offset;
                ClientToScreen(f.host.window(), &p);
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(120)), MAKELPARAM(p.x, p.y)); f.settle();
                wheel = list_page.list_scroll.offset < old_offset && list_page.selection() == OptionValue{"T100"};
            }
            f.tap(VK_END); f.settle();
            list = f.host.frame().find(list_page.handles.list);
            bool restored = list && intersect(list->options.back(), list->clip) == list->options.back() &&
                list_page.selection() == OptionValue{"T100"};
            report.check("X02-v02-list", end && hit && wheel && restored,
                sample + ": End=" + std::to_string(end) + ", hit=" + std::to_string(hit) +
                ", wheel=" + std::to_string(wheel) + ", End again=" + std::to_string(restored));

            h.tabs.activate({"v03"}); f.settle();
            auto open_dialog = [&] { h.dialog_open.focus(); h.dialog_open.invoke(); f.settle(); return *h.dialog.is_open(); };
            bool opened = open_dialog();
            auto frame = f.host.frame();
            int before = f.form.scroll.offset;
            bool has_viewport = !frame.regions.empty();
            int maximum = has_viewport ? frame.regions.front().maximum : 0;
            bool first_wheel_unchanged = false;
            if (has_viewport) {
                auto viewport = frame.regions.front().clip;
                POINT p{viewport.x + 2, viewport.y + 2}; ClientToScreen(f.host.window(), &p);
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(120)), MAKELPARAM(p.x, p.y));
                f.settle(); first_wheel_unchanged = f.form.scroll.offset == before;
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(p.x, p.y));
                f.settle();
            }
            bool blocked = has_viewport && first_wheel_unchanged && f.form.scroll.offset == before && *h.dialog.is_open() && h.dialog_cancel.describe()->focused;
            f.tap(VK_ESCAPE); f.settle();
            bool dialog_escape = !*h.dialog.is_open() && h.dialog_open.describe()->focused && f.form.progress() == 50 &&
                f.form.dialog_confirmations() == 0 && !f.host.frame().modal;
            opened = open_dialog() && opened;
            auto click_button = [&](const ButtonHandle& button) {
                auto* item = f.host.frame().find(button);
                if (!item) return false;
                auto r = intersect(item->bounds, item->clip);
                if (r.w <= 0 || r.h <= 0) return false;
                POINT p{r.x + r.w / 2, r.y + r.h / 2};
                auto* hit = f.host.frame().hit(p.x, p.y);
                bool correct = hit && hit->id == button.describe()->id;
                f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p); f.settle();
                return correct;
            };
            bool cancel_hit = click_button(h.dialog_cancel);
            bool cancel = cancel_hit && !*h.dialog.is_open() && h.dialog_open.describe()->focused && f.form.progress() == 50 &&
                f.form.dialog_confirmations() == 0 && !f.host.frame().modal;
            opened = open_dialog() && opened;
            bool confirm_hit = click_button(h.dialog_confirm);
            bool confirm = confirm_hit && !*h.dialog.is_open() && h.dialog_open.describe()->focused && f.form.progress() == 0 &&
                f.form.dialog_confirmations() == 1 && !f.host.frame().modal;
            report.check("X02-v03-dialog", opened && blocked && dialog_escape && cancel && confirm,
                sample + ": modal wheel=" + (maximum == 0 ? std::string("N/A (no overflow)") : std::to_string(blocked)) + ", scroll maximum=" + std::to_string(maximum) + ", Esc=" + std::to_string(dialog_escape) +
                ", Cancel=" + std::to_string(cancel) + ", Confirm=" + std::to_string(confirm));

            std::uint64_t now = GetTickCount64();
            auto& state = detail::access(f.form.ui);
            state.tooltip_clock([&] { return now; });
            SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
            f.post(WM_MOUSELEAVE, 0, 0); f.host.pump();
            h.progress_half.focus(); f.settle(); now += 600; state.tooltip_time(now); f.settle();
            bool shown = *h.progress_tip.visible() && h.progress_half.describe()->focused;
            auto tip_scroll = f.host.frame().regions.front();
            bool tip_wheel = tip_scroll.maximum == 0;
            bool dwell = tip_wheel;
            if (tip_scroll.maximum > 0) {
                int old = f.form.scroll.offset;
                POINT point{tip_scroll.clip.x + 2, tip_scroll.clip.y + 2}; ClientToScreen(f.host.window(), &point);
                int delta = old < tip_scroll.maximum ? -120 : 120;
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(point.x, point.y)); f.settle();
                tip_wheel = f.form.scroll.offset != old && !*h.progress_tip.visible() && h.progress_half.describe()->focused;
                now += 499; state.tooltip_time(now); bool waiting = !*h.progress_tip.visible();
                ++now; state.tooltip_time(now); dwell = waiting && *h.progress_tip.visible();
                f.host.paint();
            }
            if (tip_scroll.maximum == 0) report.na("X02-v03-tooltip-wheel", sample + ": no background overflow");
            else report.check("X02-v03-tooltip-wheel", shown && tip_wheel && dwell,
                sample + ": moved and hid=" + std::to_string(tip_wheel) + ", fresh dwell=" + std::to_string(dwell) + ", anchor=" + anchor_visibility(h.progress_half));
            for (bool thumb : {false, true}) {
                const char* case_id = thumb ? "X02-v03-tooltip-thumb" : "X02-v03-tooltip-track";
                auto bar = f.host.frame().regions.front();
                if (!bar.maximum) { report.na(case_id, sample + ": no background overflow"); continue; }
                int old = f.form.scroll.offset;
                if (thumb) {
                    POINT start{bar.thumb.x + bar.thumb.w / 2, bar.thumb.y + bar.thumb.h / 2};
                    POINT end{start.x, old < bar.maximum ? bar.track.y + bar.track.h - 1 : bar.track.y};
                    f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, end, MK_LBUTTON); f.mouse(WM_LBUTTONUP, end);
                } else {
                    POINT p{bar.track.x + bar.track.w / 2, old < bar.maximum ? bar.track.y + bar.track.h - 1 : bar.track.y};
                    f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
                }
                f.host.paint();
                bool hid = f.form.scroll.offset != old && !*h.progress_tip.visible() && h.progress_half.describe()->focused;
                now += 499; state.tooltip_time(now); bool waiting = !*h.progress_tip.visible();
                ++now; state.tooltip_time(now); bool resumed = *h.progress_tip.visible();
                f.host.paint();
                report.check(case_id, hid && waiting && resumed,
                    sample + ": moved and hid=" + std::to_string(hid) + ", fresh dwell=" + std::to_string(waiting && resumed) + ", anchor=" + anchor_visibility(h.progress_half));
            }
            // Search the actual V03 geometry for every edge/input pair. Preparation
            // sets an offset, while the scroll under test enters through Win32 messages.
            for (int input = 0; input < 3; ++input) for (bool partial : {true, false}) {
                h.progress_half.focus(); f.settle();
                auto geometry = f.host.frame();
                auto* anchor = geometry.find(h.progress_half);
                auto bar = geometry.regions.front();
                const char* case_id = input == 0 ? "X02-v03-edge-wheel" : input == 1 ? "X02-v03-edge-track" : "X02-v03-edge-thumb";
                std::string detail = sample + (partial ? " partial" : " clipped");
                if (!anchor) { report.check(case_id, false, detail + ": tooltip source missing"); continue; }
                int original_top = anchor->bounds.y + f.form.scroll.offset;
                auto height_at = [&](int offset) {
                    int top = original_top - offset;
                    return std::max(0, std::min(top + anchor->bounds.h, anchor->clip.y + anchor->clip.h) - std::max(top, anchor->clip.y));
                };
                auto desired = [&](int offset) { int visible = height_at(offset); return partial ? visible > 0 && visible < anchor->bounds.h : visible == 0; };
                int target = -1, before = -1, pointer_y = 0;
                if (input == 0) {
                    for (int n = 0; n <= bar.maximum && target < 0; ++n) if (desired(n))
                        for (int pre : {n - 1, n + 1}) if (pre >= 0 && pre <= bar.maximum && height_at(pre) > 0) {
                            target = n; before = pre; break;
                        }
                } else {
                    for (int y = bar.track.y; y < bar.track.y + bar.track.h && target < 0; ++y) {
                        int n = bar.offset_for_thumb(y - bar.thumb.h / 2);
                        if (!desired(n)) continue;
                        for (int start = 0; start <= bar.maximum; ++start) {
                            if (n == start || height_at(start) <= 0) continue;
                            int thumb_top = bar.track.y + (bar.track.h - bar.thumb.h) * start / std::max(1, bar.maximum);
                            if (input == 1 && y >= thumb_top && y < thumb_top + bar.thumb.h) continue;
                            target = n; before = start; pointer_y = y; break;
                        }
                    }
                }
                if (target < 0) {
                    report.na(case_id, detail + ": no visible start and reachable endpoint; anchor top=" + std::to_string(original_top) +
                        ", height=" + std::to_string(anchor->bounds.h) + ", clip=" + str(anchor->clip) +
                        ", maximum=" + std::to_string(bar.maximum) + ", track=" + str(bar.track) + ", thumb height=" + std::to_string(bar.thumb.h));
                    continue;
                }
                geometry.scroll_to(f.form.ui, f.form.scroll, before); f.host.paint();
                now += 600; state.tooltip_time(now); f.host.paint();
                bool opened = *h.progress_tip.visible();
                if (input == 0) {
                    POINT p{bar.clip.x + 2, bar.clip.y + 2}; ClientToScreen(f.host.window(), &p);
                    int unit = int(std::ceil(120.0 / (48 * scale)));
                    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(target > before ? -unit : unit)), MAKELPARAM(p.x, p.y));
                } else if (input == 1) {
                    POINT p{bar.track.x + bar.track.w / 2, pointer_y};
                    f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
                } else {
                    auto thumb = f.host.frame().regions.front().thumb;
                    POINT start{thumb.x + thumb.w / 2, thumb.y + thumb.h / 2}; POINT end{start.x, pointer_y};
                    f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, end, MK_LBUTTON); f.mouse(WM_LBUTTONUP, end);
                }
                f.settle();
                auto actual = anchor_visibility(h.progress_half);
                bool pass = opened && f.form.scroll.offset != before && actual == (partial ? "partial" : "clipped") &&
                    !*h.progress_tip.visible() && h.progress_half.describe()->focused;
                report.check(case_id, pass, detail + ": before=" + std::to_string(before) + ", target=" + std::to_string(target) +
                    ", actual offset=" + std::to_string(f.form.scroll.offset) + ", anchor=" + actual + ", opened=" + std::to_string(opened));
            }
            // Pointer-only eligibility: a scroll at 400 ms cancels the old deadline;
            // a new complete 500 ms dwell may show the tip if the anchor remains under it.
            h.progress_half.focus(); f.settle();
            auto hover_bar = f.host.frame().regions.front();
            if (!hover_bar.maximum) report.na("X02-v03-pointer-scroll-dwell", sample + ": no background overflow");
            else {
                h.tabs.focus(); f.settle();
                auto source = f.host.frame().find(h.progress_half);
                auto visible = intersect(source->bounds, source->clip);
                POINT p{visible.x + visible.w / 2, visible.y + visible.h / 2};
                SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0);
                SendMessageW(f.host.window(), WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
                now += 400; state.tooltip_time(now); f.host.paint();
                bool pending = !*h.progress_tip.visible();
                int old = f.form.scroll.offset;
                POINT wheel{hover_bar.clip.x + 2, hover_bar.clip.y + 2}; ClientToScreen(f.host.window(), &wheel);
                int unit = int(std::ceil(120.0 / (48 * scale)));
                // Do not pump unrelated TrackMouseEvent leaves caused by the physical
                // desktop cursor: this scenario explicitly owns a synthetic pointer visit.
                SendMessageW(f.host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(old < hover_bar.maximum ? -unit : unit)), MAKELPARAM(wheel.x, wheel.y));
                f.host.paint();
                now += 100; state.tooltip_time(now); f.host.paint(); bool stale = !*h.progress_tip.visible();
                now += 399; state.tooltip_time(now); f.host.paint(); bool waits = !*h.progress_tip.visible();
                ++now; state.tooltip_time(now); f.host.paint(); bool fresh = *h.progress_tip.visible();
                int first_offset = f.form.scroll.offset;
                SendMessageW(f.host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(first_offset < hover_bar.maximum ? -unit : unit)), MAKELPARAM(wheel.x, wheel.y));
                f.host.paint(); bool closed = !*h.progress_tip.visible() && first_offset != f.form.scroll.offset;
                now += 499; state.tooltip_time(now); f.host.paint(); bool second_wait = !*h.progress_tip.visible();
                ++now; state.tooltip_time(now); f.host.paint(); bool reopened = *h.progress_tip.visible();
                report.check("X02-v03-pointer-scroll-dwell", pending && old != first_offset && stale && waits && fresh && closed && second_wait && reopened,
                    sample + ": pending=" + std::to_string(pending) + ", stale=" + std::to_string(stale) +
                    ", waits=" + std::to_string(waits) + ", fresh=" + std::to_string(fresh) + ", closed=" + std::to_string(closed) +
                    ", reopened after fresh dwell=" + std::to_string(second_wait && reopened));
                SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0);
            }
            h.progress_half.focus(); f.settle(); now += 600; state.tooltip_time(now); f.host.paint();
            bool transparent = false;
            auto tip = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(), [](const auto& item) { return item.tooltip; });
            if (tip != f.host.frame().overlays.end()) {
                auto* target = f.host.frame().hit(tip->bounds.x + 1, tip->bounds.y + 1);
                transparent = !target || !target->tooltip;
            }
            f.tap(VK_ESCAPE); f.settle();
            bool tip_escape = !*h.progress_tip.visible() && h.progress_half.describe()->focused && f.form.progress() == 0;
            f.tap(VK_TAB); h.progress_half.focus(); f.settle(); now += 600; state.tooltip_time(now); f.settle();
            bool reshown = *h.progress_tip.visible();
            f.tap(VK_TAB); f.settle();
            bool focus_leave = !*h.progress_tip.visible() && !h.progress_half.describe()->focused && f.form.progress() == 0;
            h.progress_half.focus(); f.settle();
            auto p = f.center(h.progress_half.describe()->id);
            f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p); f.settle();
            // Deliver the synthetic pointer visit synchronously: TrackMouseEvent may also queue
            // a leave for the actual desktop pointer, which is unrelated to this posted click.
            SendMessageW(f.host.window(), WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
            now += 600; state.tooltip_time(now); f.host.paint();
            bool pointer_shown = *h.progress_tip.visible() && f.form.progress() == 50;
            SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0); f.host.paint();
            bool pointer_leave = !*h.progress_tip.visible() && h.progress_half.describe()->focused && f.form.progress() == 50;
            report.check("X02-v03-tooltip", shown && transparent && tip_escape && reshown && focus_leave && pointer_shown && pointer_leave,
                sample + ": shown=" + std::to_string(shown) + ", transparent=" + std::to_string(transparent) +
                ", Esc=" + std::to_string(tip_escape) + ", focus leave=" + std::to_string(focus_leave && reshown) +
                ", pointer shown=" + std::to_string(pointer_shown) + ", pointer leave=" + std::to_string(pointer_leave) + " (controlled dwell clock)");
            state.tooltip_clock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
        }
}

// Inventory is made from the same typed handles as the application. Each row reports what
// the actual host paint observed; an unswept applicable state remains explicitly open.
void semantic_observation_sweep(const Appearance& appearance, const std::filesystem::path& folder, const std::string& tag) {
    std::ofstream out(folder / (tag + "-x04-sweep.tsv"));
    out << "ids\tsize\tscale\tpage\tcomponent\tkind\tvisible\tname\tvalue\trestriction\thidden\tclipped\tdisabled\tread_only\tselected\tcandidate\terror\tconflict\texecuting\tmodal\ttip\n";
    for (bool alternate : {false, true}) {
        Fixture f(appearance, true, alternate);
        auto& h = f.form.handles; auto& l = f.form.list->handles;
        struct Entry { const char* page; ComponentHandle handle; };
        std::vector<Entry> entries{
            {"shared", h.tabs},
            {"v01", h.title}, {"v01", h.intro}, {"v01", h.state}, {"v01", h.draft}, {"v01", h.result}, {"v01", h.count},
            {"v01", h.name_label}, {"v01", h.name_help}, {"v01", h.name_error}, {"v01", h.name},
            {"v01", h.normal}, {"v01", h.read_only}, {"v01", h.disabled}, {"v01", h.error}, {"v01", h.long_text},
            {"v01", h.offline_disabled}, {"v01", h.region_empty}, {"v01", h.reset}, {"v01", h.apply}, {"v01", h.latest},
            {"v01", h.notices_latest}, {"v01", h.mode_latest}, {"v01", h.region_latest}, {"v01", h.volume_latest},
            {"v01", h.notices}, {"v01", h.mode}, {"v01", h.region}, {"v01", h.volume},
            {"v02", l.title}, {"v02", l.detail}, {"v02", l.search}, {"v02", l.list}, {"v02", l.zero}, {"v02", l.one},
            {"v02", l.twenty}, {"v02", l.hundred}, {"v02", l.sort}, {"v02", l.erase}, {"v02", l.disable_eight}, {"v02", l.reset},
            {"v03", h.v03_title}, {"v03", h.progress_source}, {"v03", h.dialog_result}, {"v03", h.progress},
            {"v03", h.progress_zero}, {"v03", h.progress_half}, {"v03", h.progress_full}, {"v03", h.progress_below},
            {"v03", h.progress_above}, {"v03", h.reset_v03}, {"v03", h.dialog_open}, {"v03", h.dialog_temporary},
            {"modal", h.dialog_title}, {"modal", h.dialog_body}, {"modal", h.dialog_cancel}, {"modal", h.dialog_confirm}
        };
        auto describe = [](const ComponentHandle& value) -> std::optional<Semantics> {
            return std::visit([](const auto& handle) -> std::optional<Semantics> {
                using T = std::decay_t<decltype(handle)>;
                if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
                else return handle.describe().value;
            }, value);
        };
        f.host.observe_paint(true);
        for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
            f.host.force_scale(scale); f.host.client_size(size.first, size.second);
            for (const char* page : {"v01", "v02", "v03"}) {
                h.tabs.activate({page}); f.settle();
                for (const auto& entry : entries) {
                    auto s = describe(entry.handle); if (!s) continue;
                    bool active = std::string_view(entry.page) == "shared" || std::string_view(entry.page) == page;
                    const Placement* placement = std::visit([&](const auto& handle) -> const Placement* {
                        using T = std::decay_t<decltype(handle)>;
                        if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
                        else return f.host.frame().find(handle);
                    }, entry.handle);
                    std::u16string ink;
                    bool any = false;
                    for (const auto& draw : f.host.paint_observations()) {
                        if (draw.owner != s->id && (s->target.empty() || draw.owner != s->target)) continue;
                        if (!draw.visible) continue;
                        any = true; if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    }
                    auto text_status = [&](const std::u16string& text) {
                        if (text.empty()) return "N/A";
                        if (!active || !s->visible) return "O";
                        return ink.find(text) != std::u16string::npos ? "P" : "O";
                    };
                    auto applicable = [](bool yes) { return yes ? "O" : "N/A"; };
                    auto k = s->kind;
                    const auto& type = detail::component_type(k);
                    using S = detail::ComponentState;
                    bool restricted = type.supports_state(S::disabled);
                    bool selected = type.supports_state(S::selected);
                    bool candidate = type.supports_state(S::candidate);
                    bool error = type.supports_state(S::error);
                    bool conflict = type.supports_state(S::conflict);
                    bool associated = !s->target.empty();
                    out << (alternate ? "alternate" : "shipped") << '\t' << size.first << 'x' << size.second << '\t' << int(scale * 100)
                        << '\t' << page << '\t' << s->id << '\t' << static_cast<int>(k) << '\t' << (s->visible ? "yes" : "no")
                        << '\t' << text_status(s->name) << '\t' << text_status(s->value) << '\t' << applicable(restricted) << '\t'
                        << (!active && std::string_view(entry.page) != "modal" && !s->visible && !any && !placement && !associated ? "P" : active ? "N/A" : "O") << '\t'
                        << (active && placement && !s->visible && !any && !associated ? "P" : "O") << '\t'
                        << applicable(restricted) << '\t' << applicable(type.supports_state(S::read_only)) << '\t' << applicable(selected) << '\t'
                        << applicable(candidate) << '\t' << applicable(error) << '\t' << applicable(conflict) << '\t'
                        << applicable(type.supports_state(S::executing)) << '\t' << applicable(std::string_view(entry.page) == "modal") << '\t'
                        << applicable(s->id == h.progress_half.describe()->id) << '\n';
                }
            }
        }
    }
}

// State driver shared with the core seam, sampled through real clipped host paint.
void x04_name_read_only_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& name = f.form.handles.name;
            f.host.force_scale(scale); f.host.client_size(size.first, size.second);
            bool driven = bool(x04::name_read_only.drive(f.form)) && bool(name.focus());
            f.host.observe_paint(true); f.settle();
            auto semantic = name.describe(); auto suffix = appearance.component_presentation().read_only_suffix;
            std::u16string ink;
            for (const auto& event : f.host.paint_observations())
                if (event.owner == semantic->id && event.visible && event.type == detail::PaintObservation::Type::text) ink += event.text;
            bool restricted = name.set_value(u"rejected").status == Status::read_only &&
                std::find(semantic->operations.begin(), semantic->operations.end(), Operation::edit) == semantic->operations.end();
            auto& h = f.form.handles;
            bool associations = h.name_label.describe()->target == semantic->id && h.name_label.describe()->role == TextRole::name &&
                h.name_help.describe()->target == semantic->id && h.name_help.describe()->role == TextRole::description &&
                h.name_error.describe()->target == semantic->id && h.name_error.describe()->role == TextRole::error &&
                semantic->labels.size() == 3 && !semantic->invalid && !semantic->source_conflict;
            bool painted = associations && !suffix.empty() && ink.find(semantic->name) != std::u16string::npos &&
                ink.find(semantic->value) != std::u16string::npos && ink.find(semantic->description) != std::u16string::npos &&
                ink.find(suffix) != std::u16string::npos;
            bool restored = bool(x04::name_read_only.restore(f.form)) && !name.describe()->read_only;
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "%";
            report.check("X04-name-read-only", driven && semantic->visible && semantic->enabled && semantic->read_only && restricted && painted && restored,
                sample + ": semantic=" + std::to_string(semantic->read_only) + ", restricted=" + std::to_string(restricted) +
                ", name and read-only cue painted=" + std::to_string(painted) + ", restored=" + std::to_string(restored));
        }
}

void x04_name_conflict_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& h = f.form.handles;
            f.host.force_scale(scale); f.host.client_size(size.first, size.second); f.settle();
            bool driven = bool(x04::name_conflict.drive(f.form)) && bool(h.name.focus());
            f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, h.name.describe()->id);
            f.host.observe_paint(true); f.host.paint();
            auto semantic = h.name.describe();
            std::u16string ink;
            for (const auto& event : f.host.paint_observations())
                if (event.owner == semantic->id && event.visible && event.type == detail::PaintObservation::Type::text) ink += event.text;
            const auto& cue = appearance.component_presentation().conflict_suffix;
            bool associations = h.name_label.describe()->target == semantic->id && h.name_label.describe()->role == TextRole::name &&
                h.name_help.describe()->target == semantic->id && h.name_help.describe()->role == TextRole::description &&
                h.name_error.describe()->target == semantic->id && h.name_error.describe()->role == TextRole::error &&
                semantic->labels.size() == 3 && !semantic->invalid;
            bool field_painted = semantic->visible && semantic->enabled && !semantic->read_only && semantic->source_conflict && !cue.empty() && associations &&
                ink.find(semantic->name) != std::u16string::npos && ink.find(semantic->value) != std::u16string::npos &&
                ink.find(semantic->description) != std::u16string::npos && ink.find(cue) != std::u16string::npos;
            bool latest_enabled = h.latest.describe()->enabled && bool(h.latest.focus()); f.settle();
            auto latest = h.latest.describe();
            bool action_painted = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& event) {
                return event.owner == latest->id && event.visible && event.type == detail::PaintObservation::Type::text &&
                       event.text.find(latest->name) != std::u16string::npos;
            });
            bool restored = bool(x04::name_conflict.restore(f.form)) && !h.name.describe()->source_conflict && *h.name.draft() == u"Latest";
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "%";
            report.check("X04-name-conflict", driven && field_painted && latest_enabled && action_painted && restored,
                sample + ": field name, value and cue painted=" + std::to_string(field_painted) + ", latest enabled=" +
                std::to_string(latest_enabled) + ", action painted=" + std::to_string(action_painted) + ", restored=" + std::to_string(restored));
        }
}

// Raster evidence uses the same real fonts, component adapters, clipping and Canvas as the host.
// Colored pixels demonstrate a cue was drawn; they are not OCR of the semantic string.
void semantic_raster_samples(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    auto contains_color = [](const Canvas& canvas, Rect bounds, Color color) {
        auto r = intersect(bounds, {0, 0, canvas.width, canvas.height});
        for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) {
            auto i = (static_cast<std::size_t>(canvas.height - 1 - y) * canvas.width + x) * 4;
            if (canvas.pixels[i] == color.r && canvas.pixels[i + 1] == color.g && canvas.pixels[i + 2] == color.b) return true;
        }
        return false;
    };
    auto sample = [&](std::string_view name, Canvas& pixels) {
        return f.host.render(960, 720, 1.f, folder / (report.appearance + "-x04-" + std::string(name) + ".bmp"), &pixels);
    };
    h.error.invoke();
    f.host.layout(960, 720, 1.f, f.form.scroll, h.name.describe()->id);
    f.host.observe_paint(true);
    f.host.paint();
    const auto* actual_field = f.host.frame().find(h.name);
    bool observed_error = actual_field && !actual_field->error_lines.empty() &&
        std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& event) {
            return event.owner == h.name.describe()->id && event.text == actual_field->error_lines.front().text && event.visible;
        });
    report.check("X04-native-paint-observation", observed_error,
                 "The actual host paint path records visible error text under its owning field");
    Canvas pixels(1, 1, {0, 0, 0});
    auto frame = sample("v01-error", pixels);
    auto* field = frame.find(h.name);
    bool error = field && h.name.describe()->invalid && !h.name.describe()->error.empty() && !field->error_lines.empty() &&
        contains_color(pixels, intersect(field->error_lines.front().bounds, field->clip), appearance.palette().error);
    report.check("X04-v01-raster", error, "Name semantics report an error and the real host raster contains error-colored pixels in the error line");

    h.normal.invoke(); h.name.set_value(u"Tester"); h.notices.toggle();
    f.form.reject_next_apply_with_notices_source_change();
    auto rejected = h.apply.invoke();
    h.notices.focus(); f.settle();
    auto box_id = h.notices.describe()->id;
    bool conflict_ink = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& event) {
        return event.owner == box_id && event.visible && event.type == detail::PaintObservation::Type::text &&
               event.text.find(appearance.component_presentation().conflict_suffix) != std::u16string::npos;
    });
    bool checkbox_conflict = rejected.status == Status::invalid_value && h.notices.describe()->source_conflict &&
        h.notices_latest.describe()->enabled && conflict_ink;
    h.notices_latest.focus(); f.settle();
    auto latest = h.notices_latest.describe();
    bool latest_ink = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& event) {
        return event.owner == latest->id && event.visible && event.text.find(latest->name) != std::u16string::npos;
    });
    bool recovered = bool(h.notices_latest.invoke()) && !h.notices.describe()->source_conflict && !*h.notices.draft();
    report.check("X04-checkbox-conflict", checkbox_conflict && latest_ink && recovered,
        "Assembled checkbox conflict and latest action are painted, then latest adopts the source");

    bool executing_painted = false;
    h.normal.focus(); f.settle();
    h.normal.bind([&] {
        auto semantic = h.normal.describe();
        f.host.paint();
        executing_painted = semantic->executing &&
            std::none_of(semantic->operations.begin(), semantic->operations.end(), [](Operation op) { return op == Operation::activate; }) &&
            std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& event) {
                return event.owner == semantic->id && event.visible && event.text.find(semantic->name) != std::u16string::npos &&
                       event.text.find(u"\uff08\u57f7\u884c\u4e2d\uff09") != std::u16string::npos;
            });
        return Result{};
    });
    bool invoked = bool(h.normal.invoke());
    report.check("X04-button-executing", invoked && executing_painted && !*h.normal.executing(),
        "Real paint during the callback identifies executing and semantics refuse reactivation");

    h.tabs.activate({"v02"});
    auto& list = f.form.list->handles.list;
    list.select({"T003"}); list.focus();
    f.host.layout(960, 720, 1.f, f.form.scroll, list.describe()->id);
    frame = sample("v02-selection", pixels);
    auto* item = frame.find(list);
    auto semantic = list.describe();
    bool selected = item && semantic->options.size() > 2 && semantic->options[2].selected && item->options.size() > 2 &&
        contains_color(pixels, intersect(item->options[2], item->clip), appearance.palette().selection);
    report.check("X04-v02-raster", selected, "T003 is semantically selected and its visible real-host row contains the selection color");
    f.host.paint();
    auto painted_name = [&](const Semantics& s) {
        return std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& event) {
            return event.owner == s.id && event.visible && event.text.find(s.name) != std::u16string::npos;
        });
    };
    report.check("X04-list-and-tabs-names", painted_name(*list.describe()) && painted_name(*h.tabs.describe()),
                 "Visible list and tab group names agree with the typed semantic descriptions");

    h.tabs.activate({"v03"}); h.dialog_open.invoke();
    frame = sample("v03-dialog", pixels);
    auto* title = frame.find(h.dialog_title); auto* body = frame.find(h.dialog_body);
    auto* cancel = frame.find(h.dialog_cancel); auto* confirm = frame.find(h.dialog_confirm);
    auto ink = [&](const Placement* p) {
        return p && !p->lines.empty() && contains_color(pixels, intersect(p->lines.front().bounds, p->clip), appearance.palette().text);
    };
    bool modal = frame.modal && !h.dialog_title.describe()->value.empty() && !h.dialog_body.describe()->value.empty() &&
        !h.dialog_cancel.describe()->name.empty() && !h.dialog_confirm.describe()->name.empty() &&
        ink(title) && ink(body) && ink(cancel) && ink(confirm);
    report.check("X04-v03-raster", modal, "Open dialog semantic title, body and button names have real-host text pixels in their line regions");
}

}

// ---- Real input session: SendInput through the OS input stack and the Microsoft Bopomofo IME ----
namespace {
struct Blocked : std::runtime_error { using std::runtime_error::runtime_error; };

std::string file_version(const wchar_t* path) {
    DWORD ignored = 0, size = GetFileVersionInfoSizeW(path, &ignored);
    std::vector<char> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!size || !GetFileVersionInfoW(path, 0, size, data.data()) || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length))
        return "unknown";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(info->dwFileVersionLS)) + "." + std::to_string(LOWORD(info->dwFileVersionLS));
}
std::string os_version() {
    HKEY key = nullptr;
    std::string result = "unknown";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t text[64]{};
        DWORD ubr = 0, bytes = sizeof(text);
        RegQueryValueExW(key, L"CurrentBuild", nullptr, nullptr, reinterpret_cast<BYTE*>(text), &bytes);
        DWORD n = sizeof(ubr);
        RegQueryValueExW(key, L"UBR", nullptr, nullptr, reinterpret_cast<BYTE*>(&ubr), &n);
        wchar_t display[64]{};
        bytes = sizeof(display);
        RegQueryValueExW(key, L"DisplayVersion", nullptr, nullptr, reinterpret_cast<BYTE*>(display), &bytes);
        result = "Windows build " + narrow(std::wstring(text)) + "." + std::to_string(ubr) + " (" + narrow(std::wstring(display)) + ")";
        RegCloseKey(key);
    }
    return result;
}

struct Live {
    edit_form::Form form{edit_form::default_ids(), true};
    std::unique_ptr<DesktopHost> owner;
    DesktopHost& host;
    const edit_form::Ids& ids;
    HWND helper = nullptr;
    std::size_t logged = 0;
    explicit Live(const Appearance& appearance)
        : owner(std::make_unique<DesktopHost>(form.ui, appearance, [this] { return form.page(); }, form.scroll, Fixture::options(), true, false)),
          host(*owner), ids(form.ids()) {
        host.client_size(800, 600);
        SetWindowPos(host.window(), HWND_TOP, 40, 40, 0, 0, SWP_NOSIZE);
        ShowWindow(host.window(), SW_SHOW);
        foreground(host.window());
        helper = CreateWindowExW(0, L"STATIC", L"LcarsUI IME helper", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 900, 60, 300, 200,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        foreground(host.window());
    }
    ~Live() { if (helper) DestroyWindow(helper); }
    void wait(int ms) {
        auto end = GetTickCount64() + ms;
        while (GetTickCount64() < end) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(end - GetTickCount64()), QS_ALLINPUT);
            host.pump();
        }
        UpdateWindow(host.window());
    }
    void foreground(HWND target) {
        SetForegroundWindow(target);
        wait(150);
        if (GetForegroundWindow() != target) {
            INPUT alt[2]{};
            alt[0].type = alt[1].type = INPUT_KEYBOARD;
            alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
            alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, alt, sizeof(INPUT));
            SetForegroundWindow(target);
            wait(200);
        }
        if (GetForegroundWindow() != target) throw Blocked("could not bring the test window to the foreground");
    }
    void guard() {
        if (GetForegroundWindow() != host.window()) throw Blocked("foreground moved away from the test window; input stopped");
    }
    void send(WORD vk, bool up) {
        guard();
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        if (SendInput(1, &in, sizeof(INPUT)) != 1) throw Blocked("SendInput refused (UIPI or desktop lock)");
        wait(60);
    }
    void tap(WORD vk) { send(vk, false); send(vk, true); }
    void chord(WORD modifier, WORD vk) { send(modifier, false); tap(vk); send(modifier, true); }
    void type(const char* keys) { for (const char* k = keys; *k; ++k) tap(static_cast<WORD>(*k)); wait(250); }
    void click_point(POINT p) {
        guard();
        ClientToScreen(host.window(), &p);
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        INPUT in[3]{};
        for (auto& i : in) {
            i.type = INPUT_MOUSE;
            i.mi.dx = static_cast<LONG>((p.x - vx) * 65535.0 / (vw - 1));
            i.mi.dy = static_cast<LONG>((p.y - vy) * 65535.0 / (vh - 1));
            i.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
        }
        in[1].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        in[2].mi.dwFlags |= MOUSEEVENTF_LEFTUP;
        SendInput(3, in, sizeof(INPUT));
        wait(300);
    }
    void click(std::string_view id, int x_bias = 0) {
        auto r = detail::find_placement(host.frame(), id)->bounds;
        click_point({r.x + r.w / 2 + x_bias, r.y + r.h / 2});
    }
    int history() {
        auto& ui = form.ui;
        int n = 0;
        while (detail::access(ui).undo(ids.name)) ++n;
        for (int i = 0; i < n; ++i) detail::access(ui).redo(ids.name);
        return n;
    }
    std::string state() {
        const auto* s = detail::access(form.ui).edit(ids.name);
        return "draft='" + narrow(s->text) + "' composing=" + (s->composing ? "1" : "0") + " preedit='" + narrow(s->preedit) +
               "' focus=" + detail::access(form.ui).focused() + " history=" + std::to_string(history()) + " applies=" + std::to_string(form.applies());
    }
    std::string events() {
        std::string out;
        auto& log = host.ime_log();
        for (; logged < log.size(); ++logged) out += narrow(log[logged]) + " | ";
        return out;
    }
    void clear_composition() {
        for (int i = 0; i < 3 && detail::access(form.ui).edit(ids.name)->composing; ++i) tap(VK_ESCAPE);
        wait(150);
        if (detail::access(form.ui).edit(ids.name)->composing) throw Blocked("a composition survived three Escape presses");
    }
    void start_in_name() {
        clear_composition();
        detail::access(form.ui).invoke(ids.reset);
        detail::access(form.ui).focus(ids.name);
        detail::access(form.ui).select(ids.name, 5, 5);
        wait(150);
        events();
    }
};

GUID guid(unsigned long a, unsigned short b, unsigned short c, std::initializer_list<unsigned char> d) {
    GUID g{a, b, c, {}};
    std::copy(d.begin(), d.end(), g.Data4);
    return g;
}
// For this process only: Microsoft Bopomofo, then back to the US keyboard.
struct Profiles {
    ITfInputProcessorProfileMgr* manager = nullptr;
    const CLSID bopomofo = guid(0xB115690A, 0xEA02, 0x48D5, {0xA2, 0x31, 0xE3, 0x57, 0x8D, 0x2F, 0xDF, 0x80});
    const GUID profile = guid(0xB2F9C502, 0x1742, 0x11D4, {0x97, 0x90, 0x00, 0x80, 0xC8, 0x82, 0x68, 0x7E});
    Profiles() {
        if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                    reinterpret_cast<void**>(&manager))))
            throw Blocked("TSF profile manager unavailable");
    }
    ~Profiles() { us(); if (manager) manager->Release(); }
    bool chinese() {
        return SUCCEEDED(manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0404, bopomofo, profile, nullptr,
                                                  TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE));
    }
    void us() {
        manager->ActivateProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, 0x0409, CLSID_NULL, GUID_NULL,
                                 reinterpret_cast<HKL>(static_cast<std::uintptr_t>(0x04090409)), TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    }
    std::string active() {
        TF_INPUTPROCESSORPROFILE p{};
        if (FAILED(manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &p))) return "unknown";
        bool is_bopomofo = IsEqualGUID(p.clsid, bopomofo) && IsEqualGUID(p.guidProfile, profile);
        return "langid=0x" + [&] { char b[8]; std::snprintf(b, sizeof b, "%04x", p.langid); return std::string(b); }() +
               (is_bopomofo ? " Microsoft Bopomofo TIP" : p.dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT ? " keyboard layout" : " other TIP");
    }
};
}

bool ime_session(const Appearance& appearance, Report& report, std::ofstream& events) {
    try {
        Live live(appearance);
        auto& ui = live.form.ui;
        const auto& ids = live.ids;
        Profiles profiles;
        profiles.us();
        live.wait(200);
        report.check("ENV", true, os_version() + "; IMTCTIP.DLL " + file_version(L"C:\\Windows\\System32\\IME\\IMETC\\IMTCTIP.DLL") +
                                      "; ChtIME.exe " + file_version(L"C:\\Windows\\System32\\InputMethod\\CHT\\ChtIME.exe") +
                                      "; DPI scale " + std::to_string(live.host.scale()));
        auto record = [&](const char* step, bool pass, const std::string& expectation) {
            auto e = live.events();
            report.check(step, pass, expectation + " => " + live.state());
            events << step << '\t' << e << '\n';
            for (const auto& line : live.host.trace()) events << "  trace\t" << narrow(line) << '\n';
            live.host.trace().clear();
        };

        // AC07 with real input on the US layout: Space activates the error fixture, focus moves to the name.
        detail::access(ui).focus(ids.error);
        live.wait(100);
        live.send(VK_SPACE, false);
        live.send(VK_SPACE, false);  // held: the second press arrives as an auto-repeat
        live.send(VK_SPACE, true);
        live.wait(150);
        bool moved = detail::access(ui).focused() == ids.name && detail::access(ui).text(ids.name).empty() && live.history() == 1;
        record("AC07-real", moved, "Space press, repeat and release on the error fixture: focus to name, no space leaked, one history entry");
        live.tap(VK_SPACE);
        record("AC07-real-typing", detail::access(ui).text(ids.name) == u" " && live.history() == 2, "A real Space typed afterwards inserts one space");

        if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo could not be activated for this process");
        live.start_in_name();
        report.check("IME-profile", true, profiles.active());
        if (auto imc = ImmGetContext(live.host.window())) {
            DWORD conversion = 0, sentence = 0;
            ImmGetConversionStatus(imc, &conversion, &sentence);
            if (!(conversion & IME_CMODE_NATIVE)) ImmSetConversionStatus(imc, conversion | IME_CMODE_NATIVE, sentence);
            ImmGetConversionStatus(imc, &conversion, &sentence);
            report.check("IME-mode", (conversion & IME_CMODE_NATIVE) != 0, "conversion=0x" + std::to_string(conversion) + " (native Chinese mode)");
            ImmReleaseContext(live.host.window(), imc);
        }

        live.type("JI3");
        auto* session = detail::access(ui).edit(ids.name);
        bool composing = session->composing && session->text == u"測試使用者" && !session->preedit.empty();
        record("F02-compose", composing, "ㄨㄛˇ updates the preedit only; committed text unchanged");
        live.tap(VK_RETURN);
        live.wait(200);
        record("F02-confirm", detail::access(ui).text(ids.name) == u"測試使用者我" && !session->composing && live.history() == 1 && live.form.applies() == 0,
               "Enter confirms one character once, one history entry, no apply");
        live.tap(VK_RETURN);
        record("F02-enter-after", live.form.applies() == 0 && detail::access(ui).text(ids.name) == u"測試使用者我", "A second Enter does not apply");
        live.chord(VK_CONTROL, 'Z');
        record("AC09-undo-commit", detail::access(ui).text(ids.name) == u"測試使用者", "Ctrl+Z removes the whole confirmation in one step");

        live.type("JI3");
        live.tap(VK_ESCAPE);
        live.wait(200);
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(200); }
        bool redo_kept = !session->composing && detail::access(ui).text(ids.name) == u"測試使用者" && detail::access(live.form.ui).describe(ids.name)->operations.back() == Operation::redo;
        record("E01-cancel", redo_kept, "Escape cancels the composition; committed text and the redo position survive");

        // Switch pages through the real tab during an active Microsoft Bopomofo composition.
        live.start_in_name();
        live.type("JI3");
        auto page_before = live.state();
        live.host.paint();
        live.click(live.form.handles.tabs.describe()->id);
        bool switched = *live.form.handles.tabs.active() == OptionValue{"v02"} && !session->composing &&
            session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.form.handles.tabs.describe()->focused && live.form.applies() == 0;
        live.form.handles.tabs.activate({"v01"});
        live.wait(100);
        record("E01-page-switch", switched && live.history() == 1,
               "Real tab click during composition commits once, focuses tabs and switches page; before: " + page_before);

        // The host seam opens the dialog while the real field is composing.
        live.start_in_name();
        live.type("JI3");
        auto dialog_before = live.state();
        live.host.paint();
        live.form.handles.dialog.open(live.form.handles.name);
        bool opened = *live.form.handles.dialog.is_open() && live.form.handles.dialog_cancel.describe()->focused &&
            !session->composing && session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" &&
            live.form.progress() == 50 && live.form.dialog_confirmations() == 0;
        if (*live.form.handles.dialog.is_open()) live.form.handles.dialog_cancel.invoke();
        record("E01-dialog-open", opened && live.history() == 1 && live.form.handles.name.describe()->focused,
               "Host seam opens the dialog and completes composition once, focuses cancel, and cancellation restores the field; before: " + dialog_before);

        live.start_in_name();
        live.chord(VK_CONTROL, 'A');
        live.type("JI3");
        live.tap(VK_RETURN);
        live.wait(200);
        bool replaced = detail::access(ui).text(ids.name) == u"我" && live.history() == 1;
        live.chord(VK_CONTROL, 'Z');
        record("E01-replace-selection", replaced && detail::access(ui).text(ids.name) == u"測試使用者", "Confirmation replaces the selection as one undo entry");

        live.start_in_name();
        live.type("JI3");
        live.chord(VK_CONTROL, 'Z');
        live.wait(200);
        bool untouched = detail::access(ui).text(ids.name) == u"測試使用者" && live.history() == 0;
        record("E01-shortcut-while-composing", untouched, "Ctrl+Z during composition goes to the IME and leaves committed history alone");
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(150); }
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(150); }

        live.start_in_name();
        live.type("JI3");
        live.tap(VK_TAB);
        live.wait(300);
        record("E01-tab", session->composing && detail::access(ui).focused() == ids.name && detail::access(ui).text(ids.name) == u"測試使用者" && live.form.applies() == 0,
               "Tab during composition is consumed by the IME: preedit kept, focus stays, nothing committed or applied");
        live.clear_composition();
        record("E01-tab-then-escape", detail::access(ui).text(ids.name) == u"測試使用者" && live.history() == 0, "Escape then discards that preedit cleanly");

        live.start_in_name();
        live.type("JI3");
        auto before = live.state();
        live.click(ids.long_text);
        record("E01-pointer", !session->composing && detail::access(ui).text(ids.name) == u"測試使用者我" && live.history() == 1 && detail::access(ui).focused() == ids.long_text && live.form.applies() == 0,
               "Clicking another control commits the preedit once, then focus moves; before: " + before);

        live.start_in_name();
        live.type("JI3");
        before = live.state();
        SetForegroundWindow(live.helper);
        live.wait(500);
        auto away = live.state();
        report.check("E01-window-deactivate", !session->composing && detail::access(ui).text(ids.name) == u"測試使用者我" && live.history() == 1 && live.form.applies() == 0,
                     "The IME commits on native deactivation, once; before: " + before + " => " + away);
        events << "E01-window-deactivate\t" << live.events() << '\n';
        live.foreground(live.host.window());
        record("E01-window-return", !session->composing && detail::access(ui).focused() == ids.name && detail::access(ui).text(ids.name) == u"測試使用者我" && live.history() == 1,
               "Back in the window: focus kept on the name, nothing composing, the commit kept");

        live.start_in_name();
        live.type("JI3");
        before = live.state();
        live.click(ids.name, -100);
        record("E01-pointer-same-field", !session->composing && detail::access(ui).text(ids.name) == u"測試使用者我" && live.history() == 1 && detail::access(ui).focused() == ids.name && live.form.applies() == 0,
               "Clicking inside the composing field commits once, then places the caret; before: " + before);

        profiles.us();
        live.form.handles.tabs.activate({"v03"});
        live.form.handles.progress_zero.focus(); live.wait(150);
        auto source_id = live.form.handles.progress_half.describe()->id;
        auto source_bounds = detail::find_placement(live.host.frame(), source_id)->bounds;
        POINT hover_point{source_bounds.x + source_bounds.w / 2, source_bounds.y + source_bounds.h / 2};
        ClientToScreen(live.host.window(), &hover_point);
        SetCursorPos(hover_point.x, hover_point.y);
        live.wait(650);
        bool pointer_shown = *live.form.handles.progress_tip.visible() && live.form.handles.progress_zero.describe()->focused;
        POINT away_point{10, 10}; ClientToScreen(live.host.window(), &away_point);
        SetCursorPos(away_point.x, away_point.y); live.wait(150);
        bool pointer_left = !*live.form.handles.progress_tip.visible();
        report.check("P05-real-pointer", pointer_shown && pointer_left,
                     "Physical pointer dwell displays the tooltip without taking focus; leaving closes it");
        live.form.handles.progress_half.focus();
        live.wait(650);
        bool visible_before = *live.form.handles.progress_tip.visible();
        live.foreground(live.helper);
        bool hidden_away = !*live.form.handles.progress_tip.visible();
        live.foreground(live.host.window());
        bool fresh_delay = !*live.form.handles.progress_tip.visible();
        live.wait(600);
        bool visible_after = *live.form.handles.progress_tip.visible() && live.form.handles.progress_half.describe()->focused;
        report.check("R08-real-window-focus", visible_before && hidden_away && fresh_delay && visible_after,
                     "Real foreground switch closes and restarts the delay; tooltip returns without taking source focus");
        live.wait(200);

        // Follow the desktop run loop through WM_CLOSE and host destruction while the model is still alive.
        live.form.handles.tabs.activate({"v01"});
        if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo could not be reactivated for close");
        live.start_in_name();
        live.type("JI3");
        bool close_started = session->composing && !session->preedit.empty();
        auto close_before = live.state();
        PostMessageW(live.host.window(), WM_CLOSE, 0, 0);
        int run_result = live.host.run();
        events << "E01-close-before-destroy\t" << live.events() << '\n';
        live.owner.reset();
        bool close_ok = close_started && run_result == 0 && !session->composing &&
                        detail::access(ui).text(ids.name) == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.history() == 1 && live.form.applies() == 0;
        report.check("E01-close", close_ok,
                     "WM_CLOSE exits the real host loop and destroys its window; before: " + close_before +
                     " => after destruction: " + live.state());
        return true;
    } catch (const Blocked& blocked) {
        report.check("BLOCKED", false, blocked.what());
        events << "BLOCKED\t" << blocked.what() << '\n';
        return false;
    }
}

int run_ime(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "ime.tsv");
    std::ofstream events(folder / "ime-events.log");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        events << "=== " << report.appearance << " ===\n";
        if (!ime_session(*appearance, report, events)) break;
    }
    CoUninitialize();
    std::cout << report.checks << " real-input checks, " << report.failures << " not passing\n";
    return report.failures ? 1 : 0;
}

// Real per-monitor transition. Unlike the offscreen matrix this never forces a scale.
// The window procedure wrapper observes the actual OS message before forwarding it.
namespace {
WNDPROC dpi_previous_proc = nullptr;
int dpi_messages = 0;
LRESULT CALLBACK dpi_observer(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DPICHANGED) ++dpi_messages;
    return CallWindowProcW(dpi_previous_proc, window, msg, w, l);
}
struct DpiObserver {
    HWND window;
    explicit DpiObserver(HWND w) : window(w) {
        dpi_messages = 0;
        dpi_previous_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dpi_observer)));
        if (!dpi_previous_proc) throw Blocked("could not observe WM_DPICHANGED");
    }
    ~DpiObserver() { SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dpi_previous_proc)); }
};
BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* monitors = reinterpret_cast<std::vector<MONITORINFOEXW>*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) monitors->push_back(info);
    return TRUE;
}

bool dpi_session(const Appearance& appearance, Report& report) {
    try {
        std::vector<MONITORINFOEXW> monitors;
        EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&monitors));
        if (monitors.size() < 2) throw Blocked("two active monitors are required");
        std::sort(monitors.begin(), monitors.end(), [](const auto& a, const auto& b) { return a.rcMonitor.left < b.rcMonitor.left; });
        Live live(appearance);
        DpiObserver observer(live.host.window());
        auto& ui = live.form.ui;
        const auto& ids = live.ids;
        auto move = [&](const MONITORINFOEXW& monitor) {
            auto r = monitor.rcMonitor;
            SetWindowPos(live.host.window(), nullptr, r.left + 20, r.top + 20, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            live.wait(400);
            live.host.paint();
            return GetDpiForWindow(live.host.window());
        };
        UINT first = move(monitors[0]);
        live.start_in_name();
        detail::access(ui).select(ids.name, 1, 3);
        live.host.paint();
        int before = dpi_messages;
        UINT second = move(monitors[1]);
        auto* session = detail::access(ui).edit(ids.name);
        auto* field = live.host.frame().find(live.form.handles.name);
        bool state = field && session && session->text == u"測試使用者" && session->selection.anchor == 1 &&
                     session->selection.caret == 3 && detail::access(ui).focused() == ids.name && GetFocus() == live.host.window();
        // Field-part rectangles are relative to the containing placement.
        int hit_x = field ? field->bounds.x + field->field.editor.x + field->field.editor.w / 2 : 0;
        int hit_y = field ? field->bounds.y + field->field.editor.y + field->field.editor.h / 2 : 0;
        auto* hit_item = field ? live.host.frame().hit(hit_x, hit_y) : nullptr;
        bool hit = hit_item && hit_item->id == ids.name;
        unsigned expected_caret = hit ? live.host.frame().text_offset(live.form.handles.name, hit_x) : 0;
        if (hit) live.click_point({hit_x, hit_y});
        bool pointer = hit && session->selection.anchor == expected_caret && session->selection.caret == expected_caret;
        report.check("D01-selection-forward", first != second && dpi_messages > before && state && hit && pointer,
                     narrow(std::wstring(monitors[0].szDevice)) + " " + std::to_string(first) + " -> " +
                     narrow(std::wstring(monitors[1].szDevice)) + " " + std::to_string(second) +
                     "; WM_DPICHANGED=" + std::to_string(dpi_messages - before) + "; selection/focus=" +
                     std::to_string(state) + "; field hit=" + std::to_string(hit) + "; physical click caret=" + std::to_string(pointer));

        detail::access(ui).select(ids.name, 1, 3);
        before = dpi_messages;
        UINT selection_return = move(monitors[0]);
        field = live.host.frame().find(live.form.handles.name);
        bool selection_back = field && session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005" &&
                              session->selection.anchor == 1 && session->selection.caret == 3 &&
                              detail::access(ui).focused() == ids.name && GetFocus() == live.host.window();
        hit_x = field ? field->bounds.x + field->field.editor.x + field->field.editor.w / 2 : 0;
        hit_y = field ? field->bounds.y + field->field.editor.y + field->field.editor.h / 2 : 0;
        hit_item = field ? live.host.frame().hit(hit_x, hit_y) : nullptr;
        hit = hit_item && hit_item->id == ids.name;
        expected_caret = hit ? live.host.frame().text_offset(live.form.handles.name, hit_x) : 0;
        if (hit) live.click_point({hit_x, hit_y});
        pointer = hit && session->selection.anchor == expected_caret && session->selection.caret == expected_caret;
        report.check("D01-selection-return", selection_return == first && dpi_messages > before && selection_back && hit && pointer,
                     "Return: selection/focus=" + std::to_string(selection_back) + "; physical click caret=" + std::to_string(pointer));
        detail::access(ui).select(ids.name, 1, 3);

        Profiles profiles;
        if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo could not be activated for this process");
        if (auto imc = ImmGetContext(live.host.window())) {
            DWORD conversion = 0, sentence = 0;
            ImmGetConversionStatus(imc, &conversion, &sentence);
            ImmSetConversionStatus(imc, conversion | IME_CMODE_NATIVE, sentence);
            ImmReleaseContext(live.host.window(), imc);
        }
        live.type("JI3");
        bool composing_before = session->composing && !session->preedit.empty();
        auto check_composition_move = [&](const MONITORINFOEXW& monitor, UINT expected_dpi, const char* case_name) {
            int prior = dpi_messages;
            UINT actual = move(monitor);
            field = live.host.frame().find(live.form.handles.name);
            IMECHARPOSITION position{}; position.dwSize = sizeof(position);
            bool query = SendMessageW(live.host.window(), WM_IME_REQUEST, IMR_QUERYCHARPOSITION,
                                      reinterpret_cast<LPARAM>(&position)) != 0;
            POINT caret{field ? field->caret_x(session->begin()) : 0,
                        field ? field->bounds.y + field->field.editor.y : 0};
            ClientToScreen(live.host.window(), &caret);
            CANDIDATEFORM candidate{}; bool candidate_ok = false;
            if (auto imc = ImmGetContext(live.host.window())) {
                candidate.dwIndex = 0;
                candidate_ok = ImmGetCandidateWindow(imc, 0, &candidate) && field &&
                               candidate.ptCurrentPos.x == field->caret_x(session->visible_caret()) &&
                               candidate.ptCurrentPos.y == field->bounds.y + field->field.editor.y + field->field.editor.h;
                ImmReleaseContext(live.host.window(), imc);
            }
            bool caret_ok = query && position.pt.x == caret.x && position.pt.y == caret.y;
            bool composition = composing_before && session->composing && detail::access(ui).focused() == ids.name;
            report.check(case_name, actual == expected_dpi && dpi_messages > prior && composition && caret_ok && candidate_ok,
                         "WM_DPICHANGED=" + std::to_string(dpi_messages - prior) + "; composition=" + std::to_string(composition) +
                         "; IMR caret=" + std::to_string(caret_ok) + "; candidate placement request=" + std::to_string(candidate_ok));
        };
        check_composition_move(monitors[1], second, "D01-composition-forward");
        check_composition_move(monitors[0], first, "D01-composition-return");
        live.clear_composition();
        profiles.us();
        live.form.handles.region.focus();
        live.tap(VK_RETURN);
        live.host.paint();
        before = dpi_messages;
        UINT popup_dpi = move(monitors[1]);
        const auto& frame = live.host.frame();
        bool popup = !frame.overlays.empty();
        if (popup) {
            const auto& overlay = frame.overlays.front();
            auto r = overlay.bounds;
            auto* target = frame.hit(r.x + r.w / 2, r.y + r.h / 2);
            popup = r.x >= 0 && r.y >= 0 && r.x + r.w <= frame.viewport.w && r.y + r.h <= frame.viewport.h &&
                    target && target->popup;
        }
        report.check("D01-popup-forward", popup_dpi == second && dpi_messages > before && popup,
                     "Open dropdown across monitor move: WM_DPICHANGED=" + std::to_string(dpi_messages - before) +
                     "; popup inside viewport and hit=" + std::to_string(popup));
        before = dpi_messages;
        UINT popup_return = move(monitors[0]);
        const auto& returned_frame = live.host.frame();
        bool popup_back = !returned_frame.overlays.empty();
        if (popup_back) {
            auto r = returned_frame.overlays.front().bounds;
            auto* target = returned_frame.hit(r.x + r.w / 2, r.y + r.h / 2);
            popup_back = r.x >= 0 && r.y >= 0 && r.x + r.w <= returned_frame.viewport.w &&
                         r.y + r.h <= returned_frame.viewport.h && target && target->popup;
        }
        report.check("D01-popup-return", popup_return == first && dpi_messages > before && popup_back,
                     "Return: popup inside viewport and hit=" + std::to_string(popup_back));
        return true;
    } catch (const Blocked& blocked) {
        report.check("BLOCKED", false, blocked.what());
        return false;
    }
}
}
int run_dpi(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "dpi.tsv");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        if (!dpi_session(*appearance, report)) break;
    }
    CoUninitialize();
    std::cout << report.checks << " real DPI checks, " << report.failures << " not passing\n";
    return report.failures ? 1 : 0;
}

int run(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "host.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        std::string tag = appearance == &web_appearance() ? "web" : "material";
        report.appearance = tag;
        tabs_native(*appearance, report, folder);
        progress_native(*appearance, report, folder);
        dialog_native(*appearance, report, folder);
        tooltip_native(*appearance, report, folder);
        list_native(*appearance, report);
        nested_list_native(*appearance, report);
        registered_component_uses_native_host(*appearance, report, folder);
        checkbox_and_radio_native(*appearance, report);
        slider_native(*appearance, report);
        dropdown_native(*appearance, report);
        space_activation_does_not_leak(*appearance, report);
        enter_activates_once(*appearance, report);
        pointer_activation_rules(*appearance, report);
        pointer_selection_and_clipboard(*appearance, report);
        clipboard_contention(*appearance, report);
        tab_walk_and_visible_focus(*appearance, report, folder, tag);
        resize_first_frame_matches_next(*appearance, report, 1.f);
        resize_first_frame_matches_next(*appearance, report, 1.5f);
        layout_matrix(*appearance, report, folder, tag);
        page_size_scale_matrix(*appearance, report);
        popup_dismissal_matrix(*appearance, report);
        semantic_raster_samples(*appearance, report, folder);
        x04_name_read_only_matrix(*appearance, report);
        x04_name_conflict_matrix(*appearance, report);
        semantic_observation_sweep(*appearance, folder, tag);
    }
    std::cout << report.checks << " native checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int run_x02(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x02.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        popup_dismissal_matrix(*appearance, report);
    }
    std::cout << report.checks << " X02 native checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int run_x04(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        semantic_raster_samples(*appearance, report, folder);
        x04_name_read_only_matrix(*appearance, report);
        x04_name_conflict_matrix(*appearance, report);
        semantic_observation_sweep(*appearance, folder, report.appearance);
    }
    std::cout << report.checks << " X04 raster checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

// The same AC08 scenario with fresh windows, without the unrelated layout matrix.
int run_clipboard(const std::filesystem::path& folder, int repeat) {
    std::filesystem::create_directories(folder);
    Report report(folder / "clipboard.tsv");
    for (int i = 0; i < repeat; ++i) {
        for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
            report.appearance = (appearance == &web_appearance() ? "web-" : "material-") + std::to_string(i + 1);
            pointer_selection_and_clipboard(*appearance, report);
            clipboard_contention(*appearance, report);
        }
    }
    std::cout << report.checks << " native clipboard checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

LONG WINAPI report_crash(EXCEPTION_POINTERS* info) {
    auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::cerr << "CRASH code=0x" << std::hex << info->ExceptionRecord->ExceptionCode << " module offset=0x" << (address - base) << '\n';
    return EXCEPTION_EXECUTE_HANDLER;
}

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    SetUnhandledExceptionFilter(report_crash);
    try {
        std::filesystem::path evidence = "host-evidence";
        bool ime = false, clipboard_only = false, dpi = false, x02_only = false, x04_only = false;
        int repeat = 1;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--evidence" && i + 1 < argc) evidence = argv[++i];
            else if (arg == L"--ime") ime = true;
            else if (arg == L"--dpi") dpi = true;
            else if (arg == L"--clipboard-only") clipboard_only = true;
            else if (arg == L"--x02-only") x02_only = true;
            else if (arg == L"--x04-only") x04_only = true;
            else if (arg == L"--repeat" && i + 1 < argc) repeat = std::stoi(argv[++i]);
            else throw std::runtime_error("Usage: lcars_host_verify [--ime | --dpi | --x02-only | --x04-only | --clipboard-only [--repeat N]] [--evidence DIR]");
        }
        if (repeat < 1 || (repeat != 1 && !clipboard_only) || int(ime) + int(clipboard_only) + int(dpi) + int(x02_only) + int(x04_only) > 1)
            throw std::runtime_error("Invalid verification options");
        return ime ? run_ime(evidence) : dpi ? run_dpi(evidence) : clipboard_only ? run_clipboard(evidence, repeat) : x02_only ? run_x02(evidence) : x04_only ? run_x04(evidence) : run(evidence);
    } catch (const std::exception& e) {
        std::cerr << "host verification error: " << e.what() << '\n';
        return 1;
    }
}
