# Independent verification — recovery cancellation companion v4

Date: 2026-10-10 (Asia/Taipei). Baseline: `a29ddad`. Still a disposable companion model, separate from the v5 host-confirmation state machine.

Claude Code authored implementation/scenarios/fixes in session `105986cc-6aed-4424-b313-870bbc3f96a0`, runs 044–045. Codex reviewed source, requested bounded control processing, inspected successful execution records with no permission denials, and independently ran the final executable.

## Checks

- `run.ps1 -BuildOnly`: exit 0, CMake configure/build current, output `build-resume-v4.log`.
- `safe_handoff.exe --resume-scenarios`: exit 0, 117 PASS assertions, 73 invariant checks, failures=0. Raw output `resume-scenarios-v4.log`.
- Existing `safe_handoff.exe --scenarios`: exit 0, 146 PASS assertions, 129 invariant checks unchanged; raw v5 evidence remained identical.
- Final executable SHA256: `04BDB19875BC9015BEC16C3E5C61FC16F93D0B253BC7057F965A587F424AA0CA`.

Text-log trailing whitespace and line endings were normalized for repository checks; result text and assertion outcomes are unchanged.

Verified: cancel-first versus commit-first after batch completion; mixed Applied/NotApplicable/pending results followed by cancellation, late arrival and fresh resume; repeated/old/foreign/unknown cancellation identities; independent B cancellation; off-owner refusal; empty/full-result-capacity boundaries; 30 cancel/retry cycles with bounded outcome history; route invalidation and unsuccessful commits not masquerading as successful commit; finite worker posting controlled FIFO order; per-call control budget, zero-budget no-op, queued remainder/order, identity-bearing outcomes and no duplicate state transition/application/release.

The worker preserved `../resume-scenarios-v3-red-review.log`: one failed check showed v3 drained four queued messages in one call, exceeding the newly selected fixture per-call budget of two. This finite check does not reproduce an infinite concurrent producer. Source inspection identified that draining until empty had no independent work budget; v4 limits each call, retains accepted remainder and correlates each outcome to the attempt id. v3 passing evidence remains historical.

## UI observation and limit

Computer Use located and inspected the v4 native window. All six guide buttons, Cancel Recovery, state/history labels and counters were visible. Selected Guide 4 (cancel-first) and observed its initial step 1/10. The next click was blocked because Windows Task View covered the target. This was the second such interference after an earlier successful refresh/retry; automation was stopped to avoid further desktop interference. No cancel/commit walkthrough is claimed as manually exercised in this run. The window was left open, last observed at Guide 4's first step. Automated model cases above cover both orders and cancel/retry; this is not user manual acceptance.

## Limits

Cancellation occurs at modeled owner-thread boundaries between items/slices, not preemption of an executing callback. Outcome history retains eight recent attempts in this fixture; reclaimed ids safely report Stale rather than promising indefinite TooLate/AlreadyEnded classification. The control mailbox and default budget are experimental sizes, not a general scheduler or timing guarantee. No continuous simultaneous producer/drainer stress or real-host/GPU/low-memory/performance integration was performed. The v5 and pause/resume models remain separate.
