# Ticket #100 spec updates

Apply these exact token replacements in the named sections of `E:\LcarsUI\docs\specs\`.
They change public names only; behavior and version text stay unchanged. No alias is retained.

| File | Section | Old → new |
| --- | --- | --- |
| `first-component-batch.md` | 多行文字欄位（#79）, first table row | `ComponentKind::text` → `ComponentKind::text_field` |
| `v1-text-and-presentation-settings.md` | 現況 table (appearance.hpp row); 密度（A6）, first paragraph | `arrange_field` → `arrange_text_field` |
| `v1-draw-list-gpu-rendering.md` | 失效期間各入口的行為 (V01 draft row); RF03 acceptance row | `TextHandle` → `TextFieldHandle` |
| `v1-settings-demo-app.md` | `items.dialog.note.label` / `items.dialog.note` row; 名稱欄有草稿 close/save row | `TextHandle` → `TextFieldHandle` (both plain and rich `Change` forms included) |
| `v1-semantic-layer-and-uia.md` | Problem Statement, first paragraph | `TextHandle::describe()` → `TextFieldHandle::describe()` |
| `v1-semantic-layer-and-uia.md` | 現況 table, `src/core.cpp:162-200` row | `add_text` → `add_text_field` |
| `v1-semantic-layer-and-uia.md` | 識別碼登記（S1）, 指定方式 / 重複時 / 回傳型別 rows | `add_text` → `add_text_field`; `role 為 text` → `role 為 text_field` |
| `v1-semantic-layer-and-uia.md` | 文字單位查詢（#83） paragraph; C03 文字欄位 operation row; `scroll`（#79） paragraph; 富文字（#81） paragraph; `set_word_segmentation` row; 程式入口（#83） paragraph; SEM15 富文字 acceptance row | `TextHandle` → `TextFieldHandle` |

For the current public API summary, append this paragraph to the appearance/layout discussion in
`v1-text-and-presentation-settings.md`, section 密度（A6） (old: no paragraph; new: the text below):

> `Field` 只指會回寫的欄位；文字欄位使用 `TextField`。`Appearance::arrange_text_field` 接收 `TextFieldHeights`，回傳 `TextFieldLayout`；`Placement::text_field` 持有該排列結果，多行顯示資料仍由 `Placement::view` 持有，型別為 `TextFieldView`（每行為 `TextFieldLine`）。`Appearance::field()` 與 `FieldParts` 移除，不保留別名。多行文字欄位的語意捲動資料稱 `TextFieldScroll`，`ScrollRegion::text_field` 指向該文字欄位。確認群組使用 `ConfirmFieldChange` 與 `ConfirmFieldError`；`Change<Field, Value>`、`ConfirmField`、`connect_confirmation` 不變。`TextAlign`、`TextRun`、`TextStyle`、`TextSpan`、`TextMeasure`、`TextSegment` 等一般文字名稱不變。
