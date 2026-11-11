# #81 富文字：規格更新提案

協調者請套用到未追蹤的規格檔後刪除本資料夾。所有「舊 → 新」都以原文為準。行號是 #79 合併後的檔案，僅供定位。

## 規格 3：`docs/specs/v1-semantic-layer-and-uia.md`

### 1. 交付表 D3

舊（套用 #79 後）：`14 個框架動詞（含 confirm，見已決事項 2；多行文字欄位另有 scroll，見 #79）`

新：`16 個框架動詞（含 confirm，見已決事項 2；多行文字欄位另有 scroll，見 #79；富文字欄位另有 set_style、set_typing_style，見 #81）`

### 2. 規格提案的 C++ 形狀（`enum class Verb`、`Argument`、`SemanticNode`、`SemanticTree`）

`enum class Verb` 末端再加 `set_style, set_typing_style`，並加註：

- `set_style`——只適用於富文字欄位（`Control::rich`，一定是多行欄位）。參數為 `StyleEdit { unsigned begin, end; StyleChange change; }`：對「已確定文字」的 `[begin, end)` 套用樣式變更（`StyleChange` 中有設的成員取代範圍內每個字元的該屬性；`bold = false` 取消粗體、`color = ""` 回到一般文字色）。`begin > end`、超出文字、落在代理對中間、參數型別不對回 `invalid_value`；非富文字欄位回 `not_supported`；准入與型別化入口 `TextHandle::set_style` 相同（`writable`：停用、對話框阻擋、隱藏、渲染器失效、唯讀、組字中、沒有連接寫入時機照常回傳）。一次呼叫是一筆復原。
- `set_typing_style`——參數為 `TextStyle { bool bold, italic, underline; std::string color; }`；設定「接下來要打的字」的暫時樣式（移動游標或插入文字後失效）。准入同上。
- `set_value` 的參數另可為 `RichText { std::u16string text; std::vector<StyleSpan> spans; }`（只適用於富文字欄位）：等同 `TextHandle::set_rich_value`，先正規化；區段重疊、超出文字範圍、切在代理對中間回 `invalid_value` 且欄位不變。富文字欄位收到 `u16string` 的 `set_value` 時取代文字並不帶樣式。

`Argument` 改為 `std::variant<std::monostate, std::u16string, int, Selection, OptionValue, RichText, StyleEdit, TextStyle>`。

`SemanticNode` 末端新增：

```cpp
bool rich = false;                       // 以 Control::rich 建立的文字欄位
std::vector<StyleSpan> styles;           // 富文字欄位：value 的樣式區段，正規形式（排序、相鄰同樣式合併、不列一般樣式）
std::optional<TextStyle> typing_style;   // 富文字欄位：接下來要打的字的樣式
```

`SemanticTree` 新增 `Query<Color> text_color(const NodeRef& ref, std::string_view name) const`：顏色名稱「現在」畫出的顏色——欄位顏色清單中該名稱在目前主題變體的值（深色主題用深色值，其餘用淺色值）；名稱為空或清單沒有時為目前主題的一般文字色（需已綁定外觀，否則 `not_found`）；不是富文字欄位回 `not_supported`。

`Operation` 新增 `style`（富文字且可編輯的欄位才列出）；`set_style`、`set_typing_style` 只在 `Operation::style` 被准入時出現在 `verbs`。

### 3. 「已決事項 2」`scroll` 動詞（#79 加的那一條）之後

加一條：`set_style、set_typing_style 動詞與 set_value 的 RichText 參數（#81）。第 15、16 個動詞，只適用於富文字欄位（見上）。屬性變更通知：樣式區段改變發出 property_changed("styles")（文字沒變時不發 "value" 與 text_changed），暫時輸入樣式改變發出 property_changed("typing_style")。`

### 4. UIA 控制類型與 pattern 表（C03 文字欄位）

舊（套用 #79 後）：`Value 為草稿（多行欄位的值含 LF）；SetValue 等同 set_value；唯讀時 IsReadOnly = true`

新：`Value 為草稿（多行欄位的值含 LF；富文字欄位只有純文字，樣式見 Text pattern）；SetValue 等同 set_value（富文字欄位：取代文字、不帶樣式）；唯讀時 IsReadOnly = true`

### 5. Text pattern 小節：新增文字屬性

於 GetAttributeValue 說明處加：

```
**文字屬性（#81，只有富文字欄位；其他欄位維持只回 IsReadOnly，其餘為 NotSupported）：**
- GetAttributeValue 回報 UIA_FontWeightAttributeId（VT_I4：粗體 700、否則 400）、UIA_IsItalicAttributeId（VT_BOOL）、UIA_UnderlineStyleAttributeId（VT_I4：單線 TextDecorationLineStyle_Single = 1、無 0）、UIA_ForegroundColorAttributeId（VT_I4 COLORREF，0x00BBGGRR）。前景色是「目前主題下實際畫出的顏色」（顏色名稱依主題變體取值；沒有名稱或名稱不在清單為一般文字色），切換主題後下一次讀取就是新值。
- 範圍內某個屬性的值不一致時，該屬性回 UiaGetReservedMixedAttributeValue（VT_UNKNOWN）；空範圍取插入點所在字元（文字尾端取最後一個字元）的值。
- FindAttribute 支援上述四個屬性：依 backward 在範圍內找第一段（或最後一段）值相同的連續區間；值的型別不對回 E_INVALIDARG；其他屬性仍回 UIA_E_NOTSUPPORTED。
- 樣式改變使 text_revision 改變，已取得的範圍物件失效（UIA_E_ELEMENTNOTAVAILABLE），與文字改變相同。
```

