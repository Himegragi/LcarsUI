# TextRange 單行原型與跨程序測試

2026-10-08。此輪增加有限的 Text/TextRange provider，不代表完整 UIA Edit 合規或 Narrator 已通過。
使用原本的 `run.ps1 -CrossProcess` 建置與執行；不新增套件依賴。

## 本輪通過的情境

- 外部程序取得 TextPattern、DocumentRange，文字與 Value 一致。
- Clone、Compare、CompareEndpoints、MoveEndpointByRange、Character 範圍移動、Document 擴展。
- 大小寫不敏感搜尋 `UIA`，回傳正確子字串。
- GetSelection／Select 能改變共用編輯模型，並收到 TextSelectionChanged。
- 文字修改後收到 TextChanged；Value property changed 仍維持。
- GetBoundingRectangles 使用渲染的同一份 layout：是文字行的矩形，不再拿 HWND bounds 充當文字範圍。
- RangeFromPoint 的螢幕座標對應第一個游標邊界。
- 字級變更後，舊範圍的文字端點仍有效，矩形寬高重新計算。
- 空文字回傳空內容，退化範圍回傳空矩形陣列。
- 長文字 ScrollIntoView 不修改內容，範圍與 viewport 左緣對齊。
- 外部程序選取第一個漢字後，host 讀回 UIA 提供的螢幕矩形，轉回 OpenGL client 座標，在該矩形內取樣得到反白色 RGB `(42,78,123)`。

![external TextRange selection](evidence/textrange-selection.bmp)

## 範圍生命週期提案

範圍綁定文字 revision。只改字型／字級／捲動／視窗位置時沿用文字端點並更新幾何。
文字被修改後，舊範圍回傳 `UIA_E_ELEMENTNOTAVAILABLE`（實測 `0x80040201`）；client 重新取得 DocumentRange。
此策略避免舊 offset 指向新文字，但**不是最終的動態範圍追蹤設計**，需要評估真實輔助工具對範圍存續的要求。

## 執行緒與事件

provider 的文字與幾何由 UI thread 發布到有鎖快照；範圍方法只讀快照，Select／ScrollIntoView 同步回 UI thread。
沒有從 UIA COM callback 呼叫 FreeType 或 GL。所有互動仍在原型單 UI thread 模型下。
UIA client 的 SetValue 已改用真正 `SysAllocStringLen` 配置的 BSTR，取代不正確的一般字串指標。

## 有界線的實作

- 僅單行 LTR，無嵌入子物件、多重選取或 ITextProvider2。
- Character 暫沿用 HarfBuzz cluster 邊界；不能因此宣稱已滿足 Unicode grapheme 與連字內游標規則。
- Word／Format／Paragraph 等未獨立支援，提升到較大的單行／Document 單位；並非真正語言學斷詞。
- 只有部分文字屬性；未知屬性回傳 UIA reserved NotSupported 值。FindAttribute、選取等分支已補邊界測試（見下節），仍非完整。
- preedit 出現在可見文字快照，會使 range revision 改變；實際 IME、重轉換與輔助工具對此行為尚未驗證。
- 相同文字的重新提交等事件語意、provider shutdown 競態、完整失效契約仍需測試。
- 實際候選窗、Narrator、跨螢幕 DPI、Windows10 及其他 OS 尚未通過。

## 邊界與負面契約（後續一輪）

