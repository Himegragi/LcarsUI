# v1 completion report

Status: **complete** (2026-11-10). Every required row below is either a pass or a gap listed in [evidence-gaps.md](evidence-gaps.md). Both user trials concluded that v1 can be declared complete; their change requests are deferred to #72–#76.

## Environments

| Name | Details |
| --- | --- |
| Author machine | Windows 11 build 26300, NVIDIA GeForce RTX 3060 (OpenGL 3.3.0 NVIDIA 610.88, driver 32.0.16.1088), displays `\\.\DISPLAY1` 150 % + `\\.\DISPLAY2` 100 %, Microsoft Bopomofo (`IMTCTIP.DLL` 6.2.26100.9711), Release build. |
| GitHub CI | `core.yml` (Core), `render.yml` (Render, Mesa llvmpipe), `uia` (UIA trial). The last product-code change is `dd33b85`; later commits only add tests, drivers and evidence (`git diff dd33b85 -- . ':!tests'` touches only `CMakeLists.txt` test targets and `verify-idle.ps1`). |

Author-machine evidence was copied from the idle runs into [author-machine/](author-machine). Folder names are the idle-run parts:

| Part | Date | Commit | Contents |
| --- | --- | --- | --- |
| `partA`, `partB` | 2026-11-09 11:50–12:50 | `5e027e1` | Idle run 1: clipboard, IME, DPI, host_native, UIA, UIA stress, demo UIA, KW01, real-GPU render, VC01, presentation. |
| `partC`–`partF` | 2026-11-09 16:14–16:28 | `9404974` (+ the `d1d5380` fix for `partF`) | Idle run 2: reruns of the run-1 failures after fixes `83d95e1`, `d90eb0d`, `d1d5380`. |
| `partG` | 2026-11-09 16:40 | `d1d5380` | UIA, demo UIA and UIA DPI after the dialog-bounds fix. |
| `partH` | 2026-11-10 | `e4df1c6` | `verify-idle.ps1 -Only demo` (KW02, CR09-real, APPDATA, CR05) and `-Only dpi` (D01, GA05). |
| `manual` | 2026-11-09 | `faf7785`–`dd33b85` | TDR, RenderDoc groups, DebugView output, per-user font, trial settings files. |

Failed steps in `partA`–`partD` `steps.tsv` are first attempts. Each was fixed (driver or product) and rerun green in a later part, except `partB` `presentation`, which exits 1 because its PR03 and PR04 rows are manual by design (`OPEN`); those are covered by `manual/pr03` and the UT01 records. The rows below cite the passing runs.

## Acceptance matrix

