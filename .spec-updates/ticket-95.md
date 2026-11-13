# Spec text changes for #95 (Source<T>, Change<Field, Value>)

The old read-source and change-record names are gone in 0.3.0 without aliases (#95 Q1-Q3). Two untracked specs under
`E:\LcarsUI\docs\specs\` still spell them. Replace exactly as below; nothing else in the specs names a removed type or
`Ui::connect_confirmation`.

## 1. `docs/specs/v1-draw-list-gpu-rendering.md`, renderer-failure table (line 286)

Old:

    | 失效期間應用的 `TextSource` 送出新值 | 照常更新（屬應用資料流向 UI，不是使用者修改）；重建後畫面顯示最新值。（規格提案） |

New:

    | 失效期間應用的讀取來源（`Source<std::u16string>`）送出新值 | 照常更新（屬應用資料流向 UI，不是使用者修改）；重建後畫面顯示最新值。（規格提案） |

## 2. `docs/specs/v1-settings-demo-app.md`, row `items.dialog.note.label`、`items.dialog.note` (line 164)

Old:

    （`connect_mixed_confirmation`，名稱欄為 `TextChange`、說明欄為 `RichTextChange`）

New:

    （`connect_mixed_confirmation`，名稱欄為 `Change<TextHandle>`、說明欄為 `Change<TextHandle, RichText>`）

(`connect_mixed_confirmation` keeps its name here; #96 renames it to `connect_confirmation`.)
