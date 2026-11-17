# #26 integration onto current main

Integrated reviewed fix `9db7f19ee272caa056454450169a64ae7035588b` and
acceptance evidence `f96ae19c74202e23eb33b51576faca298260cf36` with
`origin/main` at `e773b37`. Source branch: `t3/complete-issue-26-tests`.
The merge retains both histories and all original evidence unchanged.

Main already contains the structural refactor (#108) and per-host platform
accessibility configuration (#110). Overlap was limited to CMake test
registration and the UIA client. CMake merged automatically. The UIA client
conflict was resolved by retaining both readinessCase and accessibilityCase,
and both command modes, without modifying either test body or existing modes.
Core implementation, private declaration and option-node regression are
identical to the reviewed fix. No host input implementation was changed by
this integration.

## Focused integration validation (2026-11-17)

- Configured Windows Release desktop with Ninja/clang-cl through the repository
  Visual Studio environment helper.
- Built lcars_uia_client, lcars_option_nodes_tests, lcars_snapshots_tests,
  lcars_removals_tests and lcars_tests successfully.
- CTest selection `^(option_nodes|snapshots|removals|core_regressions)$`:
  **4/4 PASS**, exit 0. This selection does not launch desktop hosts.
- CTest JSON inventory retains both readiness and both accessibility cases,
  plus desktop_accessibility: **76 total registered tests**. The three extra
  accessibility cases come from current main; the original 73/73 Debug,
  73/73 Release desktop and 43/43 Release core acceptance remains evidence
  for the exact reviewed fix, not a new 76/76 integrated run.
- `git diff --cached --check` passed. Original acceptance/readiness artifacts
  and core patch were compared against f96ae19 with no differences.

The validation script and output are included beside this note. Original
runtime acceptance, including timeouts, interruption accounting and binary
identities, remains in [ticket-26-acceptance.md](ticket-26-acceptance.md).
No fresh desktop runtime tests were performed. The parent workspace and prior
evidence worktrees were not modified. Standards and Spec review of
f0e5df6..9db7f19 had zero findings as supplied by the parent; no new independent
review is claimed here. CI and final integration review remain for the parent.
