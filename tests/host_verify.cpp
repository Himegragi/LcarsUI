#include "real_input_profiles.hpp"
#include "presentation_matrix.hpp"
#include "settings_demo.hpp"
#include "core_internal.hpp"
// Native verification of the V01 form in a real Win32 window for both appearances.
// Keyboard and mouse cases are posted to the window queue and pumped through the host's own
// TranslateMessage/DispatchMessage path; layout evidence is rasterized offscreen.
// --ime additionally drives the real Microsoft Bopomofo IME with SendInput (foreground needed).
#include "desktop_host.hpp"
#include "test_component.hpp"
#include "edit_form.hpp"
#include "x04_drivers.hpp"
#include "geometry.hpp"
#include "list_page.hpp"
#include <imm.h>
#include <psapi.h>
#include <msctf.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

using namespace lcars;
using lcars::detail::DesktopHost;
using lcars::detail::Image;

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
    void open(const char* id, const std::string& evidence) {
        out << appearance << '\t' << id << "\tOPEN\t" << evidence << '\n';
        std::cout << "OPEN " << appearance << ' ' << id << ' ' << evidence << '\n';
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
// X04 samples must have their requested physical client size even when the test
// desktop's default maximum tracking size is smaller. Keep the host's normal input
// procedure and alter only this fixture window's size limits.
std::map<HWND, WNDPROC> x04_original_procs;
LRESULT CALLBACK x04_window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto found = x04_original_procs.find(window);
    if (found == x04_original_procs.end()) return DefWindowProcW(window, message, w, l);
    auto result = CallWindowProcW(found->second, window, message, w, l);
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l);
        limits->ptMaxSize = {32767, 32767};
        limits->ptMaxTrackSize = {32767, 32767};
    }
    return result;
}
// One fresh form plus host per scenario, so cases never depend on each other's state.
struct Fixture {
    edit_form::Form form;
    DesktopHost host;
    const edit_form::Ids& ids;
    WNDPROC x04_original_proc = nullptr;
    explicit Fixture(const Appearance& appearance, bool pages = false, bool alternate = false)
        : form(alternate ? alternate_ids() : edit_form::default_ids(), pages), host(form.ui, appearance, [this] { return form.page(); }, form.scroll, options(), true, true), ids(form.ids()) {
        ShowWindow(host.window(), SW_SHOWNOACTIVATE);
        host.force_scale(1.f);
        host.client_size(900, 700);
        settle();
    }
    ~Fixture() {
        if (x04_original_proc) {
            SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(x04_original_proc));
            x04_original_procs.erase(host.window());
        }
    }
    void x04_size(int width_dip, int height_dip, float scale) {
        if (!x04_original_proc) {
            auto previous = GetWindowLongPtrW(host.window(), GWLP_WNDPROC);
            if (!previous) throw std::runtime_error("Cannot read X04 window procedure");
            x04_original_proc = reinterpret_cast<WNDPROC>(previous);
            x04_original_procs.emplace(host.window(), x04_original_proc);
            if (!SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&x04_window_proc))) {
                x04_original_procs.erase(host.window()); x04_original_proc = nullptr;
                throw std::runtime_error("Cannot install X04 sizing fixture");
            }
        }
        host.force_scale(scale); host.client_size(width_dip, height_dip);
        RECT actual{}; GetClientRect(host.window(), &actual);
        int expected_width = int(std::lround(width_dip * scale));
        int expected_height = int(std::lround(height_dip * scale));
        if (actual.right != expected_width || actual.bottom != expected_height)
            throw std::runtime_error("X04 client size mismatch: expected " + std::to_string(expected_width) + "x" +
                std::to_string(expected_height) + ", got " + std::to_string(actual.right) + "x" + std::to_string(actual.bottom));
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

void atlas_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    f.host.paint();
    auto before = f.host.rasterizations();
    for (int i=0; i<100; ++i) f.host.paint();
    report.check("GA01", f.host.rasterizations()==before, "100 repeated frames reuse rasterized glyphs");
    auto clears=f.host.atlas_clears();
    f.host.force_scale(1.5f); f.host.paint();
    bool forward=f.host.atlas_clears()==clears+1 && f.host.compare_legacy(960,720,1.5f);
    f.host.force_scale(1.f); f.host.paint();
    report.check("GA02", forward && f.host.atlas_clears()==clears+2 && f.host.compare_legacy(960,720,1.f), "scale changes clear once and retain S1 pixels");
    f.host.configure_atlas(64,1);
    bool same=f.host.compare_legacy(960,720,1.f);
    report.check("GA03-CPU", same && f.host.atlas_batches()>0 && f.host.atlas_peak_pages()<=1, "small atlas batches retain S1 pixels within one page");
    f.host.configure_atlas(1024,4);
    auto settings=f.form.ui.presentation(); settings.text_size=18;
    f.form.ui.set_presentation(settings); f.host.paint();
    auto previous=f.host.atlas_clears(); settings.text_size=24;
    bool changed=bool(f.form.ui.set_presentation(settings)); f.host.paint();
    bool invalidated=f.host.atlas_clears()==previous+1 && f.host.compare_legacy(960,720,1.f);
    detail::Text text;
    auto old_layout=text.shape(u"Station",18);
    detail::RecordingCanvas old_canvas(300,80,{0,0,0});
    text.draw(old_canvas,old_layout,0,40,{255,255,255});
    auto count=text.atlas_clears(); text.clear_atlas();
    auto new_layout=text.shape(u"Station",24);
    detail::RecordingCanvas new_canvas(300,80,{0,0,0});
    text.draw(new_canvas,new_layout,0,40,{255,255,255});
    detail::Canvas reference(300,80,{0,0,0});
    text.draw(reference,new_layout,0,40,{255,255,255});
    report.check("GA04", changed && invalidated && text.atlas_clears()==count+1 &&
        detail::render_cpu(new_canvas.list,300,80).pixels==reference.pixels &&
        detail::render_cpu(old_canvas.list,300,80).pixels!=reference.pixels,
        "font-size invalidation produces the 24 DIP reference");
}

void presentation_before_input(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.name.focus(); h.name.select(1, 1); f.settle();
    auto settings = f.form.ui.presentation(); settings.text_size = 28;
    auto result = f.form.ui.set_presentation(settings);
    auto* field = f.host.frame().find(h.name);
    IMECHARPOSITION position{}; position.dwSize = sizeof(position);
    bool query = SendMessageW(f.host.window(), WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&position)) != 0;
    POINT expected{field ? field->caret_x(1) : 0, field ? field->bounds.y + field->field.editor.y : 0};
    ClientToScreen(f.host.window(), &expected);
    bool geometry = field && field->text_size == 28 && query && position.pt.x == expected.x && position.pt.y == expected.y;
    int x = field ? field->caret_x(2) : 0;
    int y = field ? field->bounds.y + field->field.editor.y + field->field.editor.h / 2 : 0;
    SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    auto* session = detail::access(f.form.ui).edit(f.ids.name);
    report.check("PS09", bool(result) && geometry && session && session->selection.caret == 2,
                 "IME query and pointer use updated layout before paint");
}

void write_comparison_bmp(const std::filesystem::path& path, const Image& image) {
    auto pixels=image.pixels;
    for(std::size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    info.biSize=sizeof(info);info.biWidth=image.width;info.biHeight=-image.height;info.biPlanes=1;info.biBitCount=32;info.biSizeImage=static_cast<DWORD>(pixels.size());
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    if(!out)throw std::runtime_error("comparison image write failed");
}
void write_comparison_failure(const std::filesystem::path& folder, const std::string& name, const detail::GpuComparison& result) {
    std::filesystem::create_directories(folder);
    write_comparison_bmp(folder/(name+"-cpu.bmp"),result.reference);
    write_comparison_bmp(folder/(name+"-gpu.bmp"),result.actual);
    Image diff{result.reference.width,result.reference.height,std::vector<unsigned char>(result.reference.pixels.size(),255)};
    for(std::size_t i=0;i<diff.pixels.size();i+=4){
        int delta=0;for(int c=0;c<4;++c)delta=std::max(delta,std::abs(int(result.reference.pixels[i+c])-int(result.actual.pixels[i+c])));
        diff.pixels[i]=static_cast<unsigned char>(delta);diff.pixels[i+1]=0;diff.pixels[i+2]=0;
    }
    write_comparison_bmp(folder/(name+"-diff.bmp"),diff);
}

void emit_failure_if_needed(const std::filesystem::path& folder, const std::string& name, const detail::GpuComparison& result, bool passed) {
    if(!passed)write_comparison_failure(folder,name,result);
}

// The same recorded frame must be executed by the graphics backend offscreen.
void gpu_draw_list_smoke(const Appearance& appearance, Report& report, const std::filesystem::path& evidence) {
    Fixture f(appearance, true);
    f.host.force_scale(1.f);
    f.settle();
    f.form.handles.name.focus();
    report.check("GPU-draw-list", bool(f.host.compare_gpu(960, 720, 1.f)),
                 "offscreen GPU execution agrees with the CPU reference");
    auto dark = f.form.ui.presentation(); dark.variant = ThemeVariant::dark();
    bool accepted = bool(f.form.ui.set_presentation(dark));
    auto dark_image = f.host.compare_gpu(960, 720, 1.f);
    auto background = appearance.palette(dark.variant).background;
    report.check("GPU-dark-background", accepted && bool(dark_image) &&
        dark_image.reference.pixels[0] == background.r && dark_image.reference.pixels[1] == background.g &&
        dark_image.reference.pixels[2] == background.b, "comparison uses the selected dark background");
    auto shifted=f.host.compare_gpu(960,720,1.f,1);
    auto missing=f.host.compare_gpu(960,720,1.f,2);
    report.check("GC03-caret", !shifted && (shifted.failed_roles & 2), "one-pixel caret shift is detected in the caret region");
    report.check("GC03-focus", !missing && (missing.failed_roles & 1), "missing focus indicator is detected in the focus region");
    auto key=report.appearance+"-injected";
    emit_failure_if_needed(evidence,key,shifted,bool(shifted));
    bool images=std::filesystem::exists(evidence/(key+"-cpu.bmp")) && std::filesystem::exists(evidence/(key+"-gpu.bmp")) && std::filesystem::exists(evidence/(key+"-diff.bmp"));
    report.check("GC05-images", images, "failed comparison writes reference, GPU and difference images");
    f.host.configure_atlas(64,1);
    report.check("GA03-GPU", bool(f.host.compare_gpu(960,720,1.f)) && f.host.atlas_peak_pages()<=1,
                 "bounded atlas segmentation preserves GPU output");
}

// #78: the desktop host paints with the Ui's palette overrides, and the semantic layer reads the same colors.
void palette_override_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    f.host.force_scale(1.f);
    f.settle();
    auto original = appearance.palette().background;
    const Color changed{240, 240, 240};
    auto first_pixel = [](const auto& image) { return Color{image.reference.pixels[0], image.reference.pixels[1], image.reference.pixels[2]}; };
    auto before = f.host.compare_gpu(960, 720, 1.f);
    report.check("PO-before", bool(before) && first_pixel(before) == original, "built-in background paints before any override");
    auto result = f.form.ui.set_palette_overrides({{std::string(appearance.name()), ThemeVariant::light(), "background", changed}});
    auto after = f.host.compare_gpu(960, 720, 1.f);
    report.check("PO-override", bool(result) && bool(after) && first_pixel(after) == changed, "the next host frame paints the overridden background and the GPU agrees");
    auto semantic = f.form.ui.semantics().effective_palette();
    report.check("PO-semantic", semantic && semantic->background == changed, "the host binds its appearance so the semantic layer reads the overridden palette");
    report.check("PO-clear", bool(f.form.ui.set_palette_overrides({})) && first_pixel(f.host.compare_gpu(960, 720, 1.f)) == original, "clearing the list restores the built-in background");
}

std::string windows_build() {
    using QueryVersion=LONG (WINAPI*)(OSVERSIONINFOW*);
    auto proc=reinterpret_cast<QueryVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
    return proc && proc(&version)==0 ? std::to_string(version.dwBuildNumber) : "unknown";
}
void draw_list_initial(const Appearance& appearance, Report& report, std::ofstream* gpu_tsv = nullptr, const std::filesystem::path& evidence = {}) {
    const char* names[] = {"v01-a","v01-b","v01-c","v01-d","v01-e","v02-a","v02-b","v02-c","v03-a","v03-b"};
    for (int scenario=0; scenario<10; ++scenario) for (float scale : {1.f,1.5f}) {
        Fixture f(appearance, true);
        f.host.force_scale(scale);
        f.host.client_size(scale == 1.f ? 960 : 640, scale == 1.f ? 720 : 480);
        f.settle();
        auto& h=f.form.handles; auto& state=detail::access(f.form.ui);
        bool setup=true;
        if(scenario<5) {
            setup=bool(h.name.focus());
            if(scenario==1) setup=setup && bool(state.select(f.ids.name,0,2));
            if(scenario==2) setup=setup && bool(state.composition_update(u"\u3118\u311c\u02cb",3));
            if(scenario==3) { setup=setup && bool(h.name.set_value(u"")); h.apply.invoke(); setup=setup && h.name.describe()->invalid; }
            if(scenario==4) { setup=bool(h.region.focus()) && bool(h.region.open()); f.tap(VK_DOWN); f.tap(VK_DOWN); setup=setup && h.region.describe()->candidate==OptionValue{"south"}; }
        } else if(scenario<8) {
            setup=bool(h.tabs.activate({"v02"})); auto& list=*f.form.list;
            state.set_system_keyboard_cues(true);  // indicator shown, as after a key press; programmatic focus alone does not show it
            if(scenario==5) setup=setup && bool(list.handles.twenty.invoke()) && bool(list.handles.list.select({"T003"})) && bool(list.handles.list.focus());
            if(scenario==6) { setup=setup && bool(list.handles.hundred.invoke()) && bool(list.handles.list.focus()); f.tap(VK_END); setup=setup && list.selection()==OptionValue{"T100"} && list.handles.list.describe()->candidate==OptionValue{"T100"}; }
            if(scenario==7) setup=setup && bool(list.handles.zero.invoke());
        } else {
            setup=bool(h.tabs.activate({"v03"}));
            if(scenario==8) { std::uint64_t now=GetTickCount64(); state.tooltip_clock([&]{return now;}); SendMessageW(f.host.window(),WM_SETFOCUS,0,0); setup=setup && bool(h.progress_half.focus()); f.tap(VK_RIGHT); f.settle(); now+=600; state.tooltip_time(now); setup=setup && *h.progress_tip.visible(); state.tooltip_clock({}); }
            if(scenario==9) { state.set_system_keyboard_cues(true); setup=setup && bool(h.dialog_open.invoke()) && *h.dialog.is_open(); }
        }
        int width=960,height=720;
        if(scenario==6 && setup) {
            auto frame=f.host.layout(width,height,scale,f.form.scroll,{});
            auto item=frame.find(f.form.list->handles.list);
            setup=item && !item->options.empty() && intersect(item->options.back(),item->clip)==item->options.back();
        }
        bool matched=setup && f.host.compare_legacy(width,height,scale);
        report.check("DL01", matched, std::string(names[scenario])+" scale="+std::to_string(scale)+" setup="+std::to_string(setup));
        auto comparison=f.host.compare_gpu(width,height,scale);
        const unsigned required = scenario==0 ? 3u : scenario==1 ? 4u : scenario==2 ? 10u : scenario==3 ? 48u : scenario==4 ? 5u : scenario==5 || scenario==6 ? 5u : scenario==9 ? 1u : 0u;
        auto has_role=[&](std::string_view owner, unsigned role, bool require_popup=false){
            return std::any_of(comparison.regions.begin(),comparison.regions.end(),[&](const auto& region){
                return region.owner==owner && (region.role & role) && region.passed && (!require_popup || region.popup);
            });
        };
        bool role_presence=(comparison.roles & required)==required;
        if(scenario==0)role_presence &= has_role(f.ids.name,1) && has_role(f.ids.name,2);
        if(scenario==1)role_presence &= has_role(f.ids.name,4);
        if(scenario==2)role_presence &= has_role(f.ids.name,8) && has_role(f.ids.name,2);
        if(scenario==3)role_presence &= has_role(f.ids.name,32) && (has_role(f.ids.name,16) || has_role(f.ids.name_error,16));
        if(scenario==4)role_presence &= has_role(f.ids.region,1) && has_role(f.ids.region,4,true);
        if(scenario==5 || scenario==6){auto id=f.form.list->handles.list.describe()->id;role_presence &= has_role(id,1) && has_role(id,4);}
        if(scenario==9)role_presence &= has_role(h.dialog_cancel.describe()->id,1);
        bool pass=setup && bool(comparison) && role_presence;
        report.check("GC01", pass, std::string(names[scenario])+" scale="+std::to_string(scale)+" roles="+std::to_string(comparison.roles));
        if(gpu_tsv){
            auto key=report.appearance+"-"+names[scenario]+"-"+(scale==1.f?"100":"150");
            write_comparison_bmp(evidence/(key+"-cpu.bmp"), comparison.reference);
            *gpu_tsv << names[scenario] << '\t' << report.appearance << '\t' << width << 'x' << height << '\t' << scale << '\t' << f.host.renderer_name() << '\t' << windows_build() << '\t' <<
#ifdef NDEBUG
            "Release"
#else
            "Debug"
#endif
            << '\t' << double(comparison.comparison.different_pixels)/(width*height) << '\t' << comparison.comparison.maximum_difference << '\t' << comparison.roles << '\t' << comparison.failed_roles << '\t';
            for(std::size_t i=0;i<comparison.regions.size();++i) {
                if(i)*gpu_tsv << ',';
                const auto& r=comparison.regions[i];
                *gpu_tsv << r.owner << ':' << r.role << ':' << r.popup << ':' << r.bounds.x << ':' << r.bounds.y << ':' << r.bounds.w << ':' << r.bounds.h << ':' << (r.passed?"PASS":"FAIL");
            }
            *gpu_tsv << '\t' << comparison.comparison.regions_ok << '\t' << (pass?"PASS":"FAIL") << '\n';
            emit_failure_if_needed(evidence,key,comparison,pass);
        }
    }
}


