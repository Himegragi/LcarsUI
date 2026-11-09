# v1 completion report

Status: **pending**. Do not declare v1 complete while any required row is pending or failed. Evidence must identify commit, configuration, machine, appearance and result.

| Acceptance group | Cases | Status | Evidence path |
| --- | --- | --- | --- |
| Demo behavior | NL, MD, KS, IT, HS, ST, AP, DD, AB | pending | pending |
| Close requests | CR01–CR15 including real entry points and IME | real SendInput and IME pending | idle evidence real-close/real-close.tsv |
| Keyboard walkthrough | KW01, KW02 | KW01 uses Space in step 6 as directed; CI and KW02 pending | build/Release/Testing/Temporary/LastTest.log |
| Screenshot comparison | VC01 llvmpipe and real GPU | author GPU pending; CI pending | build/Release/evidence/demo-render/demo-render-compare.tsv |
| Semantics and accessibility | SX01, UA01–UA07, AX01 | pending | pending |
| Performance | PF01–PF05 | idle measurement pending; startup smoke only | build/Release/evidence/demo-perf-smoke/demo-perf.tsv; idle evidence demo-perf/summary.tsv |
| Diagnostics | ND01, TD01, RD01 | pending | pending |
| Upstream specifications | Specs 1–3 and 5 completion criteria | pending | pending |
| Evidence gaps | EG01 | pending review | evidence-gaps.md |
| User trial | UT01, both appearances | pending | trial-template.md |

RF06, DG05, GC01 real GPU and PM01–PM05 also require author-machine evidence. Record every unrun or blocked case explicitly.
