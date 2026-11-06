# First component batch acceptance — issue #25

## X02 / partial X04 state-driving follow-up (2026-11-06)

**X02: PASS for the assembled samples. X04: OPEN.** The tested code commit is `82da26c`; the local `integration/first-component-batch` tip `163bec9` is an ancestor. This section supersedes the X02/X04 close attempt below while preserving its history. X04 is not complete: its 1,132 open cells are listed individually with reasons in [x04-open.tsv](x04-state-sweep/x04-open.tsv).

### X02 per-sample results

Each cell covers shipped and alternate IDs. `P` means all reachable native cases in the sample passed; `N/A` is a separately identified unreachable input or unsupported interaction, not a pass. [Final Debug rows](x04-state-sweep/debug-final.tsv) include all detailed cases, offsets and rectangle coordinates.

| Page | DIP size | Scale | Web | Material | Sample qualification |
| --- | --- | --- | --- | --- | --- |
| V01 | 960?720 | 100% | P; N/A | P; N/A | Track to partial anchor is unreachable from a visible start; wheel and thumb cover partial and clipped anchors. |
| V01 | 960?720 | 150% | P; N/A | P; N/A | Same. |
| V01 | 640?480 | 100% | P; N/A | P; N/A | Wheel, track and thumb cover partial and clipped dropdown anchors. |
| V01 | 640?480 | 150% | P; N/A | P; N/A | Same. |
| V02 | 960?720 | 100% | P | P | Nested-list navigation, reveal and wheel. |
| V02 | 960?720 | 150% | P | P | Same. |
| V02 | 640?480 | 100% | P | P | Same. |
| V02 | 640?480 | 150% | P | P | Same. |
| V03 | 960?720 | 100% | P; N/A | P; N/A | No background overflow; modal-wheel blocking and scroll-driven tooltip edges/dwell are N/A. |
| V03 | 960?720 | 150% | P; N/A | P; N/A | Same. |
| V03 | 640?480 | 100% | P; N/A | P; N/A | Tooltip scroll and pointer dwell pass; partial/clipped tooltip anchors are unreachable over the actual scroll range. |
| V03 | 640?480 | 150% | P; N/A | P; N/A | Same. |

The V03 640?480 Web anchor is top 392/height 40, clip y=68..480, maximum offset 24 at 100%; at 150% these are 588/60, y=102..720, maximum 36. Material is 404/44, y=72..480, maximum 44 at 100%; at 150%, 606/66, y=108..720, maximum 66. All offsets retain the full anchor within its clip. The actual scrollbar position/offset search is exhaustive; the TSV records those dimensions for each input, appearance and ID set. At 960?720 V01, a track click centers the long thumb on the pointer, and no pointer position outside a visible-start thumb yields a partially clipped endpoint; the TSV records the actual track, thumb and offset bounds.

Keyboard region scrolling while a popup is open is N/A: `WM_KEYDOWN` dispatches to the focused component, and the host only schedules list reveal for a focused list, not a region-scroll command in these popup states. Independently scrolling the assembled dropdown list is N/A: it has three options, with total row height equal to panel height (96 px at 100%, 144 px at 150%) in both appearances. Wheel over that panel is separately checked not to scroll the background. Pointer-only tests verify an old deadline cannot reopen the tooltip after a scroll and a new full dwell can reopen it, including after a shown tooltip is closed.

### Verification and evidence

- Debug desktop: **7/7 CTest**, **380/380 native checks**, **168 N/A** rows ([log](x04-state-sweep/debug-final.log), [rows](x04-state-sweep/debug-final.tsv)). X02 rows specifically: **272 P / 0 F / 168 N/A**. X04 native sample checks: **46 P / 0 F**; these are sample checks, not a complete X04 matrix.
- Release desktop: **7/7 CTest**, **380/380 native checks**, **168 N/A** rows ([log](x04-state-sweep/release-final.log), [rows](x04-state-sweep/release-final.tsv)). Release core-only: **6/6 CTest** ([log](x04-state-sweep/core-final.log)).
- First Debug attempt: **6/7 CTest, 348 native checks with 8 failures**, all eight the new V03 pointer-dwell fixture ([log](x04-state-sweep/debug-first.log), [rows](x04-state-sweep/debug-first.tsv)). The synthetic hover visit was interrupted by unrelated queued physical-pointer leave messages; the fixture was changed to dispatch its synthetic wheel synchronously without pumping those unrelated messages. The subsequent pre-fix-code Debug run passed **7/7**, **364/364 native checks** and **168 N/A** ([log](x04-state-sweep/debug-pointer.log), [rows](x04-state-sweep/debug-pointer.tsv)). AC08 clipboard rows passed in each of these runs. This observation does not explain the earlier historical clipboard failure in the section below.
- The production fix adds the source-change cue to a conflicting text field's painted description while preserving its associated help and latest-value action. The regression failed before the fix in both appearances and both ID sets, then passed through the core seam; the final native samples cover both sizes and scales.
- Host input production files were not changed. The Win32-message verification driver changed in `tests/host_verify.cpp`. `--clipboard-only`, `--ime` and `--dpi` were not run. Trailing spaces were removed from saved logs and TSVs without changing results.

### Not completed