client 在原情境後新增 `range_contract_edges`，再重建最後的選取供 host 渲染比對。
- 過期範圍：Clone、GetAttributeValue（支援／不支援屬性）、FindAttribute（true/false）、FindText、Compare／CompareEndpoints（兩個方向）、MoveEndpointByRange、Expand、Move、MoveEndpointByUnit、矩形、EnclosingElement、Children、Select、AddToSelection、RemoveFromSelection、ScrollIntoView，共21個呼叫，全部要求 `UIA_E_ELEMENTNOTAVAILABLE`。之後 Value 不變，且在同一 revision 下用 Compare／CompareEndpoints 比對選取數量與確切端點不變（實測選取為文件尾端的收合游標，不能只比文字）。
- 已證實並修正兩個缺陷：過期範圍 `FindAttribute(IsReadOnly,true)` 回傳 `S_OK`/null（無法分辨「沒找到」與「範圍過期」）；`RemoveFromSelection` 不檢查存續，一律回 `UIA_E_INVALIDOPERATION`。現在先檢查 revision。
- 有效範圍：IsReadOnly 為 false；不支援屬性為 reserved NotSupported；未支援屬性搜尋不會假造結果（實測 `UIA_E_NOTSUPPORTED`）。
- 退化範圍在文件頭尾的 Move／MoveEndpointByUnit 夾住且回報0；start 越過 end 時 end 跟著移動。
- 單一選取：重複加入同一範圍成功；加入第二個不相交範圍及 RemoveFromSelection 回 `UIA_E_INVALIDOPERATION`。
- 已驗證的 provider 行為：host 直接呼叫 provider（不經 UIA client），endpoint 值7 的 CompareEndpoints（兩側）與 MoveEndpointByUnit 回 `E_INVALIDARG`，輸出為0，範圍端點不變。
- 外部 client 只做觀察、不算 provider 驗證：無效 unit 得到 `E_UNEXPECTED`；endpoint 值7 得到 `S_OK`，結果與 End 相同（CompareEndpoints 回1、MoveEndpointByUnit 移動 End -1）。推論是 UIA client／傳送層在 provider 之前正規化了此值，但未加 instrumentation，不能確定在哪一層發生。

證據：`evidence/textrange-edges-red-*.log`、`textrange-edges-green-client.log`、`textrange-edges-review-client.log`、`textrange-edges-review-direct-provider.log`、`textrange-edges-verification.log`。

## 保留範圍的幾何變更（後續一輪）

client 新增 `geometry_changes`：保留第一個字的範圍，依序做視窗移動、F3／F4 字級、F2 字型兩次、縮短視窗、高度不足文字列、還原大小、長文字水平捲動。
每一步都檢查保留範圍的確切端點、文字、選取與 Value 不變（範圍仍可用，代表 revision 沒變）。

- 已證實並修正：視窗內容區比文字列矮時，矩形底部仍回報 `origin.y+pixels()+10`，超出內容區（實測底部336，內容區底305）。
  現在 `sync` 把文字列裁到內容區底部；內容區在文字列上方結束時高度為0，矩形與 GetVisibleRanges 都是空的。
- 移動視窗：矩形平移量與視窗位移完全相同。F3 縮小、F4 還原；兩次 F2 後還原。
- 水平捲動：捲出畫面的範圍沒有矩形；長範圍左右兩邊都被 viewport 裁掉；GetVisibleRanges 起點等於捲到左緣的字；RangeFromPoint 在左緣命中同一字。
- 結束時還原視窗位置、大小、字級、字型與捲動，最後 GL_BACK 反白取樣仍為 `(42,78,123)`。
- 點擊測試：在初始狀態、移動視窗、F3、F4、F2、第二次 F2 之後，對保留矩形左右緣內1像素做 RangeFromPoint，分別命中範圍的起點與終點。
- 窄寬度（前一輪）：客戶區最小只能到176（`SM_CXMINTRACK=198` 外框），第一個字被裁切與內容寬度為0兩種情境當時無法達成，沒有通過。
- 窄寬度（本輪已通過）：只在非互動的跨程序測試視窗（`--cross-process` 且非 `--interactive`），`WM_WINDOWPOSCHANGING` 跳過 `DefWindowProc` 的最小尺寸限制；互動視窗不變。
  實測只覆寫 `WM_GETMINMAXINFO` 沒有效果（客戶區仍為176，見 `evidence/narrow-width-minmax-only-client.log`），處理函式也沒被呼叫，所以已移除。
  使用實際視窗尺寸，不是假造 Bridge 狀態。以下每一步都檢查保留範圍的端點、文字、選取與 Value 不變：
  - 寬73（內容25）：第一個字矩形寬25，右緣等於 `client.right-24`；可見範圍1個。
  - 寬49（內容1）：矩形寬1；可見範圍1個。
  - 寬48（邊界）與30：沒有矩形，GetVisibleRanges 為0；DocumentRange 文字仍完整。
  - 還原寬度後先記錄實際矩形：寬40，因為窄寬度時「保持游標可見」把捲動留在11像素；ScrollIntoView 後完全回到原值。這是現有捲動行為，不視為缺陷。
