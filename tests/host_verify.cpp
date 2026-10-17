// Native verification of the V01 form in a real Win32 window for both appearances.
// Keyboard and mouse cases are posted to the window queue and pumped through the host's own
// TranslateMessage/DispatchMessage path; layout evidence is rasterized offscreen.
// --ime additionally drives the real Microsoft Bopomofo IME with SendInput (foreground needed).
#include "desktop_host.hpp"
#include "edit_form.hpp"
#include <imm.h>
#include <msctf.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

using namespace lcars;
using lcars::detail::DesktopHost;
using lcars::detail::Canvas;

namespace {
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
    explicit Fixture(const Appearance& appearance)
        : host(form.ui, appearance, [this] { return form.page(); }, form.scroll, options(), true, true), ids(form.ids()) {
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
        auto r = host.frame().find(id)->bounds;
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
    void mouse(UINT msg, POINT p, WPARAM w = 0) { post(msg, w, MAKELPARAM(p.x, p.y)); host.pump(); }
    int history() {  // undo depth, restored afterwards
        int n = 0;
        while (form.ui.undo(ids.name)) ++n;
        for (int i = 0; i < n; ++i) form.ui.redo(ids.name);
        return n;
    }
};

void space_activation_does_not_leak(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    ui.focus(f.ids.error);
    f.host.trace().clear();
    f.key(VK_SPACE);
    f.key(VK_SPACE, true);
    f.key(VK_SPACE, true);
    f.key_up(VK_SPACE);
    bool moved = ui.focused() == f.ids.name && ui.text(f.ids.name).empty();
    int chars = 0;
    for (const auto& line : f.host.trace()) if (line.starts_with(L"WM_CHAR\t32\t")) ++chars;
    std::string trace;
    for (const auto& line : f.host.trace()) trace += narrow(line) + " | ";
    int history_after_activation = f.history();
    f.tap(VK_SPACE);
    bool typed = ui.text(f.ids.name) == u" " && f.history() == history_after_activation + 1;
    report.check("AC07-R01", moved && chars == 3 && history_after_activation == 1 && typed,
                 "translated WM_CHAR spaces=" + std::to_string(chars) + " delivered while the button held focus; name length after activation=" +
                 std::to_string(ui.text(f.ids.name).size() - 1) + "; history entries after activation=" + std::to_string(history_after_activation) +
                 " (the error fixture's blanking only); typing space afterwards=" + (typed ? "inserted once" : "WRONG") + "; trace: " + trace);
}

void enter_activates_once(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.ui.focus(f.ids.apply);
    f.key(VK_RETURN);
    f.key(VK_RETURN, true);
    f.key_up(VK_RETURN);
    f.form.ui.focus(f.ids.name);
    f.tap(VK_RETURN);
    report.check("AC06-enter", f.form.applies() == 1 && f.form.ui.text(f.ids.name) == u"測試使用者",
                 "Enter press + auto-repeat on apply -> applies=" + std::to_string(f.form.applies()) + "; Enter in the name field adds nothing");
}

void pointer_activation_rules(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
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
    f.form.ui.focus(f.ids.apply);
    f.key(VK_SPACE);
    SendMessageW(f.host.window(), WM_KILLFOCUS, 0, 0);
    f.key_up(VK_SPACE);
    int deactivated = f.form.applies();
    f.form.ui.invoke(f.ids.disabled);
    f.settle();
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, apply);
    report.check("AC06-F10-N02", moved_out == 0 && clicked == 1 && canceled == 1 && deactivated == 1 && f.form.applies() == 1,
                 "press+move out+release=" + std::to_string(moved_out) + ", click=" + std::to_string(clicked) +
                 ", press+WM_CANCELMODE+release=" + std::to_string(canceled) + ", Space+WM_KILLFOCUS+release=" + std::to_string(deactivated) +
                 ", disabled click total=" + std::to_string(f.form.applies()));
    // AC10: with the data area disabled, the native pointer and keyboard cannot reach the name either.
    auto before = f.form.ui.text(f.ids.name);
    f.mouse(WM_LBUTTONDOWN, f.center(f.ids.name), MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, f.center(f.ids.name));
    f.tap('X');
    report.check("AC10-native-disabled", f.form.ui.focused() != f.ids.name && f.form.ui.text(f.ids.name) == before,
                 "click on the disabled name then typing: focus=" + f.form.ui.focused() + ", text unchanged=" +
                     (f.form.ui.text(f.ids.name) == before ? "yes" : "NO"));
}