1. X04 remains open. Only the name field's read-only and source-conflict/latest states have the new shared drivers and complete checks recorded here. The remaining **1,132 component/state aggregate cells** need state drivers, restoration and/or full semantic-versus-clipped-paint comparisons at the native and core size/scale seams. For example, `web / shipped / form.title.pages.v03.progress / clipped` remains open; the normal 960?720 V03 fixture has no overflow, so the driver must create an appropriate model state rather than treating an omitted test as a pass. The observer records a text run and whether any raster ink survives the clip; it is not OCR or proof that every glyph is visible.
2. The earlier intermittent AC08 failure remains unexplained. UIA/screen-reader, MCP adapters and prior deferred design work remain separate. No new undefined behavior policy was identified in the completed samples.

### Generated X04 component ? state table (partial)

Generated by [make-progress.py](x04-state-sweep/make-progress.py) from the final Debug native report and the [Web](x04-state-sweep/web-x04-sweep.tsv) / [Material](x04-state-sweep/material-x04-sweep.tsv) inventories. **P=8, F=0, N/A=1,596, O=1,132 aggregate cells.** `P` requires all four size/scale native samples and the shared core state driver. `N/A` is from the registered type's capabilities or fixed page/overlay membership. `O` explicitly means the required driver or full comparison has not been implemented; it is not an acceptance result. The [driven sample summary](x04-state-sweep/x04-driven.md) names those eight cells, and [the open list](x04-state-sweep/x04-open.tsv) gives each remaining cell and its reason. The table intentionally retains `O` because this is a partial implementation and must not convert missing tests into P or F.

| Appearance / IDs | Component | Normal | Hidden | Clipped | Disabled | Read-only | Selected | Candidate | Error | Conflict / latest | Executing | Modal | Tooltip |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| web / shipped | `form.title.pages.tabs` | O | N/A | O | O | N/A | O | O | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.intro` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.state` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.draft` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.applied` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.count` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name.label` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name.help` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name.error` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name` | O | O | O | O | P | N/A | N/A | O | P | N/A | N/A | N/A |
| web / shipped | `fixture.normal` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `fixture.read_only` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `fixture.disabled` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `fixture.error` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `fixture.long` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `fixture.offline_disabled` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `fixture.region_empty` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.apply` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.notices.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.mode.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.region.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.volume.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.notices` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| web / shipped | `form.mode` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| web / shipped | `form.region` | O | O | O | O | N/A | N/A | O | O | O | N/A | N/A | N/A |
| web / shipped | `form.volume` | O | O | O | O | N/A | N/A | N/A | O | O | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.detail` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.search` | O | O | O | O | O | N/A | N/A | O | O | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.list` | O | O | O | O | N/A | O | O | N/A | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.one` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.twenty` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.hundred` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.sort` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.erase` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.disable` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v02.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.source` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.result` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.progress` | O | O | O | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.half` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | O |
| web / shipped | `form.title.pages.v03.full` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.below` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.above` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.open` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.v03.temporary` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / shipped | `form.title.pages.dialog.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| web / shipped | `form.title.pages.dialog.body` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| web / shipped | `form.title.pages.dialog.cancel` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| web / shipped | `form.title.pages.dialog.confirm` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| web / alternate | `t.pages.tabs` | O | N/A | O | O | N/A | O | O | O | N/A | N/A | N/A | N/A |
| web / alternate | `t` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `i` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `s` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `d` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `r` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `c` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n.l` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n.h` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n.e` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n` | O | O | O | O | P | N/A | N/A | O | P | N/A | N/A | N/A |
| web / alternate | `f1` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `f2` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `f3` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `f4` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `f5` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `offline` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `region.empty.alt` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `f6` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `go` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `notices.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `mode.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `region.alt.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `volume.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `notices` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| web / alternate | `mode` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| web / alternate | `region.alt` | O | O | O | O | N/A | N/A | O | O | O | N/A | N/A | N/A |
| web / alternate | `volume` | O | O | O | O | N/A | N/A | N/A | O | O | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.detail` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.search` | O | O | O | O | O | N/A | N/A | O | O | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.list` | O | O | O | O | N/A | O | O | N/A | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.one` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.twenty` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.hundred` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.sort` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.erase` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.disable` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v02.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.source` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.result` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.progress` | O | O | O | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.half` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | O |
| web / alternate | `t.pages.v03.full` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.below` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.above` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.open` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.v03.temporary` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| web / alternate | `t.pages.dialog.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| web / alternate | `t.pages.dialog.body` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| web / alternate | `t.pages.dialog.cancel` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| web / alternate | `t.pages.dialog.confirm` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| material / shipped | `form.title.pages.tabs` | O | N/A | O | O | N/A | O | O | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.intro` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.state` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.draft` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.applied` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.count` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name.label` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name.help` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name.error` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name` | O | O | O | O | P | N/A | N/A | O | P | N/A | N/A | N/A |
| material / shipped | `fixture.normal` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `fixture.read_only` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `fixture.disabled` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `fixture.error` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `fixture.long` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `fixture.offline_disabled` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `fixture.region_empty` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.apply` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.notices.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.mode.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.region.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.volume.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.notices` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| material / shipped | `form.mode` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| material / shipped | `form.region` | O | O | O | O | N/A | N/A | O | O | O | N/A | N/A | N/A |
| material / shipped | `form.volume` | O | O | O | O | N/A | N/A | N/A | O | O | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.detail` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.search` | O | O | O | O | O | N/A | N/A | O | O | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.list` | O | O | O | O | N/A | O | O | N/A | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.one` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.twenty` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.hundred` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.sort` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.erase` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.disable` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v02.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.source` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.result` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.progress` | O | O | O | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.half` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | O |
| material / shipped | `form.title.pages.v03.full` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.below` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.above` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.open` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.v03.temporary` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / shipped | `form.title.pages.dialog.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| material / shipped | `form.title.pages.dialog.body` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| material / shipped | `form.title.pages.dialog.cancel` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| material / shipped | `form.title.pages.dialog.confirm` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| material / alternate | `t.pages.tabs` | O | N/A | O | O | N/A | O | O | O | N/A | N/A | N/A | N/A |
| material / alternate | `t` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `i` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `s` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `d` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `r` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `c` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n.l` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n.h` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n.e` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n` | O | O | O | O | P | N/A | N/A | O | P | N/A | N/A | N/A |
| material / alternate | `f1` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `f2` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `f3` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `f4` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `f5` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `offline` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `region.empty.alt` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `f6` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `go` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `notices.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `mode.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `region.alt.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `volume.latest` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `notices` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| material / alternate | `mode` | O | O | O | O | N/A | O | N/A | O | O | N/A | N/A | N/A |
| material / alternate | `region.alt` | O | O | O | O | N/A | N/A | O | O | O | N/A | N/A | N/A |
| material / alternate | `volume` | O | O | O | O | N/A | N/A | N/A | O | O | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.detail` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.search` | O | O | O | O | O | N/A | N/A | O | O | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.list` | O | O | O | O | N/A | O | O | N/A | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.one` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.twenty` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.hundred` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.sort` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.erase` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.disable` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v02.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.source` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.result` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.progress` | O | O | O | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.zero` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.half` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | O |
| material / alternate | `t.pages.v03.full` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.below` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.above` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.reset` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.open` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.v03.temporary` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | N/A | N/A |
| material / alternate | `t.pages.dialog.title` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| material / alternate | `t.pages.dialog.body` | O | O | O | N/A | N/A | N/A | N/A | O | N/A | N/A | O | N/A |
| material / alternate | `t.pages.dialog.cancel` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |
| material / alternate | `t.pages.dialog.confirm` | O | O | O | O | N/A | N/A | N/A | N/A | N/A | O | O | N/A |