- 渲染一致性：新增只在測試模式可用的 `WM_APP+27`，host 用與最終見證相同的方式畫到 GL_BACK（不 present），讀回單一像素。
  寬73時 UIA 矩形內為反白 `(42,78,123)`、右緣外2像素為邊界色 `(18,25,38)`；寬49的唯一內容欄為反白；寬48與30只有邊界色。
- 語意分開：**退化範圍**（範圍長度0）在正常 viewport 沒有矩形；**零 viewport**（內容寬或高為0）時範圍本身不退化，文字照常可讀，只是沒有矩形也沒有可見範圍。
- 本輪沒有發現新的程式缺陷，所以沒有紅燈修正；唯一一次失敗是測試環境的尺寸限制。

限制：
- 只測同一 DPI（144）下的字級變化，**沒有測跨螢幕 DPI 變更**。
- F2 對測試用的第一個字矩形沒有變化（此字兩種字型寬度相同），只證明切換後矩形與點擊測試一致，不能證明字型切換會改變寬度。
- 高度為0的情境只在修正後執行；紅燈在前一個斷言就停止，所以它修正前的行為未觀察。
- 只檢查文字列的上下裁切，以及捲動造成的左右裁切；沒有測視窗部分在螢幕外，或被其他視窗遮住的情況。
- RangeFromPoint 仍只依 x 座標決定位置，忽略 y。

證據：`evidence/geometry-red-short-viewport.log`、`geometry-green-client.log`、`geometry-verification.log`、`geometry-width-hit-client.log`、`geometry-width-hit-verification.log`、`narrow-width-minmax-only-client.log`、`narrow-width-green-client.log`、`narrow-width-verification.log`。

尚未驗證：實際 IME、Narrator、跨螢幕 DPI 變更、Windows 10；互動視窗的窄寬度仍受原生最小尺寸限制。

## 組字交易：測試用模型 fixture（不是 OS IME）

`WM_APP+21` 擴充成只在測試模式可用的操作碼，呼叫與 IMM 處理函式相同的 `model.start/update/commit/cancel/undo/redo` 與 `sync`：
0 取消／結束、1 原本的固定 preedit、2 開始空 preedit、3 換成固定表中的 preedit、4 提交目前 preedit、5 undo、6 redo。
跨程序傳不了字串指標，所以 preedit 用固定表索引。**這不經過 IMM、候選窗或真實輸入法，不能證明真實 IME 整合。**

client 新增 `composition_transaction`（文字「甲乙丙丁」，先選取 1:3），實測都通過：
- 開始空 preedit：Value 不變；呈現的 DocumentRange 變成「甲丁」；選取收合在 1:1；舊範圍因呈現文字改變而過期。
- 組字中外部 SetValue 與 TextRange Select 都回 `UIA_E_INVALIDOPERATION`，Value、呈現文字、選取與 revision 都不變。
- preedit 更新：在空→「ㄘ」→「測試」每一步之前各抓一個範圍，每次可見文字改變都讓剛抓的範圍過期；Value 仍是原文，選取覆蓋 preedit 1:3。
- 取消：取消前抓的範圍過期；還原原文與選取 1:3；重複取消沒有作用，且取消後抓的範圍仍有效。
- 提交：`sync` 只在呈現文字改變時增加 revision。提交前（「甲測試丁」preedit）與提交後（「甲測試丁」已確定）的呈現文字相同，
  所以**提交不會讓範圍過期**：提交前抓的範圍仍有效且文字相同；選取從 1:3 變成游標 3:3。Value 只取代一次。
  沒有組字時 fixture 拒絕提交；提交後重複結束／取消不會重複提交、還原或改變 revision。
- 一次 undo 還原整個交易（回到原文與選取 1:3，不是中間 preedit）；redo 重新套用；組字中 undo 只取消 preedit。
- 事件：交易開始時另外訂閱一個只用於此交易的處理器，並記錄 Value 事件的內容。
  - 有等到內容為已確定文字「甲測試丁」的 Value 事件。這個字串在測試中只會由提交產生，所以可歸因於提交。
  - 提交前等待300毫秒，沒有看到內容為 preedit 呈現文字（「甲丁」「甲ㄘ丁」「甲測試丁」）的 Value 事件。這只是有時限的觀察，不能排除更晚送達的事件。
  - TextChanged 與 TextSelectionChanged 只確認交易期間有收到（實測各6次），**不歸因到特定步驟**；尤其不宣稱提交本身發出 TextChanged。
