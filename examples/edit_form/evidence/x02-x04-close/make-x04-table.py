"""Turn the native sweep's per-sample observations into a conservative Markdown inventory."""
import csv
from collections import defaultdict
from pathlib import Path

folder = Path(__file__).resolve().parent
states = ["disabled", "read_only", "selected", "candidate", "error", "conflict", "executing", "modal", "tip"]
kinds = ["text", "toggle", "button", "navigation", "label", "checkbox", "radio group", "slider", "dropdown", "list", "tabs", "progress"]
groups = defaultdict(list)
for appearance in ("web", "material"):
    with (folder / f"{appearance}-x04-sweep.tsv").open(encoding="utf-8", newline="") as file:
        for row in csv.DictReader(file, delimiter="\t"):
            groups[(appearance, row["ids"], row["component"])].append(row)

def state(rows, key):
    seen = [r[key] for r in rows if r[key] != "N/A"]
    if not seen:
        return "N/A"
    passed = seen.count("P")
    return f"O ({passed} P)" if "O" in seen else "P"

def text(rows, key):
    if all(r[key] == "N/A" for r in rows):
        return "N/A"
    seen = [r[key] for r in rows if r["visible"] == "yes" and r[key] != "N/A"]
    if not seen:
        return "O"
    return f"{seen.count('P')}/{len(seen)} observed"

lines = ["| Appearance / IDs | Component | Type | Name | Value | Restriction | Hidden | Clipped | Disabled | Read-only | Selected | Candidate | Error | Conflict | Executing | Modal | Tooltip |",
         "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
for (appearance, ids, component), rows in groups.items():
    cells = [f"{appearance} / {ids}", f"`{component}`", kinds[int(rows[0]["kind"])], text(rows, "name"), text(rows, "value"),
             state(rows, "restriction"), state(rows, "hidden"), state(rows, "clipped")]
    # A label can carry an associated field error. The current TSV does not retain its role,
    # so do not classify label error cells as inapplicable without that evidence.
    cells.extend("O" if key == "error" and kinds[int(rows[0]["kind"])] == "label" else state(rows, key) for key in states)
    lines.append("| " + " | ".join(cells) + " |")
(folder / "x04-table.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
