# 規格更新提案：票 #83（文字欄位雙擊選詞、三擊選段落）

套用對象：`docs/specs/v1-semantic-layer-and-uia.md`（規格 3）。由協調者套用到未進版控的規格後移除本資料夾。

## 1. 「規格提案的 C++ 形狀」程式碼區塊（約第 117、130 行）

`enum class Verb` 舊：

```cpp
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus,
                  undo, redo, copy, cut, paste };
```

新（`confirm`、`scroll` 是 v1 後既有，一併列出；本票新增最後五個）：

```cpp
enum class Verb { invoke, toggle, set_value, select, open, cancel, focus,
                  undo, redo, copy, cut, paste, confirm, scroll,
                  set_word_segmentation,   // 引數 WordSegmentation：設定文字欄位的斷詞方式
                  select_word, select_paragraph,   // 引數 int：UTF-16 位移（從該字元開始的位置）
                  double_click, triple_click };    // 同上；另武裝「按住拖曳」的詞／段落單位
enum class TextUnitKind { word, paragraph };       // SemanticTree::text_unit 的查詢單位
```

`Argument` 舊／新：新增一個替代型別 `WordSegmentation`（`std::variant<…, OptionValue, WordSegmentation>`）。

`SemanticNode` 新增（文字欄位才有值，其他為空）：

```cpp
std::optional<WordSegmentation> word_segmentation;         // 設定的斷詞方式
std::optional<WordSegmentation> word_segmentation_in_use;  // 實際使用的方式（詞典不可用時為 per_character）
```

`SemanticTree` 新增兩個查詢：

```cpp
Query<TextSpan> text_unit(std::string_view id, TextUnitKind unit, unsigned offset) const;  // 該位置所在的詞／段落範圍
Query<unsigned> word_stop(std::string_view id, unsigned offset, bool forward) const;       // 下一個／前一個詞停點
```

## 2. 動詞表（新增列）

| 動詞 | 適用 | 引數 | 准入與結果 |
| --- | --- | --- | --- |
| `set_word_segmentation` | 文字欄位 | `WordSegmentation`（`dictionary`／`per_character`／`script_run`） | 與 `TextHandle::set_word_segmentation` 同一函式（擁有者設定：不要求欄位啟用）；引數型別不符回 `invalid_value` |
| `select_word`／`select_paragraph` | 文字欄位 | `int` 位移 ≥ 0 | 與 `select` 同一准入：停用回 `disabled`、組字中回 `busy`、唯讀可用；位移超過文字長度回 `invalid_value` |
| `double_click`／`triple_click` | 文字欄位 | `int` 位移 ≥ 0 | 同上；選取結果與 `select_word`／`select_paragraph` 相同，另武裝拖曳單位（`Frame::select_to` 之後以詞／段落擴大，直到選取被其他操作改變） |

`verbs` 列表：文字欄位恆列 `set_word_segmentation`；`select_word`／`select_paragraph`／`double_click`／`triple_click` 與 `select` 同一條件（`Operation::select` 被准入）。

事件：斷詞方式改變發 `property_changed`（`property = "word_segmentation"`）；選取改變沿用 `text_selection_changed`。

## 3. 「Text pattern（S6）」第一條（第 310 行）

舊：

> 單行、由左到右；`Character` 單位以文字後端提供的游標停點為界，`Word`／`Line`／`Paragraph` 提升為整段（與原型相同，不宣稱語言學斷詞）。

新：

> 由左到右；`Character` 單位以文字後端提供的游標停點為界。`Word` 單位用文字欄位的詞邊界（與雙擊選詞、Ctrl+←／→ 同一套，斷詞方式見 `WordSegmentation`）：一個詞連同它後面的空白是一個單位，每個標點、每個換行字元、不接在詞後的一串空白各是一個單位。`Paragraph` 單位是以 `\n` 分隔的段落，含結尾的 `\n`。`Line` 在多行欄位是畫面行（#79），單行欄位仍是整份文件；`Format`／`Page` 維持整份文件。`ExpandToEnclosingUnit`、`Move`、`MoveEndpointByUnit` 對 `Word`／`Paragraph` 都用上述邊界。

## 4. 新增小節（建議放在「Text pattern」之後）：文字單位與詞邊界

- **詞邊界來源。** 每個文字欄位有一個斷詞方式（預設 `dictionary`）：`dictionary` 呼叫系統 `icu.dll` 的 word BreakIterator（語系 `zh_TW`，執行期從 System32 載入，找不到或開啟失敗時自動改用 `per_character`）；`per_character`：每個中日文字一詞，英數字連續一串一詞；`script_run`：連續的中日文字一詞、連續英數字一詞。三種方式下每個標點各自一詞、連續空白一串、每個 `\n` 各自一詞。斷詞器在框架內部介面 `detail::WordBreaker` 之後，可被替換（測試用）。
- **雙擊（宿主計算連點）。** 點在詞上選該詞加後面的空白；點在標點選那一個標點；點在空白選那一串空白；點在行尾（換行字元上或文字末端之後）選該行（段落）最後一個詞。三擊：單行選全部；多行選 `\n` 分隔的段落（不含結尾 `\n`）。連點判定：間隔 ≤ `GetDoubleClickTime()`，與第一次按下位置的差距在 `SM_CXDOUBLECLK`／`SM_CYDOUBLECLK` 矩形內（以第一次按下為中心）；第三次按下與第二次之間適用同條件；第四次按下回到單擊；點到別的欄位重新計數。宿主讀取 Windows 設定並提供時鐘，核心計數，測試可注入兩者。
- **鍵盤。** Ctrl+→ 到「下一個詞的開頭」（跳過空白，每個標點與 `\n` 是停點，文字末端也是），Ctrl+← 到前一個停點；Ctrl+Shift+←／→ 延伸；Ctrl+Backspace／Delete 刪除到同一個停點（有選取時只刪選取；每次按鍵一個復原步驟；唯讀回 `read_only`）。
- **程式入口。** `TextHandle`：`set_word_segmentation／word_segmentation／word_segmentation_in_use／word_at／paragraph_at／next_word_stop／previous_word_stop／select_word／select_paragraph／double_click／triple_click`；與語意層動詞和查詢走同一函式，`Status` 一致。
