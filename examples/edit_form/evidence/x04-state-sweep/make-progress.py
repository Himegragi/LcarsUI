"""Summarize driven checks and enumerate unfinished component/state comparisons.

A pass requires the core suite, exact client sizes, and four native samples in both
Debug and Release. Save the final reports and inventories alongside this script.
"""
import csv
from collections import defaultdict
from pathlib import Path
import re

root = Path(__file__).resolve().parent
core_log = root / "generic-core.log"
core_ok = core_log.exists() and "100% tests passed" in core_log.read_text(encoding="utf-8") and "edit_form" in core_log.read_text(encoding="utf-8")
generic_file = root / "generic-debug.tsv"
native_file = generic_file if generic_file.exists() else root / "debug-final.tsv"
with native_file.open(encoding="utf-8", newline="") as f:
    native = list(csv.DictReader(f, delimiter="\t"))
release_file = root / "generic-release.tsv"
with release_file.open(encoding="utf-8", newline="") as f:
    release = list(csv.DictReader(f, delimiter="\t"))

required_sizes = {(size, scale) for size in ("960x720", "640x480") for scale in ("100", "150")}
def collect_sizes(rows):
    result = defaultdict(dict)
    for row in rows:
        if row["case"] != "X04-client-size":
            continue
        match = re.match(r"^(shipped|alternate) (960x720|640x480) (100|150)%:", row["evidence"])
        if match:
            ids, size, scale = match.groups()
            result[(row["appearance"], ids)].setdefault((size, scale), []).append(row["status"])
    return result
size_samples, release_sizes = collect_sizes(native), collect_sizes(release)
def complete_pass(observed):
    return set(observed) == required_sizes and all(statuses == ["PASS"] for statuses in observed.values())
def sizes_verified(appearance, ids):
    return complete_pass(size_samples[(appearance, ids)]) and complete_pass(release_sizes[(appearance, ids)])

lines = ["| Appearance / IDs | Component | State | Result | Evidence |",
         "| --- | --- | --- | --- | --- |"]
completed = set()
failed = set()
for appearance in ("web", "material"):
    for ids, component in (("shipped", "form.name"), ("alternate", "n")):
        for state, case in (("read-only", "X04-name-read-only"), ("source conflict + latest", "X04-name-conflict")):
            rows = [r for r in native if r["appearance"] == appearance and r["case"] == case and r["evidence"].startswith(ids + " ")]
            passes = sum(r["status"] == "PASS" for r in rows)
            release_rows = [r for r in release if r["appearance"] == appearance and r["case"] == case and r["evidence"].startswith(ids + " ")]
            release_passes = sum(r["status"] == "PASS" for r in release_rows)
            failures = sum(r["status"] == "FAIL" for r in rows + release_rows)
            def dimensions(sample_rows):
                return {match.groups() for row in sample_rows if (match := re.match(r"^(?:shipped|alternate) (960x720|640x480) (100|150)%", row["evidence"]))}
            complete = (len(rows) == 4 and passes == 4 and dimensions(rows) == required_sizes and
                        len(release_rows) == 4 and release_passes == 4 and dimensions(release_rows) == required_sizes)
            result = "F" if failures else "P" if core_ok and sizes_verified(appearance, ids) and complete else "O"
            reason = f"Debug {passes}/4; Release {release_passes}/4; {failures} failed"
            lines.append(f"| {appearance} / {ids} | `{component}` | {state} | {result} | {reason}; shared core driver |")
            key = (appearance, ids, component, "read_only" if state == "read-only" else "conflict")
            if result == "P": completed.add(key)
            if result == "F": failed.add(key)
