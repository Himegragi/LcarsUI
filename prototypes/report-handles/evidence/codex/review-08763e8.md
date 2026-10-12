**`08763e8` 的主要行為通過本輪驗證，未發現新的遺失回報、重複通知或提前釋放交接。** 有一項低嚴重度查詢限制，以及 README 的既有過度宣稱。**TSan 未能執行：WSL 回傳 `E_ACCESSDENIED`。**

確認分支指向指定提交，原型檔案與提交內容一致。前後核對 284 個受追蹤檔案，沒有任何內容變更；未提交。新增檔案及建置輸出皆在指定的兩個目錄。

**執行命令與結果**

以下命令從儲存庫根目錄執行，`$p = 'prototypes/report-handles/build-codex'`。腳本沿用 `run.ps1` 的 VsDevCmd、clang-cl 22.1.8、CMake／Ninja 工具鏈，調整輸出目錄。

| 命令 | 本輪結果 |
|---|---|
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$p/verify-08763e8.ps1"` | Release：81 通過、0 失敗 |
| 同上，加 `-Asan` | ASan：75 通過、0 失敗 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$p/probe-08763e8.ps1"` | 補測 3,649 次檢查通過；原探針失敗數 0 |
| 同上，加 `-Asan` | 補測 3,603 次檢查通過；原探針失敗數 0，無 ASan 報告 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$p/negative-08763e8.ps1"` | 四種禁止操作均正確編譯失敗 |
| `wsl.exe -e bash prototypes/report-handles/run-tsan.sh` | 啟動 WSL 即遭拒，未進入腳本 |

另以 `llvm-objdump -p` 確認三個 ASan 執行檔實際連結 ASan runtime。

[Release 補測日誌](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-08763e8/probes-release.log) · [ASan 補測日誌](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-08763e8/probes-asan.log) · [測試原始碼](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-08763e8/probe.cpp)

**驗證涵蓋**

- `declare_ended`：三種合法項目分別測試仍持有、已回報但未結算、已正常結算、丟棄但尚未偵測、重複宣告；也測未知交接、三種不合法種類及 enum 值 255。
- 完成與釋放：8 種正常回報／宣告組合 × 24 種完成順序，共 192 組。交接只在三項皆完成且確認已結算後釋放；宣告與正常回報紀錄分開，步驟邊界不變式成立。
- 改派：分別遺失 callback／work；宣告其中一項後，另一項未結束仍阻止遞送。兩項結束後可以再遞送；舊 lease 未結束仍保留交接。
- 重入：指定的 9 個方法，加上 `add_env`、`fault`，各測接收者自行捕捉及讓例外逸出兩種情況。皆拒絕變更，外層批次完整；`declare_ended` 回傳 `Reentrant`。
- 通知：背景執行緒丟棄，owner 執行緒送達；接收者拋例外不重送；未登記時保留可查詢狀態。
- 批次重試：安排「第一則通知 → ResultAck 配置失敗 → 第二則通知」。掃描 8 個配置位置，實際觸發一次 `bad_alloc`；重試後兩則各一次，結果亦只結清一次。
- Release 零配置：9 個遺失項跨兩批處理，分別使用未登記、純計數、拋整數例外的接收者，皆為 **0 次 C++ 配置**。宣告紀錄配置失敗另驗證可安全重試。

## Spec

**低嚴重度，已實測：接收者內的 `invariants()` 會回報暫態不一致。**

重現步驟：

1. 建立 World、環境、交接並 `deliver`。
2. 登記接收者，在其中呼叫 `w.invariants()`。
3. 丟棄 callback 把手，再呼叫 `settle()`。
4. 接收者內得到 1 項錯誤；`settle()` 返回後得到 0 項。

原因是 [report_model.hpp:348](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/report_model.hpp:348) 先設 `lost` 並通知，槽位到第 210 行才歸還；不變式計算此時已排除 lost 義務。

這是**查詢時機限制，不是持續帳目損壞**。「唯讀查詢仍可用」不能解讀為接收者內具有一致快照。可將不變式檢查限定在結算邊界，或使檢查理解處理中的槽位。

其餘本輪所測 Q143–Q145、ADR 第 171／173 行等待與撤權規則，未發現違反。

## Standards

未發現本次變更違反已讀取的適用規範。README 有以下證據界線：

- **低嚴重度，程式碼確認：** [README:55](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/README.md:55) 仍將「世界已銷毀」列入診斷計數。重現路徑是保留把手、銷毀 World、再 `report()`；原探針實測回傳 `WorldGone`，但 [report_model.hpp:125](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/report_model.hpp:125) 直接返回，沒有計數，也已沒有存活帳本。這是既有文件問題。
- **零配置宣稱需限定範圍：** 第 41 行的證據支持框架路徑搭配無配置接收者。任意接收者、`lost_parts()` 查詢或例外建構可能配置；內建接收者使用 `vector::push_back` 就可能配置。此擴大解讀的反例僅讀碼確認。
- 第 45 行「所有變更方法」有實作支持，但內建重入情境只測 7 個方法。本輪補齊其餘 4 個並通過，不能把原作者情境本身描述成逐方法完整覆蓋。

**原探針的修改**

[本輪原探針](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-08763e8/original.cpp) 相對 `b534d90` 已驗證版本，只有以下 API 必要調整，已比對確認：

1. `ownerNotices.size()` 改為 `lost_parts().size()`。
2. 整個交接的 `resolve_lost` 改成 callback／work／lease 三次 `declare_ended`。
3. 移除「cleanupSafe 為 false 時必須拒絕」的舊期待，改驗證逐項宣告不依賴該旗標。

相對最早 `67f7f65/probe.cpp`，另沿用前輪必要調整：include 路徑、F1 空 delivery／已刪除 handoff 防護，以及 F2／F3 拆成負向編譯。四項負向編譯本輪均重跑。

原壓力測試兩種配置皆完成 4,000 筆：2,666 正常回報、1,334 丟棄，帳目錯誤 0；生命週期測試各完成 300 輪、1,800 個把手。

**限制：** 沒有本輪 TSan；ASan 不執行 allocator 替換及失敗注入；配置計數不涵蓋 CRT／系統配置；有限交錯不能證明不存在競態。Git 指令因 WSL 不可用，版本與檔案核對改用唯讀解析 Git 物件。

Spec：1 項低嚴重度查詢限制；Standards：0 項硬規範違反，文件有上述宣稱與證據界線。