## X02 / X04 close attempt (2026-11-06)

**X02: OPEN. X04: OPEN.** The tested source is `2273cea`; the local integration tip `fe6fe13` was already an ancestor. This section supersedes the earlier X02/X04 follow-up, which is retained as history. The resulting code and evidence commits are on `batch/x02-x04-close`.

### X02 per-sample results

Each cell covers both shipped and alternate IDs. `D` means the specified subset passed, not that X02 as a whole passed. The core checks use the assembled pages, typed handles, fake metrics and the shared frame input seam. The native checks use real Win32 messages, host paint and fonts. `M0` is not applicable because the background has no overflow; `M+` means modal wheel blocking was exercised with overflow. `O` is an applicable or required cross-check that remains open. [Final Debug native rows](x02-x04-close/debug-final.tsv); [core and native log](x02-x04-close/debug-final.log); [Release rows](x02-x04-close/release-final.tsv).

| Page | DIP size | Scale | Web | Material | Exercised / limit |
| --- | --- | --- | --- | --- | --- |
| V01 | 960×720 | 100% | D; O | D; O | Dropdown wheel and thumb at partial/clipped edges; track at clipped edge. Track to partial edge was unreachable from a visible start in this fixture. |
| V01 | 960×720 | 150% | D; O | D; O | Same. |
| V01 | 640×480 | 100% | D | D | Dropdown wheel, track and thumb at partial and fully clipped edges. |
| V01 | 640×480 | 150% | D | D | Same. |
| V02 | 960×720 | 100% | D | D | Nested list wheel and keyboard reveal; no popup is assembled. |
| V02 | 960×720 | 150% | D | D | Same. |
| V02 | 640×480 | 100% | D | D | Same. |
| V02 | 640×480 | 150% | D | D | Same. |
| V03 | 960×720 | 100% | D; M0 | D; M0 | Tooltip dismissal/dwell and dialog actions; background scrolling and modal-wheel blocking are N/A without overflow. |
| V03 | 960×720 | 150% | D; M0 | D; M0 | Same. |
| V03 | 640×480 | 100% | D; M+; O | D; M+; O | Tooltip wheel/track/thumb close and fresh dwell with a visible anchor; partial/clipped V03 anchor cases remain untested. |
| V03 | 640×480 | 150% | D; M+; O | D; M+; O | Same. |

The existing long-description fixture was used to reach V01 edges; no content was added to create modal overflow. Drawing the previously omitted tab group name legitimately changes the page height, so some former 640×480 M0 samples now have overflow. Wheel over a dropdown panel is kept from scrolling the background; an independently scrollable dropdown-panel list is not implemented or verified here. A fully clipped focused tooltip source is covered through an additional tooltip on the assembled V01 dropdown, not as a substitute for the missing V03 edge samples.

### Verification

- Debug desktop: **7/7** CTest entries and **340/340** native checks, plus **32 N/A** native rows on `2273cea` ([log](x02-x04-close/debug-final.log), [rows](x02-x04-close/debug-final.tsv)). The assembled suite passed **42/42** cases for each appearance and ID set, 168 case executions and 14,940 cumulative checks.
- Release desktop: **7/7** CTest entries and **340/340** native checks, plus **32 N/A** native rows ([log](x02-x04-close/release-final.log), [rows](x02-x04-close/release-final.tsv)).
- Release core-only: **6/6** CTest entries ([log](x02-x04-close/core-final.log)).
- Earlier at `d4aeba8`, the first Debug desktop run was **6/7**, with **339/340** native checks: Web AC08 pointer-copy and undo results were unexpected, and clipboard text restoration was reported successful ([log](x02-x04-close/debug-first.log), [rows](x02-x04-close/debug-first.tsv)). The one requested native rerun passed **341/341**, with **31 N/A** rows ([rows](x02-x04-close/debug-retry/host.tsv)). The later final build is a different source revision; neither passing run explains the earlier failure.
- The retry saved representative V02 rasters for [Web](x02-x04-close/debug-retry/web-x04-v02-selection.bmp) and [Material](x02-x04-close/debug-retry/material-x04-v02-selection.bmp); these are supplementary, not complete text comparisons.
- `--clipboard-only`, `--ime` and `--dpi` were not run. Existing idle-desktop and user-trial evidence remains historical.