- 更正：上一版文件宣稱「提交收到 TextChanged」與「preedit／取消期間沒有 Value 事件」，所用的全域計數可能被較早的事件滿足，證據不足，已改為上述說法。

沒有發現缺陷，所以沒有紅燈修正。證據：`evidence/composition-fixture-corrected-client.log`、`composition-fixture-verification.log`（舊的 `composition-fixture-client.log` 為更正前版本）。
限制：沒有測真實 IME 的 GCS 旗標組合、重轉換、候選窗位置、焦點切換造成的取消，也沒有測 Narrator 對組字的朗讀。

## 滑鼠拖曳選取（修正）

使用者回報（本機手動測試、未指明輸入法，不是獨立驗證）：互動原型無法用滑鼠拖曳選取來取代文字，Shift+方向鍵可以；其他建議的 IME 檢查使用者回報正常。

- 原因：`WM_LBUTTONDOWN` 只處理點擊與 Shift+點擊，沒有處理 `WM_MOUSEMOVE`／`WM_LBUTTONUP`。
- 紅燈：`evidence/mouse-drag-red-client.log`，`mouse forward-move selection=1:1`，`FAIL drag move extends selection from anchor`。
- 修正（`selfdraw_editor.cpp`）：
  - 按下時設定錨點並開始拖曳；移動時延伸；放開時完成。
  - 用有號的 `GET_X_LPARAM` 取座標，夾在文字 viewport 內，再呼叫共用的 layout hit test。
  - 選取寫回模型後走既有的 `sync`，所以 UIA 選取快照與 TextSelectionChanged 都沿用原路徑。
  - 下列任一情況都會結束拖曳：`WM_CAPTURECHANGED`、`WM_CANCELMODE`、`WM_KILLFOCUS`、開始組字、組字中的移動，以及沒按住按鍵的移動。
  - 組字中按下滑鼠與移動都忽略。
- 綠燈（`evidence/mouse-drag-green-client.log`）：
  - 正向與反向拖曳都得到 1:3。
  - 拖曳後打字只取代一次，undo 還原；組字 fixture 提交只取代一次，undo 還原。
  - 拖到左外側或右外側，分別夾在文件頭與文件尾；在 -30000 放開也不會出錯。
  - 三種取消訊息與無按鍵移動之後，再移動或放開都不再延伸。
  - 組字中移動與按下都被忽略；取消後還原拖曳選取，之後的移動也不延伸；最後一般點擊收合在 3:3。
- 測試方式：跨程序 client 只對專用測試 HWND 呼叫 `SendMessage`。沒有全域輸入注入、沒有移動游標、沒有改變焦點。

自動測試與真實硬體的差異：
- 非互動測試視窗不呼叫 `SetFocus`／`SetCapture`。實測背景 `SetCapture` 成功後，系統送來代表真實滑鼠狀態（按鍵未按）的移動訊息，結束了模擬拖曳（`evidence/mouse-drag-capture-interference-client.log`）；而且自動測試不應抓住使用者的滑鼠。
- 因此**真正的系統滑鼠捕捉、拖出視窗外時的訊息遞送、硬體按鍵時序都沒有自動驗證**；只驗證了視窗程序的狀態路徑。互動模式仍會呼叫 `SetCapture`，需人工確認。
- 測試期間若使用者把真實滑鼠移到測試視窗上，可能產生無按鍵的真實移動而提早結束拖曳，造成偶發失敗。
- 沒有自動捲動（拖到 viewport 外只夾在可見邊緣）、雙擊選詞、多行或完整 grapheme 行為。

## 補充平面字元（U+20000、U+1F600）

使用者回報（本機手動、新互動版本）：兩個方向的拖曳、中文取代、在視窗外放開滑鼠都正常。這是使用者的觀察，不是獨立驗證。

