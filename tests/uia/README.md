# UIA verification work in progress

`verify-idle.ps1` accepts `uia`, `axe`, `uia-ime`, `uia-dpi`, and `uia-stress` in addition to the existing selections. Each appearance gets its own evidence directory; stress reserves 20 independent runs. A missing TSV or a `BLOCKED` row fails the command. The default selection is unchanged.

The current UIA adapter records the existing cross-process client output and exit status, but deliberately marks UIA18 blocked: the client's UIA15 status fixture is not a renderer failure/rebuild test. The Axe, IME and DPI drivers are not implemented yet; their records explicitly say BLOCKED. Do not treat these selections as completed acceptance tests.

The official [Axe.Windows CLI 2.4.2](https://github.com/microsoft/axe-windows/releases/tag/v2.4.2) archive is pinned in `.github/scripts/install-axe.ps1`: SHA-256 `aeca43f41c89b3ffb1db84011539e609ecd7cb3badd6e78fada2ada327d10a64`. The installer verifies the archive before extraction; no package manager is used.

For each formal record preserve `metadata.txt`, the case TSV and all logs/artifacts. Record commit, Release build, appearance, environment, case, expected, actual, and PASS/FAIL/BLOCKED/not run. The CI trial remains unverified: 20 consecutive first-attempt successes are required before considering it stable; an unreproducible CI failure requires the local fallback. UIA16 is removed and is not an acceptance case.
