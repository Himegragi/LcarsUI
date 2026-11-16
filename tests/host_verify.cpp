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
editForm::Ids alternateIds() {
    editForm::Ids i;
    i.title = "t"; i.intro = "i"; i.normal = "f1"; i.readOnly = "f2"; i.disabled = "f3"; i.error = "f4";
    i.longText = "f5"; i.reset = "f6"; i.nameLabel = "n.l"; i.name = "n"; i.nameHelp = "n.h"; i.nameError = "n.e";
    i.volumeLatest = "volume.latest"; i.volume = "volume"; i.notices = "notices"; i.mode = "mode"; i.offlineDisabled = "offline";
    i.region = "region.alt"; i.regionEmpty = "region.empty.alt";
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
LPARAM keyLparam(WPARAM vk, bool repeat, bool up) {
    LPARAM l = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC)) << 16);
    if (repeat || up) l |= 1LL << 30;
    if (up) l |= 1LL << 31;
    return l;
}
// X04 samples must have their requested physical client size even when the test
// desktop's default maximum tracking size is smaller. Keep the host's normal input
// procedure and alter only this fixture window's size limits.
std::map<HWND, WNDPROC> x04OriginalProcs;
LRESULT CALLBACK x04WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto found = x04OriginalProcs.find(window);
    if (found == x04OriginalProcs.end()) return DefWindowProcW(window, message, w, l);
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
    editForm::Form form;
    DesktopHost host;
    const editForm::Ids& ids;
    WNDPROC x04OriginalProc = nullptr;
    explicit Fixture(const Appearance& appearance, bool pages = false, bool alternate = false)
        : form(alternate ? alternateIds() : editForm::defaultIds(), pages), host(form.ui, appearance, [this] { return form.page(); }, form.scroll, options(), true, true), ids(form.ids()) {
        ShowWindow(host.window(), SW_SHOWNOACTIVATE);
        host.forceScale(1.f);
        host.clientSize(900, 700);
        settle();
    }
    ~Fixture() {
        if (x04OriginalProc) {
            SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(x04OriginalProc));
            x04OriginalProcs.erase(host.window());
        }
    }
    void x04Size(int widthDip, int heightDip, float scale) {
        if (!x04OriginalProc) {
            auto previous = GetWindowLongPtrW(host.window(), GWLP_WNDPROC);
            if (!previous) throw std::runtime_error("Cannot read X04 window procedure");
            x04OriginalProc = reinterpret_cast<WNDPROC>(previous);
            x04OriginalProcs.emplace(host.window(), x04OriginalProc);
            if (!SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&x04WindowProc))) {
                x04OriginalProcs.erase(host.window()); x04OriginalProc = nullptr;
                throw std::runtime_error("Cannot install X04 sizing fixture");
            }
        }
        host.forceScale(scale); host.clientSize(widthDip, heightDip);
        RECT actual{}; GetClientRect(host.window(), &actual);
        int expectedWidth = int(std::lround(widthDip * scale));
        int expectedHeight = int(std::lround(heightDip * scale));
        if (actual.right != expectedWidth || actual.bottom != expectedHeight)
            throw std::runtime_error("X04 client size mismatch: expected " + std::to_string(expectedWidth) + "x" +
                std::to_string(expectedHeight) + ", got " + std::to_string(actual.right) + "x" + std::to_string(actual.bottom));
    }
    static DesktopConfig options() { DesktopConfig o; o.title = u"LcarsUI host verification"; return o; }
    void settle() { host.pump(); host.paint(); }
    void post(UINT msg, WPARAM w, LPARAM l) { PostMessageW(host.window(), msg, w, l); }
    void key(WPARAM vk, bool repeat = false) { post(WM_KEYDOWN, vk, keyLparam(vk, repeat, false)); host.pump(); }
    void keyUp(WPARAM vk) { post(WM_KEYUP, vk, keyLparam(vk, false, true)); host.pump(); }
    void tap(WPARAM vk) { key(vk); keyUp(vk); }
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
        auto r = detail::findPlacement(host.frame(), id)->bounds;
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

void atlasNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    f.host.paint();
    auto before = f.host.rasterizations();
    for (int i=0; i<100; ++i) f.host.paint();
    report.check("GA01", f.host.rasterizations()==before, "100 repeated frames reuse rasterized glyphs");
    auto clears=f.host.atlasClears();
    f.host.forceScale(1.5f); f.host.paint();
    bool forward=f.host.atlasClears()==clears+1 && f.host.compareLegacy(960,720,1.5f);
    f.host.forceScale(1.f); f.host.paint();
    report.check("GA02", forward && f.host.atlasClears()==clears+2 && f.host.compareLegacy(960,720,1.f), "scale changes clear once and retain S1 pixels");
    f.host.configureAtlas(64,1);
    bool same=f.host.compareLegacy(960,720,1.f);
    report.check("GA03-CPU", same && f.host.atlasBatches()>0 && f.host.atlasPeakPages()<=1, "small atlas batches retain S1 pixels within one page");
    f.host.configureAtlas(1024,4);
    auto settings=f.form.ui.presentation(); settings.textSize=18;
    f.form.ui.setPresentation(settings); f.host.paint();
    auto previous=f.host.atlasClears(); settings.textSize=24;
    bool changed=bool(f.form.ui.setPresentation(settings)); f.host.paint();
    bool invalidated=f.host.atlasClears()==previous+1 && f.host.compareLegacy(960,720,1.f);
    detail::Text text;
    auto oldLayout=text.shape(u"Station",18);
    detail::RecordingCanvas oldCanvas(300,80,{0,0,0});
    text.draw(oldCanvas,oldLayout,0,40,{255,255,255});
    auto count=text.atlasClears(); text.clearAtlas();
    auto newLayout=text.shape(u"Station",24);
    detail::RecordingCanvas newCanvas(300,80,{0,0,0});
    text.draw(newCanvas,newLayout,0,40,{255,255,255});
    detail::Canvas reference(300,80,{0,0,0});
    text.draw(reference,newLayout,0,40,{255,255,255});
    report.check("GA04", changed && invalidated && text.atlasClears()==count+1 &&
        detail::renderCpu(newCanvas.list,300,80).pixels==reference.pixels &&
        detail::renderCpu(oldCanvas.list,300,80).pixels!=reference.pixels,
        "font-size invalidation produces the 24 DIP reference");
}

void presentationBeforeInput(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.name.focus(); h.name.select(1, 1); f.settle();
    auto settings = f.form.ui.presentation(); settings.textSize = 28;
    auto result = f.form.ui.setPresentation(settings);
    auto* field = f.host.frame().find(h.name);
    IMECHARPOSITION position{}; position.dwSize = sizeof(position);
    bool query = SendMessageW(f.host.window(), WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&position)) != 0;
    POINT expected{field ? field->caretX(1) : 0, field ? field->bounds.y + field->textField.editor.y : 0};
    ClientToScreen(f.host.window(), &expected);
    bool geometry = field && field->textSize == 28 && query && position.pt.x == expected.x && position.pt.y == expected.y;
    int x = field ? field->caretX(2) : 0;
    int y = field ? field->bounds.y + field->textField.editor.y + field->textField.editor.h / 2 : 0;
    SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    auto* session = detail::access(f.form.ui).edit(f.ids.name);
    report.check("PS09", bool(result) && geometry && session && session->selection.caret == 2,
                 "IME query and pointer use updated layout before paint");
}

void writeComparisonBmp(const std::filesystem::path& path, const Image& image) {
    auto pixels=image.pixels;
    for(std::size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    info.biSize=sizeof(info);info.biWidth=image.width;info.biHeight=-image.height;info.biPlanes=1;info.biBitCount=32;info.biSizeImage=static_cast<DWORD>(pixels.size());
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    if(!out)throw std::runtime_error("comparison image write failed");
}
void writeComparisonFailure(const std::filesystem::path& folder, const std::string& name, const detail::GpuComparison& result) {
    std::filesystem::create_directories(folder);
    writeComparisonBmp(folder/(name+"-cpu.bmp"),result.reference);
    writeComparisonBmp(folder/(name+"-gpu.bmp"),result.actual);
    Image diff{result.reference.width,result.reference.height,std::vector<unsigned char>(result.reference.pixels.size(),255)};
    for(std::size_t i=0;i<diff.pixels.size();i+=4){
        int delta=0;for(int c=0;c<4;++c)delta=std::max(delta,std::abs(int(result.reference.pixels[i+c])-int(result.actual.pixels[i+c])));
        diff.pixels[i]=static_cast<unsigned char>(delta);diff.pixels[i+1]=0;diff.pixels[i+2]=0;
    }
    writeComparisonBmp(folder/(name+"-diff.bmp"),diff);
}

void emitFailureIfNeeded(const std::filesystem::path& folder, const std::string& name, const detail::GpuComparison& result, bool passed) {
    if(!passed)writeComparisonFailure(folder,name,result);
}

// The same recorded frame must be executed by the graphics backend offscreen.
void gpuDrawListSmoke(const Appearance& appearance, Report& report, const std::filesystem::path& evidence) {
    Fixture f(appearance, true);
    f.host.forceScale(1.f);
    f.settle();
    f.form.handles.name.focus();
    report.check("GPU-draw-list", bool(f.host.compareGpu(960, 720, 1.f)),
                 "offscreen GPU execution agrees with the CPU reference");
    auto dark = f.form.ui.presentation(); dark.variant = ThemeVariant::dark();
    bool accepted = bool(f.form.ui.setPresentation(dark));
    auto darkImage = f.host.compareGpu(960, 720, 1.f);
    auto background = appearance.palette(dark.variant).background;
    report.check("GPU-dark-background", accepted && bool(darkImage) &&
        darkImage.reference.pixels[0] == background.r && darkImage.reference.pixels[1] == background.g &&
        darkImage.reference.pixels[2] == background.b, "comparison uses the selected dark background");
    auto shifted=f.host.compareGpu(960,720,1.f,1);
    auto missing=f.host.compareGpu(960,720,1.f,2);
    report.check("GC03-caret", !shifted && (shifted.failedRoles & 2), "one-pixel caret shift is detected in the caret region");
    report.check("GC03-focus", !missing && (missing.failedRoles & 1), "missing focus indicator is detected in the focus region");
    auto key=report.appearance+"-injected";
    emitFailureIfNeeded(evidence,key,shifted,bool(shifted));
    bool images=std::filesystem::exists(evidence/(key+"-cpu.bmp")) && std::filesystem::exists(evidence/(key+"-gpu.bmp")) && std::filesystem::exists(evidence/(key+"-diff.bmp"));
    report.check("GC05-images", images, "failed comparison writes reference, GPU and difference images");
    f.host.configureAtlas(64,1);
    report.check("GA03-GPU", bool(f.host.compareGpu(960,720,1.f)) && f.host.atlasPeakPages()<=1,
                 "bounded atlas segmentation preserves GPU output");
}

// #78: the desktop host paints with the Ui's palette overrides, and the semantic layer reads the same colors.
void paletteOverrideNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    f.host.forceScale(1.f);
    f.settle();
    auto original = appearance.palette().background;
    const Color changed{240, 240, 240};
    auto firstPixel = [](const auto& image) { return Color{image.reference.pixels[0], image.reference.pixels[1], image.reference.pixels[2]}; };
    auto before = f.host.compareGpu(960, 720, 1.f);
    report.check("PO-before", bool(before) && firstPixel(before) == original, "built-in background paints before any override");
    auto result = f.form.ui.setPaletteOverrides({{std::string(appearance.name()), ThemeVariant::light(), "background", changed}});
    auto after = f.host.compareGpu(960, 720, 1.f);
    report.check("PO-override", bool(result) && bool(after) && firstPixel(after) == changed, "the next host frame paints the overridden background and the GPU agrees");
    auto semantic = f.form.ui.semantics().effectivePalette();
    report.check("PO-semantic", semantic && semantic->background == changed, "the host binds its appearance so the semantic layer reads the overridden palette");
    report.check("PO-clear", bool(f.form.ui.setPaletteOverrides({})) && firstPixel(f.host.compareGpu(960, 720, 1.f)) == original, "clearing the list restores the built-in background");
}

std::string windowsBuild() {
    using QueryVersion=LONG (WINAPI*)(OSVERSIONINFOW*);
    auto proc=reinterpret_cast<QueryVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
    return proc && proc(&version)==0 ? std::to_string(version.dwBuildNumber) : "unknown";
}
void drawListInitial(const Appearance& appearance, Report& report, std::ofstream* gpuTsv = nullptr, const std::filesystem::path& evidence = {}) {
    const char* names[] = {"v01-a","v01-b","v01-c","v01-d","v01-e","v02-a","v02-b","v02-c","v03-a","v03-b"};
    for (int scenario=0; scenario<10; ++scenario) for (float scale : {1.f,1.5f}) {
        Fixture f(appearance, true);
        f.host.forceScale(scale);
        f.host.clientSize(scale == 1.f ? 960 : 640, scale == 1.f ? 720 : 480);
        f.settle();
        auto& h=f.form.handles; auto& state=detail::access(f.form.ui);
        bool setup=true;
        if(scenario<5) {
            setup=bool(h.name.focus());
            if(scenario==1) setup=setup && bool(state.select(f.ids.name,0,2));
            if(scenario==2) setup=setup && bool(state.compositionUpdate(u"\u3118\u311c\u02cb",3));
            if(scenario==3) { setup=setup && bool(h.name.setValue(u"")); h.apply.invoke(); setup=setup && h.name.describe()->invalid; }
            if(scenario==4) { setup=bool(h.region.focus()) && bool(h.region.open()); f.tap(VK_DOWN); f.tap(VK_DOWN); setup=setup && h.region.describe()->candidate==OptionValue{"south"}; }
        } else if(scenario<8) {
            setup=bool(h.tabs.activate({"v02"})); auto& list=*f.form.list;
            state.setSystemKeyboardCues(true);  // indicator shown, as after a key press; programmatic focus alone does not show it
            if(scenario==5) setup=setup && bool(list.handles.twenty.invoke()) && bool(list.handles.list.select({"T003"})) && bool(list.handles.list.focus());
            if(scenario==6) { setup=setup && bool(list.handles.hundred.invoke()) && bool(list.handles.list.focus()); f.tap(VK_END); setup=setup && list.selection()==OptionValue{"T100"} && list.handles.list.describe()->candidate==OptionValue{"T100"}; }
            if(scenario==7) setup=setup && bool(list.handles.zero.invoke());
        } else {
            setup=bool(h.tabs.activate({"v03"}));
            if(scenario==8) { std::uint64_t now=GetTickCount64(); state.tooltipClock([&]{return now;}); SendMessageW(f.host.window(),WM_SETFOCUS,0,0); setup=setup && bool(h.progressHalf.focus()); f.tap(VK_RIGHT); f.settle(); now+=600; state.tooltipTime(now); setup=setup && *h.progressTip.visible(); state.tooltipClock({}); }
            if(scenario==9) { state.setSystemKeyboardCues(true); setup=setup && bool(h.dialogOpen.invoke()) && *h.dialog.isOpen(); }
        }
        int width=960,height=720;
        if(scenario==6 && setup) {
            auto frame=f.host.layout(width,height,scale,f.form.scroll,{});
            auto item=frame.find(f.form.list->handles.list);
            setup=item && !item->options.empty() && intersect(item->options.back(),item->clip)==item->options.back();
        }
        bool matched=setup && f.host.compareLegacy(width,height,scale);
        report.check("DL01", matched, std::string(names[scenario])+" scale="+std::to_string(scale)+" setup="+std::to_string(setup));
        auto comparison=f.host.compareGpu(width,height,scale);
        const unsigned required = scenario==0 ? 3u : scenario==1 ? 4u : scenario==2 ? 10u : scenario==3 ? 48u : scenario==4 ? 5u : scenario==5 || scenario==6 ? 5u : scenario==9 ? 1u : 0u;
        auto hasRole=[&](std::string_view owner, unsigned role, bool requirePopup=false){
            return std::any_of(comparison.regions.begin(),comparison.regions.end(),[&](const auto& region){
                return region.owner==owner && (region.role & role) && region.passed && (!requirePopup || region.popup);
            });
        };
        bool rolePresence=(comparison.roles & required)==required;
        if(scenario==0)rolePresence &= hasRole(f.ids.name,1) && hasRole(f.ids.name,2);
        if(scenario==1)rolePresence &= hasRole(f.ids.name,4);
        if(scenario==2)rolePresence &= hasRole(f.ids.name,8) && hasRole(f.ids.name,2);
        if(scenario==3)rolePresence &= hasRole(f.ids.name,32) && (hasRole(f.ids.name,16) || hasRole(f.ids.nameError,16));
        if(scenario==4)rolePresence &= hasRole(f.ids.region,1) && hasRole(f.ids.region,4,true);
        if(scenario==5 || scenario==6){auto id=f.form.list->handles.list.describe()->id;rolePresence &= hasRole(id,1) && hasRole(id,4);}
        if(scenario==9)rolePresence &= hasRole(h.dialogCancel.describe()->id,1);
        bool pass=setup && bool(comparison) && rolePresence;
        report.check("GC01", pass, std::string(names[scenario])+" scale="+std::to_string(scale)+" roles="+std::to_string(comparison.roles));
        if(gpuTsv){
            auto key=report.appearance+"-"+names[scenario]+"-"+(scale==1.f?"100":"150");
            writeComparisonBmp(evidence/(key+"-cpu.bmp"), comparison.reference);
            *gpuTsv << names[scenario] << '\t' << report.appearance << '\t' << width << 'x' << height << '\t' << scale << '\t' << f.host.rendererName() << '\t' << windowsBuild() << '\t' <<
#ifdef NDEBUG
            "Release"
#else
            "Debug"
#endif
            << '\t' << double(comparison.comparison.differentPixels)/(width*height) << '\t' << comparison.comparison.maximumDifference << '\t' << comparison.roles << '\t' << comparison.failedRoles << '\t';
            for(std::size_t i=0;i<comparison.regions.size();++i) {
                if(i)*gpuTsv << ',';
                const auto& r=comparison.regions[i];
                *gpuTsv << r.owner << ':' << r.role << ':' << r.popup << ':' << r.bounds.x << ':' << r.bounds.y << ':' << r.bounds.w << ':' << r.bounds.h << ':' << (r.passed?"PASS":"FAIL");
            }
            *gpuTsv << '\t' << comparison.comparison.regionsOk << '\t' << (pass?"PASS":"FAIL") << '\n';
            emitFailureIfNeeded(evidence,key,comparison,pass);
        }
    }
}


int demoCompare(const std::filesystem::path& evidence) {
    std::filesystem::create_directories(evidence);
    std::ofstream out(evidence / "demo-render-compare.tsv");
    out << "scenario\tappearance\tvariant\tsize\tscale\trenderer\twindows_build\tconfiguration\tdifferent_fraction\tmaximum_difference\troles\tfailed_roles\tregions\tstatus\n";
    int failures=0, rows=0;
    for (const auto* appearance : {&webAppearance(), &materialAppearance()})
    for (bool dark : {false,true}) for (float scale : {1.f,1.5f}) for (int scenario=0; scenario<9; ++scenario) {
        settingsDemo::AboutInfo info;
        info.licenses.push_back({{"lcars"},u"LcarsUI",u"License text"});
        for(int i=0;i<40;++i) { auto n=std::to_string(i); std::u16string name=u"Font "; name.append(n.begin(),n.end()); info.fonts.push_back({name,name}); }
        settingsDemo::Demo demo(std::move(info));
        DesktopConfig options; options.title=u"Demo comparison";
        DesktopHost host(demo.ui,*appearance,[&]{return demo.page();},demo.scroll,options,false,true);
        ShowWindow(host.window(),SW_SHOWNOACTIVATE); host.forceScale(scale); host.clientSize(scale==1.f?960:640,scale==1.f?720:480);
        SendMessageW(host.window(),WM_SETFOCUS,0,0); host.pump();
        auto settings=demo.ui.presentation(); settings.variant=dark?ThemeVariant::dark():ThemeVariant::light();
        bool setup=bool(demo.ui.setPresentation(settings));
        auto tap=[&](WPARAM key){ PostMessageW(host.window(),WM_KEYDOWN,key,keyLparam(key,false,false)); PostMessageW(host.window(),WM_KEYUP,key,keyLparam(key,false,true)); host.pump(); };
        if(scenario==2 || scenario==7) detail::access(demo.ui).setSystemKeyboardCues(true);  // indicator shown, as after a key press; programmatic focus alone does not show it
        if(scenario==0) setup &= bool(demo.name.focus());
        if(scenario==1) { setup &= bool(demo.name.focus()) && bool(demo.name.setValue(u"")); tap(VK_TAB); setup &= demo.name.describe()->invalid; }
        if(scenario==2 || scenario==3) {
            setup &= bool(demo.nav.select({"items"})) && bool(demo.itemsList.select({"T003"})) && bool(demo.itemsList.focus());
            if(scenario==3) setup &= bool(demo.itemsEdit.invoke()) && demo.itemDialog.isOpen().value.value_or(false);
        }
        if(scenario==4) { setup &= bool(demo.nav.select({"appearance"})) && bool(demo.font.focus()) && bool(demo.font.open()); for(int i=0;i<30;++i) tap(VK_DOWN); auto font=demo.font.describe(); setup &= font && font->expanded && font->candidate && font->candidate->id!="default"; }
        if(scenario==5) setup &= bool(demo.nav.select({"about"})) && bool(demo.licenses.select({"lcars"}));
        if(scenario==6) { setup &= bool(demo.notices.toggle()); setup &= !demo.requestClose() && demo.closeDialog.isOpen().value.value_or(false); }
        if(scenario==7) {
            setup &= bool(demo.nav.select({"about"})) && bool(demo.nav.focus());
            // The About page is taller since #82 added the vertical label, which pushes the centered navigation list
            // out of a small window: scroll it into view the way a user would, so its focus indicator is on screen.
            host.pump();
            if(const auto* nav=host.frame().find(demo.nav)) { demo.scroll.offset+=nav->bounds.y+nav->bounds.h/2-(scale==1.f?720:480)/2; host.pump(); }
        }
        if(scenario==8) setup &= bool(demo.nav.select({"about"}));  // the vertical writing label (#82)
        // The real mouse is wherever the person running this left it, and the host reads it when the window takes the focus (and
        // Windows may send a mouse move when the window appears under it): a hovered row or dropdown would change the picture. The
        // pictures are about the focus indicator, so the pointer stays out of them. Production behavior is untouched.
        host.pump(); detail::access(demo.ui).pointerLeft(); setup &= !detail::access(demo.ui).hoverTarget();
        auto result=host.compareGpu(960,720,scale);
        auto hasRole=[&](std::string_view owner, unsigned role) { return std::any_of(result.regions.begin(),result.regions.end(),[&](const auto& r) { return r.owner==owner && (r.role & role) && r.passed; }); };
        bool cues=true;
        if(scenario==0) cues=hasRole("general.name",1) && hasRole("general.name",2);
        if(scenario==1) cues=hasRole("general.name",32) && (hasRole("general.name",16) || hasRole("general.name.error",16));
        if(scenario==2) cues=hasRole("items.list",1) && hasRole("items.list",4);
        if(scenario==7) cues=hasRole("app.nav",1);
        if(scenario==8) {
            // The vertical label is plain content: compare its own box between the CPU reference and OpenGL, and require ink in it.
            const auto* label=host.frame().find(demo.aboutVertical());
            cues=label && label->vertical;
            if(cues) {
                auto box=intersect(label->bounds,label->clip);
                auto same=compareImages(result.reference,result.actual,{box});
                int ink=0; for(int y=box.y;y<box.y+box.h;++y) for(int x=box.x;x<box.x+box.w;++x) { auto i=(static_cast<std::size_t>(y)*result.reference.width+x)*4; ink+=result.reference.pixels[i]!=result.reference.pixels[(static_cast<std::size_t>(box.y)*result.reference.width+box.x)*4]; }
                cues=same.regionsOk && ink>box.w*box.h/40;
            }
        }
        bool pass=setup && bool(result) && cues; ++rows; if(!pass) ++failures;
        std::string name="SD-"+std::string(1,char('a'+scenario));
        std::string key=name+"-"+(appearance==&webAppearance()?"web":"material")+(dark?"-dark":"-light")+(scale==1.f?"-100":"-150");
        writeComparisonBmp(evidence/(key+"-cpu.bmp"),result.reference); emitFailureIfNeeded(evidence,key,result,pass);
        out << name << '\t' << (appearance==&webAppearance()?"web":"material") << '\t' << (dark?"dark":"light") << '\t' << (scale==1.f?"960x720":"640x480") << '\t' << scale << '\t' << host.rendererName() << '\t' << windowsBuild() << '\t'
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << '\t' << double(result.comparison.differentPixels)/(960*720) << '\t' << result.comparison.maximumDifference << '\t' << result.roles << '\t' << result.failedRoles << '\t';
        for(const auto& region:result.regions) out << region.owner << ':' << region.role << ':' << (region.passed?"PASS":"FAIL") << ',';
        out << '\t' << (pass?"PASS":"FAIL") << '\n';
        std::cout << key << (pass?" PASS":" FAIL") << " setup=" << setup << '\n';
    }
    return out && rows==72 && !failures ? 0 : 1;
}

void tabsNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.name.setValue(u"draft");
    h.tabs.focus(); f.settle();
    f.tap(VK_RIGHT);
    bool candidate = *h.tabs.active() == OptionValue{"v01"} && h.tabs.describe()->candidate == OptionValue{"v02"};
    f.tap(VK_RETURN); f.settle();
    bool activated = *h.tabs.active() == OptionValue{"v02"} && !f.host.frame().find(h.name) && f.host.frame().find(f.form.list->handles.search);
    f.form.list->handles.search.setValue(u"\u5171\u7528\u7bc4\u672c"); f.form.list->handles.list.select({"T006"});
    f.host.paint(); auto tab = f.host.frame().find(h.tabs);
    auto first = tab->options[0]; POINT point{first.x + first.w / 2, first.y + first.h / 2};
    f.mouse(WM_LBUTTONDOWN, point, MK_LBUTTON); f.mouse(WM_LBUTTONUP, point); f.settle();
    bool pointer = *h.tabs.active() == OptionValue{"v01"} && *h.name.draft() == u"draft" && f.form.list->handles.search.setValue(u"blocked").status == Status::hidden;
    h.tabs.activate({"v02"});
    bool retained = *f.form.list->handles.search.draft() == u"\u5171\u7528\u7bc4\u672c" && f.form.list->selection() == OptionValue{"T006"};
    f.form.list->handles.reset.invoke();
    f.host.render(960, 720, 1.f, folder / (report.appearance + "-v02.bmp"));
    report.check("C09-native", candidate && activated && pointer && retained,
        "Arrow only moves tab focus; Enter and pointer activate; hidden content is absent and both drafts survive");
}

void progressNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v03"});
    f.settle();
    bool passed = true;
    const struct { ButtonHandle button; int source; int displayed; } cases[] = {
        {h.progressZero, 0, 0}, {h.progressHalf, 50, 50}, {h.progressFull, 100, 100},
        {h.progressBelow, -10, 0}, {h.progressAbove, 110, 100}
    };
    for (const auto& c : cases) {
        c.button.focus(); f.tap(VK_RETURN); f.settle();
        Image pixels{1, 1, {}};
        auto path = folder / (report.appearance + (c.source == 50 ? "-v03-50.bmp" : "-v03-p07.bmp"));
        auto frame = f.host.render(960, 720, 1.f, path, &pixels);
        const auto* item = frame.find(h.progress);
        if (!item || item->progressTrack.w < 4) { passed = false; continue; }
        auto track = item->progressTrack;
        auto at = [&](int x) {
            auto i = (static_cast<std::size_t>((track.y + track.h / 2)) * pixels.width + x) * 4;
            return Color{pixels.pixels[i], pixels.pixels[i + 1], pixels.pixels[i + 2]};
        };
        auto matches = [](Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; };
        auto palette = appearance.palette();
        auto quarter = at(track.x + track.w / 4), threeQuarters = at(track.x + 3 * track.w / 4);
        auto expectedLeft = c.displayed >= 50 ? palette.accent : palette.border;
        auto expectedRight = c.displayed == 100 ? palette.accent : palette.border;
        auto number = std::to_string(c.source);
        passed = passed && f.form.progress() == c.source && h.progress.describe()->numericValue == c.displayed &&
            h.progressSource.describe()->value.find(std::u16string(number.begin(), number.end())) != std::u16string::npos &&
            matches(quarter, expectedLeft) && matches(threeQuarters, expectedRight);
    }
    int frames = f.host.frames();
    f.form.updateProgress(50); f.host.pump();
    passed = passed && f.host.frames() > frames && h.progress.describe()->value == u"50%";
    report.check("P07-progress", passed, "Keyboard fixtures drive 0/50/100/-10/110; source and clamped semantics agree with track pixels; external update repaints");
}

void tooltipNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v03"});
    SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
    h.progressHalf.focus(); f.tap(VK_RIGHT); f.settle();  // the focus tooltip needs keyboard use first
    bool initiallyHidden = h.progressTip.visible() && !*h.progressTip.visible();
    Sleep(550); f.host.pump(); f.host.paint();
    bool shown = *h.progressTip.visible() && h.progressHalf.describe()->focused &&
        std::any_of(f.host.frame().overlays.begin(), f.host.frame().overlays.end(), [](const auto& p) { return p.tooltip; });
    f.tap(VK_ESCAPE); Sleep(550); f.host.pump();
    bool escaped = !*h.progressTip.visible() && h.progressHalf.describe()->focused;
    SendMessageW(f.host.window(), WM_KILLFOCUS, 0, 0);
    SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
    bool refocusWaits = !*h.progressTip.visible();
    Sleep(550); f.host.pump(); f.host.paint();
    bool returned = *h.progressTip.visible() && h.progressHalf.describe()->focused;
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
    f.form.updateProgress(0); f.tap(VK_SPACE);
    bool space = f.form.progress() == 50;
    report.check("P05-P06-R08-tooltip-messages", initiallyHidden && shown && escaped && refocusWaits && returned && geometry && space,
        "initial=" + std::to_string(initiallyHidden) + ", focus=" + std::to_string(shown) + ", escape=" + std::to_string(escaped) +
        ", refocus-delay=" + std::to_string(refocusWaits) + ", returned=" + std::to_string(returned) + ", geometry=" + std::to_string(geometry) +
        ", space=" + std::to_string(space));
}

