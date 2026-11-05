# First component batch acceptance — issue #25

**Result: NOT COMPLETED.** The required 100%↔150% monitor sample and several complete cross-scenario / appearance-specific cases remain open below. A passing subcheck or a blocked sample is not a passing batch.

The tested code is `796809d`, on top of local integration `01272acc71d3d85a6b4bf68136a4f558ea1cc1c6`; the local integration tip was merged and was already an ancestor. The production host fix is `afe67bd`; the next commit adds acceptance tests only, so the native IME, DPI and clipboard executables exercise the same production and host-verification code. This report and evidence are in the following evidence-only commit. Windows 11 Pro N 26H2 build 26300.9711; clang-cl 19.1.5; NVIDIA GeForce RTX 3060, driver 610.88, OpenGL 3.3.0. Microsoft Bopomofo: IMTCTIP.DLL 6.2.26100.9711, ChtIME.exe 6.2.26100.9278. Per-monitor-aware enumeration returned DISPLAY1 144 DPI (150%, 1920×1080 physical pixels) and DISPLAY2 120 DPI (125%, 1080×1920 physical pixels). The latter is **not** 100%.

## How to reproduce and interpret

Run sequentially from the repository root: `./build.ps1 -Configuration Debug`, `./build.ps1 -Configuration Release`, `./build.ps1 -Configuration Release -CoreOnly`; then run `build/Release/lcars_host_verify.exe --clipboard-only --repeat 100 --evidence DIR`, `--ime --evidence DIR`, and `--dpi --evidence DIR`. The last two take the foreground and send real keyboard/pointer input; `--dpi` moves the window between the first two horizontally ordered displays. Stop interacting while they run. The native tests are deliberately not run concurrently. The core/form cases exercise application-owned models and typed handles; the host seam supplies native input, geometry and IME observations. No UIA or MCP adapter is implied.

Debug desktop CTest: **7/7**; Release desktop: **7/7**, including **62/62** native checks; Release core-only: **6/6**. Release components **61/61**, layout **21/21**, list-page **6/6**, and assembled showcase **33/33** for each appearance and each of two ID sets (132 executions, 916 cumulative assertions). [Debug CTest log](ticket-25-debug-ctest.log), [Release CTest log](ticket-25-release-ctest.log), [core-only log](ticket-25-release-core-ctest.log), [native results](ticket-25-host/host.tsv). The project C++ compilation database has the C++20 flag on 20/20 Debug desktop, 20/20 Release desktop and 12/12 Release core-only translation units. Warnings from the existing OpenGL function-pointer casts and HarfBuzz CMake support were emitted; builds passed.

The application assembles both appearances from the same form and list source, using typed handles and explicitly connected application-owned values. The independent public-header consumer, including negative checks for string-ID operations, passed. IDs used by the host verification seam and semantic descriptions are not application lookup APIs.

In the tables, **P** means the listed expected outcome was observed for that appearance using the stated evidence, **Partial** means relevant checks passed but the entire specified case was not independently exercised under that appearance. W = Minimal Web; M = Material-inspired. The cases are not counted as independent requirements merely because multiple assertions support them. The source files named in the evidence column give exact steps and assertions; the TSVs give native case-by-case actual results. Sizes in the host matrix are client DIP sizes, with offscreen forced scaling at 100% and 150%.

## Cross-scenario cases

| Case and expected result | W | M | Actual evidence / limit |
| --- | --- | --- | --- |
| X01 keyboard-only completion of all three pages, including restricted, empty and modal states | P | P | `edit_form` X01 uses only keyboard input through the host seam, arranging each step in the chosen appearance: apply, disable/recover, empty and populated list, modal confirm and exit. Native tests separately cover real Win32 message dispatch. |
| X02 960×720 and 640×480 at 100% and 150%, no overlap, correct scroll/hit/popups | Partial | Partial | Native V01 matrix and V03 dialog/tooltip geometry pass; V02 and every interaction were not exercised at every size/scale combination. Real scaling is D01. |
| X03 same F01, F07, L03, L07, P02 and P03 outcomes | P | P | Same expected identities, values and counts asserted in `edit_form`, `list_page` and native cases; both appearances use the same application source. |
| X04 names, values, restrictions and visible presentation agree | Partial | Partial | Public semantic snapshots, disabled/read-only and modal retry rejection, and representative rendering pass; no exhaustive visible/semantic comparison covers every component and state. |
| X05 reset each page, rerun representative cases, preserve other pages | P | P | `edit_form` X05 resets and reruns V01 apply, V02 deletion and V03 confirmation while checking other-page data survives. |

## V01 form cases