Trailing spaces were removed from the saved logs and TSVs without changing their results.

### Not completed

1. X02: cover partial and fully clipped V03 tooltip anchors on the real input path where reachable, the 960×720 dropdown track-to-partial case, keyboard scrolling while popups are open, pointer-hover dwell after scrolling, and independently scrolling a list inside a dropdown panel. The assembled 960×720 V03 background has no overflow; the 960×720 V01 track geometry could not reach the partial endpoint from a visible start. These cases are not counted as passes.
2. X04: extend the generated inventory into a state-driving sweep for every component and every applicable disabled, read-only, selected/checked, candidate, error, conflict/latest, executing, modal and tooltip state. Compare restrictions and cues, associated labels, hidden/clipped components and every value against clipped native paint; run the same general sweep at the core size/scale seam. The assembled checkbox conflict and a representative executing button are tested, but do not establish that full matrix.
3. The earlier clipboard failure remains unexplained. UIA/screen-reader, MCP adapters and the prior deferred design work remain separate.

### Generated X04 component × state inventory

The table below is generated from [Web observations](x02-x04-close/web-x04-sweep.tsv) and [Material observations](x02-x04-close/material-x04-sweep.tsv) by [the generator](x02-x04-close/make-x04-table.py). It is also available as a [standalone table](x02-x04-close/x04-table.md). `O` means open; `N/A` comes from the component type or known modal/tooltip membership; `O (n P)` records some positive snapshots while the remainder is open. Label error cells remain open because the inventory does not record each label's association role. Name/value fractions count visible normal snapshots whose semantic string was present in a recorded text run with at least one unclipped glyph sample; they do not prove that the entire string was visible. Hidden/clipped `P` is limited to the recorded checks. The native observer records component ownership, text or graphical cue, color, clip and whether any nontransparent raster sample passed the clip; it is not OCR or an occlusion check. Normal restrictions and the remaining state columns are not yet driven by this sweep.