int demo_compare(const std::filesystem::path& evidence) {
    std::filesystem::create_directories(evidence);
    std::ofstream out(evidence / "demo-render-compare.tsv");
    out << "scenario\tappearance\tvariant\tsize\tscale\trenderer\twindows_build\tconfiguration\tdifferent_fraction\tmaximum_difference\troles\tfailed_roles\tregions\tstatus\n";
    int failures=0, rows=0;
    for (const auto* appearance : {&web_appearance(), &material_appearance()})
    for (bool dark : {false,true}) for (float scale : {1.f,1.5f}) for (int scenario=0; scenario<8; ++scenario) {
        settings_demo::AboutInfo info;
        info.licenses.push_back({{"lcars"},u"LcarsUI",u"License text"});
        for(int i=0;i<40;++i) { auto n=std::to_string(i); std::u16string name=u"Font "; name.append(n.begin(),n.end()); info.fonts.push_back({name,name}); }
        settings_demo::Demo demo(std::move(info));
        DesktopOptions options; options.title=u"Demo comparison";
        DesktopHost host(demo.ui,*appearance,[&]{return demo.page();},demo.scroll,options,false,true);
        ShowWindow(host.window(),SW_SHOWNOACTIVATE); host.force_scale(scale); host.client_size(scale==1.f?960:640,scale==1.f?720:480);
        SendMessageW(host.window(),WM_SETFOCUS,0,0); host.pump();
        auto settings=demo.ui.presentation(); settings.variant=dark?ThemeVariant::dark():ThemeVariant::light();
        bool setup=bool(demo.ui.set_presentation(settings));
        auto tap=[&](WPARAM key){ PostMessageW(host.window(),WM_KEYDOWN,key,key_lparam(key,false,false)); PostMessageW(host.window(),WM_KEYUP,key,key_lparam(key,false,true)); host.pump(); };
        if(scenario==2 || scenario==7) detail::access(demo.ui).set_system_keyboard_cues(true);  // indicator shown, as after a key press; programmatic focus alone does not show it
        if(scenario==0) setup &= bool(demo.name.focus());
        if(scenario==1) { setup &= bool(demo.name.focus()) && bool(demo.name.set_value(u"")); tap(VK_TAB); setup &= demo.name.describe()->invalid; }
        if(scenario==2 || scenario==3) {
            setup &= bool(demo.nav.select({"items"})) && bool(demo.items_list.select({"T003"})) && bool(demo.items_list.focus());
            if(scenario==3) setup &= bool(demo.items_edit.invoke()) && demo.item_dialog.is_open().value.value_or(false);
        }
        if(scenario==4) { setup &= bool(demo.nav.select({"appearance"})) && bool(demo.font.focus()) && bool(demo.font.open()); for(int i=0;i<30;++i) tap(VK_DOWN); auto font=demo.font.describe(); setup &= font && font->expanded && font->candidate && font->candidate->id!="default"; }
        if(scenario==5) setup &= bool(demo.nav.select({"about"})) && bool(demo.licenses.select({"lcars"}));
        if(scenario==6) { setup &= bool(demo.notices.toggle()); setup &= !demo.request_close() && demo.close_dialog.is_open().value.value_or(false); }
        if(scenario==7) setup &= bool(demo.nav.select({"about"})) && bool(demo.nav.focus());
        host.pump(); auto result=host.compare_gpu(960,720,scale);
        auto has_role=[&](std::string_view owner, unsigned role) { return std::any_of(result.regions.begin(),result.regions.end(),[&](const auto& r) { return r.owner==owner && (r.role & role) && r.passed; }); };
        bool cues=true;
        if(scenario==0) cues=has_role("general.name",1) && has_role("general.name",2);
        if(scenario==1) cues=has_role("general.name",32) && (has_role("general.name",16) || has_role("general.name.error",16));
        if(scenario==2) cues=has_role("items.list",1) && has_role("items.list",4);
        if(scenario==7) cues=has_role("app.nav",1);
        bool pass=setup && bool(result) && cues; ++rows; if(!pass) ++failures;
        std::string name="SD-"+std::string(1,char('a'+scenario));
        std::string key=name+"-"+(appearance==&web_appearance()?"web":"material")+(dark?"-dark":"-light")+(scale==1.f?"-100":"-150");
        write_comparison_bmp(evidence/(key+"-cpu.bmp"),result.reference); emit_failure_if_needed(evidence,key,result,pass);
        out << name << '\t' << (appearance==&web_appearance()?"web":"material") << '\t' << (dark?"dark":"light") << '\t' << (scale==1.f?"960x720":"640x480") << '\t' << scale << '\t' << host.renderer_name() << '\t' << windows_build() << '\t'
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << '\t' << double(result.comparison.different_pixels)/(960*720) << '\t' << result.comparison.maximum_difference << '\t' << result.roles << '\t' << result.failed_roles << '\t';
        for(const auto& region:result.regions) out << region.owner << ':' << region.role << ':' << (region.passed?"PASS":"FAIL") << ',';
        out << '\t' << (pass?"PASS":"FAIL") << '\n';
        std::cout << key << (pass?" PASS":" FAIL") << " setup=" << setup << '\n';
    }
    return out && rows==64 && !failures ? 0 : 1;
}

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
        Image pixels{1, 1, {}};
        auto path = folder / (report.appearance + (c.source == 50 ? "-v03-50.bmp" : "-v03-p07.bmp"));
        auto frame = f.host.render(960, 720, 1.f, path, &pixels);
        const auto* item = frame.find(h.progress);
        if (!item || item->progress_track.w < 4) { passed = false; continue; }
        auto track = item->progress_track;
        auto at = [&](int x) {
            auto i = (static_cast<std::size_t>((track.y + track.h / 2)) * pixels.width + x) * 4;
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
    h.progress_half.focus(); f.tap(VK_RIGHT); f.settle();  // the focus tooltip needs keyboard use first
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
            geometry = geometry && test::inside(tip->bounds, {0, 0, width, height}) && !tip->lines.empty();
            for (const auto& line : tip->lines) geometry = geometry && test::inside(line.bounds, tip->bounds);
        }
    }
    f.form.update_progress(0); f.tap(VK_SPACE);
    bool space = f.form.progress() == 50;
    report.check("P05-P06-R08-tooltip-messages", initially_hidden && shown && escaped && refocus_waits && returned && geometry && space,
        "initial=" + std::to_string(initially_hidden) + ", focus=" + std::to_string(shown) + ", escape=" + std::to_string(escaped) +
        ", refocus-delay=" + std::to_string(refocus_waits) + ", returned=" + std::to_string(returned) + ", geometry=" + std::to_string(geometry) +
        ", space=" + std::to_string(space));
}

// Native twin of the core focus-visibility cases (#72): real window messages drive the display state.
void focus_visibility_native(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& state = detail::access(f.form.ui);
    BOOL system_on = FALSE; SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &system_on, 0);
    auto shown = [&] { return f.form.ui.focus_indicator_visible(); };
    state.set_system_keyboard_cues(false);  // deterministic start whatever the desktop setting is
    bool started_hidden = !shown();
    f.tap(VK_TAB);
    bool key_shows = shown() && !state.focused().empty();
    SendMessageW(f.host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
    bool wheel_keeps = shown();
    f.mouse(WM_LBUTTONDOWN, f.center(f.ids.title), MK_LBUTTON); f.mouse(WM_LBUTTONUP, f.center(f.ids.title));
    bool inert_press_hides = !shown();  // the title label does not accept the pointer, yet the press still hides
    f.chord('S');
    bool chord_keeps_hidden = !shown();
    f.tap(VK_RIGHT);
    bool plain_key_shows = shown();
    POINT blank{-1, -1};
    f.host.paint();
    for (int y = 690; y > 0 && blank.x < 0; y -= 7) for (int x = 3; x < 900; x += 11)
        if (!f.host.frame().hit(x, y)) { blank = {x, y}; break; }
    bool found_blank = blank.x >= 0;
    if (found_blank) { f.mouse(WM_LBUTTONDOWN, blank, MK_LBUTTON); f.mouse(WM_LBUTTONUP, blank); }
    bool blank_press_hides = found_blank && !shown();
    state.set_system_keyboard_cues(!system_on);
    SendMessageW(f.host.window(), WM_SETTINGCHANGE, SPI_SETKEYBOARDCUES, 0);
    bool setting_change_rereads = shown() == bool(system_on);  // recorded state is hidden, so only the system value shows it
    report.check("F72-native-focus-visibility", started_hidden && key_shows && wheel_keeps && inert_press_hides && chord_keeps_hidden &&
                 plain_key_shows && blank_press_hides && setting_change_rereads,
                 "hidden at start=" + std::to_string(started_hidden) + ", key shows=" + std::to_string(key_shows) + ", wheel keeps=" + std::to_string(wheel_keeps) +
                 ", inert press hides=" + std::to_string(inert_press_hides) + ", Ctrl chord keeps hidden=" + std::to_string(chord_keeps_hidden) +
                 ", plain key shows=" + std::to_string(plain_key_shows) + ", blank press hides=" + std::to_string(blank_press_hides) +
                 ", WM_SETTINGCHANGE re-reads=" + std::to_string(setting_change_rereads));
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

void font_diagnostic_reaches_application(const Appearance& appearance, Report& report) {
    Ui ui;
    auto label=ui.add_label({.id="missing-font-glyph",.kind=Kind::label,.value=u"\U0010ffff"});
    std::vector<RenderDiagnostic> events;
    DesktopHost* active=nullptr; int depth=0, maximum_depth=0;
    auto initial=ui.presentation(); initial.font_family=u"LcarsUI definitely absent";
    ui.set_presentation(initial);
    DesktopOptions options;
    options.on_diagnostic=[&](const RenderDiagnostic& event) {
        ++depth; maximum_depth=std::max(maximum_depth,depth); events.push_back(event);
        if(active && event.code=="font.family_unavailable") active->paint();
        --depth;
    };
    ScrollState scroll;
    DesktopHost host(ui,appearance,[&] { return layout::control(*label); },scroll,options,false,true);
    active=&host;
    host.client_size(200,100); host.paint(); host.paint();
    auto dark=ui.presentation(); dark.variant=ThemeVariant::dark();
    bool dark_accepted=bool(ui.set_presentation(dark));
    host.observe_paint(true); host.paint();
    auto dark_text=appearance.palette(ThemeVariant::dark()).text;
    bool dark_missing=std::any_of(host.paint_observations().begin(),host.paint_observations().end(),[&](const auto& event) {
        return event.owner=="missing-font-glyph" && event.type==detail::PaintObservation::Type::text && event.visible &&
            event.color.r==dark_text.r && event.color.g==dark_text.g && event.color.b==dark_text.b;
    });
    report.check("dark-missing-glyph",dark_accepted && dark_missing,"Missing glyph receives the dark text color");
    auto changed=ui.presentation(); changed.font_family=u"LcarsUI another absent family";
    bool accepted=bool(ui.set_presentation(changed)); host.pump();
    auto count=std::count_if(events.begin(),events.end(),[&](const auto& e) {
        return e.code=="text.glyph_missing" && e.native_code==0x10ffff && e.ui_environment==&ui && e.severity==DiagnosticSeverity::info;
    });
    bool family=std::count_if(events.begin(),events.end(),[](const auto& e) {return e.code=="font.family_unavailable" && e.native_message.find("LcarsUI definitely absent")!=std::string::npos;})==1;
    bool changed_family=std::count_if(events.begin(),events.end(),[](const auto& e) {return e.code=="font.family_unavailable" && e.native_message.find("LcarsUI another absent family")!=std::string::npos;})==1;
    report.check("font-diagnostic",count==1 && family && accepted && changed_family && maximum_depth==1,"font diagnostics reach the application once without recursive callbacks");
}

void registered_component_uses_native_host(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    constexpr auto test_kind = static_cast<Kind>(100);
    TestComponent component;
    detail::ScopedComponentType registration(test_kind, component);
    Ui ui; detail::access(ui).seed_test_controls({{.id = "probe", .kind = test_kind, .value = u"Test component"}});
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
    Image pixels{1, 1, {}};
    host.render(200, 100, 1.f, folder / (report.appearance + "-registered-component.bmp"), &pixels);
    const std::size_t sample = (static_cast<std::size_t>(20) * 200 + 70) * 4;
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

void long_dropdown_native(const Appearance& appearance, Report& report) {
    std::optional<OptionValue> value=OptionValue{"0"}; std::function<void()> changed; bool invoked=false;
    Ui ui; ScrollState scroll; std::vector<RadioOption> options;
    for (int i=0; i<150; ++i) { auto id=std::to_string(i); options.push_back({{id},std::u16string(id.begin(),id.end())}); }
    auto dropdown=ui.add_dropdown({.id="native.dropdown",.kind=Kind::dropdown,.label=u"Font"},std::move(options)).value.value();
    auto other=ui.add_button({.id="native.other",.kind=Kind::button,.label=u"Other"}).value.value();
    dropdown.connect_source({[&] { return value; },[&](auto f) { changed=std::move(f); return [&] { changed={}; }; }});
    dropdown.connect_immediate([&](const DropdownChange& c) { value=c.value; if (changed) changed(); return Result{}; });
    other.bind([&] { invoked=true; return Result{}; });
    auto root=layout::row({layout::control(dropdown),layout::control(other)},8);
    DesktopOptions settings; DesktopHost host(ui,appearance,[&] { return root; },scroll,settings,false,true);
    ShowWindow(host.window(),SW_SHOWNOACTIVATE); host.force_scale(1.f); host.client_size(640,480);
    dropdown.open(); host.pump(); host.paint();
    auto post=[&](UINT message,WPARAM w,LPARAM l) { PostMessageW(host.window(),message,w,l); host.pump(); };
    auto region=host.frame().regions.back();
    POINT point{region.viewport.x+4,region.viewport.y+4}; ClientToScreen(host.window(),&point);
    post(WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-120)),MAKELPARAM(point.x,point.y)); host.paint();
    bool wheel=host.frame().regions.back().offset>0 && dropdown.describe()->expanded && value->id=="0";
    region=host.frame().regions.back();
    POINT start{region.thumb.x+region.thumb.w/2,region.thumb.y+region.thumb.h/2};
    POINT end{start.x,region.track.y+region.track.h-region.thumb.h/2};
    post(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
    post(WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
    post(WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y)); host.paint();
    auto frame=host.frame(); bool drag=frame.regions.back().offset==frame.regions.back().maximum && dropdown.describe()->expanded;
    bool selected=false;
    if (drag) { auto row=frame.overlays.back().options.back();
        post(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(row.x+4,row.y+row.h/2));
        post(WM_LBUTTONUP,0,MAKELPARAM(row.x+4,row.y+row.h/2)); selected=value->id=="149"; }
    dropdown.open(); host.pump(); host.paint(); auto target=host.frame().find(other)->bounds;
    post(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(target.x+4,target.y+target.h/2));
    post(WM_LBUTTONUP,0,MAKELPARAM(target.x+4,target.y+target.h/2));
    report.check("DD-native",wheel && drag && selected && invoked, "wheel, drag, selected last row and outside click");
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

// #79 twin of tests/multiline.cpp on a real window: posted messages drive a multi-line field. The input
// method position is asked through WM_IME_REQUEST, as a real IME does; typing through an IME needs the
// author's machine (see the author-run list).
void multiline_native(const Appearance& appearance, Report& report, const std::filesystem::path& folder = {}) {
    Ui ui; ScrollState scroll;
    auto model = std::make_shared<std::u16string>();
    auto note = ui.add_text({.id = "note", .kind = Kind::text, .label = u"Note", .multiline = true, .visible_lines = 3}).value.value();
    note.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    note.connect_confirmed([](const TextChange&) { return Result{}; });
    DesktopOptions options; options.title = u"LcarsUI multi-line verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::control(note)}, 12, 12); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.force_scale(1.f);
    host.client_size(600, 400);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto key = [&](WPARAM vk, bool ctrl = false, bool shift = false) {
        BYTE state[256]{};
        GetKeyboardState(state);
        state[VK_CONTROL] = ctrl ? 0x80 : 0; state[VK_SHIFT] = shift ? 0x80 : 0;
        SetKeyboardState(state);
        post(WM_KEYDOWN, vk, key_lparam(vk, false, false)); post(WM_KEYUP, vk, key_lparam(vk, false, true));
        state[VK_CONTROL] = 0; state[VK_SHIFT] = 0;
        SetKeyboardState(state);
    };
    auto placed = [&]() { return *detail::find_placement(host.frame(), "note"); };  // a copy: repaints replace the frame
    auto caret = [&]() { return detail::access(ui).edit("note")->selection; };
    auto line = [&] { return note.scroll_line().value.value_or(~0u); };
    note.focus(); host.pump(); host.paint();
    for (char16_t digit = u'1'; digit <= u'6'; ++digit) {
        post(WM_CHAR, digit, 1);
        if (digit != u'6') key(VK_RETURN);
    }
    const auto first_text = detail::access(ui).text("note");
    report.check("ML01-enter-and-reveal", first_text == u"1\n2\n3\n4\n5\n6" && line() == 3 && placed().view.lines.size() == 6,
        "Enter breaks lines through WM_KEYDOWN, typing to line six scrolls to the caret; first line=" + std::to_string(line()));
    key(VK_PRIOR);
    const bool page_up = caret().caret == 5 && line() == 0;  // three lines up, keeping the column after the last character
    key(VK_END, true);
    const bool ctrl_end = caret().caret == 11 && line() == 3;
    key(VK_HOME, true, true);
    report.check("ML02-cursor-keys", page_up && ctrl_end && caret().anchor == 11 && caret().caret == 0 && line() == 0,
        "PageUp moves a page, Ctrl+End and Shift+Ctrl+Home move over the whole text and extend the selection; PageUp=" + std::to_string(page_up) + ", Ctrl+End=" + std::to_string(ctrl_end) +
        ", selection=" + std::to_string(caret().anchor) + ".." + std::to_string(caret().caret) + ", first line=" + std::to_string(line()));

    auto field = placed();
    POINT inside{field.view.text.x + 20, field.view.text.y + 10};
    POINT screen = inside; ClientToScreen(hwnd, &screen);
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), MAKELPARAM(screen.x, screen.y));
    const unsigned after_notch = line();
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), MAKELPARAM(screen.x, screen.y));
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), MAKELPARAM(screen.x, screen.y));
    report.check("ML03-wheel", after_notch == 2 && line() == 3 && caret().caret == 0,
        "One wheel notch scrolls two lines and stops at the last page without moving the caret; first line " + std::to_string(after_notch) + " then " + std::to_string(line()));

    field = placed();
    auto track = field.view.track, thumb = field.view.thumb;
    POINT grab{thumb.x + thumb.w / 2, thumb.y + thumb.h / 2};
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(grab.x, grab.y));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(grab.x, track.y - 30));
    const unsigned top = line();
    post(WM_LBUTTONUP, 0, MAKELPARAM(grab.x, track.y - 30));
    report.check("ML04-thumb-drag", thumb.h > 0 && top == 0, "Dragging the field's scrollbar thumb to the top scrolls to the first line; line " + std::to_string(top));

    field = placed();
    auto row = [&](int index) { return POINT{field.view.text.x + 2, field.view.text.y + index * field.view.line_height + 3}; };
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(row(1).x, row(1).y));
    post(WM_LBUTTONUP, 0, MAKELPARAM(row(1).x, row(1).y));
    const auto clicked = caret();
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(row(0).x, row(0).y));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(row(2).x, row(2).y));
    const auto dragged = caret();
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(row(2).x, field.view.text.y + 3 * field.view.line_height + 8));
    const unsigned scrolled_by_drag = line();
    post(WM_LBUTTONUP, 0, MAKELPARAM(row(2).x, row(2).y));
    report.check("ML05-click-and-drag", clicked.caret == 2 && clicked.anchor == 2 && dragged.anchor == 0 && dragged.caret == 4 && scrolled_by_drag >= 1,
        "A click puts the caret on its line, a drag selects across lines, dragging below the field scrolls a line; click=" + std::to_string(clicked.caret) +
        ", drag=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) + ", first line=" + std::to_string(scrolled_by_drag));

    if (!folder.empty()) host.render(600, 220, 1.f, folder / (report.appearance + "-multiline.bmp"));  // evidence: selection across lines, scrollbar
    detail::access(ui).select("note", 8, 8); host.paint();
    detail::access(ui).composition_update(u"你", 1); host.paint();
    IMECHARPOSITION info{}; info.dwSize = sizeof(info); info.dwCharPos = 0;
    auto handled = SendMessageW(hwnd, WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&info));
    auto anchor = placed().caret_rect(8);  // dwCharPos 0 is the start of the composition
    POINT expected{anchor.x, anchor.y}; ClientToScreen(hwnd, &expected);
    report.check("ML06-ime-anchor", handled == 1 && info.pt.x == expected.x && info.pt.y == expected.y && info.cLineHeight == static_cast<UINT>(anchor.h) && anchor.h == placed().view.line_height,
        "The input method asks where the caret is and is told the caret's own line, not the field's top; y=" + std::to_string(info.pt.y) + " expected " + std::to_string(expected.y));
    detail::access(ui).composition_cancel();
}

