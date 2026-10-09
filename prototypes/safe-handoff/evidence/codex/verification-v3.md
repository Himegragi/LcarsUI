# Codex independent verification — safe handoff v3

Date: 2026-10-10 (Asia/Taipei). Throwaway prototype, not framework acceptance.

Claude Code authored the implementation and all scenario/fix changes in session `105986cc-6aed-4424-b313-870bbc3f96a0` (runs 035–037). Codex reviewed source and supplied concrete findings. Both review rounds recorded failing scenarios before fixes: v1 review failures=6, v2 review failures=5; those historical logs are superseded. No other prototype was edited.

Independent checks after run 037 completed:

- `run.ps1 -BuildOnly`: exit 0; CMake configure/build current. Output in `build-v3.log`.
- Executed `build/safe_handoff.exe --scenarios` from this evidence directory: exit 0, 21 numbered scenario groups, 67 PASS assertions, failures=0. Output in `scenarios-v3.log`.
- Reviewed source for declared C++26 mode, exception support, mutation gates, notification reservation admission and release, copied notification identities, ledger identity/generation checks, unavailable UI rejection, and unsafe-path hold behavior.
- Source search found no actual process-termination API calls in prototype source. Exit request is a model flag. No induced memory corruption, GPU failure or process-wide exception hook.

## Interactive observation

Opened a copy of the v3 executable with `--interactive`. Inspected its screenshot and accessibility state using Computer Use. The Traditional Chinese state panel, free-play controls and guided next-step button were visible and readable at the current desktop setting; this was not a DPI test.

Clicked each of Guide 1's four steps, refreshing the observed UI after every action:

1. A fault: A unavailable, B running, one pending host notice, zero delivered notices.
2. Attempt A/B actions: A refused; B value became 1; A value stayed 0.
3. Host notification: pending=0, delivered=1, host keeps process; A still unavailable. Notice credits fell from 2 to 1 (B retains its future notice eligibility).
4. Old A result: A value stayed 0, rejected-unavailable=1, applied=0, outstanding result slots fell from 2 to 1.

Reset to Guide 1's initial state and left the interactive window open for user review. This is assistant-driven UI verification, not user manual acceptance. Guides 2 and 3 were not separately clicked through; their underlying paths are covered by the CLI scenarios.

## Limits

Safety and isolation conditions are fixture declarations, not detected/proven properties of a real host. There is no real host-loop, GPU, IME/UIA integration, performance benchmark or low-memory guarantee. Notification credits bound logical admission; dynamically allocated strings/containers remain fallible. Unsafe handoff is only an out-of-scope marker with held work, not an implemented fatal path. The actual worker completes finite work and is joined cooperatively; its deadline is not forcible cancellation. Failed/missing host receiver notification disposal is explicitly a fixture choice, not a newly approved framework policy.
