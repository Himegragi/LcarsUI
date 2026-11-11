# #79 多行文字輸入：規格更新提案

協調者請套用到未追蹤的規格檔後刪除本資料夾。所有「舊 → 新」都以原文為準。

## 規格 3：`docs/specs/v1-semantic-layer-and-uia.md`

### 1. 交付表 D3（第 39 行）

舊：`語意層操作與通知：13 個框架動詞（含 confirm，見已決事項 2）、…`

新：`語意層操作與通知：14 個框架動詞（含 confirm，見已決事項 2；多行文字欄位另有 scroll，見 #79）、…`

### 2. 規格提案的 C++ 形狀（`enum class Verb` 與 `SemanticNode`）

舊：`enum class Verb { invoke, toggle, set_value, select, open, cancel, focus, undo, redo, copy, cut, paste };`

新：在列表末端加 `confirm, scroll`（已決事項 2 已有 `confirm`），並加註：`scroll`——只適用於多行文字欄位，參數為 `int`（第一條可見的畫面行，從 0 起算）；單行欄位回 `not_supported`；負數或超過「最後一頁」的位置、參數型別不對回 `invalid_value`；准入與型別化入口 `TextHandle::set_scroll_line` 相同（`available()`：停用、對話框阻擋、隱藏、渲染器失效照常回傳）。

`SemanticNode` 末端新增：

```cpp
struct TextScroll { unsigned first_line = 0, visible_lines = 0, line_count = 0; };
std::optional<TextScroll> scroll;  // 多行文字欄位：垂直捲動位置（畫面行）、設定的顯示行數、目前排列的畫面行總數
```

`line_count` 在欄位第一次排列之前為 0。`first_line` 的單位是「畫面行」（自動換行切出的行各算一行），字級或密度改變後仍指向同一段文字。

### 3. 「已決事項 2」`confirm` 動詞（第 482 行）之後

加一條：`scroll 動詞（#79）。第 14 個動詞，只適用於多行文字欄位（見上）。屬性變更通知：位置改變發出 property_changed("scroll_line")，顯示行數改變發出 property_changed("visible_lines")。`

### 4. UIA 控制類型與 pattern 表（第 282 行，C03 文字欄位）

舊：`| C03 文字欄位 | Edit | Value、Text | form.name | Value 為草稿；SetValue 等同 set_value；唯讀時 IsReadOnly = true |`

新：`| C03 文字欄位 | Edit | Value、Text；多行欄位另有 Scroll | form.name | Value 為草稿（多行欄位的值含 LF）；SetValue 等同 set_value；唯讀時 IsReadOnly = true |`

### 5. Text pattern 小節（第 308–309 行）

舊：`- 單行、由左到右；Character 單位以文字後端提供的游標停點為界，Word／Line／Paragraph 提升為整段（與原型相同，不宣稱語言學斷詞）。`

新：
```
- 由左到右。Character 單位以文字後端提供的游標停點為界。單行欄位：Word／Line／Paragraph 提升為整段（與原型相同，不宣稱語言學斷詞）。多行欄位（#79）：Line 為畫面行（自動換行切出的行各算一行；一行的範圍含它結尾的 LF），Paragraph 為以 LF 分隔的行；Word 與 Document 仍為整段。ExpandToEnclosingUnit、Move、MoveEndpointByUnit 對 Line／Paragraph 以這些邊界計算。
- 多行欄位的 GetBoundingRectangles 回傳範圍觸及的每條可見畫面行各一個矩形；GetVisibleRanges 為可見畫面行的起點到終點；RangeFromPoint 依 x 與 y 取畫面行與位置；ScrollIntoView 以 scroll 動詞把範圍起點所在行捲進可見範圍（alignToTop 為真時置頂，否則置底）。
```

### 6. 新增小節「Scroll pattern（#79）」（Text pattern 小節之後）

```
**Scroll pattern（#79，只有多行文字欄位）：** 只實作垂直方向，單位是畫面行。
- VerticallyScrollable 為「line_count > visible_lines」；HorizontallyScrollable 恆為 false；HorizontalScrollPercent 為 UIA_ScrollPatternNoScroll（-1）、HorizontalViewSize 為 100。
- VerticalViewSize = visible_lines／line_count × 100（不可捲時 100）；VerticalScrollPercent = first_line／(line_count − visible_lines) × 100（不可捲時 0）。
- Scroll(水平, 垂直)：水平不是 NoAmount 回 UIA_E_INVALIDOPERATION；SmallIncrement／Decrement 為一行，LargeIncrement／Decrement 為 visible_lines 行，夾在 0 與最後一頁之間，已在邊界時成功但不動。
- SetScrollPercent(水平, 垂直)：水平不是 -1 回 UIA_E_INVALIDOPERATION；垂直為 -1 時不動，超出 0–100 回 E_INVALIDARG；其餘四捨五入成畫面行，經 scroll 動詞設定。
```