void pointer_selection_and_clipboard(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    ui.set_value(f.ids.name, u"Alpha 中文");
    f.host.paint();
    const Placement field = *f.host.frame().find(f.ids.name);  // a copy: repaints replace the frame
    auto x_of = [&](unsigned offset) {
        return field.bounds.x + field.field.editor.x + static_cast<int>(field.run.x(offset) - field.offset);
    };
    int y = field.bounds.y + field.field.editor.y + field.field.editor.h / 2;
    f.mouse(WM_LBUTTONDOWN, {x_of(5), y}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {x_of(5), y});
    auto placed = ui.edit(f.ids.name)->selection;
    f.mouse(WM_LBUTTONDOWN, {x_of(8), y}, MK_LBUTTON | MK_SHIFT);
    f.mouse(WM_LBUTTONUP, {x_of(8), y});
    auto extended = ui.edit(f.ids.name)->selection;
    f.mouse(WM_LBUTTONDOWN, {x_of(0), y}, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, {x_of(5), y}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {x_of(5), y});
    auto dragged = ui.edit(f.ids.name)->selection;
    detail::WindowsClipboard clipboard(f.host.window());
    auto saved = clipboard.read();
    f.chord('C');
    auto copied = clipboard.read();
    clipboard.write(u"X\r\nY");
    f.chord('A');
    f.chord('V');
    auto pasted = ui.text(f.ids.name);
    f.chord('Z');
    auto undone = ui.text(f.ids.name);
    if (saved) clipboard.write(*saved);
    report.check("AC08-pointer-clipboard",
                 placed.anchor == 5 && placed.caret == 5 && extended.anchor == 5 && extended.caret == 8 && dragged.anchor == 0 &&
                     dragged.caret == 5 && copied == u"Alpha" && pasted == u"X Y" && undone == u"Alpha 中文",
                 "click caret=" + std::to_string(placed.caret) + ", shift-click=" + std::to_string(extended.anchor) + ".." +
                     std::to_string(extended.caret) + ", drag=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) +
                     ", Ctrl+C clipboard='" + narrow(copied.value_or(u"<none>")) + "', Ctrl+A Ctrl+V of CRLF -> '" + narrow(pasted) +
                     "', Ctrl+Z -> '" + narrow(undone) + "'; user clipboard text restored=" + (saved ? "yes" : "no text saved"));
}

void tab_walk_and_visible_focus(const Appearance& appearance, Report& report, const std::filesystem::path& folder, const std::string& tag) {
    Fixture f(appearance);
    std::vector<Id> seen;
    for (int i = 0; i < 9; ++i) { f.tap(VK_TAB); seen.push_back(f.form.ui.focused()); }
    BYTE state[256]{};
    GetKeyboardState(state);
    state[VK_SHIFT] = 0x80;
    SetKeyboardState(state);
    f.tap(VK_TAB);
    state[VK_SHIFT] = 0;
    SetKeyboardState(state);
    bool back = f.form.ui.focused() == f.ids.apply;
    Canvas pixels(1, 1, {0, 0, 0});
    auto frame = f.host.render(900, 700, 1.f, folder / (tag + "-focus-apply.bmp"), &pixels);
    auto r = frame.find(f.ids.apply)->bounds;
    auto color_at = [&](int x, int y) {
        auto i = (static_cast<std::size_t>(pixels.height - 1 - y) * pixels.width + x) * 4;
        return Color{pixels.pixels[i], pixels.pixels[i + 1], pixels.pixels[i + 2]};
    };
    auto accent = appearance.palette().accent, ring = color_at(r.x + 1, r.y + r.h / 2);
    bool visible = ring.r == accent.r && ring.g == accent.g && ring.b == accent.b;
    auto other = frame.find(f.ids.reset)->bounds;
    auto plain = color_at(other.x + 1, other.y + other.h / 2);
    bool distinct = !(plain.r == accent.r && plain.g == accent.g && plain.b == accent.b);
    std::string order;
    for (const auto& id : seen) order += id + " ";
    bool wraps = seen.size() == 9 && seen[6] == f.ids.name && seen[7] == f.ids.apply && seen[8] == seen[0];
    report.check("AC11-tab", wraps && back && visible && distinct,
                 "Tab order: " + order + "; Shift+Tab back to apply=" + (back ? "yes" : "no") + "; focused apply ring pixel is accent=" +
                     (visible ? "yes" : "NO") + ", unfocused reset ring is not=" + (distinct ? "yes" : "NO") + " (" + tag + "-focus-apply.bmp)");
}

