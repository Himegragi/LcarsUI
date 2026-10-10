# Independent verification — explicit confirmation and reassignment v5

Date: 2026-10-10 (Asia/Taipei). Baseline: prototype commit `2c6c022` (v3). This is a disposable logic model, not framework acceptance.

Claude Code authored all implementation, scenario and fix changes using captured session `105986cc-6aed-4424-b313-870bbc3f96a0`, runs 038–040. Codex reviewed source and supplied concrete findings. Exit records were successful and reported no permission denials. v3 evidence is historical and does not validate the new confirmation contract.

## Independent checks

- `run.ps1 -BuildOnly`: exit 0, CMake configure/build current; output `build-v5.log`.
- Executed the final `build/safe_handoff.exe --scenarios` from this directory: exit 0, 146 PASS assertions, failures=0, 129 capacity-equation checks. Raw result: `scenarios-v5.log`.
- Scenario labels run through 33; label 16 is a historical replacement marker whose actual coverage moved to scenario 26. Diagnostic scenario 5 also has three modes. Assertion counts above are literal observed results, not inflated by labels.
- Final executable SHA256: `B1B9533DA3F583B86FC1998A5888FD1284A63571848AC2E290EC0F9FD4F6AD65`.
- Reviewed explicit confirmation and assignment identities, immediate revocation on accepted reassignment, cancellation/activation ordering, delayed release after callback return, retained confirmation after callback failure, stale/foreign/duplicate refusal, candidate validity checks, bounded tracked-work identity and handoff slot reuse.
- Source scan found no actual process-termination API calls in prototype C++ source. No fatal-path execution, induced corruption or GPU failure.

The worker's review reproduction `../scenarios-v4-red-review.log` contained 14 failing checks before fixes. v5 corrects duplicate completion of one of two tracked jobs, unsafe post-callback continuation before/after confirmation, wrong-thread reassignment counter mutation, empty receiving callbacks, and repeated service-fault reports when the handoff table is full. Historical red results were preserved, not overwritten with passing output.

## UI observations

Computer Use inspected the v5 Win32 window and operated all four steps of Guide 1:

1. A fault created one pending handoff; capacity remained 2 (one running B + one handoff).
2. Host callback returned without confirmation: handoff AwaitingAck, confirmed=0, released=0, capacity still 2.
3. Delayed explicit confirmation: confirmed=1, released=1, capacity=1, A remained unavailable.
4. Duplicate confirmation: rejected count increased; confirmed/released stayed 1 and capacity stayed 1.

A display-only review then changed `可生效` to the precise `舊接管工作已結束` and shortened the sixth guide button. The model/scenarios did not change. The final executable was rerun through all CLI scenarios. Its window was reopened, the corrected label and guide button were verified, and it was reset to Guide 1's initial state and left open. Other guides' underlying paths were checked by CLI; they were not all manually clicked through. These are assistant-driven checks, not user manual acceptance or a DPI/UIA conformance test.

## Limits

- Safety/isolation flags are fixture inputs. There is no real host-loop/GPU integration, corruption detection, low-memory or performance validation.
- Fixed credit and table counts bound logical admission, not dynamic string/container allocation failures or entire process memory.
- Unsafe-path marking holds work and capacity; no fatal recovery/termination mechanism is implemented.
- Owner-thread control messages come from finite cooperative workers. The wait/join is not a hard cancellation guarantee.
- Complete receiver shutdown, arbitrary host effects and production resource ownership remain outside this model. Fixed capacities and C++ types are experimental.