| Case / expected | W | M | Actual evidence |
| --- | --- | --- | --- |
| F01 five drafts applied once | P | P | `edit_form`: exact values and one application. |
| F02 Chinese composition / Enter does not apply | P | P | [IME](ticket-25-ime/ime.tsv) F02, composition and confirmation once. |
| F03 multiline paste, undo and redo | P | P | `edit_form` AC09: `Alpha Beta`, original, `Alpha Beta`, applied result unchanged. |
| F04 read-only inspect/copy, reject edits | P | P | `edit_form` AC10 plus shared `components` read-only copy/edit tests. |
| F05 disabled input rejected and recovered | P | P | `edit_form` AC10: pointer, focus and public retries rejected; normal fixture restores data/history. |
| F06 blank-name validation and correction | P | P | `edit_form` AC05: correct field/focus, zero then one apply. |
| F07 dropdown Escape/outside/Tab cancellation | P | P | Native F07 checks candidate, cancellation, focus and popup click. |
| F08 confirm, empty, restore and reselect | P | P | `edit_form` F08 checks South, missing region error, no resurrection, explicit Central. |
| F09 checkbox, radio and slider boundaries | P | P | `edit_form` F09 and native F09 keyboard/pointer checks. |
| F10 cancelled and legal button activation | P | P | Native AC06/F10/N02 and `edit_form` AC06: cancelled press, click and keyboard counts. |

## V02 list cases

| Case / expected | W | M | Actual evidence |
| --- | --- | --- | --- |
| L01 empty/single focus and boundaries | P | P | `edit_form` V02 navigation: empty Tab exit and T001. |
| L02 T003→T004→T020→T001 | P | P | `edit_form` V02 navigation uses arranged row hit and host pointer/keyboard seam. |
| L03 duplicate name retains T006 through sorting | P | P | `edit_form` V02 identity checks selection and detail with both orders. |
| L04 filtering clears and does not revive selection | P | P | `edit_form` V02 identity checks T020 and clearing the filter. |
| L05 delete selected item once | P | P | `edit_form` V02 identity checks cleared selection, T004 candidate and disabled repeat delete. |
| L06 wheel, scrollbar and End reach T100 | P | P | Native L06/N07 and R02: wheel, actual thumb drag, click, End, shrink clamp. |
| L07 retain V01 draft and V02 search/T006 across pages | P | P | `edit_form` page scopes and progress/page isolation. |
| L08 V02 reset keeps V01 draft/history | P | P | `edit_form` page scopes: V02 initial list and V01 undo/offsets. |

## V03 popup and progress cases

| Case / expected | W | M | Actual evidence |
| --- | --- | --- | --- |
| P01 cancel and Escape preserve progress | P | P | `edit_form` cancel and native P01 Escape. |
| P02 keyboard confirm exactly once | P | P | `edit_form` and native P02: 0%, one confirmation, focus return. |
| P03 modal confines focus and blocks background | P | P | `edit_form` plus native P03 mask, Tab, geometry and admission. |
| P04 removed opener falls back on cancel/confirm | P | P | `edit_form` P04 exercises both outcomes and stale handle. |
| P05 tooltip coexists with pointer and Space | P | P | Native P05/P06 and real-input P05; Escape, dwell and button action. |
| P06 narrow-window tooltip/dialog remains usable | P | P | Native tooltip and dialog geometry at all four forced size/scale samples; layout tests wrap and keep lines inside. |
| P07 clamp displayed progress without changing source | P | P | Native P07 and `edit_form`: 0/50/100/-10/110 and painted fill. |
| P08 reset V03, preserve other pages | P | P | `edit_form` P08 and progress isolation. |

## Prototype regressions

| Case / expected | W | M | Actual evidence |
| --- | --- | --- | --- |
| R01 Space activation does not leak text | P | P | Native AC07 and real-input AC07, including repeat/release and later space typing. |
| R02 narrow End reveals T100 | P | P | Native R02 and nested list checks at 640 DIP. |
| R03 delete T003, candidate T004, no selection | P | P | `edit_form` V02 identity. |
| R04 region error stays on region | P | P | `edit_form` F08. |
| R05 restricted/empty states readable | P | P | `edit_form` AC10, `components` and `layout` readable state checks. |
| R06 resize after deep scroll gives consistent first frame/hit | P | P | Native R06 at forced 100% and 150%. |
| R07 V02 reset preserves V01 draft/history/horizontal offset | P | P | `edit_form` page scopes. |
| R08 real deactivation/reactivation restores tooltip without focus theft | P | P | [IME results](ticket-25-ime/ime.tsv) R08 real foreground switch. |
| R09 disabled T008 skipped and cannot be selected | P | P | Native R09 pointer/keyboard plus public selection rejection. |
| R10 Tab reentry starts at first valid row without selection | P | P | `edit_form` V02 identity uses actual host Tab after deletion. |
| R11 filtered T009 recovers T010 | P | P | `edit_form` V02 neighbors, `list_page` R11. |

