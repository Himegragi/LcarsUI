#include "desktop_host.hpp"
#include "edit_form.hpp"
#include "core_internal.hpp"
#include "renderer.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <iostream>
#include <algorithm>
namespace {
WNDPROC original_proc = nullptr;
edit_form::Form* active_form = nullptr;
lcars::detail::DesktopHost* active_host = nullptr;
int root_requests = 0;
lcars::detail::RendererTestHooks* active_hooks = nullptr;
LRESULT CALLBACK test_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_GETOBJECT && static_cast<LONG>(l) == UiaRootObjectId) ++root_requests;
    if (msg == WM_APP + 80) {
        switch (w) {
        case 2: return static_cast<LRESULT>(active_form->handles.tabs.activate({"v03"}).status);
        case 3: return static_cast<LRESULT>(active_form->handles.dialog_temporary.remove().status);
        case 4: return static_cast<LRESULT>(active_form->handles.reset_v03.invoke().status);
        case 5: return static_cast<LRESULT>(active_form->handles.tabs.activate({"v01"}).status);
        case 6: return static_cast<LRESULT>(active_form->handles.tabs.activate({"v02"}).status);
        case 7: return static_cast<LRESULT>(active_form->handles.dialog_open.invoke().status);
        case 8: return static_cast<LRESULT>(active_form->handles.dialog_cancel.invoke().status);
        case 9: return static_cast<LRESULT>(active_form->handles.name.read_only(true).status);
        case 10: return static_cast<LRESULT>(active_form->handles.name.read_only(false).status);
        case 11: {
            auto focused = active_form->handles.name.focus();
            if (!focused) return static_cast<LRESULT>(focused.status);
            return static_cast<LRESULT>(lcars::detail::access(active_form->ui).composition_update(u"測", 1).status);
        }
        case 12: return static_cast<LRESULT>(lcars::detail::access(active_form->ui).composition_cancel().status);
        case 13: lcars::detail::access(active_form->ui).renderer_failed(true); return 0;
        case 14: lcars::detail::access(active_form->ui).renderer_failed(false); return 0;
        case 15: return static_cast<LRESULT>(active_form->handles.notices.toggle().status);
        case 16: return root_requests;
        case 20: return static_cast<LRESULT>(active_form->handles.apply.focus().status);
        case 21: lcars::detail::access(active_form->ui).window_active(true); return 0;
        case 23: return static_cast<LRESULT>(active_form->handles.dialog_open.focus().status);
        case 24: return lcars::detail::access(active_form->ui).focused() == "form.title.pages.v03.half";
        case 26: active_hooks->gl_error_at_frame = active_host->frames() + 1; active_host->paint(); return active_host->renderer_failed() ? 0 : 1;
        case 27: return static_cast<LRESULT>(active_host->rebuild_renderer().status);
        case 28: return active_host->renderer_failed() ? 1 : 0;
        case 32: {
            auto tree = active_form->ui.semantics();
            auto name = tree.get({"form.name"});
            auto region = tree.get({"form.region"});
            auto item20 = tree.get({"form.title.pages.v02.list", lcars::OptionValue{"T020"}});
            auto item21 = tree.get({"form.title.pages.v02.list", lcars::OptionValue{"T021"}});
            auto progress = tree.get({"form.title.pages.v03.progress"});
            auto dialog = active_form->handles.dialog.is_open();
            switch (l) {
            case 0: return name && !name->hidden && region && !region->expanded;
            case 1: return name && !name->hidden && region && region->expanded;
            case 2: return item20 && !item20->hidden && !item21;
            case 3: return progress && !progress->hidden && dialog && !*dialog;
            case 4: return progress && !progress->hidden && dialog && *dialog;
            default: return 0;
            }
        }
        case 30: { auto e = lcars::detail::access(active_form->ui).edit("form.name"); return e && e->composing ? 1 : 0; }
        case 31: return static_cast<LRESULT>(active_form->handles.name.set_value(u"UIA baseline").status);
        case 34: {
            auto focused = active_form->handles.region.focus(); if (!focused) return static_cast<LRESULT>(focused.status);
            active_host->paint();
            const auto* region = lcars::detail::find_placement(active_host->frame(), "form.region");
            if (!region) return 1;
            active_form->scroll.offset = std::max(0, active_form->scroll.offset + region->bounds.y - active_host->frame().viewport.y - 100);
            active_host->paint(); return 0;
        }
        case 35: {
            const auto* field = lcars::detail::find_placement(active_host->frame(), "form.name");
            if (!field) return -1;
            POINT point{field->bounds.x + (l ? field->bounds.w : 0), field->bounds.y + (l ? field->bounds.h : 0)};
            ClientToScreen(window, &point); return MAKELPARAM(point.x, point.y);
        }
        case 29: return static_cast<LRESULT>(active_form->handles.region.open().status);
        case 25: { auto opened = active_form->handles.dialog_open.invoke(); if (!opened) return static_cast<LRESULT>(opened.status); return static_cast<LRESULT>(active_form->handles.dialog_cancel.invoke().status); }
        case 22: { auto r = active_form->list->handles.list.focus({"T003"}); if (!r) return static_cast<LRESULT>(r.status); return static_cast<LRESULT>(lcars::detail::access(active_form->ui).key_down({.key=lcars::Key::down}).status); }
        case 17: { auto settings = active_form->ui.presentation(); settings.text_size = static_cast<int>(l); return static_cast<LRESULT>(active_form->ui.set_presentation(settings).status); }
        case 19: { const auto* item = lcars::detail::find_placement(active_host->frame(), "form.name"); if (!item) return -1; POINT p{item->caret_x(static_cast<unsigned>(l)), item->bounds.y + item->field.editor.y}; ClientToScreen(window, &p); return MAKELPARAM(p.x, p.y); }
        case 18: { auto node = active_form->ui.semantics().get({"form.name"}); if (!node || !node->text_selection) return -1; return MAKELPARAM(node->text_selection->anchor, node->text_selection->caret); }
        }
    }
    return CallWindowProcW(original_proc, window, msg, w, l);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        const bool material = argc > 1 && std::wstring_view(argv[1]) == L"material";
        const bool unhandled = argc > 2 && std::wstring_view(argv[2]) == L"--unhandled";
        const bool probe_unobserved = argc > 1 && std::wstring_view(argv[1]) == L"--probe-unobserved";
        int unrelated_reads = 0;
        bool source_bound = false;
        edit_form::Form form(edit_form::default_ids(), true, [](std::u16string_view family) {
            lcars::PresentationSettings settings; settings.font_family = family;
            return lcars::system_font_catalog().resolve(settings);
        });
        for (int i = 0; i <= static_cast<int>(lcars::Status::renderer_failed); ++i) {
            auto id = "fixture.status." + std::to_string(i);
            auto button = form.ui.add_button({.id = id, .kind = lcars::Kind::button, .label = u"Status fixture"}).value.value();
            button.bind([i] { return lcars::Result{static_cast<lcars::Status>(i), {}}; });
        }
        auto disabled = form.ui.add_button({.id = "fixture.real_disabled", .kind = lcars::Kind::button, .label = u"Disabled"}).value.value();
        disabled.enable(false);
        auto write = form.ui.add_button({.id = "fixture.name_write", .kind = lcars::Kind::button, .label = u"Write name"}).value.value();
        write.bind([&form] { return form.handles.name.set_value(u"UIA test"); });
        auto invalid = form.ui.add_button({.id = "fixture.name_invalid", .kind = lcars::Kind::button, .label = u"Invalid name"}).value.value();
        invalid.bind([&form] { return form.handles.name.set_value(std::u16string(lcars::default_max_text_length + 1, u'x')); });
        if (probe_unobserved) {
            auto label = form.ui.add_label({.id = "fixture.unrelated_label", .kind = lcars::Kind::label}).value.value();
            auto connected = label.connect_source([&] { ++unrelated_reads; return std::u16string(u"Unrelated label"); });
            auto described = label.describe();
            source_bound = connected && described && unrelated_reads > 0;
        }
        const auto& appearance = material ? lcars::material_appearance() : lcars::web_appearance();
        lcars::DesktopOptions options; options.title = u"LcarsUI UIA test host";
        lcars::detail::RendererTestHooks hooks;
        lcars::detail::DesktopHost host(form.ui, appearance, [&] { return form.page(); }, form.scroll, options, false, true, &hooks);
        if (!unhandled) host.on_renderer_failure([](const lcars::RendererFailure&) {});
        if (probe_unobserved) {
            form.ui.semantics().dispatch_events();
            unrelated_reads = 0;
            bool described = true;
            for (int i = 0; i < 5; ++i) described = described && bool(form.handles.apply.describe());
            const bool pass = source_bound && described && unrelated_reads == 0;
            std::cout << "Unobserved host unrelated semantic reads: " << (pass ? "PASS" : "FAIL") << " reads=" << unrelated_reads << '\n';
            return pass ? 0 : 1;
        }
        active_form = &form; active_host = &host; active_hooks = &hooks;
        original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(test_proc)));
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        const int result = host.run();
        return host.renderer_failed() ? static_cast<int>(lcars::Status::renderer_failed) : result;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
