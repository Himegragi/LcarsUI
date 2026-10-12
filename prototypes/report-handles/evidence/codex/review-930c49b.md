**Q149／Q150 的本輪實測通過，未發現通知遺失、重送或巢狀批次破壞帳目的問題。Q147 仍有一項計數契約落差，README 也有過度宣稱與過期描述。**

原型內容已核對符合 `930c49b`；前後比對 289 個 tracked 檔案，沒有修改。所有新增檔案與建置輸出均在指定目錄，未提交。

**命令與結果**

以下從儲存庫根目錄執行，`$p = 'prototypes/report-handles/build-codex'`。建置腳本是 `run.ps1` 的副本，只調整來源／輸出路徑；每次均啟動全新 `powershell.exe`。

| 命令 | 結果 |
|---|---|
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$p/verify-930c49b.ps1"` | Release：92 通過、0 失敗 |
| 同上，加 `-Asan` | ASan：84 通過、0 失敗 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$p/probes-930c49b.ps1"` | 舊探針 3,693 次檢查、新補測 4,654 次檢查通過；`original.cpp` 失敗數 0 |
| 同上，加 `-Asan` | 舊探針 3,647 次、新補測 3,183 次檢查通過；`original.cpp` 失敗數 0，無 ASan 報告 |
| `wsl.exe -e bash prototypes/report-handles/build-codex/run-tsan-codex.sh` | WSL 啟動即回報 `Wsl/Service/CreateInstance/E_ACCESSDENIED`；未執行 TSan |

四個 ASan 執行檔皆以 `llvm-objdump -p` 確認連結 ASan runtime。TSan 副本也只調整輸出路徑。

[Release 日誌](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/probes-release.log) · [ASan 日誌](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex-asan/probes-asan.log) · [新增測試原始碼](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-930c49b/q150.cpp)

**驗證涵蓋**

- **Q150 重入：**逐項實測 `settle`、成功的 `deliver`、`reassign`、實際收取結果的 `collect`、`abandon`、`declare_ended`、`close`、`set_cleanup_safe`；另涵蓋 `start_command`、`add_env`、`fault`。
- **真正的巢狀批次：**15 則通知，外層處理 8 筆，接收者內巢狀處理剩餘 7 筆。每則恰好一次；巢狀非空／空批次返回後仍拒絕替換接收者，最外層返回後可替換。
- **例外與重試：**新增補測實際觸發 **20 次 `bad_alloc`**：逐配置點掃描 9 次、三個容器成長點各連續失敗三次、巢狀批次失敗兩次。已完成項目的通知保留，重試不重送；原例外仍傳出。
- **接收者例外：**部分通知拋例外，其餘照送；包含接收者將巢狀批次的 `bad_alloc` 再拋出。`receiverFailures`／`lostNotified` 與實際呼叫相符。
- **Q149：**讀碼確認三類義務只由 `deliver()` 發行；未登記與空接收者均不留下部分義務或預留。`fault()` 尚未承擔三類義務；`start_command()`／`collect()` 發行的是 Result／ResultAck。測過丟棄後、結算前、通知中、巢狀通知中及批次結束後替換接收者。
- **Q147：**N＝0、1、2、3、16，各以 80 次混合 Empty／Revoked 拒絕檢查計數、順序、淘汰與反覆繞回。Release 記錄路徑零次 C++ 配置；查詢配置另計。
- **舊測試：**保留 192 組完成順序；兩種建置各完成 4,000 筆並行壓力測試及 300 輪／1,800 個把手的生命週期測試。接收者內 `invariants()` 本版得到 0 項錯誤，前版暫態問題已消失。

**Spec：1 項中嚴重度契約落差，已實測**

Q147「各類計數永遠準確」仍不涵蓋 moved-from 把手。[report_model.hpp:134](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/report_model.hpp:134)

重現：

```cpp
World w(4);
auto env = *w.add_env("A", Contract::Handover);
auto source = *w.start_command(env);
auto destination = std::move(source);
auto result = source.report();
```

World 仍存活，但結果為 `Empty`、`rejected(Empty) == 0`、詳細紀錄為空。這是**既有限制與新契約的落差，非本次新增回歸**；README 第 61 行已有揭露，然而第 30 行與 ADR 的無條件承諾仍過廣。應明確限定統計範圍，或讓移動後來源仍可歸屬診斷帳本。`WorldGone` 直接返回、不計數則由讀碼及舊測試確認。

其餘本輪所測 Q149／Q150 未發現缺陷。

**Standards：0 項硬規範違反；2 項低嚴重度文件問題**

1. **測試覆蓋宣稱過廣，僅讀碼確認。**  
   重現方式：對照 [README:45](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/README.md:45) 與 `main.cpp:381–392`。所稱 9 個方法包含刻意被拒絕的 `set_lost_receiver`，沒有 `deliver`；其中 `settle()` 當時只是空批次，不能當作巢狀通知證據。S13 的單一 ResultAck 配置失敗案例，也不能泛化為所有配置點。本輪補測已擴充，但不能倒推原作者情境已經證明這些事項。

2. **舊版描述仍標成目前限制，讀碼確認，部分已實測反證。**  
   逐行對照 [README:51](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/README.md:51)：
   - 第 51 行仍稱「本版」拒絕所有重入。
   - 第 58 行稱通知時機與未登記政策尚未核定，與 Q149／Q150 不符。
   - 第 59 行稱沒有有界明細，與環形紀錄不符。
   - 第 63 行稱通知時槽位尚未歸還；本版先歸還才通知，本輪接收者內不變式檢查也正常。

**前次測試的修改**

[逐行修改清單](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/review-930c49b/api-changes.diff)：

- `original.cpp`：只在共用環境建立函式加入空操作接收者。
- `probe.cpp`：同樣預先登記接收者；移除已不存在的 `Reentrant`／`reentryRefused` 期待，改驗證操作生效。
- 宣告現在會真正結清交接，因此放在被測操作之後；批次後期待改為已完成宣告。
- 原「未登記仍承擔」情境改用空操作接收者，通知計數隨之調整；未登記拒絕另由新補測驗證。

**限制：**TSan 未能執行；ASan 不執行 allocator 替換與失敗注入。配置掃描涵蓋此 Windows／MSVC 標準函式庫下實際出現的 C++ 配置點，不涵蓋 CRT、系統配置或任意接收者程式。有限交錯與批次不能證明所有競態、遞迴深度或真實宿主行為。