# UIA and Axe verification

The `uia` workflow is a non-required trial. It builds the desktop, reuses the pinned Mesa llvmpipe installer, checks renderer failure handling, preflights the interactive desktop and UIA root, then runs UIA and Axe against both appearances. Trial record at implementation: 0 of 20 first-attempt runs; stability is unverified until CI runs. Count first-attempt runs on main and PRs: require 20 consecutive passes before making this check required. If a failure cannot be reproduced locally on the same commit, record the failure, disable the trial job with its reason, and use the local fallback. The coordinator should update the issue with trial results.

For the local fallback, build Release and run `./verify-idle.ps1 -Only uia,axe` on an idle desktop. Each appearance has separate TSV, metadata and client/host logs. `axe` checks five verified showcase states per appearance, saves the `.a11ytest` and verbose output for each, and records exclusions in `exclusions.txt`; no rules are excluded. Axe.Windows CLI 2.4.2 is downloaded from the [official release](https://github.com/microsoft/axe-windows/releases/tag/v2.4.2) and its archive SHA-256 is `aeca43f41c89b3ffb1db84011539e609ecd7cb3badd6e78fada2ada327d10a64`. It is extracted without a package manager. An existing verified extraction can be supplied via `LCARS_AXE_EXE`; its adjacent archive and executable hashes are checked.

The additional idle-desktop selections are:

- `-Only uia-ime`: activate Microsoft Bopomofo, start real preedit and use a cross-process UIA client to check that SetValue is rejected, Value contains only the committed baseline, and preedit remains intact.
- `-Only uia-dpi`: move the test window from a 144 DPI monitor to a 120 DPI monitor and back; compare UIA field and first-character geometry with the host and require layout notifications.
- `-Only uia-stress`: run the full client 20 times for each appearance, with separate evidence folders and no retries or relaxed assertions.

Each formal record includes commit, dirty state, Release build, appearance, environment, case, expected, actual and PASS/FAIL/BLOCKED. Missing TSV, blocked cases, failed processes, and missing case results fail the command. The defaults remain `clipboard,ime,dpi`; dry runs only inspect existing evidence. UIA16 was removed from the specification and is not run.

## Settings demo (UA01–UA07 and AX01)

`./verify-idle.ps1 -Only demo-uia` runs the same cross-process client and eight Axe states for Web and Material. The fixture uses the shared demo logic, a deterministic 150-family font catalog, and a save callback that fails the run if discard attempts a save. It checks both modal Window patterns, window title and close behavior, navigation events, hidden cached elements, shortcut metadata and font option visibility. The CI trial runs the same entry point.

Each appearance's `demo-uia.tsv` records the UA cases and the eight AX01 scans. `exclusions.txt` records excluded Axe rules; currently none are excluded. Each state keeps the CLI log and `.a11ytest` artifact. The host checks the requested state before and after each scan. CI trial stability remains unverified until its required trial runs complete.

Shortcut metadata exposes the first registered chord for a button, including scoped shortcuts; the semantic tree is the provider's source. The UIA root still subscribes lazily on first observation.

UIA23 (`lcars_uia_client`, edit-form host) checks option focus for tabs, an open dropdown, and radio groups:
keyboard movement must raise focus events on the option returned by `GetFocusedElement`, with keyboard focusability
set. `SetFocus` on tab `v03` preserves the active page; Esc restores the dropdown component's focus event, and
closed dropdown options are not keyboard focusable. The core `option_nodes` matrix and event tests exercise the
same contract, using the shared focus predicate in `option_properties.hpp`; `focus_ring` covers disabled radio
selections and navigation candidates in both appearances. Run `verify-idle.ps1 -Only uia,axe,demo-uia` on an idle
author machine after rebuilding Release. Do not run WSL during those desktop checks.

## Platform accessibility configuration (#109)

Desktop-host initialization changes require these Windows platform runs on an idle desktop:

```powershell
./build.ps1 -Configuration Release -Tests '^(desktop_accessibility|uia_accessibility_(web|material)|uia_(web|material))$'
```

`desktop_accessibility` checks disabled Handle/semantic operations, focus/property/structure events through the host pump, keyboard and pointer input, and per-host provider exposure. Each `uia_accessibility` run exercises default-on, explicit-on, disabled, and simultaneous enabled/disabled hosts through an external UIA client. Enabled cases provide query/action/event controls; disabled cases check known LcarsUI components and their property/focus/structure events. Native system window information and events are allowed. The existing `uia_web` and `uia_material` runs guard unrelated UIA behavior and localized expectations.

These runs validate the Windows implementation; they do not establish support for another platform, runtime switching, or absence of the native window from OS accessibility tools. CI trial status and additional manual IME/DPI checks remain as documented above.