| Group | Cases | Result | Evidence |
| --- | --- | --- | --- |
| Demo behavior | NL, MD, KS, IT, HS, ST, AP, DD, AB | Pass | CI at `dd33b85`: Core [42596300569](https://github.com/Himegragi/LcarsUI/actions/runs/42596300569), Render [42596300365](https://github.com/Himegragi/LcarsUI/actions/runs/42596300365). Local full CTest 47/47 at `84ffcba` (Release). |
| Close requests | CR01–CR15 | Pass | CI Render at `dd33b85`. Real entry points CR05 (title-bar ✕, Alt+F4, taskbar; both appearances, 6/6): `author-machine/partH/demo/CR05/real-close.tsv`. Real Bopomofo CR09: `author-machine/partH/demo/CR09-real.tsv`. The host completes the composition before the close handler runs (E01), so the handler reads `Before我`; the IME trace is in `partH/demo/demo-events.log`. |
| Keyboard walkthrough | KW01, KW02 | Pass | KW01: CI and `author-machine/partB/demo-keyboard.log`. KW02 (SendInput, Microsoft Bopomofo types 共用範本 in step 6, both appearances, 12 steps): `author-machine/partH/demo/KW02.tsv`. |
| Screenshot comparison | VC01 | Pass | llvmpipe: CI Render. RTX 3060: 64/64 rows, `author-machine/partB/demo-render/demo-render-compare.tsv`. |
| Semantics and accessibility | SX01, UA01–UA07, AX01 | Pass | CI Render and UIA trial [42596300474](https://github.com/Himegragi/LcarsUI/actions/runs/42596300474). Full `demo-uia` record: `author-machine/partG/demo-uia/demo-uia/{web,material}/demo-uia.tsv` (UA01–UA07) and `axe.tsv` (AX01: 8 states per appearance, 0 errors, no excluded rules). |
| Performance | PF01–PF05 | Pass | `demo-perf` 20/20 scenarios (both appearances, 1080p and 4K): `author-machine/partC/demo-perf/`. The 4K rows run offscreen; see the real 4K gap. |
| Diagnostics | ND01, TD01, RD01 | ND01 pass; RD01 pass; **TD01 gap** | ND01: CI Render. RD01: RenderDoc captures of the demo and showcase with named debug groups, `author-machine/manual/renderdoc/*.groups.txt`. TD01: a real TDR found a hang, fixed in `dd33b85`; the fixed build was not verified on a real TDR because the forced TDR crashed the machine (`author-machine/manual/tdr/summary.md`). Listed in evidence-gaps.md. |
| Upstream specifications | Specs 1–3 and 5 completion criteria | Pass, with listed gaps | See the next section. |
| Evidence gaps | EG01 | Pass | [evidence-gaps.md](evidence-gaps.md) lists every unrun or blocked case. |
| User trial | UT01, both appearances | Pass | [trial-2026-11-09-web.md](trial-2026-11-09-web.md), [trial-2026-11-09-material.md](trial-2026-11-09-material.md): all 9 tasks done in each appearance; dark and compact values and the navigation list were accepted as they are; conclusion "v1 can be declared complete". Settings files saved during the trials: `author-machine/manual/trial/`. |

## Upstream specifications

| Spec | Author-machine and CI evidence | Remaining gaps |
| --- | --- | --- |
| Spec 1, draw list and rendering (#32) | Real GPU comparison 40 rows and DL01: `author-machine/partB/render-real-gpu/`. host_native Release: `author-machine/partA/host_native-Release.log` (Debug also passed). DG03 debugger output: `author-machine/manual/dg03/showcase-debug-dbwin.txt`. DG05 RenderDoc groups: `author-machine/manual/renderdoc/`. GA05 real drag from 100 % to 150 % clears the glyph atlas once and matches the CPU text region: `author-machine/partH/dpi/dpi.tsv`. | RF06 real TDR on the fixed build; PM01–PM05 4K offscreen only. |
| Spec 2, text and presentation settings (#31) | PR01 real Bopomofo and cross-monitor D01: `author-machine/partB/presentation/presentation/presentation.tsv`, `author-machine/partH/dpi/dpi.tsv`. Clipboard 400 rows: `author-machine/partA/clipboard/clipboard/clipboard.tsv`. E01 IME matrix: `author-machine/partD/ime/ime/ime.tsv`. PR03 per-user font (Atkinson Hyperlegible from `%LOCALAPPDATA%`): `author-machine/manual/pr03/`. PR04: trial screenshots and first-frame font-switch timing `author-machine/partB/presentation/presentation/trial/font-switch-timing.tsv`; user feedback in the UT01 records (task 3 and the dark/compact decision). PS03 now covers size, font, dark and compact switches during composition in CTest (`edit_form`). | PR03 was checked in the demo's font list because showcase V04 has a fixed list (deviation). |
| Spec 3, semantic layer and UIA (#30) | `uia`, `uia-ime`: `author-machine/partB/uia*/`, rerun `author-machine/partG/uia/`. `uia-dpi`: `author-machine/partG/uia-dpi/`. `uia-stress` 20 runs × 2 appearances: `author-machine/partB/uia-stress.log`. AXE01 and UIA18 in the UIA trial CI. UIA08 now compares the text-range rectangle with the drawn glyph cell (≤ 1 px) in CTest (`uia_web`, `uia_material`). | CI01: the 20-run first-try UIA CI trial is incomplete. UIA17-DPI ran 96↔144 instead of 144↔120 (deviation). |
| Spec 5, packaging (#33) | AU01, PK01, PK03 passed in CI and locally (merged `37c9d81`). | The S5 exit check (`find_package` with version 0.1 must fail) was run locally only (deviation). |
