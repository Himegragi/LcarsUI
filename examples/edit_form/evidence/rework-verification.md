# Volume slider verification - issue #18 (2026-11-05)

Production and test code: commit `4a0545982f62b8e553884f3d8a2685a875a9f243`, based on local integration tip `e2c3cf05ce7904b8f8a1bdb6c977db81fd3ea3bf`; merging that tip reported already up to date. The following evidence-only commit records these runs. The form and public driver use typed handles and connections; native input and geometry use the internal host interface. Both appearances run the same cases and application logic.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 5/5 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 47/47 cases, 209 checks; layout 13/13 cases, 87 checks |
| Release desktop build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release`; includes native host and independent consumer |
| Native host, both appearances | 40/40 checks passed, 0 failed | [host results](ticket-18-host/host.tsv), including slider arrow, Home/End, real Win32 drag, apply and disabled admission; [Web](ticket-18-host/web-960x720-100-initial.bmp), [Material](ticket-18-host/material-960x720-100-initial.bmp) |
| Clipboard stress, both appearances | 400/400 checks passed, 0 failed | [clipboard results](ticket-18-clipboard/clipboard.tsv), 100 repetitions per appearance, including forced contention |
| Real Microsoft Bopomofo, first attempt | 27 passed, 1 blocked | [IME results](ticket-18-ime/ime.tsv), [events](ticket-18-ime/ime-events.log); Web completed, Material stopped when foreground moved away |
| Real Microsoft Bopomofo, retry | 6 passed, 2 blocked | [IME results](ticket-18-ime-retry/ime.tsv), [events](ticket-18-ime-retry/ime-events.log); both appearances stopped when foreground moved away |

The real-IME verifier records `BLOCKED` rows as `FAIL` in its TSV and returns a nonzero exit code. No input was sent after losing the test window. Material's full IME sequence remains unverified; the retry does not turn either blocked run into a pass. The verifier used real foreground input, not simulated model updates.

The form suite contains 11 cases executed for both appearances and two ID sets (44 executions, 328 checks). Numeric coverage includes group submission and rejection, an edit during submission, source conflict and latest value, disconnection and expired handles. N04 covers range -3 to 8 with step 4, upward midpoint ties, the shorter final interval, both endpoints, and invalid configurations. The layout test checks shared slider track/hit geometry and dragging for both appearances. The independent consumer checks incompatible typed sources and the lack of string-ID application operations. The initial tests for numeric connection, non-divisible keyboard steps, form integration, track geometry, source recovery, conflict action and missing range were observed failing before their fixes.

Windows 11 Pro N build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. All project C++ translation units in the Release compile databases specify C++20: 10/10 core-only and 16/16 desktop. Native layout covers 960x720 and 640x480 DIP at forced 100% and 150%; forced scale is not a real cross-monitor DPI migration. Only the two representative initial bitmaps were retained from the native run; other bitmap names in its TSV were temporary outputs. User trial, UIA, MCP, full cross-page behavior and actual cross-monitor DPI migration were not run. Confirmation callbacks remain synchronous, as in the prior slice.

---

# Checkbox and radio verification - issue #17 (2026-11-05)

Production and test code: commit `b6708843a70009ba7d31d2fea1d6b6ddcabcd2bc`, based on and merged with local integration tip `c8a58c0ca2c9463a63efceb5dd8d1fa883fd3ce7` (already an ancestor). The evidence-only commit follows it. The form uses typed handles and connected application-owned values; the native tests use the internal host seam for real input and geometry. Both appearances run the same form and cases.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 5/5 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 42/42 cases and 189 checks, including typed connections, mixed rejection and N03 |
| Release desktop build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release`; includes public consumer and native host |
| Native host, both appearances | 38/38 checks passed, 0 failed | [host results](ticket-17-host/host.tsv); F09 checkbox Space/click, radio arrow/click, disabled option and apply; [Web](ticket-17-host/web-initial.bmp), [Material](ticket-17-host/material-initial.bmp) |
| Clipboard stress, both appearances | 400/400 checks passed, 0 failed | [clipboard results](ticket-17-clipboard/clipboard.tsv), 100 repetitions per appearance including forced contention |
| Real Microsoft Bopomofo, both appearances | 36/36 checks passed, 0 blocked or failed | [IME results](ticket-17-ime/ime.tsv), [events](ticket-17-ime/ime-events.log); real foreground `SendInput` |

The form suite has nine cases executed for each of two appearances and two ID sets (36 case executions, 304 checks). Radio tests include initially empty selection, disabled first/last options, an all-disabled group that can be exited, boundary navigation, and pointer hit geometry. Mixed confirmation tests cover a single batch, retained drafts, per-field errors and first-error focus. The independent consumer has compile-time checks that boolean, option and text source types do not implicitly convert. First tests for checkbox, radio, mixed confirmation, pointer geometry, form integration, source conflicts and available semantic actions were observed failing before the respective implementation. Earlier intermediate runs also caught the changed native focus order and incorrect Chinese source encoding; both were corrected before the recorded runs.

