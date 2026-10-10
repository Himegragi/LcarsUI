# Independent verification — pause/resume companion v2

Date: 2026-10-10 (Asia/Taipei). Baseline commit: `362fdc5`. This companion model is separate from the v5 host-confirmation model; passing both suites does not prove subsystem integration.

Claude Code authored implementation, scenarios and corrections in captured session `105986cc-6aed-4424-b313-870bbc3f96a0`, runs 041–043. Codex reviewed source, requested concrete corrections, checked successful execution records/no permission denials, and ran the final artifact independently.

## Independent checks

- Build check through `run.ps1 -BuildOnly`: exit 0; output `build-resume-v2.log`.
- `safe_handoff.exe --resume-scenarios`: exit 0, 70 PASS checks, 31 invariant checks, failures=0. Raw final result: `resume-scenarios-v2.log`.
- Existing `safe_handoff.exe --scenarios`: exit 0, 146 PASS checks and 129 invariant checks unchanged. Its raw v5 evidence remained identical.
- Final binary SHA256: `46E3F1C018C835A27AA9FD9E20F90A19A2F896BFAD3A99C349C32297E6271AEF`.
- Source scan of new model/scenarios found no real process termination calls and no remaining reinterpret-as-int-array counter snapshot helper.

Verified cases include route loss between recovery slices and after the last disposition but before commit, per-apply/final condition checks, stale attempt refusal, new explicit snapshots excluding disposed results, same-name route replacement, finite arrivals outside the frozen batch, an independent B refilling result capacity, full retention recovery, source-version validation, duplicate/foreign/old-generation publication rejection, bounded result and receipt ownership, and real finite worker publication through reserved ledger slots.

Review evidence: the original route registry grew to 22 entries across 20 loss/install/resume cycles; `../resume-scenarios-v1-red-review.log` preserves that one failing assertion. v2 keeps the registry at its four-entry limit and preserves independent B. The circular free-result-slot prerequisite was removed before v1 delivery and is not represented as a reproduced v1 failure. Identity-bearing NotExecuted/Completed/Cancelled receipts were missing interfaces in v1; new v2 scenarios check them rather than inventing a prior red run for nonexistent APIs. Receipt and route limits are fixture choices.

## UI observation

Computer Use inspected the Chinese Win32 companion and operated all 12 steps of Guide 1 before the final display-only correction:

1. Losing A's route paused A, left B running and produced one NotExecuted receipt for A's queued action.
2. Two prestarted A commands completed; their results were retained, UI value stayed zero and no result capacity was freed.
3. Installing a new route alone did not resume A. Explicit resume froze two results even with all six result slots occupied.
4. One result applied (value 4, applied=1, freed=1). Route loss then invalidated the attempt; a stale commit was refused and A stayed paused.
5. New route + explicit resume created a new batch with only the remaining result. After it applied, value=5, applied=2, freed=2; final commit reopened A. No result applied twice.
6. Taking receipts returned exactly `NotExecuted#10`, `Completed#4`, `Completed#5`; receipt credits fell from 7 to 4, while the four still-running jobs retained their result reservations.

Two misleading historical-state labels were corrected by Claude without model changes: no active attempt is now labeled explicitly (old batch counters are history), and receipt UI-pending flags are labeled as settlement-time snapshots rather than current UI status. The final binary was rerun through the resume scenarios and its revised initial display was inspected. The final window was left open at Guide 1 step 1. Other guides' underlying cases were covered by CLI, not all manually clicked through. This is assistant-driven verification, not user manual acceptance or DPI/UIA conformance testing.

## Limits

No real host/GPU integration, process-fatal path, memory corruption detector, low-memory proof or performance measurement. Late results following reopen are processed in bounded normal rounds as an experimental choice, not a finalized scheduling contract. Fixed logical capacities do not guarantee allocation success or arbitrary payload memory bounds. Recovery-batch storage is reserved in the fixture; no spare result slot is required to start or commit recovery. Worker synchronization is cooperative and finite; there is no hard cancellation/latency guarantee. Receipts require caller consumption; this is in-memory delivery, not durable persistence.
