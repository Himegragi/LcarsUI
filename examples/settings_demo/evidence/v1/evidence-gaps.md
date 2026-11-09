# Evidence gaps

These gaps are not evidence of a pass. Record actual machines and results when available.

| Gap | Decision | Available evidence | Unverified risk | How to close |
| --- | --- | --- | --- | --- |
| Windows 10 22H2 build 19045 | #11 Q5, Q9 | Pending author-machine and CI records | Font and DirectWrite differences | Run the full acceptance matrix on Windows 10. |
| Intel GPU | #11 Q9 | Pending NVIDIA and llvmpipe records | Driver-specific rendering and recovery | Run real GPU comparisons, performance and recovery on Intel. |
| AMD GPU | #11 Q9 | Pending NVIDIA and llvmpipe records | Driver-specific rendering and recovery | Run real GPU comparisons, performance and recovery on AMD. |
| Real 4K display | #11 Q9, Q12 | 200% offscreen fallback may be used | Fallback excludes compositor cost | Repeat on a physical 4K monitor. |
| Narrator | #11 Q13 | UIA and Axe records pending | Screen-reader usability | Optional user trial with Narrator. |
| High contrast | #11 Q8 | Out of v1 scope | Unverified accessibility | Test in a later phase. |
| UIA CI stability | Spec 3 decision 6 | Pending 20-run evidence | Intermittent failures | Record 20 consecutive CI runs. |
| Font differences | #11 Q4, Q16 | CPU and GPU compared on each machine | CI and author fonts differ | Record font inventory and compare on both machines. |
| Blocked upstream cases | Specs 1–3, 5 | Pending full review | Missing acceptance evidence | List each blocked case and evidence here after reviewing upstream reports. |
