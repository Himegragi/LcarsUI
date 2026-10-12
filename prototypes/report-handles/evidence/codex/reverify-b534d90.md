**F1–F8 的原反例均已修正。** 本輪未發現新的回報遺失或重複结算，但確認一項低嚴重度配置成長退化，以及 README 診斷宣稱仍過廣。**TSan 因 WSL 存取遭拒，未能驗證。**

[完整報告、命令與證據索引](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-b534d90.md>)

HEAD／分支指向 `b534d901ed9debf26987240141be864dbd9d4630`。透過唯讀解析 Git 物件比對，282 個受追蹤檔案符合該提交，驗證前後內容未變更。沒有提交；新增檔案全在指定兩個 build 目錄。

**執行結果**

使用 `run.ps1` 的工具鏈：clang-cl 22.1.8、VsDevCmd、CMake、Ninja；調整輸出至指定目錄。

| 執行 | 原始結果摘要 |
|---|---|
| `reverify.ps1`／`-Asan` | Release：`failures=0 pass=55`；ASan：`failures=0 pass=51` |
| `original.ps1`／`-Asan` | 皆 `probe_unexpected_failures=0` |
| `negative.ps1` | 四種禁止操作均編譯 exit 1，拒絕原因正確 |
| `expanded.ps1`／`-Asan` | 皆 `expanded_failures=0` |
| 原壓力探針 | 每配置 4,000 命令；2,666 受理、1,334 丟棄；帳目錯誤 0；每次最多 8 筆 |
| 原生命週期探針 | 每配置 300 輪、1,800 把手；ASan 無報告 |
| `js-compare.cjs` | 10 組、385 步、110 項檢查全部通過 |
| `llvm-objdump -p` | 確認連結 ASan runtime DLL |
| `wsl.exe -e bash prototypes/report-handles/run-tsan.sh` | `Wsl/Service/CreateInstance/E_ACCESSDENIED`，未進入腳本 |

完整命令列與 stdout／stderr 均見報告及相鄰日誌；未採信作者的測試日誌。

**前次發現裁決**

| 項目 | 裁決 | 獨立證據 |
|---|---|---|
| F1 重複遞送 | 已修正 | 再遞送拒絕；`live=1 inv=0`；舊 ack 正常完成 |
| F2 World 複製 | 已修正 | 實際編譯失敗：deleted constructor |
| F3 公開 Ledger 寫入 | 已修正 | 實際編譯失敗：不能對 `const Ledger` 呼叫 `fill` |
| F4 取消結果寫回 | 已修正 | 環境值維持 0，與 JS 一致 |
| F5 不安全收取 | 已修正 | `receipts=1 outcomes=0 ended=0`；稍後安全處置成功 |
| F6 重複回報診斷 | 已修正原反例 | 第二次回報 `Empty`，計數為 1；廣義診斷宣稱仍有缺口 |
| F7 建立失敗洩漏 | 已修正 | 注入後預留、命令、回條及不變量錯誤皆為 0 |
| F8 結算例外遺失 | 已修正 | 中途失敗後可重試，通知／結清紀錄不重複 |
| wrong-thread counter | 已修正 | 原始碼為 atomic；有限雙執行緒測試計數正確，未經本輪 TSan |
| stale registrations | 已修正，原始碼確認 | reassign／abandon 撤權時同步移除對應登記 |

原探針只調整 include、將 F2／F3 拆成負向編譯，以及防止 F1 修正後解參考空的第二次 delivery、查詢已刪除 handoff；其餘原測試保留。

**修正邊界驗證**

- `complete` 重複呼叫、撤權後呼叫、槽位重用後用舊 ref 呼叫，均不會額外歸還容量；Reserved 槽位不能提前完成。
- F1 在 ack 為 Reserved／Filled／Dropped 時都拒絕再遞送；之後 reassign／redelivery 正常，舊 lease 仍保留。
- F5 的 collect／abandon 不安全拒絕，以及稍後安全清理，兩種契約均通過。
- 八種配置注入情境各掃描位置 0–47，**共觸發 24 次實際 `bad_alloc`**，涵蓋 deliver、collect、abandon、record、通知與 ack 結算；恢復後沒有遺失或重複紀錄。

正常模型狀態下，各 `apply` 分支的配置均在業務變更前完成，支持**逐筆邏輯狀態的 strong guarantee**。整批 settle／collect／abandon 並非交易：前筆成功不會因後筆失敗而回滾。

**新增發現**

1. **低嚴重度，已執行：配置成長退化。**  
   `record()` 每次 `reserve(size+1)`，在本機實作造成平方級累計配置量。同一探針收取 1,024 筆結果：
   
   | 版本 | 配置次數 | 累計請求 bytes |
   |---|---:|---:|
   | `67f7f65` | 18 | 154,017 |
   | `b534d90` | 1,025 | 25,259,828 |

   這是累計請求量，**不是峰值記憶體或耗時**。通知 vector 也有同型寫法，該部分僅原始碼確認。

2. **低嚴重度，已執行：README 診斷宣稱過廣。**  
   將把手移走後，對 moved-from 把手回報會得到 `Empty`，但診斷計數仍為 0。因此「用過把手重複回報有計數」已修正，「所有無效回報都有計數」僅部分成立。

README 的 Release／ASan 數字已重現；TSan 與前版控制組仍只是作者聲明。「最多 8 筆」也不能解讀為固定執行時間。

限制包括：沒有本輪 TSan、真實系統低記憶體或完整配置攔截；ASan 不執行替換 allocator 的注入測試；未實測私有登記表長期大小。有限通過不構成全面符合 Q135–Q143 或不存在競態的證明。