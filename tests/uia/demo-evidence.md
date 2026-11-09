# Settings demo UIA and Axe evidence

The cross-process demo fixture uses the shared settings demo logic in both appearances. Run `./verify-idle.ps1 -Only demo-uia` after a Release build. The resulting `demo-uia.tsv`, `client.log`, eight Axe logs and `.a11ytest` files are stored per appearance in the chosen output directory; the CI trial uploads them as `uia-trial-evidence`.

Local run on 2026-11-09: UA01–UA07 passed for Web and Material. AX01 passed all eight states in each appearance, with zero errors in all 16 scans. Raw local artifacts: `build/evidence/ticket69-final/demo-uia/` (generated, not checked in). The scans use Axe.Windows CLI 2.4.2, verified by the pinned archive and executable SHA-256 hashes.

Excluded rules: **none**. The generated `exclusions.txt` also records this for each appearance. CI results and trial stability remain unverified until the workflow runs.