// #84 twin of tests/justify.cpp on a real window with the real shaper: justified lines end flush at the
// edge, real clicks and drags land on the characters as drawn, and the user's "justified text as left"
// setting draws everything left. The glyph pixels themselves and a real input method need the author's
// machine (see the author-run list).
void justify_native(const Appearance& appearance, Report& report, const std::filesystem::path& folder = {}) {
    const std::u16string text =
        u"LcarsUI 是面向一般桌面應用的 UI 框架，並以讓 AI 操作應用程式作為未來整合方向。這一段中英數混排的文字，用來比較靠左、置中、靠右、"
        u"左右對齊與分散對齊：Windows 11 的 2026 年版本、Direct2D 與 OpenGL 渲染，以及 12345 這類數字。";
    Ui ui; ScrollState scroll;
    auto model = std::make_shared<std::u16string>(text);
    auto sample = ui.add_label({.id = "sample", .kind = Kind::label, .value = text, .text_align = TextAlign::justify}).value.value();
    auto note = ui.add_text({.id = "note", .kind = Kind::text, .label = u"Note", .value = text, .multiline = true, .visible_lines = 6, .text_align = TextAlign::justify}).value.value();
    note.connect_source({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    note.connect_confirmed([](const TextChange&) { return Result{}; });
    DesktopOptions options; options.title = u"LcarsUI justified text verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::control(sample), layout::control(note)}, 12, 12); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.force_scale(1.f);
    host.client_size(560, 460);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto placed = [&](const char* id) { return *detail::find_placement(host.frame(), id); };  // a copy: repaints replace the frame
    auto caret = [&]() { return detail::access(ui).edit("note")->selection; };
    detail::Text shaper; detail::ShapedMeasure measure(shaper);

    // JU01: the label. Every line but the last is stretched, its visible text ends at the label's right edge.
    auto label = placed("sample");
    bool label_flush = label.lines.size() > 2;
    std::string label_detail;
    for (std::size_t i = 0; i < label.lines.size(); ++i) {
        const auto& line = label.lines[i];
        const bool last = i + 1 == label.lines.size();
        if (last) { label_flush &= line.segments.empty(); continue; }
        if (line.segments.empty()) { label_flush = false; label_detail += " line " + std::to_string(i) + " not stretched;"; continue; }
        const auto content_end = static_cast<unsigned>(line.text.find_last_not_of(u' ') + 1);
        float right = 0;
        for (const auto& piece : line.segments)
            if (piece.begin < content_end) right = static_cast<float>(line.bounds.x - label.bounds.x) + piece.x + measure.measure(line.text.substr(piece.begin, std::min(piece.end, content_end) - piece.begin), label.text_size).width;
        label_flush &= std::abs(right - static_cast<float>(label.bounds.w)) <= 2.f;
        label_detail += " line " + std::to_string(i) + " right=" + std::to_string(right) + ";";
    }
    report.check("JU01-label-flush", label_flush, "A justified label's lines end at its width (" + std::to_string(label.bounds.w) + " px), the last stays left;" + label_detail);

    // JU02: the multi-line field, same rule at the wrap edge; caret stops equal what is drawn.
    auto field = placed("note");
    bool field_flush = field.multiline && field.view.lines.size() > 2;
    float edge = 0; std::string field_detail; int stretched_lines = 0;
    for (unsigned i = 0; i < field.view.lines.size(); ++i) {
        const auto& line = field.view.lines[i];
        const auto visible = std::u16string_view(text).substr(line.begin, line.end - line.begin);
        const auto content_end = line.begin + static_cast<unsigned>(visible.find_last_not_of(u' ') + 1);
        const float right = field.view.x_in_line(i, content_end); field_detail += " [" + std::to_string(i) + ": soft=" + std::to_string(line.soft) + " pieces=" + std::to_string(line.segments.size()) + " right=" + std::to_string(right) + "]";
        // A line that happens to fill the width exactly (31 wide characters) needs no stretching and has no pieces.
        if (line.soft) { if (edge == 0) edge = right; stretched_lines += line.segments.size() > 1; field_flush &= std::abs(right - edge) <= 2.f; }
        else field_flush &= line.segments.empty();
    }
    report.check("JU02-field-flush", field_flush && stretched_lines > 0 && edge > 0.8f * static_cast<float>(field.view.text.w), "Wrapped lines of the field end at one edge (" + std::to_string(edge) + " px of " + std::to_string(field.view.text.w) + ")" + field_detail);

    // JU03: a real click at every drawn caret stop of line 0 puts the caret there.
    detail::access(ui).focus("note"); host.pump(); host.paint();
    field = placed("note");
    const int y0 = field.view.row_y(0) + field.view.row_height / 2;
    unsigned wrong = 0, probed = 0;
    std::string wrong_detail;
    const int y_reset = field.view.row_y(3) + field.view.row_height / 2;
    const auto first_stops = field.view.lines[0].run.stops;  // a copy: repainting replaces the view
    for (const auto& stop : first_stops) {
        const unsigned offset = field.view.lines[0].begin + stop.first;
        const int x = field.view.text.x + static_cast<int>(std::lround(field.view.x_in_line(0, offset)));
        // A click elsewhere first, so two probes close together are never taken for a double click.
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(field.view.text.x + 1, y_reset)); post(WM_LBUTTONUP, 0, MAKELPARAM(field.view.text.x + 1, y_reset));
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y0)); post(WM_LBUTTONUP, 0, MAKELPARAM(x, y0));
        ++probed;
        if (caret().caret != offset) { ++wrong; if (wrong <= 5) wrong_detail += " offset " + std::to_string(offset) + " at x=" + std::to_string(x) + " got " + std::to_string(caret().caret) + ";"; }
        field = placed("note");
    }
    report.check("JU03-click-on-stops", probed > 8 && wrong == 0, "Clicking each drawn stop of the first line puts the caret there; probed " + std::to_string(probed) + ", wrong " + std::to_string(wrong) + wrong_detail);

    // JU04: a drag into the second line selects up to the character under the pointer.
    const auto& second = field.view.lines[1];
    const unsigned target = second.begin + second.run.stops[second.run.stops.size() / 2].first;
    const int y1 = field.view.row_y(1) + field.view.row_height / 2;
    const int x1 = field.view.text.x + static_cast<int>(std::lround(field.view.x_in_line(1, target)));
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(field.view.text.x + 1, y0));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x1, y1));
    const auto dragged = caret();
    post(WM_LBUTTONUP, 0, MAKELPARAM(x1, y1));
    if (!folder.empty()) host.render(560, 460, 1.f, folder / (report.appearance + "-justify.bmp"));
    report.check("JU04-drag-select", dragged.anchor == 0 && dragged.caret == target, "A drag into the second line selects to the drawn character; selection=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) + ", expected " + std::to_string(target));

    // JU05: the user's setting draws both left-aligned and changes no property; the UIA-facing node still says justify.
    auto settings = ui.presentation(); settings.justify_as_left = true;
    ui.set_presentation(settings); host.pump(); host.paint();
    label = placed("sample"); field = placed("note");
    bool left = std::all_of(label.lines.begin(), label.lines.end(), [](const Line& l) { return l.segments.empty(); }) &&
                std::all_of(field.view.lines.begin(), field.view.lines.end(), [](const FieldLine& l) { return l.segments.empty() && l.shift == 0; });
    report.check("JU05-user-setting", left && *sample.text_align() == TextAlign::justify && *note.text_align() == TextAlign::justify,
                 "With 'justified text as left' on both draw left-aligned and keep their property");
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
    bool dragged = false, held = false, released = false, canceled = false;
    if (item) {
        auto r = item->slider_track; const std::string id = item->id;
        POINT start{r.x, r.y + r.h / 2}, middle{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON);
        bool at_start = *f.form.handles.volume.draft() == 0;
        f.mouse(WM_MOUSEMOVE, middle, MK_LBUTTON);
        f.mouse(WM_LBUTTONUP, middle);
        dragged = at_start && *f.form.handles.volume.draft() == 50;
        // #73: native messages hold the thumb under the pointer until release or interruption.
        auto& state = detail::access(f.form.ui);
        POINT off_tick{r.x + static_cast<LONG>(r.w * 0.47), r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, off_tick, MK_LBUTTON);
        held = state.slider_drag_x(id) == off_tick.x;
        f.mouse(WM_LBUTTONUP, off_tick);
        released = !state.slider_drag_x(id);
        f.mouse(WM_LBUTTONDOWN, off_tick, MK_LBUTTON);
        SendMessageW(f.host.window(), WM_CANCELMODE, 0, 0);
        canceled = !state.slider_drag_x(id);
        f.mouse(WM_LBUTTONUP, off_tick);
        f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, middle, MK_LBUTTON); f.mouse(WM_LBUTTONUP, middle);  // back to 50 for the apply check
    }
    dragged = dragged && held && released && canceled;
    bool applied = f.form.volume_applied() == 40 && bool(f.form.handles.apply.invoke()) && f.form.volume_applied() == 50;
    f.form.handles.disabled.invoke();
    bool disabled = f.form.handles.volume.set_value(75).status == Status::disabled && *f.form.handles.volume.draft() == 50;
    report.check("F09-slider", step && home && end && dragged && applied && disabled,
        "arrow=" + std::to_string(step) + ", Home=" + std::to_string(home) + ", End=" + std::to_string(end) +
        ", native drag=" + std::to_string(dragged) + " (held=" + std::to_string(held) + ", released=" + std::to_string(released) + ", canceled=" + std::to_string(canceled) + "), apply=" + std::to_string(applied) + ", disabled=" + std::to_string(disabled));
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
    Image pixels{1, 1, {}};
    auto frame = f.host.render(900, 700, 1.f, folder / (tag + "-focus-apply.bmp"), &pixels);
    auto r = detail::find_placement(frame, f.ids.apply)->bounds;
    auto color_at = [&](int x, int y) {
        auto i = (static_cast<std::size_t>(y) * pixels.width + x) * 4;
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

WNDPROC r06_original_proc = nullptr;
LRESULT CALLBACK r06_clamped_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_WINDOWPOSCHANGING) {
        auto* pos = reinterpret_cast<WINDOWPOS*>(l);
        if (!(pos->flags & SWP_NOSIZE)) { pos->cx = std::min(pos->cx, 1024); pos->cy = std::min(pos->cy, 768); }
    }
    return CallWindowProcW(r06_original_proc, window, message, w, l);
}

