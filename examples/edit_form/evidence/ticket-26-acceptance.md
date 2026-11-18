# #26 final-fix acceptance

**Required runtime acceptance is complete at
`9db7f19ee272caa056454450169a64ae7035588b`.** Every required Debug desktop,
Release desktop and Release core test name has a completed PASS on this fix.
No production code, assertions, provider timeouts or repository test-timeout
properties were changed during this continuation. Parent integration, cumulative
final review, CI and issue closure remain separate work.

Date: 2026-11-17. Branch: `t3/complete-issue-26-tests`. The clean assigned checkout
was fast-forwarded from f0e5df6 to the reviewed fix before execution. Parent
reported Standards and Spec reviews of the fix with no findings; this report
records acceptance execution, not another code review.

## Results

| Configuration | Completed unique test names | Evidence accounting |
| --- | --- | --- |
| Debug desktop | **73/73 PASS** | 10 targeted + 2 Web fixture passes + original smoke + 41 before interruption + 17 resumed + 2 original fixture completions |
| Release desktop | **73/73 PASS** | 11 targeted + 62 remaining cases |
| Release core only | **43/43 PASS** | Fresh build and complete CTest run, exit 0, 45.09 s |

These are unions of exact-fix test-name passes across recorded serial selections,
not a claim that one new uninterrupted invocation ran each complete desktop
suite. [pass-manifest.json](ticket-26-acceptance/pass-manifest.json) maps every
name to its passing log and elapsed seconds. It reports no missing names.
Old-base results and stopped/partial cases are not counted as final-fix passes.

| Original case | Debug | Release |
| --- | --- | --- |
| uia_web | PASS 1258.64 s | PASS 63.13 s |
| uia_material | PASS 1228.86 s | PASS 63.35 s |
| uia_demo_web | PASS 289.12 s | PASS 12.70 s |
| uia_demo_material | PASS 303.95 s | PASS 12.62 s |
| host_native | PASS 491.82 s | PASS 264.56 s |
| demo_settings_smoke | PASS 16.45 s | PASS 4.66 s |

Both final Debug original UIA cases completed renderer lifecycle for handled and
unhandled paths. Their completion selection returned exit 0, 2/2 PASS, 2487.51 s.
The 17 resumed Debug cases returned exit 0, 541.39 s; the 62 remaining Release
cases returned exit 0, 528.92 s. Both native host tables contain 5,826 PASS and
164 N/A rows, no FAIL rows. Debug/Release presentation matrix and both render
comparisons also passed.

Desktop CTest count is 73: the prior 71 plus the two readiness cases added by the
fix. Release core remains 43 programs; option_nodes adds one internal regression
case. Earlier refactor case-count history and source acceptance are documented
in the prior verification report identified below.

## Execution, timeouts and interruption

Reused desktop binaries from the unchanged fix worktree:
`C:/Users/user/.t3/worktrees/LcarsUI/t3-6586e5ee-6be5a2eeeed9b7de`.
Tracked source was checked against the target before reuse and remained clean
after execution. [binary-hashes.txt](ticket-26-acceptance/binary-hashes.txt)
records the reused fixture/client/native binary identities. Release core was
built in this assigned checkout using `build.ps1 -Configuration Release
-CoreOnly -SkipTests`, then tested with the Visual Studio environment imported.
[environment.txt](ticket-26-acceptance/environment.txt) records VS
17.14.37827.9 and MSVC 14.44.35207; CI was unset.

The initial original Debug Web and Material selection ran serially with an
invocation timeout of 1200 seconds per case. Both **TIMED OUT at 1200.02 s**
after status mapping, removal and close reported PASS, before renderer lifecycle
finished. Those results remain in
[debug-uia-original.log](ticket-26-acceptance/debug-uia-original.log) and are not
counted as passes. This measured near-completion justified one later 1800-second
completion attempt, which passed both original cases. No assertions, settle
sleeps or repository timeout properties were changed to obtain green. Web
needed 58.64 seconds beyond the original bound; Material needed 28.86 seconds.
No assertion failure required a new production fix.

The turn interruption terminated the earlier runner during
presentation_matrix_native. Recovery confirmed no test process remained.
[debug-before-interruption.log](ticket-26-acceptance/debug-before-interruption.log)
preserves 41 completed passes. The interrupted matrix has no verdict and was
not counted. The resumed selection excluded all 41 completed names and all
other reused exact-fix passes, starting at the unfinished matrix. Its 17 cases
all passed; the completed matrix took 397.96 s.

Commands and exclusions are preserved in
[resume-desktop.ps1](ticket-26-acceptance/resume-desktop.ps1). Principal selections:

```powershell
# Original fixture invocation, then the single measured completion attempt:
ctest --test-dir "$fix/build/Debug" -R '^uia_(web|material)$' -j 1 --timeout 1200 --output-on-failure -V
ctest --test-dir "$fix/build/Debug" -R '^uia_(web|material)$' -j 1 --timeout 1800 --output-on-failure -V
# Remaining desktop tests use -E with the exact passed-name exclusions in the script.
ctest --test-dir build/Release-core -j 1 --timeout 1200 --output-on-failure
```

## Desktop authorization and completion

The original Debug smoke PASS was obtained by the parent under the earlier
explicit 90-second idle authorization. Initially this continuation ran only
nonactivating fixture UIA. The user then authorized idle desktop use until batch
completion (`可以，現在保持閒置`), and this continuation became the sole desktop-test
owner. Activating demo, clipboard/native and all other desktop cases ran
serially. Foreground/clipboard completion was announced when Release finished;
only nonactivating fixtures ran afterward.

The native verifier's supported clipboard **text** restoration results are
preserved in its tables. Debug Web initially reported no text saved; Material
reported restored text, and contention checks reported restoration OK. This is
not a claim that arbitrary nontext formats were restored. Demo persistence
checks use isolated test settings directories.

All desktop operations finished at **2026-11-17 21:51:30 +08:00**. A final process
inventory found **zero CTest or Lcars test processes**; see
[final-processes.json](ticket-26-acceptance/final-processes.json). The parent was
notified that normal desktop use can resume. No additional desktop work remains.

#26 explicitly makes real Bopomofo and physical DPI reruns conditional on host
input changes. This readiness fix changes semantic capture and adds regression
coverage; it contains no host-input production change. Those extra real-input
runs were therefore not required for this fix. The native CTest, original demo
UIA, simulated IME and standard keyboard cases were executed as recorded.

## Evidence and handoff

Concise source logs and the manifest are committed beside this report. Raw
LastTest logs, the Release-core build log, native host tables and copied matrix/
smoke artifacts are preserved under `.scratch/acceptance-26/` in this assigned
checkout. Original generated evidence also remains in the fix worktree. No
evidence worktree was removed. Prior baseline/source acceptance report:
`C:/Users/user/.t3/worktrees/LcarsUI/t3-9d4ea340-90ee53fb657623d3/.scratch/issue-26-verification/report.md`.

There are **no remaining runtime acceptance cases** from this assignment. The
parent retains integration, cumulative final review, CI and closure. No PR or
issue was created or updated. This continuation adds evidence only; production
and test code remain the reviewed fix.