// #74: pointer hover through the real window procedure, with posted mouse messages (no real cursor).
void hoverNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    auto& ui = detail::access(f.form.ui);
    h.tabs.activate({"v03"});
    SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
    f.settle();
    auto hovered = [&](const std::string& id) { auto t = f.form.ui.hover(); return t && *t && (**t).id == id && !(**t).item; };
    auto none = [&] { auto t = f.form.ui.hover(); return t && !*t; };
    // Drain what the system already queued (a real WM_KILLFOCUS from another program taking the foreground),
    // then activate, so contention on a shared desktop cannot undo the activation before the posted move.
    // Mouse messages are sent, not posted: a real cursor resting over the window cannot slip its own
    // WM_MOUSEMOVE between the message and the check.
    auto send = [&](UINT msg, POINT p, WPARAM w = 0) { SendMessageW(f.host.window(), msg, w, MAKELPARAM(p.x, p.y)); };
    auto activate = [&] { f.host.pump(); SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0); };
    const std::string half = h.progressHalf.describe()->id, dialogOpener = h.dialogOpen.describe()->id;
    // Another program taking the foreground queues a real WM_KILLFOCUS ahead of a posted move; retry so
    // contention on a shared desktop cannot fail what is being checked.
    auto restOn = [&](const std::string& id, POINT at) {
        for (int attempt = 0; attempt < 5; ++attempt) { activate(); send(WM_MOUSEMOVE, at); if (hovered(id)) return true; }
        return false;
    };
    bool moved = restOn(half, f.center(half));
    // The CPU reference and the GPU agree on the overlay; the role is part of the observed draw list.
    auto compared = f.host.compareGpu(960, 720, 1.f);
    bool drawn = bool(compared) && (compared.roles & static_cast<unsigned>(detail::PaintRole::hover)) != 0;
    // A touch-synthesized mouse message neither moves nor clears the hover (Windows marks it in the extra info).
    // A posted message cannot carry the signature, so the host reads it through a seam here; the decoding
    // of the extra info is checked on literals.
    bool pretendTouch = false, touchKept = false;
    f.host.touchSource([&] { return pretendTouch; });
    for (int attempt = 0; attempt < 5 && !touchKept; ++attempt) {
        pretendTouch = false;
        if (!restOn(half, f.center(half))) continue;
        pretendTouch = true;
        send(WM_MOUSEMOVE, f.center(dialogOpener));
        send(WM_MOUSEMOVE, POINT{2, 2});
        touchKept = hovered(half);
    }
    pretendTouch = false; f.host.touchSource({});
    bool touchIgnored = touchKept && DesktopHost::touchSignature(0xFF515780) && DesktopHost::touchSignature(0xFF515700) &&
        !DesktopHost::touchSignature(0) && !DesktopHost::touchSignature(0xFF515600) && !DesktopHost::touchSignature(0x00515700);
    bool followed = restOn(dialogOpener, f.center(dialogOpener));
    activate();
    SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0);
    bool left = none();
    bool back = restOn(half, f.center(half));
    SendMessageW(f.host.window(), WM_KILLFOCUS, 0, 0);
    bool deactivated = none();
    send(WM_MOUSEMOVE, f.center(half));
    bool inactiveQuiet = none();
    activate();
    h.dialogOpen.invoke(); f.settle();
    bool modalClears = none();
    h.dialogCancel.invoke(); f.settle();
    activate();
    // The switch hides the overlay while the target stays queryable.
    auto settings = ui.presentation(); settings.hover = Hover::off;
    ui.setPresentation(settings);
    send(WM_MOUSEMOVE, f.center(half));
    auto switched = f.host.compareGpu(960, 720, 1.f);
    bool offHidden = hovered(half) && bool(switched) && (switched.roles & static_cast<unsigned>(detail::PaintRole::hover)) == 0;
    // Dragging (text selection, slider) keeps the hover on the dragged component; the release hands it to
    // whatever lies under the pointer.
    h.tabs.activate({"v01"}); f.host.clientSize(900, 420); f.settle(); activate();
    auto hoveredId = [&](const std::string& id) { return hovered(id); };
    // The window is deliberately short (and a CI desktop is smaller than the page anyway), so each drag first scrolls the
    // dragged control to the top third of the window and releases over the nearest other control that is on screen with it.
    auto centerOf = [&](const std::string& id) { return f.center(id); };
    auto onScreen = [&](POINT p) { RECT c{}; GetClientRect(f.host.window(), &c); return p.x >= 0 && p.y >= 0 && p.x < c.right && p.y < c.bottom; };
    auto reveal = [&](const std::string& id) {
        f.form.scroll.offset = 0; f.settle();
        RECT c{}; GetClientRect(f.host.window(), &c);
        int y = detail::findPlacement(f.host.frame(), id)->bounds.y;
        f.form.scroll.offset = std::clamp(y - static_cast<int>(c.bottom) / 3, 0, static_cast<int>(f.host.frame().maximum)); f.settle(); activate();
    };
    auto drag = [&](const std::string& id, bool& held, bool& released) {
        for (int attempt = 0; attempt < 5 && !(held && released); ++attempt) {
            reveal(id);
            std::string target;
            for (const auto& candidate : {f.ids.apply, f.ids.reset, f.ids.name, f.ids.volume})
                if (candidate != id && onScreen(centerOf(candidate))) { target = candidate; break; }
            if (target.empty()) continue;
            POINT from = centerOf(id), to = centerOf(target);
            activate();
            send(WM_MOUSEMOVE, from); send(WM_LBUTTONDOWN, from, MK_LBUTTON);
            send(WM_MOUSEMOVE, to, MK_LBUTTON);
            held = hoveredId(id);
            send(WM_LBUTTONUP, to);
            released = hoveredId(target);
        }
    };
    bool textHeld = false, textReleased = false, sliderHeld = false, sliderReleased = false;
    drag(f.ids.name, textHeld, textReleased);
    drag(f.ids.volume, sliderHeld, sliderReleased);
    bool drags = textHeld && textReleased && sliderHeld && sliderReleased;
    report.check("DF-hover-drag-native", drags,
        "text-held=" + std::to_string(textHeld) + ", text-released=" + std::to_string(textReleased) +
        ", slider-held=" + std::to_string(sliderHeld) + ", slider-released=" + std::to_string(sliderReleased));
    report.check("DF-hover-native", moved && drawn && touchIgnored && followed && left && back && deactivated && inactiveQuiet && modalClears && offHidden,
        "moved=" + std::to_string(moved) + ", gpu-overlay=" + std::to_string(drawn) + ", touch-ignored=" + std::to_string(touchIgnored) +
        ", followed=" + std::to_string(followed) + ", leave=" + std::to_string(left) + ", back=" + std::to_string(back) +
        ", deactivate=" + std::to_string(deactivated) + ", inactive=" + std::to_string(inactiveQuiet) + ", modal=" + std::to_string(modalClears) +
        ", switch-off=" + std::to_string(offHidden) + ", now=" + (f.form.ui.hover() && *f.form.ui.hover() ? (**f.form.ui.hover()).id : std::string("none")));
}
// Native twin of the core focus-visibility cases (#72): real window messages drive the display state.
void focusVisibilityNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& state = detail::access(f.form.ui);
    BOOL systemOn = FALSE; SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &systemOn, 0);
    auto shown = [&] { return f.form.ui.focusIndicatorVisible(); };
    state.setSystemKeyboardCues(false);  // deterministic start whatever the desktop setting is
    bool startedHidden = !shown();
    f.tap(VK_TAB);
    bool keyShows = shown() && !state.focused().empty();
    SendMessageW(f.host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
    bool wheelKeeps = shown();
    f.mouse(WM_LBUTTONDOWN, f.center(f.ids.title), MK_LBUTTON); f.mouse(WM_LBUTTONUP, f.center(f.ids.title));
    bool inertPressHides = !shown();  // the title label does not accept the pointer, yet the press still hides
    f.chord('S');
    bool chordKeepsHidden = !shown();
    f.tap(VK_RIGHT);
    bool plainKeyShows = shown();
    // Code review 2026-11-12 item 1: the right button is a press in the client area too. Over the title label (no menu to open) it must hide.
    f.mouse(WM_RBUTTONDOWN, f.center(f.ids.title), MK_RBUTTON); f.mouse(WM_RBUTTONUP, f.center(f.ids.title));
    bool inertRightPressHides = !shown();
    f.tap(VK_RIGHT);
    bool rightKeyShowsAgain = shown();
    POINT blank{-1, -1};
    f.host.paint();
    for (int y = 690; y > 0 && blank.x < 0; y -= 7) for (int x = 3; x < 900; x += 11)
        if (!f.host.frame().hit(x, y)) { blank = {x, y}; break; }
    bool foundBlank = blank.x >= 0;
    if (foundBlank) { f.mouse(WM_LBUTTONDOWN, blank, MK_LBUTTON); f.mouse(WM_LBUTTONUP, blank); }
    bool blankPressHides = foundBlank && !shown();
    f.tap(VK_RIGHT);
    const bool beforeBlankRight = shown();
    if (foundBlank) { f.mouse(WM_RBUTTONDOWN, blank, MK_RBUTTON); f.mouse(WM_RBUTTONUP, blank); }
    bool blankRightPressHides = foundBlank && beforeBlankRight && !shown();
    state.setSystemKeyboardCues(!systemOn);
    SendMessageW(f.host.window(), WM_SETTINGCHANGE, SPI_SETKEYBOARDCUES, 0);
    bool settingChangeRereads = shown() == bool(systemOn);  // recorded state is hidden, so only the system value shows it
    report.check("F72-native-focus-visibility", startedHidden && keyShows && wheelKeeps && inertPressHides && chordKeepsHidden &&
                 plainKeyShows && blankPressHides && settingChangeRereads && inertRightPressHides && rightKeyShowsAgain && blankRightPressHides,
                 "hidden at start=" + std::to_string(startedHidden) + ", key shows=" + std::to_string(keyShows) + ", wheel keeps=" + std::to_string(wheelKeeps) +
                 ", inert press hides=" + std::to_string(inertPressHides) + ", Ctrl chord keeps hidden=" + std::to_string(chordKeepsHidden) +
                 ", plain key shows=" + std::to_string(plainKeyShows) + ", blank press hides=" + std::to_string(blankPressHides) +
                 ", WM_SETTINGCHANGE re-reads=" + std::to_string(settingChangeRereads) + ", right press on a label hides=" + std::to_string(inertRightPressHides) +
                 ", key shows again=" + std::to_string(rightKeyShowsAgain) + ", right press on blank hides=" + std::to_string(blankRightPressHides));
}

void dialogNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v03"}); h.dialogOpen.focus(); f.settle();
    f.tap(VK_RETURN); f.settle();
    auto frame = f.host.render(960, 720, 1.f, folder / (report.appearance + "-dialog.bmp"));
    bool initial = frame.modal && frame.find(h.dialogCancel) && frame.find(h.dialogConfirm) && h.dialogCancel.describe()->focused;
    bool geometry = true;
    for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) {
        for (float scale : {1.f, 1.5f}) {
            int width = static_cast<int>(size.first * scale), height = static_cast<int>(size.second * scale);
            auto sample = f.host.render(width, height, scale, folder / (report.appearance + "-dialog-" + std::to_string(size.first) + "-" + std::to_string(static_cast<int>(scale * 100)) + ".bmp"));
            for (auto button : {h.dialogCancel, h.dialogConfirm}) {
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
    bool shield = *h.dialog.isOpen() && h.dialogCancel.describe()->focused && f.form.progress() == 50;
    f.tap(VK_TAB);
    bool tab = h.dialogConfirm.describe()->focused;
    f.key(VK_RETURN); f.key(VK_RETURN, true); f.keyUp(VK_RETURN); f.settle();
    bool once = f.form.progress() == 0 && f.form.dialogConfirmations() == 1 && h.dialogOpen.describe()->focused && !*h.dialog.isOpen();
    report.check("P02-P03-dialog", initial && shield && tab && once,
        "Dialog overlays the window, mask blocks pointer, Tab reaches confirm, held Enter settles once and restores focus");
    report.check("P03-dialog-layout", geometry, "Dialog buttons are visible and hit at 960x720 and 640x480 DIP, at forced 100% and 150%");
    h.resetV03.invoke(); h.dialogOpen.invoke(); f.tap(VK_ESCAPE); f.settle();
    report.check("P01-dialog-escape", f.form.progress() == 50 && f.form.dialogConfirmations() == 0 && h.dialogOpen.describe()->focused && !*h.dialog.isOpen(),
        "Escape cancels and returns focus without changing progress");
}

void fontDiagnosticReachesApplication(const Appearance& appearance, Report& report) {
    Ui ui;
    auto label=ui.addLabel({.id="missing-font-glyph",.value=u"\U0010ffff"});
    std::vector<RenderDiagnostic> events;
    DesktopHost* active=nullptr; int depth=0, maximumDepth=0;
    auto initial=ui.presentation(); initial.fontFamily=u"LcarsUI definitely absent";
    ui.setPresentation(initial);
    DesktopConfig options;
    options.onDiagnostic=[&](const RenderDiagnostic& event) {
        ++depth; maximumDepth=std::max(maximumDepth,depth); events.push_back(event);
        if(active && event.code=="font.family_unavailable") active->paint();
        --depth;
    };
    ScrollState scroll;
    DesktopHost host(ui,appearance,[&] { return layout::component(*label); },scroll,options,false,true);
    active=&host;
    host.clientSize(200,100); host.paint(); host.paint();
    auto dark=ui.presentation(); dark.variant=ThemeVariant::dark();
    bool darkAccepted=bool(ui.setPresentation(dark));
    host.observePaint(true); host.paint();
    auto darkText=appearance.palette(ThemeVariant::dark()).text;
    bool darkMissing=std::any_of(host.paintObservations().begin(),host.paintObservations().end(),[&](const auto& event) {
        return event.owner=="missing-font-glyph" && event.type==detail::PaintObservation::Type::text && event.visible &&
            event.color.r==darkText.r && event.color.g==darkText.g && event.color.b==darkText.b;
    });
    report.check("dark-missing-glyph",darkAccepted && darkMissing,"Missing glyph receives the dark text color");
    auto changed=ui.presentation(); changed.fontFamily=u"LcarsUI another absent family";
    bool accepted=bool(ui.setPresentation(changed)); host.pump();
    auto count=std::count_if(events.begin(),events.end(),[&](const auto& e) {
        return e.code=="text.glyph_missing" && e.nativeCode==0x10ffff && e.uiEnvironment==&ui && e.severity==DiagnosticSeverity::info;
    });
    bool family=std::count_if(events.begin(),events.end(),[](const auto& e) {return e.code=="font.family_unavailable" && e.nativeMessage.find("LcarsUI definitely absent")!=std::string::npos;})==1;
    bool changedFamily=std::count_if(events.begin(),events.end(),[](const auto& e) {return e.code=="font.family_unavailable" && e.nativeMessage.find("LcarsUI another absent family")!=std::string::npos;})==1;
    report.check("font-diagnostic",count==1 && family && accepted && changedFamily && maximumDepth==1,"font diagnostics reach the application once without recursive callbacks");
}

void registeredComponentUsesNativeHost(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    constexpr auto testKind = static_cast<ComponentKind>(100);
    TestComponent component;
    detail::ScopedComponentType registration(testKind, component);
    Ui ui; detail::access(ui).seedTestControls({{testKind, {.id = "probe", .value = u"Test component"}}});
    auto& state = detail::access(ui);
    int activations = 0;
    state.bind("probe", [&] { ++activations; return Result{}; });
    ScrollState scroll;
    DesktopConfig options;
    DesktopHost host(ui, appearance, [&] { return layout::component(state.layoutHandle("probe")); }, scroll, options, false, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.forceScale(1.f);
    host.clientSize(200, 100);
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
    post(WM_KEYDOWN, VK_RETURN, keyLparam(VK_RETURN, false, false));
    bool keyboard = activations == 2;
    report.check("registered-component", painted && outside && pointer && keyboard,
                 "custom color and hit region render; inside click and Enter each activate once");
}

void longDropdownNative(const Appearance& appearance, Report& report) {
    std::optional<OptionValue> value=OptionValue{"0"}; std::function<void()> changed; bool invoked=false;
    Ui ui; ScrollState scroll; std::vector<OptionItem> options;
    for (int i=0; i<150; ++i) { auto id=std::to_string(i); options.push_back({{id},std::u16string(id.begin(),id.end())}); }
    auto dropdown=ui.addDropdown({.id="native.dropdown",.label=u"Font"},std::move(options)).value.value();
    auto other=ui.addButton({.id="native.other",.label=u"Other"}).value.value();
    dropdown.connectSource({[&] { return value; },[&](auto f) { changed=std::move(f); return [&] { changed={}; }; }});
    dropdown.connectImmediate([&](const Change<DropdownHandle>& c) { value=c.value; if (changed) changed(); return Result{}; });
    other.bind([&] { invoked=true; return Result{}; });
    auto root=layout::row({layout::component(dropdown),layout::component(other)},8);
    DesktopConfig settings; DesktopHost host(ui,appearance,[&] { return root; },scroll,settings,false,true);
    ShowWindow(host.window(),SW_SHOWNOACTIVATE); host.forceScale(1.f); host.clientSize(640,480);
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

void listNative(const Appearance& appearance, Report& report) {
    listPage::Page page;
    DesktopConfig options;
    DesktopHost host(page.ui, appearance, [&] { return page.page(); }, page.scroll, options, false, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE); host.forceScale(1.f); host.clientSize(640, 480);
    page.handles.hundred.invoke(); page.handles.search.focus(); host.pump(); host.paint();
    auto post = [&](UINT msg, WPARAM w, LPARAM l) { PostMessageW(host.window(), msg, w, l); host.pump(); };
    page.handles.list.focus(); host.pump();
    post(WM_KEYDOWN, VK_END, keyLparam(VK_END, false, false));
    post(WM_KEYUP, VK_END, keyLparam(VK_END, false, true));
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
    bool wheel = page.listScroll.offset > 0;
    frame = host.frame(); region = frame.regions.back();
    POINT start{region.thumb.x + region.thumb.w / 2, region.thumb.y + region.thumb.h / 2};
    POINT finish{start.x, region.track.y + region.track.h - region.thumb.h / 2};
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(finish.x, finish.y));
    post(WM_LBUTTONUP, 0, MAKELPARAM(finish.x, finish.y)); host.paint();
    item = host.frame().find(page.handles.list);
    bool dragged = page.listScroll.offset == host.frame().regions.back().maximum && item && visible(item->options.back());
    bool clicked = false;
    if (dragged) {
        auto r = item->options.back(); POINT point{r.x + 8, r.y + r.h / 2};
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        post(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        clicked = page.selection() == OptionValue{"T100"};
    }
    page.handles.one.invoke(); host.pump(); host.paint();
    bool clamped = page.listScroll.offset <= host.frame().regions.back().maximum;
    report.check("L06-N07-native-scroll", wheel && dragged && clicked && clamped,
        "wheel=" + std::to_string(wheel) + ", thumb drag=" + std::to_string(dragged) + ", click T100=" + std::to_string(clicked) + ", shrink clamped=" + std::to_string(clamped));

    page.handles.twenty.invoke(); page.handles.disableEight.invoke(); page.handles.list.focus();
    host.pump();
    post(WM_KEYDOWN, VK_HOME, keyLparam(VK_HOME, false, false));
    for (int i = 0; i < 6; ++i) post(WM_KEYDOWN, VK_DOWN, keyLparam(VK_DOWN, false, false));
    host.paint();
    region = host.frame().regions.back();
    wheelpoint = {region.clip.x + 10, region.clip.y + 10}; ClientToScreen(host.window(), &wheelpoint);
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(wheelpoint.x, wheelpoint.y)); host.paint();
    item = host.frame().find(page.handles.list);
    bool disabledClick = false;
    if (item && item->options.size() > 7 && visible(item->options[7])) {
        auto r = item->options[7]; POINT point{r.x + 8, r.y + r.h / 2};
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        post(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        disabledClick = page.selection() == OptionValue{"T007"};
    }
    post(WM_KEYDOWN, VK_DOWN, keyLparam(VK_DOWN, false, false));
    report.check("R09-native-disabled-row", disabledClick && page.selection() == OptionValue{"T009"},
        "disabled T008 click ignored=" + std::to_string(disabledClick) + ", Down from T007 skips to T009=" + std::to_string(page.selection() == OptionValue{"T009"}));
}

void nestedListNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    h.tabs.activate({"v02"});
    auto& page = *f.form.list;
    page.handles.hundred.invoke();
    f.host.clientSize(640, 300); f.settle();
    page.handles.list.focus(); f.settle();
    f.tap(VK_END); f.settle();
    auto item = f.host.frame().find(page.handles.list);
    auto row = item->options.back(); auto clip = item->clip;
    bool revealed = page.selection() == OptionValue{"T100"} && page.listScroll.offset > 0 &&
        f.form.scroll.offset > 0 && row.y >= clip.y && row.y + row.h <= clip.y + clip.h;
    int inner = page.listScroll.offset;
    f.form.scroll.offset = f.host.frame().maximum / 2; f.settle();
    const auto& region = f.host.frame().regions.back();
    POINT point{region.clip.x + 5, region.clip.y + region.clip.h / 2};
    ClientToScreen(f.host.window(), &point);
    int outer = f.form.scroll.offset;
    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(point.x, point.y));
    f.settle();
    bool bubbles = page.listScroll.offset == inner && f.form.scroll.offset > outer;
    report.check("B03-native-nested", revealed && bubbles,
        "End reveals T100 through both viewports=" + std::to_string(revealed) +
        ", wheel at inner limit moves outer=" + std::to_string(bubbles));
}

void spaceActivationDoesNotLeak(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    detail::access(ui).focus(f.ids.error);
    f.host.trace().clear();
    f.key(VK_SPACE);
    f.key(VK_SPACE, true);
    f.key(VK_SPACE, true);
    f.keyUp(VK_SPACE);
    bool moved = detail::access(ui).focused() == f.ids.name && detail::access(ui).text(f.ids.name).empty();
    int chars = 0;
    for (const auto& line : f.host.trace()) if (line.starts_with(L"WM_CHAR\t32\t")) ++chars;
    std::string trace;
    for (const auto& line : f.host.trace()) trace += narrow(line) + " | ";
    int historyAfterActivation = f.history();
    f.tap(VK_SPACE);
    bool typed = detail::access(ui).text(f.ids.name) == u" " && f.history() == historyAfterActivation + 1;
    report.check("AC07-R01", moved && chars == 3 && historyAfterActivation == 1 && typed,
                 "translated WM_CHAR spaces=" + std::to_string(chars) + " delivered while the button held focus; name length after activation=" +
                 std::to_string(detail::access(ui).text(f.ids.name).size() - 1) + "; history entries after activation=" + std::to_string(historyAfterActivation) +
                 " (the error fixture's blanking only); typing space afterwards=" + (typed ? "inserted once" : "WRONG") + "; trace: " + trace);
}

void enterActivatesOnce(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    detail::access(f.form.ui).focus(f.ids.apply);
    f.key(VK_RETURN);
    f.key(VK_RETURN, true);
    f.keyUp(VK_RETURN);
    detail::access(f.form.ui).focus(f.ids.name);
    f.tap(VK_RETURN);
    report.check("AC06-enter", f.form.applies() == 1 && detail::access(f.form.ui).text(f.ids.name) == u"測試使用者",
                 "Enter press + auto-repeat on apply -> applies=" + std::to_string(f.form.applies()) + "; Enter in the name field adds nothing");
}

void pointerActivationRules(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.handles.apply.focus(); f.settle();
    auto apply = f.center(f.ids.apply);
    POINT outside{2, 2};
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, outside, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, outside);
    int movedOut = f.form.applies();
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
    f.keyUp(VK_SPACE);
    int deactivated = f.form.applies();
    detail::access(f.form.ui).invoke(f.ids.disabled);
    f.settle();
    f.mouse(WM_LBUTTONDOWN, apply, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, apply);
    report.check("AC06-F10-N02", movedOut == 0 && clicked == 1 && canceled == 1 && deactivated == 1 && f.form.applies() == 1,
                 "press+move out+release=" + std::to_string(movedOut) + ", click=" + std::to_string(clicked) +
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

void pointerSelectionAndClipboard(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    detail::access(ui).setValue(f.ids.name, u"Alpha 中文");
    f.host.paint();
    const Placement field = *detail::findPlacement(f.host.frame(), f.ids.name);  // a copy: repaints replace the frame
    auto xOf = [&](unsigned offset) {
        return field.bounds.x + field.textField.editor.x + static_cast<int>(field.run.x(offset) - field.offset);
    };
    int y = field.bounds.y + field.textField.editor.y + field.textField.editor.h / 2;
    f.mouse(WM_LBUTTONDOWN, {xOf(5), y}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {xOf(5), y});
    auto placed = detail::access(ui).edit(f.ids.name)->selection;
    f.mouse(WM_LBUTTONDOWN, {xOf(8), y}, MK_LBUTTON | MK_SHIFT);
    f.mouse(WM_LBUTTONUP, {xOf(8), y});
    auto extended = detail::access(ui).edit(f.ids.name)->selection;
    f.mouse(WM_LBUTTONDOWN, {xOf(0), y}, MK_LBUTTON);
    f.mouse(WM_MOUSEMOVE, {xOf(5), y}, MK_LBUTTON);
    f.mouse(WM_LBUTTONUP, {xOf(5), y});
    auto dragged = detail::access(ui).edit(f.ids.name)->selection;
    detail::WindowsClipboard clipboard(f.host.window());
    auto saved = clipboard.read();
    if (saved.status != Status::ok && saved.status != Status::notSupported) {
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
    bool restored = saved.status == Status::notSupported || (saved && clipboard.write(*saved));
    report.check("AC08-pointer-clipboard",
                 placed.anchor == 5 && placed.caret == 5 && extended.anchor == 5 && extended.caret == 8 && dragged.anchor == 0 &&
                     dragged.caret == 5 && (copied && *copied == u"Alpha") && supplied && pasted == u"X Y" && undone == u"Alpha \u4e2d\u6587" && restored,
                 "click caret=" + std::to_string(placed.caret) + ", shift-click=" + std::to_string(extended.anchor) + ".." +
                     std::to_string(extended.caret) + ", drag=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) +
                     ", Ctrl+C=" + ((copied && *copied == u"Alpha") ? "ok" : copied ? "unexpected" : "unavailable") +
                     ", fixture write=" + (supplied ? "ok" : "failed") + ", Ctrl+V=" + (pasted == u"X Y" ? "ok" : "unexpected") +
                     ", Ctrl+Z=" + (undone == u"Alpha \u4e2d\u6587" ? "ok" : "unexpected") +
                     "; user clipboard text restored=" + (saved.status == Status::notSupported ? "no text saved" : restored ? "yes" : "FAILED"));
}

// Context menu (#85) through the real window procedure: right button, menu key, Shift+F10, Alt, Space.
// The core-seam twin is tests/context_menu.cpp; this proves the messages reach it and the menu is painted.
void contextMenuNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = f.form.ui;
    auto& core = detail::access(ui);
    const std::string field = f.ids.name;
    core.setValue(field, u"Alpha 中文");
    f.settle();
    const Placement name = *detail::findPlacement(f.host.frame(), field);
    const int y = name.bounds.y + name.textField.editor.y + name.textField.editor.h / 2;
    auto xOf = [&](unsigned offset) { return name.bounds.x + name.textField.editor.x + static_cast<int>(name.run.x(offset) - name.offset); };
    auto panel = [&]() -> const Placement* { return f.host.frame().menuPanel(); };
    auto open = [&] { return core.menuOpen(); };
    auto selection = [&] { return core.edit(field)->selection; };
    auto highlighted = [&]() -> std::string {
        auto menu = ui.contextMenu();
        if (menu) for (const auto& item : menu->items) if (item.highlighted) return item.id;
        return {};
    };
    auto rowCenter = [&](std::string_view id) -> POINT {
        for (const auto& row : panel()->menuRows) if (row.id == id) return {row.bounds.x + row.bounds.w / 2, row.bounds.y + row.bounds.h / 2};
        return {-1, -1};
    };
    auto rightClick = [&](POINT p) { f.mouse(WM_RBUTTONDOWN, p, MK_RBUTTON); f.mouse(WM_RBUTTONUP, p); f.settle(); };
    auto leftClick = [&](POINT p) { f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p); f.settle(); };

    // 1. Release of the right button opens the menu beside the pointer, focusing the field.
    core.select(field, 0, 0);
    f.mouse(WM_RBUTTONDOWN, {xOf(5), y}, MK_RBUTTON);
    f.settle();
    const bool notOnPress = !open();
    f.mouse(WM_RBUTTONUP, {xOf(5), y});
    f.settle();
    const bool beside = open() && panel() && panel()->bounds.x == xOf(5) && panel()->bounds.y == y;
    report.check("CM-open-on-release", notOnPress && beside && core.focused() == field && selection().caret == 5,
                 std::string("not on press=") + (notOnPress ? "yes" : "NO") + ", panel=" + (panel() ? str(panel()->bounds) : "none") + ", pointer=" + std::to_string(xOf(5)) + "," + std::to_string(y) + ", caret moved to " + std::to_string(selection().caret));
    // 2. It is painted: items with their letters and hints reach the draw list as visible ink.
    f.host.observePaint(true);
    f.host.paint();
    auto painted = [&](std::u16string_view text) {
        return std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(),
            [&](const auto& event) { return event.type == detail::PaintObservation::Type::text && event.text == text && event.visible && event.popup && !event.tooltip; });
    };
    const bool inked = painted(u"復原(U)") && painted(u"剪下(T)") && painted(u"全選(A)") && painted(u"Ctrl+X");
    f.host.observePaint(false);
    report.check("CM-painted", inked, std::string("labels and hints drawn as visible popup ink: ") + (inked ? "yes" : "NO"));
    // 3. Hover highlights a row; a press and release on a disabled row leaves the menu open.
    f.mouse(WM_MOUSEMOVE, rowCenter(menuCut), 0);
    f.settle();
    const bool hovered = highlighted() == menuCut;
    leftClick(rowCenter(menuCut));
    const bool stays = open() && !ui.contextMenu()->items[3].enabled;
    f.mouse(WM_LBUTTONDOWN, rowCenter("separator:2"), MK_LBUTTON); f.mouse(WM_LBUTTONUP, rowCenter("separator:2")); f.settle();
    report.check("CM-hover-disabled-line", hovered && stays && open(), std::string("hover highlights cut=") + (hovered ? "yes" : "NO") + ", disabled click keeps it open=" + (stays ? "yes" : "NO") + ", a line does nothing=" + (open() ? "yes" : "NO"));
    // 4. Release on an enabled item runs it and closes the menu; focus is on the field.
    leftClick(rowCenter(menuSelectAll));
    report.check("CM-click-runs-item", !open() && selection().anchor == 0 && selection().caret == 8 && core.focused() == field,
                 "select all: selection " + std::to_string(selection().anchor) + ".." + std::to_string(selection().caret) + ", closed=" + (!open() ? "yes" : "NO"));
    // 5. Right button inside the selection keeps it; outside it moves the caret.
    core.select(field, 1, 4);
    f.settle();
    rightClick({xOf(2), y});
    const auto inside = selection();
    core.closeMenu(); f.settle();
    rightClick({xOf(7), y});
    const auto outside = selection();
    core.closeMenu(); f.settle();
    report.check("CM-selection-rules", inside.anchor == 1 && inside.caret == 4 && outside.anchor == 7 && outside.caret == 7,
                 "inside kept " + std::to_string(inside.anchor) + ".." + std::to_string(inside.caret) + ", outside caret " + std::to_string(outside.caret));
    // 6. Menu key opens at the caret; Down/Up walk; Escape closes and focus stays on the field.
    core.select(field, 3, 3); f.settle();
    f.tap(VK_APPS); f.settle();
    const int caretX = name.bounds.x + name.textField.editor.x + static_cast<int>(std::lround(name.run.x(3) - name.offset));
    const int lineBottom = name.bounds.y + name.textField.editor.y + name.textField.editor.h;
    const bool atCaret = open() && panel() && panel()->bounds.x == caretX && panel()->bounds.y == lineBottom;
    const std::string placed = panel() ? str(panel()->bounds) : "none";
    f.key(VK_DOWN); f.key(VK_DOWN); f.key(VK_DOWN); f.settle();
    const bool walked = highlighted() == menuCut;
    f.tap(VK_ESCAPE); f.settle();
    report.check("CM-menu-key", atCaret && walked && !open() && core.focused() == field,
                 std::string("menu key at caret=") + (atCaret ? "yes" : "NO") + " (panel " + placed + ", caret " + std::to_string(caretX) + "," + std::to_string(lineBottom) + "), Down x3 lands on cut=" + (walked ? "yes" : "NO") + ", Escape closed=" + (!open() ? "yes" : "NO"));
    // 7. Shift+F10 (a system key message) opens; F10 and Alt close; their releases are not left for the system menu.
    auto withShift = [&](auto&& act) {
        BYTE state[256]{}; GetKeyboardState(state); state[VK_SHIFT] = 0x80; SetKeyboardState(state);
        act();
        state[VK_SHIFT] = 0; SetKeyboardState(state);
    };
    withShift([&] { f.post(WM_SYSKEYDOWN, VK_F10, keyLparam(VK_F10, false, false)); f.host.pump(); });
    f.post(WM_SYSKEYUP, VK_F10, keyLparam(VK_F10, false, true) | (1LL << 29)); f.host.pump(); f.settle();
    const bool shiftF10 = open();
    f.post(WM_SYSKEYDOWN, VK_MENU, keyLparam(VK_MENU, false, false) | (1LL << 29)); f.host.pump(); f.settle();
    const bool altClosed = !open();
    const auto altUp = SendMessageW(f.host.window(), WM_SYSKEYUP, VK_MENU, keyLparam(VK_MENU, false, true) | (1LL << 29));
    withShift([&] { f.post(WM_SYSKEYDOWN, VK_F10, keyLparam(VK_F10, false, false)); f.host.pump(); });
    f.post(WM_SYSKEYDOWN, VK_F10, keyLparam(VK_F10, false, false)); f.host.pump(); f.settle();
    const bool f10Closed = !open();
    report.check("CM-shift-f10-alt-f10", shiftF10 && altClosed && altUp == 0 && f10Closed,
                 std::string("Shift+F10 opens=") + (shiftF10 ? "yes" : "NO") + ", Alt closes=" + (altClosed ? "yes" : "NO") + ", Alt release swallowed=" + (altUp == 0 ? "yes" : "NO") + ", F10 closes=" + (f10Closed ? "yes" : "NO"));
    // 8. Space runs the highlighted item and its character does not type into the field.
    core.select(field, 0, 5); f.settle();
    const auto beforeText = core.text(field);
    f.tap(VK_APPS); f.key(VK_END); f.settle();
    f.key(VK_SPACE);  // TranslateMessage turns this key message into the WM_CHAR a real Space produces
    f.keyUp(VK_SPACE); f.settle();
    report.check("CM-space-runs-no-typing", !open() && core.text(field) == beforeText && selection().anchor == 0 && selection().caret == static_cast<unsigned>(beforeText.size()),
                 "select all ran, text unchanged: " + std::string(core.text(field) == beforeText ? "yes" : "NO") + " open=" + (open() ? "yes" : "NO") + " text=" + narrow(core.text(field)) + " before=" + narrow(beforeText) + " sel=" + std::to_string(selection().anchor) + ".." + std::to_string(selection().caret));
    // 9. A click outside only closes the menu: a button under the pointer is not pressed (focus stays on the
    //    field), nor is its release acted on; the next click works as usual.
    core.openMenu(field, MenuPosition{5, 5}, std::nullopt); f.settle();
    std::string button; POINT buttonAt{};
    for (const auto& item : f.host.frame().items) {
        auto visible = intersect(item.bounds, item.clip);
        if (item.kind == ComponentKind::button && visible.w > 0 && visible.h > 0 && panel() && !panel()->bounds.contains(visible.x + visible.w / 2, visible.y + visible.h / 2)) {
            button = item.id; buttonAt = {visible.x + visible.w / 2, visible.y + visible.h / 2}; break;
        }
    }
    const bool targetClear = open() && !button.empty() && f.host.frame().hit(buttonAt.x, buttonAt.y) && f.host.frame().hit(buttonAt.x, buttonAt.y)->id == button;
    leftClick(buttonAt);
    const bool swallowed = !open() && core.focused() == field;
    leftClick(buttonAt);
    const bool worksAgain = core.focused() == button;
    report.check("CM-outside-click-swallowed", targetClear && swallowed && worksAgain,
                 "button " + button + " at " + std::to_string(buttonAt.x) + "," + std::to_string(buttonAt.y) + " clear of the menu=" + (targetClear ? "yes" : "NO") +
                 ", the click that closed the menu did not focus or press it=" + (swallowed ? "yes" : "NO") + ", the next click does=" + (worksAgain ? "yes" : "NO"));
    // 10. The window losing focus closes the menu.
    core.windowActive(true);
    core.openMenu(field, MenuPosition{5, 5}, std::nullopt); f.settle();
    const bool opened = open();
    SendMessageW(f.host.window(), WM_KILLFOCUS, 0, 0); f.settle();
    report.check("CM-deactivation-closes", opened && !open(), std::string("closed by WM_KILLFOCUS=") + (!open() ? "yes" : "NO"));
    // 11. At the bottom right corner it opens up and to the left; in a small window it scrolls.
    RECT client{}; GetClientRect(f.host.window(), &client);
    core.openMenu(field, MenuPosition{client.right - 3, client.bottom - 3}, std::nullopt); f.settle();
    const bool flipped = panel() && panel()->bounds.x + panel()->bounds.w == client.right - 3 && panel()->bounds.y + panel()->bounds.h == client.bottom - 3;
    core.closeMenu();
    f.host.clientSize(300, 140); f.settle();
    GetClientRect(f.host.window(), &client);
    // The window cannot shrink below WM_GETMINMAXINFO (400x300 window DIP) and how much of that is frame depends on the
    // system theme (CI had a taller client than the author machine, where the 9 built-in items only just overflowed).
    // So the application adds enough items that the menu is taller than any client this window can have.
    std::vector<MenuItem> extra;
    for (int i = 0; i < 8; ++i) extra.push_back({.id = "cm.extra" + std::to_string(i), .label = u"Extra item " + std::u16string(1, static_cast<char16_t>(u'A' + i)), .action = [](const MenuContext&) { return Result{}; }});
    const bool extrasAdded = bool(ui.addContextMenu(f.form.handles.name, extra, MenuMode::append));
    core.openMenu(field, MenuPosition{20, 20}, std::nullopt); f.settle();
    const bool scrolls = extrasAdded && panel() && panel()->menuMaximum > 0 && panel()->bounds.x >= 0 && panel()->bounds.y >= 0 &&
        panel()->bounds.x + panel()->bounds.w <= client.right && panel()->bounds.y + panel()->bounds.h <= client.bottom;
    const int panelHeight = panel() ? panel()->bounds.h : -1, panelMaximum = panel() ? panel()->menuMaximum : -1;
    f.mouse(WM_MOUSEWHEEL, {panel() ? panel()->bounds.x + 4 : 0, panel() ? panel()->bounds.y + 4 : 0}, MAKEWPARAM(0, -WHEEL_DELTA));
    core.closeMenu();
    report.check("CM-flip-and-scroll", flipped && scrolls, std::string("flips up-left=") + (flipped ? "yes" : "NO") + ", small window scrolls inside it=" + (scrolls ? "yes" : "NO") +
                 " (client " + std::to_string(client.right) + "x" + std::to_string(client.bottom) + ", menu panel height " + std::to_string(panelHeight) + ", scroll maximum " + std::to_string(panelMaximum) + ")");
}

// #82 twin of tests/vertical.cpp on a real window: posted messages scroll a vertical-writing label sideways.
// Reading starts at the right edge; wheel down (also with Shift) goes toward the end of the text, the horizontal
// wheel and the thumb go both ways, Left and Right read on and back. The 300 characters make 25 columns of 24 px
// (12 cells each in 200 DIP), more than the window's minimum width shows, so the label must scroll.
void verticalLabelNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder = {}) {
    Ui ui; ScrollState scroll;
    auto note = ui.addLabel({.id = "note", .value = std::u16string(300, u'字'),
                              .writingMode = WritingMode::vertical}).value.value();
    DesktopConfig options; options.title = u"LcarsUI vertical label verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::verticalText(note, 200)}, 0, 8); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.forceScale(1.f);
    host.clientSize(420, 300);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto region = [&]() -> std::optional<ScrollRegion> {  // a copy: repaints replace the frame
        for (const auto& r : host.frame().regions) if (r.horizontal) return r;
        return std::nullopt;
    };
    auto label = [&]() { return *detail::findPlacement(host.frame(), "note"); };
    auto offset = [&] { auto r = region(); return r ? r->offset : -1; };
    auto screen = [&](POINT p) { ClientToScreen(hwnd, &p); return MAKELPARAM(p.x, p.y); };
    auto start = region();
    report.check("VT01-starts-at-the-text-start", start && start->offset == 0 && start->maximum > 0 && label().vertical &&
                     label().columnLayout.columns[0].bounds.x + label().columnLayout.columns[0].bounds.w == label().bounds.x + label().bounds.w - (label().columnLayout.track.x - label().bounds.x),  // a ring's thickness in from the edge (#90): the bar starts at x = ring
        "The label scrolls sideways and starts with the first column at the right edge; maximum=" + std::to_string(start ? start->maximum : -1));
    const auto bounds = label().bounds;
    POINT inside{bounds.x + bounds.w / 2, bounds.y + 40};
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), screen(inside));
    const int afterWheel = offset();
    post(WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), screen(inside));
    const int afterShift = offset();
    post(WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(WHEEL_DELTA))), screen(inside));
    const int afterHorizontal = offset();
    report.check("VT02-wheels", afterWheel > 0 && afterShift > afterWheel && afterHorizontal < afterShift && scroll.offset == 0,
        "Wheel down and Shift+wheel move toward the end of the text, the horizontal wheel to the right moves back, and the page does not scroll; offsets " +
            std::to_string(afterWheel) + ", " + std::to_string(afterShift) + ", " + std::to_string(afterHorizontal));
    // Thumb: press it, drag it to the far left, release.
    auto drag = region();
    bool dragged = false;
    if (drag) {
        POINT grab{drag->thumb.x + drag->thumb.w / 2, drag->thumb.y + drag->thumb.h / 2};
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(grab.x, grab.y));
        post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(drag->track.x - 40, grab.y));
        const int atLeft = offset();
        post(WM_LBUTTONUP, 0, MAKELPARAM(drag->track.x - 40, grab.y));
        post(WM_MOUSEMOVE, 0, MAKELPARAM(grab.x, grab.y));
        dragged = atLeft == drag->maximum && offset() == drag->maximum;
    }
    report.check("VT03-thumb-drag", dragged, "Dragging the horizontal thumb to the left end shows the last column; offset=" + std::to_string(offset()));
    // Focus and keys: a label that scrolls takes focus and reads on with Left, back with Right.
    ui.semantics().perform({"note"}, Verb::focus);
    host.pump(); host.paint();
    auto key = [&](WPARAM vk) { post(WM_KEYDOWN, vk, keyLparam(vk, false, false)); post(WM_KEYUP, vk, keyLparam(vk, false, true)); };
    key(VK_HOME);
    const int home = offset();
    key(VK_LEFT);
    const int left = offset();
    key(VK_RIGHT);
    const int right = offset();
    key(VK_END);
    const int end = offset();
    report.check("VT04-keys", home == 0 && left > 0 && right == 0 && end == region()->maximum && detail::access(ui).focused() == "note",
        "Home, Left, Right and End scroll the focused label; offsets " + std::to_string(home) + ", " + std::to_string(left) + ", " + std::to_string(right) + ", " + std::to_string(end));
    if (!folder.empty()) {
        std::filesystem::create_directories(folder);
        host.render(420, 300, 1.f, folder / ("vertical-label-" + report.appearance + ".bmp"));
    }
    auto comparison = host.compareGpu(420, 300, 1.f);
    report.check("VT05-gpu", bool(comparison), "The CPU reference and the OpenGL picture of the vertical label agree");
}