| Appearance / IDs | Component | Type | Name | Value | Restriction | Hidden | Clipped | Disabled | Read-only | Selected | Candidate | Error | Conflict | Executing | Modal | Tooltip |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| web / shipped | `form.title.pages.tabs` | tabs | 12/12 observed | 12/12 observed | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A |
| web / shipped | `form.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.intro` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.state` | label | N/A | O | N/A | P | O (4 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.draft` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.applied` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `result.count` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name.label` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name.help` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name.error` | label | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.name` | text | 4/4 observed | 4/4 observed | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / shipped | `fixture.normal` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `fixture.read_only` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `fixture.disabled` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `fixture.error` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `fixture.long` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `fixture.offline_disabled` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `fixture.region_empty` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.apply` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.notices.latest` | button | 2/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.mode.latest` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.region.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.volume.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.notices` | checkbox | 4/4 observed | 0/4 observed | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / shipped | `form.mode` | radio group | 2/2 observed | 2/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / shipped | `form.region` | dropdown | 2/2 observed | 0/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / shipped | `form.volume` | slider | O | O | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.detail` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.search` | text | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.list` | list | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v02.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.one` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.twenty` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.hundred` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.sort` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.erase` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.disable` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v02.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.source` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.result` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.progress` | progress | 4/4 observed | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| web / shipped | `form.title.pages.v03.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.half` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | O (0 P) |
| web / shipped | `form.title.pages.v03.full` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.below` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.above` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.open` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.v03.temporary` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / shipped | `form.title.pages.dialog.title` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| web / shipped | `form.title.pages.dialog.body` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| web / shipped | `form.title.pages.dialog.cancel` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| web / shipped | `form.title.pages.dialog.confirm` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| web / alternate | `t.pages.tabs` | tabs | 12/12 observed | 12/12 observed | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A |
| web / alternate | `t` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `i` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `s` | label | N/A | O | N/A | P | O (4 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `d` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `r` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `c` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n.l` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n.h` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n.e` | label | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `n` | text | 4/4 observed | 4/4 observed | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / alternate | `f1` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `f2` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `f3` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `f4` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `f5` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `offline` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `region.empty.alt` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `f6` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `go` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `notices.latest` | button | 2/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `mode.latest` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `region.alt.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `volume.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `notices` | checkbox | 4/4 observed | 0/4 observed | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / alternate | `mode` | radio group | 2/2 observed | 2/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / alternate | `region.alt` | dropdown | 2/2 observed | 0/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / alternate | `volume` | slider | O | O | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.detail` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.search` | text | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.list` | list | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v02.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.one` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.twenty` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.hundred` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.sort` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.erase` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.disable` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v02.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.source` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.result` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.progress` | progress | 4/4 observed | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| web / alternate | `t.pages.v03.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.half` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | O (0 P) |
| web / alternate | `t.pages.v03.full` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.below` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.above` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.open` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.v03.temporary` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| web / alternate | `t.pages.dialog.title` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| web / alternate | `t.pages.dialog.body` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| web / alternate | `t.pages.dialog.cancel` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| web / alternate | `t.pages.dialog.confirm` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| material / shipped | `form.title.pages.tabs` | tabs | 12/12 observed | 12/12 observed | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A |
| material / shipped | `form.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.intro` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.state` | label | N/A | O | N/A | P | O (4 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.draft` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.applied` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `result.count` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name.label` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name.help` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name.error` | label | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.name` | text | 4/4 observed | 4/4 observed | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / shipped | `fixture.normal` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `fixture.read_only` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `fixture.disabled` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `fixture.error` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `fixture.long` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `fixture.offline_disabled` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `fixture.region_empty` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.apply` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.notices.latest` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.mode.latest` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.region.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.volume.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.notices` | checkbox | 4/4 observed | 0/4 observed | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / shipped | `form.mode` | radio group | 2/2 observed | 2/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / shipped | `form.region` | dropdown | 2/2 observed | 0/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / shipped | `form.volume` | slider | O | O | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.detail` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.search` | text | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.list` | list | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v02.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.one` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.twenty` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.hundred` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.sort` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.erase` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.disable` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v02.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.source` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.result` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.progress` | progress | 4/4 observed | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| material / shipped | `form.title.pages.v03.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.half` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | O (0 P) |
| material / shipped | `form.title.pages.v03.full` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.below` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.above` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.open` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.v03.temporary` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / shipped | `form.title.pages.dialog.title` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| material / shipped | `form.title.pages.dialog.body` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| material / shipped | `form.title.pages.dialog.cancel` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| material / shipped | `form.title.pages.dialog.confirm` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| material / alternate | `t.pages.tabs` | tabs | 12/12 observed | 12/12 observed | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A |
| material / alternate | `t` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `i` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `s` | label | N/A | O | N/A | P | O (4 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `d` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `r` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `c` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n.l` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n.h` | label | N/A | 4/4 observed | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n.e` | label | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `n` | text | 4/4 observed | 4/4 observed | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / alternate | `f1` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `f2` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `f3` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `f4` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `f5` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `offline` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `region.empty.alt` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `f6` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `go` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `notices.latest` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `mode.latest` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `region.alt.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `volume.latest` | button | O | N/A | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `notices` | checkbox | 4/4 observed | 0/4 observed | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / alternate | `mode` | radio group | 2/2 observed | 2/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / alternate | `region.alt` | dropdown | 2/2 observed | 0/2 observed | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / alternate | `volume` | slider | O | O | O (0 P) | P | O (4 P) | O (0 P) | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.detail` | label | N/A | 2/2 observed | N/A | P | O (2 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.search` | text | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.list` | list | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v02.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.one` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.twenty` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.hundred` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.sort` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.erase` | button | 2/2 observed | N/A | O (0 P) | P | O (2 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.disable` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v02.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.title` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.source` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.result` | label | N/A | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.progress` | progress | 4/4 observed | 4/4 observed | N/A | P | O (0 P) | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
| material / alternate | `t.pages.v03.zero` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.half` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | O (0 P) |
| material / alternate | `t.pages.v03.full` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.below` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.above` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.reset` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.open` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.v03.temporary` | button | 4/4 observed | N/A | O (0 P) | P | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | N/A | N/A |
| material / alternate | `t.pages.dialog.title` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| material / alternate | `t.pages.dialog.body` | label | N/A | O | N/A | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | O | N/A | N/A | O (0 P) | N/A |
| material / alternate | `t.pages.dialog.cancel` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |
| material / alternate | `t.pages.dialog.confirm` | button | O | N/A | O (0 P) | O (0 P) | O (0 P) | O (0 P) | N/A | N/A | N/A | N/A | N/A | O (0 P) | O (0 P) | N/A |

## X02 / X04 implementation follow-up (2026-11-06)

**Current result: NOT COMPLETED. X02 and X04 remain open.** This section supersedes only the X02 and X04 rows in the earlier sections. The tested code is `32632d2` on `batch/x02-x04`; the local integration tip `dee2d74` was already an ancestor. No production, host input, IME, or DPI code changed. The existing D01, E01 and user-trial evidence above the removal follow-up remains valid for the earlier build; those idle-desktop tests were deliberately not rerun here.

### X02 per-sample results

Each cell covers **both shipped and alternate ID sets**. `D` means the new checks passed for the interactions named for that page; it is not an X02 acceptance pass. Core tests drive typed handles and the input seam with fake metrics; native checks use a real Win32 window, the host's real fonts and paint, posted keyboard/click/wheel messages, and synchronous `WM_MOUSEMOVE`/`WM_MOUSELEAVE` for a controlled pointer visit. Tooltip dwell uses a controlled test clock. `Q` means the open dropdown/tooltip scroll policy below was not asserted. `M0` means a modal wheel was delivered and offset stayed unchanged, but the assembled background had no overflow; `M+` means the modal wheel was checked against an overflowing background. [Native sample rows](x02-x04/host.tsv); [core cases](x02-x04/edit-form.log).

| Page | DIP size | Scale | Web | Material | Defined behavior exercised |
| --- | --- | --- | --- | --- | --- |
| V01 | 960×720 | 100% | D, Q | D, Q | Dropdown Esc, Tab, outside click; value, focus, outside hit and target action |
| V01 | 960×720 | 150% | D, Q | D, Q | Same |
| V01 | 640×480 | 100% | D, Q | D, Q | Same |
| V01 | 640×480 | 150% | D, Q | D, Q | Same |
| V02 | 960×720 | 100% | D | D | 100-item nested list: End, visible row, hit, wheel and End again; no popup is assembled on this page |
| V02 | 960×720 | 150% | D | D | Same |
| V02 | 640×480 | 100% | D | D | Same |
| V02 | 640×480 | 150% | D | D | Same |
| V03 | 960×720 | 100% | D, Q, M0 | D, Q, M0 | Tooltip Esc, focus leave, pointer leave, transparency; dialog Esc, Cancel, Confirm and wheel |
| V03 | 960×720 | 150% | D, Q, M0 | D, Q, M0 | Same |
| V03 | 640×480 | 100% | D, Q, M0 | D, Q, M+ | Same |
| V03 | 640×480 | 150% | D, Q, M0 | D, Q, M+ | Same |

**Policy questions:** For V01, when the containing region scrolls with the dropdown open, must it follow the anchor, clip, or close, both while the anchor remains partially visible and when it becomes fully clipped? For V03, what are those outcomes for the tooltip in the same two anchor-visibility situations? B03/B04 and X02 require consistent scroll, clipping, hit and popup behavior but do not choose these outcomes; ADR 0009's “follow” is about the source **appearance**, not a scroll policy. Those cases were stopped rather than made into assertions of current behavior. V03 dialog background scrolling is blocked by the modal contract, but the M0 samples do not demonstrate blocking a movable viewport. V02 has no assembled popup, so an open-popup scroll case there is not applicable.

### X04 component × state evidence

The following rows apply to **both appearances and both ID sets**. `B` is the prior baseline: semantic names/values/option labels were compared with text **sent to** each arranged component's paint adapter with fake metrics, even if clipped; it does not establish visible pixels. `S` is a new paint-command state check, `R` is a representative real-font host raster check, and `O` is still open. A dash means the state is not supplied by this assembled fixture or not applicable to that row. `S` checks the listed cues, not a complete clipped-pixel comparison. [Core cases](x02-x04/edit-form.log); [native checks](x02-x04/host.tsv); [V01 error](x02-x04/web-x04-v01-error.bmp), [V02 selection](x02-x04/web-x04-v02-selection.bmp), [V03 dialog](x02-x04/web-x04-v03-dialog.bmp) and corresponding Material images in the same directory. The real raster checks detect colored/text pixels in expected regions; they do not OCR or compare every semantic string.

The historical hand-written matrix is superseded by the generated X04 inventory in the current top section.

The hidden-page check inventories every placement produced on each active page and verifies it is absent and reports `hidden` admission on the other pages. One fully clipped V01 button is checked for a zero visible intersection and no hit. This is not an exhaustive visibility report for every component, and the semantic snapshot does not expose a general visible/clipped property. Direct component probes do not honor the host's clipping and may record offscreen commands. The native raster samples cover a state on each page under each appearance, but not every component or state.

The assembled form has no exposed boolean-source update/reentrant rejection fixture for the checkbox conflict; the existing standalone checkbox regression is not substituted for that assembled-page comparison. Button `executing` is transient inside synchronous callbacks, and this follow-up does not capture the real paint path during that state. A native paint observation that records clipped text and its owning component, or an equivalent independent raster/text comparison, is still needed for the exhaustive claim.

During test development, the first [wheel fixture](x02-x04/wheel-fixture-before-correction.tsv) failed samples with no overflow because it required a positive maximum; the corrected result records those samples as M0. The first [pointer fixture](x02-x04/tooltip-fixture-before-correction.tsv) failed to establish the pointer visit with queued synthetic moves while `TrackMouseEvent` could also queue a leave for the actual desktop pointer. The final test delivers the controlled pointer move and leave synchronously through the Win32 window procedure. These were test-fixture corrections, not production fixes; no conclusion about real hardware pointer movement is drawn from the controlled visit alone.

### Verification and remaining work

Debug desktop CTest passed **7/7**, including **156/156** native checks ([log](x02-x04/debug.log)). The first Release desktop run passed 6/7 CTest entries and **154/156** native checks: Material AC08 pointer-copy and 60 ms clipboard-contention copy failed; both reported clipboard restoration successful ([log](x02-x04/release-first.log), [native rows](x02-x04/host-first.tsv)). The new X02/X04 native rows passed in that run. The requested solo clipboard stress completed **386/400**, with **14 failures** across ordinary copy/paste/undo and contention-copy cases ([rows](x02-x04/clipboard/clipboard.tsv), [log](x02-x04/clipboard.log)); each failure reported Unicode clipboard text restored. This is the previously recorded intermittent area, but its cause is not established.

After the solo run, a sequential Release desktop rerun passed **7/7** CTest entries and **156/156** native checks ([log](x02-x04/release.log), [rows](x02-x04/host.tsv)); this does not erase the preceding failures. Release core-only passed **6/6** ([log](x02-x04/core.log)). The assembled showcase suite passed **41/41** cases in each appearance and ID set, 164 case executions and 14,604 cumulative checks ([output](x02-x04/edit-form.log)). The 64 native X02 subchecks and six X04 raster subchecks passed in the final native run. Verification used Windows, clang-cl 19.1.5 and OpenGL 3.3.0 NVIDIA 610.88; the report and evidence are committed after the tested code.

Trailing spaces and trailing blank lines were removed from saved build and host logs without changing outcomes.

Not completed:

1. Resolve the two exact popup-scroll policy questions above, then cross the defined result with every applicable sample through both seams. Obtain a meaningful movable-background modal-wheel check in the M0 samples without changing their requested client dimensions or pretending a zero maximum is a scroll.
2. Complete X04's actual clipped-visible semantic comparison for every listed component and applicable state, including checkbox conflict/action, dropdown candidate, executing buttons, semantic list/tab names, tooltip content, and every hidden or clipped component. Extend native real-font/paint comparison beyond the three representative cues per appearance.
3. Investigate the clipboard failures above; their cause is not established and this run does not establish clipboard stress as passing. UIA/screen-reader and MCP adapters and the previously recorded deferred design refactors remain separate work. Real IME and DPI were intentionally not rerun here.

## Idle-desktop reruns, D01 100%↔150% and user trial (2026-11-06)

**Current result: NOT COMPLETED — X02 and X04 remain open.** This section supersedes the D01, E01 and user-trial rows of the follow-up below; its S-H1, P-B3, X02, X04 and clipboard rows still stand. All runs below rebuilt the stated commit in a clean worktree and were run by the coordinator with no other desktop activity and no concurrent native tests.

| Item | Result | Evidence |
| --- | --- | --- |
| D01 150%↔125%, `532c0e8` | **12/12** | [results](idle-rerun/dpi-532c0e8/dpi.tsv), [Release CTest 7/7](idle-rerun/dpi-532c0e8/release.log) |
| D01 150%↔125%, `cf51267` | **12/12** | [results](idle-rerun/dpi-cf51267/dpi.tsv), [Release CTest 7/7](idle-rerun/release-cf51267.log) |
| E01 real Microsoft Bopomofo, `cf51267` | **46/46** | [results](idle-rerun/ime-cf51267/ime.tsv), [events](idle-rerun/ime-cf51267/ime-events.log); IMTCTIP.DLL 6.2.26100.9711, ChtIME.exe 6.2.26100.9278 |
| D01 100%↔150%, `cf51267` | **12/12** | [results](dpi-100-150/dpi.tsv), [Release CTest 7/7](dpi-100-150/release.log): `\\.\DISPLAY1` 144 DPI ↔ `\\.\DISPLAY2` 96 DPI, both appearances, forward and return, selection/focus and physical click caret, composition with IME caret and candidate-position request, open dropdown popup. |
| D01 candidate window, visual | **Confirmed by the user** | The user watched the real Bopomofo candidate window follow the caret on both displays during the 100%↔150% run. |
| U01 user trial | **Confirmed by the user** | The user reported V01, V02 and V03 working in a manual trial. |

The earlier `cf51267` D01 10/12 (Web popup) and interrupted IME run were not reproduced with an idle desktop, and `532c0e8` also passed, so they are attributed to desktop activity during those runs and not to a regression. Each idle rerun was performed once; this does not prove the failures cannot recur.

Still not completed: X02's uncrossed popup/scroll interactions and X04's complete per-component visible-versus-semantic comparison. The historical clipboard intermittency remains without an established cause; UIA/screen-reader and MCP adapters and the deferred design refactors remain separate work.

## Removal and acceptance follow-up (2026-11-06)

**Current result: NOT COMPLETED.** This section supersedes the review-fix status below; the remaining sections are historical. The final code/test commit is `99d96ec`; local `integration/first-component-batch` at `532c0e8` was already an ancestor. Final verification used Windows 11 Pro N 26H2 build 26300.9711, clang-cl 19.1.5, OpenGL 3.3.0 NVIDIA 610.88, Microsoft Bopomofo IMTCTIP.DLL 6.2.26100.9711 / ChtIME.exe 6.2.26100.9278. The final evidence commit follows the tested code.

| Item | Current result | Evidence and limit |
| --- | --- | --- |
| S-H1 | Passed for the supported button removal API | `components` tests request self-removal in an action, see storage retained through the callback, reject dispatch to the pending target, and verify reclamation and stale identity after reusing the ID. A focus-callback regression checks pointer target revalidation. The test seam checks control, page map and member vector, actions, errors, tooltips, live identity, pending removals and confirmation connection sizes after 1,000 cycles; captured callbacks are released. The assembled V03 test runs 1,000 remove/reset cycles for each appearance and ID set. Confirmation fields can be reconnected after their button is removed. Removal of other component types is not offered by this API. |
| P-B3 | Passed | `TabsHandle::add_member` accepts a general `ComponentHandle`, rejects foreign and duplicate members, and obeys hidden-page admission. The V03 temporary opener uses it; P04 cancel/confirm and P08 reset remain in the assembled suite. |
| X02 | Partial, matrix below | The prior 24 native page/size/forced-scale/appearance overlap and reveal/hit samples pass on the final build. New core layout tests cross the applicable popup and scroll checks below with both sizes, scales, appearances and ID sets. They use the host/core input seam and fake text metrics, not actual OS wheel/thumb input or raster inspection for every combination. Scrolling while a popup is open and all pointer/keyboard dismissal variants are not crossed with every sample. |
| X04 | Partial | On all three pages and both appearances/ID sets, the new paint-command probe compares arranged controls' applicable semantic names, values and option labels with text actually sent to each component's paint adapter. It does not inspect final pixels, clipped visibility, every modal/hidden component, or every applicable disabled, read-only, selection, error and source-conflict visual state. Existing focused conflict/restriction tests remain supplementary; the requested exhaustive comparison is open. |
| D01 | Failed / incomplete | [Final probe](removal-final/dpi/dpi.tsv): 10/12 checks pass at 150% <-> 125%. Selection and composition pass in both directions for both appearances; Material popup checks pass both ways. Web popup forward and return checks fail. The recorded combined predicate does not distinguish absent overlay, out-of-bounds geometry, and hit failure; cause is not established. 100% <-> 150% and visual inspection of the OS candidate window remain open. |
| E01 real IME | Blocked | The single requested [final run](removal-final/ime/ime.tsv) passed 6 checks through active Bopomofo preedit, then stopped after foreground moved away. The remaining scenarios and Material appearance were not run on this final build. Prior 46/46 evidence below is historical. |
| Clipboard | Passed in this run | [Solo stress](removal-final/clipboard/clipboard.tsv) 400/400, 100 repetitions x two appearances x ordinary and forced 60 ms contention. Prior unexplained intermittent failures remain undiagnosed; Unicode text only is restored. |

### X02 automated per-sample table

P means the listed core layout/input-seam checks passed at the given physical size and forced scale in that appearance, with both shipped and alternate IDs. The 24 native overlap/reveal checks are recorded separately in [Release CTest output](removal-final/release-tests.log). The interaction tests use fake font metrics and do not close the limits in the X02 row above.

| Page | DIP size | Scale | Web | Material | Interaction exercised |
| --- | --- | --- | --- | --- | --- |
| V01 | 960x720 | 100% | P | P | Dropdown: reveal, open, bounds, topmost hit, cancel |
| V01 | 960x720 | 150% | P | P | Dropdown: reveal, open, bounds, topmost hit, cancel |
| V01 | 640x480 | 100% | P | P | Dropdown: reveal, open, bounds, topmost hit, cancel |
| V01 | 640x480 | 150% | P | P | Dropdown: reveal, open, bounds, topmost hit, cancel |
| V02 | 960x720 | 100% | P | P | List: focus, End, nested scroll and last row visible |
| V02 | 960x720 | 150% | P | P | List: focus, End, nested scroll and last row visible |
| V02 | 640x480 | 100% | P | P | List: focus, End, nested scroll and last row visible |
| V02 | 640x480 | 150% | P | P | List: focus, End, nested scroll and last row visible |
| V03 | 960x720 | 100% | P | P | Tooltip: reveal, delay and bounds; dialog: bounds, hit, cancel |
| V03 | 960x720 | 150% | P | P | Tooltip: reveal, delay and bounds; dialog: bounds, hit, cancel |
| V03 | 640x480 | 100% | P | P | Tooltip: reveal, delay and bounds; dialog: bounds, hit, cancel |
| V03 | 640x480 | 150% | P | P | Tooltip: reveal, delay and bounds; dialog: bounds, hit, cancel |

### Final verification

- [Debug desktop](removal-final/debug.log): **7/7** CTest entries; [Release desktop](removal-final/release.log): **7/7**, including **86/86** native checks; [Release core-only](removal-final/core.log): **6/6**.
- [Release case output](removal-final/release-tests.log): components **67/67** (4,273 checks), layout **22/22** (124), list-page **6/6** (20), showcase **39/39** for each appearance and ID set (156 case executions; 13,692 cumulative checks).
- Real IME **6/7 checks passed, one blocked**; DPI **10/12 passed**; clipboard **400/400 passed**. Failed and blocked cases are not acceptance passes.

### Not completed on this build

1. Finish X04's per-component visible-versus-semantic comparison, including applicable restrictions, conflicts, modal content and actual drawn output.
2. Complete X02's uncrossed popup/scroll interactions, especially scrolling with an open popup and pointer/keyboard dismissal combinations, at each applicable size/scale/appearance.
3. Investigate the two final Web DPI popup failures, then verify real **100% <-> 150%** transitions in both directions for both appearances, including actual candidate-window position. The available run used 150% <-> 125%.
4. Rerun the full real Bopomofo IME suite with uninterrupted foreground; the required single run was blocked after six passes.
5. The historical clipboard intermittency has no established cause. User trial U01 and UIA/screen-reader and MCP adapter work remain open, as do the previously documented deferred design refactors.


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

## Not completed (historical review-fix build)

1. **S-H1 and P-B3:** Implement safe-point structural removal, bounded storage and stale-handle identity without per-removed-component tombstones; test many remove/reset cycles and reentrant callbacks. Resolve the button-specific tab insertion API as a general capability or move it to the test seam.
2. **D01:** In Windows Settings > System > Display, select one of the first two horizontally ordered displays and set Scale to 100%; leave the other at 150%. Confirm the applied values, close Settings, stop interacting, and run `build/Release/lcars_host_verify.exe --dpi --evidence DIR`. For both appearances inspect the candidate window itself while the verifier moves the composing field in each direction; check that the real candidate follows the caret. Record the OS and IME versions, enumerated DPI, all results, and restore the previous display scale. The automated candidate-position request is not visual confirmation.
3. **X02 / X04:** Cross popup and scroll interactions with the full page/size/scale matrix and complete the visible-versus-semantic restricted-state comparisons for every applicable control. The matrix above is a bounded subset.
4. **Clipboard diagnosis:** Two earlier intermittent solo failures have no demonstrated cause. A passing stress run and a bounded retry do not prove why they occurred or guarantee success during longer clipboard holds.
5. **User trial U01 and A01:** No user trial feedback was supplied; UIA/screen-reader and MCP adapters remain separate work.
6. **Deferred design smells:** The public type and dispatch refactors listed above remain for a focused change with appropriate regression coverage.