void resize_first_frame_matches_next(const Appearance& appearance, Report& report, float scale, bool force_clamp = false) {
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
    RECT initial_client{};
    GetClientRect(f.host.window(), &initial_client);
    if (force_clamp) r06_original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(f.host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&r06_clamped_proc)));
    int before = f.host.frames();
    f.host.client_size(720, 540);
    f.host.pump();
    UpdateWindow(f.host.window());
    if (force_clamp) SetWindowLongPtrW(f.host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(r06_original_proc));
    Frame first = f.host.frame();
    RECT client{};
    GetClientRect(f.host.window(), &client);
    const int width = client.right - client.left, height = client.bottom - client.top;
    const int requested_width = static_cast<int>(720 * scale), requested_height = static_cast<int>(540 * scale);
    const bool clamped = width != requested_width || height != requested_height;
    int painted = f.host.frames() - before;
    InvalidateRect(f.host.window(), nullptr, FALSE);
    UpdateWindow(f.host.window());
    const Frame& second = f.host.frame();
    bool same = first.items.size() == second.items.size() && first.offset == second.offset &&
                first.maximum == second.maximum && first.viewport == second.viewport;
    for (std::size_t i = 0; same && i < first.items.size(); ++i)
        same = first.items[i].id == second.items[i].id && first.items[i].bounds == second.items[i].bounds &&
               first.items[i].clip == second.items[i].clip;
    auto target = detail::find_placement(first, f.ids.apply)->bounds;
    const auto* hit = second.hit(target.x + target.w / 2, target.y + target.h / 2);
    const auto* first_hit = first.hit(target.x + target.w / 2, target.y + target.h / 2);
    bool visible = target.x >= 0 && target.y >= 0 && target.x + target.w <= width && target.y + target.h <= height;
    bool scroll_ok = clamped ? first.offset == std::min(deep, first.maximum) : first.offset == first.maximum && first.offset < deep;
    report.check(force_clamp ? "R06-native-150-clamped" : scale > 1 ? "R06-native-150" : "R06-native-100", deep > 0 && deep == deep_max && painted == 1 && width > 0 && height > 0 && (width != initial_client.right || height != initial_client.bottom) && first.viewport.w == width && first.maximum > 0 && scroll_ok && (!force_clamp || clamped) && same && visible && first_hit && first_hit->id == f.ids.apply && hit && hit->id == f.ids.apply,
                 "640x480 wheel to offset " + std::to_string(deep) + "/" + std::to_string(deep_max) + "; SetWindowPos 720x540 -> " +
                     std::to_string(painted) + " WM_PAINT, requested client " + std::to_string(requested_width) + "x" + std::to_string(requested_height) +
                     ", actual " + std::to_string(width) + "x" + std::to_string(height) + (clamped ? " (clamped)" : " (unclamped)") + ", first frame offset " + std::to_string(first.offset) + " max " +
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
            bool end = list && list->options.size() == 100 && test::inside(list->options.back(), list->clip) &&
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
            bool restored = list && test::inside(list->options.back(), list->clip) &&
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
            h.progress_half.focus(); f.tap(VK_RIGHT); f.settle(); now += 600; state.tooltip_time(now); f.settle();
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
                f.tap(VK_RIGHT);  // the scrollbar press hid the indicator; a key shows it and starts the fresh dwell
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
            h.progress_half.focus(); f.tap(VK_RIGHT); f.settle(); now += 600; state.tooltip_time(now); f.host.paint();
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

void x04_client_size_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) {
        Fixture f(appearance, true, alternate);
        for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
            f.x04_size(size.first, size.second, scale);
            RECT actual{}; GetClientRect(f.host.window(), &actual);
            int width = int(std::lround(size.first * scale)), height = int(std::lround(size.second * scale));
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "%";
            report.check("X04-client-size", actual.right == width && actual.bottom == height,
                sample + ": expected=" + std::to_string(width) + "x" + std::to_string(height) +
                ", actual=" + std::to_string(actual.right) + "x" + std::to_string(actual.bottom));
        }
    }
}

// Inventory is made from the same typed handles as the application. Each row reports what
// the actual host paint observed; an unswept applicable state remains explicitly open.
void semantic_observation_sweep(const Appearance& appearance, const std::filesystem::path& folder, const std::string& tag) {
    std::ofstream out(folder / (tag + "-x04-sweep.tsv"));
    out << "ids\tsize\tscale\tpage\tcomponent\tkind\tvisible\tname\tvalue\trestriction\thidden\tclipped\tdisabled\tread_only\tselected\tcandidate\terror\tconflict\texecuting\tmodal\ttip\n";
    for (bool alternate : {false, true}) {
        Fixture f(appearance, true, alternate);
        auto& h = f.form.handles;
        auto entries = x04::inventory(f.form);
        f.host.observe_paint(true);
        for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
            f.x04_size(size.first, size.second, scale);
            for (const auto& page : x04::pages(f.form)) {
                h.tabs.activate({page}); f.settle();
                for (const auto& entry : entries) {
                    auto s = x04::describe(entry.handle); if (!s) continue;
                    bool active = std::string_view(entry.page) == "shared" || std::string_view(entry.page) == page;
                    const Placement* placement = x04::placement(f.host.frame(), entry.handle);
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
                    bool conflict = type.supports_state(S::conflict) && !x04::conflict_inapplicable(f.form, entry);
                    bool associated = !s->target.empty();
                    auto scroll = x04::enclosing_scroll(f.form, *s);
                    out << (alternate ? "alternate" : "shipped") << '\t' << size.first << 'x' << size.second << '\t' << int(scale * 100)
                        << '\t' << page << '\t' << s->id << '\t' << static_cast<int>(k) << '\t' << (s->visible ? "yes" : "no")
                        << '\t' << text_status(s->name) << '\t' << text_status(s->value) << '\t' << applicable(restricted) << '\t'
                        << (!active && std::string_view(entry.page) != "modal" && !s->visible && !any && !placement && !associated ? "P" : active ? "N/A" : "O") << '\t'
                        << (scroll && !*scroll ? "N/A" : active && placement && !s->visible && !any && !associated ? "P" : "O") << '\t'
                        << applicable(restricted) << '\t' << applicable(type.supports_state(S::read_only)) << '\t' << applicable(selected) << '\t'
                        << applicable(candidate) << '\t' << applicable(error) << '\t' << applicable(conflict) << '\t'
                        << applicable(type.supports_state(S::executing)) << '\t' << applicable(std::string_view(entry.page) == "modal") << '\t'
                        << applicable(s->id == h.progress_half.describe()->id) << '\n';
                }
            }
        }
    }
}

// A page switch checks all inactive members at once; results retain each component ID
// and require both of the other pages, including associated labels painted by a field.
void x04_hidden_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true);
            auto entries = x04::inventory(f.form);
            struct Outcome { int pages = 0; bool passed = true; };
            std::map<std::string, Outcome> results;
            for (const auto& page : x04::pages(f.form)) {
                bool switched = bool(f.form.handles.tabs.activate({page})); f.settle();
                for (const auto& entry : entries) {
                    if (entry.page == "shared" || entry.page == "modal" || entry.page == page) continue;
                    auto semantic = x04::describe(entry.handle);
                    if (!semantic) continue;
                    bool painted = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(),
                        [&](const auto& draw) { return draw.visible && (draw.owner == semantic->id ||
                            (!semantic->target.empty() && draw.owner == semantic->target)); });
                    auto& result = results[semantic->id]; ++result.pages;
                    result.passed = result.passed && switched && !semantic->visible && !painted &&
                        !x04::placement(f.host.frame(), entry.handle) &&
                        detail::access(f.form.ui).available(semantic->id).status == Status::hidden &&
                        x04::hidden_retry(entry.handle);
                }
            }
            bool closed = bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialog_open.invoke());
            f.settle();
            closed = closed && bool(f.form.handles.dialog_cancel.invoke()); f.settle();
            for (const auto& entry : entries) if (entry.page == "modal") {
                auto semantic = x04::describe(entry.handle);
                bool painted = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(),
                    [&](const auto& draw) { return draw.visible && draw.owner == semantic->id; });
                bool rejected = detail::access(f.form.ui).available(semantic->id).status == Status::hidden &&
                    detail::access(f.form.ui).pointer_down(semantic->id).status == Status::hidden && x04::hidden_retry(entry.handle);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-hidden-member", closed && !semantic->visible && !painted &&
                    !x04::placement(f.host.frame(), entry.handle) && rejected,
                    sample + ": closed dialog; semantics, paint, placement and admission");
            }
            for (const auto& [id, result] : results) {
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + id;
                report.check("X04-hidden-member", result.pages == static_cast<int>(x04::pages(f.form).size()) - 1 && result.passed,
                    sample + ": all inactive pages; semantics, paint, placement and admission");
            }
        }
}

// Every placement without an enclosing scroll region must also fit the client.
void x04_fixed_placement_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04_size(size.first, size.second, scale);
            bool opened = bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialog_open.invoke());
            f.settle();
            int width = int(std::lround(size.first * scale)), height = int(std::lround(size.second * scale));
            for (const auto& entry : x04::inventory(f.form)) {
                auto semantic = x04::describe(entry.handle); auto scroll = x04::enclosing_scroll(f.form, *semantic);
                if (!scroll || *scroll) continue;
                auto* item = x04::placement(f.host.frame(), entry.handle);
                bool inside = item && x04::inside_client(*item, width, height);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-fixed-placement", opened && inside, sample + ": no enclosing scroll; inside client=" + std::to_string(inside));
            }
        }
}

void x04_selected_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baseline_context(f.form);
                auto baseline_model = x04::snapshot(f.form);
                auto presentation = f.form.ui.presentation();
                auto before = x04::describe(entry.handle);
                if (!before || !detail::component_type(before->kind).supports_state(detail::ComponentState::selected)) continue;
                bool page = entry.page == "shared" || bool(f.form.handles.tabs.activate({std::string(entry.page)}));
                bool driven = baseline && page && x04::drive_selected(entry);
                auto semantic = x04::describe(entry.handle);
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, semantic->id);
                f.host.paint(); semantic = x04::describe(entry.handle);
                const auto palette = appearance.palette(f.form.ui.presentation().variant);
                auto cue = semantic->kind == Kind::checkbox || semantic->kind == Kind::radio_group
                    ? palette.accent : palette.selection;
                std::u16string ink; bool cue_visible = false;
                for (const auto& draw : f.host.paint_observations()) {
                    if (draw.owner != semantic->id || !draw.visible) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    if (draw.color.r == cue.r && draw.color.g == cue.g && draw.color.b == cue.b &&
                        draw.type != detail::PaintObservation::Type::text) cue_visible = true;
                }
                auto expected = semantic->kind == Kind::checkbox ? semantic->name : semantic->value;
                bool compared = semantic->visible && x04::placement(f.host.frame(), entry.handle) &&
                    x04::selected_semantics(*semantic) && !expected.empty() && ink.find(expected) != std::u16string::npos && cue_visible;
                bool restored = x04::restore_selected(f.form, entry, *before, presentation) && f.form.ui.presentation() == presentation && x04::same_model(baseline_model, x04::snapshot(f.form));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-selected-member", driven && compared && restored, sample +
                    ": driven=" + std::to_string(driven) + ", semantics and visible cue=" + std::to_string(compared) +
                    ", initial selection restored=" + std::to_string(restored));
            }
        }
}

void x04_executing_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                auto* button = std::get_if<ButtonHandle>(&entry.handle);
                if (!button || !detail::component_type(button->describe()->kind).supports_state(detail::ComponentState::executing) ||
                    !x04::execution_supported(f.form, *button)) continue;
                bool baseline_ok = x04::baseline_context(f.form); auto baseline = x04::snapshot(f.form);
                auto before = button->describe(); bool prepared = x04::prepare_execution(f.form, entry), observed = false;
                bool bound = x04::observe_execution(f.form, *button, [&] {
                    f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, before->id);
                    f.host.paint(); auto semantic = button->describe(); auto* item = f.host.frame().find(*button);
                    std::u16string ink;
                    for (const auto& draw : f.host.paint_observations()) {
                        if (!item || draw.owner != semantic->id || !draw.visible || draw.type != detail::PaintObservation::Type::text) continue;
                        if (intersect(draw.bounds, intersect(draw.clip, item->bounds)).w == draw.bounds.w) ink += draw.text;
                    }
                    observed = item && semantic->visible && semantic->executing && button->invoke().status == Status::busy &&
                        std::find(semantic->operations.begin(), semantic->operations.end(), Operation::activate) == semantic->operations.end() &&
                        ink.find(semantic->name) != std::u16string::npos &&
                        ink.find(appearance.component_presentation().executing_suffix) != std::u16string::npos;
                    return Result{};
                });
                bool invoked = bound && bool(button->invoke()) && observed && !*button->executing();
                bool restored = x04::restore_execution(f.form, entry, before->enabled) && x04::same_model(baseline, x04::snapshot(f.form));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + before->id;
                report.check("X04-executing-member", baseline_ok && prepared && bound && invoked && restored,
                    sample + ": prepared=" + std::to_string(prepared) + ", callback semantics, busy retry and paint=" +
                    std::to_string(invoked) + ", original action restored=" + std::to_string(restored));
            }
            bool action = bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.progress_zero.invoke()) && f.form.progress() == 0;
            report.check("X04-executing-restore", action, std::string(alternate ? "alternate" : "shipped") + " " +
                std::to_string(size.first) + "x" + std::to_string(size.second) + " " + std::to_string(int(scale * 100)) +
                "%: restored progress action updates the application model");
        }
}

void x04_clipped_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto inventory = x04::inventory(f.form);
            f.x04_size(size.first, size.second, scale); f.host.observe_paint(true);
            for (const auto& entry : inventory) {
                if (entry.page == "shared" || entry.page == "modal") continue;
                auto baseline = x04::snapshot(f.form);
                bool prepared = x04::prepare_clipped(f.form, entry);
                f.form.scroll.offset = 0; f.host.paint();
                auto semantic = x04::describe(entry.handle); auto initial_rect = x04::clipped_rect(f.host.frame(), *semantic);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
                if (!prepared || !initial_rect || f.host.frame().maximum <= 0) {
                    bool restored = x04::restore_clipped(f.form, entry) && x04::same_model(baseline, x04::snapshot(f.form));
                    if (!restored) report.check("X04-clipped-member", false, sample + ": fixture did not restore the model");
                    else report.open("X04-clipped-member", sample + ": fixture could not produce a scrollable target");
                    continue;
                }
                bool fully = false, partial = false, correct = true;
                for (int offset : x04::clipped_offsets(f.host.frame(), *initial_rect)) {
                    f.form.scroll.offset = offset; f.host.paint(); semantic = x04::describe(entry.handle);
                    auto geometry = x04::clipped_rect(f.host.frame(), *semantic);
                    auto* owner = x04::clipped_owner(f.host.frame(), *semantic);
                    if (!geometry || !owner) { correct = false; continue; }
                    auto area = intersect(*geometry, owner->clip);
                    const std::vector<Line>* associated = nullptr;
                    if (!semantic->target.empty()) {
                        if (semantic->role == TextRole::name) associated = &owner->label_lines;
                        else if (semantic->role == TextRole::description) associated = &owner->description_lines;
                        else if (semantic->role == TextRole::error) associated = &owner->error_lines;
                    }
                    bool visible_paint = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& draw) {
                        if (draw.owner != owner->id || !draw.visible) return false;
                        if (!associated) return true;
                        return draw.type == detail::PaintObservation::Type::text && std::any_of(associated->begin(), associated->end(),
                            [&](const Line& line) { return line.text == draw.text; });
                    });
                    if (area.w == 0 || area.h == 0) {
                        auto* hit = f.host.frame().hit(geometry->x + std::max(0, geometry->w / 2), geometry->y + std::max(0, geometry->h / 2));
                        correct = correct && !semantic->visible && !visible_paint && (!hit || hit->id != owner->id); fully = true;
                    } else if (area != *geometry) {
                        correct = correct && semantic->visible;
                        if (semantic->visible && visible_paint) partial = true;
                    }
                }
                bool restored = x04::restore_clipped(f.form, entry) && x04::same_model(baseline, x04::snapshot(f.form));
                correct = correct && restored;
                if (!correct) report.check("X04-clipped-member", false, sample + ": clipping comparison or model restoration disagreed");
                else if (!fully || !partial) report.open("X04-clipped-member", sample + ": fixture reached fully=" + std::to_string(fully) +
                    ", partially clipped visible paint=" + std::to_string(partial));
                else report.check("X04-clipped-member", true, sample + ": full and partial clipping, semantics, visible paint and hit agree");
            }
        }
}

void x04_error_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baseline_context(f.form);
                auto semantic = x04::describe(entry.handle);
                if (!semantic || !detail::component_type(semantic->kind).supports_state(detail::ComponentState::error) ||
                    !x04::error_supported(*semantic)) continue;
                bool driven = baseline && x04::drive_error(f.form, entry);
                auto owner = semantic->target.empty() ? semantic->id : semantic->target;
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, owner);
                f.host.paint(); semantic = x04::describe(entry.handle);
                auto* item = detail::find_placement(f.host.frame(), owner);
                std::u16string ink; bool color = false; auto error_color = appearance.palette().error;
                for (const auto& draw : f.host.paint_observations()) {
                    if (draw.owner != owner || !draw.visible) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    if (draw.color.r == error_color.r && draw.color.g == error_color.g && draw.color.b == error_color.b) color = true;
                }
                bool cue = semantic->kind == Kind::tabs ? ink.find(appearance.component_presentation().invalid_tab_marker) != std::u16string::npos :
                    ink.find(x04::error_message) != std::u16string::npos && color;
                bool semantic_state = x04::error_semantics(f.form, *semantic);
                bool compared = item && semantic->visible && semantic_state && cue;
                bool restored = x04::restore_error(f.form, entry) && !x04::error_semantics(f.form, *x04::describe(entry.handle));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-error-member", driven && compared && restored,
                    sample + ": driven=" + std::to_string(driven) + ", semantics, association and cue=" + std::to_string(compared) +
                    ", restored=" + std::to_string(restored));
            }
        }
}

