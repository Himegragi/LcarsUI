# Ticket #102 spec updates (0.4.0)

Apply to `E:\LcarsUI\docs\specs\v1-semantic-layer-and-uia.md`, section 語意層讀取（S2）, after the #100 and #101 updates.
No files under `docs/` are committed. The author-machine glossary already lists the six node kinds and ADR 0011
already records the #102 decision; those local notes require no further change.

## Snapshot relationship paragraph added by #101

Old final sentence:

> 共同 struct 的抽取另由 #102 實作。

New final sentences:

> 共同的 24 個欄位宣告在 core.hpp 的 CommonState，ComponentSnapshot 與 NodeSnapshot 都以它為公開基底；讀法仍為 s.name、n.verbs。CommonState 持有自己的資料，不儲存指向暫時物件或即時狀態的參照。SemanticTree::get 只整塊複製一次共同部分，保留完整的來源快照供元件登記表回呼讀取；選項節點的欄位值維持 #101 完成後的原樣，#106 另行處理。

## S2 C++ snapshot shape

Old: the complete `struct NodeSnapshot { ... };` declaration immediately before `class SemanticTree`.
The common fields below were declared directly in that struct. #101 already moved Verb and TextFieldScroll
to core.hpp; #100's final scroll type name is TextFieldScroll.

New: put this common declaration in the core.hpp portion, immediately before ComponentSnapshot, and change
the existing component declaration from `struct ComponentSnapshot {` to `struct ComponentSnapshot : CommonState {`.
Remove the common fields from ComponentSnapshot, retaining only id, kind, source_conflict, multiline, items,
candidate, target, label_kind and labels; add `bool operator==(const ComponentSnapshot&) const = default;`.

```cpp
struct CommonState {
    std::u16string name, value, description, error;
    // enabled 是自身設定；focused 指此刻鍵盤輸入送到這裡。
    bool enabled = true, read_only = false, focusable = false, focused = false, invalid = false, executing = false;
    bool visible = false; // 最近一次排版後仍有正面積且未隱藏；首次排列前為 false
    bool rich = false;
    std::optional<TextFieldScroll> scroll;
    TextAlign text_align = TextAlign::left; // 設定值，不受 justify_as_left 改變
    JustifyMethod justify_method = JustifyMethod::automatic;
    double line_spacing = 1.0;
    int paragraph_spacing = 0;
    std::optional<SliderRange> range;
    std::optional<int> numeric_value;
    std::optional<bool> checked;
    bool expanded = false;
    std::vector<Verb> verbs; // 此元件或節點目前准入的動作；hover 永不列入
    std::optional<WritingMode> writing_mode;
    std::optional<int> vertical_digits;
    bool operator==(const CommonState&) const = default;
};
```

Replace the S2 NodeSnapshot declaration with this node-only declaration; common-field comments belong only
to CommonState. The already implemented instance and text_revision fields are retained in the current shape.

```cpp
struct NodeSnapshot : CommonState {
    NodeRef ref; Role role = Role::label;
    std::uint64_t instance = 0;
    std::u16string accelerator_key, access_key;
    bool selected = false;
    bool hidden = false, blocked_by_modal = false;
    bool selection_required = false;
    std::optional<Rect> bounds;
    std::vector<NodeRef> labelled_by, described_by;
    std::optional<Selection> text_selection;
    std::vector<StyleSpan> styles;
    std::optional<TextStyle> typing_style;
    std::optional<WordSegmentation> word_segmentation, word_segmentation_in_use;
    std::uint64_t text_revision = 0;
    bool operator==(const NodeSnapshot&) const = default;
};
```

Old: no complete-snapshot equality paragraph after the C++ shape.

New paragraph:

> CommonState、ComponentSnapshot、NodeSnapshot、SliderRange、OptionSnapshot 與 Selection 都有預設的 ==。完整快照的比較包含基底與自身欄位；只比較共同部分時，明確把兩邊轉成 const CommonState&。快照不隨元件變更，元件移除後仍保留已抄下的值。

## Acceptance paragraph added by #101

Old sentence:

> 每次比對 describe 與 get 的全部共同欄位，及每個 items 與子節點的 enabled、selected、invalid。

New sentences:

> 每次把 describe 與 get 的共同部分轉成 const CommonState&，以一行 == 比對，新增共同欄位自動納入；失敗訊息至少包含元件識別碼與狀態名稱。每個 items 與子節點的 enabled、selected、invalid 比對維持不變。另檢查只有 items 不同的 ComponentSnapshot 不相等，以及只有 bounds 不同的 NodeSnapshot 不相等，避免繼承基底的 == 時漏比完整快照。
