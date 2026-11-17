#include "desktop_host.hpp"
#include "edit_form.hpp"
#include "core_internal.hpp"
#include "renderer.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <atomic>
std::atomic<HWND> companionWindow{nullptr};
int wmain(int, wchar_t**);
namespace {
thread_local WNDPROC originalProc = nullptr;
thread_local editForm::Form* activeForm = nullptr;
thread_local lcars::detail::DesktopHost* activeHost = nullptr;
thread_local int rootRequests = 0;
thread_local lcars::detail::RendererTestHooks* activeHooks = nullptr;
LRESULT CALLBACK testProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_APP + 81) return reinterpret_cast<LRESULT>(companionWindow.load());
    if (msg == WM_GETOBJECT && static_cast<LONG>(l) == UiaRootObjectId) ++rootRequests;
    if (msg == WM_APP + 80) {
        switch (w) {
        case 2: return static_cast<LRESULT>(activeForm->handles.tabs.activate({"v03"}).status);
        case 3: return static_cast<LRESULT>(activeForm->handles.dialogTemporary.remove().status);
        case 4: return static_cast<LRESULT>(activeForm->handles.resetV03.invoke().status);
        case 5: return static_cast<LRESULT>(activeForm->handles.tabs.activate({"v01"}).status);
        case 6: return static_cast<LRESULT>(activeForm->handles.tabs.activate({"v02"}).status);
        case 7: return static_cast<LRESULT>(activeForm->handles.dialogOpen.invoke().status);
        case 8: return static_cast<LRESULT>(activeForm->handles.dialogCancel.invoke().status);
        case 9: return static_cast<LRESULT>(activeForm->handles.name.readOnly(true).status);
        case 10: return static_cast<LRESULT>(activeForm->handles.name.readOnly(false).status);
        case 11: {
            auto focused = activeForm->handles.name.focus();
            if (!focused) return static_cast<LRESULT>(focused.status);
            return static_cast<LRESULT>(lcars::detail::access(activeForm->ui).compositionUpdate(u"測", 1).status);
        }
        case 12: return static_cast<LRESULT>(lcars::detail::access(activeForm->ui).compositionCancel().status);
        case 13: lcars::detail::access(activeForm->ui).rendererFailed(true); return 0;
        case 14: lcars::detail::access(activeForm->ui).rendererFailed(false); return 0;
        case 15: return static_cast<LRESULT>(activeForm->handles.notices.toggle().status);
        case 16: return rootRequests;
        case 20: return static_cast<LRESULT>(activeForm->handles.apply.focus().status);
        case 21: lcars::detail::access(activeForm->ui).windowActive(true); lcars::detail::access(activeForm->ui).setSystemKeyboardCues(true); return 0;  // focus tooltips need the indicator state on; UIA SetFocus does not turn it on
        case 23: return static_cast<LRESULT>(activeForm->handles.dialogOpen.focus().status);
        case 24: return lcars::detail::access(activeForm->ui).focused() == "form.title.pages.v03.half";
        case 26: activeHooks->errorAtFrame = activeHost->frames() + 1; activeHost->paint(); return activeHost->rendererFailed() ? 0 : 1;
        case 27: return static_cast<LRESULT>(activeHost->rebuildRenderer().status);
        case 28: return activeHost->rendererFailed() ? 1 : 0;
        case 32: {
            auto tree = activeForm->ui.semantics();
            auto name = tree.get({"form.name"});
            auto region = tree.get({"form.region"});
            auto item20 = tree.get({"form.title.pages.v02.list", lcars::OptionValue{"T020"}});
            auto item21 = tree.get({"form.title.pages.v02.list", lcars::OptionValue{"T021"}});
            auto progress = tree.get({"form.title.pages.v03.progress"});
            auto dialog = activeForm->handles.dialog.isOpen();
            switch (l) {
            case 0: return name && !name->hidden && region && !region->expanded;
            case 1: return name && !name->hidden && region && region->expanded;
            case 2: return item20 && !item20->hidden && !item21;
            case 3: return progress && !progress->hidden && dialog && !*dialog;
            case 4: return progress && !progress->hidden && dialog && *dialog;
            default: return 0;
            }
        }
        case 30: { auto e = lcars::detail::access(activeForm->ui).edit("form.name"); return e && e->composing ? 1 : 0; }
        case 31: return static_cast<LRESULT>(activeForm->handles.name.setValue(u"UIA baseline").status);
        case 34: {
            auto focused = activeForm->handles.region.focus(); if (!focused) return static_cast<LRESULT>(focused.status);
            activeHost->paint();
            const auto* region = lcars::detail::findPlacement(activeHost->frame(), "form.region");
            if (!region) return 1;
            activeForm->scroll.offset = std::max(0, activeForm->scroll.offset + region->bounds.y - activeHost->frame().viewport.y - 100);
            activeHost->paint(); return 0;
        }
        case 36: {
            // The UIA range describes an advance cell, not the black-pixel box.
            // Infer that cell from GPU ink and an independently rasterized glyph's bearings.
            if (!activeForm->handles.name.select(2, 2)) return -1;
            activeHost->paint();
            RECT client{}; GetClientRect(window, &client);
            auto image = activeHost->compareGpu(client.right, client.bottom, activeHost->scale()).actual;
            const auto* field = lcars::detail::findPlacement(activeHost->frame(), "form.name");
            if (!field) return -1;
            lcars::detail::Text text;
            auto glyph = text.shape(u"\u5de5", field->textSize);
            lcars::detail::Canvas sample(128, 128, {255,255,255});
            text.draw(sample, glyph, 32, 32 + lcars::detail::ShapedMeasure::baseline(field->textSize), {0,0,0});
            int sampleLeft = 128, sampleRight = -1;
            for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
                auto i = (static_cast<std::size_t>(y) * 128 + x) * 4;
                if (sample.pixels[i] != 255 || sample.pixels[i+1] != 255 || sample.pixels[i+2] != 255) {
                    sampleLeft = std::min(sampleLeft, x); sampleRight = std::max(sampleRight, x);
                }
            }
            auto editor = field->textField.editor;
            editor.x += field->bounds.x; editor.y += field->bounds.y;
            int left = image.width, right = -1;
            int start = std::max(editor.x, field->caretX(0) - 2);
            int end = std::min(editor.x + editor.w, field->caretX(1));
            for (int y = std::max(0, editor.y); y < std::min(image.height, editor.y + editor.h); ++y) {
                int bgx = std::min(image.width - 1, editor.x + editor.w - 2);
                if (bgx < 0) return -1;
                auto bg = (static_cast<std::size_t>(y) * image.width + bgx) * 4;
                for (int x = std::max(0, start); x < std::min(image.width, end); ++x) {
                    auto i = (static_cast<std::size_t>(y) * image.width + x) * 4;
                    if (image.pixels[i] != image.pixels[bg] || image.pixels[i+1] != image.pixels[bg+1] || image.pixels[i+2] != image.pixels[bg+2]) {
                        left = std::min(left, x); right = std::max(right, x);
                    }
                }
            }
            if (right < left || sampleRight < sampleLeft || std::abs((right-left)-(sampleRight-sampleLeft)) > 1) return -1;
            int cellLeft = left - (sampleLeft - 32);
            POINT origin{cellLeft, 0}; ClientToScreen(window, &origin);
            return MAKELPARAM(origin.x, origin.x + static_cast<int>(std::lround(glyph.width)));
        }
        case 35: {
            const auto* field = lcars::detail::findPlacement(activeHost->frame(), "form.name");
            if (!field) return -1;
            POINT point{field->bounds.x + (l ? field->bounds.w : 0), field->bounds.y + (l ? field->bounds.h : 0)};
            ClientToScreen(window, &point); return MAKELPARAM(point.x, point.y);
        }
        case 29: return static_cast<LRESULT>(activeForm->handles.region.open().status);
        case 25: { auto opened = activeForm->handles.dialogOpen.invoke(); if (!opened) return static_cast<LRESULT>(opened.status); return static_cast<LRESULT>(activeForm->handles.dialogCancel.invoke().status); }
        case 22: { auto r = activeForm->list->handles.list.focus({"T003"}); if (!r) return static_cast<LRESULT>(r.status); return static_cast<LRESULT>(lcars::detail::access(activeForm->ui).keyDown({.key=lcars::Key::down}).status); }
        case 17: { auto settings = activeForm->ui.presentation(); settings.textSize = static_cast<int>(l); return static_cast<LRESULT>(activeForm->ui.setPresentation(settings).status); }
        case 19: { const auto* item = lcars::detail::findPlacement(activeHost->frame(), "form.name"); if (!item) return -1; POINT p{item->caretX(static_cast<unsigned>(l)), item->bounds.y + item->textField.editor.y}; ClientToScreen(window, &p); return MAKELPARAM(p.x, p.y); }
        case 18: { auto node = activeForm->ui.semantics().get({"form.name"}); if (!node || !node->textSelection) return -1; return MAKELPARAM(node->textSelection->anchor, node->textSelection->caret); }
        }
    }
    return CallWindowProcW(originalProc, window, msg, w, l);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        const bool material = argc > 1 && std::wstring_view(argv[1]) == L"material";
        const bool unhandled = argc > 2 && std::wstring_view(argv[2]) == L"--unhandled";
        const bool probeUnobserved = argc > 1 && std::wstring_view(argv[1]) == L"--probe-unobserved";
        int unrelatedReads = 0;
        bool sourceBound = false;
        editForm::Form form(editForm::defaultIds(), true, [](std::u16string_view family) {
            lcars::PresentationSettings settings; settings.fontFamily = family;
            return lcars::systemFontCatalog().resolve(settings);
        });
        for (int i = 0; i <= static_cast<int>(lcars::Status::rendererFailed); ++i) {
            auto id = "fixture.status." + std::to_string(i);
            auto button = form.ui.addButton({.id = id, .label = u"Status fixture"}).value.value();
            button.bind([i] { return lcars::Result{static_cast<lcars::Status>(i), {}}; });
        }
        auto disabled = form.ui.addButton({.id = "fixture.real_disabled", .label = u"Disabled"}).value.value();
        disabled.enable(false);
        for (bool enabled : {false, true}) {
            auto button = form.ui.addButton({.id = enabled ? "fixture.mode_enabled" : "fixture.mode_disabled", .label = u"Radio availability"}).value.value();
            button.bind([&form, enabled] { return form.handles.mode.enable(enabled); });
        }
        auto write = form.ui.addButton({.id = "fixture.name_write", .label = u"Write name"}).value.value();
        write.bind([&form] { return form.handles.name.setValue(u"UIA test"); });
        auto invalid = form.ui.addButton({.id = "fixture.name_invalid", .label = u"Invalid name"}).value.value();
        invalid.bind([&form] { return form.handles.name.setValue(std::u16string(lcars::defaultMaxTextLength + 1, u'x')); });
        if (probeUnobserved) {
            auto label = form.ui.addLabel({.id = "fixture.unrelated_label"}).value.value();
            auto connected = label.connectSource([&] { ++unrelatedReads; return std::u16string(u"Unrelated label"); });
            auto described = label.describe();
            sourceBound = connected && described && unrelatedReads > 0;
        }
        const auto& appearance = material ? lcars::materialAppearance() : lcars::webAppearance();
        lcars::DesktopConfig options;
        if (argc > 2 && std::wstring_view(argv[2]) == L"--accessibility-disabled") options.platformAccessibilityEnabled = false;
        if (argc > 2 && std::wstring_view(argv[2]) == L"--accessibility-enabled") options.platformAccessibilityEnabled = true;
        options.title = u"LcarsUI UIA test host";
        lcars::detail::RendererTestHooks hooks;
        lcars::detail::DesktopHost host(form.ui, appearance, [&] { return form.page(); }, form.scroll, options, false, true, &hooks);
        if (!unhandled) host.onRendererFailure([](const lcars::RendererFailure&) {});
        if (probeUnobserved) {
            form.ui.semantics().dispatchEvents();
            unrelatedReads = 0;
            bool described = true;
            for (int i = 0; i < 5; ++i) described = described && bool(form.handles.apply.describe());
            const bool pass = sourceBound && described && unrelatedReads == 0;
            std::cout << "Unobserved host unrelated semantic reads: " << (pass ? "PASS" : "FAIL") << " reads=" << unrelatedReads << '\n';
            return pass ? 0 : 1;
        }
        activeForm = &form; activeHost = &host; activeHooks = &hooks;
        originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(testProc)));
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        if (argc > 2 && std::wstring_view(argv[2]) == L"--accessibility-disabled") companionWindow = host.window();
        if (argc > 2 && std::wstring_view(argv[2]) == L"--accessibility-mixed") {
            std::thread([material] {
                wchar_t executable[] = L"host", web[] = L"web", mat[] = L"material", disabled[] = L"--accessibility-disabled";
                wchar_t* args[] = {executable, material ? mat : web, disabled};
                wmain(3, args);
            }).detach();
        }
        const int result = host.run();
        return host.rendererFailed() ? static_cast<int>(lcars::Status::rendererFailed) : result;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