void x04_candidate_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baseline_context(f.form);
                auto baseline_model = x04::snapshot(f.form);
                auto before = x04::describe(entry.handle);
                if (!before || !detail::component_type(before->kind).supports_state(detail::ComponentState::candidate)) continue;
                bool prepared = baseline && x04::prepare_candidate(f.form, entry);
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, before->id); f.host.paint();
                bool opened = x04::open_candidate(entry);
                auto vk = before->kind == Kind::tabs ? VK_RIGHT : VK_DOWN;
                SendMessageW(f.host.window(), WM_KEYDOWN, vk, key_lparam(vk, false, false));
                SendMessageW(f.host.window(), WM_KEYUP, vk, key_lparam(vk, false, true));
                f.host.paint(); auto semantic = x04::describe(entry.handle);
                auto* item = x04::candidate_placement(f.host.frame(), *semantic); auto label = x04::candidate_label(*semantic);
                auto cue = semantic->kind == Kind::dropdown ? appearance.palette().selection : appearance.palette().accent;
                std::u16string ink; bool cue_visible = false;
                for (const auto& draw : f.host.paint_observations()) {
                    if (draw.owner != semantic->id || !draw.visible || draw.tooltip ||
                        (semantic->kind == Kind::dropdown) != draw.popup) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    else if (draw.color.r == cue.r && draw.color.g == cue.g && draw.color.b == cue.b) cue_visible = true;
                }
                bool compared = item && semantic->visible && label && ink.find(*label) != std::u16string::npos && cue_visible;
                if (semantic->kind == Kind::tabs || semantic->kind == Kind::dropdown) compared = compared && semantic->value == before->value;
                bool restored = x04::restore_candidate(f.form, entry) && x04::same_model(baseline_model, x04::snapshot(f.form));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-candidate-member", prepared && opened && compared && restored,
                    sample + ": prepared=" + std::to_string(prepared && opened) + ", semantics and cue=" + std::to_string(compared) +
                    ", restored=" + std::to_string(restored));
            }
        }
}

void x04_search_read_only_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& search = f.form.list->handles.search;
            f.x04_size(size.first, size.second, scale); f.host.observe_paint(true);
            bool driven = x04::drive_search_read_only(f.form) && bool(search.focus());
            f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, search.describe()->id);
            f.host.paint(); auto semantic = search.describe(); std::u16string ink;
            for (const auto& draw : f.host.paint_observations())
                if (draw.owner == semantic->id && draw.visible && draw.type == detail::PaintObservation::Type::text) ink += draw.text;
            bool restricted = search.set_value(u"blocked").status == Status::read_only;
            SendMessageW(f.host.window(), WM_CHAR, L'x', 0);
            restricted = restricted && search.describe()->value == semantic->value;
            bool compared = semantic->visible && semantic->enabled && semantic->read_only &&
                ink.find(semantic->name) != std::u16string::npos && ink.find(semantic->value) != std::u16string::npos &&
                ink.find(appearance.component_presentation().read_only_suffix) != std::u16string::npos;
            bool restored = x04::restore_search_read_only(f.form) && !search.describe()->read_only;
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
            report.check("X04-search-read-only", driven && restricted && compared && restored,
                sample + ": restricted=" + std::to_string(restricted) + ", paint=" + std::to_string(compared) + ", restored=" + std::to_string(restored));
        }
}

void x04_modal_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true); bool opened = x04::open_modal(f.form); f.host.paint();
            bool blocked = f.host.frame().modal && f.form.handles.progress_zero.invoke().status == Status::blocked_by_modal;
            struct Row { std::string id; bool passed; }; std::vector<Row> rows;
            for (const auto& entry : x04::inventory(f.form)) {
                if (entry.page != "modal") continue;
                auto semantic = x04::describe(entry.handle); auto* item = x04::placement(f.host.frame(), entry.handle);
                std::u16string ink;
                for (const auto& draw : f.host.paint_observations())
                    if (draw.owner == semantic->id && draw.visible && draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                bool compared = opened && blocked && item && semantic->visible && x04::normal_semantics(*semantic);
                for (const auto& expected : x04::normal_text(*semantic)) compared = compared && ink.find(expected) != std::u16string::npos;
                rows.push_back({semantic->id, compared});
            }
            bool restored = x04::close_modal(f.form) && !*f.form.handles.dialog.is_open();
            for (const auto& row : rows) {
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + row.id;
                report.check("X04-modal-member", row.passed && restored,
                    sample + ": modal semantics, paint and background rejection=" + std::to_string(row.passed) + ", restored=" + std::to_string(restored));
            }
        }
}

void x04_tooltip_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& state = detail::access(f.form.ui); std::uint64_t now = 100;
            f.x04_size(size.first, size.second, scale); f.host.observe_paint(true);
            state.tooltip_clock([&] { return now; }); SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
            bool driven = x04::prepare_tip(f.form); f.tap(VK_RIGHT); auto semantic = f.form.handles.progress_half.describe();
            f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, semantic->id);
            f.host.paint(); now += 600; state.tooltip_time(now); f.host.paint();
            auto tip = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(), [](const auto& p) { return p.tooltip; });
            bool visible = *f.form.handles.progress_tip.visible() && tip != f.host.frame().overlays.end();
            std::u16string ink;
            for (const auto& draw : f.host.paint_observations())
                if (draw.owner == semantic->id && draw.tooltip && draw.visible && draw.type == detail::PaintObservation::Type::text) ink += draw.text;
            bool transparent = false;
            if (visible) { auto* hit = f.host.frame().hit(tip->bounds.x + 1, tip->bounds.y + 1); transparent = !hit || !hit->tooltip; }
            bool text = visible && !semantic->description.empty() && ink.find(semantic->description) != std::u16string::npos;
            SendMessageW(f.host.window(), WM_KEYDOWN, VK_ESCAPE, key_lparam(VK_ESCAPE, false, false));
            SendMessageW(f.host.window(), WM_KEYUP, VK_ESCAPE, key_lparam(VK_ESCAPE, false, true));
            bool restored = !*f.form.handles.progress_tip.visible() && f.form.handles.progress_half.describe()->focused;
            state.tooltip_clock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
            report.check("X04-tip-member", driven && text && transparent && restored,
                sample + ": text=" + std::to_string(text) + ", transparent=" + std::to_string(transparent) + ", restored=" + std::to_string(restored));
        }
}

void x04_normal_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& state = detail::access(f.form.ui);
            f.x04_size(size.first, size.second, scale); f.host.observe_paint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baseline_context(f.form);
                bool modal = entry.page == "modal";
                bool page = modal ? bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialog_open.invoke()) :
                    entry.page == "shared" || bool(f.form.handles.tabs.activate({std::string(entry.page)}));
                auto semantic = x04::describe(entry.handle);
                auto owner = semantic->target.empty() ? semantic->id : semantic->target;
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, owner);
                f.host.paint(); semantic = x04::describe(entry.handle);
                auto* item = detail::find_placement(f.host.frame(), owner);
                std::u16string ink; bool visible_owner_ink = false, accent = false, selected = false;
                auto palette = appearance.palette();
                for (const auto& draw : f.host.paint_observations()) {
                    if (draw.owner != owner || !draw.visible) continue;
                    visible_owner_ink = true;
                    if (draw.type == detail::PaintObservation::Type::text && item &&
                        intersect(draw.bounds, intersect(draw.clip, item->bounds)).w == draw.bounds.w) ink += draw.text;
                    if (draw.type != detail::PaintObservation::Type::text) {
                        if (draw.color.r == palette.accent.r && draw.color.g == palette.accent.g && draw.color.b == palette.accent.b) accent = true;
                        if (draw.color.r == palette.selection.r && draw.color.g == palette.selection.g && draw.color.b == palette.selection.b) selected = true;
                    }
                }
                bool compared = baseline && page && x04::normal_semantics(*semantic);
                if (!semantic->target.empty()) {
                    auto target = state.describe(semantic->target);
                    compared = compared && target && std::find(target->labels.begin(), target->labels.end(), semantic->id) != target->labels.end();
                }
                if (!semantic->visible && semantic->kind == Kind::label && semantic->value.empty()) {
                    compared = compared && (semantic->target.empty() ? !visible_owner_ink :
                        semantic->role == TextRole::error && state.describe(semantic->target)->error.empty());
                } else {
                    compared = compared && item && semantic->visible;
                    for (const auto& expected : x04::normal_text(*semantic)) compared = compared && ink.find(expected) != std::u16string::npos;
                    if (semantic->kind == Kind::checkbox && semantic->checked == true) compared = compared && accent;
                    if (semantic->kind == Kind::radio_group && !semantic->value.empty()) compared = compared && accent;
                    if ((semantic->kind == Kind::list || semantic->kind == Kind::tabs) && !semantic->value.empty()) compared = compared && selected;
                    if (semantic->kind == Kind::progress && item && semantic->numeric_value)
                        compared = compared && item->progress_fill.w == item->progress_track.w * *semantic->numeric_value / 100 &&
                            (*semantic->numeric_value == 0 || accent);
                }
                bool restored = (!modal || (bool(f.form.handles.dialog.settle(false)) && bool(f.form.handles.reset_v03.invoke()))) &&
                    x04::baseline_context(f.form);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-normal-member", compared && restored, sample +
                    ": visible=" + std::to_string(semantic->visible) + ", semantics/paint=" + std::to_string(compared) +
                    ", restored=" + std::to_string(restored));
            }
        }
}

void x04_disabled_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04_size(size.first, size.second, scale); f.host.observe_paint(true);
            auto reveal = [&](const std::string& id) {
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, id); f.host.paint();
            };
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baseline_context(f.form);
                auto baseline_model = x04::snapshot(f.form);
                auto initial = x04::describe(entry.handle);
                if (!initial || !detail::component_type(initial->kind).supports_state(detail::ComponentState::disabled)) continue;
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + initial->id;
                if (initial->kind == Kind::tabs) {
                    auto tabs = std::get<TabsHandle>(entry.handle);
                    bool driven = baseline && bool(tabs.activate({"v01"})) && bool(tabs.enable_page({"v02"}, false)) && bool(tabs.focus());
                    f.host.paint(); auto sem = tabs.describe(); auto* placed = f.host.frame().find(tabs);
                    bool cue = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& draw) {
                        return draw.owner == sem->id && draw.visible && draw.type == detail::PaintObservation::Type::text &&
                               draw.text.find(appearance.component_presentation().disabled_tab_marker) != std::u16string::npos;
                    });
                    bool rejected = tabs.activate({"v02"}).status == Status::disabled && !sem->options[1].enabled;
                    bool pointer = false;
                    if (placed && placed->options.size() > 1) {
                        auto r = intersect(placed->options[1], placed->clip); POINT pt{r.x + r.w / 2, r.y + r.h / 2};
                        auto* hit = f.host.frame().hit(pt.x, pt.y);
                        bool hits_tab = hit && hit->id == sem->id;
                        SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                        SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                        pointer = r.w > 0 && r.h > 0 && hits_tab && *tabs.active() == OptionValue{"v01"};
                    }
                    SendMessageW(f.host.window(), WM_KEYDOWN, VK_RIGHT, key_lparam(VK_RIGHT, false, false));
                    SendMessageW(f.host.window(), WM_KEYUP, VK_RIGHT, key_lparam(VK_RIGHT, false, true));
                    bool keyboard = tabs.describe()->candidate == OptionValue{"v03"} && *tabs.active() == OptionValue{"v01"};
                    bool recovered = bool(tabs.enable_page({"v02"}, true)) && bool(tabs.activate({"v02"})) && bool(tabs.activate({"v01"})) &&
                        x04::baseline_context(f.form) && x04::same_model(baseline_model, x04::snapshot(f.form));
                    report.check("X04-disabled-member", driven && cue && rejected && pointer && keyboard && recovered,
                        sample + ": tab cue=" + std::to_string(cue) + ", rejected=" + std::to_string(rejected) +
                        ", pointer=" + std::to_string(pointer) + ", keyboard=" + std::to_string(keyboard) + ", recovered=" + std::to_string(recovered));
                    continue;
                }
                bool page = entry.page == "modal" ? bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialog_open.invoke()) :
                    bool(f.form.handles.tabs.activate({std::string(entry.page)}));
                bool driven = baseline && page && x04::set_enabled(entry.handle, true) && x04::focus(f.form.ui, entry.handle) && x04::set_enabled(entry.handle, false);
                reveal(initial->id); auto sem = x04::describe(entry.handle);
                auto* placed = x04::placement(f.host.frame(), entry.handle); std::u16string visible_text;
                for (const auto& draw : f.host.paint_observations()) {
                    if (draw.owner != sem->id || !draw.visible || draw.type != detail::PaintObservation::Type::text || !placed) continue;
                    auto visible_width = intersect(draw.bounds, intersect(draw.clip, placed->bounds)).w;
                    if (draw.bounds.w > 0 && visible_width == draw.bounds.w) visible_text += draw.text;
                }
                bool cue = sem->visible && !sem->enabled && !sem->focused &&
                    visible_text.find(appearance.component_presentation().disabled_suffix) != std::u16string::npos;
                auto snapshot = x04::snapshot(f.form);
                bool rejected = x04::disabled_retry(entry.handle);
                bool pointer = false;
                if (placed) {
                    auto r = intersect(placed->bounds, placed->clip); POINT pt{r.x + r.w / 2, r.y + r.h / 2};
                    auto* hit = f.host.frame().hit(pt.x, pt.y);
                    bool hits_target = hit && hit->id == sem->id;
                    SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                    SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                    pointer = r.w > 0 && r.h > 0 && hits_target;
                }
                SendMessageW(f.host.window(), WM_KEYDOWN, VK_SPACE, key_lparam(VK_SPACE, false, false));
                SendMessageW(f.host.window(), WM_KEYUP, VK_SPACE, key_lparam(VK_SPACE, false, true));
                auto after = x04::describe(entry.handle);
                bool unchanged = after && !after->focused && x04::same_model(snapshot, x04::snapshot(f.form));
                bool recovered = x04::set_enabled(entry.handle, true) && x04::focus(f.form.ui, entry.handle) &&
                    x04::set_enabled(entry.handle, initial->enabled);
                bool option_checks = true;
                if (sem->kind == Kind::radio_group && initial->enabled) {
                    auto radio = std::get<RadioGroupHandle>(entry.handle); auto previous = *radio.draft();
                    bool option = previous && bool(radio.select({"focus"})) && bool(radio.enable_option({"offline"}, false)) && bool(radio.focus());
                    reveal(sem->id); auto option_semantic = radio.describe(); auto* option_item = f.host.frame().find(radio);
                    bool public_reject = !option_semantic->options[2].enabled && radio.select({"offline"}).status == Status::disabled;
                    bool cue_option = std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(), [&](const auto& draw) {
                        return draw.owner == sem->id && draw.visible && draw.type == detail::PaintObservation::Type::text &&
                            draw.text.find(option_semantic->options[2].label + appearance.component_presentation().disabled_suffix) != std::u16string::npos;
                    });
                    bool pointer_option = false;
                    if (option_item && option_item->options.size() > 2) {
                        auto r = intersect(option_item->options[2], option_item->clip); POINT pt{r.x + r.w / 2, r.y + r.h / 2};
                        auto* hit = f.host.frame().hit(pt.x, pt.y);
                        bool target = hit && hit->id == sem->id && f.host.frame().option_at(*option_item, pt.x, pt.y) == 2;
                        SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                        SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                        pointer_option = r.w > 0 && r.h > 0 && target && *radio.draft() == OptionValue{"focus"};
                    }
                    SendMessageW(f.host.window(), WM_KEYDOWN, VK_RIGHT, key_lparam(VK_RIGHT, false, false));
                    SendMessageW(f.host.window(), WM_KEYUP, VK_RIGHT, key_lparam(VK_RIGHT, false, true));
                    bool keyboard_option = *radio.draft() == OptionValue{"focus"};
                    bool option_restored = bool(radio.enable_option({"offline"}, true)) && previous &&
                        bool(radio.select({"offline"})) && bool(radio.select(*previous));
                    option_checks = option && public_reject && cue_option && pointer_option && keyboard_option && option_restored;
                    recovered = recovered && option_checks;
                }
                if (entry.page == "modal") recovered = bool(f.form.handles.dialog.settle(false)) && bool(f.form.handles.reset_v03.invoke()) && recovered;
                if (sem->kind == Kind::list) recovered = x04::baseline_context(f.form) && bool(f.form.handles.tabs.activate({"v02"})) &&
                    bool(f.form.list->handles.zero.invoke()) && bool(f.form.list->handles.twenty.invoke()) && recovered;
                recovered = x04::baseline_context(f.form) && x04::same_model(baseline_model, x04::snapshot(f.form)) && recovered;
                report.check("X04-disabled-member", driven && cue && rejected && pointer && unchanged && recovered,
                    sample + ": driven=" + std::to_string(driven) + ", cue=" + std::to_string(cue) +
                    ", public=" + std::to_string(rejected) + ", pointer=" + std::to_string(pointer) +
                    ", unchanged=" + std::to_string(unchanged) + ", recovered=" + std::to_string(recovered) +
                    (sem->kind == Kind::radio_group ? ", option checks=" + std::to_string(option_checks) : ""));
            }
        }
}

