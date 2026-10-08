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
- 只有部分文字屬性；未知屬性回傳 UIA reserved NotSupported 值。FindAttribute、AddToSelection 等分支尚未全面測試。
- preedit 出現在可見文字快照，會使 range revision 改變；實際 IME、重轉換與輔助工具對此行為尚未驗證。
- 相同文字的重新提交等事件語意、provider shutdown 競態、完整失效契約仍需測試。
- 實際候選窗、Narrator、跨螢幕 DPI、Windows10 及其他 OS 尚未通過。

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
