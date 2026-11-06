# V03 tooltip - issue #24 (2026-11-06)

Production and tests: `330dcda`, based on local integration `1c67d42`; the local integration tip was merged and already an ancestor. V03 attaches the explanatory tooltip to the typed 50% button handle. The application still owns progress and the same application logic is used by both appearances.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 61/61, layout 21/21, form 24/24 for each appearance and ID set (676 cumulative checks), list 6/6 |
| Release desktop build and CTest | 7/7 suites passed, 0 failed; 62/62 native checks | `build.ps1 -Configuration Release`; [host results](ticket-24-host/host.tsv) |
| P05, P06, P08 and R08 | Passed for both appearances | Public handles verify configurable delay, Escape, pointer and focus eligibility, source removal, modal and page reset. Native message tests cover timing, focus, Space, and 960?720 / 640?480 DIP at forced 100% / 150%. [Web tooltip](ticket-24-host/web-tooltip-640-150.bmp), [Material tooltip](ticket-24-host/material-tooltip-640-150.bmp), [Web dialog](ticket-24-host/web-dialog-640-150.bmp), [Material dialog](ticket-24-host/material-dialog-640-150.bmp). |
| Clipboard stress | 400/400 passed, 0 failed | [results](ticket-24-clipboard/clipboard.tsv), 100 repetitions per appearance, including the 60 ms forced-contention case |
| Real Microsoft Bopomofo, one run | 44/44 passed, 0 failed or blocked | [results](ticket-24-ime/ime.tsv), [events](ticket-24-ime/ime-events.log). Physical pointer dwell/leave and an actual foreground switch away and back confirmed P05/R08 for each appearance. The IME composition, page and dialog cases also ran; the run was not interrupted. |

The TDD keyboard and layout cases initially failed for missing functionality; a modal test exposed a visible tooltip surviving modal activation and was fixed. An intermediate native test posted pointer movement to a background window and failed because real Windows mouse-leave tracking cleared that artificial hover. The final message test records its narrower scope; physical dwell and leave were checked in the real-input run. Intermediate failures are not counted as final passes.

Environment: Windows build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Forced scaling is layout evidence, not a real cross-monitor DPI migration. Real cross-monitor DPI migration, UIA/MCP, user trial, and the remaining E01 matrix remain open.

---

# V03 modal dialog - issue #23 (2026-11-05)

Production: `a946f24`, based on integration tip `9179e12`; native matrix and documentation: `f2a67a3`. The integration tip was merged locally and was already an ancestor. V03 owns the progress and result; confirmation sets progress to 0 and increments the count once. Cancel leaves progress unchanged. The temporary opener can be removed from inside the dialog; reset recreates it and keeps the old handle invalid.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 61/61 cases, layout 21/21; edit form 19/19 for each appearance and ID set (568 cumulative checks) |
| Release desktop build and CTest | 7/7 suites passed, 0 failed; 60/60 native checks | `build.ps1 -Configuration Release`; [host results](ticket-23-host/host.tsv) |
| P01–P04, both appearances | Passed in public-handle cases and native input checks | P01 cancel and Escape; P02 keyboard confirmation and held Enter; P03 Tab/Shift+Tab, blocked public operations, page changes, mask and pointer; P04 confirm and cancel after temporary opener removal, plus reset and recreation. The host checks dialog buttons and mask at 960×720 and 640×480 DIP at forced 100% and 150%. [Web dialog](ticket-23-host/web-dialog-640-150.bmp), [Material dialog](ticket-23-host/material-dialog-640-150.bmp). |
| Clipboard stress, both appearances | 400/400 passed, 0 failed | [results](ticket-23-clipboard/clipboard.tsv), 100 repetitions per appearance, including 60 ms forced contention |
| Real Microsoft Bopomofo, one run | 40/40 passed, 0 failed or blocked | [results](ticket-23-ime/ime.tsv), [events](ticket-23-ime/ime-events.log). A real pointer click on the V01 dialog opener during composition committed `我` once as one undo entry, moved focus to Cancel, and cancellation restored the opener in each appearance; progress and confirmation count did not change. |

The P01 test first failed to compile because the dialog was absent; P02/P03 then failed for the absent overlay. The reset-after-removal case later failed until the temporary entry could be recreated. An earlier clipboard stress run was intentionally stopped after the reset gap was discovered; its [partial record](ticket-23-clipboard-interrupted.tsv) contains 135 passing rows and is not counted as a complete run. The IME run was not interrupted or repeated.

