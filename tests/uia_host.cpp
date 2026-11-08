#include "desktop_host.hpp"
#include "edit_form.hpp"
#include <windows.h>
#include <iostream>
namespace {
WNDPROC original_proc = nullptr;
edit_form::Form* active_form = nullptr;
LRESULT CALLBACK test_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_APP + 80) {
        switch (w) {
        case 1: {
            auto name = L"Local\\LcarsUiaBusy-" + std::to_wstring(GetCurrentProcessId());
            if (auto signal = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str())) { SetEvent(signal); CloseHandle(signal); }
            Sleep(3000); return 0;
        }
        case 2: return static_cast<LRESULT>(active_form->handles.tabs.activate({"v03"}).status);
        case 3: return static_cast<LRESULT>(active_form->handles.dialog_temporary.remove().status);
        case 4: return static_cast<LRESULT>(active_form->handles.reset_v03.invoke().status);
        case 5: return static_cast<LRESULT>(active_form->handles.tabs.activate({"v01"}).status);
        case 6: return static_cast<LRESULT>(active_form->handles.tabs.activate({"v02"}).status);
        case 7: return static_cast<LRESULT>(active_form->handles.dialog_open.invoke().status);
        case 8: return static_cast<LRESULT>(active_form->handles.dialog_cancel.invoke().status);
        }
    }
    return CallWindowProcW(original_proc, window, msg, w, l);
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        const bool material = argc > 1 && std::wstring_view(argv[1]) == L"material";
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
        const auto& appearance = material ? lcars::material_appearance() : lcars::web_appearance();
        lcars::DesktopOptions options; options.title = u"LcarsUI UIA test host";
        lcars::detail::DesktopHost host(form.ui, appearance, [&] { return form.page(); }, form.scroll, options, false, true);
        active_form = &form;
        original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(host.window(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(test_proc)));
        ShowWindow(host.window(), SW_SHOWNOACTIVATE); UpdateWindow(host.window());
        return host.run();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