// #79 twin of tests/multiline.cpp on a real window: posted messages drive a multi-line field. The input
// method position is asked through WM_IME_REQUEST, as a real IME does; typing through an IME needs the
// author's machine (see the author-run list).
void multilineNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder = {}) {
    Ui ui; ScrollState scroll;
    auto model = std::make_shared<std::u16string>();
    auto note = ui.addTextField({.id = "note", .label = u"Note", .multiline = true, .visibleLines = 3}).value.value();
    note.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    note.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    DesktopConfig options; options.title = u"LcarsUI multi-line verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::component(note)}, 12, 12); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.forceScale(1.f);
    host.clientSize(600, 400);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto key = [&](WPARAM vk, bool ctrl = false, bool shift = false) {
        BYTE state[256]{};
        GetKeyboardState(state);
        state[VK_CONTROL] = ctrl ? 0x80 : 0; state[VK_SHIFT] = shift ? 0x80 : 0;
        SetKeyboardState(state);
        post(WM_KEYDOWN, vk, keyLparam(vk, false, false)); post(WM_KEYUP, vk, keyLparam(vk, false, true));
        state[VK_CONTROL] = 0; state[VK_SHIFT] = 0;
        SetKeyboardState(state);
    };
    auto placed = [&]() { return *detail::findPlacement(host.frame(), "note"); };  // a copy: repaints replace the frame
    auto caret = [&]() { return detail::access(ui).edit("note")->selection; };
    auto line = [&] { return note.scrollLine().value.value_or(~0u); };
    note.focus(); host.pump(); host.paint();
    for (char16_t digit = u'1'; digit <= u'6'; ++digit) {
        post(WM_CHAR, digit, 1);
        if (digit != u'6') key(VK_RETURN);
    }
    const auto firstText = detail::access(ui).text("note");
    report.check("ML01-enter-and-reveal", firstText == u"1\n2\n3\n4\n5\n6" && line() == 3 && placed().view.lines.size() == 6,
        "Enter breaks lines through WM_KEYDOWN, typing to line six scrolls to the caret; first line=" + std::to_string(line()));
    key(VK_PRIOR);
    const bool pageUp = caret().caret == 5 && line() == 0;  // three lines up, keeping the column after the last character
    key(VK_END, true);
    const bool ctrlEnd = caret().caret == 11 && line() == 3;
    key(VK_HOME, true, true);
    report.check("ML02-cursor-keys", pageUp && ctrlEnd && caret().anchor == 11 && caret().caret == 0 && line() == 0,
        "PageUp moves a page, Ctrl+End and Shift+Ctrl+Home move over the whole text and extend the selection; PageUp=" + std::to_string(pageUp) + ", Ctrl+End=" + std::to_string(ctrlEnd) +
        ", selection=" + std::to_string(caret().anchor) + ".." + std::to_string(caret().caret) + ", first line=" + std::to_string(line()));

    auto field = placed();
    POINT inside{field.view.text.x + 20, field.view.text.y + 10};
    POINT screen = inside; ClientToScreen(hwnd, &screen);
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), MAKELPARAM(screen.x, screen.y));
    const unsigned afterNotch = line();
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), MAKELPARAM(screen.x, screen.y));
    post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(-WHEEL_DELTA))), MAKELPARAM(screen.x, screen.y));
    report.check("ML03-wheel", afterNotch == 2 && line() == 3 && caret().caret == 0,
        "One wheel notch scrolls two lines and stops at the last page without moving the caret; first line " + std::to_string(afterNotch) + " then " + std::to_string(line()));

    field = placed();
    auto track = field.view.track, thumb = field.view.thumb;
    POINT grab{thumb.x + thumb.w / 2, thumb.y + thumb.h / 2};
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(grab.x, grab.y));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(grab.x, track.y - 30));
    const unsigned top = line();
    post(WM_LBUTTONUP, 0, MAKELPARAM(grab.x, track.y - 30));
    report.check("ML04-thumb-drag", thumb.h > 0 && top == 0, "Dragging the field's scrollbar thumb to the top scrolls to the first line; line " + std::to_string(top));

    field = placed();
    auto row = [&](int index) { return POINT{field.view.text.x + 2, field.view.text.y + index * field.view.lineHeight + 3}; };
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(row(1).x, row(1).y));
    post(WM_LBUTTONUP, 0, MAKELPARAM(row(1).x, row(1).y));
    const auto clicked = caret();
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(row(0).x, row(0).y));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(row(2).x, row(2).y));
    const auto dragged = caret();
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(row(2).x, field.view.text.y + 3 * field.view.lineHeight + 8));
    const unsigned scrolledByDrag = line();
    post(WM_LBUTTONUP, 0, MAKELPARAM(row(2).x, row(2).y));
    report.check("ML05-click-and-drag", clicked.caret == 2 && clicked.anchor == 2 && dragged.anchor == 0 && dragged.caret == 4 && scrolledByDrag >= 1,
        "A click puts the caret on its line, a drag selects across lines, dragging below the field scrolls a line; click=" + std::to_string(clicked.caret) +
        ", drag=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) + ", first line=" + std::to_string(scrolledByDrag));

    if (!folder.empty()) host.render(600, 220, 1.f, folder / (report.appearance + "-multiline.bmp"));  // evidence: selection across lines, scrollbar
    detail::access(ui).select("note", 8, 8); host.paint();
    detail::access(ui).compositionUpdate(u"你", 1); host.paint();
    IMECHARPOSITION info{}; info.dwSize = sizeof(info); info.dwCharPos = 0;
    auto handled = SendMessageW(hwnd, WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&info));
    auto anchor = placed().caretRect(8);  // dwCharPos 0 is the start of the composition
    POINT expected{anchor.x, anchor.y}; ClientToScreen(hwnd, &expected);
    report.check("ML06-ime-anchor", handled == 1 && info.pt.x == expected.x && info.pt.y == expected.y && info.cLineHeight == static_cast<UINT>(anchor.h) && anchor.h == placed().view.lineHeight,
        "The input method asks where the caret is and is told the caret's own line, not the field's top; y=" + std::to_string(info.pt.y) + " expected " + std::to_string(expected.y));
    detail::access(ui).compositionCancel();
}

// #81 twin of tests/richtext.cpp on a real window with the real fonts: Ctrl+B through WM_KEYDOWN, what the host
// actually draws (faces, colors per theme, underline) read from the paint observations, and the real clipboard's
// private format. Typing through an IME and the look of the faces on screen are the author's to confirm.
void richtextNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder = {}) {
    Ui ui; ScrollState scroll;
    auto make = [&](const char* id, std::u16string value) {
        auto model = std::make_shared<RichText>(RichText{value, {}});
        auto field = ui.addTextField({.id = id, .label = u"Note", .value = std::move(value), .multiline = true, .visibleLines = 3, .rich = true}).value.value();
        field.connectRichSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
        field.connectRichConfirmed([](const Change<TextFieldHandle, RichText>&) { return Result{}; });
        return field;
    };
    auto note = make("note", u"Alpha 重要 Beta");
    auto other = make("other", u"");
    note.setColors({{"red", {200, 0, 0}, {255, 140, 140}}});
    DesktopConfig options; options.title = u"LcarsUI rich text verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::component(note), layout::component(other)}, 12, 12); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.forceScale(1.f);
    host.clientSize(600, 420);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto key = [&](WPARAM vk, bool ctrl = false, bool shift = false) {
        BYTE state[256]{};
        GetKeyboardState(state);
        state[VK_CONTROL] = ctrl ? 0x80 : 0; state[VK_SHIFT] = shift ? 0x80 : 0;
        SetKeyboardState(state);
        post(WM_KEYDOWN, vk, keyLparam(vk, false, false)); post(WM_KEYUP, vk, keyLparam(vk, false, true));
        state[VK_CONTROL] = 0; state[VK_SHIFT] = 0;
        SetKeyboardState(state);
    };
    auto spans = [&](const TextFieldHandle& field) { return field.richValue()->spans; };

    note.focus(); host.pump(); host.paint();
    note.select(0, 5);
    key('B', true);
    const bool boldApplied = spans(note) == std::vector<StyleSpan>{{.start = 0, .length = 5, .bold = true}};
    key('I', true); key('U', true);
    note.select(6, 8);
    key('U', true);
    note.setStyle(9, 13, {.color = "red"});
    host.observePaint(true); host.paint();
    const auto& seen = host.paintObservations();
    auto findText = [&](std::u16string_view text) { return std::find_if(seen.begin(), seen.end(), [&](const auto& o) { return o.type == detail::PaintObservation::Type::text && o.owner == "note" && o.text == text; }); };
    auto alpha = findText(u"Alpha"), red = findText(u"Beta");
    const auto lightText = detail::access(ui).effectivePalette(appearance).text;
    report.check("RT01-faces-colors-underline", boldApplied && alpha != seen.end() && alpha->bold && alpha->italic && alpha->color == lightText &&
                     red != seen.end() && !red->bold && red->color == Color{200, 0, 0} &&
                     std::count_if(seen.begin(), seen.end(), [&](const auto& o) { return o.type == detail::PaintObservation::Type::fill && o.owner == "note" && o.bounds.h <= 2 && o.bounds.w > 8 && o.color == lightText; }) >= 1,
                 std::string("Ctrl+B/I/U through WM_KEYDOWN style the selection; the host draws the run bold+italic with a thin underline rect, and the red run in the light value of its name; bold=") +
                     (alpha != seen.end() && alpha->bold ? "yes" : "no") + ", italic=" + (alpha != seen.end() && alpha->italic ? "yes" : "no"));
    if (!folder.empty()) host.render(600, 260, 1.f, folder / (report.appearance + "-richtext-light.bmp"));  // evidence: faces and colors, light theme
    ui.setPresentation({.variant = ThemeVariant::dark()});
    host.paint();
    const auto& dark = host.paintObservations();
    auto darkRed = std::find_if(dark.begin(), dark.end(), [&](const auto& o) { return o.type == detail::PaintObservation::Type::text && o.owner == "note" && o.text == u"Beta"; });
    report.check("RT02-theme-recolors-by-name", darkRed != dark.end() && darkRed->color == Color{255, 140, 140},
                 "After switching to the dark theme the same color name is drawn in its dark value");
    if (!folder.empty()) host.render(600, 260, 1.f, folder / (report.appearance + "-richtext-dark.bmp"));
    ui.setPresentation({});
    host.observePaint(false); host.paint();

    // Typing takes the previous character's style; the real clipboard carries the styles between two fields
    // and plain text from elsewhere takes the typing style.
    note.select(5, 5);
    post(WM_CHAR, u'x', 1);
    const bool typedBold = detail::access(ui).text("note") == u"Alphax 重要 Beta" && spans(note).front().length == 6;
    detail::WindowsClipboard clipboard(host.window());
    auto saved = clipboard.read();
    bool kept = false, plainOk = false, copied = false;
    if (saved.status == Status::ok || saved.status == Status::notSupported) {
        note.select(0, 5);
        key('C', true);
        other.focus(); host.pump(); host.paint();
        key('V', true);
        kept = other.richValue()->text == u"Alpha" && !spans(other).empty() && spans(other).front().bold && spans(other).front().italic && spans(other).front().underline;
        copied = true;
        other.select(5, 5);
        clipboard.write(u"plain");
        key('V', true);
        plainOk = other.richValue()->text == u"Alphaplain" && !spans(other).empty() && spans(other).front().length == 10;  // plain text from elsewhere follows the previous character
        if (saved.status == Status::ok) clipboard.write(*saved);
    }
    report.check("RT03-typing-and-real-clipboard", typedBold && (!copied || (kept && plainOk)),
                 std::string("A typed character after a bold one is bold=") + (typedBold ? "yes" : "no") + "; Ctrl+C then Ctrl+V into the other field kept the styles (real private clipboard format)=" +
                     (copied ? (kept ? "yes" : "NO") : "skipped (clipboard text could not be saved)") + "; external plain text pasted with the typing style=" + (copied ? (plainOk ? "yes" : "NO") : "skipped"));
}

// #83 twin of tests/word_select.cpp on a real window: posted WM_LBUTTONDOWN messages are counted by the host
// against the system double-click time and box, WM_KEYDOWN with Ctrl goes by words. ASCII text and an explicit
// per-character mode keep the expectations independent of the system dictionary. The real mouse and the user's own
// double-click settings are not involved (the messages are posted), so the author-run trial still covers feel.
void wordSelectNative(const Appearance& appearance, Report& report) {
    Ui ui; ScrollState scroll;
    auto model = std::make_shared<std::u16string>(u"alpha beta,gamma");
    auto fieldHandle = ui.addTextField({.id = "line", .label = u"Line", .value = u"alpha beta,gamma"}).value.value();
    fieldHandle.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    fieldHandle.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    auto noteModel = std::make_shared<std::u16string>(u"one two\nthree four\nfive");
    auto note = ui.addTextField({.id = "note", .label = u"Note", .value = u"one two\nthree four\nfive", .multiline = true, .visibleLines = 4}).value.value();
    note.connectSource({[noteModel] { return *noteModel; }, [](std::function<void()>) { return [] {}; }});
    note.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    fieldHandle.setWordSegmentation(WordSegmentation::perCharacter);
    note.setWordSegmentation(WordSegmentation::perCharacter);
    DesktopConfig options; options.title = u"LcarsUI word selection verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::component(fieldHandle), layout::component(note)}, 12, 12); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.forceScale(1.f);
    host.clientSize(600, 400);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto key = [&](WPARAM vk, bool ctrl = false, bool shift = false) {
        BYTE state[256]{};
        GetKeyboardState(state);
        state[VK_CONTROL] = ctrl ? 0x80 : 0; state[VK_SHIFT] = shift ? 0x80 : 0;
        SetKeyboardState(state);
        post(WM_KEYDOWN, vk, keyLparam(vk, false, false)); post(WM_KEYUP, vk, keyLparam(vk, false, true));
        state[VK_CONTROL] = 0; state[VK_SHIFT] = 0;
        SetKeyboardState(state);
    };
    auto placed = [&](const char* id) { return *detail::findPlacement(host.frame(), id); };  // a copy: repaints replace the frame
    auto selection = [&](const char* id) { return detail::access(ui).edit(id)->selection; };
    // The middle of the character starting at `offset` in the single-line field, and of a row's first characters in the note.
    auto atChar = [&](unsigned offset) { auto p = placed("line"); return POINT{(p.caretX(offset) + p.caretX(offset + 1)) / 2, p.bounds.y + p.textField.editor.y + p.textField.editor.h / 2}; };
    auto click = [&](POINT point) { post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y)); post(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y)); };
    auto range = [&](const char* id) { auto s = selection(id); return std::to_string(std::min(s.anchor, s.caret)) + ".." + std::to_string(std::max(s.anchor, s.caret)); };
    fieldHandle.focus(); host.pump(); host.paint();

    auto collapsed = [&](const char* id) { auto s = selection(id); return s.anchor == s.caret; };
    click(atChar(7));
    const auto single = range("line"); const bool singleCaret = collapsed("line");
    click(atChar(7));
    const auto word = range("line");
    click(atChar(7));
    const auto paragraph = range("line");
    click(atChar(7));
    const auto again = range("line"); const bool againCaret = collapsed("line");
    report.check("WS01-double-and-triple-click", singleCaret && word == "6..10" && paragraph == "0..16" && againCaret,
        "Presses in quick succession on 'e' of beta: caret " + single + ", word " + word + ", whole text " + paragraph + ", fourth press " + again);

    Sleep(GetDoubleClickTime() + 150);
    click(atChar(7)); Sleep(GetDoubleClickTime() + 150); click(atChar(7));
    report.check("WS02-slow-presses-stay-single", collapsed("line"), "Two presses further apart than the system double-click time are two single clicks: " + range("line"));

    Sleep(GetDoubleClickTime() + 150);
    click(atChar(2)); post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(atChar(2).x, atChar(2).y));
    const auto picked = range("line");
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(atChar(12).x, atChar(12).y));
    const auto grown = range("line");
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(atChar(2).x, atChar(2).y));
    const auto shrunk = range("line");
    post(WM_LBUTTONUP, 0, MAKELPARAM(atChar(2).x, atChar(2).y));
    report.check("WS03-drag-by-words", picked == "0..6" && grown == "0..16" && shrunk == "0..6",
        "Holding the second press and dragging grows by words and keeps the first one: " + picked + " -> " + grown + " -> " + shrunk);

    detail::access(ui).select("line", 0, 0); host.paint();
    std::vector<unsigned> stops;
    for (int i = 0; i < 4; ++i) { key(VK_RIGHT, true); stops.push_back(selection("line").caret); }
    key(VK_LEFT, true, true);
    const auto extended = range("line");
    key(VK_BACK, true);
    const auto afterDelete = detail::access(ui).text("line");
    report.check("WS04-ctrl-arrows-and-delete", stops == std::vector<unsigned>{6, 10, 11, 16} && extended == "11..16" && afterDelete == u"alpha beta,",
        "Ctrl+Right stops at 6 10 11 16; Ctrl+Shift+Left selects " + extended + "; Ctrl+Backspace then removes only the selection (text length " + std::to_string(afterDelete.size()) + ")");

    note.focus(); host.pump(); host.paint();
    auto row = [&](int index) { auto p = placed("note"); return POINT{p.view.text.x + 10, p.view.text.y + index * p.view.lineHeight + p.view.lineHeight / 2}; };
    Sleep(GetDoubleClickTime() + 150);
    click(row(1)); click(row(1)); click(row(1));
    report.check("WS05-triple-click-paragraph", range("note") == "8..18",
        "Three quick presses in the middle row of the note select that paragraph without its line feed: " + range("note"));
}

// #84 twin of tests/justify.cpp on a real window with the real shaper: justified lines end flush at the
// edge, real clicks and drags land on the characters as drawn, and the user's "justified text as left"
// setting draws everything left. The glyph pixels themselves and a real input method need the author's
// machine (see the author-run list).
void justifyNative(const Appearance& appearance, Report& report, const std::filesystem::path& folder = {}) {
    const std::u16string text =
        u"LcarsUI 是面向一般桌面應用的 UI 框架，並以讓 AI 操作應用程式作為未來整合方向。這一段中英數混排的文字，用來比較靠左、置中、靠右、"
        u"左右對齊與分散對齊：Windows 11 的 2026 年版本、Direct2D 與 OpenGL 渲染，以及 12345 這類數字。";
    Ui ui; ScrollState scroll;
    auto model = std::make_shared<std::u16string>(text);
    auto sample = ui.addLabel({.id = "sample", .value = text, .textAlign = TextAlign::justify}).value.value();
    auto note = ui.addTextField({.id = "note", .label = u"Note", .value = text, .multiline = true, .visibleLines = 6, .textAlign = TextAlign::justify}).value.value();
    note.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }});
    note.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    DesktopConfig options; options.title = u"LcarsUI justified text verification";
    DesktopHost host(ui, appearance, [&] { return layout::column({layout::component(sample), layout::component(note)}, 12, 12); }, scroll, options, true, true);
    ShowWindow(host.window(), SW_SHOWNOACTIVATE);
    host.forceScale(1.f);
    host.clientSize(560, 460);
    host.pump(); host.paint();
    auto hwnd = host.window();
    auto post = [&](UINT message, WPARAM w, LPARAM l) { PostMessageW(hwnd, message, w, l); host.pump(); host.paint(); };
    auto placed = [&](const char* id) { return *detail::findPlacement(host.frame(), id); };  // a copy: repaints replace the frame
    auto caret = [&]() { return detail::access(ui).edit("note")->selection; };
    detail::Text shaper; detail::ShapedMeasure measure(shaper);

    // JU01: the label. Every line but the last is stretched, its visible text ends at the label's right edge.
    auto label = placed("sample");
    bool labelFlush = label.lines.size() > 2;
    std::string labelDetail;
    for (std::size_t i = 0; i < label.lines.size(); ++i) {
        const auto& line = label.lines[i];
        const bool last = i + 1 == label.lines.size();
        if (last) { labelFlush &= line.segments.empty(); continue; }
        if (line.segments.empty()) { labelFlush = false; labelDetail += " line " + std::to_string(i) + " not stretched;"; continue; }
        const auto contentEnd = static_cast<unsigned>(line.text.find_last_not_of(u' ') + 1);
        float right = 0;
        for (const auto& piece : line.segments)
            if (piece.begin < contentEnd) right = static_cast<float>(line.bounds.x - label.bounds.x) + piece.x + measure.measure(line.text.substr(piece.begin, std::min(piece.end, contentEnd) - piece.begin), label.textSize).width;
        labelFlush &= std::abs(right - static_cast<float>(label.bounds.w)) <= 2.f;
        labelDetail += " line " + std::to_string(i) + " right=" + std::to_string(right) + ";";
    }
    report.check("JU01-label-flush", labelFlush, "A justified label's lines end at its width (" + std::to_string(label.bounds.w) + " px), the last stays left;" + labelDetail);

    // JU02: the multi-line field, same rule at the wrap edge; caret stops equal what is drawn.
    auto field = placed("note");
    bool fieldFlush = field.multiline && field.view.lines.size() > 2;
    float edge = 0; std::string fieldDetail; int stretchedLines = 0;
    for (unsigned i = 0; i < field.view.lines.size(); ++i) {
        const auto& line = field.view.lines[i];
        const auto visible = std::u16string_view(text).substr(line.begin, line.end - line.begin);
        const auto contentEnd = line.begin + static_cast<unsigned>(visible.find_last_not_of(u' ') + 1);
        const float right = field.view.xInLine(i, contentEnd); fieldDetail += " [" + std::to_string(i) + ": soft=" + std::to_string(line.soft) + " pieces=" + std::to_string(line.segments.size()) + " right=" + std::to_string(right) + "]";
        // A line that happens to fill the width exactly (31 wide characters) needs no stretching and has no pieces.
        if (line.soft) { if (edge == 0) edge = right; stretchedLines += line.segments.size() > 1; fieldFlush &= std::abs(right - edge) <= 2.f; }
        else fieldFlush &= line.segments.empty();
    }
    report.check("JU02-field-flush", fieldFlush && stretchedLines > 0 && edge > 0.8f * static_cast<float>(field.view.text.w), "Wrapped lines of the field end at one edge (" + std::to_string(edge) + " px of " + std::to_string(field.view.text.w) + ")" + fieldDetail);

    // JU03: a real click at every drawn caret stop of line 0 puts the caret there.
    detail::access(ui).focus("note"); host.pump(); host.paint();
    field = placed("note");
    const int y0 = field.view.rowY(0) + field.view.rowHeight / 2;
    unsigned wrong = 0, probed = 0;
    std::string wrongDetail;
    const int yReset = field.view.rowY(3) + field.view.rowHeight / 2;
    const auto firstStops = field.view.lines[0].run.stops;  // a copy: repainting replaces the view
    for (const auto& stop : firstStops) {
        const unsigned offset = field.view.lines[0].begin + stop.first;
        const int x = field.view.text.x + static_cast<int>(std::lround(field.view.xInLine(0, offset)));
        // A click elsewhere first, so two probes close together are never taken for a double click.
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(field.view.text.x + 1, yReset)); post(WM_LBUTTONUP, 0, MAKELPARAM(field.view.text.x + 1, yReset));
        post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y0)); post(WM_LBUTTONUP, 0, MAKELPARAM(x, y0));
        ++probed;
        if (caret().caret != offset) { ++wrong; if (wrong <= 5) wrongDetail += " offset " + std::to_string(offset) + " at x=" + std::to_string(x) + " got " + std::to_string(caret().caret) + ";"; }
        field = placed("note");
    }
    report.check("JU03-click-on-stops", probed > 8 && wrong == 0, "Clicking each drawn stop of the first line puts the caret there; probed " + std::to_string(probed) + ", wrong " + std::to_string(wrong) + wrongDetail);

    // JU04: a drag into the second line selects up to the character under the pointer.
    const auto& second = field.view.lines[1];
    const unsigned target = second.begin + second.run.stops[second.run.stops.size() / 2].first;
    const int y1 = field.view.rowY(1) + field.view.rowHeight / 2;
    const int x1 = field.view.text.x + static_cast<int>(std::lround(field.view.xInLine(1, target)));
    post(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(field.view.text.x + 1, y0));
    post(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x1, y1));
    const auto dragged = caret();
    post(WM_LBUTTONUP, 0, MAKELPARAM(x1, y1));
    if (!folder.empty()) host.render(560, 460, 1.f, folder / (report.appearance + "-justify.bmp"));
    report.check("JU04-drag-select", dragged.anchor == 0 && dragged.caret == target, "A drag into the second line selects to the drawn character; selection=" + std::to_string(dragged.anchor) + ".." + std::to_string(dragged.caret) + ", expected " + std::to_string(target));

    // JU05: the user's setting draws both left-aligned and changes no property; the UIA-facing node still says justify.
    auto settings = ui.presentation(); settings.justifyAsLeft = true;
    ui.setPresentation(settings); host.pump(); host.paint();
    label = placed("sample"); field = placed("note");
    bool left = std::all_of(label.lines.begin(), label.lines.end(), [](const Line& l) { return l.segments.empty(); }) &&
                std::all_of(field.view.lines.begin(), field.view.lines.end(), [](const TextFieldLine& l) { return l.segments.empty() && l.shift == 0; });
    report.check("JU05-user-setting", left && *sample.textAlign() == TextAlign::justify && *note.textAlign() == TextAlign::justify,
                 "With 'justified text as left' on both draw left-aligned and keep their property");
}