Environment: Windows build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Forced scale is layout evidence, not a real cross-monitor DPI migration. Real cross-monitor DPI migration, UIA/MCP, user trial, and the remaining E01 matrix remain open. The removed button's identity is retained as a tombstone until UI destruction so stale handles cannot revive; repeated remove/reset cycles therefore retain those small identity records. Host and IME event-log trailing whitespace was normalized without changing outcomes.

---
# V03 progress - issue #22 (2026-11-05)

Production: `91b654d`, based on local integration `c1d1e6c`; merging the local integration tip reported already up to date. The subsequent evidence commit adds only the extra page-isolation assertions and this record. V03 owns the source in the application, initially 50, and connects it to a read-only typed progress handle. The displayed percentage is clamped to 0–100, while the source label and application model retain the original value. The display does not infer a business result from 100%. The dialog and tooltip remain for #23 and #24.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 61/61 cases; edit form 15/15 cases for each appearance and ID set (508 cumulative checks after the added isolation assertions) |
| Release desktop build and CTest | 7/7 suites passed, 0 failed; 54/54 native checks | `build.ps1 -Configuration Release`; [host results](ticket-22-host/host.tsv) |
| P07, both appearances | Passed for 0, 50, 100, -10 and 110 | Public handle and frame checks run in all four appearance/ID combinations; native keyboard activation checks source and clamped semantic values against painted track pixels. [Web 50%](ticket-22-host/web-v03-50.bmp), [Material 50%](ticket-22-host/material-v03-50.bmp), [Web 110 source](ticket-22-host/web-v03-p07.bmp), [Material 110 source](ticket-22-host/material-v03-p07.bmp). An external source notification also triggered repaint. |
| Clipboard stress, both appearances | 400/400 passed, 0 failed | [results](ticket-22-clipboard/clipboard.tsv); 100 repetitions per appearance, including the 60 ms forced-contention case |
| Real Microsoft Bopomofo, one run | 38/38 passed, 0 failed or blocked | [results](ticket-22-ime/ime.tsv), [events](ticket-22-ime/ime-events.log); real foreground input, including the composition/page-switch path in each appearance |

The initial P07 test failed to compile before the component existed. A later source-lifecycle test failed because the first implementation did not identify progress as read-only; the implementation was corrected before the passing runs. The tests also check expiry and subscription disconnection, V03 reset to 50%, progress retention across page switches, and preservation of V01 draft and V02 search/selection after reset. The final two isolation checks were added after the desktop, clipboard and IME runs; they changed no production or native-host code, and core-only CTest was rerun afterwards.

Environment: Windows build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Host layout checks include 960×720 and 640×480 DIP at forced 100% and 150%; the retained V03 images are 960×720 at forced 100%. Forced scaling does not establish real cross-monitor DPI behavior. Real cross-monitor DPI migration, UIA/MCP, user trial, the remaining E01 matrix, and the later V03 dialog and tooltip scenarios remain open. Host and Host and IME event-log trailing whitespace was normalized without changing the outcomes.

---

# Integrated V02 and nested scrolling - issues #20 and #21 (2026-11-05)

Production and tests: `9224592`, based on local integration `10bf9ff`. V02 is the same page, model and list code in both the standalone example and the tabbed showcase. V03 remains a placeholder. Applications drive the page through typed handles and data connections; native input uses the host interface. Both appearances use the same scenarios.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 6/6 suites passed | `build.ps1 -Configuration Release -CoreOnly`; components 60/60 cases, layout 21/21, list page 6/6; V01/V02 cases run for both appearances and two ID sets |
| Release desktop build and CTest | 7/7 suites passed; 52/52 native checks | `build.ps1 -Configuration Release`; [host results](ticket-20-21-host/host.tsv), [Web V02](ticket-20-21-host/web-v02.bmp), [Material V02](ticket-20-21-host/material-v02.bmp) |
| Clipboard stress on final production | 400/400 passed, 0 failed | [results](ticket-20-21-clipboard/clipboard.tsv), 100 repetitions per appearance, including 60 ms forced contention |
| Real Microsoft Bopomofo, one run | 38/38 passed, 0 failed or blocked | [results](ticket-20-21-ime/ime.tsv), [events](ticket-20-21-ime/ime-events.log). This was run on `a65136b`, before the later R07 layout-measurement fix in `9224592`; the final binary has not had a second real-IME run. |