測試文字「繁體A」+U+20000+「中」+U+1F600+「B」（UTF-16 共9個單位）。原始碼用跳脫序列寫入，避免編碼問題。
全部經過真實的 `WM_CHAR`／`WM_KEYDOWN` 處理、共用 layout 與 UIA 範圍；沒有假造 Bridge。每次讀取 Value 都檢查是合法的 UTF-16。
結果全部通過，**沒有發現缺陷，所以沒有紅燈或修正**（`evidence/supplementary-client.log`）：
- 成對的 `WM_CHAR` 各插入一個完整字元；每個字元一個 undo 步驟（undo 一次只移除 emoji，再一次只移除 U+20000），redo 兩次還原。
- UIA Character 範圍依序回傳7個完整字元；在 emoji 位置 ExpandToEnclosingUnit 回傳完整 emoji。
- VK_LEFT 從尾端依序停在 8、6、5、3（跳過整個代理對），VK_RIGHT 從 3 到 5。
- Backspace 刪掉整個 emoji，Delete 刪掉整個 U+20000；undo 都還原。
- 選取 U+20000 後打字取代；選取「中」與 emoji 後用組字 fixture 提交取代；undo 都還原。
- host 直接檢查共用 layout：每個字形 cluster 與游標停點都不落在代理對的後半段。

缺字形與資料損壞的區別：
- layout 回報 `missing_glyphs=2`：U+20000 與 U+1F600 在目前字型與後備字型中都是 `id=0`（.notdef，畫成紅色方框）。
- 同時 Value、範圍文字與 UTF-16 單位都完整。所以畫面看到方框是**字型沒有涵蓋**，不是資料被破壞。沒有彩色 emoji，也沒有字型涵蓋測試。

邊界觀察（只記錄，不宣稱 UAX#29 grapheme）：
- `e`+U+0301+`x`：HarfBuzz cluster 讓 `e` 與組合記號成為同一個 Character 範圍。
- U+1F468 ZWJ U+1F469 `x`：被分成三個 Character 範圍（人、ZWJ、人）。這不是使用者心目中的「一個字」，游標可以停在 ZWJ 兩側，但沒有拆開任何代理對。

## WM_CHAR 代理對組合器的生命週期（修正）

問題：`WM_CHAR` 先收到高位代理時會暫存，等下一個低位代理再組成一個字元。暫存值原本只在下一個字元到來時才清除，
所以中間如果發生 UIA 寫入、選取、方向鍵、刪除、焦點離開、滑鼠點擊或組字，之後的低位代理會和舊的高位代理，在**新位置**組出一個過期的字元。

紅燈（`evidence/surrogate-interruption-red-client.log`）：9種中斷情境全部失敗，例如 `uia-setvalue` 得到「子丑」+U+20000（應為「子丑」），`key-right` 在位置2插入 U+20000。

修正（`selfdraw_editor.cpp`，只清除暫存值，不改模型或 revision 策略）。這是**有限的 WM_CHAR 組合規則**：
- 高位代理等待下一個低位代理；兩者連續到達時插入一個字元，一個 undo 步驟。
- 單獨的低位代理忽略；第二個高位代理取代暫存的高位代理（原本已是如此）。
- 以下**被接受**的編輯情境變更會丟棄暫存值：UIA SetValue／Select、編輯與導覽鍵（方向、Home、End、Backspace、Delete、Ctrl 組合鍵）、滑鼠按下、失去焦點、開始組字、測試 fixture 操作。
- 不會丟棄：F2／F3／F4（只改幾何）、被拒絕的 UIA 操作（例如多行 SetValue 回 `E_INVALIDARG`）、視窗大小或位置變更。
- 不涵蓋：Model 或 UIA 對一般不合法 UTF-16 的處理政策、grapheme 規則。

綠燈（`evidence/surrogate-interruption-green-client.log`）：每個情境都檢查確切文字、選取與 undo 邊界。
- 每個情境先寫入獨特的標記「標記」，再寫入「甲乙丙丁」並把游標放在1。
- 編輯類中斷（SetValue、Backspace、Delete）undo 回到「甲乙丙丁」；非編輯類 undo 回到「標記」，證明沒有多出 undo 步驟。
- 紅燈那次執行用的是較弱的歷史檢查（事前文字剛好等於測試文字），當時的失敗是由確切文字斷言抓到的；綠燈時已改成標記文字。
- 連續的高位＋低位、中間插入 F3、中間有被拒絕的 UIA 寫入，都插入一個字元並且一次 undo 移除；單獨低位被忽略；兩個高位時由第二個配對。