void x04_group_conflict_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04_size(size.first, size.second, scale);
            f.host.observe_paint(true);
            auto reveal = [&](const std::string& id) {
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, id);
                f.host.paint();
            };
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baseline_context(f.form);
                auto semantic = x04::describe(entry.handle);
                if (!semantic || !detail::component_type(semantic->kind).supports_state(detail::ComponentState::conflict) ||
                    !x04::group_conflict_supported(f.form, *semantic)) continue;
                bool driven = baseline && x04::drive_group_conflict(f.form, entry);
                reveal(semantic->id); semantic = x04::describe(entry.handle);
                std::u16string ink; bool checked_cue = false;
                auto accent = appearance.palette().accent;
                for (const auto& draw : f.host.paint_observations()) {
                    if (draw.owner != semantic->id || !draw.visible) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    else if (draw.color.r == accent.r && draw.color.g == accent.g && draw.color.b == accent.b) checked_cue = true;
                }
                bool field = semantic->visible && semantic->source_conflict && ink.find(semantic->name) != std::u16string::npos &&
                    ink.find(appearance.component_presentation().conflict_suffix) != std::u16string::npos &&
                    (semantic->kind == Kind::checkbox ? semantic->checked == true && checked_cue :
                        semantic->value.empty() || ink.find(semantic->value) != std::u16string::npos);
                auto latest = x04::latest_for(f.form, entry); auto action = latest.describe();
                bool enabled = action->enabled; reveal(action->id); action = latest.describe();
                bool action_painted = action->visible && std::any_of(f.host.paint_observations().begin(), f.host.paint_observations().end(),
                    [&](const auto& draw) { return draw.owner == action->id && draw.visible &&
                        draw.type == detail::PaintObservation::Type::text && draw.text.find(action->name) != std::u16string::npos; });
                bool restored = bool(latest.invoke()) && !x04::describe(entry.handle)->source_conflict && bool(f.form.handles.reset.invoke()) && x04::baseline_context(f.form);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-conflict-member", driven && field && enabled && action_painted && restored, sample +
                    ": driven=" + std::to_string(driven) + ", field=" + std::to_string(field) +
                    ", latest=" + std::to_string(enabled && action_painted) + ", restored=" + std::to_string(restored));
            }
        }
}

// State driver shared with the core seam, sampled through real clipped host paint.
void x04_name_read_only_matrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& name = f.form.handles.name;
            f.x04_size(size.first, size.second, scale);
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
            f.x04_size(size.first, size.second, scale); f.settle();
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
    auto contains_color = [](const Image& canvas, Rect bounds, Color color) {
        auto r = intersect(bounds, {0, 0, canvas.width, canvas.height});
        for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) {
            auto i = (static_cast<std::size_t>(y) * canvas.width + x) * 4;
            if (canvas.pixels[i] == color.r && canvas.pixels[i + 1] == color.g && canvas.pixels[i + 2] == color.b) return true;
        }
        return false;
    };
    {
        Fixture dark(appearance, true); auto& d = dark.form.handles;
        auto settings = dark.form.ui.presentation(); settings.variant = ThemeVariant::dark();
        bool applied = bool(dark.form.ui.set_presentation(settings));
        d.error.invoke();
        dark.host.layout(960, 720, 1.f, dark.form.scroll, d.name.describe()->id);
        Image image{1, 1, {}};
        auto dark_frame = dark.host.render(960, 720, 1.f, folder / (report.appearance + "-pv03-dark.bmp"), &image);
        auto* field = dark_frame.find(d.name); auto colors = appearance.palette(ThemeVariant::dark());
        bool error_ink = field && !field->error_lines.empty() && contains_color(image, intersect(field->error_lines.front().bounds, field->clip), colors.error);
        bool invalid = d.name.describe()->invalid;
        auto error_compare = dark.host.compare_gpu(960, 720, 1.f);
        write_comparison_bmp(folder / (report.appearance + "-pv03-error-cpu.bmp"), error_compare.reference);
        emit_failure_if_needed(folder, report.appearance + "-pv03-error", error_compare, bool(error_compare));
        d.name.set_value(u"Tester"); d.name.focus(); d.name.select(1, 4);
        dark_frame = dark.host.render(960, 720, 1.f, folder / (report.appearance + "-pv03-selection.bmp"), &image);
        field = dark_frame.find(d.name);
        bool selection = field && contains_color(image, intersect(field->bounds, field->clip), colors.selection);
        bool focus = field && contains_color(image, intersect(field->bounds, field->clip), colors.accent);
        auto selection_compare = dark.host.compare_gpu(960, 720, 1.f);
        write_comparison_bmp(folder / (report.appearance + "-pv03-selection-cpu.bmp"), selection_compare.reference);
        emit_failure_if_needed(folder, report.appearance + "-pv03-selection", selection_compare, bool(selection_compare));
        d.disabled.invoke();
        dark.host.observe_paint(true); dark.host.paint();
        bool disabled = !d.name.describe()->enabled && std::any_of(dark.host.paint_observations().begin(), dark.host.paint_observations().end(), [&](const auto& event) {
            return event.visible && event.text.find(appearance.component_presentation().disabled_suffix) != std::u16string::npos;
        });
        dark.host.render(960, 720, 1.f, folder / (report.appearance + "-pv03-disabled.bmp"));
        auto disabled_compare = dark.host.compare_gpu(960, 720, 1.f);
        write_comparison_bmp(folder / (report.appearance + "-pv03-disabled-cpu.bmp"), disabled_compare.reference);
        emit_failure_if_needed(folder, report.appearance + "-pv03-disabled", disabled_compare, bool(disabled_compare));
        report.check("PV03-dark-raster", applied && error_ink && selection && focus && invalid && disabled && bool(error_compare) && bool(selection_compare) && bool(disabled_compare),
                     "CPU raster: applied=" + std::to_string(applied) + ", error=" + std::to_string(error_ink) + ", selection=" + std::to_string(selection) + ", focus=" + std::to_string(focus) + ", invalid=" + std::to_string(invalid) + ", disabled=" + std::to_string(disabled));
    }
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    auto sample = [&](std::string_view name, Image& pixels) {
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
    Image pixels{1, 1, {}};
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
        std::u16string executing_ink;
        for (const auto& event : f.host.paint_observations())
            if (event.owner == semantic->id && event.visible && event.type == detail::PaintObservation::Type::text)
                executing_ink += event.text;
        executing_painted = semantic->executing &&
            std::none_of(semantic->operations.begin(), semantic->operations.end(), [](Operation op) { return op == Operation::activate; }) &&
            executing_ink.find(semantic->name) != std::u16string::npos &&
            executing_ink.find(appearance.component_presentation().executing_suffix) != std::u16string::npos;
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


}

int run_uia_ime(const std::filesystem::path& folder, const std::wstring& appearance) {
    std::filesystem::create_directories(folder); Report report(folder / "uia-ime.tsv");
    report.appearance = appearance == L"material" ? "material" : "web";
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) { report.check("UIA21", false, "COM initialization failed"); return 1; }
    try {
        Live live(appearance == L"material" ? material_appearance() : web_appearance());
        Profiles profiles;
        if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo could not be activated");
        live.start_in_name();
        if (!live.form.handles.name.set_value(u"UIA baseline") || !live.form.handles.name.select(4, 4)) throw Blocked("Unable to prepare committed value");
        if (auto imc = ImmGetContext(live.host.window())) {
            DWORD conversion = 0, sentence = 0; ImmGetConversionStatus(imc, &conversion, &sentence);
            ImmSetConversionStatus(imc, conversion | IME_CMODE_NATIVE, sentence); ImmReleaseContext(live.host.window(), imc);
        }
        live.type("JI3");
        const auto* edit = detail::access(live.form.ui).edit(live.ids.name);
        if (!edit || !edit->composing || edit->preedit.empty()) throw Blocked("Bopomofo composition did not start");
        const auto committed = edit->text; const auto preedit = edit->preedit;
        wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr, module, MAX_PATH);
        auto client = std::filesystem::path(module).parent_path() / L"lcars_uia_client.exe";
        std::wstring command = L"\"" + client.wstring() + L"\" --ime-probe " + std::to_wstring(reinterpret_cast<std::uintptr_t>(live.host.window()));
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) throw Blocked("UIA client could not start");
        CloseHandle(process.hThread); auto deadline = GetTickCount64() + 30000;
        while (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) live.wait(50);
        DWORD code = 1;
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
        else GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess); live.wait(100);
        edit = detail::access(live.form.ui).edit(live.ids.name);
        bool intact = edit && edit->composing && edit->preedit == preedit && edit->text == committed;
        report.check("UIA21", code == 0 && intact, "UIA SetValue rejected; committed Value unchanged; Bopomofo preedit intact; client exit=" + std::to_string(code));
        live.clear_composition();
    } catch (const Blocked& e) { report.check("UIA21-BLOCKED", false, e.what()); }
    CoUninitialize(); return report.failures ? 1 : 0;
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
        const auto* tab_placement = detail::find_placement(live.host.frame(), live.form.handles.tabs.describe()->id);
        if (!tab_placement || tab_placement->options.size() < 2) throw std::runtime_error("Second tab has no placement");
        const auto target_tab = tab_placement->options[1];
        live.click_point({target_tab.x + target_tab.w / 2, target_tab.y + target_tab.h / 2});
        auto page_after = live.form.handles.tabs.active()->id;
        bool switched = *live.form.handles.tabs.active() == OptionValue{"v02"} && !session->composing &&
            session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.form.handles.tabs.describe()->focused && live.form.applies() == 0;
        live.form.handles.tabs.activate({"v01"});
        live.wait(100);
        record("E01-page-switch", switched && live.history() == 1,
               "Real tab click during composition commits once, focuses tabs and switches page; page=" + page_after + "; before: " + page_before);

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
        live.form.handles.progress_half.focus(); live.tap(VK_RIGHT);  // the focus tooltip needs keyboard use first
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

bool dpi_session(const Appearance& appearance, Report& report, bool presentation = false) {
    try {
        std::vector<MONITORINFOEXW> monitors;
        EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&monitors));
        if (monitors.size() < 2) throw Blocked("two active monitors are required");
        std::sort(monitors.begin(), monitors.end(), [](const auto& a, const auto& b) { return a.rcMonitor.left < b.rcMonitor.left; });
        Live live(appearance);
        DpiObserver observer(live.host.window());
        if (presentation) { auto settings = live.form.ui.presentation(); settings.text_size = 28; if (!live.form.ui.set_presentation(settings)) throw Blocked("28 DIP setting rejected"); }
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
        auto atlas_before_forward = live.host.atlas_clears();
        UINT second = move(monitors[1]);
        auto atlas_after_forward = live.host.atlas_clears();
        auto sharp_text = [&] {
            RECT client{}; GetClientRect(live.host.window(), &client);
            auto comparison = live.host.compare_gpu(client.right, client.bottom, live.host.scale());
            const auto* text_field = live.host.frame().find(live.form.handles.name);
            Rect text_area{};
            if (text_field) text_area = {text_field->bounds.x + text_field->field.editor.x,
                text_field->bounds.y + text_field->field.editor.y,
                text_field->field.editor.w, text_field->field.editor.h};
            auto text_compare = compare_images(comparison.reference, comparison.actual, {text_area});
            return text_field && text_area.w > 0 && text_area.h > 0 && bool(comparison) && text_compare.regions_ok;
        };
        bool forward_sharp = !presentation && first == 96 && second == 144 && sharp_text();
        if (presentation && !((first == 96 && second == 144) || (first == 144 && second == 96)))
            throw Blocked("PR02 requires one 100% and one 150% monitor");
        if (presentation && first == 96) {
            std::swap(monitors[0], monitors[1]);
            first = move(monitors[0]);
            before = dpi_messages;
            second = move(monitors[1]);
        }
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
        auto atlas_before_return = live.host.atlas_clears();
        UINT selection_return = move(monitors[0]);
        auto atlas_after_return = live.host.atlas_clears();
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
        // GA05 uses the actual 100% -> 150% leg, irrespective of monitor ordering.
        if (!presentation) {
            bool forward = first == 96 && second == 144;
            bool backward = first == 144 && second == 96;
            unsigned clears_before = forward ? atlas_before_forward : atlas_before_return;
            unsigned clears_after = forward ? atlas_after_forward : atlas_after_return;
            bool sharp = forward ? forward_sharp : (backward && sharp_text());
            report.check("GA05", (forward || backward) && clears_after == clears_before + 1 && sharp,
                "100% to 150%; atlas clears=" + std::to_string(clears_before) + " -> " + std::to_string(clears_after) +
                "; GPU/CPU text region=" + std::to_string(sharp));
        }
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
        // Report which part of the popup predicate failed: absent dropdown, geometry or hit.
        auto popup_check = [&](const Frame& f, std::string& detail) {
            RECT client{}; GetClientRect(live.host.window(), &client);
            auto result = test::popup_in_client(f, {client.left, client.top, client.right - client.left, client.bottom - client.top}, ids.region);
            bool expanded = live.form.handles.region.describe() && live.form.handles.region.describe()->expanded;
            detail = "overlays=" + std::to_string(f.overlays.size()) + " expanded=" + std::to_string(expanded) +
                     " dropdown=" + std::to_string(result.popup != nullptr);
            if (result.popup) {
                detail += " bounds=" + str(result.popup->bounds) + " viewport=" + str(f.viewport) +
                          " client=" + std::to_string(client.right) + "x" + std::to_string(client.bottom) +
                          " inside=" + std::to_string(result.inside) + " hit=" + std::to_string(result.hit);
            }
            return expanded && bool(result);
        };
        before = dpi_messages;
        UINT popup_dpi = move(monitors[1]);
        const auto& frame = live.host.frame();
        std::string popup_detail;
        bool popup = popup_check(frame, popup_detail);
        report.check("D01-popup-forward", popup_dpi == second && dpi_messages > before && popup,
                     "Open dropdown across monitor move: WM_DPICHANGED=" + std::to_string(dpi_messages - before) +
                     "; popup inside window and hit=" + std::to_string(popup) + "; " + popup_detail);
        before = dpi_messages;
        UINT popup_return = move(monitors[0]);
        const auto& returned_frame = live.host.frame();
        bool popup_back = popup_check(returned_frame, popup_detail);
        report.check("D01-popup-return", popup_return == first && dpi_messages > before && popup_back,
                     "Return: popup inside window and hit=" + std::to_string(popup_back) + "; " + popup_detail);
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
        presentation_before_input(*appearance, report);
        draw_list_initial(*appearance, report);
        palette_override_native(*appearance, report);
        tabs_native(*appearance, report, folder);
        atlas_native(*appearance, report);
        progress_native(*appearance, report, folder);
        dialog_native(*appearance, report, folder);
        tooltip_native(*appearance, report, folder);
        focus_visibility_native(*appearance, report);
        long_dropdown_native(*appearance, report);
        list_native(*appearance, report);
        nested_list_native(*appearance, report);
        font_diagnostic_reaches_application(*appearance, report);
        registered_component_uses_native_host(*appearance, report, folder);
        checkbox_and_radio_native(*appearance, report);
        slider_native(*appearance, report);
        dropdown_native(*appearance, report);
        space_activation_does_not_leak(*appearance, report);
        enter_activates_once(*appearance, report);
        pointer_activation_rules(*appearance, report);
        pointer_selection_and_clipboard(*appearance, report);
        multiline_native(*appearance, report, folder);
        justify_native(*appearance, report, folder);
        clipboard_contention(*appearance, report);
        tab_walk_and_visible_focus(*appearance, report, folder, tag);
        resize_first_frame_matches_next(*appearance, report, 1.f);
        resize_first_frame_matches_next(*appearance, report, 1.5f);
        resize_first_frame_matches_next(*appearance, report, 1.5f, true);
        layout_matrix(*appearance, report, folder, tag);
        page_size_scale_matrix(*appearance, report);
        popup_dismissal_matrix(*appearance, report);
        semantic_raster_samples(*appearance, report, folder);
        x04_client_size_matrix(*appearance, report);
        x04_fixed_placement_matrix(*appearance, report);
        x04_hidden_matrix(*appearance, report);
        x04_selected_matrix(*appearance, report);
        x04_normal_matrix(*appearance, report);
        x04_executing_matrix(*appearance, report);
        x04_clipped_matrix(*appearance, report);
        x04_error_matrix(*appearance, report);
        x04_candidate_matrix(*appearance, report);
        x04_search_read_only_matrix(*appearance, report);
        x04_modal_matrix(*appearance, report);
        x04_tooltip_matrix(*appearance, report);
        x04_disabled_matrix(*appearance, report);
        x04_group_conflict_matrix(*appearance, report);
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
        x04_client_size_matrix(*appearance, report);
        x04_fixed_placement_matrix(*appearance, report);
        x04_hidden_matrix(*appearance, report);
        x04_selected_matrix(*appearance, report);
        x04_normal_matrix(*appearance, report);
        x04_executing_matrix(*appearance, report);
        x04_clipped_matrix(*appearance, report);
        x04_error_matrix(*appearance, report);
        x04_candidate_matrix(*appearance, report);
        x04_search_read_only_matrix(*appearance, report);
        x04_modal_matrix(*appearance, report);
        x04_tooltip_matrix(*appearance, report);
        x04_disabled_matrix(*appearance, report);
        x04_group_conflict_matrix(*appearance, report);
        x04_name_read_only_matrix(*appearance, report);
        x04_name_conflict_matrix(*appearance, report);
        semantic_observation_sweep(*appearance, folder, report.appearance);
    }
    std::cout << report.checks << " X04 raster checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int run_x04_selection(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-selection.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        x04_selected_matrix(*appearance, report);
        x04_candidate_matrix(*appearance, report);
    }
    std::cout << report.checks << " X04 selection checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int run_x04_disabled(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-disabled.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        x04_disabled_matrix(*appearance, report);
    }
    std::cout << report.checks << " X04 disabled checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int run_x04_executing(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-executing.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        x04_executing_matrix(*appearance, report);
    }
    std::cout << report.checks << " X04 executing checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int run_x04_clipped(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-clipped.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        x04_clipped_matrix(*appearance, report);
    }
    std::cout << report.checks << " X04 clipped checks, " << report.failures << " failures\n";
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