### 7. 範圍外（第 460 行）

舊：`- 多行文字、grapheme cluster、RTL 的 Text pattern；…`

新：`- grapheme cluster、RTL 的 Text pattern；（多行文字已由 #79 支援，單行規則不變）…`

## 第一批元件規格：`docs/specs/first-component-batch.md`

### 8. 元件表 C03（第 23 行與第 102 行）

舊（第 23 行）：`| C03 | 單行文字欄位 | …`

新：`| C03 | 文字欄位（預設單行；建立時可選多行，#79） | …`

舊（第 102 行）：`| C03 | 單行編輯；游標、選取、Home／End、…`

新：`| C03 | 單行編輯（多行為建立時的選項，行為見下「多行文字欄位（#79）」）；游標、選取、Home／End、…`（其餘不變）

### 9. 「貼上換行」列（第 130 行）後新增一節「多行文字欄位（#79）」

```
| 多行文字欄位（#79） | Control::multiline 與 visible_lines 在建立時決定，之後不能改；仍是 Kind::text。單行欄位的所有行為（含貼上換行轉空白）不變。 |
| 值與換行 | 以單一 LF 表示換行，CR 與其他控制字元不合法；程式、資料來源、語意層給的值含 CR 一律是無效值（與其他無效值相同，不自動轉換）。貼上的 CRLF／CR／LF 各成為一個 LF；複製與剪下寫入剪貼簿時換行為 CRLF。max_length 中一個換行算 1。 |
| 按鍵 | Enter 與 Shift+Enter 插入換行（各一筆復原），欄位先處理，不觸發任何 Enter 快捷鍵；Ctrl+Enter 欄位不處理（落到快捷鍵）；Tab 仍移動焦點；組字中 Enter 交給輸入法、不換行。↑／↓ 依畫面行並保持水平位置；Home／End 為畫面行的開頭／結尾（自動換行處 End 停在該行行尾）；Ctrl+Home／End 為整段文字；PageUp／PageDown 移動「顯示行數」那麼多行（畫面跟著捲同樣的行數）；加 Shift 擴大選取。 |
| 高度與捲動 | 高度固定為顯示行數，不隨內容長高；自動換行（規則同標籤），沒有水平捲動；捲軸寬度一律預留、只在內容超過可見行數時畫出。捲動位置是「第一條可見的畫面行」；游標移動、輸入、程式改值或改游標後自動捲到游標可見，單純捲動不移動游標也不會被拉回；字級、密度或寬度改變後，同一段文字仍在最上方。顯示行數可由程式於執行中改變並立即重排。 |
| 滾輪與滑鼠 | 滾輪由最內層先捲，到頂／底才交給外層；對話框開著時，對話框內的可捲動內容（含此欄位）可用滾輪與拖曳捲軸，對話框外的背景不捲。點擊把游標放到該行該位置（點在自動換行行尾之後仍留在該行）；拖曳可跨行選取，拖到欄位上緣或下緣外時每步往該方向捲一行（桌面宿主以計時器重複）。 |
```

### 10. 非目標（第 274 行）

舊：`- 多行、密碼與富文字編輯；完整 grapheme／RTL／彩色 emoji 支援。`

新：`- 密碼與富文字編輯；完整 grapheme／RTL／彩色 emoji 支援。（多行已由 #79 提供；隨內容長高、水平捲動、雙擊／三擊選取仍不在範圍。）`

### 11. 平台待驗證表 T01 文字範圍（第 264 行）

舊：`第一批聚焦繁中／英文單行；…`

新：`第一批聚焦繁中／英文單行，多行為 #79 的選項（同樣的字型與 UTF-16 邊界）；…`

## 規格 4（示範應用）：`docs/specs/v1-settings-demo-app.md`

### 12. 項目對話框欄位表（第 163 行）

舊：`| items.dialog.note.label、items.dialog.note | 「說明（選填）」，最長 200。 |`

新：`| items.dialog.note.label、items.dialog.note | 「說明（選填）」，最長 200；3 行多行欄位（#79，換行算 1 個字元）。 |`

### 13. 設定檔驗證表（第 326 行）

舊：`... name 同 general.name；note 合法單行、≤ 200、可空 |`

新：`... name 同 general.name；note 合法多行（允許 LF，其餘控制字元與 CR 不合法）、≤ 200、可空；JSON 中的換行存成 \n |`

### 14. 設定檔格式範例（第 312 行附近）

加註：`note 含換行時，JSON 字串使用 \n 跳脫；載入端以 valid_multi_line 檢查（只有 note 允許 LF；name 與 general.name 仍是單行）。`

## 驗收案例新增（建議編號 ML01–ML06，對應測試）

核心（tests/multiline.cpp）與平台孿生（`lcars_host_verify --multiline-only`，`host_native` 內含）；UIA 跨程序檢查為 `uia_demo_*` 的 UA08。作者機手動項目見實作回報。