// Reproduce a clipboard manager briefly holding the OS clipboard during a real key chord.
void clipboardContention(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    auto& ui = detail::access(f.form.ui);
    detail::WindowsClipboard clipboard(f.host.window());
    auto saved = clipboard.read();
    if (saved.status != Status::ok && saved.status != Status::notSupported) {
        report.check("AC08-clipboard-contention", false, "Could not save clipboard text; test did not overwrite it");
        return;
    }
    ui.setValue(f.ids.name, u"Alpha");
    ui.focus(f.ids.name);
    ui.select(f.ids.name, 0, 5);

    auto heldDuring = [&](const std::function<void()>& action) {
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
    bool heldCopy = heldDuring([&] { f.chord('C'); });
    auto copied = clipboard.read();
    bool supplied = clipboard.write(u"X\r\nY");
    f.chord('A');
    bool heldPaste = heldDuring([&] { f.chord('V'); });
    auto pasted = ui.text(f.ids.name);
    bool restored = saved.status == Status::notSupported || (saved && clipboard.write(*saved));
    report.check("AC08-clipboard-contention", heldCopy && (copied && *copied == u"Alpha") && supplied && heldPaste && pasted == u"X Y" && restored,
                 std::string("60ms clipboard hold: copy=") + ((copied && *copied == u"Alpha") ? "ok" : "failed") + ", paste=" + (pasted == u"X Y" ? "ok" : "failed") +
                     ", fixture write=" + (supplied ? "ok" : "failed") + ", clipboard text restore=" + (saved.status == Status::notSupported ? "no text saved" : restored ? "ok" : "failed"));
}

void checkboxAndRadioNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.handles.notices.focus(); f.host.pump();
    f.tap(VK_SPACE);
    bool toggled = !*f.form.handles.notices.draft();
    f.host.paint();
    auto box = f.host.frame().find(f.form.handles.notices);
    bool clickedBox = false;
    if (box) {
        auto r = box->bounds; POINT p{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
        clickedBox = *f.form.handles.notices.draft();
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
        clickedBox = clickedBox && !*f.form.handles.notices.draft();
    }
    f.form.handles.mode.focus(); f.host.pump();
    f.tap(VK_RIGHT);
    bool arrow = *f.form.handles.mode.draft() == OptionValue{"focus"};
    f.form.handles.offlineDisabled.invoke();
    f.host.paint();
    auto item = f.host.frame().find(f.form.handles.mode);
    bool hadOption = item && item->options.size() == 3;
    if (hadOption) {
        auto r = item->options[2]; POINT p{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
    }
    bool disabled = *f.form.handles.mode.draft() == OptionValue{"focus"};
    f.form.handles.mode.enableOption({"offline"}, true);
    f.host.paint(); item = f.host.frame().find(f.form.handles.mode);
    if (item && item->options.size() == 3) {
        auto r = item->options[2]; POINT p{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
    }
    bool clicked = *f.form.handles.mode.draft() == OptionValue{"offline"};
    bool applied = bool(f.form.handles.apply.invoke()) && !f.form.noticesApplied() && f.form.modeApplied() == OptionValue{"offline"};
    report.check("F09-checkbox-radio", toggled && clickedBox && arrow && hadOption && disabled && clicked && applied,
        "Space toggled=" + std::to_string(toggled) + ", checkbox clicks=" + std::to_string(clickedBox) + ", arrow=" + std::to_string(arrow) + ", disabled click ignored=" + std::to_string(disabled) +
        ", enabled click=" + std::to_string(clicked) + ", apply=" + std::to_string(applied));
}

void sliderNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.form.handles.volume.focus(); f.settle();
    f.tap(VK_RIGHT);
    bool step = *f.form.handles.volume.draft() == 45 && f.form.volumeApplied() == 40;
    f.tap(VK_HOME); bool home = *f.form.handles.volume.draft() == 0;
    f.tap(VK_END); bool end = *f.form.handles.volume.draft() == 100;
    f.host.paint();
    const auto* item = f.host.frame().find(f.form.handles.volume);
    bool dragged = false, held = false, released = false, canceled = false;
    if (item) {
        auto r = item->sliderTrack; const std::string id = item->id;
        POINT start{r.x, r.y + r.h / 2}, middle{r.x + r.w / 2, r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON);
        bool atStart = *f.form.handles.volume.draft() == 0;
        f.mouse(WM_MOUSEMOVE, middle, MK_LBUTTON);
        f.mouse(WM_LBUTTONUP, middle);
        dragged = atStart && *f.form.handles.volume.draft() == 50;
        // #73: native messages hold the thumb under the pointer until release or interruption.
        auto& state = detail::access(f.form.ui);
        POINT offTick{r.x + static_cast<LONG>(r.w * 0.47), r.y + r.h / 2};
        f.mouse(WM_LBUTTONDOWN, offTick, MK_LBUTTON);
        held = state.sliderDragX(id) == offTick.x;
        f.mouse(WM_LBUTTONUP, offTick);
        released = !state.sliderDragX(id);
        f.mouse(WM_LBUTTONDOWN, offTick, MK_LBUTTON);
        SendMessageW(f.host.window(), WM_CANCELMODE, 0, 0);
        canceled = !state.sliderDragX(id);
        f.mouse(WM_LBUTTONUP, offTick);
        f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, middle, MK_LBUTTON); f.mouse(WM_LBUTTONUP, middle);  // back to 50 for the apply check
    }
    dragged = dragged && held && released && canceled;
    bool applied = f.form.volumeApplied() == 40 && bool(f.form.handles.apply.invoke()) && f.form.volumeApplied() == 50;
    f.form.handles.disabled.invoke();
    bool disabled = f.form.handles.volume.setValue(75).status == Status::disabled && *f.form.handles.volume.draft() == 50;
    report.check("F09-slider", step && home && end && dragged && applied && disabled,
        "arrow=" + std::to_string(step) + ", Home=" + std::to_string(home) + ", End=" + std::to_string(end) +
        ", native drag=" + std::to_string(dragged) + " (held=" + std::to_string(held) + ", released=" + std::to_string(released) + ", canceled=" + std::to_string(canceled) + "), apply=" + std::to_string(applied) + ", disabled=" + std::to_string(disabled));
}

// #75: the host drives the shared animation clock. Frames are scheduled only while a transition runs,
// the Windows animation preference (simulated here) and a hidden window end and prevent transitions.
void sliderAnimationNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    bool systemOn = true;
    f.host.systemAnimationSource([&] { return systemOn; });
    f.host.hiddenSource([&] { return IsIconic(f.host.window()) != FALSE; });
    auto& ui = f.form.ui;
    auto animating = [&] { return ui.animating().value.value_or(false); };
    auto pumpFor = [&](int milliseconds) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (std::chrono::steady_clock::now() < end) {
            f.host.pump();
            MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        f.host.pump();
    };
    // Sent synchronously, so the answer is read before a slow frame could outlast the 150 ms transition.
    auto tapNow = [&](WPARAM vk) {
        SendMessageW(f.host.window(), WM_KEYDOWN, vk, keyLparam(vk, false, false));
        SendMessageW(f.host.window(), WM_KEYUP, vk, keyLparam(vk, false, true));
    };
    f.form.handles.volume.focus(); f.settle();
    // A key starts a transition; frames follow while it runs and stop once it has ended.
    const int framesBefore = f.host.frames();
    tapNow(VK_RIGHT);
    const bool started = animating() && *f.form.handles.volume.draft() == 45;
    pumpFor(100);
    const int framesDuring = f.host.frames() - framesBefore;
    pumpFor(400);
    const bool ended = !animating();
    const int settled = f.host.frames(); pumpFor(300);
    const bool idle = f.host.frames() == settled;
    report.check("A75-schedule", started && framesDuring >= 2 && ended && idle,
        "started=" + std::to_string(started) + ", frames during=" + std::to_string(framesDuring) + ", ended=" + std::to_string(ended) + ", no frames after=" + std::to_string(idle));
    // The system preference, changed while running, lands the transition at once and holds for later changes.
    tapNow(VK_RIGHT); const bool running = animating();
    systemOn = false; SendMessageW(f.host.window(), WM_SETTINGCHANGE, SPI_SETCLIENTAREAANIMATION, 0); f.host.pump();
    const bool landed = !animating();
    tapNow(VK_RIGHT); const bool instant = !animating() && *f.form.handles.volume.draft() == 55;
    systemOn = true; SendMessageW(f.host.window(), WM_SETTINGCHANGE, SPI_SETCLIENTAREAANIMATION, 0); f.host.pump();
    tapNow(VK_LEFT); const bool again = animating();
    report.check("A75-system-setting", running && landed && instant && again,
        "running=" + std::to_string(running) + ", landed on change=" + std::to_string(landed) + ", instant while off=" + std::to_string(instant) + ", plays after on=" + std::to_string(again));
    pumpFor(400);
    // #72 x #75: both tickets follow WM_SETTINGCHANGE through ONE case. The "everything changed" broadcast (wParam 0)
    // re-reads the keyboard-cue setting AND the animation preference; a single-setting message still re-reads the other.
    {
        auto& state = detail::access(ui);
        BOOL cuesOn = FALSE; SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &cuesOn, 0);
        state.pointerPressed();  // hide what the earlier key presses showed, so only the system value can show the indicator
        state.setSystemKeyboardCues(!cuesOn);  // the recorded values are stale on purpose
        systemOn = false;
        SendMessageW(f.host.window(), WM_SETTINGCHANGE, 0, 0); f.host.pump();
        const bool cuesReread = ui.focusIndicatorVisible() == (cuesOn != FALSE);
        tapNow(VK_RIGHT);
        const bool animationReread = !animating();  // off now: the arrow key lands at once
        state.pointerPressed();
        systemOn = true;
        SendMessageW(f.host.window(), WM_SETTINGCHANGE, SPI_SETKEYBOARDCUES, 0); f.host.pump();
        tapNow(VK_LEFT);
        const bool animationBack = animating();  // a keyboard-cues message also re-read the animation preference
        report.check("A75-setting-change-shared", cuesReread && animationReread && animationBack,
            "cues re-read=" + std::to_string(cuesReread) + ", animation re-read on broadcast=" + std::to_string(animationReread) +
            ", animation re-read on SPI_SETKEYBOARDCUES=" + std::to_string(animationBack));
        pumpFor(400);
    }
    // Minimized: the transition ends at once and nothing is drawn while minimized.
    tapNow(VK_RIGHT); const bool beforeMinimize = animating();
    ShowWindow(f.host.window(), SW_MINIMIZE); f.host.pump();
    const bool minimizedEnded = IsIconic(f.host.window()) && !animating();
    const int minimizedFrames = f.host.frames(); pumpFor(200);
    const bool noRedraw = f.host.frames() == minimizedFrames;
    ShowWindow(f.host.window(), SW_SHOWNOACTIVATE); if (IsIconic(f.host.window())) ShowWindow(f.host.window(), SW_RESTORE);
    f.host.pump(); f.settle();
    tapNow(VK_LEFT); const bool afterRestore = !IsIconic(f.host.window()) && animating();
    report.check("A75-minimized", beforeMinimize && minimizedEnded && noRedraw && afterRestore,
        "running=" + std::to_string(beforeMinimize) + ", ended on minimize=" + std::to_string(minimizedEnded) + ", no frames while minimized=" + std::to_string(noRedraw) + ", plays after restore=" + std::to_string(afterRestore));
    pumpFor(400);
    // Fully covered by another opaque window (the real check, without the test reader).
    f.host.hiddenSource({});
    RECT client{}; GetClientRect(f.host.window(), &client);
    POINT corner{0, 0}; ClientToScreen(f.host.window(), &corner);
    HWND cover = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP | WS_VISIBLE,
        corner.x - 20, corner.y - 20, client.right + 40, client.bottom + 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    f.host.pump();
    tapNow(VK_RIGHT);
    const bool coveredInstant = cover && !animating() && f.host.windowHidden();
    if (cover) DestroyWindow(cover);
    f.host.hiddenSource([&] { return false; });
    tapNow(VK_LEFT); const bool uncoveredPlays = animating();
    report.check("A75-covered", coveredInstant && uncoveredPlays,
        "covering window created=" + std::to_string(cover != nullptr) + ", instant while covered=" + std::to_string(coveredInstant) + ", plays once uncovered=" + std::to_string(uncoveredPlays));
}

void dropdownNative(const Appearance& appearance, Report& report) {
    Fixture f(appearance);
    f.host.clientSize(900, 1000); f.settle();
    auto& region = f.form.handles.region;
    region.focus(); f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN);
    bool candidate = region.describe()->expanded && region.describe()->candidate == OptionValue{"south"} && *region.draft() == OptionValue{"north"};
    f.tap(VK_ESCAPE);
    bool escape = !region.describe()->expanded && region.describe()->focused && *region.draft() == OptionValue{"north"};
    f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN); f.tap(VK_TAB);
    bool tab = !region.describe()->expanded && f.form.handles.volume.describe()->focused && *region.draft() == OptionValue{"north"};
    region.focus(); f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN);
    auto oldIntro = f.form.handles.intro.describe()->value;
    auto target = f.center(f.ids.longText);
    f.mouse(WM_LBUTTONDOWN, target, MK_LBUTTON); f.mouse(WM_LBUTTONUP, target);
    bool outside = !region.describe()->expanded && *region.draft() == OptionValue{"north"} &&
        f.form.handles.longText.describe()->focused && f.form.handles.intro.describe()->value != oldIntro;
    region.focus(); f.tap(VK_RETURN); f.host.paint();
    bool pointer = false;
    if (!f.host.frame().overlays.empty() && f.host.frame().overlays[0].options.size() == 3) {
        auto rect = f.host.frame().overlays[0].options[2];
        POINT pt{rect.x + rect.w / 2, rect.y + rect.h / 2};
        f.mouse(WM_LBUTTONDOWN, pt, MK_LBUTTON); f.mouse(WM_LBUTTONUP, pt);
        pointer = *region.draft() == OptionValue{"south"} && !region.describe()->expanded && f.form.regionApplied() == OptionValue{"north"};
    }
    report.check("F07-dropdown-native", candidate && escape && tab && outside && pointer,
        "candidate=" + std::to_string(candidate) + ", Escape=" + std::to_string(escape) + ", Tab=" + std::to_string(tab) +
        ", outside action=" + std::to_string(outside) + ", popup click=" + std::to_string(pointer));
}

void tabWalkAndVisibleFocus(const Appearance& appearance, Report& report, const std::filesystem::path& folder, const std::string& tag) {
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
    auto r = detail::findPlacement(frame, f.ids.apply)->bounds;
    auto colorAt = [&](int x, int y) {
        auto i = (static_cast<std::size_t>(y) * pixels.width + x) * 4;
        return Color{pixels.pixels[i], pixels.pixels[i + 1], pixels.pixels[i + 2]};
    };
    auto accent = appearance.palette().accent, ring = colorAt(r.x + 1, r.y + r.h / 2);
    bool visible = ring.r == accent.r && ring.g == accent.g && ring.b == accent.b;
    const Placement* other = nullptr;
    for (const auto& item : frame.items) {
        auto visible = intersect(item.bounds, item.clip);
        if (item.kind == ComponentKind::button && item.id != f.ids.apply && visible.w > 2 && visible.h == item.bounds.h &&
            item.bounds.x >= 0 && item.bounds.y >= 0 && item.bounds.y + item.bounds.h <= pixels.height) { other = &item; break; }
    }
    bool distinct = false;
    if (other) {
        auto plain = colorAt(other->bounds.x + 1, other->bounds.y + other->bounds.h / 2);
        distinct = !(plain.r == accent.r && plain.g == accent.g && plain.b == accent.b);
    }
    std::string order;
    for (const auto& id : seen) order += id + " ";
    bool wraps = seen.size() == 15 && seen[6] == f.ids.name && seen[7] == f.ids.notices && seen[8] == f.ids.mode && seen[9] == f.ids.region && seen[10] == f.ids.volume && seen[11] == f.ids.offlineDisabled && seen[12] == f.ids.regionEmpty && seen[13] == f.ids.apply && seen[14] == seen[0];
    report.check("AC11-tab", wraps && back && visible && distinct,
                 "Tab order: " + order + "; Shift+Tab back to apply=" + (back ? "yes" : "no") + "; focused apply ring pixel is accent=" +
                     (visible ? "yes" : "NO") + ", unfocused reset ring is not=" + (distinct ? "yes" : "NO") + " (" + tag + "-focus-apply.bmp)");
}

WNDPROC r06OriginalProc = nullptr;
LRESULT CALLBACK r06ClampedProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_WINDOWPOSCHANGING) {
        auto* pos = reinterpret_cast<WINDOWPOS*>(l);
        if (!(pos->flags & SWP_NOSIZE)) { pos->cx = std::min(pos->cx, 1024); pos->cy = std::min(pos->cy, 768); }
    }
    return CallWindowProcW(r06OriginalProc, window, message, w, l);
}

void resizeFirstFrameMatchesNext(const Appearance& appearance, Report& report, float scale, bool forceClamp = false) {
    Fixture f(appearance);
    f.host.forceScale(scale);
    detail::access(f.form.ui).invoke(f.ids.longText);
    f.host.clientSize(640, 480);
    f.host.pump();
    UpdateWindow(f.host.window());
    POINT wheelpoint{20, 20}; ClientToScreen(f.host.window(), &wheelpoint);
    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120 * 40)), MAKELPARAM(wheelpoint.x, wheelpoint.y));
    f.host.pump();
    UpdateWindow(f.host.window());
    int deep = f.host.frame().offset, deepMax = f.host.frame().maximum;
    RECT initialClient{};
    GetClientRect(f.host.window(), &initialClient);
    if (forceClamp) r06OriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(f.host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&r06ClampedProc)));
    int before = f.host.frames();
    f.host.clientSize(720, 540);
    f.host.pump();
    UpdateWindow(f.host.window());
    if (forceClamp) SetWindowLongPtrW(f.host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(r06OriginalProc));
    Frame first = f.host.frame();
    RECT client{};
    GetClientRect(f.host.window(), &client);
    const int width = client.right - client.left, height = client.bottom - client.top;
    const int requestedWidth = static_cast<int>(720 * scale), requestedHeight = static_cast<int>(540 * scale);
    const bool clamped = width != requestedWidth || height != requestedHeight;
    int painted = f.host.frames() - before;
    InvalidateRect(f.host.window(), nullptr, FALSE);
    UpdateWindow(f.host.window());
    const Frame& second = f.host.frame();
    bool same = first.items.size() == second.items.size() && first.offset == second.offset &&
                first.maximum == second.maximum && first.viewport == second.viewport;
    for (std::size_t i = 0; same && i < first.items.size(); ++i)
        same = first.items[i].id == second.items[i].id && first.items[i].bounds == second.items[i].bounds &&
               first.items[i].clip == second.items[i].clip;
    auto target = detail::findPlacement(first, f.ids.apply)->bounds;
    const auto* hit = second.hit(target.x + target.w / 2, target.y + target.h / 2);
    const auto* firstHit = first.hit(target.x + target.w / 2, target.y + target.h / 2);
    bool visible = target.x >= 0 && target.y >= 0 && target.x + target.w <= width && target.y + target.h <= height;
    bool scrollOk = clamped ? first.offset == std::min(deep, first.maximum) : first.offset == first.maximum && first.offset < deep;
    report.check(forceClamp ? "R06-native-150-clamped" : scale > 1 ? "R06-native-150" : "R06-native-100", deep > 0 && deep == deepMax && painted == 1 && width > 0 && height > 0 && (width != initialClient.right || height != initialClient.bottom) && first.viewport.w == width && first.maximum > 0 && scrollOk && (!forceClamp || clamped) && same && visible && firstHit && firstHit->id == f.ids.apply && hit && hit->id == f.ids.apply,
                 "640x480 wheel to offset " + std::to_string(deep) + "/" + std::to_string(deepMax) + "; SetWindowPos 720x540 -> " +
                     std::to_string(painted) + " WM_PAINT, requested client " + std::to_string(requestedWidth) + "x" + std::to_string(requestedHeight) +
                     ", actual " + std::to_string(width) + "x" + std::to_string(height) + (clamped ? " (clamped)" : " (unclamped)") + ", first frame offset " + std::to_string(first.offset) + " max " +
                     std::to_string(first.maximum) + ", identical to the next frame=" + (same ? "yes" : "NO") + ", apply at " + str(target) +
                     " hit=" + (hit ? hit->id : std::string("none")));
}