# Generic rows are only credited when the core test suite also passed.  Require the
# exact four distinct size/scale samples, rather than accepting duplicate rows.
case_states = {"X04-normal-member": "normal", "X04-executing-member": "executing", "X04-clipped-member": "clipped", "X04-error-member": "error", "X04-candidate-member": "candidate", "X04-search-read-only": "read_only", "X04-modal-member": "modal", "X04-tip-member": "tip", "X04-hidden-member": "hidden", "X04-selected-member": "selected", "X04-disabled-member": "disabled", "X04-conflict-member": "conflict"}
open_samples = defaultdict(list)
def collect_samples(rows, build):
    samples, restores = defaultdict(dict), defaultdict(dict)
    for row in rows:
        if row["case"] == "X04-executing-restore":
            match = re.match(r"^(shipped|alternate) (960x720|640x480) (100|150)%:", row["evidence"])
            if match:
                ids, size, scale = match.groups()
                restores[(row["appearance"], ids)].setdefault((size, scale), []).append(row["status"])
            continue
        state = case_states.get(row["case"])
        match = re.match(r"^(shipped|alternate) (960x720|640x480) (100|150)% ([^: ]+):", row["evidence"])
        if not state or not match:
            continue
        ids, size, scale, component = match.groups()
        key = (row["appearance"], ids, component, state)
        samples[key].setdefault((size, scale), []).append(row["status"])
        if row["status"] == "OPEN":
            open_samples[key].append(f"{build} {size} {scale}%: {row['evidence'].split(': ', 1)[-1]}")
    return samples, restores
samples, restore_samples = collect_samples(native, "Debug")
release_samples, release_restores = collect_samples(release, "Release")
for key in sorted(samples.keys() | release_samples.keys()):
    observed, released = samples[key], release_samples[key]
    all_statuses = [status for group in (observed, released) for statuses in group.values() for status in statuses]
    restored, release_restored = restore_samples[(key[0], key[1])], release_restores[(key[0], key[1])]
    restore_ok = key[3] != "executing" or (complete_pass(restored) and complete_pass(release_restored))
    failed_sample = any(status == "FAIL" for status in all_statuses)
    if key[3] == "executing":
        failed_sample = failed_sample or any(status == "FAIL" for group in (restored, release_restored) for statuses in group.values() for status in statuses)
    passed = core_ok and sizes_verified(key[0], key[1]) and restore_ok and complete_pass(observed) and complete_pass(released)
    result = "F" if failed_sample else "P" if passed else "O"
    if result == "F": failed.add(key)
    if result == "P": completed.add(key)
    debug_passes = sum(status == "PASS" for statuses in observed.values() for status in statuses)
    release_passes = sum(status == "PASS" for statuses in released.values() for status in statuses)
    lines.append(f"| {key[0]} / {key[1]} | `{key[2]}` | {key[3]} | {result} | Debug {debug_passes}/4; Release {release_passes}/4; core {'passed' if core_ok else 'not verified'} |")