void resize_first_frame_matches_next(const Appearance& appearance, Report& report, float scale) {
    Fixture f(appearance);
    f.host.force_scale(scale);
    f.form.ui.invoke(f.ids.long_text);
    f.host.client_size(640, 480);
    f.host.pump();
    UpdateWindow(f.host.window());
    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120 * 40)), 0);
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
    auto target = first.find(f.ids.apply)->bounds;
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
                    ui.invoke(f.ids.long_text);
                    ui.invoke(f.ids.error);
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
                auto field = frame.find(f.ids.name)->bounds, apply = frame.find(f.ids.apply)->bounds;
                auto draft = frame.find(f.ids.draft)->bounds;
                bool arrangement = size.w == 960 ? draft.x > field.x && draft.y < apply.y + apply.h : draft.x == field.x && draft.y > apply.y;
                // Every keyboard entry can be scrolled fully into the viewport.
                int reachable = 0, entries = 0;
                for (const auto& id : ui.focus_order()) {
                    ScrollState probe;
                    auto revealed = f.host.layout(w, h, scale, probe, id);
                    auto b = revealed.find(id)->bounds;
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
    edit_form::Form form;
    DesktopHost host;
    const edit_form::Ids& ids;
    HWND helper = nullptr;
    std::size_t logged = 0;
    explicit Live(const Appearance& appearance)
        : host(form.ui, appearance, [this] { return form.page(); }, form.scroll, Fixture::options(), true, false), ids(form.ids()) {
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
    void click(std::string_view id, int x_bias = 0) {
        guard();
        auto r = host.frame().find(id)->bounds;
        POINT p{r.x + r.w / 2 + x_bias, r.y + r.h / 2};
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
    int history() {
        auto& ui = form.ui;
        int n = 0;
        while (ui.undo(ids.name)) ++n;
        for (int i = 0; i < n; ++i) ui.redo(ids.name);
        return n;
    }
    std::string state() {
        const auto* s = form.ui.edit(ids.name);
        return "draft='" + narrow(s->text) + "' composing=" + (s->composing ? "1" : "0") + " preedit='" + narrow(s->preedit) +
               "' focus=" + form.ui.focused() + " history=" + std::to_string(history()) + " applies=" + std::to_string(form.applies());
    }
    std::string events() {
        std::string out;
        auto& log = host.ime_log();
        for (; logged < log.size(); ++logged) out += narrow(log[logged]) + " | ";
        return out;
    }
    void clear_composition() {
        for (int i = 0; i < 3 && form.ui.edit(ids.name)->composing; ++i) tap(VK_ESCAPE);
        wait(150);
        if (form.ui.edit(ids.name)->composing) throw Blocked("a composition survived three Escape presses");
    }
    void start_in_name() {
        clear_composition();
        form.ui.invoke(ids.reset);
        form.ui.focus(ids.name);
        form.ui.select(ids.name, 5, 5);
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

void ime_session(const Appearance& appearance, Report& report, std::ofstream& events) {
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
        ui.focus(ids.error);
        live.wait(100);
        live.send(VK_SPACE, false);
        live.send(VK_SPACE, false);  // held: the second press arrives as an auto-repeat
        live.send(VK_SPACE, true);
        live.wait(150);
        bool moved = ui.focused() == ids.name && ui.text(ids.name).empty() && live.history() == 1;
        record("AC07-real", moved, "Space press, repeat and release on the error fixture: focus to name, no space leaked, one history entry");
        live.tap(VK_SPACE);
        record("AC07-real-typing", ui.text(ids.name) == u" " && live.history() == 2, "A real Space typed afterwards inserts one space");

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
        auto* session = ui.edit(ids.name);
        bool composing = session->composing && session->text == u"測試使用者" && !session->preedit.empty();
        record("F02-compose", composing, "ㄨㄛˇ updates the preedit only; committed text unchanged");
        live.tap(VK_RETURN);
        live.wait(200);
        record("F02-confirm", ui.text(ids.name) == u"測試使用者我" && !session->composing && live.history() == 1 && live.form.applies() == 0,
               "Enter confirms one character once, one history entry, no apply");
        live.tap(VK_RETURN);
        record("F02-enter-after", live.form.applies() == 0 && ui.text(ids.name) == u"測試使用者我", "A second Enter does not apply");
        live.chord(VK_CONTROL, 'Z');
        record("AC09-undo-commit", ui.text(ids.name) == u"測試使用者", "Ctrl+Z removes the whole confirmation in one step");

        live.type("JI3");
        live.tap(VK_ESCAPE);
        live.wait(200);
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(200); }
        bool redo_kept = !session->composing && ui.text(ids.name) == u"測試使用者" && live.form.ui.describe(ids.name)->operations.back() == Operation::redo;
        record("E01-cancel", redo_kept, "Escape cancels the composition; committed text and the redo position survive");

        live.start_in_name();
        live.chord(VK_CONTROL, 'A');
        live.type("JI3");
        live.tap(VK_RETURN);
        live.wait(200);
        bool replaced = ui.text(ids.name) == u"我" && live.history() == 1;
        live.chord(VK_CONTROL, 'Z');
        record("E01-replace-selection", replaced && ui.text(ids.name) == u"測試使用者", "Confirmation replaces the selection as one undo entry");

        live.start_in_name();
        live.type("JI3");
        live.chord(VK_CONTROL, 'Z');
        live.wait(200);
        bool untouched = ui.text(ids.name) == u"測試使用者" && live.history() == 0;
        record("E01-shortcut-while-composing", untouched, "Ctrl+Z during composition goes to the IME and leaves committed history alone");
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(150); }
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(150); }

        live.start_in_name();
        live.type("JI3");
        live.tap(VK_TAB);
        live.wait(300);
        record("E01-tab", session->composing && ui.focused() == ids.name && ui.text(ids.name) == u"測試使用者" && live.form.applies() == 0,
               "Tab during composition is consumed by the IME: preedit kept, focus stays, nothing committed or applied");
        live.clear_composition();
        record("E01-tab-then-escape", ui.text(ids.name) == u"測試使用者" && live.history() == 0, "Escape then discards that preedit cleanly");

        live.start_in_name();
        live.type("JI3");
        auto before = live.state();
        live.click(ids.long_text);
        record("E01-pointer", !session->composing && ui.text(ids.name) == u"測試使用者我" && live.history() == 1 && ui.focused() == ids.long_text && live.form.applies() == 0,
               "Clicking another control commits the preedit once, then focus moves; before: " + before);

        live.start_in_name();
        live.type("JI3");
        before = live.state();
        SetForegroundWindow(live.helper);
        live.wait(500);
        auto away = live.state();
        report.check("E01-window-deactivate", !session->composing && ui.text(ids.name) == u"測試使用者我" && live.history() == 1 && live.form.applies() == 0,
                     "The IME commits on native deactivation, once; before: " + before + " => " + away);
        events << "E01-window-deactivate\t" << live.events() << '\n';
        live.foreground(live.host.window());
        record("E01-window-return", !session->composing && ui.focused() == ids.name && ui.text(ids.name) == u"測試使用者我" && live.history() == 1,
               "Back in the window: focus kept on the name, nothing composing, the commit kept");

        live.start_in_name();
        live.type("JI3");
        before = live.state();
        live.click(ids.name, -100);
        record("E01-pointer-same-field", !session->composing && ui.text(ids.name) == u"測試使用者我" && live.history() == 1 && ui.focused() == ids.name && live.form.applies() == 0,
               "Clicking inside the composing field commits once, then places the caret; before: " + before);

        profiles.us();
        live.wait(200);
    } catch (const Blocked& blocked) {
        report.check("BLOCKED", false, blocked.what());
        events << "BLOCKED\t" << blocked.what() << '\n';
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
        ime_session(*appearance, report, events);
    }
    CoUninitialize();
    std::cout << report.checks << " real-input checks, " << report.failures << " not passing\n";
    return report.failures ? 1 : 0;
}

int run(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "host.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        std::string tag = appearance == &web_appearance() ? "web" : "material";
        report.appearance = tag;
        space_activation_does_not_leak(*appearance, report);
        enter_activates_once(*appearance, report);
        pointer_activation_rules(*appearance, report);
        pointer_selection_and_clipboard(*appearance, report);
        tab_walk_and_visible_focus(*appearance, report, folder, tag);
        resize_first_frame_matches_next(*appearance, report, 1.f);
        resize_first_frame_matches_next(*appearance, report, 1.5f);
        layout_matrix(*appearance, report, folder, tag);
    }
    std::cout << report.checks << " native checks, " << report.failures << " failures\n";
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
        bool ime = false;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--evidence" && i + 1 < argc) evidence = argv[++i];
            else if (arg == L"--ime") ime = true;
            else throw std::runtime_error("Usage: lcars_host_verify [--ime] [--evidence DIR]");
        }
        return ime ? run_ime(evidence) : run(evidence);
    } catch (const std::exception& e) {
        std::cerr << "host verification error: " << e.what() << '\n';
        return 1;
    }
}
