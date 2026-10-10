# Independent verification — integrated custody and pause/recovery prototype

Date: 2026-10-10 (Asia/Taipei). Baseline: c373878. Artifact: ../../integrated.html, disposable HTML logic model v3 with final isolation fix. No production framework source changed.

Claude Code authored implementation and corrections in session 105986cc-6aed-4424-b313-870bbc3f96a0, runs 048–051. Codex scoped, reviewed, reproduced counterexamples, and independently executed the final model. All four worker result events: success, is_error=false, no permission denials. Worker had no shell execution tools; its static review claims were not treated as execution evidence.

## Final observed checks

Node v24.18.0 extracted the DOM-free prototype-model script into a fresh vm context and ran IntegrationPrototype.runGuides(). Each model action invokes invariants; the page uses these same action objects and core. No DOM UI walkthrough was executed.

| Guide | Actions | Outcome checks | Failed outcomes | Invariant errors |
| --- | ---: | ---: | ---: | ---: |
| Last UI closes | 27 | 16 | 0 | 0 |
| Shared unsafe resource / explicit isolation | 33 | 10 | 0 | 0 |
| Ordinary/control/retry saturation | 53 | 8 | 0 | 0 |
| Cancel/commit and slot reuse | 24 | 7 | 0 | 0 |
| Counterexamples and boundaries | 83 | 21 | 0 | 0 |
| Late completions and unsafe idle closure | 33 | 10 | 0 | 0 |
| Total | 253 | 72 | 0 | 0 |

Full per-action observations: guides-final.json. Both inline scripts also compiled successfully via vm.Script (syntax only, shell DOM behavior unverified).

Independent probes-final.log: 14 PASS checks, failures=0. Verified all idle enable reservations returned on safe closure, duplicate close no extra return; unsafe idle retains scope/credit until declared safe; route/service floods bounded; both control and sender staging full explicitly report unretained; stopped pump cannot start/close; foreign ACK/callback/work/lease controls don't change true obligations; valid then duplicate completion releases once; new resource alone cannot establish isolation; declared independent B resumes while A handoff pending; receipt capacity is reserved before worker starts and explicit collection returns it.

Final HTML SHA256: 68AF0F812DA38147B95307EB7BC039B40507C0B823A6B822F10B476BB58B8667.

## Review evidence and corrections

Initial four guides passed but independent v1 probes exposed actual failures: 3 idle-close handoff reservations never returned; 20 unused route installs produced 23 entries; control4+retry8 overflow falsely claimed the thirteenth message was retained; command start still mutated state after pump stop; a foreign callbackEnd ended the real callback. review-v1-red.log preserves these observations. Further source review required per-request terminal receipts, fault invalidation/NotExecuted handling, and refusing reassignment after valid ACK.

Later review found unsafe idle finalization, missing bounded normal processing for completions excluded from a fixed recovery batch, and recovery permitted after resource replacement without declared isolation proof. Claude corrected these and added guide coverage. One caller probe initially assumed assignments were strings rather than snapshot objects and threw TypeError after eight successful probes; the corrected complete probe rerun passed all fourteen. This was an inspection-script error, not a model failure.

## Boundaries

This is a newly integrated JavaScript fixture representing both contract sets. It does NOT merge or compile the existing native handoff/resume models. Their old passing results were not rerun or relabeled as integration evidence. Fixed custody Q133/Q134, identity checking and scope/payload/receipt separation are represented; cross-custody migration is absent.

Browser security policy blocked navigating to a local file URL. No alternate browser/server route was attempted; no browser interaction, screenshot, user manual acceptance, or rendered layout verification is claimed. User may open the standalone HTML themselves; no server or dependencies required.

Owner-thread, pump, callbacks, resource safety and isolation are deterministic fixture declarations/actions, not real OS threads, C++ lifetime proof, host loop integration, GPU handling, unsafe callback preemption, corruption detection, low-memory behavior, performance or universal fairness proof. No external-effect exactly-once promise, crash persistence, termination path, or recovery from actual unsafe state. Sender retry tickets, receipt collection, registry sizes, budgets and version representation are experimental arrangements, not public API choices. External data version change is not exercised. Actual per-request receipts are bounded and retained until explicit collection. Controls rejected by both queues remain caller responsibility; no claim they are always admitted.