bool overlaps(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

void layoutMatrix(const Appearance& appearance, Report& report, const std::filesystem::path& folder, const std::string& tag) {
    struct Size { int w, h; };
    for (auto size : {Size{960, 720}, Size{640, 480}}) {
        for (float scale : {1.f, 1.5f}) {
            for (int variant = 0; variant < 2; ++variant) {
                Fixture f(appearance);
                auto& ui = f.form.ui;
                if (variant == 1) {
                    detail::access(ui).invoke(f.ids.longText);
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
                auto field = detail::findPlacement(frame, f.ids.name)->bounds, apply = detail::findPlacement(frame, f.ids.apply)->bounds;
                auto draft = detail::findPlacement(frame, f.ids.draft)->bounds;
                bool arrangement = size.w == 960 ? draft.x > field.x && draft.y < apply.y + apply.h : draft.x == field.x && draft.y > apply.y;
                // Every keyboard entry can be scrolled fully into the viewport.
                int reachable = 0, entries = 0;
                for (const auto& id : detail::access(ui).focusOrder()) {
                    ScrollState probe;
                    auto revealed = f.host.layout(w, h, scale, probe, id);
                    auto b = detail::findPlacement(revealed, id)->bounds;
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
void pageSizeScaleMatrix(const Appearance& appearance, Report& report) {
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
            for (const auto& id : detail::access(f.form.ui).focusOrder()) {
                ScrollState probe;
                auto revealed = f.host.layout(w, ht, scale, probe, id);
                auto* item = detail::findPlacement(revealed, id);
                ++entries;
                if (!item) continue;
                auto area = intersect(item->bounds, item->clip);
                auto* hit = area.w && area.h ? revealed.hit(area.x + area.w / 2, area.y + area.h / 2) : nullptr;
                if ((area == item->bounds || item->kind == ComponentKind::list) && hit && hit->id == id) ++reached;
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
void popupDismissalMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            auto& h = f.form.handles;
            f.host.forceScale(scale); f.host.clientSize(size.first, size.second); f.settle();
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(static_cast<int>(scale * 100)) + "%";
            auto anchorVisibility = [&](const auto& handle) {
                const auto* item = f.host.frame().find(handle);
                if (!item) return std::string("absent");
                auto part = intersect(item->bounds, item->clip);
                if (part.w <= 0 || part.h <= 0) return std::string("clipped");
                return part == item->bounds ? std::string("visible") : std::string("partial");
            };
            auto& region = h.region;
            auto prepareDropdown = [&] {
                region.focus(); f.settle(); f.tap(VK_RETURN); f.tap(VK_DOWN); f.tap(VK_DOWN); f.settle();
                return region.describe()->expanded && region.describe()->candidate == OptionValue{"south"} &&
                    *region.draft() == OptionValue{"north"};
            };
            bool prepared = prepareDropdown();
            f.tap(VK_ESCAPE); f.settle();
            bool escaped = !region.describe()->expanded && region.describe()->focused && *region.draft() == OptionValue{"north"} &&
                f.host.frame().overlays.empty();
            prepared = prepareDropdown() && prepared;
            f.tap(VK_TAB); f.settle();
            bool tab = !region.describe()->expanded && h.volume.describe()->focused && *region.draft() == OptionValue{"north"} &&
                f.host.frame().overlays.empty();
            prepared = prepareDropdown() && prepared;
            auto* tabs = f.host.frame().find(h.tabs);
            bool outside = false;
            if (tabs && !tabs->options.empty()) {
                auto rect = tabs->options.front(); POINT p{rect.x + rect.w / 2, rect.y + rect.h / 2};
                auto* hit = f.host.frame().hit(p.x, p.y);
                bool targetsTabs = hit && hit->id == h.tabs.describe()->id && !hit->popup;
                f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p); f.settle();
                hit = f.host.frame().hit(p.x, p.y);
                outside = targetsTabs && hit && hit->id == h.tabs.describe()->id && !hit->popup &&
                    !region.describe()->expanded && h.tabs.describe()->focused && *region.draft() == OptionValue{"north"} &&
                    *h.tabs.active() == OptionValue{"v01"};
            }
            report.check("X02-v01-dismiss", prepared && escaped && tab && outside,
                sample + ": candidate=" + std::to_string(prepared) + ", Esc=" + std::to_string(escaped) +
                ", Tab=" + std::to_string(tab) + ", outside=" + std::to_string(outside));

            prepared = prepareDropdown();
            auto scrollRegion = f.host.frame().regions.front();
            int beforeScroll = f.form.scroll.offset;
            POINT wheelPoint{scrollRegion.clip.x + 2, scrollRegion.clip.y + 2};
            ClientToScreen(f.host.window(), &wheelPoint);
            // Choose a direction with room to move, away from the dropdown panel.
            int wheelDelta = beforeScroll < scrollRegion.maximum ? -120 : 120;
            f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(wheelDelta)), MAKELPARAM(wheelPoint.x, wheelPoint.y));
            f.settle();
            bool wheelCancel = f.form.scroll.offset != beforeScroll && !region.describe()->expanded &&
                !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
            report.check("X02-v01-wheel-cancel", prepared && wheelCancel,
                sample + ": moved and canceled=" + std::to_string(wheelCancel) + ", anchor=" + anchorVisibility(region));

            prepared = prepareDropdown();
            auto popup = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(),
                                      [](const Placement& p) { return p.popup && !p.tooltip; });
            bool popupWheel = false;
            if (popup != f.host.frame().overlays.end()) {
                POINT p{popup->bounds.x + popup->bounds.w / 2, popup->bounds.y + popup->bounds.h / 2};
                ClientToScreen(f.host.window(), &p);
                int old = f.form.scroll.offset;
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(p.x, p.y)); f.settle();
                popupWheel = f.form.scroll.offset == old && region.describe()->expanded &&
                    region.describe()->candidate == OptionValue{"south"};
            }
            report.check("X02-v01-popup-wheel", prepared && popupWheel,
                sample + ": wheel over panel does not scroll background or cancel=" + std::to_string(popupWheel));
            popup = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(),
                                 [](const Placement& p) { return p.popup && !p.tooltip; });
            // The assembled dropdown has three fixed rows. Record whether the panel can
            // overflow at this actual size instead of treating a missing list driver as a pass.
            if (popup != f.host.frame().overlays.end()) {
                auto optionCount = popup->options.size();
                int rowsHeight = optionCount ? popup->options.back().y + popup->options.back().h - popup->bounds.y : 0;
                if (rowsHeight <= popup->bounds.h)
                    report.na("X02-v01-panel-list-scroll", sample + ": " + std::to_string(optionCount) +
                        " options; rows=" + std::to_string(rowsHeight) + "px, panel=" + std::to_string(popup->bounds.h) + "px; no overflow");
                else
                    report.check("X02-v01-panel-list-scroll", false, sample + ": panel overflows; independent list scroll must be tested");
            } else report.check("X02-v01-panel-list-scroll", false, sample + ": open panel missing");
            // WM_KEYDOWN dispatches to the focused component; only list navigation schedules
            // reveal. A dropdown owns the keyboard while open and is not a scrollable region.
            report.na("X02-popup-keyboard-region-scroll", sample + ": host offers no keyboard scrolling of a region while a popup is open");
            region.cancel();
            prepared = prepareDropdown();
            scrollRegion = f.host.frame().regions.front();
            beforeScroll = f.form.scroll.offset;
            POINT trackPoint{scrollRegion.track.x + scrollRegion.track.w / 2,
                beforeScroll < scrollRegion.maximum ? scrollRegion.track.y + scrollRegion.track.h - 1 : scrollRegion.track.y};
            f.mouse(WM_LBUTTONDOWN, trackPoint, MK_LBUTTON); f.mouse(WM_LBUTTONUP, trackPoint); f.settle();
            bool trackCancel = f.form.scroll.offset != beforeScroll && !region.describe()->expanded &&
                !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
            report.check("X02-v01-track-cancel", prepared && trackCancel,
                sample + ": track moved and canceled=" + std::to_string(trackCancel) + ", anchor=" + anchorVisibility(region));

            prepared = prepareDropdown();
            scrollRegion = f.host.frame().regions.front();
            beforeScroll = f.form.scroll.offset;
            POINT thumbPoint{scrollRegion.thumb.x + scrollRegion.thumb.w / 2, scrollRegion.thumb.y + scrollRegion.thumb.h / 2};
            POINT dragPoint{thumbPoint.x, beforeScroll < scrollRegion.maximum ? scrollRegion.track.y + scrollRegion.track.h - 1 : scrollRegion.track.y};
            f.mouse(WM_LBUTTONDOWN, thumbPoint, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, dragPoint, MK_LBUTTON);
            f.mouse(WM_LBUTTONUP, dragPoint); f.settle();
            bool thumbCancel = f.form.scroll.offset != beforeScroll && !region.describe()->expanded &&
                !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
            report.check("X02-v01-thumb-cancel", prepared && thumbCancel,
                sample + ": thumb moved and canceled=" + std::to_string(thumbCancel) + ", anchor=" + anchorVisibility(region));

            // Exercise each input at each reachable clipped endpoint, using only actual page
            // geometry. A one-pixel wheel step models a high-resolution wheel message.
            h.longText.invoke(); f.host.paint();
            for (int input = 0; input < 3; ++input) for (bool partial : {true, false}) {
                auto geometry = f.host.frame();
                auto* anchor = geometry.find(region);
                auto bar = geometry.regions.front();
                int originalTop = anchor->bounds.y + f.form.scroll.offset;
                auto heightAt = [&](int offset) {
                    int top = originalTop - offset;
                    return std::max(0, std::min(top + anchor->bounds.h, anchor->clip.y + anchor->clip.h) - std::max(top, anchor->clip.y));
                };
                auto desired = [&](int offset) { int visible = heightAt(offset); return partial ? visible > 0 && visible < anchor->bounds.h : visible == 0; };
                int target = -1, before = -1, pointerY = 0;
                if (input == 0) {
                    for (int n = 0; n <= bar.maximum && target < 0; ++n) if (desired(n)) {
                        for (int pre : {n - 1, n + 1}) if (pre >= 0 && pre <= bar.maximum && heightAt(pre) > 0) {
                            target = n; before = pre; break;
                        }
                    }
                } else {
                    for (int y = bar.track.y; y < bar.track.y + bar.track.h && target < 0; ++y) {
                        int top = y - bar.thumb.h / 2;
                        int n = bar.offsetForThumb(top);
                        if (!desired(n)) continue;
                        for (int start = 0; start <= bar.maximum; ++start) {
                            if (n == start || heightAt(start) <= 0) continue;
                            int thumbTop = bar.track.y + (bar.track.h - bar.thumb.h) * start / std::max(1, bar.maximum);
                            if (input == 1 && y >= thumbTop && y < thumbTop + bar.thumb.h) continue;
                            target = n; before = start; pointerY = y; break;
                        }
                    }
                }
                const char* caseId = input == 0 ? "X02-v01-edge-wheel" : input == 1 ? "X02-v01-edge-track" : "X02-v01-edge-thumb";
                auto detail = sample + (partial ? " partial" : " clipped");
                if (target < 0) { report.na(caseId, detail + ": no visible starting offset and reachable endpoint; anchor top=" +
                    std::to_string(originalTop) + ", height=" + std::to_string(anchor->bounds.h) + ", clip=" + str(anchor->clip) +
                    ", maximum=" + std::to_string(bar.maximum) + ", track=" + str(bar.track) + ", thumb height=" + std::to_string(bar.thumb.h)); continue; }
                geometry.scrollTo(f.form.ui, f.form.scroll, before); f.host.paint();
                region.focus(); region.open(); f.tap(VK_DOWN); f.tap(VK_DOWN); f.settle();
                bool candidate = region.describe()->expanded && region.describe()->candidate == OptionValue{"south"};
                if (input == 0) {
                    POINT p{bar.clip.x + 2, bar.clip.y + 2}; ClientToScreen(f.host.window(), &p);
                    int unit = int(std::ceil(120.0 / (48 * scale)));
                    int delta = target > before ? -unit : unit;
                    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(p.x, p.y));
                } else if (input == 1) {
                    POINT p{bar.track.x + bar.track.w / 2, pointerY};
                    f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
                } else {
                    auto thumb = f.host.frame().regions.front().thumb;
                    POINT start{thumb.x + thumb.w / 2, thumb.y + thumb.h / 2};
                    POINT end{start.x, pointerY};
                    f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, end, MK_LBUTTON); f.mouse(WM_LBUTTONUP, end);
                }
                f.settle();
                auto actual = anchorVisibility(region);
                bool pass = candidate && f.form.scroll.offset != before && actual == (partial ? "partial" : "clipped") &&
                    !region.describe()->expanded && !region.describe()->candidate && region.describe()->focused && *region.draft() == OptionValue{"north"};
                report.check(caseId, pass, detail + ": anchor=" + actual + ", canceled=" + std::to_string(pass));
            }
            h.longText.invoke(); f.host.paint();

            h.tabs.activate({"v02"});
            auto& listPage = *f.form.list;
            listPage.handles.hundred.invoke(); listPage.handles.list.focus();
            f.tap(VK_END); f.settle();
            auto* list = f.host.frame().find(listPage.handles.list);
            bool end = list && list->options.size() == 100 && test::inside(list->options.back(), list->clip) &&
                listPage.selection() == OptionValue{"T100"} && listPage.listScroll.offset > 0;
            bool wheel = false, hit = false;
            if (list) {
                auto visible = intersect(list->bounds, list->clip);
                POINT p{visible.x + visible.w / 2, visible.y + visible.h / 2};
                auto* target = f.host.frame().hit(p.x, p.y);
                hit = target && target->id == listPage.handles.list.describe()->id;
                int oldOffset = listPage.listScroll.offset;
                ClientToScreen(f.host.window(), &p);
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(120)), MAKELPARAM(p.x, p.y)); f.settle();
                wheel = listPage.listScroll.offset < oldOffset && listPage.selection() == OptionValue{"T100"};
            }
            f.tap(VK_END); f.settle();
            list = f.host.frame().find(listPage.handles.list);
            bool restored = list && test::inside(list->options.back(), list->clip) &&
                listPage.selection() == OptionValue{"T100"};
            report.check("X02-v02-list", end && hit && wheel && restored,
                sample + ": End=" + std::to_string(end) + ", hit=" + std::to_string(hit) +
                ", wheel=" + std::to_string(wheel) + ", End again=" + std::to_string(restored));

            h.tabs.activate({"v03"}); f.settle();
            auto openDialog = [&] { h.dialogOpen.focus(); h.dialogOpen.invoke(); f.settle(); return *h.dialog.isOpen(); };
            bool opened = openDialog();
            auto frame = f.host.frame();
            int before = f.form.scroll.offset;
            bool hasViewport = !frame.regions.empty();
            int maximum = hasViewport ? frame.regions.front().maximum : 0;
            bool firstWheelUnchanged = false;
            if (hasViewport) {
                auto viewport = frame.regions.front().clip;
                POINT p{viewport.x + 2, viewport.y + 2}; ClientToScreen(f.host.window(), &p);
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(120)), MAKELPARAM(p.x, p.y));
                f.settle(); firstWheelUnchanged = f.form.scroll.offset == before;
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), MAKELPARAM(p.x, p.y));
                f.settle();
            }
            bool blocked = hasViewport && firstWheelUnchanged && f.form.scroll.offset == before && *h.dialog.isOpen() && h.dialogCancel.describe()->focused;
            f.tap(VK_ESCAPE); f.settle();
            bool dialogEscape = !*h.dialog.isOpen() && h.dialogOpen.describe()->focused && f.form.progress() == 50 &&
                f.form.dialogConfirmations() == 0 && !f.host.frame().modal;
            opened = openDialog() && opened;
            auto clickButton = [&](const ButtonHandle& button) {
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
            bool cancelHit = clickButton(h.dialogCancel);
            bool cancel = cancelHit && !*h.dialog.isOpen() && h.dialogOpen.describe()->focused && f.form.progress() == 50 &&
                f.form.dialogConfirmations() == 0 && !f.host.frame().modal;
            opened = openDialog() && opened;
            bool confirmHit = clickButton(h.dialogConfirm);
            bool confirm = confirmHit && !*h.dialog.isOpen() && h.dialogOpen.describe()->focused && f.form.progress() == 0 &&
                f.form.dialogConfirmations() == 1 && !f.host.frame().modal;
            report.check("X02-v03-dialog", opened && blocked && dialogEscape && cancel && confirm,
                sample + ": modal wheel=" + (maximum == 0 ? std::string("N/A (no overflow)") : std::to_string(blocked)) + ", scroll maximum=" + std::to_string(maximum) + ", Esc=" + std::to_string(dialogEscape) +
                ", Cancel=" + std::to_string(cancel) + ", Confirm=" + std::to_string(confirm));

            std::uint64_t now = GetTickCount64();
            auto& state = detail::access(f.form.ui);
            state.tooltipClock([&] { return now; });
            SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
            f.post(WM_MOUSELEAVE, 0, 0); f.host.pump();
            h.progressHalf.focus(); f.tap(VK_RIGHT); f.settle(); now += 600; state.tooltipTime(now); f.settle();
            bool shown = *h.progressTip.visible() && h.progressHalf.describe()->focused;
            auto tipScroll = f.host.frame().regions.front();
            bool tipWheel = tipScroll.maximum == 0;
            bool dwell = tipWheel;
            if (tipScroll.maximum > 0) {
                int old = f.form.scroll.offset;
                POINT point{tipScroll.clip.x + 2, tipScroll.clip.y + 2}; ClientToScreen(f.host.window(), &point);
                int delta = old < tipScroll.maximum ? -120 : 120;
                f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), MAKELPARAM(point.x, point.y)); f.settle();
                tipWheel = f.form.scroll.offset != old && !*h.progressTip.visible() && h.progressHalf.describe()->focused;
                now += 499; state.tooltipTime(now); bool waiting = !*h.progressTip.visible();
                ++now; state.tooltipTime(now); dwell = waiting && *h.progressTip.visible();
                f.host.paint();
            }
            if (tipScroll.maximum == 0) report.na("X02-v03-tooltip-wheel", sample + ": no background overflow");
            else report.check("X02-v03-tooltip-wheel", shown && tipWheel && dwell,
                sample + ": moved and hid=" + std::to_string(tipWheel) + ", fresh dwell=" + std::to_string(dwell) + ", anchor=" + anchorVisibility(h.progressHalf));
            for (bool thumb : {false, true}) {
                const char* caseId = thumb ? "X02-v03-tooltip-thumb" : "X02-v03-tooltip-track";
                auto bar = f.host.frame().regions.front();
                if (!bar.maximum) { report.na(caseId, sample + ": no background overflow"); continue; }
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
                bool hid = f.form.scroll.offset != old && !*h.progressTip.visible() && h.progressHalf.describe()->focused;
                f.tap(VK_RIGHT);  // the scrollbar press hid the indicator; a key shows it and starts the fresh dwell
                now += 499; state.tooltipTime(now); bool waiting = !*h.progressTip.visible();
                ++now; state.tooltipTime(now); bool resumed = *h.progressTip.visible();
                f.host.paint();
                report.check(caseId, hid && waiting && resumed,
                    sample + ": moved and hid=" + std::to_string(hid) + ", fresh dwell=" + std::to_string(waiting && resumed) + ", anchor=" + anchorVisibility(h.progressHalf));
            }
            // Search the actual V03 geometry for every edge/input pair. Preparation
            // sets an offset, while the scroll under test enters through Win32 messages.
            for (int input = 0; input < 3; ++input) for (bool partial : {true, false}) {
                h.progressHalf.focus(); f.settle();
                auto geometry = f.host.frame();
                auto* anchor = geometry.find(h.progressHalf);
                auto bar = geometry.regions.front();
                const char* caseId = input == 0 ? "X02-v03-edge-wheel" : input == 1 ? "X02-v03-edge-track" : "X02-v03-edge-thumb";
                std::string detail = sample + (partial ? " partial" : " clipped");
                if (!anchor) { report.check(caseId, false, detail + ": tooltip source missing"); continue; }
                int originalTop = anchor->bounds.y + f.form.scroll.offset;
                auto heightAt = [&](int offset) {
                    int top = originalTop - offset;
                    return std::max(0, std::min(top + anchor->bounds.h, anchor->clip.y + anchor->clip.h) - std::max(top, anchor->clip.y));
                };
                auto desired = [&](int offset) { int visible = heightAt(offset); return partial ? visible > 0 && visible < anchor->bounds.h : visible == 0; };
                int target = -1, before = -1, pointerY = 0;
                if (input == 0) {
                    for (int n = 0; n <= bar.maximum && target < 0; ++n) if (desired(n))
                        for (int pre : {n - 1, n + 1}) if (pre >= 0 && pre <= bar.maximum && heightAt(pre) > 0) {
                            target = n; before = pre; break;
                        }
                } else {
                    for (int y = bar.track.y; y < bar.track.y + bar.track.h && target < 0; ++y) {
                        int n = bar.offsetForThumb(y - bar.thumb.h / 2);
                        if (!desired(n)) continue;
                        for (int start = 0; start <= bar.maximum; ++start) {
                            if (n == start || heightAt(start) <= 0) continue;
                            int thumbTop = bar.track.y + (bar.track.h - bar.thumb.h) * start / std::max(1, bar.maximum);
                            if (input == 1 && y >= thumbTop && y < thumbTop + bar.thumb.h) continue;
                            target = n; before = start; pointerY = y; break;
                        }
                    }
                }
                if (target < 0) {
                    report.na(caseId, detail + ": no visible start and reachable endpoint; anchor top=" + std::to_string(originalTop) +
                        ", height=" + std::to_string(anchor->bounds.h) + ", clip=" + str(anchor->clip) +
                        ", maximum=" + std::to_string(bar.maximum) + ", track=" + str(bar.track) + ", thumb height=" + std::to_string(bar.thumb.h));
                    continue;
                }
                geometry.scrollTo(f.form.ui, f.form.scroll, before); f.host.paint();
                now += 600; state.tooltipTime(now); f.host.paint();
                bool opened = *h.progressTip.visible();
                if (input == 0) {
                    POINT p{bar.clip.x + 2, bar.clip.y + 2}; ClientToScreen(f.host.window(), &p);
                    int unit = int(std::ceil(120.0 / (48 * scale)));
                    f.post(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(target > before ? -unit : unit)), MAKELPARAM(p.x, p.y));
                } else if (input == 1) {
                    POINT p{bar.track.x + bar.track.w / 2, pointerY};
                    f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p);
                } else {
                    auto thumb = f.host.frame().regions.front().thumb;
                    POINT start{thumb.x + thumb.w / 2, thumb.y + thumb.h / 2}; POINT end{start.x, pointerY};
                    f.mouse(WM_LBUTTONDOWN, start, MK_LBUTTON); f.mouse(WM_MOUSEMOVE, end, MK_LBUTTON); f.mouse(WM_LBUTTONUP, end);
                }
                f.settle();
                auto actual = anchorVisibility(h.progressHalf);
                bool pass = opened && f.form.scroll.offset != before && actual == (partial ? "partial" : "clipped") &&
                    !*h.progressTip.visible() && h.progressHalf.describe()->focused;
                report.check(caseId, pass, detail + ": before=" + std::to_string(before) + ", target=" + std::to_string(target) +
                    ", actual offset=" + std::to_string(f.form.scroll.offset) + ", anchor=" + actual + ", opened=" + std::to_string(opened));
            }
            // Pointer-only eligibility: a scroll at 400 ms cancels the old deadline;
            // a new complete 500 ms dwell may show the tip if the anchor remains under it.
            h.progressHalf.focus(); f.settle();
            auto hoverBar = f.host.frame().regions.front();
            if (!hoverBar.maximum) report.na("X02-v03-pointer-scroll-dwell", sample + ": no background overflow");
            else {
                h.tabs.focus(); f.settle();
                auto source = f.host.frame().find(h.progressHalf);
                auto visible = intersect(source->bounds, source->clip);
                POINT p{visible.x + visible.w / 2, visible.y + visible.h / 2};
                SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0);
                SendMessageW(f.host.window(), WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
                now += 400; state.tooltipTime(now); f.host.paint();
                bool pending = !*h.progressTip.visible();
                int old = f.form.scroll.offset;
                POINT wheel{hoverBar.clip.x + 2, hoverBar.clip.y + 2}; ClientToScreen(f.host.window(), &wheel);
                int unit = int(std::ceil(120.0 / (48 * scale)));
                // Do not pump unrelated TrackMouseEvent leaves caused by the physical
                // desktop cursor: this scenario explicitly owns a synthetic pointer visit.
                SendMessageW(f.host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(old < hoverBar.maximum ? -unit : unit)), MAKELPARAM(wheel.x, wheel.y));
                f.host.paint();
                now += 100; state.tooltipTime(now); f.host.paint(); bool stale = !*h.progressTip.visible();
                now += 399; state.tooltipTime(now); f.host.paint(); bool waits = !*h.progressTip.visible();
                ++now; state.tooltipTime(now); f.host.paint(); bool fresh = *h.progressTip.visible();
                int firstOffset = f.form.scroll.offset;
                SendMessageW(f.host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(firstOffset < hoverBar.maximum ? -unit : unit)), MAKELPARAM(wheel.x, wheel.y));
                f.host.paint(); bool closed = !*h.progressTip.visible() && firstOffset != f.form.scroll.offset;
                now += 499; state.tooltipTime(now); f.host.paint(); bool secondWait = !*h.progressTip.visible();
                ++now; state.tooltipTime(now); f.host.paint(); bool reopened = *h.progressTip.visible();
                report.check("X02-v03-pointer-scroll-dwell", pending && old != firstOffset && stale && waits && fresh && closed && secondWait && reopened,
                    sample + ": pending=" + std::to_string(pending) + ", stale=" + std::to_string(stale) +
                    ", waits=" + std::to_string(waits) + ", fresh=" + std::to_string(fresh) + ", closed=" + std::to_string(closed) +
                    ", reopened after fresh dwell=" + std::to_string(secondWait && reopened));
                SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0);
            }
            h.progressHalf.focus(); f.tap(VK_RIGHT); f.settle(); now += 600; state.tooltipTime(now); f.host.paint();
            bool transparent = false;
            auto tip = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(), [](const auto& item) { return item.tooltip; });
            if (tip != f.host.frame().overlays.end()) {
                auto* target = f.host.frame().hit(tip->bounds.x + 1, tip->bounds.y + 1);
                transparent = !target || !target->tooltip;
            }
            f.tap(VK_ESCAPE); f.settle();
            bool tipEscape = !*h.progressTip.visible() && h.progressHalf.describe()->focused && f.form.progress() == 0;
            f.tap(VK_TAB); h.progressHalf.focus(); f.settle(); now += 600; state.tooltipTime(now); f.settle();
            bool reshown = *h.progressTip.visible();
            f.tap(VK_TAB); f.settle();
            bool focusLeave = !*h.progressTip.visible() && !h.progressHalf.describe()->focused && f.form.progress() == 0;
            h.progressHalf.focus(); f.settle();
            auto p = f.center(h.progressHalf.describe()->id);
            f.mouse(WM_LBUTTONDOWN, p, MK_LBUTTON); f.mouse(WM_LBUTTONUP, p); f.settle();
            // Deliver the synthetic pointer visit synchronously: TrackMouseEvent may also queue
            // a leave for the actual desktop pointer, which is unrelated to this posted click.
            SendMessageW(f.host.window(), WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
            now += 600; state.tooltipTime(now); f.host.paint();
            bool pointerShown = *h.progressTip.visible() && f.form.progress() == 50;
            SendMessageW(f.host.window(), WM_MOUSELEAVE, 0, 0); f.host.paint();
            bool pointerLeave = !*h.progressTip.visible() && h.progressHalf.describe()->focused && f.form.progress() == 50;
            report.check("X02-v03-tooltip", shown && transparent && tipEscape && reshown && focusLeave && pointerShown && pointerLeave,
                sample + ": shown=" + std::to_string(shown) + ", transparent=" + std::to_string(transparent) +
                ", Esc=" + std::to_string(tipEscape) + ", focus leave=" + std::to_string(focusLeave && reshown) +
                ", pointer shown=" + std::to_string(pointerShown) + ", pointer leave=" + std::to_string(pointerLeave) + " (controlled dwell clock)");
            state.tooltipClock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
        }
}

void x04ClientSizeMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) {
        Fixture f(appearance, true, alternate);
        for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
            f.x04Size(size.first, size.second, scale);
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
void semanticObservationSweep(const Appearance& appearance, const std::filesystem::path& folder, const std::string& tag) {
    std::ofstream out(folder / (tag + "-x04-sweep.tsv"));
    out << "ids\tsize\tscale\tpage\tcomponent\tkind\tvisible\tname\tvalue\trestriction\thidden\tclipped\tdisabled\tread_only\tselected\tcandidate\terror\tconflict\texecuting\tmodal\ttip\n";
    for (bool alternate : {false, true}) {
        Fixture f(appearance, true, alternate);
        auto& h = f.form.handles;
        auto entries = x04::inventory(f.form);
        f.host.observePaint(true);
        for (auto size : {std::pair{960, 720}, std::pair{640, 480}}) for (float scale : {1.f, 1.5f}) {
            f.x04Size(size.first, size.second, scale);
            for (const auto& page : x04::pages(f.form)) {
                h.tabs.activate({page}); f.settle();
                for (const auto& entry : entries) {
                    auto s = x04::describe(entry.handle); if (!s) continue;
                    bool active = std::string_view(entry.page) == "shared" || std::string_view(entry.page) == page;
                    const Placement* placement = x04::placement(f.host.frame(), entry.handle);
                    std::u16string ink;
                    bool any = false;
                    for (const auto& draw : f.host.paintObservations()) {
                        if (draw.owner != s->id && (s->target.empty() || draw.owner != s->target)) continue;
                        if (!draw.visible) continue;
                        any = true; if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    }
                    auto textStatus = [&](const std::u16string& text) {
                        if (text.empty()) return "N/A";
                        if (!active || !s->visible) return "O";
                        return ink.find(text) != std::u16string::npos ? "P" : "O";
                    };
                    auto applicable = [](bool yes) { return yes ? "O" : "N/A"; };
                    auto k = s->kind;
                    const auto& type = detail::componentType(k);
                    using S = detail::ComponentState;
                    bool restricted = type.supportsState(S::disabled);
                    bool selected = type.supportsState(S::selected);
                    bool candidate = type.supportsState(S::candidate);
                    bool error = type.supportsState(S::error);
                    bool conflict = type.supportsState(S::conflict) && !x04::conflictInapplicable(f.form, entry);
                    bool associated = !s->target.empty();
                    auto scroll = x04::enclosingScroll(f.form, *s);
                    out << (alternate ? "alternate" : "shipped") << '\t' << size.first << 'x' << size.second << '\t' << int(scale * 100)
                        << '\t' << page << '\t' << s->id << '\t' << static_cast<int>(k) << '\t' << (s->visible ? "yes" : "no")
                        << '\t' << textStatus(s->name) << '\t' << textStatus(s->value) << '\t' << applicable(restricted) << '\t'
                        << (!active && std::string_view(entry.page) != "modal" && !s->visible && !any && !placement && !associated ? "P" : active ? "N/A" : "O") << '\t'
                        << (scroll && !*scroll ? "N/A" : active && placement && !s->visible && !any && !associated ? "P" : "O") << '\t'
                        << applicable(restricted) << '\t' << applicable(type.supportsState(S::readOnly)) << '\t' << applicable(selected) << '\t'
                        << applicable(candidate) << '\t' << applicable(error) << '\t' << applicable(conflict) << '\t'
                        << applicable(type.supportsState(S::executing)) << '\t' << applicable(std::string_view(entry.page) == "modal") << '\t'
                        << applicable(s->id == h.progressHalf.describe()->id) << '\n';
                }
            }
        }
    }
}

// A page switch checks all inactive members at once; results retain each component ID
// and require both of the other pages, including associated labels painted by a field.
void x04HiddenMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true);
            auto entries = x04::inventory(f.form);
            struct Outcome { int pages = 0; bool passed = true; };
            std::map<std::string, Outcome> results;
            for (const auto& page : x04::pages(f.form)) {
                bool switched = bool(f.form.handles.tabs.activate({page})); f.settle();
                for (const auto& entry : entries) {
                    if (entry.page == "shared" || entry.page == "modal" || entry.page == page) continue;
                    auto semantic = x04::describe(entry.handle);
                    if (!semantic) continue;
                    bool painted = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(),
                        [&](const auto& draw) { return draw.visible && (draw.owner == semantic->id ||
                            (!semantic->target.empty() && draw.owner == semantic->target)); });
                    auto& result = results[semantic->id]; ++result.pages;
                    result.passed = result.passed && switched && !semantic->visible && !painted &&
                        !x04::placement(f.host.frame(), entry.handle) &&
                        detail::access(f.form.ui).available(semantic->id).status == Status::hidden &&
                        x04::hiddenRetry(entry.handle);
                }
            }
            bool closed = bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialogOpen.invoke());
            f.settle();
            closed = closed && bool(f.form.handles.dialogCancel.invoke()); f.settle();
            for (const auto& entry : entries) if (entry.page == "modal") {
                auto semantic = x04::describe(entry.handle);
                bool painted = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(),
                    [&](const auto& draw) { return draw.visible && draw.owner == semantic->id; });
                bool rejected = detail::access(f.form.ui).available(semantic->id).status == Status::hidden &&
                    detail::access(f.form.ui).pointerDown(semantic->id).status == Status::hidden && x04::hiddenRetry(entry.handle);
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
void x04FixedPlacementMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04Size(size.first, size.second, scale);
            bool opened = bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialogOpen.invoke());
            f.settle();
            int width = int(std::lround(size.first * scale)), height = int(std::lround(size.second * scale));
            for (const auto& entry : x04::inventory(f.form)) {
                auto semantic = x04::describe(entry.handle); auto scroll = x04::enclosingScroll(f.form, *semantic);
                if (!scroll || *scroll) continue;
                auto* item = x04::placement(f.host.frame(), entry.handle);
                bool inside = item && x04::insideClient(*item, width, height);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-fixed-placement", opened && inside, sample + ": no enclosing scroll; inside client=" + std::to_string(inside));
            }
        }
}

void x04SelectedMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baselineContext(f.form);
                auto baselineModel = x04::snapshot(f.form);
                auto presentation = f.form.ui.presentation();
                auto before = x04::describe(entry.handle);
                if (!before || !detail::componentType(before->kind).supportsState(detail::ComponentState::selected)) continue;
                bool page = entry.page == "shared" || bool(f.form.handles.tabs.activate({std::string(entry.page)}));
                bool driven = baseline && page && x04::driveSelected(entry);
                auto semantic = x04::describe(entry.handle);
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, semantic->id);
                f.host.paint(); semantic = x04::describe(entry.handle);
                const auto palette = appearance.palette(f.form.ui.presentation().variant);
                auto cue = semantic->kind == ComponentKind::checkbox || semantic->kind == ComponentKind::radioGroup
                    ? palette.accent : palette.selection;
                std::u16string ink; bool cueVisible = false;
                for (const auto& draw : f.host.paintObservations()) {
                    if (draw.owner != semantic->id || !draw.visible) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    if (draw.color.r == cue.r && draw.color.g == cue.g && draw.color.b == cue.b &&
                        draw.type != detail::PaintObservation::Type::text) cueVisible = true;
                }
                auto expected = semantic->kind == ComponentKind::checkbox ? semantic->name : semantic->value;
                bool compared = semantic->visible && x04::placement(f.host.frame(), entry.handle) &&
                    x04::selectedSemantics(*semantic) && !expected.empty() && ink.find(expected) != std::u16string::npos && cueVisible;
                bool restored = x04::restoreSelected(f.form, entry, *before, presentation) && f.form.ui.presentation() == presentation && x04::sameModel(baselineModel, x04::snapshot(f.form));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-selected-member", driven && compared && restored, sample +
                    ": driven=" + std::to_string(driven) + ", semantics and visible cue=" + std::to_string(compared) +
                    ", initial selection restored=" + std::to_string(restored));
            }
        }
}

void x04ExecutingMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                auto* button = std::get_if<ButtonHandle>(&entry.handle);
                if (!button || !detail::componentType(button->describe()->kind).supportsState(detail::ComponentState::executing) ||
                    !x04::executionSupported(f.form, *button)) continue;
                bool baselineOk = x04::baselineContext(f.form); auto baseline = x04::snapshot(f.form);
                auto before = button->describe(); bool prepared = x04::prepareExecution(f.form, entry), observed = false;
                bool bound = x04::observeExecution(f.form, *button, [&] {
                    f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, before->id);
                    f.host.paint(); auto semantic = button->describe(); auto* item = f.host.frame().find(*button);
                    std::u16string ink;
                    for (const auto& draw : f.host.paintObservations()) {
                        if (!item || draw.owner != semantic->id || !draw.visible || draw.type != detail::PaintObservation::Type::text) continue;
                        if (intersect(draw.bounds, intersect(draw.clip, item->bounds)).w == draw.bounds.w) ink += draw.text;
                    }
                    observed = item && semantic->visible && semantic->executing && button->invoke().status == Status::busy &&
                        std::find(semantic->verbs.begin(), semantic->verbs.end(), Verb::invoke) == semantic->verbs.end() &&
                        ink.find(semantic->name) != std::u16string::npos &&
                        ink.find(appearance.componentPresentation().executingSuffix) != std::u16string::npos;
                    return Result{};
                });
                bool invoked = bound && bool(button->invoke()) && observed && !*button->executing();
                bool restored = x04::restoreExecution(f.form, entry, before->enabled) && x04::sameModel(baseline, x04::snapshot(f.form));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + before->id;
                report.check("X04-executing-member", baselineOk && prepared && bound && invoked && restored,
                    sample + ": prepared=" + std::to_string(prepared) + ", callback semantics, busy retry and paint=" +
                    std::to_string(invoked) + ", original action restored=" + std::to_string(restored));
            }
            bool action = bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.progressZero.invoke()) && f.form.progress() == 0;
            report.check("X04-executing-restore", action, std::string(alternate ? "alternate" : "shipped") + " " +
                std::to_string(size.first) + "x" + std::to_string(size.second) + " " + std::to_string(int(scale * 100)) +
                "%: restored progress action updates the application model");
        }
}

void x04ClippedMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto inventory = x04::inventory(f.form);
            f.x04Size(size.first, size.second, scale); f.host.observePaint(true);
            for (const auto& entry : inventory) {
                if (entry.page == "shared" || entry.page == "modal") continue;
                auto baseline = x04::snapshot(f.form);
                bool prepared = x04::prepareClipped(f.form, entry);
                f.form.scroll.offset = 0; f.host.paint();
                auto semantic = x04::describe(entry.handle); auto initialRect = x04::clippedRect(f.host.frame(), *semantic);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
                if (!prepared || !initialRect || f.host.frame().maximum <= 0) {
                    bool restored = x04::restoreClipped(f.form, entry) && x04::sameModel(baseline, x04::snapshot(f.form));
                    if (!restored) report.check("X04-clipped-member", false, sample + ": fixture did not restore the model");
                    else report.open("X04-clipped-member", sample + ": fixture could not produce a scrollable target");
                    continue;
                }
                bool fully = false, partial = false, correct = true;
                for (int offset : x04::clippedOffsets(f.host.frame(), *initialRect)) {
                    f.form.scroll.offset = offset; f.host.paint(); semantic = x04::describe(entry.handle);
                    auto geometry = x04::clippedRect(f.host.frame(), *semantic);
                    auto* owner = x04::clippedOwner(f.host.frame(), *semantic);
                    if (!geometry || !owner) { correct = false; continue; }
                    auto area = intersect(*geometry, owner->clip);
                    const std::vector<Line>* associated = nullptr;
                    if (!semantic->target.empty()) {
                        if (semantic->labelKind == LabelKind::name) associated = &owner->labelLines;
                        else if (semantic->labelKind == LabelKind::description) associated = &owner->descriptionLines;
                        else if (semantic->labelKind == LabelKind::error) associated = &owner->errorLines;
                    }
                    bool visiblePaint = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& draw) {
                        if (draw.owner != owner->id || !draw.visible) return false;
                        if (!associated) return true;
                        return draw.type == detail::PaintObservation::Type::text && std::any_of(associated->begin(), associated->end(),
                            [&](const Line& line) { return line.text == draw.text; });
                    });
                    if (area.w == 0 || area.h == 0) {
                        auto* hit = f.host.frame().hit(geometry->x + std::max(0, geometry->w / 2), geometry->y + std::max(0, geometry->h / 2));
                        correct = correct && !semantic->visible && !visiblePaint && (!hit || hit->id != owner->id); fully = true;
                    } else if (area != *geometry) {
                        correct = correct && semantic->visible;
                        if (semantic->visible && visiblePaint) partial = true;
                    }
                }
                bool restored = x04::restoreClipped(f.form, entry) && x04::sameModel(baseline, x04::snapshot(f.form));
                correct = correct && restored;
                if (!correct) report.check("X04-clipped-member", false, sample + ": clipping comparison or model restoration disagreed");
                else if (!fully || !partial) report.open("X04-clipped-member", sample + ": fixture reached fully=" + std::to_string(fully) +
                    ", partially clipped visible paint=" + std::to_string(partial));
                else report.check("X04-clipped-member", true, sample + ": full and partial clipping, semantics, visible paint and hit agree");
            }
        }
}