int run_presentation_idle(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "presentation.tsv");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        try {
            Live live(*appearance); Profiles profiles;
            if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo unavailable");
            if (!system_font_catalog().contains(u"Microsoft JhengHei") || !system_font_catalog().contains(u"MingLiU"))
                throw Blocked("PR01 requires installed Microsoft JhengHei and MingLiU");
            auto settings = live.form.ui.presentation(); settings.font_family = u"Microsoft JhengHei";
            if (!live.form.ui.set_presentation(settings)) throw Blocked("initial font setting rejected");
            live.start_in_name(); live.type("JI3");
            auto* session = detail::access(live.form.ui).edit(live.ids.name);
            bool composing = session && session->composing && !session->preedit.empty();
            if (!composing) throw Blocked("Microsoft Bopomofo did not start composition");
            bool positions = composing;
            for (int step = 0; step < 4; ++step) {
                if (step == 0) settings.text_size = 28;
                if (step == 1) settings.font_family = u"MingLiU";
                if (step == 2) settings.variant = ThemeVariant::dark();
                if (step == 3) settings.density = Density::compact;
                positions &= bool(live.form.ui.set_presentation(settings));
                auto* field = live.host.frame().find(live.form.handles.name);
                IMECHARPOSITION position{}; position.dwSize = sizeof(position);
                bool query = SendMessageW(live.host.window(), WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&position)) != 0;
                POINT caret{field ? field->caret_x(session->begin()) : 0, field ? field->bounds.y + field->field.editor.y : 0};
                ClientToScreen(live.host.window(), &caret);
                CANDIDATEFORM candidate{}; bool candidate_ok = false;
                if (auto imc = ImmGetContext(live.host.window())) {
                    candidate.dwIndex = 0;
                    candidate_ok = ImmGetCandidateWindow(imc, 0, &candidate) && field && candidate.ptCurrentPos.x == field->caret_x(session->visible_caret()) && candidate.ptCurrentPos.y == field->bounds.y + field->field.editor.y + field->field.editor.h;
                    ImmReleaseContext(live.host.window(), imc);
                }
                positions &= session->composing && query && position.pt.x == caret.x && position.pt.y == caret.y && candidate_ok;
            }
            live.tap(VK_RETURN); live.wait(250);
            bool committed = session && !session->composing && session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.history() == 1;
            report.check("PR01", composing && positions && committed, profiles.active() + "; IMTCTIP " + file_version(LR"(C:\Windows\System32\IME\IMETC\IMTCTIP.DLL)") + "; candidates and caret=" + std::to_string(positions) + "; single commit and undo=" + std::to_string(committed));
        } catch (const Blocked& error) { report.check("PR01", false, error.what()); }
        dpi_session(*appearance, report, true);
        report.open("PR03", "Install a font for the current user, confirm its catalog entry and local path, and inspect its display in V04; record the evidence manually.");
        report.open("PR04", "Generate trial screenshots and record first-frame font-switch timing and user feedback.");
    }
    CoUninitialize();
    return report.failures ? 1 : 0;
}

int native_presentation_matrix(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "presentation-matrix.tsv");
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        report.appearance = appearance == &web_appearance() ? "web" : "material";
        try {
            presentation_matrix::run(edit_form::default_ids(), *appearance, [&](edit_form::Form& form) {
                DesktopOptions options; options.title = u"Presentation matrix";
                auto host = std::make_shared<DesktopHost>(form.ui, *appearance, [&form] { return form.page(); }, form.scroll, options, false, true);
                return presentation_matrix::Driver{[host, &form](int width, int height, float scale, std::string_view reveal) {
                    return host->layout(width, height, scale, form.scroll, reveal);
                }, [host, &folder, &report](int width, int height, float scale) {
                    auto result = host->compare_gpu(width, height, scale);
                    if (!result) write_comparison_failure(folder, report.appearance + "-matrix-failure", result);
                    return bool(result);
                }};
            }, [](bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); });
            report.check("MX01-shaped", true, "all presentation dimensions and focusable components");
        } catch (const std::exception& error) { report.check("MX01-shaped", false, error.what()); }
    }
    return report.failures ? 1 : 0;
}

int trial_screenshots(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    std::ofstream manifest(folder / "trial.tsv");
    std::ofstream timing(folder / "font-switch-timing.tsv");
    timing << "appearance\tfont\tmilliseconds\tstatus\n";
    manifest << "appearance\tvariant\tdensity\ttext_size\tfile\n";
    for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
        const std::string name = appearance == &web_appearance() ? "web" : "material";
        Fixture f(*appearance, true);
        if (system_font_catalog().contains(u"MingLiU")) {
            auto settings = f.form.ui.presentation(); settings.font_family = u"MingLiU";
            auto start = std::chrono::steady_clock::now();
            bool accepted = bool(f.form.ui.set_presentation(settings));
            f.host.paint();
            auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            timing << name << "\tMingLiU\t" << elapsed << '\t' << (accepted ? "recorded" : "FAIL") << '\n';
            if (!accepted) return 1;
            settings.font_family.clear(); if (!f.form.ui.set_presentation(settings)) return 1;
        } else timing << name << "\tMingLiU\t\tN/A (font absent)\n";
        for (bool dark : {false, true}) for (bool compact : {false, true}) for (int size : {16, 28}) {
            auto settings = f.form.ui.presentation();
            settings.variant = dark ? ThemeVariant::dark() : ThemeVariant::light();
            settings.density = compact ? Density::compact : Density::standard;
            settings.text_size = size;
            if (!f.form.ui.set_presentation(settings) || !f.form.handles.tabs.activate({"v01"})) return 1;
            f.form.scroll.offset = 0;
            auto file = name + "-" + (dark ? "dark" : "light") + "-" + (compact ? "compact" : "standard") + "-" + std::to_string(size) + ".bmp";
            f.host.render(960, 720, 1.f, folder / file);
            manifest << name << '\t' << (dark ? "dark" : "light") << '\t' << (compact ? "compact" : "standard") << '\t' << size << '\t' << file << '\n';
        }
    }
    return manifest && timing ? 0 : 1;
}

// A fresh process is used for each run so startup age and warmup share one origin.
int run_perf(const std::filesystem::path& evidence, const std::wstring& scenario, const std::wstring& appearance,
             bool large, int delay, int duration_ms) {
    if (scenario!=L"startup" && scenario!=L"list-hold-down" && scenario!=L"typing" && scenario!=L"idle")
        throw std::runtime_error("Unknown perf scenario");
    if (appearance!=L"web" && appearance!=L"material") throw std::runtime_error("Unknown perf appearance");
    if (delay<0 || duration_ms<0) throw std::runtime_error("Invalid perf duration or delay");
    const auto& selected_appearance=appearance==L"web" ? web_appearance() : material_appearance();
    edit_form::Form form(edit_form::default_ids(),true);
    DesktopHost host(form.ui,selected_appearance,[&]{return form.page();},form.scroll,Fixture::options());
    const bool offscreen=large && (GetSystemMetrics(SM_CXSCREEN)<3840 || GetSystemMetrics(SM_CYSCREEN)<2160);
    host.perf_enable(delay,offscreen);
    auto previous_proc=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(host.window(),GWLP_WNDPROC));
    x04_original_procs.emplace(host.window(),previous_proc);
    if (!SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&x04_window_proc))) throw std::runtime_error("Perf sizing fixture failed");
    host.force_scale(large && !offscreen ? 2.f : 1.f); host.client_size(1920,1080);
    RECT actual{}; GetClientRect(host.window(),&actual);
    if(actual.right!=(large && !offscreen ? 3840 : 1920) || actual.bottom!=(large && !offscreen ? 2160 : 1080)) throw std::runtime_error("Perf client size mismatch");
    ShowWindow(host.window(),SW_SHOW); SetForegroundWindow(host.window()); SetFocus(host.window()); host.pump();
    if (GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window did not acquire foreground focus");
    if (scenario==L"list-hold-down") {
        if (!form.handles.tabs.activate({"v02"}) || !form.list->handles.hundred.invoke() || !form.list->handles.list.focus())
            throw std::runtime_error("Perf list setup failed");
    } else if (scenario==L"typing" || scenario==L"idle") {
        if (!form.handles.tabs.activate({"v01"}) || !form.handles.name.focus()) throw std::runtime_error("Perf typing setup failed");
    }
    host.pump();
    if(scenario!=L"startup") host.perf_reset();
    auto send=[&](WORD key, DWORD flags) { if(GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window lost foreground focus"); INPUT input{}; input.type=INPUT_KEYBOARD; input.ki.wVk=(flags & KEYEVENTF_UNICODE)?0:key; input.ki.wScan=(flags & KEYEVENTF_UNICODE)?key:0; input.ki.dwFlags=flags; if(SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("SendInput failed"); };
    auto wait=[&](int milliseconds) { auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds); while(std::chrono::steady_clock::now()<end) { host.pump(); auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count(); if(remaining>0) MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(remaining),QS_ALLINPUT,MWMO_INPUTAVAILABLE); } host.pump(); };
    if(scenario==L"list-hold-down") {
        int duration=duration_ms ? duration_ms : 5000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration);
        while(std::chrono::steady_clock::now()<end) { send(VK_DOWN,0); wait(33); } send(VK_DOWN,KEYEVENTF_KEYUP); host.pump();
    } else if(scenario==L"typing") {
        int duration=duration_ms ? duration_ms : 10000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration); int i=0;
        while(std::chrono::steady_clock::now()<end) { WORD c=(i++%2)?0x4e2d:L'A'; send(c,KEYEVENTF_UNICODE); send(c,KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(50); }
    }
    int paints=0,presents=0; double cpu_ms=0;
    if(scenario==L"idle") {
        send(L'A',KEYEVENTF_UNICODE); send(L'A',KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(1500);
        int before_paints=host.paint_messages(), before_frames=host.frames(); FILETIME c{},e{},k1{},u1{},k2{},u2{};
        GetProcessTimes(GetCurrentProcess(),&c,&e,&k1,&u1); wait(duration_ms ? duration_ms : 10000); GetProcessTimes(GetCurrentProcess(),&c,&e,&k2,&u2);
        auto ticks=[](FILETIME t){ULARGE_INTEGER x{}; x.LowPart=t.dwLowDateTime; x.HighPart=t.dwHighDateTime; return x.QuadPart;};
        cpu_ms=double(ticks(k2)-ticks(k1)+ticks(u2)-ticks(u1))/10000.; paints=host.paint_messages()-before_paints; presents=host.frames()-before_frames;
    }
    auto warm=host.perf_samples().summary(true), steady=host.perf_samples().summary(false);
    bool ok=scenario==L"startup" ? host.first_present_ms()>=0 && host.first_present_ms()<=1000 : scenario==L"idle" ? detail::idle_pass(presents,paints,cpu_ms) : steady.count>0 && (delay ? steady.over_budget>0 : steady.maximum<=16.7);
    PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) throw std::runtime_error("Cannot read process memory");
    std::filesystem::create_directories(evidence); std::ofstream out(evidence/"perf.tsv");
    out << "scenario\tappearance\tresolution\tscale\trenderer_driver\twindows_build\tconfiguration\tmode\tphase\tcount\tp50_ms\tp95_ms\tp99_ms\tmax_ms\tover_16_7\tstartup_ms\tinput_present_ms\tpresents\twm_paint\tcpu_ms\tdelay_ms\tpeak_working_set_bytes\trun_kind\tstatus\n";
    for (auto phase : {true,false}) { const auto& v=phase?warm:steady;
        out << narrow(scenario) << '\t' << narrow(appearance) << '\t' << (large?"3840x2160":"1920x1080") << '\t' << (large?2:1) << '\t' << host.renderer_name() << '\t' << windows_build() << '\t'
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << '\t' << (offscreen?"offscreen-no-compositor":"window") << '\t' << (phase?"warmup":"steady") << '\t' << v.count << '\t' << v.p50 << '\t' << v.p95 << '\t' << v.p99 << '\t' << v.maximum << '\t' << v.over_budget << '\t' << host.first_present_ms() << '\t' << host.input_present_ms() << '\t' << presents << '\t' << paints << '\t' << cpu_ms << '\t' << delay << '\t' << memory.PeakWorkingSetSize << '\t' << (duration_ms?"smoke":"measurement") << '\t' << (ok?"PASS":"FAIL") << '\n';
    }
    if(!out) throw std::runtime_error("Cannot write perf report");
    SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous_proc)); x04_original_procs.erase(host.window());
    return ok?0:1;
}