All project C++ translation units in the compile databases use C++20: 10/10 core-only and 16/16 desktop. Environment: Windows build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060/OpenGL 3.3; Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Trailing whitespace in the saved text evidence was normalized; outcomes and event text were retained. The 150% render is forced scale, not a real cross-monitor DPI migration. User trial, UIA, MCP, full cross-page behavior and actual cross-monitor DPI migration were not run; they remain outside these results. Confirmation callbacks are synchronous, as in S1; full asynchronous command handling remains outside this slice. F09's slider portion belongs to a later ticket and is not claimed here.

---

# Component type dispatch verification - issue #16 (2026-11-05)

The production code tested is commit `0df8e06`, based on local integration tip `f0561f018dda303d69eceda567411a34d8bcf01f`. The evidence-only commit follows it. Component type registration is internal: it does not expose a new public plug-in API or change the application's typed handles and data connections. The test-only type supplies its own geometry, painted color, hit region and keyboard/pointer behavior. Both appearances exercise it through the existing layout and core seams and the real Win32 host; the host test rejects a click inside its bounds but outside its hit region, and checks one activation each from a click and Enter. Before the implementation, the test failed to compile because the dispatch seam did not exist; the distinct geometry/hit and paint extensions were also checked red before implementation.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 5/5 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`, including independent public consumer |
| Release desktop build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release`, including independent public consumer and native host |
| Native host, both appearances | 36/36 checks passed, 0 failed | [host results](ticket-16-host/host.tsv); includes two registered-component checks; [Web probe](ticket-16-host/web-registered-component.bmp), [Material probe](ticket-16-host/material-registered-component.bmp) |
| Clipboard stress, both appearances | 400/400 checks passed, 0 failed | [clipboard results](ticket-16-clipboard/clipboard.tsv); 100 repetitions per appearance, including forced contention |
| Real Microsoft Bopomofo, both appearances | 36/36 checks passed, 0 blocked or failed | [IME results](ticket-16-ime/ime.tsv), [events](ticket-16-ime/ime-events.log); foreground `SendInput` |

The unchanged V01 application uses the same handles, data connections and cases for both appearances. At 640x480 DIP and 100% forced scale, the initial Web and Material host BMPs are each byte-identical (SHA-256) to their respective [prior Web](review-fixes-host/web-640x480-100-initial.bmp) and [prior Material](review-fixes-host/material-640x480-100-initial.bmp) evidence. This is a comparison of those two initial views, not proof of all visual states. Trailing whitespace was removed from the retained host and IME event logs; their recorded outcomes and event text are unchanged. All project C++ translation units in the compile databases specified C++20: 10/10 core-only and 16/16 desktop.

Environment: Windows build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060/OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Forced 150% rendering is not a real cross-monitor DPI migration. User trial, UIA, MCP, full cross-page behavior and actual cross-monitor DPI migration were not run; they remain outside these results.

---

# S1 review-fix verification ? 2026-11-05

The final production binary was built from `415026e` on `s1/review-fixes`, after `3d0ada1`; the subsequent evidence-only commit records this run. The results below supersede the earlier rework record retained underneath. The form's application cases now use its publicly exposed handles; host input, focus traversal, clipboard setup and geometry checks still use the internal host seam. The old AC15 record overstated the earlier driver's public coverage; it is corrected in [verification.md](verification.md). Current AC15 passes within the S1 scope using the stated seams, not UIA or MCP.

| Check | Current result | Evidence |
| --- | --- | --- |
| Release core-only CTest | 5/5 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`, including independent consumer |
| Release desktop CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release`, including native host and consumer |
| Native host, both appearances | 34/34 checks passed, 0 failed | [host results](review-fixes-host/host.tsv); [Web](review-fixes-host/web-640x480-100-initial.bmp), [Material](review-fixes-host/material-640x480-100-initial.bmp) |
| Real Microsoft Bopomofo, both appearances | 36/36 checks passed, 0 blocked or failed | [IME results](review-fixes-ime/ime.tsv), [events](review-fixes-ime/ime-events.log) |
| Clipboard stress, both appearances | 400/400 checks passed, 0 failed | [clipboard results](review-fixes-clipboard/clipboard.tsv), 100 repetitions per appearance; binary from `4ea931d` |

The clipboard stress binary preceded only a shutdown test (`2f7988f`) and normalization of generic error messages (`415026e`); final native CTest and real IME were rerun after those changes.

The rebuilt project's compile databases specify C++20 for 9/9 core-only and 15/15 desktop project C++ translation units. Windows build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060/OpenGL 3.3; Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. The 150% render scale is not a real cross-monitor DPI migration. User trial, UIA, MCP, full cross-page behavior and real cross-monitor DPI remain outside these results.

## Review findings

- S-H1: handle queries carry an explicit status; S-H2: reset and label connection check UI-thread/configuration admission; S-H3: the settings MVP and build targets were removed, with the unchanged prototype at local `prototype/mvp-settings` (`e6d3fb3`); S-H4: the root README now illustrates handles.
- P-A1/P-A2: public text selection, copy, cut and redo and button/label semantic queries are available; P-A3: V01 cases use form handles for application behavior, with the host seam reserved as stated above; P-A4: layout nodes and public placement queries accept handles, and internal host lookup retains IDs; P-A5: semantic snapshots include source conflict.
- P-C1: text without a connected source and explicit write timing rejects edits and does not advertise edit operations; P-C2: group confirmation actions cannot be rebound; P-C3: the form explicitly reads its draft. Standalone `confirm()` shares the command guard in both directions. Ungrouped explicit-timing connections remain supported.
- Error results use field handles rather than vector positions; the form's duplicate validation text and order-dependent declaration indexing were removed. Standalone writes share a submission path, text connection logic was moved into UI state, and empty history has its own status. Lifetime checks remain at each non-owning handle entry point to preserve admission before dereferencing; a shared handle inheritance refactor was not needed for these fixes. Generic missing-target, wrong-thread and stopped messages were made consistent; specific failure messages retain their context.
- The pre-existing `Kind`/`Control` names, broader layout/desktop argument clumps, appearance `FieldParts`/`FieldLayout`, host message chains, repeated kind dispatch, and layout's view-offset and state-wording responsibilities remain. These are wider architectural changes or predate this slice; changing them was not required for the reviewed contracts.

Settings-only persistence, modal workflow, search and settings-history tests were dropped with that out-of-scope application; applicable core admission, UTF-16, composition and appearance geometry regressions remain in `core_regressions`, `components`, `layout` and native tests.

---

# S1 public-interface rework verification

Status: **verified** for the S1 scope on 2026-11-05. The code tested is commit `a6dc714`; `integration/first-component-batch` was already an ancestor when merged. The earlier [verification](verification.md) and [first rework record](rework-host-intermittent-failure.tsv) remain historical evidence.

Application-facing `lcars/core.hpp` exposes typed non-owning handles and data connections, not string-ID lookup, operation, or binding on `Ui`. Identity strings remain in component declarations, layout and semantic data. Host, layout, settings prototype and regression drivers use the separate `src/core_internal.hpp`, which is not exported as an include directory. The form uses a field-handle blur callback; the independent consumer includes public headers only and has separate compile-time negative checks for the removed operations. The compile check failed against the pre-change interface and passed after separation.

Ungrouped connections explicitly select immediate or confirmed writes. Confirmation callbacks execute synchronously; the existing execution test covers re-entry and edits during the callback. Full asynchronous commands are outside S1. The source must outlive its UI connection and deliver notifications on the UI thread; UI destruction disconnects it. The framework does not own or roll back application data.

| Verification | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 5/5 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; includes independent public-header consumer |
| Release desktop build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release`; native suite includes forced clipboard contention |
| Native host | 34/34 checks passed, 0 failed | [host results](rework2-host.tsv); both appearances, real Win32 dispatch and offscreen layout |
| Real Microsoft Bopomofo | 36/36 checks passed, 0 blocked or failed | [results](rework2-ime.tsv), [events](rework2-ime-events.log); real `SendInput` |
| Clipboard baseline | 153/200 passed, 47 failed | [before retry](rework-clipboard-before.tsv); 100 repetitions of the original case per appearance; unexpected clipboard contents were redacted |
| Clipboard after retry | 400/400 passed, 0 failed | [100 repetitions](rework-clipboard-after.tsv): original case and forced 60 ms hold per appearance; see logging note below |
| Final clipboard verifier | 40/40 passed, 0 failed | [10 repetitions](rework-clipboard-final.tsv) with corrected evidence formatting and explicit fixture-write and restoration checks |
| Representative initial windows | Captured | [Web](rework2-web-960x720-initial.png), [Material](rework2-material-960x720-initial.png); screenshots supplement behavior checks |

