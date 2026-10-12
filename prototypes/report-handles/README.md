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
```
檔案：`report_model.hpp`（模型）、`main.cpp`（情境 S1–S10）、`CMakeLists.txt`（C++20、clang-cl）、`run.ps1`。`build/`、`build-asan/` 不納入版本控制。

## 結構（fixture 安排，未核定為公開介面）
- `Ledger`：背景執行緒與擁有者共用，建構時配好全部槽位；`reserve`／`fill`／`drop`／`revoke`／`drain` 都不配置記憶體。槽位有世代，撤權或結算後世代加一，舊把手因此失效。
- `ReportHandle`：持有 `weak_ptr<Ledger>`＋槽位＋世代；建構子只對 `World` 開放。`report()` 只能用一次；解構時若未用過就呼叫 `drop`。
- `World`：擁有者執行緒狀態；每個變更呼叫都檢查執行緒。`settle()` 每次最多結算 8 筆。
- 交接遞送一次預留 4 個位置（回呼結束、工作結束、租用歸還、交接確認），不足則全部不承擔。
- 「逐筆明確確認」契約下，`collect()` 對每份結果各發一個確認把手。

## 證據狀態（誠實紀錄）
- **作者（Claude Code）自行執行**：
  - Release：S1–S10 共 41 項檢查通過（`evidence/claude/scenarios-release.log`）。
  - ASan：39 項通過；S3「不配置記憶體」的 2 項在 ASan 下跳過，因為 ASan 接管 `operator new`（`evidence/claude/scenarios-asan.log`）。已用 `llvm-objdump` 確認執行檔連結 `clang_rt.asan_dynamic-x86_64.dll`。
  - **變異測試**：故意讓「丟棄把手不通知」→ 7 項失敗；故意讓「關閉不等結果交付」→ 3 項失敗；還原後全數通過。變異未保存為檔案。
- **尚未做**：Codex 獨立審查與反例探測。

## 限制
- **沒有 ThreadSanitizer**：Windows 版 clang 不提供，WSL 未安裝 g++。資料競爭只靠 mutex 設計與壓力情境間接觀察，不能宣稱已證明無競爭。
- Ledger 用單一 mutex；沒有測公平性、延遲或大量執行緒下的效能。
- 「收尾安全」仍是宣告，不是偵測；擁有者處置遺失回報（`resolve_lost`）是 fixture 介面，ADR 尚未核定。
- 診斷只有計數，沒有有上限的明細紀錄；拒絕原因只有三種。
- 結清紀錄在單一情境內不設上限。
- 防護範圍依 Q139：只防程式錯誤。同程序內的程式碼仍可直接改寫 `World` 的公開欄位。
- 沒有暫停／恢復、接收途徑、共享服務隔離；這些由 JavaScript 模型涵蓋，兩者未整合。
- 沒有真實宿主事件迴圈、GPU、低記憶體注入或程序終止。