L07 and L08 use the real search and T006 selection alongside the unapplied V01 draft and result. R07 checks the public rendered field offset, undo history and vertical offset after a real V02 reset; both inner and outer V02 offsets and the T100 selection survive page switching and reset only with V02. The public-frame R07 test first failed in all four appearance/ID combinations: provisional measurement at a wider width clamped a saved horizontal offset. The subsequent fix prevents measurement probes from committing view state. B03/R02 tests reveal the last list row in both viewports on the first frame, check combined clipping, and use native End, the inner scrollbar and wheel; native wheel at the inner limit reaches the outer viewport. The one real-IME run clicked the tab during active composition and observed one commit, one undo entry, no Apply and a switch to the real V02 page, for both appearances.

A desktop build ran while a shared header was being edited and its native test crashed; that run is not counted as a pass, and its cause was not established. The next completed native run passed 46/50 checks: its four R06 failures were caused by the test posting a synthetic wheel message at screen `(0, 0)`, outside the test window after wheel routing became location-sensitive. [Those results](ticket-20-21-host-initial/host.tsv) are retained. The test was corrected to post an in-window screen position; the final native run passed. An intermediate core run also exposed the R07 issue described above; the final core run passed.

With no other agent running, the orchestrator's pre-work `10bf9ff` clipboard stress passed 400/400: [baseline TSV](ticket-20-21-clipboard-baseline/clipboard.tsv). The earlier #20 387/400 and #21 383/400 runs occurred while both agents ran native tests concurrently against the shared Windows clipboard. That concurrency is a likely cause of those failures, not a proven diagnosis. This work's first solo stress on `a65136b` also passed 400/400: [results](ticket-20-21-clipboard-before/clipboard.tsv). No clipboard implementation change was made. Trailing whitespace in the host and IME event logs was normalized without changing the recorded outcomes.

Environment: Windows 11 Pro N build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. The retained V02 images are 960x720 DIP at forced 100% offscreen rendering; native layout also covers 640x480 and forced 100%/150%. Forced scaling is not a real cross-monitor DPI migration. Real IME on the final layout-fix build remains unverified because only one run was authorized. Real cross-monitor DPI, UIA/MCP, user trial and the rest of the E01 matrix beyond this slice remain open; V03 is still a placeholder.

---

# Tabs and page scopes - issue #21 (2026-11-05)

After the runs below, the local integration tip advanced to `8c5dd45` with #20. Its list and this ticket's tabs were merged, retaining the V02 placeholder in the three-page example and preserving the standalone list example. On the merged tree, Release core-only CTest passed 6/6 suites (components 60/60, layout 20/20, list page 6/6) and Release desktop CTest passed 7/7 suites with [50/50 native checks](ticket-21-merged-host/host.tsv). A combined test verifies that a list member refuses selection and focus on a hidden page, accepts selection on its active page, and retains selection when leaving. The one permitted real-IME run and clipboard stress below were performed before this merge, against `6342f07`; they were not repeated on the merged build. The merge includes changes to the desktop host for list interaction, so the merged binary's real-IME page-switch path remains unverified.

Production and tests: `6342f07`, based on `integration/first-component-batch` at `067bf41`; merging that local tip before verification reported already up to date. The evidence-only commit follows. V01 is unchanged when used alone; the desktop example enables the three-page assembly. Public handles and data connections drive application behavior; native event and geometry tests use the internal host seam. Both appearances run the same scenarios.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only CTest | 5/5 suites passed | `build.ps1 -Configuration Release -CoreOnly`; components 55/55 cases and 239 checks, layout 18/18 cases and 111 checks; form 14/14 cases under each of two appearances and two ID sets (412 cumulative checks) |
| Release desktop CTest, initial | 5/6 suites passed; native 42/44 checks passed | [host results](ticket-21-host-initial/host.tsv): one Web copy and one Material paste check failed. Other native checks, including C09 in both appearances, passed. |
| Release desktop CTest, retry | 6/6 suites passed; native 44/44 checks passed | [host results](ticket-21-host-retry/host.tsv); same binary, rerun after the clipboard stress and IME checks. This does not erase the initial failures. |
| Clipboard stress | 383/400 passed, 17 failed | [results](ticket-21-clipboard/clipboard.tsv), 100 repetitions per appearance: ordinary copy/paste 194/200; forced contention 189/200. No clipboard fix was made in this ticket. |
| Real Microsoft Bopomofo | 38/38 passed, 0 blocked or failed | [results](ticket-21-ime/ime.tsv), [events](ticket-21-ime/ime-events.log). One foreground-input run, including a real tab click during composition for each appearance. The preedit committed once, focus moved to the tabs, the page switched, the undo depth was one, and Apply did not run. |