## Formal supplemental cases

| Case / expected | W | M | Actual evidence / limit |
| --- | --- | --- | --- |
| N01 long label and multiline error, association survives appearance | P | P | `edit_form` N01 checks both wrap and stay inside the remeasured field, the next control moves down, and semantic associations remain intact. |
| N02 held Enter/Space and cancelled press/deactivation | P | P | Native AC06/F10/N02 and `edit_form` button cases. |
| N03 empty radio, disabled first/last/all, exit | P | P | `edit_form` N03 checks rendered empty, boundary-disabled and all-disabled groups, navigation and Tab exit under each appearance. |
| N04 non-divisible slider and invalid ranges | P | P | `edit_form` N04 renders -3..8/4 and checks endpoints, steps and invalid configurations for each appearance. |
| N05 disabled tab and hidden-page admission | P | P | `edit_form` N05 checks rendered tabs, keyboard/programmatic rejection and hidden-page admission for each appearance. |
| N06 rows, columns, grid, long text/narrow/large type | P | P | `layout` N06 uses 16/24 text sizes with both appearances. |
| N07 scrollbar reaches T100 and clamps after shrink | P | P | Native L06/N07 thumb drag, click and shrink. |
| N08 selected T008 disabled, neighbor/identity recovery | P | P | `edit_form` V02 neighbors and shared `list_page` N08. |

## Platform evidence

| Case | Result | Expected and actual |
| --- | --- | --- |
| E01, both appearances | **16/16 required scenario/appearance samples passed** | Confirm, cancel, selection replacement, Tab, pointer blur, page switch, dialog opening, and close were exercised with the recorded Microsoft Bopomofo version. The full run was [46/46 checks](ticket-25-ime/ime.tsv), with [events](ticket-25-ime/ime-events.log). Close uses the real WM_CLOSE/run-loop/host-destruction path: an unconfirmed preedit is cancelled, draft `測試使用者`, history 0, no apply, composing 0; logical focus remains on the retained field after the native window is destroyed. |
| D01, both appearances | **Partial: 6/6 available-transition checks passed; required sample open** | [DPI results](ticket-25-dpi/dpi.tsv) record real 144→120→144 DPI messages, retained focus/selection, physical click/caret, active composition, IME caret and candidate-position requests, and popup hit/placement. This is 150%↔125%, not the required 100%↔150%; the OS candidate window's actual screen position was not visually verified. |
| Clipboard, both appearances | **400/400 passed**, 0 failed | [Final solo run](ticket-25-clipboard-final/clipboard.tsv), 100 repetitions × two appearances × normal and forced 60 ms contention. Earlier concurrent-agent failures and #19's unexplained 2/400 solo failures remain historical; this run does not establish why they occurred or guarantee longer clipboard holds. |

The new close test first failed twice with a retained `composing=1` after destruction ([before fix](ticket-25-ime-before-fix.tsv)); the host now cancels the native and retained composition before releasing its window. An initial DPI probe incorrectly treated field-relative editor coordinates as window-relative; [its two failures](ticket-25-dpi-probe-before-correction.tsv) were probe errors, corrected before the final physical-click run. Neither intermediate run is counted as a final pass. Trailing whitespace was removed from the saved host and event logs without changing outcomes. Forced-scale images supplement geometry rather than replace assertions: [Web long error](ticket-25-host/web-640x480-100-long-error.bmp), [Material long error](ticket-25-host/material-640x480-100-long-error.bmp).

## Not completed

1. **D01:** Set one display to 100% while retaining a 150% display, record the OS and IME versions and actual per-monitor DPI, then run `--dpi` for each appearance with the focused selection and active candidate; confirm the *actual* candidate window follows the caret on both displays, exercise pointer and popup placement in both directions, and restore the display settings. The available displays were 125% and 150%; changing the user's desktop scaling was not performed here.
2. **X02 and X04:** Run and record the full page/size/scale layout matrix and all visible/semantic restricted-state comparisons. The passing constituent checks above do not establish these complete cases.
3. **User trial U01:** No user feedback on the three scenarios was supplied or obtained. UIA/screen-reader and MCP adapters are separate future work (A01 / #10), not proven by the public C++ semantics here.
4. **Identity retention and clipboard history:** Removed button identities are retained as tombstones until UI destruction so stale handles cannot revive; repeated remove/reset cycles retain small records. The earlier intermittent clipboard failures remain unexplained even though the final solo stress run passed.