int run_demo_perf(const std::filesystem::path& evidence, const std::wstring& scenario, const std::wstring& appearance,
             bool large, int delay, int duration_ms, bool alternate_font, const char* report_file="demo-perf.tsv") {
    // #73: slider-drag-text-size only reports per-frame and per-stage times; it never decides pass or fail.
    const bool slider_drag=scenario==L"slider-drag-text-size";
    if (scenario!=L"startup" && scenario!=L"list-hold-down" && scenario!=L"typing" && scenario!=L"idle" && !slider_drag)
        throw std::runtime_error("Unknown perf scenario");
    if (appearance!=L"web" && appearance!=L"material") throw std::runtime_error("Unknown perf appearance");
    if (delay<0 || duration_ms<0) throw std::runtime_error("Invalid perf duration or delay");
    const auto& selected_appearance=appearance==L"web" ? web_appearance() : material_appearance();
    settings_demo::Demo form;
    if (alternate_font) {
        auto document=form.document();
        const auto& fonts=system_font_catalog().families();
        auto found=std::find_if(fonts.begin(),fonts.end(),[](const auto& family) { return family.key!=u"Segoe UI" && !family.key.empty(); });
        if(found==fonts.end()) throw std::runtime_error("No non-default font available");
        document.presentation.font_family=found->key; form.load_document(document);
    }
    if (scenario==L"list-hold-down") {
        auto document=form.document();
        while(document.items.size()<100) { int n=int(document.items.size())+1; auto id=std::to_string(1000+n); document.items.push_back({OptionValue{"T"+id},u"Performance item",u""}); }
        document.next_item_id=1101; form.load_document(document);
    }
    DesktopHost host(form.ui,selected_appearance,[&]{return form.page();},form.scroll,Fixture::options());
    const bool offscreen=large && (GetSystemMetrics(SM_CXSCREEN)<3840 || GetSystemMetrics(SM_CYSCREEN)<2160);
    if (slider_drag && offscreen) throw std::runtime_error("slider-drag-text-size needs real mouse input, so it cannot run offscreen");
    host.perf_enable(delay,offscreen);
    auto previous_proc=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(host.window(),GWLP_WNDPROC));
    x04_original_procs.emplace(host.window(),previous_proc);
    if (!SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&x04_window_proc))) throw std::runtime_error("Perf sizing fixture failed");
    host.force_scale(large && !offscreen ? 2.f : 1.f); host.client_size(1920,1080);
    RECT actual{}; GetClientRect(host.window(),&actual);
    if(actual.right!=(large && !offscreen ? 3840 : 1920) || actual.bottom!=(large && !offscreen ? 2160 : 1080)) throw std::runtime_error("Perf client size mismatch");
    ShowWindow(host.window(),SW_SHOW); SetForegroundWindow(host.window()); SetFocus(host.window()); host.pump();
    if (GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window did not acquire foreground focus");
    if (scenario==L"list-hold-down") {
        if (!form.nav.select({"items"}) || !form.items_list.focus())
            throw std::runtime_error("Perf list setup failed");
    } else if (scenario==L"typing" || scenario==L"idle") {
        if (!form.nav.select({"general"}) || !form.name.focus()) throw std::runtime_error("Perf typing setup failed");
    } else if (slider_drag) {
        if (!form.nav.select({"appearance"})) throw std::runtime_error("Perf slider setup failed");
    }
    host.pump();
    if(scenario!=L"startup") host.perf_reset();
    auto send=[&](WORD key, DWORD flags) { if(GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window lost foreground focus"); INPUT input{}; input.type=INPUT_KEYBOARD; input.ki.wVk=(flags & KEYEVENTF_UNICODE)?0:key; input.ki.wScan=(flags & KEYEVENTF_UNICODE)?key:0; input.ki.dwFlags=flags; if(SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("SendInput failed"); };
    auto wait=[&](int milliseconds) { auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds); while(std::chrono::steady_clock::now()<end) { host.pump(); auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count(); if(remaining>0) MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(remaining),QS_ALLINPUT,MWMO_INPUTAVAILABLE); } host.pump(); };
    if(scenario==L"list-hold-down") {
        int duration=duration_ms ? duration_ms : 5000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration);
        while(std::chrono::steady_clock::now()<end) { send(VK_DOWN,0); wait(33); } send(VK_DOWN,KEYEVENTF_KEYUP); host.pump();
    } else if(slider_drag) {
        const std::string slider_id="appearance.text_size";
        auto thumb=[&]() -> POINT {
            const auto* item=host.frame().find(form.text_size);
            if (!item) { host.paint(); item=host.frame().find(form.text_size); }
            if (!item || item->slider_track.w<=0) throw std::runtime_error("Perf slider track not arranged");
            const auto& track=item->slider_track; int value=*form.text_size.draft();
            return {track.x+static_cast<int>(std::lround(track.w*(value-12)/16.0)), track.y+track.h/2};
        };
        auto mouse=[&](POINT client, DWORD flags) {
            if (GetForegroundWindow()!=host.window()) throw std::runtime_error("Perf window lost foreground focus");
            POINT screen=client; ClientToScreen(host.window(),&screen);
            const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN), vy=GetSystemMetrics(SM_YVIRTUALSCREEN), vw=GetSystemMetrics(SM_CXVIRTUALSCREEN), vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
            INPUT input{}; input.type=INPUT_MOUSE;
            input.mi.dx=static_cast<LONG>((static_cast<long long>(screen.x-vx)*65535+(vw-1)/2)/(vw-1));
            input.mi.dy=static_cast<LONG>((static_cast<long long>(screen.y-vy)*65535+(vh-1)/2)/(vh-1));
            input.mi.dwFlags=flags|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
            if (SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("SendInput failed");
        };
        auto start_point=thumb();
        mouse(start_point,MOUSEEVENTF_MOVE); wait(100);
        POINT cursor{}; GetCursorPos(&cursor); POINT want=start_point; ClientToScreen(host.window(),&want);
        if (std::abs(cursor.x-want.x)>3 || std::abs(cursor.y-want.y)>3) throw std::runtime_error("Perf cursor did not reach the slider thumb");
        mouse(start_point,MOUSEEVENTF_LEFTDOWN); wait(50);
        if (!detail::access(form.ui).slider_drag_x(slider_id)) throw std::runtime_error("Perf slider drag did not start");
        host.perf_reset();
        // About 16 ms per move, sweeping the whole track back and forth every 2 s.
        int duration=duration_ms ? duration_ms : 5000; auto begin=std::chrono::steady_clock::now(), until=begin+std::chrono::milliseconds(duration);
        while(std::chrono::steady_clock::now()<until) {
            double t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/2000.0;
            double phase=t-std::floor(t), ratio=phase<.5 ? phase*2 : 2-phase*2;
            const auto* item=host.frame().find(form.text_size);
            if (item && item->slider_track.w>0) mouse({item->slider_track.x+static_cast<int>(std::lround(item->slider_track.w*ratio)), item->slider_track.y+item->slider_track.h/2},MOUSEEVENTF_MOVE);
            wait(16);
        }
        POINT release_point=start_point;
        if (const auto* item=host.frame().find(form.text_size)) release_point={item->slider_track.x, item->slider_track.y+item->slider_track.h/2};
        mouse(release_point,MOUSEEVENTF_LEFTUP); wait(100);
    } else if(scenario==L"typing") {
        int duration=duration_ms ? duration_ms : 10000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration); int i=0;
        while(std::chrono::steady_clock::now()<end) {
            // Keep producing edits after the demo name field reaches its 64-character limit.
            if (i && i%50==0) { send(VK_CONTROL,0); send('A',0); send('A',KEYEVENTF_KEYUP); send(VK_CONTROL,KEYEVENTF_KEYUP); }
            WORD c=(i++%2)?0x4e2d:L'A'; send(c,KEYEVENTF_UNICODE); send(c,KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(50);
        }
    }
    int paints=0,presents=0; double cpu_ms=0;
    if(scenario==L"idle") {
        send(L'A',KEYEVENTF_UNICODE); send(L'A',KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(1500);
        int before_paints=host.paint_messages(), before_frames=host.frames(); FILETIME c{},e{},k1{},u1{},k2{},u2{};
        GetProcessTimes(GetCurrentProcess(),&c,&e,&k1,&u1); wait(duration_ms ? duration_ms : 10000); GetProcessTimes(GetCurrentProcess(),&c,&e,&k2,&u2);
        auto ticks=[](FILETIME t){ULARGE_INTEGER x{}; x.LowPart=t.dwLowDateTime; x.HighPart=t.dwHighDateTime; return x.QuadPart;};
        cpu_ms=double(ticks(k2)-ticks(k1)+ticks(u2)-ticks(u1))/10000.; paints=host.paint_messages()-before_paints; presents=host.frames()-before_frames;
    }
    auto warm=host.perf_samples().summary(true), steady=host.perf_samples().summary(false);
    bool ok=slider_drag ? true : scenario==L"startup" ? host.first_present_ms()>=0 && host.first_present_ms()<=1000 : scenario==L"idle" ? detail::idle_pass(presents,paints,cpu_ms) : steady.count>0 && (delay ? steady.over_budget>0 : steady.maximum<=16.7);
    PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) throw std::runtime_error("Cannot read process memory");
    if (slider_drag && warm.count+steady.count==0) throw std::runtime_error("Perf slider drag produced no frames");
    auto stages_warm=host.perf_breakdown().summary(true), stages_steady=host.perf_breakdown().summary(false);
    std::filesystem::create_directories(evidence); std::ofstream out(evidence/report_file);
    out << "scenario\tappearance\tfont_mode\tresolution\tscale\trenderer_driver\twindows_build\tconfiguration\tmode\tphase\tcount\tp50_ms\tp95_ms\tp99_ms\tmax_ms\tover_16_7\tstartup_ms\tinput_present_ms\tpresents\twm_paint\tcpu_ms\tdelay_ms\tpeak_working_set_bytes\trun_kind\tstatus";
    if (slider_drag) for (const char* stage : {"layout","list","gpu"}) for (const char* column : {"p50","p95","p99","max"}) out << '\t' << stage << '_' << column << "_ms";
    out << '\n';
    for (auto phase : {true,false}) { const auto& v=phase?warm:steady;
        out << narrow(scenario) << '\t' << narrow(appearance) << '\t' << (alternate_font?"non-default":"default") << '\t' << (large?"3840x2160":"1920x1080") << '\t' << (large?2:1) << '\t' << host.renderer_name() << '\t' << windows_build() << '\t'
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << '\t' << (offscreen?"offscreen-no-compositor":"window") << '\t' << (phase?"warmup":"steady") << '\t' << v.count << '\t' << v.p50 << '\t' << v.p95 << '\t' << v.p99 << '\t' << v.maximum << '\t' << v.over_budget << '\t' << host.first_present_ms() << '\t' << host.input_present_ms() << '\t' << presents << '\t' << paints << '\t' << cpu_ms << '\t' << delay << '\t' << memory.PeakWorkingSetSize << '\t' << (duration_ms?"smoke":"measurement") << '\t' << (slider_drag?"REPORT":ok?"PASS":"FAIL");
        if (slider_drag) {
            const auto& stages=phase?stages_warm:stages_steady;
            for (const auto* stage : {&stages.layout,&stages.list,&stages.gpu}) out << '\t' << stage->p50 << '\t' << stage->p95 << '\t' << stage->p99 << '\t' << stage->maximum;
        }
        out << '\n';
    }
    if(!out) throw std::runtime_error("Cannot write perf report");
    SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous_proc)); x04_original_procs.erase(host.window());
    return ok?0:1;
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
        bool multiline_only = false, justify_only = false;
        bool dl01_only = false, trial = false, matrix = false, presentation = false, backend_check = false, r06_only = false, dd_only = false;
        bool uia_ime = false, demo_comparison = false, demo_perf = false, demo_perf_font = false;
        bool ime = false, clipboard_only = false, dpi = false, x02_only = false, x04_only = false, x04_clipped_only = false, x04_executing_only = false, x04_disabled_only = false, x04_selection_only = false;
        int repeat = 1, perf_delay=0, perf_duration=0; bool perf_large=false;
        std::wstring perf, perf_appearance=L"web";
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--demo-perf" && i+1<argc) { demo_perf=true; perf=argv[++i]; }
            else if (arg == L"--perf" && i+1<argc) perf=argv[++i];
            else if (arg == L"--demo-perf-font") demo_perf_font=true;
            else if (arg == L"--perf-appearance" && i+1<argc) perf_appearance=argv[++i];
            else if (arg == L"--perf-large") perf_large=true;
            else if (arg == L"--paint-delay-ms" && i+1<argc) perf_delay=std::stoi(argv[++i]);
            else if (arg == L"--perf-duration-ms" && i+1<argc) perf_duration=std::stoi(argv[++i]);
            else if (arg == L"--evidence" && i + 1 < argc) evidence = argv[++i];
            else if (arg == L"--dl01-only" || arg == L"--render-compare") dl01_only = true;
            else if (arg == L"--demo-compare") demo_comparison = true;
            else if (arg == L"--trial-screenshots") trial = true;
            else if (arg == L"--presentation-matrix") matrix = true;
            else if (arg == L"--presentation-idle") presentation = true;
            else if (arg == L"--backend-check") backend_check = true;
            else if (arg == L"--r06-only") r06_only = true;
            else if (arg == L"--dd-only") dd_only = true;
            else if (arg == L"--multiline-only") multiline_only = true;
            else if (arg == L"--justify-only") justify_only = true;
            else if (arg == L"--ime") ime = true;
            else if (arg == L"--uia-ime") uia_ime = true;
            else if (arg == L"--dpi") dpi = true;
            else if (arg == L"--clipboard-only") clipboard_only = true;
            else if (arg == L"--x02-only") x02_only = true;
            else if (arg == L"--x04-only") x04_only = true;
            else if (arg == L"--x04-clipped-only") x04_clipped_only = true;
            else if (arg == L"--x04-executing-only") x04_executing_only = true;
            else if (arg == L"--x04-disabled-only") x04_disabled_only = true;
            else if (arg == L"--x04-selection-only") x04_selection_only = true;
            else if (arg == L"--repeat" && i + 1 < argc) repeat = std::stoi(argv[++i]);
            else throw std::runtime_error("Usage: lcars_host_verify [--ime | --dpi | --x02-only | --x04-only | --x04-clipped-only | --x04-executing-only | --x04-disabled-only | --x04-selection-only | --clipboard-only [--repeat N]] [--evidence DIR]");
        }
        if (demo_comparison) return demo_compare(evidence);
        if (uia_ime) return run_uia_ime(evidence, perf_appearance);
        if (dd_only) {
            std::filesystem::create_directories(evidence); Report report(evidence / "dd.tsv");
            for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
                report.appearance = appearance == &web_appearance() ? "web" : "material";
                long_dropdown_native(*appearance, report);
            }
            return report.failures ? 1 : 0;
        }
        if (multiline_only) {  // #79 native checks alone, for quick runs
            std::filesystem::create_directories(evidence); Report report(evidence / "multiline.tsv");
            for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
                report.appearance = appearance == &web_appearance() ? "web" : "material";
                multiline_native(*appearance, report, evidence);
            }
            return report.failures ? 1 : 0;
        }
        if (justify_only) {  // #84 native checks alone, for quick runs
            std::filesystem::create_directories(evidence); Report report(evidence / "justify.tsv");
            for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
                report.appearance = appearance == &web_appearance() ? "web" : "material";
                justify_native(*appearance, report, evidence);
            }
            return report.failures ? 1 : 0;
        }
        if (presentation) return run_presentation_idle(evidence);
        if (trial) return trial_screenshots(evidence);
        if (matrix) return native_presentation_matrix(evidence);
        if (r06_only) {
            std::filesystem::create_directories(evidence);
            Report report(evidence / "r06.tsv");
            for (const auto* appearance : {&web_appearance(), &material_appearance()}) {
                report.appearance = appearance == &web_appearance() ? "web" : "material";
                resize_first_frame_matches_next(*appearance, report, 1.f);
                resize_first_frame_matches_next(*appearance, report, 1.5f);
                resize_first_frame_matches_next(*appearance, report, 1.5f, true);
            }
            return report.failures ? 1 : 0;
        }
        if (backend_check) {
            Fixture fixture(web_appearance(), true);
            const auto renderer = fixture.host.renderer_name();
            std::cout << "OpenGL renderer: " << renderer << '\n';
            if (renderer.find("llvmpipe") == std::string::npos) {
                std::cerr << "Expected Mesa llvmpipe\n";
                return 1;
            }
            return 0;
        }
        if (demo_perf) return run_demo_perf(evidence,perf,perf_appearance,perf_large,perf_delay,perf_duration,demo_perf_font);
        // #73: this scenario drives the demo's Appearance page, so --perf routes it to the demo runner.
        if (perf==L"slider-drag-text-size") return run_demo_perf(evidence,perf,perf_appearance,perf_large,perf_delay,perf_duration,false,"perf.tsv");
        if (!perf.empty()) return run_perf(evidence,perf,perf_appearance,perf_large,perf_delay,perf_duration);
        if (dl01_only) { std::filesystem::create_directories(evidence); Report report(evidence / "dl01.tsv"); std::ofstream gpu_tsv(evidence/"render-compare.tsv"); gpu_tsv << "scenario\tappearance\tsize\tscale\trenderer\twindows_build\tconfiguration\tdifferent_fraction\tmaximum_difference\trole_mask\tfailed_role_mask\tregion_results\tregions_ok\tstatus\n"; for (const auto* appearance : {&web_appearance(), &material_appearance()}) { report.appearance = appearance == &web_appearance() ? "web" : "material"; draw_list_initial(*appearance, report, &gpu_tsv, evidence); gpu_draw_list_smoke(*appearance, report, evidence); } gpu_tsv.flush(); bool written=bool(gpu_tsv); gpu_tsv.close(); std::ifstream verify(evidence/"render-compare.tsv"); std::string line; int lines=0; bool header=false; while(std::getline(verify,line)){ if(lines==0)header=line.find("scenario\tappearance\tsize\tscale\trenderer\twindows_build\tconfiguration\tdifferent_fraction\tmaximum_difference\trole_mask\tfailed_role_mask\tregion_results\tregions_ok\tstatus")!=std::string::npos; ++lines; } report.check("GC05-tsv", written && header && lines==41, "complete comparison table has 40 rows and required columns"); return report.failures ? 1 : 0; }
        if (repeat < 1 || (repeat != 1 && !clipboard_only) || int(ime) + int(clipboard_only) + int(dpi) + int(x02_only) + int(x04_only) + int(x04_clipped_only) + int(x04_executing_only) + int(x04_disabled_only) + int(x04_selection_only) > 1)
            throw std::runtime_error("Invalid verification options");
        return ime ? run_ime(evidence) : dpi ? run_dpi(evidence) : clipboard_only ? run_clipboard(evidence, repeat) : x02_only ? run_x02(evidence) : x04_only ? run_x04(evidence) : x04_clipped_only ? run_x04_clipped(evidence) : x04_executing_only ? run_x04_executing(evidence) : x04_disabled_only ? run_x04_disabled(evidence) : x04_selection_only ? run_x04_selection(evidence) : run(evidence);
    } catch (const std::exception& e) {
        std::cerr << "host verification error: " << e.what() << '\n';
        return 1;
    }
}
