# Evidence gaps

These gaps are not evidence of a pass. They are accepted for v1 (spec 4 EG01: the list must exist; a listed gap does not block completion). Author-machine evidence: Windows 11 build 26300, NVIDIA RTX 3060, 150 % + 100 % displays, Microsoft Bopomofo; see [report.md](report.md).

| Gap | Decision | Available evidence | Unverified risk | How to close |
| --- | --- | --- | --- | --- |
| TD01 / spec 1 RF06: real TDR on the fixed build | User decision 2026-11-09: do not repeat the forced TDR on the author machine | A real TDR at `faf7785` showed the demo hung instead of prompting (the per-frame `glGetError` drain never ends after a context loss). Fixed in `dd33b85` with a `renderer_failure` test. The post-fix attempt crashed the machine (bugcheck 0x10E) and produced no result. `author-machine/manual/tdr/summary.md`. | The fixed build is verified only by the `context_lost` seam test; a real driver reset could still behave differently. | Force a TDR (`dxcap.exe -forcetdr`) on a spare machine or VM with GPU passthrough and confirm the Retry/Cancel prompt. |
| Windows 10 22H2 build 19045 | #11 Q5, Q9 | None; every author-machine run is Windows 11 build 26300 | Font, DirectWrite, TSF and UIA differences | Run the CI matrix and `verify-idle.ps1` on Windows 10. |
| Intel GPU | #11 Q9 | NVIDIA RTX 3060 and llvmpipe only | Driver-specific rendering and recovery | Run real-GPU comparison, `demo-perf` and recovery on Intel. |
| AMD GPU | #11 Q9 | NVIDIA RTX 3060 and llvmpipe only | Driver-specific rendering and recovery | Same as Intel, on AMD. |
| Real 4K display (spec 4 PF, spec 1 PM01–PM05) | #11 Q9, Q12 | 4K rows run offscreen: `author-machine/partC/demo-perf/*/4k/` | Offscreen excludes compositor and presentation cost | Repeat `demo-perf` and `perf` on a physical 4K monitor. |
| Narrator | #11 Q13 | UIA and Axe.Windows records (0 errors), no screen-reader session | Screen-reader usability | Optional user trial with Narrator. |
| High contrast | #11 Q8 (out of v1 scope) | None | Unverified accessibility | Later phase. |
| Spec 3 CI01 / UIA CI stability | Spec 3 decision 6 | The 20-run first-try UIA CI trial stopped at 18 runs (one Mesa download HTTP 504 at `3d0905b`; CI now retries the download). Local `uia-stress` passed 20 runs × 2 appearances: `author-machine/partB/uia-stress.log`. | Intermittent CI failures | Record 20 consecutive first-try UIA CI runs. |
| Font differences | #11 Q4, Q16 | CPU and GPU compared on each machine (VC01, GC01) | CI and author-machine font inventories differ | Record both font inventories and compare. |

## Deviations from upstream acceptance wording

Each was accepted as covering the intent of the case; none is counted as a plain pass.

| Case | Deviation | Evidence |
| --- | --- | --- |
| Spec 3 UIA17-DPI | Ran 96 ↔ 144 DPI (the author machine has 100 % + 150 %), not 144 ↔ 120. | `author-machine/partG/uia-dpi/` |
| Spec 2 PR03 | Per-user font checked in the demo's font list; showcase V04 has a fixed six-font list. | `author-machine/manual/pr03/` |
| Spec 2 PR04 | The trial record is the UT01 trial (task 3, dark/compact decision) plus the run-1 screenshots and font-switch timing, not a separate PR04 trial file. | `trial-2026-11-09-*.md`, `author-machine/partB/presentation/presentation/trial/font-switch-timing.tsv` |
| Spec 5 S5 exit check | `find_package(LcarsUI 0.1)` must fail: run locally only. | Coordinator record; not in CI |
| Spec 1 PM01–PM05 | 4K rows are offscreen (see the real 4K gap). | `author-machine/partC/demo-perf/` |

## Known issues found during verification (not blocking)

- Debug builds echo every `KHR_debug` push/pop group to `OutputDebugStringW`, about 52 lines per frame (`author-machine/manual/dg03/showcase-debug-dbwin.txt`). Release builds are unaffected.
- Web trial: a small red dot at the right edge of a slider screenshot was not explained. No slider paint code draws a red shape; the user did not answer whether it came from the app.
- Trial change requests deferred past v1: #72 (focus cues only after keyboard use; slider focus dot), #73 (slider drag lag and stepping), #74 (hover on clickable components), #75 (animation clock and slider transition), #76 (text layout).