限制：
- 只用專用測試 HWND 的 `SendMessage`；沒有真實鍵盤、IME 或焦點切換。
- 真實的 `WM_KILLFOCUS` 與 IME 訊息順序可能和這裡模擬的不同。
- 沒有涵蓋 `WM_UNICHAR` 或 `WM_IME_CHAR` 路徑。
- 刻意未納入：`WM_CANCELMODE` 與 `WM_CAPTURECHANGED` 只結束滑鼠拖曳，**不會**清除暫存的高位代理，也沒有測試；
  WM_KEYDOWN 的 Ctrl+C（只複製）目前也會清除暫存值，這不是必要的，也沒有測試。

## 獨立驗證失敗：程序內 UIA roundtrip（未修正）

Codex 獨立重跑 `-CrossProcess`（未重建）在新測試之前就失敗：`UIA client HRESULT=0x80004005`，沒有 `UIA role` 行。
原始內容複製在 `evidence/independent-uia-roundtrip-failure.log`。

加入只做診斷的階段紀錄後，我連續3次都重現（同一份程式在00:51的執行曾通過）：
- 失敗階段確定是程序內 MTA 執行緒的 `ElementFromHandle`，約2毫秒回 `E_FAIL`；`CoCreateInstance` 成功。
- 這次呼叫本身沒有讓視窗收到 `WM_GETOBJECT`。
- 呼叫前，已有另一個 root `WM_GETOBJECT`（wParam `0xf360a40a`）進入 UI 執行緒處理中，之後才完成並回傳非零的 provider。

推論（未證實）：外部 UIA 請求正在 UI 執行緒處理時，程序內的 `ElementFromHandle` 會很快失敗。發送者與因果關係都沒有確定。
沒有修正，也沒有加入重試或放寬斷言；斷言條件不變，只多了階段、時間與 `WM_GETOBJECT` 的紀錄（只在測試模式輸出）。
（歷史紀錄）當時在這個失敗下，本輪的 surrogate 與先前的回歸測試無法在當時的環境完成重跑。下一節的後續診斷取代此段狀態，但**不代表已修正**。

### 後續診斷（`evidence/uia-first-use-diagnosis.log`）

**沒有找到根本原因，也沒有修正。**
- 失敗時，同一個程式裡**第一個** `IUIAutomation` 物件，對桌面、殼層視窗與本程式的原生 STATIC 控制項，連 `GetRootElement` 都回 `E_FAIL`。這表示失敗**不限於**自訂 provider；但不能據此說和本程式完全無關。
- 同一個物件稍後會成功，不是單一物件壞掉。等待實驗同時包含經過的時間、UI 執行緒處理訊息、以及期間可能的外部活動，**無法分辨哪一項是必要或足夠的**。
- 移除探針後，同一份程式再跑一次就整批通過（`ElementFromHandle` 63毫秒成功）。這只是一次觀察，**不能證明環境改變了**，也不能證明問題消失。
- 沒有證據指出根本原因在本程式或我們的程式碼之外。
- 沒有加入等待、重試、預熱或放寬 HRESULT。本次調查**沒有建立任何有正確性依據的就緒屏障**；我沒有完整查證系統是否另有正式的就緒訊號。
- 假設檢驗：
  - 1（provider 就緒）：只看自訂目標不足以解釋，因為非自訂目標也失敗。
  - 2（程序內 client／UI 執行緒副作用）：**沒有隔離**；失敗期間沒有做獨立程序的對照。
  - 3（更廣的程序或系統狀態／時間點）：仍然可能，觸發條件未知。
