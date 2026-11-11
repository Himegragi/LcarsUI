# Spec updates for #82 (vertical writing, display only)

For the coordinator to apply to the untracked spec; delete this folder afterwards.
Target: `docs/specs/v1-semantic-layer-and-uia.md` (規格 3). Nothing else in the spec changes.

## 1. D3 deliverable row: the verb count

Section "交付物", row D3.

Old:

> | D3 | 語意層操作與通知：13 個框架動詞（含 `confirm`，見已決事項 2）、`Status` 優先序、焦點／屬性／結構／文字通知。 |

New:

> | D3 | 語意層操作與通知：15 個框架動詞（含 `confirm`，見已決事項 2；`set_writing_mode`、`set_vertical_digits` 見動詞對照表 C01 列）、`Status` 優先序、焦點／屬性／結構／文字通知。 |

## 2. S2 C++ shape: `Verb` and `SemanticNode`

Section "語意層讀取（S2）", the C++ block.

Old (`Verb`; the block there lists 12 verbs, the code has 13 with `confirm`):

```cpp
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus,
                  undo, redo, copy, cut, paste };
```

New:

```cpp
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus,
                  undo, redo, copy, cut, paste, confirm,
                  set_writing_mode, set_vertical_digits };
// Argument gains one alternative, appended: std::variant<std::monostate, std::u16string, int, Selection, OptionValue, WritingMode>
```

In `struct SemanticNode`, after `std::optional<Rect> bounds;` add:

```cpp
    std::optional<WritingMode> writing_mode;  // 標籤才有：horizontal（預設）或 vertical
    std::optional<int> vertical_digits;       // 標籤才有：縱中橫最多位數，0-3，預設 3
```

## 3. S3 verb table: new row for C01

Section "語意層操作（S3）", table "動詞對照表". The last row currently reads
`| C01、C11、C12 | 標籤、提示框、進度條 | 無動詞（唯讀） | — |`.

Old:

> | C01、C11、C12 | 標籤、提示框、進度條 | 無動詞（唯讀） | — |

New (labels get their own row; tooltips and progress stay read-only):

> | C01 標籤 | `about.vertical` | `set_writing_mode(WritingMode)`、`set_vertical_digits(int)`；唯讀元件的 `focus` 規則不變 | `LabelHandle::set_writing_mode`、`set_vertical_digits` |
> | C11、C12 | 提示框、進度條 | 無動詞（唯讀） | — |

Add this paragraph after the table (before 「邏輯項目一律以…」):

> **標籤的書寫方向（#82）：** `set_writing_mode` 的參數是 `WritingMode`（`horizontal`／`vertical`），`set_vertical_digits` 的參數是 `int`，範圍 0–3；超出範圍、參數型別不符回 `invalid_value`，對標籤以外的元件回 `not_supported`。兩個動詞與 `LabelHandle` 的同名方法走同一個內部入口，`Status` 相同（元件已不存在回 `not_found`、渲染器失效回 `renderer_failed`）；`blocked_by_modal`／`hidden` 只擋語意層入口（與其他動詞一致），不擋應用程式碼經 Handle 的設定。節點上讀回的 `writing_mode`、`vertical_digits` 與 Handle 的 `writing_mode()`、`vertical_digits()` 一致；值改變時發出 `property_changed`（屬性名 `writing_mode`、`vertical_digits`）。標籤的值與名稱不變，名稱仍是原文字順序（直書不改閱讀順序）。標籤不新增 UIA TextPattern，也不回報 `UIA_TextFlowDirectionsAttributeId`。
