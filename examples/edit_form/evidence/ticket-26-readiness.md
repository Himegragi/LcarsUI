# #26 Debug UIA readiness implementation

Base: integration/refactor-26d, f0e5df6490baaa0d605f21e469aff06e6deefc93.
Date: 2026-11-17. All new binaries were built in this worktree.

## Finding and change

Semantic event capture rebuilt a component's complete option collection for every
option, then rebuilt it again while asking each leaf for children. With a UIA
observer attached, focus and other operations capture before/after trees. This
made large hidden collections expensive even when the action targeted an
unrelated field. The settings fixture has 150 font options.

The fix shares one owner description among its option nodes during a single
semantic capture and does not traverse option/menu-item leaves. The description
is local to that capture, not a persistent cache. Normal SemanticTree::get and
capture use the same private node builder. No public or test-only API was added;
provider timeouts, original assertions and the original smoke are unchanged.

A host transaction-batching experiment improved the narrow fixture but was
insufficient for the broader UIA case. It was removed. No desktop-host or layout
production changes remain in the final patch.

## Red/green evidence

Safe reproduction (no foreground activation, injected input or clipboard):

    build/Debug/lcars_uia_client.exe build/Debug/lcars_uia_demo_host.exe web --readiness

The original implementation failed twice: SetFocus returned 0x80131505; the
following ValuePattern request took 1078/1094 ms. This is a provider readiness
failure, not a reproduction of the .NET smoke's exact Unsupported Pattern text.
The smaller edit-form fixture passed, helping isolate the collection-dependent
cost. Temporary snapshot timings showed about 117–126 ms per 272-node capture.

Ranked hypotheses were repeated semantic capture, layout/painting CPU work, then
UIA event-worker blocking. The core reproduction isolated capture without any
Windows host: focus a button while observing a separate 64-option list and count
public Source reads. A generous constant budget (32) failed before the fix:

    build/Debug/lcars_option_nodes_tests.exe
    FAIL snapshot_read_budget: semantic capture reads a collection a bounded number of times
    option_nodes: 14/15 cases, 3327 checks

After the fix: 15/15 cases, 3329 checks. The regression also requires delivery of
the focus event and preservation of the list's selected option. Existing option,
snapshot, menu and removal tests cover the unchanged node fields and lifetimes.
Logs: .scratch/core-red.log and .scratch/core-green.log.

Final Debug targeted CTest: 10/10 PASS (9.09 s), including sem01,
core_regressions, layout, handles, snapshots, option_nodes, removals,
uia_unobserved_guard and both new readiness tests. Readiness completed in
2.78/2.71 seconds; a subsequent run completed in 2.68/2.70 seconds.
One recorded Material sequence: focus 453 ms, immediate pattern 156 ms;
subsequent patterns 0 ms. No settle sleep was added.
Logs: .scratch/core-fix-debug.log, .scratch/core-fix-uia-partial.log.

Final Release targeted CTest: 11/11 PASS (2.60 s), the same checks plus
context_menu. Both Debug readiness tests then passed three consecutive times
each (six runs, 16.35 s total). Logs: .scratch/core-fix-release.log and
.scratch/core-fix-debug-repeat.log.

## Remaining acceptance

This is not full #26 acceptance. The original Debug uia_web still hit the
180-second bound, now progressing through list HelpText, disabled-row and list
focus assertions (all PASS). The subsequent partial run passed Web simulated
IME and global-event fixtures. Material was deliberately stopped when that run
was ended, not counted as pass. No 600-second retry or timeout increase was used.
Logs: .scratch/uia-core-fix-debug.log.tmp6fb3b and
.scratch/core-fix-uia-partial.log. The earlier host-only experiment's failed and
stopped cases are in .scratch/uia-final-debug.log; they are not final-patch passes.

The unchanged demo_settings_smoke was NOT run: it launches the actual activating
demo, focuses fields, edits a scratch settings value, saves, closes and reloads.
The assignment explicitly prohibits foreground/input/clipboard tests without
session authorization. README.md lines 333–336 separately explain the project's
idle-desktop platform verification convention; this core-only patch does not
invent an additional host-change approval requirement.

The exact bounded desktop operations still requested are one unchanged Debug
CTest `demo_settings_smoke` with a 90-second bound: launch Web then Material,
UIA-focus general.name, set its scratch value, focus app.nav, invoke app.save,
close and relaunch to verify persistence. No real settings or clipboard are
needed. Command, once that session is authorized:

    ctest --test-dir build/Debug -R '^demo_settings_smoke$' --timeout 90 --output-on-failure

Further Debug fixture performance/acceptance and the previously deferred native
and demo UIA cases remain with the parent. No issue was closed and no PR created.

## History and scope

Read the complete prior verification report and its focused failure logs.
Read-only GitHub compare confirmed remote main e773b37 contains the base; its
only desktop-host production difference is #109's platform accessibility option,
not a readiness/capture fix. Parent workspace and old evidence worktrees were
not modified or removed. CTest gains two programs (71 to 73); option_nodes gains
one internal regression case. Temporary tagged instrumentation was removed.