The first rework's isolated Web failure could not identify a cause. The repeat loop reproduced the same symptoms; a targeted 20-check diagnostic run saw six failures and recorded `OpenClipboard` error 5 (access denied) while Explorer processes held the clipboard. A deterministic real-window regression holding the clipboard for 60 ms failed for both appearances before the fix and passed after it. The host now retries opening it up to 40 times with 5 ms intervals, for a nominal maximum 195 ms of sleeping. If it remains unavailable beyond that bound, the operation still reports failure. The diagnostic instrumentation was removed.

The 400-check run's intermediate evidence formatter incorrectly printed `<unexpected>` for paste and undo even on passing rows; the actual pass condition compared both values to the expected fixtures. The subsequent 40-check run fixed the formatter and additionally made fixture write and clipboard-text restoration part of the pass condition. The post-fix loop does not prove immunity to every external clipboard manager or longer hold.

The components suite has 27 cases; the form suite has eight cases executed for two appearances and two ID sets (32 executions). All project C++ translation units in the Release compile databases specified C++20: 19/19 desktop and 10/10 core-only. Toolchain and environment: clang-cl 19.1.5, CMake 3.31.6-msvc6, Windows build 26300.9711, NVIDIA RTX 3060/OpenGL 3.3, Microsoft Bopomofo. Forced 150% layout is not a real cross-monitor DPI test. User trial, full cross-page behavior, UIA and real cross-monitor DPI remain outside these results.
