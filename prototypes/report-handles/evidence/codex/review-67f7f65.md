**結論：作者情境全部通過，但 `67f7f65` 尚不能視為符合 Q135–Q143。** 獨立探針重現了預留帳破壞、跨 World 結果誤套用、配置失敗後遺失回報，以及兩項 JS 行為退化。

我沒有修改受追蹤檔案，也沒有提交。期間其他作業修改了 `main.cpp`、`.gitignore`，並新增 `run-tsan.sh`；我保留那些修改，最後改用從 **`67f7f65d807120d1734c76772bc55003a0cdfca7` 匯出的快照**重跑作者情境與探針。

**已執行的驗證**

以下命令從工作樹根目錄執行；腳本使用 `vswhere`、`VsDevCmd`、Ninja、clang-cl 22.1.8，ASan 執行時加入指定 runtime PATH：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/build-codex/verify-target.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/build-codex/verify-target.ps1 -Asan
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/build-codex/probes.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/build-codex/probes.ps1 -Asan
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/report-handles/build-codex/negative.ps1
node prototypes/report-handles/build-codex/probes/js-compare.cjs
```

| 實測 | 原始結果摘要 |
|---|---|
| 作者 Release 情境 | `failures=0 pass=41` |
| 作者 ASan 情境 | `failures=0 pass=39`；跳過兩項配置檢查 |
| 複製／自行建構把手 | 兩次編譯均 exit 1：deleted constructor／private constructor |
| 12 執行緒、4,000 個命令 | accepted=2666、dropped=1334、帳目錯誤=0、單次 drain 最大=8 |
| Release 配置計數 | 回報／丟棄區段 `allocations=0`，六種把手生命週期區段亦為 0 |
| World 銷毀競態 | 每配置 300 輪、1,800 個把手，涵蓋六種類型；ASan 無報錯 |
| JS 原有引導 | 10 組、385 步、110 項預期檢查，全部通過 |
| ASan runtime | `llvm-objdump -p` 確認連結 `clang_rt.asan_dynamic-x86_64.dll` |

原始輸出：[作者 Release](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/target-build/scenarios.log>)、[作者 ASan](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex-asan/target-build/scenarios.log>)、[獨立 Release 探針](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/probe.log>)、[獨立 ASan 探針](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex-asan/probe.log>)。

**已執行並重現的問題**

重現程式集中於 [probe.cpp](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/probes/probe.cpp:24>)，以下 F 編號對應輸出。

| 嚴重度 | 重現方式與結果 |
|---|---|
| **高 — F1：重複遞送破壞確認資格與預留帳** | `deliver()` → 回報 callback/work/lease 並 settle → 保留舊 ack，再次 `deliver()`。第二次被受理，`live=5`，但不變量已失敗。舊 ack 仍回傳受理，並清掉新 ack 的追蹤；新一批全部回報後，settle 拋出 `invalid map<K, T> key`。Release、ASan 均重現。 |
| **高 — F2：World 可複製，卻共享同一 Ledger** | `World copy=w` 後，兩個 World 各自啟動命令；副本回報 77，原 World settle。原命令被結算為 77，自己的把手仍未回報；`live=1` 且不變量失敗。這是一般 C++ 複製操作，不需要竄改私有資料。 |
| **高 — F8：結算配置失敗會永久遺失同批回報** | 丟棄 callback、work 把手，讓下一次 `operator new` 拋出 `bad_alloc`，再 settle。`drain()` 已先歸還兩個槽位，建立 callback 遺失通知時拋例外；work 仍 active、未標 lost，也沒有預留。結果 `inv=2 next_settle=0`，無法重新取得遺失的回報。Release 注入實測。 |
| **中 — F7：建立義務配置失敗洩漏預留** | `start_command()` 預留成功後，第一個 map 配置失敗。留下 `live=1 commands=0 receipts=0 inv=1`。Release 注入實測。 |
| **中 — F4：取消結果仍寫回環境** | `report({123,true})` 後 settle，C++ `env.value=123`；相同 JS 操作維持 0。這是已實測的行為退化。 |
| **中 — F5：不安全收尾仍可交付結果** | 設 cleanup unsafe、close、回報、settle、collect。C++ 得到 `receipts=0 outcomes=1`；JS 保留 `receipts=1 outcomes=0`，等待安全處置。C++ 雖尚未 ended，結果責任已提早結清。 |
| **中 — F6：重複回報沒有診斷紀錄** | 同一把手 report 兩次，第二次正確回傳 `Empty`，但 `rejected(Empty)=0`。只有 Revoked 會累計；README「無效回報……並記診斷計數」過度概括。 |
| **介面邊界 — F3：公開 Ledger 可繞過把手** | 從公開 `commands` 取 Ref，直接 `w.ledger().fill(ref,{88})`，結果受理並套用 88，原把手仍 valid。沒有成功偽造 `ReportHandle`，但「回報必須持有把手」並未由整個公開介面保證。README 已承認 fixture 公開欄位，因此這應列為驗證範圍缺口，不當成惡意程式防護問題。 |

F1 的入口位於 [deliver()](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/probes/target/report_model.hpp:161>)；F8 涉及 [settle()](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/probes/target/report_model.hpp:189>) 先 drain、後 apply 的順序。

**把手與丟棄行為的實測結果**

- self-move-assignment 保留把手；move-assign 覆蓋活把手時，舊義務恰好被丟棄一次。
- moved-from、已使用把手均回傳 `Empty`；沒有成功透過正常建構／複製取得第二個有效把手。
- result 丟棄會結算為提供者失效。
- callback、work、lease 丟棄後維持 active、標記 lost，產生三則通知；不安全時拒絕 `resolve_lost()`。
- handoff ack 丟棄不構成確認；result ack 丟棄會恢復可重新交付狀態。
- 已撤權的 handoff ack、result ack 再丟棄，不重複歸還容量。其餘四種沒有對應的正常撤權 API。
- 六種類型均涵蓋 World 銷毀後，以及與 settle／World 銷毀交錯的回報或丟棄。
- `Empty`、`Revoked`、`WorldGone` 三個拒絕原因皆到達。正常拒絕未變更業務義務；但 Revoked 會增加診斷，首次拒絕也會消耗把手，因此不能逐字宣稱「任何狀態完全不變」。

**僅原始碼審查，未用競態偵測器證實**

- **高：錯誤執行緒拒絕路徑本身有 data race。** [owner()](</C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/report-handles/build-codex/probes/target/report_model.hpp:261>) 在拋例外前執行非 atomic 的 `++ownerThreadViolations`。兩個背景執行緒同時違規呼叫便會競爭；S9 只有單一執行緒，無法涵蓋。
- 正常使用下，Ledger 的槽位、世代、live 計數皆受同一 mutex 保護；weak_ptr 成功 lock 後持有 shared_ptr，能保護此次 fill/drop 的 Ledger 生命週期。有限 ASan 結果與此設計一致。
- 同一個 `ReportHandle` 的 report／move／析構不能並行操作：`live_` 與 `l_` 沒有同步。探針遵守單一持有者，沒有用未定義行為冒充反例。
- `reassign()`、`abandon()` 撤銷槽位時沒有移除相應 `partOwner`／`ackOwner` 登記；重複循環會累積過期登記。`commands` 也保留歷史終態，README 僅說明 outcomes 無上限，未完整揭露這些成長。
- `settle()` 限制的是一次取出八筆；`finalize()` 仍掃描狀態集合，不能據此推論固定執行時間或公平性。

**限制與證據界線**

沒有執行 TSan；ASan 通過不代表沒有 data race。零配置計數涵蓋探針替換的 C++ `new/new[]/aligned new`，不是所有 CRT／系統配置的完整攔截，也不是整個 settle 路徑零配置。配置失敗注入僅測了上述兩個明確位置。

Q143 本文沒有明定兩種 ack 丟棄策略；此處驗證的是原型目前的重試行為，不能稱為 ADR 已核定。Q136–Q138 的一般交付、逐筆確認、明確放棄與關閉等待路徑通過，但 F4、F5 表明不能宣稱與 JS 模型全面等價。

所有最終驗證產物均留在指定的 `build-codex/`、`build-codex-asan/`；建置腳本初次曾將 configure log 寫在旁邊，已移回指定目錄並修正腳本。未讀取或採信作者的 `evidence/claude/*.log`。