# 並行讀寫、真實輸入干擾與 provider 銷毀

2026-10-08，本機 Windows11／RTX3060。延續可丟棄文字原型，並未完成 Narrator、實際 IME 端到端或跨平台驗收。

## 執行

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/run.ps1 -Concurrency -Lifecycle -TraceState
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/stress-uia.ps1 -Trials 3 -Concurrency -Lifecycle
```

每輪先跑原跨程序契約與文字範圍檢查，再測兩個 MTA 讀取者與一個寫入者，最後保留外部介面並銷毀專用測試 HWND。
互動模式不能與 Lifecycle 合用，以免關閉正在人工操作的測試視窗。

## 並行讀寫結果

每輪40次 Value 寫入、兩個讀取者各100次 DocumentRange/GetText；每個讀取者使用自己的 UIA client。
成功的字串必須完整符合本輪已寫入的版本格式；若讀取期間文字 revision 改變，接受且只接受明確的 UIA_E_ELEMENTNOTAVAILABLE。
不要求跨兩個 API 呼叫取得同一版本，因為那並非目前介面的原子性保證。

| 輪次 | 完整讀取 | 明確過期 | 無效結果 | 與讀取重疊的寫入 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 125 | 75 | 0 | 39 |
| 2 | 123 | 77 | 0 | 39 |
| 3 | 122 | 78 | 0 | 39 |

共600次讀取，370次完整快照、230次明確過期，沒有不完整文字或非預期 HRESULT；120次寫入完成。
這只驗證目前單UI執行緒寫入、有鎖快照與revision失效策略，不外推為最終動態anchor或全面並行安全。

## 找到的一個不穩定來源

首輪負載測試在 Select 成功後、Clone 前失敗。軌跡記錄了非測試注入的事件：

- 序號113–115：選取0:1，UIA快照也為0:1。
- 116之後：VK_PROCESSKEY（229）、WM_IME_STARTCOMPOSITION（269）、多筆 WM_IME_COMPOSITION（271）。
- preedit 長度由0變1、2、3，導致可見文字 revision 改變，舊範圍因此依契約失效。

原來自動測試視窗會取得前景焦點，讓真實输入法事件進入固定測試情境。
修正：自動 GL／文字測試視窗使用 WS_EX_NOACTIVATE 與 SW_SHOWNOACTIVATE，不主動 SetFocus；只有 Interactive 模式會取得焦點。
測試明確檢查自動視窗在 UIA 請求期間未成為前景視窗。沒有忽略 IME 訊息或放寬舊範圍的失效判斷。
畫面證據改讀自己的 GL_BACK，再呈現，不依賴前景或視窗遮擋情況。

這是有軌跡支持的同類失敗來源；先前兩次沒有完整軌跡的歷史失敗，不能逐次確定也出自同一來源。

## provider 銷毀發現與修正

初版測試把 provider 標為不可用後，get_HostRawElementProvider 也立即拒絕，導致 UiaDisconnectProvider 無法取得身分，返回 E_INVALIDARG（0x80070057）。
當時 TextRange 已自行拒絕存取，但高階 Value getter 回 S_OK／空字串；GetCurrentPropertyValueEx(ignoreDefault=true) 顯示 unsupported，不能算成功清理。

修正為三階段：
1. 停止 Value/Text 操作、解除視窗回呼與重新匯出 provider 的路徑。
2. 在 disconnecting 階段保留 HWND 身分查詢，供 UiaDisconnectProvider 完成清理。
3. 清理返回後關閉身分查詢，再銷毀 HWND；避免舊 handle 重用時誤指向新視窗。

修正後三輪皆確認：
- UiaDisconnectProvider 返回 S_OK。
- 原始 provider 的讀值與寫值立即回 UIA_E_ELEMENTNOTAVAILABLE。
- 真實 HWND 已不存在，而外部 client 仍保留 Value、TextPattern 與 TextRange。
- 外部讀值、寫值、DocumentRange、GetText 及不取預設值的屬性查詢，全部回 UIA_E_ELEMENTNOTAVAILABLE（0x80040201）。
- client 釋放介面後程序正常結束；沒有使用預設空值或任何失敗碼作寬鬆通過條件。

參考：[UiaDisconnectProvider](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiadisconnectprovider)、[不取預設值的屬性查詢](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationelement-getcurrentpropertyvalueex)。

## 證據與回歸

- `evidence/concurrent-first-failure.log`、`concurrent-failure-trace.tsv`：真實 IME 干擾的失敗。
- `evidence/concurrency-lifecycle-summary.csv`、`lifecycle-run-1.log` 至 `-3.log`：修正後三輪完整紀錄。
- `evidence/lifecycle-diagnosis.log`：清理單項驗證。
- `evidence/fault-regression-after-lifecycle.log`：既有兩個受控失敗仍能被捕捉且安全清理。
- `evidence/gl-noactivate-regression.log`：最小 GL Release 的渲染、resize與context重建回歸。

原實際輸入法、Narrator、完整Unicode規則、跨DPI與Windows10／其他OS驗收仍待完成；本輪的組字是自動情境之外的干擾證據，不是輸入法正確性證明。