- 診斷用 `run.ps1 -ProbeUiaRoot`：只建置外部 client，以 `--probe-root` 開一個全新程序，做 MTA 初始化、建立 `CUIAutomation`、呼叫一次 `GetRootElement`；沒有 host、視窗、provider、執行緒、預熱、等待或重試，失敗時回非零。預設驗收不變。
  Claude 這邊（Bash 工具啟動 powershell.exe）執行一次：全部 `hr=0`，exit 0（`evidence/uia-probe-root-claude.log`）。同一時間 Codex 的 host 執行失敗；兩者啟動方式不同，而且我沒有在同一時間跑 host，所以**不能據此判斷原因**。
- 診斷用 `run.ps1 -DiagnoseUiaFailure`（含 `-CrossProcess`）：**只有在**原本的程序內 roundtrip 已經失敗後，host 繼續處理訊息，並依序啟動兩個唯讀的全新子程序：
  先以 `--probe-hwnd` 對同一個 host HWND 做第一次 UIA 呼叫 `ElementFromHandle`，並檢查 PID；再以 `--probe-root` 呼叫一次 `GetRootElement`。
  子程序最多20秒，逾時只終止自己的子程序；host 仍照原樣失敗。一般驗收不使用這個模式，也沒有重試、退路或預熱。
  Claude 執行3次，原本的 `ElementFromHandle` 每次都約60毫秒成功，**沒有捕捉到失敗**（`evidence/uia-diagnose-failure-attempts.log`）。
  這不是重現，也不代表已修正。Codex 獨立執行 `-DiagnoseUiaFailure -TraceState` 也通過，同樣沒有捕捉到自然發生的失敗。
- 診斷自我測試 `run.ps1 -TestUiaFailureDiagnostics`（含 `-CrossProcess`，只限非互動模式）：原本的 roundtrip **通過後**，注入標記為 `[SYNTHETIC]` 的人工失敗，走同一條子程序路徑；
  整次執行**預期以非零結束**，這不是自然重現。實測（`evidence/uia-diagnose-selftest.log`）：
  - 子程序1 `--probe-hwnd`（正確 PID）：Win32 擁有者與 UIA ProcessId 都是 host PID；`ElementFromHandle` 與隨後的 `GetRootElement` 都 `hr=0`；exit 0。
  - 子程序2 `--probe-root`：`GetRootElement hr=0`；exit 0。
  - 子程序3 `--probe-hwnd`（故意給錯的 PID）：回報身分不符，exit 5（預期中的受控失敗）。
  - 三個子程序都沒有逾時，handle 都已關閉；host 最後標記失敗、`cleanup_host_detached=1`、run.ps1 非零。
  - **未驗證**：20秒逾時與終止子程序的路徑、子程序啟動失敗的路徑、自然失敗當下子程序的行為。

## 不穩定結果：尚未確定根因

後續已追加20次追蹤、受控故障與例外清理修正，見 [UIA 重跑與清理回歸](UIA_STRESS.md)；原偶發根因仍未定論。
再後續的 [並行與銷毀測試](CONCURRENT_LIFECYCLE.md) 捕捉了可確認的 IME 干擾重現，已將自動測試改為不取焦點；仍不對缺少原始軌跡的歷史個案作絕對歸因。

開發此輪時曾有兩次跨程序失敗：一次 Select 後讀回的選取不符；另一次初次 SetValue 得到 `UIA_E_INVALIDOPERATION`。
後者可能涉及組字狀態或外部訊息交錯，但**沒有足夠證據判定根因**，不宣稱已修復。
加入針對性紀錄後，5 次連續測試通過；移除臨時診斷後，重建的一次測試及另 3 次連續執行通過。這些成功不足以排除先前偶發。
保留診斷／連續執行日誌，未透過跳過功能或放寬斷言使測試通過。後续應在可重現條件下隔離 OS IME 與測試訊息，並檢查選取快照的時序。

## 證據

- `evidence/textrange-build.log`：移除臨時診斷後的建置與完整情境。
- `evidence/textrange-clean-1.log` 至 `-3.log`：後續連跑。
- `evidence/textrange-diagnosis.log`、`textrange-trial-*.log`：帶臨時診斷的觀察。
- `evidence/uia-textrange-client.log`：外部程序詳細步驟。
- `evidence/uia-text-rectangle.tsv`、`textrange-selection.bmp`：跨程序回報的矩形及其反白畫面。

參考：[Microsoft Text/TextRange 指南](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-implementingtextandtextrange)。
