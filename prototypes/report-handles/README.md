# 可丟棄 C++ 原型：回報把手（ADR 0003 Q135–Q143）

**用完即丟。**不是框架程式碼，也不是 API 提案。只有主控台情境，沒有視窗。

## 問題
JavaScript 模型 `prototypes/result-delivery-contracts/` 已驗證 Q135–Q138 在模型內成立。這裡只測 C++ 才測得出來的部分，並加入 Q139–Q143 的回報把手：
- 把手只能移動、不能複製，外部無法由公開編號拼出（Q140／Q142）。
- 把手未使用即被銷毀時，框架能偵測：命令結果自動結算為「提供者失效」；回呼／工作／租用維持未結清、標記回報者遺失並通知擁有者（Q143）。
- 真實 `std::thread` 背景執行緒轉交、回報、丟棄把手；擁有者執行緒有界結算。
- 回報與丟棄路徑不配置記憶體（Q135：保存不依賴可用記憶體）。
- 無效回報不改狀態，回傳原因並記診斷計數（Q141）。
- 世界先銷毀、背景執行緒才回報：不崩潰，回傳「世界已銷毀」。
- Q136／Q137／Q138 的收尾、承接成立點、擁有者放棄，以 C++ 型別重做一次。

## 一個指令
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/run.ps1         # Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/run.ps1 -Asan   # AddressSanitizer
wsl.exe -e bash prototypes/report-handles/run-tsan.sh                                               # ThreadSanitizer（WSL clang）
```
檔案：`report_model.hpp`（模型）、`main.cpp`（情境 S1–S11）、`CMakeLists.txt`（C++20、clang-cl）、`run.ps1`、`run-tsan.sh`。`build*/` 不納入版本控制。Windows 版 clang 沒有 TSan，因此 TSan 在 WSL（Ubuntu 26.04、clang 21.1.8）編譯同一份原始碼。

## 結構（fixture 安排，未核定為公開介面）
- `Ledger`：背景執行緒與擁有者共用，建構時配好全部槽位；`reserve`／`fill`／`drop`／`revoke`／`peek`／`complete` 都不配置記憶體。外部只拿得到唯讀檢視。槽位有世代，撤權或結算後世代加一，舊把手因此失效。
- `ReportHandle`：持有 `weak_ptr<Ledger>`＋槽位＋世代；建構子只對 `World` 開放。`report()` 只能用一次；解構時若未用過就呼叫 `drop`。
- `World`：擁有者執行緒狀態，不可複製；每個變更呼叫都檢查執行緒。`settle()` 每次最多結算 8 筆，先處理再歸還槽位：處理途中拋例外時，回報留在帳本，下次結算再處理。每筆處理先完成所有配置，再做不會失敗的變更。
- 交接遞送一次預留 4 個位置（回呼結束、工作結束、租用歸還、交接確認），不足則全部不承擔。舊確認把手未用掉時拒絕再遞送，須先改派撤權。
- 「逐筆明確確認」契約下，`collect()` 對每份結果各發一個確認把手。

## 證據狀態（誠實紀錄）
- **第一版 `67f7f65`**：作者 41 項通過、ASan 39 項通過；變異測試（丟棄把手不通知 → 7 項失敗；關閉不等交付 → 3 項失敗）證明情境有抓錯能力。
- **Codex 審查 `67f7f65`**（`evidence/codex/review-67f7f65.md`，探針原始碼與輸出在 `evidence/codex/review-67f7f65/`）：實測重現 F1 重複遞送破壞確認與帳目、F2 World 可複製而共用帳本、F8 結算途中配置失敗遺失回報、F7 建立命令途中配置失敗洩漏預留、F4 取消結果寫回環境、F5 不安全收尾仍交付、F6 重複回報無診斷、F3 公開 Ledger 可繞過把手；原始碼審查另指出錯誤執行緒計數器的資料競爭與撤權後登記未清除。
- **修正（本版）**：S11 先重現上述問題（紅燈輸出 `evidence/claude/scenarios-red-67f7f65-findings.log`：10 項失敗），再修模型：
  F1 有未用確認時拒絕再遞送；F2 禁止複製 World；F3 只回傳 `const Ledger&`；F4 取消結果不寫回；F5 收尾不安全時 `collect` 不交出；F6 用過的把手再回報也計入診斷；F7 建立義務失敗時退回預留；F8 結算改為先處理後歸還、處理時先配置後變更；計數器改為 atomic；撤權時清除登記。
  F7／F8 以配置失敗注入驗證：讓第 0～39 次 `operator new` 各失敗一次，每次都檢查帳目與不變式。
- **作者執行（本版）**：Release 55 項通過；ASan 51 項通過（配置計數與配置失敗注入共 4 項跳過，因 ASan 接管 `operator new`）；TSan 51 項通過且 0 則資料競爭報告（`evidence/claude/scenarios-{release,asan,tsan}.log`）。
- **TSan 對照組**：以同一份情境搭配修正前的模型，TSan 回報 `World::owner()` 的資料競爭，即 Codex 原始碼審查指出的位置（`evidence/claude/tsan-control-67f7f65.log`）。證明 TSan 在此設定下確實能抓到競爭。
- **尚未做**：Codex 再驗證本版。

## 限制
- TSan 在 Linux（libstdc++）上跑，不涵蓋 Windows 標準函式庫或 Windows 特有的執行緒行為；TSan 只能報告實際執行到的交錯，不證明不存在競爭。
- 同一個把手不能被兩條執行緒同時操作（單一持有者的前提）；這不是框架能偵測的錯誤。
- 配置失敗注入只攔截 C++ `operator new`，不涵蓋 CRT 或系統配置。
- Ledger 用單一 mutex；沒有測公平性、延遲或大量執行緒下的效能。
- 「收尾安全」仍是宣告，不是偵測；擁有者處置遺失回報（`resolve_lost`）是 fixture 介面，ADR 尚未核定。
- 診斷只有計數，沒有有上限的明細紀錄；拒絕原因只有三種。
- 結清紀錄、`commands` 歷史在單一情境內不設上限。
- 防護範圍依 Q139：只防程式錯誤。同程序內的程式碼仍可直接改寫 `World` 的公開欄位。
- 沒有暫停／恢復、接收途徑、共享服務隔離；這些由 JavaScript 模型涵蓋，兩者未整合。
- 沒有真實宿主事件迴圈、GPU、低記憶體注入或程序終止。