C09 coverage includes manual keyboard activation, disabled-page pointer and programmatic rejection, modal admission, hidden controls including disabled ones, fixed tab geometry, and independent page state. G11 is exercised for text and mixed confirmation groups across pages, including first-error page activation, focus and first-frame scroll reveal, and a visible/semantic marker for errors on other pages. New tests were observed red before their corresponding implementations; a disabled hidden-control rendering case and nested page-change callback case were also observed red before correction. L07/L08/R07 use the V02 placeholder search draft and a sample-selection flag, together with V01 draft, applied value, undo history and horizontal/vertical offsets. The list from #20 now exists as a standalone example; wiring it into the three-page showcase and verifying actual T006 selection, filtering, list data, and scrollbar behavior across page switches remain open. V03 is a placeholder.

Windows 11 Pro N build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. All project C++ translation units in the Release compile databases use C++20: 10/10 core-only and 16/16 desktop. The native layout matrix covers 960x720 and 640x480 DIP at forced 100% and 150%; forced scaling is not a real cross-monitor DPI migration. Native-run bitmaps were temporary outputs and are not retained here. User trial, UIA, MCP, actual cross-monitor DPI migration, and the portions delegated to #20 were not verified. Confirmation callbacks remain synchronous. Trailing whitespace in the saved logs was normalized without changing outcomes.
# List and scrollbar verification - issue #20 (2026-11-05)

Production and example: commits `83af9df` and `1235824`, based on local integration `067bf41b8af2ec0b16a9acf7d70bf825232b4505`; merging that tip reported already up to date. The last native check below was added after the single IME and clipboard runs; no production code changed afterward. This record follows in a separate commit. V02 is a standalone example pending #21; the two executables share the page, model and list behavior.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 55/55 cases, 233 checks; layout 18/18, 111 checks; V02 page 6/6, 20 checks |
| Release desktop build and CTest, before final native check | 7/7 suites passed, 0 failed | `build.ps1 -Configuration Release`; [46 native checks](ticket-20-host/host.tsv) passed |
| Release desktop build and CTest, final native check | 6/7 suites passed, 1 failed | [46/48 native checks passed](ticket-20-host/host-latest.tsv); two forced-contention clipboard checks failed, one in each appearance. All six new list-native checks passed. |
| Real Microsoft Bopomofo, one run | 36/36 passed, 0 failed or blocked | [IME results](ticket-20-ime/ime.tsv), [events](ticket-20-ime/ime-events.log); foreground SendInput |
| Clipboard stress, one complete run | 387/400 passed, 13 failed | [results](ticket-20-clipboard/clipboard.tsv); 100 repetitions per appearance. Seven failures were forced-contention checks; six were ordinary copy/paste checks. |

The latest native run's forced-contention failures both reported paste failed after a 60 ms clipboard hold. In the stress run, failures included failed fixture writes or copy, unavailable or unexpected clipboard contents, and failed paste; see the TSV for each repetition. These results do not identify the cause. The previously reported intermittent contention problem remains open, and this run also observed failures in the ordinary scenario. No clipboard changes were made. The earlier fully passing desktop run is preserved rather than replacing the later failure.

V02 tests cover empty and one-item focus/exit, T003/T004/Home/End, duplicate T005/T006 identities through sorting, filtering and clearing, deletion and reentry, T008 disable/selection recovery, and R11's T009-to-T010 old-order recovery. List layout tests exercise full row reveal and clipped hits for both appearances; native Win32 messages exercise End at 640x480, wheel, actual thumb drag to T100, clicking T100 after entering from another control, shrinking content, and the disabled T008 pointer/keyboard path in both appearances. The N06 layout case combines rows, columns and grid with long text, a narrow width and two text sizes. The V02 behavior cases use the public handles; host input and frame geometry use the internal host seam.