### 6. 範圍外（#79 加的那一行）

舊（套用 #79 後）：`- 多行文字、grapheme cluster、RTL 的 Text pattern；…` 之後原有的「富文字」字樣

新：把「富文字的 UIA 文字屬性」從範圍外移除，改列：`- 標籤的富文字、除上述四個之外的文字屬性（字型名稱、字級、刪除線、背景色等）、UIA 的 Text 編輯事件（TextEditTextChanged）對樣式變化的細分。`

### 7. 驗證表

新增一列：

| SEM12 富文字（#81） | 對富文字欄位：`set_value`（`RichText`）、`set_style`、`set_typing_style`、讀 `styles`／`typing_style`；對重疊的值、反向範圍、唯讀欄位、非富文字欄位各做一次 | 與型別化入口（`TextHandle`）逐項回相同的 `Status` 與相同結果；`styles` 恆為正規形式；樣式改變只發 `styles` 屬性事件 |

## 規格 5：`docs/specs/v1-settings-demo-app.md`

### 1. 項目對話框控制項表（第 163 行）

舊：`| items.dialog.note.label、items.dialog.note | 「說明（選填）」，最長 200。 |`

新：

```
| items.dialog.note.label、items.dialog.note | 「說明（選填）」，最長 200；**富文字多行欄位**（#81，3 行；`Control::rich`），以 `connect_rich_source` 連接，隨「確定」的混合確認群組（`connect_mixed_confirmation`，名稱欄為 `TextChange`、說明欄為 `RichTextChange`）一起提交。顏色清單：紅、綠、藍（各有淺色／深色值；兩套外觀、兩個主題下對欄位底色對比 ≥ 4.5:1，有測試）。 |
| items.dialog.note.bold、.italic、.underline | 欄位上方一排按鈕「粗體」「斜體」「底線」；按下後呼叫 `toggle_style`，焦點回到說明欄，選取範圍不變。無選取時改變「接下來要打的字」。 |
| items.dialog.note.color | 下拉選單「文字顏色」：預設、紅、綠、藍；選定後對說明欄選取套用（無選取：暫時輸入樣式），焦點回到說明欄。每次開啟對話框回到「預設」。 |
```

### 2. 設定檔格式（第 312 行與第 326 行）

舊：`"items": [ { "id": "T001", "name": "範本 01", "note": "" } ],`

新：`"items": [ { "id": "T001", "name": "範本 01", "note": { "text": "", "spans": [] } } ],`，並說明：每個區段為 `{ "start": 0, "length": 2, "bold": true, "italic": true, "underline": true, "color": "red" }`（`bold`／`italic`／`underline`／`color` 只在有值時寫出；`start`、`length` 為 UTF-16 偏移）。

舊：`note 合法單行、≤ 200、可空`（第 326 行，套用 #79 後為「合法多行（只含 LF）、≤ 200、可空」）

新：`note 為物件 { text, spans }：text 合法多行（只含 LF）、≤ 200、可空；spans 為陣列（≤ 1000 筆），每筆 start、length 為整數，bold／italic／underline 若出現必為布林，color 若出現為字串（≤ 64 位元組），不可有其他成員；區段重疊、超出 text、切在代理對中間即整份檔案無效；載入時依 normalize_rich_text 正規化。**舊版檔案中字串形式的 note 仍有效，載入為沒有樣式的文字。**`

### 3. 驗證表

新增：

| IT-R1 | 新增項目，說明欄選取一段後按「粗體」、再選另一段選「紅」，確定、存檔、重新載入、編輯 | 說明欄的區段與存檔前完全相同；按鈕按下後焦點回到說明欄、選取範圍不變 |
| IT-R2 | 載入舊版檔案（`"note": "x"`） | 載入成功，說明沒有樣式；再存檔時以 `{ text, spans }` 寫出 |

## 規格 4：`docs/specs/v1-text-and-presentation-settings.md`（文字後端）

在字型目錄／字型選用小節加註（#81）：目錄除了每個家族「最接近一般粗細、不傾斜」的檔案外，也記錄該家族真正的粗體（字重 ≥ 600，取最接近 700）、斜體、粗斜體檔；`system_styled_font_file(family, bold, italic)` 取得，沒有就回空路徑。`Text::shape(text, size, FontStyle)` 依樣式選檔：有真檔用真檔；缺的樣式以合成補（粗體：outline 加粗、進位量加 `max(1, 字級/16)` 像素；斜體：outline 向右剪切）；粗斜體在只有粗體或斜體其一時，用那個真檔並合成缺的一項。備援字型同樣處理（每個備援家族各自找真檔；沒有就合成；彩色點陣字形如 Emoji 只有輪廓字形才合成，點陣字形維持原樣）。字形快取鍵為（face id、字形 id、大小、合成旗標），所以同一字形的一般／粗／斜／粗斜體各是一筆。繪圖清單不新增指令種類：底線是現有的填滿矩形，字形指令因字型檔或合成旗標不同而在快取鍵上可區分。