void x04ErrorMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baselineContext(f.form);
                auto semantic = x04::describe(entry.handle);
                if (!semantic || !detail::componentType(semantic->kind).supportsState(detail::ComponentState::error) ||
                    !x04::errorSupported(*semantic)) continue;
                bool driven = baseline && x04::driveError(f.form, entry);
                auto owner = semantic->target.empty() ? semantic->id : semantic->target;
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, owner);
                f.host.paint(); semantic = x04::describe(entry.handle);
                auto* item = detail::findPlacement(f.host.frame(), owner);
                std::u16string ink; bool color = false; auto errorColor = appearance.palette().error;
                for (const auto& draw : f.host.paintObservations()) {
                    if (draw.owner != owner || !draw.visible) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    if (draw.color.r == errorColor.r && draw.color.g == errorColor.g && draw.color.b == errorColor.b) color = true;
                }
                bool cue = semantic->kind == ComponentKind::tabs ? ink.find(appearance.componentPresentation().invalidTabMarker) != std::u16string::npos :
                    ink.find(x04::errorMessage) != std::u16string::npos && color;
                bool semanticState = x04::errorSemantics(f.form, *semantic);
                bool compared = item && semantic->visible && semanticState && cue;
                bool restored = x04::restoreError(f.form, entry) && !x04::errorSemantics(f.form, *x04::describe(entry.handle));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-error-member", driven && compared && restored,
                    sample + ": driven=" + std::to_string(driven) + ", semantics, association and cue=" + std::to_string(compared) +
                    ", restored=" + std::to_string(restored));
            }
        }
}

void x04CandidateMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baselineContext(f.form);
                auto baselineModel = x04::snapshot(f.form);
                auto before = x04::describe(entry.handle);
                if (!before || !detail::componentType(before->kind).supportsState(detail::ComponentState::candidate)) continue;
                bool prepared = baseline && x04::prepareCandidate(f.form, entry);
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, before->id); f.host.paint();
                bool opened = x04::openCandidate(entry);
                auto vk = before->kind == ComponentKind::tabs ? VK_RIGHT : VK_DOWN;
                SendMessageW(f.host.window(), WM_KEYDOWN, vk, keyLparam(vk, false, false));
                SendMessageW(f.host.window(), WM_KEYUP, vk, keyLparam(vk, false, true));
                f.host.paint(); auto semantic = x04::describe(entry.handle);
                auto* item = x04::candidatePlacement(f.host.frame(), *semantic); auto label = x04::candidateLabel(*semantic);
                auto cue = semantic->kind == ComponentKind::dropdown ? appearance.palette().selection : appearance.palette().accent;
                std::u16string ink; bool cueVisible = false;
                for (const auto& draw : f.host.paintObservations()) {
                    if (draw.owner != semantic->id || !draw.visible || draw.tooltip ||
                        (semantic->kind == ComponentKind::dropdown) != draw.popup) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    else if (draw.color.r == cue.r && draw.color.g == cue.g && draw.color.b == cue.b) cueVisible = true;
                }
                bool compared = item && semantic->visible && label && ink.find(*label) != std::u16string::npos && cueVisible;
                if (semantic->kind == ComponentKind::tabs || semantic->kind == ComponentKind::dropdown) compared = compared && semantic->value == before->value;
                bool restored = x04::restoreCandidate(f.form, entry) && x04::sameModel(baselineModel, x04::snapshot(f.form));
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-candidate-member", prepared && opened && compared && restored,
                    sample + ": prepared=" + std::to_string(prepared && opened) + ", semantics and cue=" + std::to_string(compared) +
                    ", restored=" + std::to_string(restored));
            }
        }
}

void x04SearchReadOnlyMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& search = f.form.list->handles.search;
            f.x04Size(size.first, size.second, scale); f.host.observePaint(true);
            bool driven = x04::driveSearchReadOnly(f.form) && bool(search.focus());
            f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, search.describe()->id);
            f.host.paint(); auto semantic = search.describe(); std::u16string ink;
            for (const auto& draw : f.host.paintObservations())
                if (draw.owner == semantic->id && draw.visible && draw.type == detail::PaintObservation::Type::text) ink += draw.text;
            bool restricted = search.setValue(u"blocked").status == Status::readOnly;
            SendMessageW(f.host.window(), WM_CHAR, L'x', 0);
            restricted = restricted && search.describe()->value == semantic->value;
            bool compared = semantic->visible && semantic->enabled && semantic->readOnly &&
                ink.find(semantic->name) != std::u16string::npos && ink.find(semantic->value) != std::u16string::npos &&
                ink.find(appearance.componentPresentation().readOnlySuffix) != std::u16string::npos;
            bool restored = x04::restoreSearchReadOnly(f.form) && !search.describe()->readOnly;
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
            report.check("X04-search-read-only", driven && restricted && compared && restored,
                sample + ": restricted=" + std::to_string(restricted) + ", paint=" + std::to_string(compared) + ", restored=" + std::to_string(restored));
        }
}

void x04ModalMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true); bool opened = x04::openModal(f.form); f.host.paint();
            bool blocked = f.host.frame().modal && f.form.handles.progressZero.invoke().status == Status::blockedByModal;
            struct Row { std::string id; bool passed; }; std::vector<Row> rows;
            for (const auto& entry : x04::inventory(f.form)) {
                if (entry.page != "modal") continue;
                auto semantic = x04::describe(entry.handle); auto* item = x04::placement(f.host.frame(), entry.handle);
                std::u16string ink;
                for (const auto& draw : f.host.paintObservations())
                    if (draw.owner == semantic->id && draw.visible && draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                bool compared = opened && blocked && item && semantic->visible && x04::normalSemantics(*semantic);
                for (const auto& expected : x04::normalText(*semantic)) compared = compared && ink.find(expected) != std::u16string::npos;
                rows.push_back({semantic->id, compared});
            }
            bool restored = x04::closeModal(f.form) && !*f.form.handles.dialog.isOpen();
            for (const auto& row : rows) {
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                    std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + row.id;
                report.check("X04-modal-member", row.passed && restored,
                    sample + ": modal semantics, paint and background rejection=" + std::to_string(row.passed) + ", restored=" + std::to_string(restored));
            }
        }
}

void x04TooltipMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& state = detail::access(f.form.ui); std::uint64_t now = 100;
            f.x04Size(size.first, size.second, scale); f.host.observePaint(true);
            state.tooltipClock([&] { return now; }); SendMessageW(f.host.window(), WM_SETFOCUS, 0, 0);
            bool driven = x04::prepareTip(f.form); f.tap(VK_RIGHT); auto semantic = f.form.handles.progressHalf.describe();
            f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, semantic->id);
            f.host.paint(); now += 600; state.tooltipTime(now); f.host.paint();
            auto tip = std::find_if(f.host.frame().overlays.begin(), f.host.frame().overlays.end(), [](const auto& p) { return p.tooltip; });
            bool visible = *f.form.handles.progressTip.visible() && tip != f.host.frame().overlays.end();
            std::u16string ink;
            for (const auto& draw : f.host.paintObservations())
                if (draw.owner == semantic->id && draw.tooltip && draw.visible && draw.type == detail::PaintObservation::Type::text) ink += draw.text;
            bool transparent = false;
            if (visible) { auto* hit = f.host.frame().hit(tip->bounds.x + 1, tip->bounds.y + 1); transparent = !hit || !hit->tooltip; }
            bool text = visible && !semantic->description.empty() && ink.find(semantic->description) != std::u16string::npos;
            SendMessageW(f.host.window(), WM_KEYDOWN, VK_ESCAPE, keyLparam(VK_ESCAPE, false, false));
            SendMessageW(f.host.window(), WM_KEYUP, VK_ESCAPE, keyLparam(VK_ESCAPE, false, true));
            bool restored = !*f.form.handles.progressTip.visible() && f.form.handles.progressHalf.describe()->focused;
            state.tooltipClock([] { return static_cast<std::uint64_t>(GetTickCount64()); });
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "% " + semantic->id;
            report.check("X04-tip-member", driven && text && transparent && restored,
                sample + ": text=" + std::to_string(text) + ", transparent=" + std::to_string(transparent) + ", restored=" + std::to_string(restored));
        }
}

void x04NormalMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& state = detail::access(f.form.ui);
            f.x04Size(size.first, size.second, scale); f.host.observePaint(true);
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baselineContext(f.form);
                bool modal = entry.page == "modal";
                bool page = modal ? bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialogOpen.invoke()) :
                    entry.page == "shared" || bool(f.form.handles.tabs.activate({std::string(entry.page)}));
                auto semantic = x04::describe(entry.handle);
                auto owner = semantic->target.empty() ? semantic->id : semantic->target;
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, owner);
                f.host.paint(); semantic = x04::describe(entry.handle);
                auto* item = detail::findPlacement(f.host.frame(), owner);
                std::u16string ink; bool visibleOwnerInk = false, accent = false, selected = false;
                auto palette = appearance.palette();
                for (const auto& draw : f.host.paintObservations()) {
                    const bool blankPiece = draw.type == detail::PaintObservation::Type::text && x04::blankOnly(draw.text);  // no ink, but part of the text
                    if (draw.owner != owner || !(draw.visible || blankPiece)) continue;
                    visibleOwnerInk = true;
                    if (draw.type == detail::PaintObservation::Type::text && item &&
                        intersect(draw.bounds, intersect(draw.clip, item->bounds)).w == draw.bounds.w) ink += draw.text;
                    if (draw.type != detail::PaintObservation::Type::text) {
                        if (draw.color.r == palette.accent.r && draw.color.g == palette.accent.g && draw.color.b == palette.accent.b) accent = true;
                        if (draw.color.r == palette.selection.r && draw.color.g == palette.selection.g && draw.color.b == palette.selection.b) selected = true;
                    }
                }
                bool compared = baseline && page && x04::normalSemantics(*semantic);
                if (!semantic->target.empty()) {
                    auto target = state.describe(semantic->target);
                    compared = compared && target && std::find(target->labels.begin(), target->labels.end(), semantic->id) != target->labels.end();
                }
                if (!semantic->visible && semantic->kind == ComponentKind::label && semantic->value.empty()) {
                    compared = compared && (semantic->target.empty() ? !visibleOwnerInk :
                        semantic->labelKind == LabelKind::error && state.describe(semantic->target)->error.empty());
                } else {
                    compared = compared && item && semantic->visible;
                    for (const auto& expected : x04::normalText(*semantic)) compared = compared && ink.find(expected) != std::u16string::npos;
                    if (semantic->kind == ComponentKind::checkbox && semantic->checked == true) compared = compared && accent;
                    if (semantic->kind == ComponentKind::radioGroup && !semantic->value.empty()) compared = compared && accent;
                    if ((semantic->kind == ComponentKind::list || semantic->kind == ComponentKind::tabs) && !semantic->value.empty()) compared = compared && selected;
                    if (semantic->kind == ComponentKind::progress && item && semantic->numericValue)
                        compared = compared && item->progressFill.w == item->progressTrack.w * *semantic->numericValue / 100 &&
                            (*semantic->numericValue == 0 || accent);
                }
                bool restored = (!modal || (bool(f.form.handles.dialog.settle(false)) && bool(f.form.handles.resetV03.invoke()))) &&
                    x04::baselineContext(f.form);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-normal-member", compared && restored, sample +
                    ": visible=" + std::to_string(semantic->visible) + ", semantics/paint=" + std::to_string(compared) +
                    ", restored=" + std::to_string(restored));
            }
        }
}

void x04DisabledMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04Size(size.first, size.second, scale); f.host.observePaint(true);
            auto reveal = [&](const std::string& id) {
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, id); f.host.paint();
            };
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baselineContext(f.form);
                auto baselineModel = x04::snapshot(f.form);
                auto initial = x04::describe(entry.handle);
                if (!initial || !detail::componentType(initial->kind).supportsState(detail::ComponentState::disabled)) continue;
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + initial->id;
                if (initial->kind == ComponentKind::tabs) {
                    auto tabs = std::get<TabsHandle>(entry.handle);
                    bool driven = baseline && bool(tabs.activate({"v01"})) && bool(tabs.enablePage({"v02"}, false)) && bool(tabs.focus());
                    f.host.paint(); auto sem = tabs.describe(); auto* placed = f.host.frame().find(tabs);
                    bool cue = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& draw) {
                        return draw.owner == sem->id && draw.visible && draw.type == detail::PaintObservation::Type::text &&
                               draw.text.find(appearance.componentPresentation().disabledTabMarker) != std::u16string::npos;
                    });
                    bool rejected = tabs.activate({"v02"}).status == Status::disabled && !sem->items[1].enabled;
                    bool pointer = false;
                    if (placed && placed->options.size() > 1) {
                        auto r = intersect(placed->options[1], placed->clip); POINT pt{r.x + r.w / 2, r.y + r.h / 2};
                        auto* hit = f.host.frame().hit(pt.x, pt.y);
                        bool hitsTab = hit && hit->id == sem->id;
                        SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                        SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                        pointer = r.w > 0 && r.h > 0 && hitsTab && *tabs.active() == OptionValue{"v01"};
                    }
                    SendMessageW(f.host.window(), WM_KEYDOWN, VK_RIGHT, keyLparam(VK_RIGHT, false, false));
                    SendMessageW(f.host.window(), WM_KEYUP, VK_RIGHT, keyLparam(VK_RIGHT, false, true));
                    bool keyboard = tabs.describe()->candidate == OptionValue{"v03"} && *tabs.active() == OptionValue{"v01"};
                    bool recovered = bool(tabs.enablePage({"v02"}, true)) && bool(tabs.activate({"v02"})) && bool(tabs.activate({"v01"})) &&
                        x04::baselineContext(f.form) && x04::sameModel(baselineModel, x04::snapshot(f.form));
                    report.check("X04-disabled-member", driven && cue && rejected && pointer && keyboard && recovered,
                        sample + ": tab cue=" + std::to_string(cue) + ", rejected=" + std::to_string(rejected) +
                        ", pointer=" + std::to_string(pointer) + ", keyboard=" + std::to_string(keyboard) + ", recovered=" + std::to_string(recovered));
                    continue;
                }
                bool page = entry.page == "modal" ? bool(f.form.handles.tabs.activate({"v03"})) && bool(f.form.handles.dialogOpen.invoke()) :
                    bool(f.form.handles.tabs.activate({std::string(entry.page)}));
                bool driven = baseline && page && x04::setEnabled(entry.handle, true) && x04::focus(f.form.ui, entry.handle) && x04::setEnabled(entry.handle, false);
                reveal(initial->id); auto sem = x04::describe(entry.handle);
                auto* placed = x04::placement(f.host.frame(), entry.handle); std::u16string visibleText;
                for (const auto& draw : f.host.paintObservations()) {
                    if (draw.owner != sem->id || !draw.visible || draw.type != detail::PaintObservation::Type::text || !placed) continue;
                    auto visibleWidth = intersect(draw.bounds, intersect(draw.clip, placed->bounds)).w;
                    if (draw.bounds.w > 0 && visibleWidth == draw.bounds.w) visibleText += draw.text;
                }
                bool cue = sem->visible && !sem->enabled && !sem->focused &&
                    visibleText.find(appearance.componentPresentation().disabledSuffix) != std::u16string::npos;
                auto snapshot = x04::snapshot(f.form);
                bool rejected = x04::disabledRetry(entry.handle);
                bool pointer = false;
                if (placed) {
                    auto r = intersect(placed->bounds, placed->clip); POINT pt{r.x + r.w / 2, r.y + r.h / 2};
                    auto* hit = f.host.frame().hit(pt.x, pt.y);
                    bool hitsTarget = hit && hit->id == sem->id;
                    SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                    SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                    pointer = r.w > 0 && r.h > 0 && hitsTarget;
                }
                SendMessageW(f.host.window(), WM_KEYDOWN, VK_SPACE, keyLparam(VK_SPACE, false, false));
                SendMessageW(f.host.window(), WM_KEYUP, VK_SPACE, keyLparam(VK_SPACE, false, true));
                auto after = x04::describe(entry.handle);
                bool unchanged = after && !after->focused && x04::sameModel(snapshot, x04::snapshot(f.form));
                bool recovered = x04::setEnabled(entry.handle, true) && x04::focus(f.form.ui, entry.handle) &&
                    x04::setEnabled(entry.handle, initial->enabled);
                bool optionChecks = true;
                if (sem->kind == ComponentKind::radioGroup && initial->enabled) {
                    auto radio = std::get<RadioGroupHandle>(entry.handle); auto previous = *radio.draft();
                    bool option = previous && bool(radio.select({"focus"})) && bool(radio.enableOption({"offline"}, false)) && bool(radio.focus());
                    reveal(sem->id); auto optionSemantic = radio.describe(); auto* optionItem = f.host.frame().find(radio);
                    bool publicReject = !optionSemantic->items[2].enabled && radio.select({"offline"}).status == Status::disabled;
                    bool cueOption = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& draw) {
                        return draw.owner == sem->id && draw.visible && draw.type == detail::PaintObservation::Type::text &&
                            draw.text.find(optionSemantic->items[2].label + appearance.componentPresentation().disabledSuffix) != std::u16string::npos;
                    });
                    bool pointerOption = false;
                    if (optionItem && optionItem->options.size() > 2) {
                        auto r = intersect(optionItem->options[2], optionItem->clip); POINT pt{r.x + r.w / 2, r.y + r.h / 2};
                        auto* hit = f.host.frame().hit(pt.x, pt.y);
                        bool target = hit && hit->id == sem->id && f.host.frame().optionAt(*optionItem, pt.x, pt.y) == 2;
                        SendMessageW(f.host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                        SendMessageW(f.host.window(), WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                        pointerOption = r.w > 0 && r.h > 0 && target && *radio.draft() == OptionValue{"focus"};
                    }
                    SendMessageW(f.host.window(), WM_KEYDOWN, VK_RIGHT, keyLparam(VK_RIGHT, false, false));
                    SendMessageW(f.host.window(), WM_KEYUP, VK_RIGHT, keyLparam(VK_RIGHT, false, true));
                    bool keyboardOption = *radio.draft() == OptionValue{"focus"};
                    bool optionRestored = bool(radio.enableOption({"offline"}, true)) && previous &&
                        bool(radio.select({"offline"})) && bool(radio.select(*previous));
                    optionChecks = option && publicReject && cueOption && pointerOption && keyboardOption && optionRestored;
                    recovered = recovered && optionChecks;
                }
                if (entry.page == "modal") recovered = bool(f.form.handles.dialog.settle(false)) && bool(f.form.handles.resetV03.invoke()) && recovered;
                if (sem->kind == ComponentKind::list) recovered = x04::baselineContext(f.form) && bool(f.form.handles.tabs.activate({"v02"})) &&
                    bool(f.form.list->handles.zero.invoke()) && bool(f.form.list->handles.twenty.invoke()) && recovered;
                recovered = x04::baselineContext(f.form) && x04::sameModel(baselineModel, x04::snapshot(f.form)) && recovered;
                report.check("X04-disabled-member", driven && cue && rejected && pointer && unchanged && recovered,
                    sample + ": driven=" + std::to_string(driven) + ", cue=" + std::to_string(cue) +
                    ", public=" + std::to_string(rejected) + ", pointer=" + std::to_string(pointer) +
                    ", unchanged=" + std::to_string(unchanged) + ", recovered=" + std::to_string(recovered) +
                    (sem->kind == ComponentKind::radioGroup ? ", option checks=" + std::to_string(optionChecks) : ""));
            }
        }
}

void x04GroupConflictMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate);
            f.x04Size(size.first, size.second, scale);
            f.host.observePaint(true);
            auto reveal = [&](const std::string& id) {
                f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, id);
                f.host.paint();
            };
            for (const auto& entry : x04::inventory(f.form)) {
                bool baseline = x04::baselineContext(f.form);
                auto semantic = x04::describe(entry.handle);
                if (!semantic || !detail::componentType(semantic->kind).supportsState(detail::ComponentState::conflict) ||
                    !x04::groupConflictSupported(f.form, *semantic)) continue;
                bool driven = baseline && x04::driveGroupConflict(f.form, entry);
                reveal(semantic->id); semantic = x04::describe(entry.handle);
                std::u16string ink; bool checkedCue = false;
                auto accent = appearance.palette().accent;
                for (const auto& draw : f.host.paintObservations()) {
                    if (draw.owner != semantic->id || !draw.visible) continue;
                    if (draw.type == detail::PaintObservation::Type::text) ink += draw.text;
                    else if (draw.color.r == accent.r && draw.color.g == accent.g && draw.color.b == accent.b) checkedCue = true;
                }
                bool field = semantic->visible && semantic->sourceConflict && ink.find(semantic->name) != std::u16string::npos &&
                    ink.find(appearance.componentPresentation().conflictSuffix) != std::u16string::npos &&
                    (semantic->kind == ComponentKind::checkbox ? semantic->checked == true && checkedCue :
                        semantic->value.empty() || ink.find(semantic->value) != std::u16string::npos);
                auto latest = x04::latestFor(f.form, entry); auto action = latest.describe();
                bool enabled = action->enabled; reveal(action->id); action = latest.describe();
                bool actionPainted = action->visible && std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(),
                    [&](const auto& draw) { return draw.owner == action->id && draw.visible &&
                        draw.type == detail::PaintObservation::Type::text && draw.text.find(action->name) != std::u16string::npos; });
                bool restored = bool(latest.invoke()) && !x04::describe(entry.handle)->sourceConflict && bool(f.form.handles.reset.invoke()) && x04::baselineContext(f.form);
                std::string sample = std::string(alternate ? "alternate" : "shipped") + " " +
                    std::to_string(size.first) + "x" + std::to_string(size.second) + " " +
                    std::to_string(int(scale * 100)) + "% " + semantic->id;
                report.check("X04-conflict-member", driven && field && enabled && actionPainted && restored, sample +
                    ": driven=" + std::to_string(driven) + ", field=" + std::to_string(field) +
                    ", latest=" + std::to_string(enabled && actionPainted) + ", restored=" + std::to_string(restored));
            }
        }
}

// State driver shared with the core seam, sampled through real clipped host paint.
void x04NameReadOnlyMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& name = f.form.handles.name;
            f.x04Size(size.first, size.second, scale);
            bool driven = bool(x04::nameReadOnly.drive(f.form)) && bool(name.focus());
            f.host.observePaint(true); f.settle();
            auto semantic = name.describe(); auto suffix = appearance.componentPresentation().readOnlySuffix;
            std::u16string ink;
            for (const auto& event : f.host.paintObservations())
                if (event.owner == semantic->id && event.visible && event.type == detail::PaintObservation::Type::text) ink += event.text;
            bool restricted = name.setValue(u"rejected").status == Status::readOnly &&
                std::find(semantic->verbs.begin(), semantic->verbs.end(), Verb::setValue) == semantic->verbs.end();
            auto& h = f.form.handles;
            bool associations = h.nameLabel.describe()->target == semantic->id && h.nameLabel.describe()->labelKind == LabelKind::name &&
                h.nameHelp.describe()->target == semantic->id && h.nameHelp.describe()->labelKind == LabelKind::description &&
                h.nameError.describe()->target == semantic->id && h.nameError.describe()->labelKind == LabelKind::error &&
                semantic->labels.size() == 3 && !semantic->invalid && !semantic->sourceConflict;
            bool painted = associations && !suffix.empty() && ink.find(semantic->name) != std::u16string::npos &&
                ink.find(semantic->value) != std::u16string::npos && ink.find(semantic->description) != std::u16string::npos &&
                ink.find(suffix) != std::u16string::npos;
            bool restored = bool(x04::nameReadOnly.restore(f.form)) && !name.describe()->readOnly;
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "%";
            report.check("X04-name-read-only", driven && semantic->visible && semantic->enabled && semantic->readOnly && restricted && painted && restored,
                sample + ": semantic=" + std::to_string(semantic->readOnly) + ", restricted=" + std::to_string(restricted) +
                ", name and read-only cue painted=" + std::to_string(painted) + ", restored=" + std::to_string(restored));
        }
}

void x04NameConflictMatrix(const Appearance& appearance, Report& report) {
    for (bool alternate : {false, true}) for (auto size : {std::pair{960, 720}, std::pair{640, 480}})
        for (float scale : {1.f, 1.5f}) {
            Fixture f(appearance, true, alternate); auto& h = f.form.handles;
            f.x04Size(size.first, size.second, scale); f.settle();
            bool driven = bool(x04::nameConflict.drive(f.form)) && bool(h.name.focus());
            f.host.layout(int(size.first * scale), int(size.second * scale), scale, f.form.scroll, h.name.describe()->id);
            f.host.observePaint(true); f.host.paint();
            auto semantic = h.name.describe();
            std::u16string ink;
            for (const auto& event : f.host.paintObservations())
                if (event.owner == semantic->id && event.visible && event.type == detail::PaintObservation::Type::text) ink += event.text;
            const auto& cue = appearance.componentPresentation().conflictSuffix;
            bool associations = h.nameLabel.describe()->target == semantic->id && h.nameLabel.describe()->labelKind == LabelKind::name &&
                h.nameHelp.describe()->target == semantic->id && h.nameHelp.describe()->labelKind == LabelKind::description &&
                h.nameError.describe()->target == semantic->id && h.nameError.describe()->labelKind == LabelKind::error &&
                semantic->labels.size() == 3 && !semantic->invalid;
            bool fieldPainted = semantic->visible && semantic->enabled && !semantic->readOnly && semantic->sourceConflict && !cue.empty() && associations &&
                ink.find(semantic->name) != std::u16string::npos && ink.find(semantic->value) != std::u16string::npos &&
                ink.find(semantic->description) != std::u16string::npos && ink.find(cue) != std::u16string::npos;
            bool latestEnabled = h.latest.describe()->enabled && bool(h.latest.focus()); f.settle();
            auto latest = h.latest.describe();
            bool actionPainted = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& event) {
                return event.owner == latest->id && event.visible && event.type == detail::PaintObservation::Type::text &&
                       event.text.find(latest->name) != std::u16string::npos;
            });
            bool restored = bool(x04::nameConflict.restore(f.form)) && !h.name.describe()->sourceConflict && *h.name.draft() == u"Latest";
            std::string sample = std::string(alternate ? "alternate" : "shipped") + " " + std::to_string(size.first) + "x" +
                std::to_string(size.second) + " " + std::to_string(int(scale * 100)) + "%";
            report.check("X04-name-conflict", driven && fieldPainted && latestEnabled && actionPainted && restored,
                sample + ": field name, value and cue painted=" + std::to_string(fieldPainted) + ", latest enabled=" +
                std::to_string(latestEnabled) + ", action painted=" + std::to_string(actionPainted) + ", restored=" + std::to_string(restored));
        }
}

// Raster evidence uses the same real fonts, component adapters, clipping and Canvas as the host.
// Colored pixels demonstrate a cue was drawn; they are not OCR of the semantic string.
void semanticRasterSamples(const Appearance& appearance, Report& report, const std::filesystem::path& folder) {
    auto containsColor = [](const Image& canvas, Rect bounds, Color color) {
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
        bool applied = bool(dark.form.ui.setPresentation(settings));
        d.error.invoke();
        dark.host.layout(960, 720, 1.f, dark.form.scroll, d.name.describe()->id);
        Image image{1, 1, {}};
        auto darkFrame = dark.host.render(960, 720, 1.f, folder / (report.appearance + "-pv03-dark.bmp"), &image);
        auto* field = darkFrame.find(d.name); auto colors = appearance.palette(ThemeVariant::dark());
        bool errorInk = field && !field->errorLines.empty() && containsColor(image, intersect(field->errorLines.front().bounds, field->clip), colors.error);
        bool invalid = d.name.describe()->invalid;
        auto errorCompare = dark.host.compareGpu(960, 720, 1.f);
        writeComparisonBmp(folder / (report.appearance + "-pv03-error-cpu.bmp"), errorCompare.reference);
        emitFailureIfNeeded(folder, report.appearance + "-pv03-error", errorCompare, bool(errorCompare));
        d.name.setValue(u"Tester"); d.name.focus(); d.name.select(1, 4);
        darkFrame = dark.host.render(960, 720, 1.f, folder / (report.appearance + "-pv03-selection.bmp"), &image);
        field = darkFrame.find(d.name);
        bool selection = field && containsColor(image, intersect(field->bounds, field->clip), colors.selection);
        bool focus = field && containsColor(image, intersect(field->bounds, field->clip), colors.accent);
        auto selectionCompare = dark.host.compareGpu(960, 720, 1.f);
        writeComparisonBmp(folder / (report.appearance + "-pv03-selection-cpu.bmp"), selectionCompare.reference);
        emitFailureIfNeeded(folder, report.appearance + "-pv03-selection", selectionCompare, bool(selectionCompare));
        d.disabled.invoke();
        dark.host.observePaint(true); dark.host.paint();
        bool disabled = !d.name.describe()->enabled && std::any_of(dark.host.paintObservations().begin(), dark.host.paintObservations().end(), [&](const auto& event) {
            return event.visible && event.text.find(appearance.componentPresentation().disabledSuffix) != std::u16string::npos;
        });
        dark.host.render(960, 720, 1.f, folder / (report.appearance + "-pv03-disabled.bmp"));
        auto disabledCompare = dark.host.compareGpu(960, 720, 1.f);
        writeComparisonBmp(folder / (report.appearance + "-pv03-disabled-cpu.bmp"), disabledCompare.reference);
        emitFailureIfNeeded(folder, report.appearance + "-pv03-disabled", disabledCompare, bool(disabledCompare));
        report.check("PV03-dark-raster", applied && errorInk && selection && focus && invalid && disabled && bool(errorCompare) && bool(selectionCompare) && bool(disabledCompare),
                     "CPU raster: applied=" + std::to_string(applied) + ", error=" + std::to_string(errorInk) + ", selection=" + std::to_string(selection) + ", focus=" + std::to_string(focus) + ", invalid=" + std::to_string(invalid) + ", disabled=" + std::to_string(disabled));
    }
    Fixture f(appearance, true);
    auto& h = f.form.handles;
    auto sample = [&](std::string_view name, Image& pixels) {
        return f.host.render(960, 720, 1.f, folder / (report.appearance + "-x04-" + std::string(name) + ".bmp"), &pixels);
    };
    h.error.invoke();
    f.host.layout(960, 720, 1.f, f.form.scroll, h.name.describe()->id);
    f.host.observePaint(true);
    f.host.paint();
    const auto* actualField = f.host.frame().find(h.name);
    bool observedError = actualField && !actualField->errorLines.empty() &&
        std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& event) {
            return event.owner == h.name.describe()->id && event.text == actualField->errorLines.front().text && event.visible;
        });
    report.check("X04-native-paint-observation", observedError,
                 "The actual host paint path records visible error text under its owning field");
    Image pixels{1, 1, {}};
    auto frame = sample("v01-error", pixels);
    auto* field = frame.find(h.name);
    bool error = field && h.name.describe()->invalid && !h.name.describe()->error.empty() && !field->errorLines.empty() &&
        containsColor(pixels, intersect(field->errorLines.front().bounds, field->clip), appearance.palette().error);
    report.check("X04-v01-raster", error, "Name semantics report an error and the real host raster contains error-colored pixels in the error line");

    h.normal.invoke(); h.name.setValue(u"Tester"); h.notices.toggle();
    f.form.rejectNextApplyWithNoticesSourceChange();
    auto rejected = h.apply.invoke();
    h.notices.focus(); f.settle();
    auto boxId = h.notices.describe()->id;
    bool conflictInk = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& event) {
        return event.owner == boxId && event.visible && event.type == detail::PaintObservation::Type::text &&
               event.text.find(appearance.componentPresentation().conflictSuffix) != std::u16string::npos;
    });
    bool checkboxConflict = rejected.status == Status::invalidValue && h.notices.describe()->sourceConflict &&
        h.noticesLatest.describe()->enabled && conflictInk;
    h.noticesLatest.focus(); f.settle();
    auto latest = h.noticesLatest.describe();
    bool latestInk = std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& event) {
        return event.owner == latest->id && event.visible && event.text.find(latest->name) != std::u16string::npos;
    });
    bool recovered = bool(h.noticesLatest.invoke()) && !h.notices.describe()->sourceConflict && !*h.notices.draft();
    report.check("X04-checkbox-conflict", checkboxConflict && latestInk && recovered,
        "Assembled checkbox conflict and latest action are painted, then latest adopts the source");

    bool executingPainted = false;
    h.normal.focus(); f.settle();
    h.normal.bind([&] {
        auto semantic = h.normal.describe();
        f.host.paint();
        std::u16string executingInk;
        for (const auto& event : f.host.paintObservations())
            if (event.owner == semantic->id && event.visible && event.type == detail::PaintObservation::Type::text)
                executingInk += event.text;
        executingPainted = semantic->executing &&
            std::none_of(semantic->verbs.begin(), semantic->verbs.end(), [](Verb op) { return op == Verb::invoke; }) &&
            executingInk.find(semantic->name) != std::u16string::npos &&
            executingInk.find(appearance.componentPresentation().executingSuffix) != std::u16string::npos;
        return Result{};
    });
    bool invoked = bool(h.normal.invoke());
    report.check("X04-button-executing", invoked && executingPainted && !*h.normal.executing(),
        "Real paint during the callback identifies executing and semantics refuse reactivation");

    h.tabs.activate({"v02"});
    auto& list = f.form.list->handles.list;
    list.select({"T003"}); list.focus();
    f.host.layout(960, 720, 1.f, f.form.scroll, list.describe()->id);
    frame = sample("v02-selection", pixels);
    auto* item = frame.find(list);
    auto semantic = list.describe();
    bool selected = item && semantic->items.size() > 2 && semantic->items[2].selected && item->options.size() > 2 &&
        containsColor(pixels, intersect(item->options[2], item->clip), appearance.palette().selection);
    report.check("X04-v02-raster", selected, "T003 is semantically selected and its visible real-host row contains the selection color");
    f.host.paint();
    auto paintedName = [&](const ComponentSnapshot& s) {
        return std::any_of(f.host.paintObservations().begin(), f.host.paintObservations().end(), [&](const auto& event) {
            return event.owner == s.id && event.visible && event.text.find(s.name) != std::u16string::npos;
        });
    };
    report.check("X04-list-and-tabs-names", paintedName(*list.describe()) && paintedName(*h.tabs.describe()),
                 "Visible list and tab group names agree with the typed semantic descriptions");

    h.tabs.activate({"v03"}); h.dialogOpen.invoke();
    frame = sample("v03-dialog", pixels);
    auto* title = frame.find(h.dialogTitle); auto* body = frame.find(h.dialogBody);
    auto* cancel = frame.find(h.dialogCancel); auto* confirm = frame.find(h.dialogConfirm);
    auto ink = [&](const Placement* p) {
        return p && !p->lines.empty() && containsColor(pixels, intersect(p->lines.front().bounds, p->clip), appearance.palette().text);
    };
    bool modal = frame.modal && !h.dialogTitle.describe()->value.empty() && !h.dialogBody.describe()->value.empty() &&
        !h.dialogCancel.describe()->name.empty() && !h.dialogConfirm.describe()->name.empty() &&
        ink(title) && ink(body) && ink(cancel) && ink(confirm);
    report.check("X04-v03-raster", modal, "Open dialog semantic title, body and button names have real-host text pixels in their line regions");
}

}