Windows 11 Pro N build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Project C++ translation units in the Release compile databases specify C++20: 12/12 core-only and 20/20 desktop. The retained [Web](ticket-20-host/web-960x720-100-initial.bmp) and [Material](ticket-20-host/material-960x720-100-initial.bmp) images are V01 host images, not V02 screenshots. V02 has geometry and native interaction evidence but no retained representative screenshot. Forced 100%/150% rendering is not a real cross-monitor DPI migration. User trial, UIA, MCP and cross-page behavior were not run. The existing layout supports one scroll region per frame; nested inner/outer scroll coordination in the parent specification remains unimplemented and unverified.

---

# Dropdown and overlay verification - issue #19 (2026-11-05)

Production and tests: commit `d1e32d89d440128703799da78e3f336281f840d2`, based on local integration tip `e43966dc300b6733a326f07bd276ea199cb3d3db`. Merging that tip reported already up to date. The following evidence-only commit records these runs. The form uses typed handles and an application-owned region source in the same confirmation group as name, notices, mode and volume. Both appearances run the same application and cases; native input uses the host interface.

| Check | Result | Evidence |
| --- | --- | --- |
| Release core-only build and CTest | 5/5 suites passed, 0 failed | `build.ps1 -Configuration Release -CoreOnly`; components 51/51 cases and 225 checks, layout 16/16 cases and 103 checks |
| Release desktop build and CTest | 6/6 suites passed, 0 failed | `build.ps1 -Configuration Release`, including independent consumer and native host |
| Native host, both appearances | 42/42 checks passed, 0 failed | [host results](ticket-19-host/host.tsv), including native F07 candidate, Escape, Tab, outside action and popup click; [Web](ticket-19-host/web-960x720-100-initial.bmp), [Material](ticket-19-host/material-960x720-100-initial.bmp) |
| Real Microsoft Bopomofo, one run | 36/36 checks passed, 0 failed or blocked | [IME results](ticket-19-ime/ime.tsv), [events](ticket-19-ime/ime-events.log); real foreground `SendInput` |
| Clipboard stress, first complete run | 398/400 checks passed, 2 failed | [results](ticket-19-clipboard/clipboard.tsv), 100 repetitions per appearance including 60 ms forced contention |
| Clipboard stress, one repeat | 400/400 checks passed, 0 failed | [repeat results](ticket-19-clipboard-retry/clipboard.tsv), same binary and repetitions |

The first complete clipboard run failed forced-contention checks at Web repetitions 14 and 31: copy failed in both and paste also failed at repetition 14. The test's output does not distinguish failure to open the clipboard from an unexpected value, so the cause is unconfirmed. The passing repeat does not erase the intermittent failure; reliable behavior under contention remains open. An earlier partial stress run was stopped before completion to rebuild a dropdown repaint fix and is not counted. The first desktop CTest run also failed the existing pointer-activation check in both appearances because the added form row placed Apply below the fixture's viewport. The test was changed to reveal Apply before clicking; the subsequent complete desktop run passed.

The form suite has 13 cases run with both appearances and two ID sets (52 executions, 368 cumulative checks). F01 submits all five drafts together; F08 and R04 check missing region, field-specific errors and name-first focus. F07 is exercised through native keyboard and pointer events. Component and layout cases cover cancellation, empty and restored options, disabled/modal admission, identity-preserving pointer presses, non-recursive notifications, programmatic repaint, stack hit order, edge placement, and readable empty/disabled states. The candidate, popup geometry, form connection, readable states, non-recursive notification and repaint tests were observed failing before their respective fixes.

Windows 11 Pro N build 26300.9711 (26H2), clang-cl 19.1.5, NVIDIA RTX 3060 / OpenGL 3.3, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 and ChtIME.exe 6.2.26100.9278. Native layout covers 960x720 and 640x480 DIP at forced 100% and 150%; forced scale is not a real cross-monitor DPI migration. Only the two representative initial bitmaps were retained from the native run; other bitmap names in its TSV were temporary outputs. Trailing whitespace in the host and IME event logs was normalized. User trial, UIA, MCP, full cross-page behavior and actual cross-monitor DPI migration were not run. Confirmation callbacks remain synchronous, as in the previous slice.

---

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
