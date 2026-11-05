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
