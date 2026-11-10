# TD01 / RF06 — real TDR on the author machine (2026-11-09)

Machine: NVIDIA GeForce RTX 3060, driver 32.0.16.1088; Parsec Virtual Display Adapter 0.45.0.0 also present.
Windows build 26300. Displays: 150 % and 100 %. Tool: `C:\Windows\System32\dxcap.exe -forcetdr` (elevated).

| Run | Commit | Result |
| --- | --- | --- |
| 1 (17:25) | faf7785 | Not run: the elevation prompt was cancelled. |
| 2 (17:30) | faf7785 | TDR happened (nvlddmkm event 153 at 17:30:25). The Web demo did **not** show the Retry/Cancel prompt: it hung ("Not Responding", one core busy, private memory 73.4 GB). Cause: the end-of-frame loop draining `glGetError` until `GL_NO_ERROR` never ends after a context loss (`GL_CONTEXT_LOST` on every call). Fixed in dd33b85 with a renderer_failure test for it. Evidence: `web-before.png`, `web-tdr.log`. |
| 3 (17:53) | dd33b85 | Post-fix verification attempt. The forced TDR crashed the system: bugcheck 0x10E VIDEO_MEMORY_MANAGEMENT_INTERNAL (0x37, 0, 0, 0), reboot at 17:53:51, `C:\Windows\Minidump\110926-10156-01.dmp`. No result for the demo. |

User decision (2026-11-09): do not repeat the forced TDR on this machine. TD01 and RF06 stay **open as evidence gaps**: the real-TDR hang was found and fixed, but the fixed build has not been verified on a real TDR; the fix is covered only by the `context_lost` seam test.