// ---- Real input session: SendInput through the OS input stack and the Microsoft Bopomofo IME ----
namespace {


std::string fileVersion(const wchar_t* path) {
    DWORD ignored = 0, size = GetFileVersionInfoSizeW(path, &ignored);
    std::vector<char> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!size || !GetFileVersionInfoW(path, 0, size, data.data()) || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length))
        return "unknown";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(info->dwFileVersionLS)) + "." + std::to_string(LOWORD(info->dwFileVersionLS));
}
std::string osVersion() {
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
    editForm::Form form{editForm::defaultIds(), true};
    std::unique_ptr<DesktopHost> owner;
    DesktopHost& host;
    const editForm::Ids& ids;
    HWND helper = nullptr;
    std::size_t logged = 0;
    explicit Live(const Appearance& appearance)
        : owner(std::make_unique<DesktopHost>(form.ui, appearance, [this] { return form.page(); }, form.scroll, Fixture::options(), true, false)),
          host(*owner), ids(form.ids()) {
        host.clientSize(800, 600);
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
    void clickPoint(POINT p) {
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
    void click(std::string_view id, int xBias = 0) {
        auto r = detail::findPlacement(host.frame(), id)->bounds;
        clickPoint({r.x + r.w / 2 + xBias, r.y + r.h / 2});
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
        auto& log = host.imeLog();
        for (; logged < log.size(); ++logged) out += narrow(log[logged]) + " | ";
        return out;
    }
    void clearComposition() {
        for (int i = 0; i < 3 && detail::access(form.ui).edit(ids.name)->composing; ++i) tap(VK_ESCAPE);
        wait(150);
        if (detail::access(form.ui).edit(ids.name)->composing) throw Blocked("a composition survived three Escape presses");
    }
    void startInName() {
        clearComposition();
        detail::access(form.ui).invoke(ids.reset);
        detail::access(form.ui).focus(ids.name);
        detail::access(form.ui).select(ids.name, 5, 5);
        wait(150);
        events();
    }
};


}

int runUiaIme(const std::filesystem::path& folder, const std::wstring& appearance) {
    std::filesystem::create_directories(folder); Report report(folder / "uia-ime.tsv");
    report.appearance = appearance == L"material" ? "material" : "web";
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) { report.check("UIA21", false, "COM initialization failed"); return 1; }
    try {
        Live live(appearance == L"material" ? materialAppearance() : webAppearance());
        Profiles profiles;
        if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo could not be activated");
        live.startInName();
        if (!live.form.handles.name.setValue(u"UIA baseline") || !live.form.handles.name.select(4, 4)) throw Blocked("Unable to prepare committed value");
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
        live.clearComposition();
    } catch (const Blocked& e) { report.check("UIA21-BLOCKED", false, e.what()); }
    CoUninitialize(); return report.failures ? 1 : 0;
}

bool imeSession(const Appearance& appearance, Report& report, std::ofstream& events) {
    try {
        Live live(appearance);
        auto& ui = live.form.ui;
        const auto& ids = live.ids;
        Profiles profiles;
        profiles.us();
        live.wait(200);
        report.check("ENV", true, osVersion() + "; IMTCTIP.DLL " + fileVersion(L"C:\\Windows\\System32\\IME\\IMETC\\IMTCTIP.DLL") +
                                      "; ChtIME.exe " + fileVersion(L"C:\\Windows\\System32\\InputMethod\\CHT\\ChtIME.exe") +
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
        live.startInName();
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
        bool redoKept = !session->composing && detail::access(ui).text(ids.name) == u"測試使用者" && ([](const auto& s) { return std::find(s.verbs.begin(), s.verbs.end(), Verb::redo) != s.verbs.end(); })(*detail::access(live.form.ui).describe(ids.name));
        record("E01-cancel", redoKept, "Escape cancels the composition; committed text and the redo position survive");

        // Switch pages through the real tab during an active Microsoft Bopomofo composition.
        live.startInName();
        live.type("JI3");
        auto pageBefore = live.state();
        live.host.paint();
        const auto* tabPlacement = detail::findPlacement(live.host.frame(), live.form.handles.tabs.describe()->id);
        if (!tabPlacement || tabPlacement->options.size() < 2) throw std::runtime_error("Second tab has no placement");
        const auto targetTab = tabPlacement->options[1];
        live.clickPoint({targetTab.x + targetTab.w / 2, targetTab.y + targetTab.h / 2});
        auto pageAfter = live.form.handles.tabs.active()->id;
        bool switched = *live.form.handles.tabs.active() == OptionValue{"v02"} && !session->composing &&
            session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.form.handles.tabs.describe()->focused && live.form.applies() == 0;
        live.form.handles.tabs.activate({"v01"});
        live.wait(100);
        record("E01-page-switch", switched && live.history() == 1,
               "Real tab click during composition commits once, focuses tabs and switches page; page=" + pageAfter + "; before: " + pageBefore);

        // The host seam opens the dialog while the real field is composing.
        live.startInName();
        live.type("JI3");
        auto dialogBefore = live.state();
        live.host.paint();
        live.form.handles.dialog.open(live.form.handles.name);
        bool opened = *live.form.handles.dialog.isOpen() && live.form.handles.dialogCancel.describe()->focused &&
            !session->composing && session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" &&
            live.form.progress() == 50 && live.form.dialogConfirmations() == 0;
        if (*live.form.handles.dialog.isOpen()) live.form.handles.dialogCancel.invoke();
        record("E01-dialog-open", opened && live.history() == 1 && live.form.handles.name.describe()->focused,
               "Host seam opens the dialog and completes composition once, focuses cancel, and cancellation restores the field; before: " + dialogBefore);

        live.startInName();
        live.chord(VK_CONTROL, 'A');
        live.type("JI3");
        live.tap(VK_RETURN);
        live.wait(200);
        bool replaced = detail::access(ui).text(ids.name) == u"我" && live.history() == 1;
        live.chord(VK_CONTROL, 'Z');
        record("E01-replace-selection", replaced && detail::access(ui).text(ids.name) == u"測試使用者", "Confirmation replaces the selection as one undo entry");

        live.startInName();
        live.type("JI3");
        live.chord(VK_CONTROL, 'Z');
        live.wait(200);
        bool untouched = detail::access(ui).text(ids.name) == u"測試使用者" && live.history() == 0;
        record("E01-shortcut-while-composing", untouched, "Ctrl+Z during composition goes to the IME and leaves committed history alone");
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(150); }
        if (session->composing) { live.tap(VK_ESCAPE); live.wait(150); }

        live.startInName();
        live.type("JI3");
        live.tap(VK_TAB);
        live.wait(300);
        record("E01-tab", session->composing && detail::access(ui).focused() == ids.name && detail::access(ui).text(ids.name) == u"測試使用者" && live.form.applies() == 0,
               "Tab during composition is consumed by the IME: preedit kept, focus stays, nothing committed or applied");
        live.clearComposition();
        record("E01-tab-then-escape", detail::access(ui).text(ids.name) == u"測試使用者" && live.history() == 0, "Escape then discards that preedit cleanly");

        live.startInName();
        live.type("JI3");
        auto before = live.state();
        live.click(ids.longText);
        record("E01-pointer", !session->composing && detail::access(ui).text(ids.name) == u"測試使用者我" && live.history() == 1 && detail::access(ui).focused() == ids.longText && live.form.applies() == 0,
               "Clicking another control commits the preedit once, then focus moves; before: " + before);

        live.startInName();
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

        live.startInName();
        live.type("JI3");
        before = live.state();
        live.click(ids.name, -100);
        record("E01-pointer-same-field", !session->composing && detail::access(ui).text(ids.name) == u"測試使用者我" && live.history() == 1 && detail::access(ui).focused() == ids.name && live.form.applies() == 0,
               "Clicking inside the composing field commits once, then places the caret; before: " + before);

        profiles.us();
        live.form.handles.tabs.activate({"v03"});
        live.form.handles.progressZero.focus(); live.wait(150);
        auto sourceId = live.form.handles.progressHalf.describe()->id;
        auto sourceBounds = detail::findPlacement(live.host.frame(), sourceId)->bounds;
        POINT hoverPoint{sourceBounds.x + sourceBounds.w / 2, sourceBounds.y + sourceBounds.h / 2};
        ClientToScreen(live.host.window(), &hoverPoint);
        SetCursorPos(hoverPoint.x, hoverPoint.y);
        live.wait(650);
        bool pointerShown = *live.form.handles.progressTip.visible() && live.form.handles.progressZero.describe()->focused;
        POINT awayPoint{10, 10}; ClientToScreen(live.host.window(), &awayPoint);
        SetCursorPos(awayPoint.x, awayPoint.y); live.wait(150);
        bool pointerLeft = !*live.form.handles.progressTip.visible();
        report.check("P05-real-pointer", pointerShown && pointerLeft,
                     "Physical pointer dwell displays the tooltip without taking focus; leaving closes it");
        live.form.handles.progressHalf.focus(); live.tap(VK_RIGHT);  // the focus tooltip needs keyboard use first
        live.wait(650);
        bool visibleBefore = *live.form.handles.progressTip.visible();
        live.foreground(live.helper);
        bool hiddenAway = !*live.form.handles.progressTip.visible();
        live.foreground(live.host.window());
        bool freshDelay = !*live.form.handles.progressTip.visible();
        live.wait(600);
        bool visibleAfter = *live.form.handles.progressTip.visible() && live.form.handles.progressHalf.describe()->focused;
        report.check("R08-real-window-focus", visibleBefore && hiddenAway && freshDelay && visibleAfter,
                     "Real foreground switch closes and restarts the delay; tooltip returns without taking source focus");
        live.wait(200);

        // Follow the desktop run loop through WM_CLOSE and host destruction while the model is still alive.
        live.form.handles.tabs.activate({"v01"});
        if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo could not be reactivated for close");
        live.startInName();
        live.type("JI3");
        bool closeStarted = session->composing && !session->preedit.empty();
        auto closeBefore = live.state();
        PostMessageW(live.host.window(), WM_CLOSE, 0, 0);
        int runResult = live.host.run();
        events << "E01-close-before-destroy\t" << live.events() << '\n';
        live.owner.reset();
        bool closeOk = closeStarted && runResult == 0 && !session->composing &&
                        detail::access(ui).text(ids.name) == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.history() == 1 && live.form.applies() == 0;
        report.check("E01-close", closeOk,
                     "WM_CLOSE exits the real host loop and destroys its window; before: " + closeBefore +
                     " => after destruction: " + live.state());
        return true;
    } catch (const Blocked& blocked) {
        report.check("BLOCKED", false, blocked.what());
        events << "BLOCKED\t" << blocked.what() << '\n';
        return false;
    }
}

int runIme(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "ime.tsv");
    std::ofstream events(folder / "ime-events.log");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        events << "=== " << report.appearance << " ===\n";
        if (!imeSession(*appearance, report, events)) break;
    }
    CoUninitialize();
    std::cout << report.checks << " real-input checks, " << report.failures << " not passing\n";
    return report.failures ? 1 : 0;
}

// Real per-monitor transition. Unlike the offscreen matrix this never forces a scale.
// The window procedure wrapper observes the actual OS message before forwarding it.
namespace {
WNDPROC dpiPreviousProc = nullptr;
int dpiMessages = 0;
LRESULT CALLBACK dpiObserver(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DPICHANGED) ++dpiMessages;
    return CallWindowProcW(dpiPreviousProc, window, msg, w, l);
}
struct DpiObserver {
    HWND window;
    explicit DpiObserver(HWND w) : window(w) {
        dpiMessages = 0;
        dpiPreviousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dpiObserver)));
        if (!dpiPreviousProc) throw Blocked("could not observe WM_DPICHANGED");
    }
    ~DpiObserver() { SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dpiPreviousProc)); }
};
BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* monitors = reinterpret_cast<std::vector<MONITORINFOEXW>*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) monitors->push_back(info);
    return TRUE;
}

bool dpiSession(const Appearance& appearance, Report& report, bool presentation = false) {
    try {
        std::vector<MONITORINFOEXW> monitors;
        EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&monitors));
        if (monitors.size() < 2) throw Blocked("two active monitors are required");
        std::sort(monitors.begin(), monitors.end(), [](const auto& a, const auto& b) { return a.rcMonitor.left < b.rcMonitor.left; });
        Live live(appearance);
        DpiObserver observer(live.host.window());
        if (presentation) { auto settings = live.form.ui.presentation(); settings.textSize = 28; if (!live.form.ui.setPresentation(settings)) throw Blocked("28 DIP setting rejected"); }
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
        live.startInName();
        detail::access(ui).select(ids.name, 1, 3);
        live.host.paint();
        int before = dpiMessages;
        auto atlasBeforeForward = live.host.atlasClears();
        UINT second = move(monitors[1]);
        auto atlasAfterForward = live.host.atlasClears();
        auto sharpText = [&] {
            RECT client{}; GetClientRect(live.host.window(), &client);
            auto comparison = live.host.compareGpu(client.right, client.bottom, live.host.scale());
            const auto* textField = live.host.frame().find(live.form.handles.name);
            Rect textArea{};
            if (textField) textArea = {textField->bounds.x + textField->textField.editor.x,
                textField->bounds.y + textField->textField.editor.y,
                textField->textField.editor.w, textField->textField.editor.h};
            auto textCompare = compareImages(comparison.reference, comparison.actual, {textArea});
            return textField && textArea.w > 0 && textArea.h > 0 && bool(comparison) && textCompare.regionsOk;
        };
        bool forwardSharp = !presentation && first == 96 && second == 144 && sharpText();
        if (presentation && !((first == 96 && second == 144) || (first == 144 && second == 96)))
            throw Blocked("PR02 requires one 100% and one 150% monitor");
        if (presentation && first == 96) {
            std::swap(monitors[0], monitors[1]);
            first = move(monitors[0]);
            before = dpiMessages;
            second = move(monitors[1]);
        }
        auto* session = detail::access(ui).edit(ids.name);
        auto* field = live.host.frame().find(live.form.handles.name);
        bool state = field && session && session->text == u"測試使用者" && session->selection.anchor == 1 &&
                     session->selection.caret == 3 && detail::access(ui).focused() == ids.name && GetFocus() == live.host.window();
        // Field-part rectangles are relative to the containing placement.
        int hitX = field ? field->bounds.x + field->textField.editor.x + field->textField.editor.w / 2 : 0;
        int hitY = field ? field->bounds.y + field->textField.editor.y + field->textField.editor.h / 2 : 0;
        auto* hitItem = field ? live.host.frame().hit(hitX, hitY) : nullptr;
        bool hit = hitItem && hitItem->id == ids.name;
        unsigned expectedCaret = hit ? live.host.frame().textOffset(live.form.handles.name, hitX) : 0;
        if (hit) live.clickPoint({hitX, hitY});
        bool pointer = hit && session->selection.anchor == expectedCaret && session->selection.caret == expectedCaret;
        report.check("D01-selection-forward", first != second && dpiMessages > before && state && hit && pointer,
                     narrow(std::wstring(monitors[0].szDevice)) + " " + std::to_string(first) + " -> " +
                     narrow(std::wstring(monitors[1].szDevice)) + " " + std::to_string(second) +
                     "; WM_DPICHANGED=" + std::to_string(dpiMessages - before) + "; selection/focus=" +
                     std::to_string(state) + "; field hit=" + std::to_string(hit) + "; physical click caret=" + std::to_string(pointer));

        detail::access(ui).select(ids.name, 1, 3);
        before = dpiMessages;
        auto atlasBeforeReturn = live.host.atlasClears();
        UINT selectionReturn = move(monitors[0]);
        auto atlasAfterReturn = live.host.atlasClears();
        field = live.host.frame().find(live.form.handles.name);
        bool selectionBack = field && session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005" &&
                              session->selection.anchor == 1 && session->selection.caret == 3 &&
                              detail::access(ui).focused() == ids.name && GetFocus() == live.host.window();
        hitX = field ? field->bounds.x + field->textField.editor.x + field->textField.editor.w / 2 : 0;
        hitY = field ? field->bounds.y + field->textField.editor.y + field->textField.editor.h / 2 : 0;
        hitItem = field ? live.host.frame().hit(hitX, hitY) : nullptr;
        hit = hitItem && hitItem->id == ids.name;
        expectedCaret = hit ? live.host.frame().textOffset(live.form.handles.name, hitX) : 0;
        if (hit) live.clickPoint({hitX, hitY});
        pointer = hit && session->selection.anchor == expectedCaret && session->selection.caret == expectedCaret;
        report.check("D01-selection-return", selectionReturn == first && dpiMessages > before && selectionBack && hit && pointer,
                     "Return: selection/focus=" + std::to_string(selectionBack) + "; physical click caret=" + std::to_string(pointer));
        // GA05 uses the actual 100% -> 150% leg, irrespective of monitor ordering.
        if (!presentation) {
            bool forward = first == 96 && second == 144;
            bool backward = first == 144 && second == 96;
            unsigned clearsBefore = forward ? atlasBeforeForward : atlasBeforeReturn;
            unsigned clearsAfter = forward ? atlasAfterForward : atlasAfterReturn;
            bool sharp = forward ? forwardSharp : (backward && sharpText());
            report.check("GA05", (forward || backward) && clearsAfter == clearsBefore + 1 && sharp,
                "100% to 150%; atlas clears=" + std::to_string(clearsBefore) + " -> " + std::to_string(clearsAfter) +
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
        bool composingBefore = session->composing && !session->preedit.empty();
        auto checkCompositionMove = [&](const MONITORINFOEXW& monitor, UINT expectedDpi, const char* caseName) {
            int prior = dpiMessages;
            UINT actual = move(monitor);
            field = live.host.frame().find(live.form.handles.name);
            IMECHARPOSITION position{}; position.dwSize = sizeof(position);
            bool query = SendMessageW(live.host.window(), WM_IME_REQUEST, IMR_QUERYCHARPOSITION,
                                      reinterpret_cast<LPARAM>(&position)) != 0;
            POINT caret{field ? field->caretX(session->begin()) : 0,
                        field ? field->bounds.y + field->textField.editor.y : 0};
            ClientToScreen(live.host.window(), &caret);
            CANDIDATEFORM candidate{}; bool candidateOk = false;
            if (auto imc = ImmGetContext(live.host.window())) {
                candidate.dwIndex = 0;
                candidateOk = ImmGetCandidateWindow(imc, 0, &candidate) && field &&
                               candidate.ptCurrentPos.x == field->caretX(session->visibleCaret()) &&
                               candidate.ptCurrentPos.y == field->bounds.y + field->textField.editor.y + field->textField.editor.h;
                ImmReleaseContext(live.host.window(), imc);
            }
            bool caretOk = query && position.pt.x == caret.x && position.pt.y == caret.y;
            bool composition = composingBefore && session->composing && detail::access(ui).focused() == ids.name;
            report.check(caseName, actual == expectedDpi && dpiMessages > prior && composition && caretOk && candidateOk,
                         "WM_DPICHANGED=" + std::to_string(dpiMessages - prior) + "; composition=" + std::to_string(composition) +
                         "; IMR caret=" + std::to_string(caretOk) + "; candidate placement request=" + std::to_string(candidateOk));
        };
        checkCompositionMove(monitors[1], second, "D01-composition-forward");
        checkCompositionMove(monitors[0], first, "D01-composition-return");
        live.clearComposition();
        profiles.us();
        live.form.handles.region.focus();
        live.tap(VK_RETURN);
        live.host.paint();
        // Report which part of the popup predicate failed: absent dropdown, geometry or hit.
        auto popupCheck = [&](const Frame& f, std::string& detail) {
            RECT client{}; GetClientRect(live.host.window(), &client);
            auto result = test::popupInClient(f, {client.left, client.top, client.right - client.left, client.bottom - client.top}, ids.region);
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
        before = dpiMessages;
        UINT popupDpi = move(monitors[1]);
        const auto& frame = live.host.frame();
        std::string popupDetail;
        bool popup = popupCheck(frame, popupDetail);
        report.check("D01-popup-forward", popupDpi == second && dpiMessages > before && popup,
                     "Open dropdown across monitor move: WM_DPICHANGED=" + std::to_string(dpiMessages - before) +
                     "; popup inside window and hit=" + std::to_string(popup) + "; " + popupDetail);
        before = dpiMessages;
        UINT popupReturn = move(monitors[0]);
        const auto& returnedFrame = live.host.frame();
        bool popupBack = popupCheck(returnedFrame, popupDetail);
        report.check("D01-popup-return", popupReturn == first && dpiMessages > before && popupBack,
                     "Return: popup inside window and hit=" + std::to_string(popupBack) + "; " + popupDetail);
        return true;
    } catch (const Blocked& blocked) {
        report.check("BLOCKED", false, blocked.what());
        return false;
    }
}
}
int runDpi(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "dpi.tsv");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        if (!dpiSession(*appearance, report)) break;
    }
    CoUninitialize();
    std::cout << report.checks << " real DPI checks, " << report.failures << " not passing\n";
    return report.failures ? 1 : 0;
}

int run(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "host.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        std::string tag = appearance == &webAppearance() ? "web" : "material";
        report.appearance = tag;
        presentationBeforeInput(*appearance, report);
        drawListInitial(*appearance, report);
        paletteOverrideNative(*appearance, report);
        tabsNative(*appearance, report, folder);
        atlasNative(*appearance, report);
        progressNative(*appearance, report, folder);
        hoverNative(*appearance, report);
        dialogNative(*appearance, report, folder);
        tooltipNative(*appearance, report, folder);
        focusVisibilityNative(*appearance, report);
        longDropdownNative(*appearance, report);
        listNative(*appearance, report);
        nestedListNative(*appearance, report);
        fontDiagnosticReachesApplication(*appearance, report);
        registeredComponentUsesNativeHost(*appearance, report, folder);
        checkboxAndRadioNative(*appearance, report);
        sliderNative(*appearance, report);
        sliderAnimationNative(*appearance, report);
        dropdownNative(*appearance, report);
        spaceActivationDoesNotLeak(*appearance, report);
        enterActivatesOnce(*appearance, report);
        pointerActivationRules(*appearance, report);
        pointerSelectionAndClipboard(*appearance, report);
        contextMenuNative(*appearance, report);
        multilineNative(*appearance, report, folder);
        verticalLabelNative(*appearance, report, folder);
        richtextNative(*appearance, report, folder);
        wordSelectNative(*appearance, report);
        justifyNative(*appearance, report, folder);
        clipboardContention(*appearance, report);
        tabWalkAndVisibleFocus(*appearance, report, folder, tag);
        resizeFirstFrameMatchesNext(*appearance, report, 1.f);
        resizeFirstFrameMatchesNext(*appearance, report, 1.5f);
        resizeFirstFrameMatchesNext(*appearance, report, 1.5f, true);
        layoutMatrix(*appearance, report, folder, tag);
        pageSizeScaleMatrix(*appearance, report);
        popupDismissalMatrix(*appearance, report);
        semanticRasterSamples(*appearance, report, folder);
        x04ClientSizeMatrix(*appearance, report);
        x04FixedPlacementMatrix(*appearance, report);
        x04HiddenMatrix(*appearance, report);
        x04SelectedMatrix(*appearance, report);
        x04NormalMatrix(*appearance, report);
        x04ExecutingMatrix(*appearance, report);
        x04ClippedMatrix(*appearance, report);
        x04ErrorMatrix(*appearance, report);
        x04CandidateMatrix(*appearance, report);
        x04SearchReadOnlyMatrix(*appearance, report);
        x04ModalMatrix(*appearance, report);
        x04TooltipMatrix(*appearance, report);
        x04DisabledMatrix(*appearance, report);
        x04GroupConflictMatrix(*appearance, report);
        x04NameReadOnlyMatrix(*appearance, report);
        x04NameConflictMatrix(*appearance, report);
        semanticObservationSweep(*appearance, folder, tag);
    }
    std::cout << report.checks << " native checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runFocus(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "focus.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        focusVisibilityNative(*appearance, report);
    }
    std::cout << report.checks << " focus native checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}
int runHover(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "hover.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        hoverNative(*appearance, report);
    }
    std::cout << report.checks << " hover native checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}
int runX02(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x02.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        popupDismissalMatrix(*appearance, report);
    }
    std::cout << report.checks << " X02 native checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runX04(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        semanticRasterSamples(*appearance, report, folder);
        x04ClientSizeMatrix(*appearance, report);
        x04FixedPlacementMatrix(*appearance, report);
        x04HiddenMatrix(*appearance, report);
        x04SelectedMatrix(*appearance, report);
        x04NormalMatrix(*appearance, report);
        x04ExecutingMatrix(*appearance, report);
        x04ClippedMatrix(*appearance, report);
        x04ErrorMatrix(*appearance, report);
        x04CandidateMatrix(*appearance, report);
        x04SearchReadOnlyMatrix(*appearance, report);
        x04ModalMatrix(*appearance, report);
        x04TooltipMatrix(*appearance, report);
        x04DisabledMatrix(*appearance, report);
        x04GroupConflictMatrix(*appearance, report);
        x04NameReadOnlyMatrix(*appearance, report);
        x04NameConflictMatrix(*appearance, report);
        semanticObservationSweep(*appearance, folder, report.appearance);
    }
    std::cout << report.checks << " X04 raster checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runX04Selection(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-selection.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        x04SelectedMatrix(*appearance, report);
        x04CandidateMatrix(*appearance, report);
    }
    std::cout << report.checks << " X04 selection checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runX04Disabled(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-disabled.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        x04DisabledMatrix(*appearance, report);
    }
    std::cout << report.checks << " X04 disabled checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runX04Executing(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-executing.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        x04ExecutingMatrix(*appearance, report);
    }
    std::cout << report.checks << " X04 executing checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runX04Clipped(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "x04-clipped.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        x04ClippedMatrix(*appearance, report);
    }
    std::cout << report.checks << " X04 clipped checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

// The same AC08 scenario with fresh windows, without the unrelated layout matrix.
int runClipboard(const std::filesystem::path& folder, int repeat) {
    std::filesystem::create_directories(folder);
    Report report(folder / "clipboard.tsv");
    for (int i = 0; i < repeat; ++i) {
        for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
            report.appearance = (appearance == &webAppearance() ? "web-" : "material-") + std::to_string(i + 1);
            pointerSelectionAndClipboard(*appearance, report);
            clipboardContention(*appearance, report);
        }
    }
    std::cout << report.checks << " native clipboard checks, " << report.failures << " failures\n";
    return report.failures ? 1 : 0;
}

int runPresentationIdle(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "presentation.tsv");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        try {
            Live live(*appearance); Profiles profiles;
            if (!profiles.chinese()) throw Blocked("Microsoft Bopomofo unavailable");
            if (!systemFontCatalog().contains(u"Microsoft JhengHei") || !systemFontCatalog().contains(u"MingLiU"))
                throw Blocked("PR01 requires installed Microsoft JhengHei and MingLiU");
            auto settings = live.form.ui.presentation(); settings.fontFamily = u"Microsoft JhengHei";
            if (!live.form.ui.setPresentation(settings)) throw Blocked("initial font setting rejected");
            live.startInName(); live.type("JI3");
            auto* session = detail::access(live.form.ui).edit(live.ids.name);
            bool composing = session && session->composing && !session->preedit.empty();
            if (!composing) throw Blocked("Microsoft Bopomofo did not start composition");
            bool positions = composing;
            for (int step = 0; step < 4; ++step) {
                if (step == 0) settings.textSize = 28;
                if (step == 1) settings.fontFamily = u"MingLiU";
                if (step == 2) settings.variant = ThemeVariant::dark();
                if (step == 3) settings.density = Density::compact;
                positions &= bool(live.form.ui.setPresentation(settings));
                auto* field = live.host.frame().find(live.form.handles.name);
                IMECHARPOSITION position{}; position.dwSize = sizeof(position);
                bool query = SendMessageW(live.host.window(), WM_IME_REQUEST, IMR_QUERYCHARPOSITION, reinterpret_cast<LPARAM>(&position)) != 0;
                POINT caret{field ? field->caretX(session->begin()) : 0, field ? field->bounds.y + field->textField.editor.y : 0};
                ClientToScreen(live.host.window(), &caret);
                CANDIDATEFORM candidate{}; bool candidateOk = false;
                if (auto imc = ImmGetContext(live.host.window())) {
                    candidate.dwIndex = 0;
                    candidateOk = ImmGetCandidateWindow(imc, 0, &candidate) && field && candidate.ptCurrentPos.x == field->caretX(session->visibleCaret()) && candidate.ptCurrentPos.y == field->bounds.y + field->textField.editor.y + field->textField.editor.h;
                    ImmReleaseContext(live.host.window(), imc);
                }
                positions &= session->composing && query && position.pt.x == caret.x && position.pt.y == caret.y && candidateOk;
            }
            live.tap(VK_RETURN); live.wait(250);
            bool committed = session && !session->composing && session->text == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && live.history() == 1;
            report.check("PR01", composing && positions && committed, profiles.active() + "; IMTCTIP " + fileVersion(LR"(C:\Windows\System32\IME\IMETC\IMTCTIP.DLL)") + "; candidates and caret=" + std::to_string(positions) + "; single commit and undo=" + std::to_string(committed));
        } catch (const Blocked& error) { report.check("PR01", false, error.what()); }
        dpiSession(*appearance, report, true);
        report.open("PR03", "Install a font for the current user, confirm its catalog entry and local path, and inspect its display in V04; record the evidence manually.");
        report.open("PR04", "Generate trial screenshots and record first-frame font-switch timing and user feedback.");
    }
    CoUninitialize();
    return report.failures ? 1 : 0;
}

