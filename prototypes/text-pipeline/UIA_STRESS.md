# UIA 重跑、故障注入與清理回歸

2026-10-08。本輪追查 TextRange 的兩次偶發失敗，沒有宣稱已找出兩者根因。
後續負載測試已捕捉同類失敗的真實 IME 干擾來源，並修正自動視窗焦點與 provider 解除連線問題，詳見 [並行與銷毀測試](CONCURRENT_LIFECYCLE.md)。

## 已確認結果

1. 帶狀態軌跡的原情境連續 20 次通過；只有預期的 WM_CHAR／按鍵／測試訊息，沒有額外 WM_IME_STARTCOMPOSITION 或 WM_IME_COMPOSITION。
2. 新腳本為每次執行建立獨立目錄，保存 host/client log、矩形／影像、state-trace.tsv 及 summary.csv，避免後一次覆寫前一次證據。
3. 兩個受控故障都能讓原斷言失敗：初次 UIA 寫入前先組字；TextRange Select 後改動游標。這只驗證偵測能力，不證明原偶發就是相同原因。
4. 負面情境發現並修正**例外清理的失效指標**：EditorHost 已銷毀後，Context 的 DestroyWindow 仍可能觸發視窗回呼，透過 global host 存取失效模型。
5. 改由 HostBinding RAII 先清空 host、還原視窗程序，再銷毀 EditorHost。兩個負面情境確認 cleanup_host_detached=1，軌跡沒有超出文字長度的選取；正常情境補跑 3 次通過。
6. 修正 Windows PowerShell 腳本：先收集 native stderr 和 exit code，再保存摘要並拋出失敗，避免 ErrorActionPreference=Stop 在保存摘要前退出。

修正前的 fault trace 曾在 WM_KILLFOCUS 看到長度已為0，但 anchor/caret 仍為10或1；來源碼的物件解構順序確認了錯誤路徑。修正後關閉訊息不再進入該模型回呼。

## 重跑方式

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/stress-uia.ps1 -Trials 20
```

每次都建立新的專用 host/client；不操作原先開啟的互動視窗或系統剪貼簿。
預設遇第一個失敗就停止，輸出證據位置。原始產物在 build/stress/<run-id>/，不全部提交到 Git。
有界 trace 最多保留最近2048筆，僅在 `--trace-state` 啟用；包含事件序號、thread、訊息種類、文字長度與選取／組字狀態，不記錄文字內容。

故障控制（預期 exit=1）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/stress-uia.ps1 -Trials 1 -Fault composition
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/stress-uia.ps1 -Trials 1 -Fault selection
```

驗證故障被正確捕捉與清理（兩項都符合時此指令 exit=0）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/text-pipeline/verify-uia-faults.ps1
```

不會略過或放寬原 UIA 斷言；負面回歸還會確認失敗原因、回呼解除與狀態範圍不變量。
注入只允許在 cross-process 測試模式，不是正式編輯器功能，也不是實際 IME 驗證。

## 證據與未解項目

- `evidence/trace-stress-summary.csv`：20次原情境與訊息統計。
- `evidence/stress-sample-host.log`、`stress-sample-trace.tsv`：其中一份完整樣本。
- `evidence/*-before-teardown-fix.tsv`：修正前的負面情境。
- `evidence/*-fault-host.log`、`*-fault-client.log`、`*-fault-trace.tsv`：修正後負面情境。
- `evidence/fault-verification.log`：可重跑的負面回歸結果。
- `evidence/stress-runner-check.log`：修正後正常3次。

仍未定論：先前 Select 後選取不符及初次 SetValue=0x80131509 的觸發原因。不能將這個清理修正、20次成功或受控注入視為原偶發已修復。
真實 IME、Narrator、完整 Unicode／動態 ranges、跨 DPI／其他 OS 仍是独立缺口。
