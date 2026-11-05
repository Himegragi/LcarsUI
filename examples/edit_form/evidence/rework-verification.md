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
