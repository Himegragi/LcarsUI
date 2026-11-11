#pragma once
// The test-only window messages the UIA demo host (uia_demo_host.cpp) answers and the cross-process demo client
// (uia_demo_client.cpp) sends. One list, so a number is never reused by accident (the host answers
// WM_APP + 80 for the plain UIA host, a different program).
#include <windows.h>

namespace uia_demo {
constexpr UINT message_set_search_value = WM_APP + 81;   // wParam 1: the Items search box gets "sample"; 0: empty
constexpr UINT message_select_in_search = WM_APP + 82;   // selects the first two characters of the Items search box
constexpr UINT message_navigate = WM_APP + 83;           // wParam: the page / dialog step to put the demo in
constexpr UINT message_expect_page = WM_APP + 84;        // wParam: the same step; 1 when the demo is there
constexpr UINT message_text_flow = WM_APP + 85;          // #80/#84 text alignment of the search box (see the host)
constexpr UINT message_context_menu = WM_APP + 86;       // #85 context menu driver (see the host)
constexpr UINT message_rich_text = WM_APP + 87;          // #81 styled note value / theme switch (see the host)
constexpr UINT message_hover = WM_APP + 88;              // #74 hover driver (see the host)
}
