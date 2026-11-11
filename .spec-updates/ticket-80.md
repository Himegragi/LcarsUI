# Spec updates for #80 (文字對齊與行距)

Target: `docs/specs/v1-semantic-layer-and-uia.md` (規格 3). The planning docs are untracked; the coordinator applies these.

## 1. Verb enum (around line 117)

Old: the `Verb` list ends with `... undo, redo, copy, cut, paste` (later tickets add `confirm`, `scroll`).
New: append `set_text_align, set_line_spacing, set_paragraph_spacing`. `Argument` gains `TextAlign` (left / center / right) and `double`.

## 2. Verb table (動詞對照表, around lines 173-184)

C03 文字欄位 row, verbs column: add `set_text_align(TextAlign)`、`set_line_spacing(double)`、`set_paragraph_spacing(int)`; handle column: `TextHandle` 同名方法（`set_text_align` 等）.

Last row old: `| C01、C11、C12 | 標籤、提示框、進度條 | 無動詞（唯讀） | — |`
New: split the label out: `| C01 標籤 | 任一標籤 | `set_text_align(TextAlign)`、`set_line_spacing(double)`、`set_paragraph_spacing(int)` | `LabelHandle` 同名方法 |` and keep `| C11、C12 | 提示框、進度條 | 無動詞（唯讀） | — |`.
Rules: line spacing 1.0-3.0 (a multiple of the font line height), paragraph spacing 0-48 DIP, any other value `invalid_value` and the component is unchanged; a wrong argument type is `invalid_value`; other component kinds `not_supported`. Typed handle and semantic verb call the same `UiState` entry points (same admission check and `Status`).

## 3. SemanticNode

Add fields `TextAlign text_align`, `double line_spacing`, `int paragraph_spacing` (defaults left / 1.0 / 0; meaningful on labels and text fields only; the verbs are listed only on those roles).

## 4. property_changed property names (around line 238)

Old list: `name`、`value`、`enabled`、`read_only`、`checked`、`selected`、`expanded`、`invalid`、`blocked_by_modal`、`numeric_value`
New: add `text_align`、`line_spacing`、`paragraph_spacing`.

## 5. UIA, C03 文字欄位 row (around line 282)

Append to 要點: 文字範圍的 `GetAttributeValue` 回報 `UIA_HorizontalTextAlignmentAttributeId`（`VT_I4`：`HorizontalTextAlignment_Left／Centered／Right`）與 `UIA_LineSpacingAttributeId`（`VT_BSTR`，行高倍數的十進位文字，例如 `1`、`1.5`；單倍行距為 `1`）。Other attribute ids stay `UiaGetReservedNotSupportedValue`. (The BSTR format is an implementation choice: Microsoft's reference only says the value is a string.)

## 6. Out of scope list (around line 460)

"文字屬性的完整集合" stays out of scope; only the two attributes above are reported.