int nativePresentationMatrix(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Report report(folder / "presentation-matrix.tsv");
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        report.appearance = appearance == &webAppearance() ? "web" : "material";
        try {
            presentationMatrix::run(editForm::defaultIds(), *appearance, [&](editForm::Form& form) {
                DesktopConfig options; options.title = u"Presentation matrix";
                auto host = std::make_shared<DesktopHost>(form.ui, *appearance, [&form] { return form.page(); }, form.scroll, options, false, true);
                return presentationMatrix::Driver{[host, &form](int width, int height, float scale, std::string_view reveal) {
                    return host->layout(width, height, scale, form.scroll, reveal);
                }, [host, &folder, &report](int width, int height, float scale) {
                    auto result = host->compareGpu(width, height, scale);
                    if (!result) writeComparisonFailure(folder, report.appearance + "-matrix-failure", result);
                    return bool(result);
                }};
            }, [](bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); });
            report.check("MX01-shaped", true, "all presentation dimensions and focusable components");
        } catch (const std::exception& error) { report.check("MX01-shaped", false, error.what()); }
    }
    return report.failures ? 1 : 0;
}

int trialScreenshots(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    std::ofstream manifest(folder / "trial.tsv");
    std::ofstream timing(folder / "font-switch-timing.tsv");
    timing << "appearance\tfont\tmilliseconds\tstatus\n";
    manifest << "appearance\tvariant\tdensity\ttext_size\tfile\n";
    for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
        const std::string name = appearance == &webAppearance() ? "web" : "material";
        Fixture f(*appearance, true);
        if (systemFontCatalog().contains(u"MingLiU")) {
            auto settings = f.form.ui.presentation(); settings.fontFamily = u"MingLiU";
            auto start = std::chrono::steady_clock::now();
            bool accepted = bool(f.form.ui.setPresentation(settings));
            f.host.paint();
            auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            timing << name << "\tMingLiU\t" << elapsed << '\t' << (accepted ? "recorded" : "FAIL") << '\n';
            if (!accepted) return 1;
            settings.fontFamily.clear(); if (!f.form.ui.setPresentation(settings)) return 1;
        } else timing << name << "\tMingLiU\t\tN/A (font absent)\n";
        for (bool dark : {false, true}) for (bool compact : {false, true}) for (int size : {16, 28}) {
            auto settings = f.form.ui.presentation();
            settings.variant = dark ? ThemeVariant::dark() : ThemeVariant::light();
            settings.density = compact ? Density::compact : Density::standard;
            settings.textSize = size;
            if (!f.form.ui.setPresentation(settings) || !f.form.handles.tabs.activate({"v01"})) return 1;
            f.form.scroll.offset = 0;
            auto file = name + "-" + (dark ? "dark" : "light") + "-" + (compact ? "compact" : "standard") + "-" + std::to_string(size) + ".bmp";
            f.host.render(960, 720, 1.f, folder / file);
            manifest << name << '\t' << (dark ? "dark" : "light") << '\t' << (compact ? "compact" : "standard") << '\t' << size << '\t' << file << '\n';
        }
    }
    return manifest && timing ? 0 : 1;
}

// A fresh process is used for each run so startup age and warmup share one origin.
int runPerf(const std::filesystem::path& evidence, const std::wstring& scenario, const std::wstring& appearance,
             bool large, int delay, int durationMs) {
    if (scenario!=L"startup" && scenario!=L"list-hold-down" && scenario!=L"typing" && scenario!=L"idle")
        throw std::runtime_error("Unknown perf scenario");
    if (appearance!=L"web" && appearance!=L"material") throw std::runtime_error("Unknown perf appearance");
    if (delay<0 || durationMs<0) throw std::runtime_error("Invalid perf duration or delay");
    const auto& selectedAppearance=appearance==L"web" ? webAppearance() : materialAppearance();
    editForm::Form form(editForm::defaultIds(),true);
    DesktopHost host(form.ui,selectedAppearance,[&]{return form.page();},form.scroll,Fixture::options());
    const bool offscreen=large && (GetSystemMetrics(SM_CXSCREEN)<3840 || GetSystemMetrics(SM_CYSCREEN)<2160);
    host.perfEnable(delay,offscreen);
    auto previousProc=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(host.window(),GWLP_WNDPROC));
    x04OriginalProcs.emplace(host.window(),previousProc);
    if (!SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&x04WindowProc))) throw std::runtime_error("Perf sizing fixture failed");
    host.forceScale(large && !offscreen ? 2.f : 1.f); host.clientSize(1920,1080);
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
    if(scenario!=L"startup") host.perfReset();
    auto send=[&](WORD key, DWORD flags) { if(GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window lost foreground focus"); INPUT input{}; input.type=INPUT_KEYBOARD; input.ki.wVk=(flags & KEYEVENTF_UNICODE)?0:key; input.ki.wScan=(flags & KEYEVENTF_UNICODE)?key:0; input.ki.dwFlags=flags; if(SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("SendInput failed"); };
    auto wait=[&](int milliseconds) { auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds); while(std::chrono::steady_clock::now()<end) { host.pump(); auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count(); if(remaining>0) MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(remaining),QS_ALLINPUT,MWMO_INPUTAVAILABLE); } host.pump(); };
    if(scenario==L"list-hold-down") {
        int duration=durationMs ? durationMs : 5000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration);
        while(std::chrono::steady_clock::now()<end) { send(VK_DOWN,0); wait(33); } send(VK_DOWN,KEYEVENTF_KEYUP); host.pump();
    } else if(scenario==L"typing") {
        int duration=durationMs ? durationMs : 10000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration); int i=0;
        while(std::chrono::steady_clock::now()<end) { WORD c=(i++%2)?0x4e2d:L'A'; send(c,KEYEVENTF_UNICODE); send(c,KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(50); }
    }
    int paints=0,presents=0; double cpuMs=0;
    if(scenario==L"idle") {
        send(L'A',KEYEVENTF_UNICODE); send(L'A',KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(1500);
        int beforePaints=host.paintMessages(), beforeFrames=host.frames(); FILETIME c{},e{},k1{},u1{},k2{},u2{};
        GetProcessTimes(GetCurrentProcess(),&c,&e,&k1,&u1); wait(durationMs ? durationMs : 10000); GetProcessTimes(GetCurrentProcess(),&c,&e,&k2,&u2);
        auto ticks=[](FILETIME t){ULARGE_INTEGER x{}; x.LowPart=t.dwLowDateTime; x.HighPart=t.dwHighDateTime; return x.QuadPart;};
        cpuMs=double(ticks(k2)-ticks(k1)+ticks(u2)-ticks(u1))/10000.; paints=host.paintMessages()-beforePaints; presents=host.frames()-beforeFrames;
    }
    auto warm=host.perfSamples().summary(true), steady=host.perfSamples().summary(false);
    bool ok=scenario==L"startup" ? host.firstPresentMs()>=0 && host.firstPresentMs()<=1000 : scenario==L"idle" ? detail::idlePass(presents,paints,cpuMs) : steady.count>0 && (delay ? steady.overBudget>0 : steady.maximum<=16.7);
    PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) throw std::runtime_error("Cannot read process memory");
    std::filesystem::create_directories(evidence); std::ofstream out(evidence/"perf.tsv");
    out << "scenario\tappearance\tresolution\tscale\trenderer_driver\twindows_build\tconfiguration\tmode\tphase\tcount\tp50_ms\tp95_ms\tp99_ms\tmax_ms\tover_16_7\tstartup_ms\tinput_present_ms\tpresents\twm_paint\tcpu_ms\tdelay_ms\tpeak_working_set_bytes\trun_kind\tstatus\n";
    for (auto phase : {true,false}) { const auto& v=phase?warm:steady;
        out << narrow(scenario) << '\t' << narrow(appearance) << '\t' << (large?"3840x2160":"1920x1080") << '\t' << (large?2:1) << '\t' << host.rendererName() << '\t' << windowsBuild() << '\t'
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << '\t' << (offscreen?"offscreen-no-compositor":"window") << '\t' << (phase?"warmup":"steady") << '\t' << v.count << '\t' << v.p50 << '\t' << v.p95 << '\t' << v.p99 << '\t' << v.maximum << '\t' << v.overBudget << '\t' << host.firstPresentMs() << '\t' << host.inputPresentMs() << '\t' << presents << '\t' << paints << '\t' << cpuMs << '\t' << delay << '\t' << memory.PeakWorkingSetSize << '\t' << (durationMs?"smoke":"measurement") << '\t' << (ok?"PASS":"FAIL") << '\n';
    }
    if(!out) throw std::runtime_error("Cannot write perf report");
    SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previousProc)); x04OriginalProcs.erase(host.window());
    return ok?0:1;
}

int runDemoPerf(const std::filesystem::path& evidence, const std::wstring& scenario, const std::wstring& appearance,
             bool large, int delay, int durationMs, bool alternateFont, const char* reportFile="demo-perf.tsv") {
    // #73: slider-drag-text-size only reports per-frame and per-stage times; it never decides pass or fail.
    const bool sliderDrag=scenario==L"slider-drag-text-size";
    // #75: the idle rule must still hold after one transition has played (arrow key on the volume slider).
    const bool idleAfterAnimation=scenario==L"idle-after-animation";
    if (scenario!=L"startup" && scenario!=L"list-hold-down" && scenario!=L"typing" && scenario!=L"idle" && !sliderDrag && !idleAfterAnimation)
        throw std::runtime_error("Unknown perf scenario");
    if (appearance!=L"web" && appearance!=L"material") throw std::runtime_error("Unknown perf appearance");
    if (delay<0 || durationMs<0) throw std::runtime_error("Invalid perf duration or delay");
    const auto& selectedAppearance=appearance==L"web" ? webAppearance() : materialAppearance();
    settingsDemo::Demo form;
    if (alternateFont) {
        auto document=form.document();
        const auto& fonts=systemFontCatalog().families();
        auto found=std::find_if(fonts.begin(),fonts.end(),[](const auto& family) { return family.key!=u"Segoe UI" && !family.key.empty(); });
        if(found==fonts.end()) throw std::runtime_error("No non-default font available");
        document.presentation.fontFamily=found->key; form.loadDocument(document);
    }
    if (scenario==L"list-hold-down") {
        auto document=form.document();
        while(document.items.size()<100) { int n=int(document.items.size())+1; auto id=std::to_string(1000+n); document.items.push_back({OptionValue{"T"+id},u"Performance item",u""}); }
        document.nextItemId=1101; form.loadDocument(document);
    }
    DesktopHost host(form.ui,selectedAppearance,[&]{return form.page();},form.scroll,Fixture::options());
    const bool offscreen=large && (GetSystemMetrics(SM_CXSCREEN)<3840 || GetSystemMetrics(SM_CYSCREEN)<2160);
    if (sliderDrag && offscreen) throw std::runtime_error("slider-drag-text-size needs real mouse input, so it cannot run offscreen");
    host.perfEnable(delay,offscreen);
    auto previousProc=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(host.window(),GWLP_WNDPROC));
    x04OriginalProcs.emplace(host.window(),previousProc);
    if (!SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&x04WindowProc))) throw std::runtime_error("Perf sizing fixture failed");
    host.forceScale(large && !offscreen ? 2.f : 1.f); host.clientSize(1920,1080);
    RECT actual{}; GetClientRect(host.window(),&actual);
    if(actual.right!=(large && !offscreen ? 3840 : 1920) || actual.bottom!=(large && !offscreen ? 2160 : 1080)) throw std::runtime_error("Perf client size mismatch");
    ShowWindow(host.window(),SW_SHOW); SetForegroundWindow(host.window()); SetFocus(host.window()); host.pump();
    if (GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window did not acquire foreground focus");
    if (scenario==L"list-hold-down") {
        if (!form.nav.select({"items"}) || !form.itemsList.focus())
            throw std::runtime_error("Perf list setup failed");
    } else if (scenario==L"typing" || scenario==L"idle") {
        if (!form.nav.select({"general"}) || !form.name.focus()) throw std::runtime_error("Perf typing setup failed");
    } else if (idleAfterAnimation) {
        if (!form.nav.select({"general"}) || !form.volume.focus()) throw std::runtime_error("Perf animation setup failed");
    } else if (sliderDrag) {
        if (!form.nav.select({"appearance"})) throw std::runtime_error("Perf slider setup failed");
    }
    host.pump();
    if(scenario!=L"startup") host.perfReset();
    auto send=[&](WORD key, DWORD flags) { if(GetForegroundWindow()!=host.window() || GetFocus()!=host.window()) throw std::runtime_error("Perf window lost foreground focus"); INPUT input{}; input.type=INPUT_KEYBOARD; input.ki.wVk=(flags & KEYEVENTF_UNICODE)?0:key; input.ki.wScan=(flags & KEYEVENTF_UNICODE)?key:0; input.ki.dwFlags=flags; if(SendInput(1,&input,sizeof(input))!=1) throw std::runtime_error("SendInput failed"); };
    auto wait=[&](int milliseconds) { auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds); while(std::chrono::steady_clock::now()<end) { host.pump(); auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()).count(); if(remaining>0) MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(remaining),QS_ALLINPUT,MWMO_INPUTAVAILABLE); } host.pump(); };
    if(scenario==L"list-hold-down") {
        int duration=durationMs ? durationMs : 5000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration);
        while(std::chrono::steady_clock::now()<end) { send(VK_DOWN,0); wait(33); } send(VK_DOWN,KEYEVENTF_KEYUP); host.pump();
    } else if(sliderDrag) {
        const std::string sliderId="appearance.text_size";
        auto thumb=[&]() -> POINT {
            const auto* item=host.frame().find(form.textSize);
            if (!item) { host.paint(); item=host.frame().find(form.textSize); }
            if (!item || item->sliderTrack.w<=0) throw std::runtime_error("Perf slider track not arranged");
            const auto& track=item->sliderTrack; int value=*form.textSize.draft();
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
        auto startPoint=thumb();
        mouse(startPoint,MOUSEEVENTF_MOVE); wait(100);
        POINT cursor{}; GetCursorPos(&cursor); POINT want=startPoint; ClientToScreen(host.window(),&want);
        if (std::abs(cursor.x-want.x)>3 || std::abs(cursor.y-want.y)>3) throw std::runtime_error("Perf cursor did not reach the slider thumb");
        mouse(startPoint,MOUSEEVENTF_LEFTDOWN); wait(50);
        if (!detail::access(form.ui).sliderDragX(sliderId)) throw std::runtime_error("Perf slider drag did not start");
        host.perfReset();
        // About 16 ms per move, sweeping the whole track back and forth every 2 s.
        int duration=durationMs ? durationMs : 5000; auto begin=std::chrono::steady_clock::now(), until=begin+std::chrono::milliseconds(duration);
        while(std::chrono::steady_clock::now()<until) {
            double t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/2000.0;
            double phase=t-std::floor(t), ratio=phase<.5 ? phase*2 : 2-phase*2;
            const auto* item=host.frame().find(form.textSize);
            if (item && item->sliderTrack.w>0) mouse({item->sliderTrack.x+static_cast<int>(std::lround(item->sliderTrack.w*ratio)), item->sliderTrack.y+item->sliderTrack.h/2},MOUSEEVENTF_MOVE);
            wait(16);
        }
        POINT releasePoint=startPoint;
        if (const auto* item=host.frame().find(form.textSize)) releasePoint={item->sliderTrack.x, item->sliderTrack.y+item->sliderTrack.h/2};
        mouse(releasePoint,MOUSEEVENTF_LEFTUP); wait(100);
    } else if(scenario==L"typing") {
        int duration=durationMs ? durationMs : 10000; auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(duration); int i=0;
        while(std::chrono::steady_clock::now()<end) {
            // Keep producing edits after the demo name field reaches its 64-character limit.
            if (i && i%50==0) { send(VK_CONTROL,0); send('A',0); send('A',KEYEVENTF_KEYUP); send(VK_CONTROL,KEYEVENTF_KEYUP); }
            WORD c=(i++%2)?0x4e2d:L'A'; send(c,KEYEVENTF_UNICODE); send(c,KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(50);
        }
    }
    int paints=0,presents=0; double cpuMs=0;
    if(scenario==L"idle" || idleAfterAnimation) {
        if (idleAfterAnimation) {
            // One real arrow key; the 150 ms slide must be over (and the host quiet) before measuring.
            send(VK_RIGHT,0); send(VK_RIGHT,KEYEVENTF_KEYUP); wait(100);
            if (!detail::access(form.ui).animationRunning()) throw std::runtime_error("Perf animation did not start (animations are off in Windows, or the window is hidden)");
            wait(1500);
            if (detail::access(form.ui).animationRunning()) throw std::runtime_error("Perf animation did not end");
        } else { send(L'A',KEYEVENTF_UNICODE); send(L'A',KEYEVENTF_UNICODE|KEYEVENTF_KEYUP); wait(1500); }
        int beforePaints=host.paintMessages(), beforeFrames=host.frames(); FILETIME c{},e{},k1{},u1{},k2{},u2{};
        GetProcessTimes(GetCurrentProcess(),&c,&e,&k1,&u1); wait(durationMs ? durationMs : 10000); GetProcessTimes(GetCurrentProcess(),&c,&e,&k2,&u2);
        auto ticks=[](FILETIME t){ULARGE_INTEGER x{}; x.LowPart=t.dwLowDateTime; x.HighPart=t.dwHighDateTime; return x.QuadPart;};
        cpuMs=double(ticks(k2)-ticks(k1)+ticks(u2)-ticks(u1))/10000.; paints=host.paintMessages()-beforePaints; presents=host.frames()-beforeFrames;
    }
    auto warm=host.perfSamples().summary(true), steady=host.perfSamples().summary(false);
    bool ok=sliderDrag ? true : scenario==L"startup" ? host.firstPresentMs()>=0 && host.firstPresentMs()<=1000 : (scenario==L"idle" || idleAfterAnimation) ? detail::idlePass(presents,paints,cpuMs) : steady.count>0 && (delay ? steady.overBudget>0 : steady.maximum<=16.7);
    PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))) throw std::runtime_error("Cannot read process memory");
    if (sliderDrag && warm.count+steady.count==0) throw std::runtime_error("Perf slider drag produced no frames");
    auto stagesWarm=host.perfBreakdown().summary(true), stagesSteady=host.perfBreakdown().summary(false);
    std::filesystem::create_directories(evidence); std::ofstream out(evidence/reportFile);
    out << "scenario\tappearance\tfont_mode\tresolution\tscale\trenderer_driver\twindows_build\tconfiguration\tmode\tphase\tcount\tp50_ms\tp95_ms\tp99_ms\tmax_ms\tover_16_7\tstartup_ms\tinput_present_ms\tpresents\twm_paint\tcpu_ms\tdelay_ms\tpeak_working_set_bytes\trun_kind\tstatus";
    if (sliderDrag) for (const char* stage : {"layout","list","gpu"}) for (const char* column : {"p50","p95","p99","max"}) out << '\t' << stage << '_' << column << "_ms";
    out << '\n';
    for (auto phase : {true,false}) { const auto& v=phase?warm:steady;
        out << narrow(scenario) << '\t' << narrow(appearance) << '\t' << (alternateFont?"non-default":"default") << '\t' << (large?"3840x2160":"1920x1080") << '\t' << (large?2:1) << '\t' << host.rendererName() << '\t' << windowsBuild() << '\t'
#ifdef NDEBUG
            << "Release"
#else
            << "Debug"
#endif
            << '\t' << (offscreen?"offscreen-no-compositor":"window") << '\t' << (phase?"warmup":"steady") << '\t' << v.count << '\t' << v.p50 << '\t' << v.p95 << '\t' << v.p99 << '\t' << v.maximum << '\t' << v.overBudget << '\t' << host.firstPresentMs() << '\t' << host.inputPresentMs() << '\t' << presents << '\t' << paints << '\t' << cpuMs << '\t' << delay << '\t' << memory.PeakWorkingSetSize << '\t' << (durationMs?"smoke":"measurement") << '\t' << (sliderDrag?"REPORT":ok?"PASS":"FAIL");
        if (sliderDrag) {
            const auto& stages=phase?stagesWarm:stagesSteady;
            for (const auto* stage : {&stages.layout,&stages.list,&stages.gpu}) out << '\t' << stage->p50 << '\t' << stage->p95 << '\t' << stage->p99 << '\t' << stage->maximum;
        }
        out << '\n';
    }
    if(!out) throw std::runtime_error("Cannot write perf report");
    SetWindowLongPtrW(host.window(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previousProc)); x04OriginalProcs.erase(host.window());
    return ok?0:1;
}

LONG WINAPI reportCrash(EXCEPTION_POINTERS* info) {
    auto address = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::cerr << "CRASH code=0x" << std::hex << info->ExceptionRecord->ExceptionCode << " module offset=0x" << (address - base) << '\n';
    return EXCEPTION_EXECUTE_HANDLER;
}

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    SetUnhandledExceptionFilter(reportCrash);
    try {
        std::filesystem::path evidence = "host-evidence";
        bool dl01Only = false, trial = false, matrix = false, presentation = false, backendCheck = false, r06Only = false, ddOnly = false, contextMenuOnly = false, animationOnly = false;
        bool multilineOnly = false, verticalOnly = false, richtextOnly = false, wordSelectOnly = false, justifyOnly = false;
                bool uiaIme = false, demoComparison = false, demoPerf = false, demoPerfFont = false;
        bool ime = false, clipboardOnly = false, dpi = false, x02Only = false, x04Only = false, x04ClippedOnly = false, x04ExecutingOnly = false, x04DisabledOnly = false, x04SelectionOnly = false, hoverOnly = false, focusOnly = false;
        int repeat = 1, perfDelay=0, perfDuration=0; bool perfLarge=false;
        std::wstring perf, perfAppearance=L"web";
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--demo-perf" && i+1<argc) { demoPerf=true; perf=argv[++i]; }
            else if (arg == L"--perf" && i+1<argc) perf=argv[++i];
            else if (arg == L"--demo-perf-font") demoPerfFont=true;
            else if (arg == L"--perf-appearance" && i+1<argc) perfAppearance=argv[++i];
            else if (arg == L"--perf-large") perfLarge=true;
            else if (arg == L"--paint-delay-ms" && i+1<argc) perfDelay=std::stoi(argv[++i]);
            else if (arg == L"--perf-duration-ms" && i+1<argc) perfDuration=std::stoi(argv[++i]);
            else if (arg == L"--evidence" && i + 1 < argc) evidence = argv[++i];
            else if (arg == L"--dl01-only" || arg == L"--render-compare") dl01Only = true;
            else if (arg == L"--demo-compare") demoComparison = true;
            else if (arg == L"--trial-screenshots") trial = true;
            else if (arg == L"--presentation-matrix") matrix = true;
            else if (arg == L"--presentation-idle") presentation = true;
            else if (arg == L"--backend-check") backendCheck = true;
            else if (arg == L"--r06-only") r06Only = true;
            else if (arg == L"--dd-only") ddOnly = true;
            else if (arg == L"--context-menu-only") contextMenuOnly = true;
            else if (arg == L"--multiline-only") multilineOnly = true;
            else if (arg == L"--justify-only") justifyOnly = true;
            else if (arg == L"--animation-only") animationOnly = true;
            else if (arg == L"--vertical-only") verticalOnly = true;
            else if (arg == L"--richtext-only") richtextOnly = true;
            else if (arg == L"--word-select-only") wordSelectOnly = true;
            else if (arg == L"--ime") ime = true;
            else if (arg == L"--uia-ime") uiaIme = true;
            else if (arg == L"--dpi") dpi = true;
            else if (arg == L"--clipboard-only") clipboardOnly = true;
            else if (arg == L"--x02-only") x02Only = true;
            else if (arg == L"--hover-only") hoverOnly = true;
            else if (arg == L"--focus-only") focusOnly = true;
            else if (arg == L"--x04-only") x04Only = true;
            else if (arg == L"--x04-clipped-only") x04ClippedOnly = true;
            else if (arg == L"--x04-executing-only") x04ExecutingOnly = true;
            else if (arg == L"--x04-disabled-only") x04DisabledOnly = true;
            else if (arg == L"--x04-selection-only") x04SelectionOnly = true;
            else if (arg == L"--repeat" && i + 1 < argc) repeat = std::stoi(argv[++i]);
            else throw std::runtime_error("Usage: lcars_host_verify [--ime | --dpi | --x02-only | --x04-only | --x04-clipped-only | --x04-executing-only | --x04-disabled-only | --x04-selection-only | --clipboard-only [--repeat N]] [--evidence DIR]");
        }
        if (demoComparison) return demoCompare(evidence);
        if (uiaIme) return runUiaIme(evidence, perfAppearance);
        if (animationOnly) {
            std::filesystem::create_directories(evidence); Report report(evidence / "animation.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                sliderAnimationNative(*appearance, report);
            }
            return report.failures ? 1 : 0;
        }
        if (ddOnly) {
            std::filesystem::create_directories(evidence); Report report(evidence / "dd.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                longDropdownNative(*appearance, report);
            }
            return report.failures ? 1 : 0;
        }
        if (contextMenuOnly) {
            std::filesystem::create_directories(evidence); Report report(evidence / "context-menu.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                contextMenuNative(*appearance, report);
            }
            std::cout << report.checks << " context menu checks, " << report.failures << " not passing\n";
            return report.failures ? 1 : 0;
        }
        if (verticalOnly) {  // #82 native checks alone, for quick runs
            std::filesystem::create_directories(evidence); Report report(evidence / "vertical.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                verticalLabelNative(*appearance, report, evidence);
            }
            return report.failures ? 1 : 0;
        }
        if (multilineOnly) {  // #79 native checks alone, for quick runs
            std::filesystem::create_directories(evidence); Report report(evidence / "multiline.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                multilineNative(*appearance, report, evidence);
            }
            return report.failures ? 1 : 0;
        }
        if (richtextOnly) {  // #81 native checks alone (uses the real clipboard: run on an idle desktop)
            std::filesystem::create_directories(evidence); Report report(evidence / "richtext.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                richtextNative(*appearance, report, evidence);
            }
            return report.failures ? 1 : 0;
        }
        if (wordSelectOnly) {  // #83 native checks alone, for quick runs
            std::filesystem::create_directories(evidence); Report report(evidence / "word-select.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                wordSelectNative(*appearance, report);
            }
            return report.failures ? 1 : 0;
        }
        if (justifyOnly) {  // #84 native checks alone, for quick runs
            std::filesystem::create_directories(evidence); Report report(evidence / "justify.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                justifyNative(*appearance, report, evidence);
            }
            return report.failures ? 1 : 0;
        }
        if (presentation) return runPresentationIdle(evidence);
        if (trial) return trialScreenshots(evidence);
        if (matrix) return nativePresentationMatrix(evidence);
        if (r06Only) {
            std::filesystem::create_directories(evidence);
            Report report(evidence / "r06.tsv");
            for (const auto* appearance : {&webAppearance(), &materialAppearance()}) {
                report.appearance = appearance == &webAppearance() ? "web" : "material";
                resizeFirstFrameMatchesNext(*appearance, report, 1.f);
                resizeFirstFrameMatchesNext(*appearance, report, 1.5f);
                resizeFirstFrameMatchesNext(*appearance, report, 1.5f, true);
            }
            return report.failures ? 1 : 0;
        }
        if (backendCheck) {
            Fixture fixture(webAppearance(), true);
            const auto renderer = fixture.host.rendererName();
            std::cout << "OpenGL renderer: " << renderer << '\n';
            if (renderer.find("llvmpipe") == std::string::npos) {
                std::cerr << "Expected Mesa llvmpipe\n";
                return 1;
            }
            return 0;
        }
        if (demoPerf) return runDemoPerf(evidence,perf,perfAppearance,perfLarge,perfDelay,perfDuration,demoPerfFont);
        // #73: this scenario drives the demo's Appearance page, so --perf routes it to the demo runner.
        // Report-only (code review 2026-11-12, item 14): a setup problem (no real mouse, window not in front, ...) is printed and
        // leaves no report; it never fails the run, so it can never fail an idle verification. verify-idle shows the missing report.
        if (perf==L"slider-drag-text-size") {
            try { return runDemoPerf(evidence,perf,perfAppearance,perfLarge,perfDelay,perfDuration,false,"perf.tsv"); }
            catch (const std::exception& e) { std::cerr << "slider-drag-text-size is report-only and could not run: " << e.what() << '\n'; return 0; }
        }
        // #75: idle after one animation, same pass rule as idle (spec 1: 10 s CPU <= 20 ms, no repaint).
        if (perf==L"idle-after-animation") return runDemoPerf(evidence,perf,perfAppearance,perfLarge,perfDelay,perfDuration,false,"perf.tsv");
        if (!perf.empty()) return runPerf(evidence,perf,perfAppearance,perfLarge,perfDelay,perfDuration);
        if (dl01Only) { std::filesystem::create_directories(evidence); Report report(evidence / "dl01.tsv"); std::ofstream gpuTsv(evidence/"render-compare.tsv"); gpuTsv << "scenario\tappearance\tsize\tscale\trenderer\twindows_build\tconfiguration\tdifferent_fraction\tmaximum_difference\trole_mask\tfailed_role_mask\tregion_results\tregions_ok\tstatus\n"; for (const auto* appearance : {&webAppearance(), &materialAppearance()}) { report.appearance = appearance == &webAppearance() ? "web" : "material"; drawListInitial(*appearance, report, &gpuTsv, evidence); gpuDrawListSmoke(*appearance, report, evidence); } gpuTsv.flush(); bool written=bool(gpuTsv); gpuTsv.close(); std::ifstream verify(evidence/"render-compare.tsv"); std::string line; int lines=0; bool header=false; while(std::getline(verify,line)){ if(lines==0)header=line.find("scenario\tappearance\tsize\tscale\trenderer\twindows_build\tconfiguration\tdifferent_fraction\tmaximum_difference\trole_mask\tfailed_role_mask\tregion_results\tregions_ok\tstatus")!=std::string::npos; ++lines; } report.check("GC05-tsv", written && header && lines==41, "complete comparison table has 40 rows and required columns"); return report.failures ? 1 : 0; }
        if (repeat < 1 || (repeat != 1 && !clipboardOnly) || int(ime) + int(clipboardOnly) + int(dpi) + int(x02Only) + int(x04Only) + int(x04ClippedOnly) + int(x04ExecutingOnly) + int(x04DisabledOnly) + int(x04SelectionOnly) + int(hoverOnly) + int(focusOnly) > 1)
            throw std::runtime_error("Invalid verification options");
        return ime ? runIme(evidence) : hoverOnly ? runHover(evidence) : focusOnly ? runFocus(evidence) : dpi ? runDpi(evidence) : clipboardOnly ? runClipboard(evidence, repeat) : x02Only ? runX02(evidence) : x04Only ? runX04(evidence) : x04ClippedOnly ? runX04Clipped(evidence) : x04ExecutingOnly ? runX04Executing(evidence) : x04DisabledOnly ? runX04Disabled(evidence) : x04SelectionOnly ? runX04Selection(evidence) : run(evidence);
    } catch (const std::exception& e) {
        std::cerr << "host verification error: " << e.what() << '\n';
        return 1;
    }
}
