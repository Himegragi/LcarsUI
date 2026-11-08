# AU01 package verification

- Date and location: 2026-11-09, author Windows 11 Pro N build 26300, x64.
- Source: integration/v1 5a2b369 plus documentation commit 304cbc1.
- Framework: full Debug and Release, Ninja, clang-cl 19.1.5, Visual Studio 2022 17.14.37827.9, MSVC toolset 14.44.35207; default dynamic MSVC runtime.
- Prefix: both configurations installed into `build/package-au01`. Matrix command: `tests/package/verify-package.ps1 -Prefix build/package-au01 -Configurations Debug,Release -Generators Ninja,VS2022 -Evidence build/evidence/au01`.
- Expected: no FAIL; Debug Desktop consumer selects debug FreeType and HarfBuzz. Actual: 32 PASS, one N/A (missing Desktop is inapplicable to the full prefix), zero FAIL. See results.tsv and case logs in logs.zip. 001.log records the Debug link to `lcars_freetyped.lib` and `lcars_harfbuzzd.lib` and the successful consumer tests.
- MIT license: source and installed `share/doc/LcarsUI/LICENSE` SHA256 both `c16d0fc9b1ae8340b30af933e743c81692e41c044f8aefe08f42f807e372131e`.
- PK01/PK03: `tests/package/install.cmake` expects the six Core headers and four Desktop/Diagnostics headers in the specification's public-header table, plus the matching public and internal libraries. UIA is built into Desktop; draw lists and UIA internals are not installed. Core and full `package_install` tests exercise the exact lists.
- Appearance comparison: not applicable to documentation and packaging; the same consumer and package cases are used for both appearance components.
- CI-only verification and idle-desktop clipboard, IME, DPI, rendering, performance, presentation and UIA checks are not claimed by this record; they require their respective CI or coordinator runs.
- Core-only prefix matrix: 27 PASS, zero FAIL; `../core/results.tsv` and its logs.zip record both configurations, both generators, and the expected missing-Desktop rejection.
- CTest: full Release 34/34; core-only Release and Debug 19/19 each. Full Debug `host_native` timed out after 900 s on the shared desktop; coordinator must rerun on an idle desktop. The other 33 Debug cases passed, including UIA, presentation and render comparison.
- After merging integration/v1 5f54d29 (merge db56e392), both full configurations were rebuilt and installed to the same prefix. The full matrix was rerun: 32 PASS, one N/A, zero FAIL; see ../merged/results.tsv and logs.zip. Debug was rebuilt after the merge, but its full CTest run above predates it. Release core CTest after merge: 19/19 PASS.
- Post-merge full Release CTest: 34/34 PASS, including host_native in 372 s and both appearance UIA cases.
