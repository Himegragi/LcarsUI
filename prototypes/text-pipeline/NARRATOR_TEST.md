# Narrator 人工驗收表（自繪單行輸入框原型）

所有項目**尚未測試**，結果由使用者填寫。這份表不規定 Narrator 必須說出的確切字句：實際語音會受聲音、語言、詳細程度與其他設定影響。
本表也不指定 Narrator 快捷鍵；請依 Microsoft 官方說明操作。請勿為了測試修改 Narrator 的詳細程度、語言、聲音或掃描模式設定，並記錄當時實際使用的設定。

## 啟動

在 workspace 根目錄：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/run.ps1 -ManualNarrator
```

- 執行 `editor_cross_host.exe --interactive --manual-narrator`。
- 一般的 `--interactive` 在進入人工迴圈前，會先跑自動檢查：送 WM_CHAR／Backspace、組字 fixture、程序內 UIA client 的 SetValue 等，會改動文字與 UIA 狀態。
  `--manual-narrator` 跳過**全部**這些步驟，不啟動任何 UIA 測試 client，也不使用 `--cross-process` 測試 fixture。
- 初始文字「自繪輸入：繁體中文 ABC」（直接設定，不是可 undo 的編輯），游標在文字尾端，字級24 DIP，視窗取得焦點。
- 關閉視窗即結束。

## provider 實際提供的資訊（程式碼確認，不代表 Narrator 會這樣念）

- Name：`LcarsUI 自繪文字輸入框`；Control type：Edit；AutomationId：`lcars.prototype.text`。
- Value pattern（可寫）與 Text pattern（單行 TextRange，`SupportedTextSelection_Single`）。
- 事件：Value 變更、TextChanged、TextSelectionChanged、焦點變更。

## 驗收項目

每項請記錄：Narrator 版本／語言／聲音、實際聽到的內容（大意即可）、畫面上的實際狀態、結果（通過／不通過／不確定）。

| # | 操作 | 觀察重點 | 結果 |
| --- | --- | --- | --- |
| 1 | 讓輸入框取得焦點 | 是否提到名稱與「編輯」類角色 | 待測 |
| 2 | 用 Narrator 的方式朗讀目前這一行／全部內容 | 內容是否與畫面文字一致 | 待測 |
| 3 | 左右方向鍵逐字移動 | 念出的字是否與游標實際經過的字一致；中英文交界處 | 待測 |
| 4 | Home／End | 游標位置與朗讀是否一致 | 待測 |
| 5 | Shift+方向鍵選取 | 是否提到選取或選取內容；與畫面反白是否一致 | 待測 |
| 6 | 滑鼠拖曳選取 | 是否有選取相關回饋（可能沒有） | 待測 |
| 7 | 輸入英文字母、Backspace、Delete | 修改後朗讀的內容是否反映新文字 | 待測 |
| 8 | Ctrl+Z／Ctrl+Y | undo／redo 後朗讀內容是否與畫面一致 | 待測 |
| 9 | 中文輸入法：開始組字後按 Esc 取消 | 取消後文字與朗讀是否回到原狀；組字中念了什麼 | 待測 |
| 10 | 中文輸入法：組字後確定送出 | 送出後朗讀是否為確定的文字；是否重複或缺字 | 待測 |
| 11 | 視窗失去焦點再回來 | 回到輸入框時的朗讀 | 待測 |

## 已知原型限制（可能直接影響 Narrator）

- 只實作 `ITextProvider`（Text pattern），沒有 `ITextProvider2`（TextPattern2，例如 caret range）。這是**可能的**相容性缺口：Narrator 是否依賴它、在本原型會造成什麼差異，都尚未驗證。
- TextRange 綁定文字 revision：文字（含組字中的 preedit）一改，舊範圍就回 `UIA_E_ELEMENTNOTAVAILABLE`。輔助工具若保留舊範圍，可能讀不到或需重新取得。
- Character 單位沿用 HarfBuzz cluster，不是 UAX#29 grapheme；組合 emoji（ZWJ）中間可以停游標。
- Word、Line、Paragraph 等單位沒有獨立實作，會擴大成整行／整份文件。
- 只有單一選取；只有部分文字屬性（IsReadOnly），其他屬性回 NotSupported。
- 組字中的 preedit 出現在呈現文字與 TextRange，但不在 Value。
- 補充平面字元（例如 U+20000、U+1F600）在目前字型顯示為方框；資料本身完整。
- 只支援單行文字；沒有測過跨螢幕 DPI 變更；只在 Windows 11 測過。

## 先前的人工觀察（使用者回報，與 Narrator 無關，保留於此供參考）

- 滑鼠：修正後在新互動版本中，兩個方向拖曳、中文取代、在視窗外放開滑鼠，使用者回報正常。
- IME：使用者在本機（未指明輸入法）回報建議的 IME 檢查正常。
這些都不是獨立驗證，也不代表 Narrator 行為。

## 結果紀錄

- 測試日期：
- Windows 版本／Narrator 版本、語言、聲音：
- 使用的輸入法：
- 備註：
