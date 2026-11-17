# Option focus reporting (#107)

Base: `f698871f5565466216a76a826a2737c47a2a5911`, `integration/refactor-26d` (0.4.0).

The component keeps reporting keyboard focus while its enabled focus option also reports focus.
The implemented external focus-event flow is:

```text
closed dropdown component --open--> candidate option
candidate option --arrow or focus(value)--> next enabled candidate option
candidate option --Esc or Enter--> dropdown component
```

Lists, navigation lists and tabs use their candidate; radio groups use the selected enabled
option, falling back to the first enabled option. Radio painting and arrow navigation use the
same target. A navigation candidate can receive focus without changing the selected page.
Only a change in the deepest focused node raises a focus event.

## Regression evidence

- Before the change, `option_nodes` failed at `before_layout/radio/a: option-only CommonState`
  when the expected focusability was updated to #107's rule. The updated matrix passes.
- The new focus-ring regression exposed the navigation ring remaining on the selected page
  after `focus(value)` moved its candidate. Both appearances now frame the candidate.
- `option_nodes` covers all five component kinds, no focus, selected/candidate focus, closed
  dropdowns, disabled radio selection and list cursor, and all-disabled option sets. Page
  containers reject disabling the active page, so an all-disabled page set is not reachable.
- Semantic event checks require one event per transition, including closing a dropdown,
  disabling/removing the list cursor, and disabling the last radio option. Repeated focus
  sends no event. Typed and semantic focus preserve selection and reject disabled targets;
  focusing a closed dropdown option returns `invalidValue`. An offscreen dropdown target is
  revealed after arrangement without selecting it.
- UIA23 runs the edit-form demo across processes. It checks tab/radio/dropdown option events,
  `GetFocusedElement`, focusability, Esc returning to the dropdown, and `SetFocus(v03)` keeping
  the active page unchanged. Core and UIA share the predicate in `tests/option_properties.hpp`.

## Verification (author machine, 2026-11-17)

- Release full CTest run: 70/71 passed. The remaining `component_registry` assertions still
  listed tab and navigation option verbs as `select`; only those two old expectations were
  changed to `select,focus`. Rebuilt and reran `component_registry`: PASS (all five cases).
- Targeted `components`, `edit_form`, `option_nodes`, `focus_ring`, `navigation`: 5/5 PASS.
- Required `verify-idle` selections: `uia`, `axe`, `demo-uia`, Web and Material: all six PASS.
  UIA23 passed in both appearances. No WSL process was launched during these selections.
- After review fixes: rebuilt and reran six affected core suites, 6/6 PASS; rebuilt the
  desktop host and client and reran `uia_web` / `uia_material`, 2/2 PASS. Logs:
  `.scratch/107-review-fixes-core.log`, `.scratch/107-review-fixes-uia.log`.

## Standards

- P2: `focusPage` cached a page's enabled state across synchronous blur callbacks, so a
  callback could disable the page before the candidate was assigned. Fixed by rechecking
  admission and enabled state after `focus(id)`. The new tabs/navigation regression failed
  before the fix (`tabs/b: blur-disabled focus target is rejected`).
- P3: selection preservation and closed-dropdown focusability had separate core and UIA
  assertions, contrary to `CODING_STANDARDS.md`'s shared-predicate rule. Both seams now use
  the additional predicates in `tests/option_properties.hpp`.

Two findings; worst severity P2. Both addressed. No additional actionable heuristic smells.

## Spec

- P2: the same callback issue violated decision 3 (focus options must be enabled) and
  decision 6 (disabled pages return `Status::disabled`). The post-callback admission check
  addresses both tabs and navigation lists.

One finding; worst severity P2. Addressed. No other actionable gaps or scope creep found.

## Reproduction

```powershell
./build.ps1 -Configuration Release
./verify-idle.ps1 -Only uia,axe,demo-uia -OutDir build/Release/evidence/107-idle
```

Use an idle interactive author desktop for the second command; do not launch WSL during it.
The local wrapper `.scratch/107-idle.ps1` captures source metadata through WSL before the run
and serves those cached answers to the verification script's metadata queries. It reuses an
existing Axe 2.4.2 extraction whose archive and executable hashes the verifier checks.
Raw full-suite output is `.scratch/107-full-release.log`; author-machine TSVs, client logs,
Axe artifacts and metadata are under `build/Release/evidence/107-idle/` (local, untracked).
