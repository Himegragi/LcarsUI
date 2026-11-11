# Spec updates for #84 (文字左右對齊)

Target: `docs/specs/v1-semantic-layer-and-uia.md` (規格 3). The planning docs are untracked; the coordinator applies these. They extend the #80 entries in `ticket-80.md`.

## 1. Verb enum

Old (after #80): `... paste, confirm, scroll, set_text_align, set_line_spacing, set_paragraph_spacing`
New: append `set_justify_method`. `Argument` gains `JustifyMethod` (automatic / spaces / per_character). `TextAlign` is now `left / center / right / justify / distribute`.

## 2. Verb table (動詞對照表)

C03 文字欄位 row and C01 標籤 row, verbs column: add `set_justify_method(JustifyMethod)`; handle column: `TextHandle` / `LabelHandle` 同名方法（`set_justify_method`、`justify_method()`）。
Rules: `set_text_align` 接受 `justify`（左右對齊）與 `distribute`（分散對齊）；**文字欄位**收到 `distribute` 回 `Status::invalid_value`，欄位不變（標籤可用）。Out-of-range enum values are `invalid_value`; a wrong argument type is `invalid_value`; other component kinds `not_supported`. Typed handle and semantic verb call the same `UiState` entry points (`set_text_align` / `set_justify_method`), so the same admission check and `Status`.

## 3. SemanticNode

Add field `JustifyMethod justify_method` (default `automatic`; meaningful on labels and text fields only; the verb is listed only on those roles). `text_align` reads the property as set: while `PresentationSettings::justify_as_left` is on it still reads `justify` / `distribute` (only what is drawn and reported to UIA changes).

## 4. property_changed property names

Old (after #80): `... numeric_value, text_align, line_spacing, paragraph_spacing`
New: add `justify_method`.

## 5. UIA, C03 文字欄位 row

`UIA_HorizontalTextAlignmentAttributeId`: `justify` and `distribute` both report `HorizontalTextAlignment_Justified` (3; UIA has no distributed value, the semantic layer still tells them apart). While `PresentationSettings::justify_as_left` is on they report `HorizontalTextAlignment_Left`.

## 6. Presentation settings (規格 4/5, wherever `PresentationSettings` is listed)

`PresentationSettings` gains `bool justify_as_left` (default false; "左右對齊改為靠左", WCAG 1.4.8). Applied through `Ui::set_presentation` and re-laid-out at once; justified and distributed text is then drawn left-aligned, the component properties are unchanged. Included in `operator==`. The settings demo saves it as the optional boolean `presentation.justify_as_left` (a file without it means off; a non-boolean rejects the document).
