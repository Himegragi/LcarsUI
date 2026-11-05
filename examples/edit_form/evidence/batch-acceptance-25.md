# First component batch acceptance — issue #25

**Result: NOT COMPLETED.** The required 100%↔150% monitor sample and several complete cross-scenario / appearance-specific cases remain open below. A passing subcheck or a blocked sample is not a passing batch.

The tested code is `796809d`, on top of local integration `01272acc71d3d85a6b4bf68136a4f558ea1cc1c6`; the local integration tip was merged and was already an ancestor. The production host fix is `afe67bd`; the next commit adds acceptance tests only, so the native IME, DPI and clipboard executables exercise the same production and host-verification code. This report and evidence are in the following evidence-only commit. Windows 11 Pro N 26H2 build 26300.9711; clang-cl 19.1.5; NVIDIA GeForce RTX 3060, driver 610.88, OpenGL 3.3.0. Microsoft Bopomofo: IMTCTIP.DLL 6.2.26100.9711, ChtIME.exe 6.2.26100.9278. Per-monitor-aware enumeration returned DISPLAY1 144 DPI (150%, 1920×1080 physical pixels) and DISPLAY2 120 DPI (125%, 1080×1920 physical pixels). The latter is **not** 100%.

## Review-fix verification (2026-11-06)

**Result: NOT COMPLETED.** The final production/test build in this worktree is `57bf179`; this evidence is committed afterward. The local integration tip was merged and was already an ancestor. The sections below headed as the original batch acceptance remain historical and describe the earlier tested revision, not the current code.

| Finding | Status | Current evidence / limitation |
| --- | --- | --- |
| S-H1 | **Open** | Button removal still uses tombstones and does not reclaim its structural entries. Bounded remove/reset cycles, safe-point reclamation, and stale identity on ID reuse remain required. |
| S-H2 | Fixed for reviewed wording/dimensions | `ComponentPresentation` is supplied by the appearance; a custom appearance test changes disabled wording and radio geometry. Further visual structure remains in the common component implementation. |
| C-1 | Fixed | Clipboard open retries use a 250 ms elapsed-time deadline. |
| C-2 | Fixed | Clipboard reads return distinct `failed` and `not_supported` statuses; the core regression checks unchanged text on failure. |
| C-3 | Fixed | The native verifier refuses to overwrite the clipboard if its initial read failed, and distinguishes that from no text saved. Clipboard restoration covers Unicode text only, not other formats. |
| P-A1 | Fixed | Checkbox conflict notice and use-latest behavior are tested during a rejected confirmation; radio and dropdown notices plus their visible latest actions are tested with divergent source updates under both appearances. |
| P-A2 / P-C1 | Fixed | V03 tooltip, dialog title, body, and two buttons use the specified text; the assembled-form test checks them. |
| P-B1 / P-B2 | Fixed | Removed the V01 opener and dialog test button; IME dialog opening and temporary-source removal are driven via the host/test seam. |
| P-B3 | **Open** | `ButtonHandle::remove` is retained as requested, but S-H1 is not fixed. `TabsHandle::add_button` remains a button-specific API for the replacement fixture; its general design or relocation to the seam is not resolved. |
| P-C2 | Fixed | Both cancel and confirm after source removal return focus to the permanent dialog opener. |
| P-C3 | Fixed | The P05 regression clicks the source, leaves it, and checks the tooltip closes while focus remains; keyboard focus makes it eligible again. |
| P-C4 | Fixed | Out-of-range public slider edits return `invalid_value`; invalid range/step returns a query result; disabled list items return `disabled`. Pointer and key clamping remain. |
| P-C5 | Fixed for observed platform | Both appearances passed real Bopomofo close tests. The close trace records `host:complete-request`, `wm:ime-result`, then `wm:ime-end`; one undo entry is retained. Other IMEs may decline completion and then the existing fallback cancels preedit. [Events](review-fixes-final-ime/ime-events.log), [results](review-fixes-final-ime/ime.tsv). |
| X02 | Partial | All 24 page x size x forced-scale x appearance samples passed visible overlap/width and reveal/hit checks. Popup/scroll interactions have other constituent checks but were not crossed with every sample. |
| X04 | **Open** | Existing visible/semantic comparisons and refusal checks pass, but the complete applicable state-by-control comparison has not been established. |
| D01 | Partial | [12/12](review-fixes-final-dpi/dpi.tsv) available 150% <-> 125% directional samples passed for selection/focus, physical caret hit, composition/IME caret and candidate-position request, and popup bounds/hit. The required 100% <-> 150% sample and actual candidate-window visual inspection remain open. |

Final Debug desktop CTest **7/7**, Release desktop **7/7** including **86/86** host checks, and Release core-only **6/6**. Core: components **62/62**, layout **22/22**, list-page **6/6**, showcase **36/36** for each appearance and each ID set (144 executions, 952 cumulative assertions). [Debug](review-fixes-final-ctest/debug.log), [Release](review-fixes-final-ctest/release.log), [core](review-fixes-final-ctest/core.log). Final real IME **46/46**. Trailing whitespace was removed from saved CTest and IME event logs without changing outcomes. Clipboard solo stress: [run 1](review-fixes-clipboard-1/clipboard.tsv) **400/400** and [run 2](review-fixes-clipboard-2/clipboard.tsv) **400/400**, each with both appearances and 100 repetitions of ordinary and forced 60 ms contention cases. The pre-fix [solo baseline](review-fixes-clipboard-baseline/clipboard.tsv) also passed **400/400**. The old 2/400 failure was not reproduced by the pre-fix 400/400 solo baseline; the ambiguous read and retry timing are plausible contributors but these observations do not establish the cause.

Judgement-call review: the refused-bequest inheritance for radio/list/dropdown was removed; they now derive directly from the component type. The other proposed refactorings remain open: shared `Source<T>`/`Change<T>`, handle base, registry dispatch for `pointer_up`/`describe`, error-line consolidation, naming changes, and removing duplicated checkbox kind. These span public types or dispatch and were not safely completed and reverified in this pass. The appearance change extracts reviewed text and dimensions, but does not claim a complete independent component-layout system.

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

## Not completed (current review-fix build)

1. **S-H1 and P-B3:** Implement safe-point structural removal, bounded storage and stale-handle identity without per-removed-component tombstones; test many remove/reset cycles and reentrant callbacks. Resolve the button-specific tab insertion API as a general capability or move it to the test seam.
2. **D01:** In Windows Settings > System > Display, select one of the first two horizontally ordered displays and set Scale to 100%; leave the other at 150%. Confirm the applied values, close Settings, stop interacting, and run `build/Release/lcars_host_verify.exe --dpi --evidence DIR`. For both appearances inspect the candidate window itself while the verifier moves the composing field in each direction; check that the real candidate follows the caret. Record the OS and IME versions, enumerated DPI, all results, and restore the previous display scale. The automated candidate-position request is not visual confirmation.
3. **X02 / X04:** Cross popup and scroll interactions with the full page/size/scale matrix and complete the visible-versus-semantic restricted-state comparisons for every applicable control. The matrix above is a bounded subset.
4. **Clipboard diagnosis:** Two earlier intermittent solo failures have no demonstrated cause. A passing stress run and a bounded retry do not prove why they occurred or guarantee success during longer clipboard holds.
5. **User trial U01 and A01:** No user trial feedback was supplied; UIA/screen-reader and MCP adapters remain separate work.
6. **Deferred design smells:** The public type and dispatch refactors listed above remain for a focused change with appropriate regression coverage.
