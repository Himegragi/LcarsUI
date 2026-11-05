# S1 public-interface rework verification

Status: **incomplete**, not ready to merge. Results below were run on code commit `df93733` on 2026-11-05; this evidence is recorded separately from the original S1 results in [verification.md](verification.md).

The legacy `Id = std::string` and string lookup, operation, and binding methods remain publicly exposed for the existing host, layout, settings application, and regression suites. The form and the independent consumer use the new handles for their field and application connections, but the public surface as a whole still conflicts with G10 and ADR 0005. Ungrouped connections support explicitly selected immediate or confirmed writes. Confirmation callbacks are synchronous; the execution test exercises re-entry and edits made during the callback, not a separate asynchronous scheduler. These are implementation gaps, not passing acceptance claims.

Implemented flow:

```text
Application source -> field handle -> draft -> confirm group -> one application write entry
                                     |                |-> rejection: retain draft, show errors, focus first
                                     |-> source changes: retain draft -> use latest or submit with origin
                                     |-> accepted: keep field history; undo makes a new draft
```

The source must outlive the UI connection and deliver notifications on the UI thread. Destruction of the UI disconnects the source. The framework does not own or roll back application data.

| Verification | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 5/5 suites passed; 0 failed | `build.ps1 -Configuration Release -CoreOnly`; no desktop, OpenGL or font backend |
| Release desktop build and CTest | Initial 5/6 suites; retry 6/6 | `build.ps1 -Configuration Release`; initial failure was the Web native clipboard check, detailed below. Retry ran CTest without rebuilding. |
| Native host | Initial 31/32, then two runs 32/32 | [failure](rework-host-intermittent-failure.tsv), [passing CTest retry](rework-host.tsv); two appearances, real Win32 dispatch and offscreen layout |
| Real Microsoft Bopomofo input | First 33 passed, 1 blocked; retry 36/36 passed | [interrupted run](rework-ime-interrupted.tsv), [events](rework-ime-interrupted-events.log); [passing retry](rework-ime.tsv), [events](rework-ime-events.log); real `SendInput` |
| Representative initial windows | Captured | [Web](rework-web-960x720-initial.png), [Material](rework-material-960x720-initial.png); screenshots do not replace behavior checks |

The components suite has 27 passing cases; the form suite has 8 cases run with two appearances and two sets of application IDs (32 executions). Both CMake configurations compiled all project translation units in required C++20 mode: 19/19 desktop and 10/10 core-only contained `-std:c++20` in `compile_commands.json`. Compiler: clang-cl 19.1.5; CMake 3.31.6-msvc6. OS: Windows build 26300.9711; NVIDIA RTX 3060, OpenGL 3.3, driver 610.88; Microsoft Bopomofo `IMTCTIP.DLL` 6.2.26100.9711 and `ChtIME.exe` 6.2.26100.9278. Forced 150% layout is not a real cross-monitor DPI test.

On the first final-code desktop run, Web `AC08-pointer-clipboard` could not read the expected copied text and the paste result was wrong. The subsequent standalone native run and complete CTest retry passed. The cause has not been established; the failure is preserved above and should not be treated as resolved. On the first final-code IME run, the verifier stopped when foreground focus moved away; the retry completed.

The prior CTest, platform, and screenshot evidence remains historical and was not counted as a new run. The README-listed core, consumer, native-host and real-IME verifications all ran. User trial, full cross-page behavior, UIA, and real cross-monitor DPI remain outside these results. No spec or ADR contradiction was resolved by overriding a documented decision; the unfinished items above remain open.
