"""Summarize driven checks and enumerate unfinished component/state comparisons.

This deliberately does not convert observations into passes. Run after saving the final
native TSV and inventory files alongside this script.
"""
import csv
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parent
with (root / "debug-final.tsv").open(encoding="utf-8", newline="") as f:
    native = list(csv.DictReader(f, delimiter="\t"))

lines = ["| Appearance / IDs | Component | State | Result | Evidence |",
         "| --- | --- | --- | --- | --- |"]
completed = set()
failed = set()
for appearance in ("web", "material"):
    for ids, component in (("shipped", "form.name"), ("alternate", "n")):
        for state, case in (("read-only", "X04-name-read-only"), ("source conflict + latest", "X04-name-conflict")):
            rows = [r for r in native if r["appearance"] == appearance and r["case"] == case and r["evidence"].startswith(ids + " ")]
            passes = sum(r["status"] == "PASS" for r in rows)
            failures = sum(r["status"] == "FAIL" for r in rows)
            result = "F" if failures else "P" if len(rows) == 4 and passes == 4 else "O"
            reason = f"{passes}/4 size/scale native samples; {failures} failed" if len(rows) == 4 else f"only {len(rows)}/4 samples recorded"
            lines.append(f"| {appearance} / {ids} | `{component}` | {state} | {result} | {reason}; shared core driver |")
            key = (appearance, ids, component, "read_only" if state == "read-only" else "conflict")
            if result == "P": completed.add(key)
            if result == "F": failed.add(key)
(root / "x04-driven.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

pending = []
full = ["| Appearance / IDs | Component | Normal | Hidden | Clipped | Disabled | Read-only | Selected | Candidate | Error | Conflict / latest | Executing | Modal | Tooltip |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
counts = defaultdict(int)
for appearance in ("web", "material"):
    with (root / f"{appearance}-x04-sweep.tsv").open(encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    groups = defaultdict(list)
    for row in rows:
        groups[(row["ids"], row["component"])].append(row)
    for (ids, component), observations in groups.items():
        pending.append((appearance, ids, component, "normal", "not driven and fully compared at both seams"))
        cells = ["O"]
        counts["O"] += 1
        for state in ("hidden", "clipped", "disabled", "read_only", "selected", "candidate", "error", "conflict", "executing", "modal", "tip"):
            if all(row[state] == "N/A" for row in observations):
                status = "N/A"
            elif (appearance, ids, component, state) in completed:
                status = "P"
            elif (appearance, ids, component, state) in failed:
                status = "F"
                pending.append((appearance, ids, component, state, "native state check failed; inspect debug-final.tsv"))
            else:
                status = "O"
                pending.append((appearance, ids, component, state, "driver or complete native/core comparison not implemented"))
            cells.append(status)
            counts[status] += 1
        full.append("| " + " | ".join([f"{appearance} / {ids}", f"`{component}`", *cells]) + " |")
(root / "x04-table.md").write_text("\n".join(full) + "\n", encoding="utf-8")
with (root / "x04-open.tsv").open("w", encoding="utf-8", newline="") as f:
    writer = csv.writer(f, delimiter="\t", lineterminator="\n")
    writer.writerow(("appearance", "ids", "component", "state", "reason"))
    writer.writerows(pending)
print(f"X04 aggregate cells: P={counts['P']}, F={counts['F']}, N/A={counts['N/A']}, open={counts['O']}")