(root / "x04-driven.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

pending = []
full = ["| Appearance / IDs | Component | Normal | Hidden | Clipped | Disabled | Read-only | Selected | Candidate | Error | Conflict / latest | Executing | Modal | Tooltip |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
counts = defaultdict(int)
state_counts = defaultdict(lambda: defaultdict(int))
for appearance in ("web", "material"):
    latest_inventory = root / f"generic-{appearance}-x04-sweep.tsv"
    inventory_file = latest_inventory if latest_inventory.exists() else root / f"{appearance}-x04-sweep.tsv"
    with inventory_file.open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    groups = defaultdict(list)
    for row in rows:
        groups[(row["ids"], row["component"])].append(row)
    for (ids, component), observations in groups.items():
        normal_key = (appearance, ids, component, "normal")
        normal = "P" if normal_key in completed else "F" if normal_key in failed else "O"
        if normal != "P":
            pending.append((appearance, ids, component, "normal", "normal visible semantics/paint comparison failed" if normal == "F" else "normal driver lacks four passing samples in each native build or a passing core sweep"))
        cells = [normal]
        counts[normal] += 1
        state_counts["normal"][normal] += 1
        for state in ("hidden", "clipped", "disabled", "read_only", "selected", "candidate", "error", "conflict", "executing", "modal", "tip"):
            if all(row[state] == "N/A" for row in observations):
                status = "N/A"
            elif (appearance, ids, component, state) in completed:
                status = "P"
            elif (appearance, ids, component, state) in failed:
                status = "F"
                pending.append((appearance, ids, component, state, "native state check failed; inspect generic-debug.tsv and generic-release.tsv"))
            else:
                status = "O"
                reasons = {
                    "hidden": "inactive fixed-page comparisons lack four passing samples in each native build or a passing core sweep",
                    "clipped": "fully and partially clipped semantics, paint and hit checks lack a complete passing sample set at both seams",
                    "disabled": "disabled/recovery driver lacks four passing samples in each native build or a passing core sweep",
                    "read_only": "read-only state, rejection and cue not driven at both seams",
                    "selected": "selection driver lacks four passing samples in each native build or a passing core sweep",
                    "candidate": "candidate state and its painted cue not driven at both seams",
                    "error": "error state, association and cue not driven at both seams",
                    "conflict": "source conflict and latest-action comparison not driven at both seams",
                    "executing": "callback-time executing comparison and safe restoration of the application's existing bound action are not implemented at both seams",
                    "modal": "modal member state and clipped paint not driven at both seams",
                    "tip": "tooltip state and clipped paint not driven at both seams",
                }
                reason = reasons[state]
                key = (appearance, ids, component, state)
                if key in open_samples:
                    reason = "native sample remained open: " + "; ".join(open_samples[key])
                if state == "hidden" and ".dialog." in component:
                    reason = "dialog-only member has no inactive tab-page membership; closed dialog admission is disabled, not hidden; outcome requires clarification"
                elif state == "clipped" and component.endswith(".tabs"):
                    reason = "fixed tab header has no scroll region; how to drive it fully clipped in the assembled page needs clarification"
                elif state == "clipped" and ".dialog." in component:
                    reason = "modal member is in a fixed overlay without its own scroll region; a clipped fixture has not been established"
                elif state == "clipped" and ".v03." in component and key not in open_samples:
                    reason = "V03's large viewport has no baseline overflow; the expanded model fixture lacks a complete passing sample set"
                elif state == "error" and component in ("form.name.error", "n.e"):
                    reason = "associated error label has not been driven through its field error and compared at both seams"
                elif state == "error" and observations[0]["kind"] == "4":
                    reason = "label type declares error, but only associated error-role labels carry field errors and LabelHandle has no error setter; applicability or API requires clarification"
                elif state == "error" and observations[0]["kind"] == "10":
                    reason = "invalid tab marker and member error association are not driven at both seams"
                elif state == "executing" and component in ("form.apply", "go"):
                    reason = "confirmation button rejects ButtonHandle.bind; callback-time observation and action restoration need a supported route"
                elif state == "conflict" and component.endswith(".search"):
                    reason = "search source has no application update hook; conflict transition and latest-action policy remain unverified"
                pending.append((appearance, ids, component, state, reason))
            cells.append(status)
            counts[status] += 1
            state_counts[state][status] += 1
        full.append("| " + " | ".join([f"{appearance} / {ids}", f"`{component}`", *cells]) + " |")
(root / "x04-table.md").write_text("\n".join(full) + "\n", encoding="utf-8")
with (root / "x04-open.tsv").open("w", encoding="utf-8", newline="") as f:
    writer = csv.writer(f, delimiter="\t", lineterminator="\n")
    writer.writerow(("appearance", "ids", "component", "state", "reason"))
    writer.writerows(pending)
print(f"X04 aggregate cells: P={counts['P']}, F={counts['F']}, N/A={counts['N/A']}, open={counts['O']}")

for state in ("normal", "hidden", "clipped", "disabled", "read_only", "selected", "candidate", "error", "conflict", "executing", "modal", "tip"):
    c = state_counts[state]
    print(f"{state}: P={c['P']} F={c['F']} N/A={c['N/A']} open={c['O']}")
