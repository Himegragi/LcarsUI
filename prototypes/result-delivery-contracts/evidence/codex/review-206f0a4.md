**審查結論：內建引導全數通過，但不能判定四項契約已完整成立。**獨立反例重現 **2 項核心缺陷**，另有 **1 項確認操作缺口**。全程未修改檔案，未使用作者的 `guides-author.log` 作為證據。

已讀取 [ADR Q135–Q138](E:/LcarsUI/docs/adr/0003-interaction-state-and-functional-core.md:209)。透過唯讀解析 Git 物件，確認 HTML、README 與 `206f0a4c1061671f18ec9baf5b73f1a7dcea54a1` 完全一致。

**實際執行與原始結果摘要**

命令／執行方式：

- 嘗試 PowerShell `Get-Content …`：命令工具初始化失敗。
- 嘗試 `wsl.exe -e git rev-parse HEAD`：回報 `Wsl/Service/CreateInstance/E_ACCESSDENIED`。
- 改用 Node REPL 的 `fs.readFile` 唯讀讀檔；以 `zlib.inflateSync` 解析 Git commit/tree/blob 核對版本。
- 模型實際載入與執行的核心程式如下；探測程式全部留在記憶體中：

```js
const vm = await import('node:vm');
const html = await fs.readFile(path, 'utf8');
const code = html.match(
  /<script id="prototype-model">([\s\S]*?)<\/script>/
)[1];
const ctx = vm.createContext({});
new vm.Script(code).runInContext(ctx);
const M = ctx.IntegrationPrototype;
const guides = M.runGuides();
```

另以 `new vm.Script(...)` 編譯兩段 inline script，均通過語法檢查。

| 引導 | 動作 | 預期檢查 | 失敗 | 不變式錯誤 |
|---|---:|---:|---:|---:|
| 1 | 27 | 16 | 0 | 0 |
| 2 | 33 | 10 | 0 | 0 |
| 3 | 56 | 8 | 0 | 0 |
| 4 | 24 | 7 | 0 | 0 |
| 5 | 83 | 21 | 0 | 0 |
| 6 | 33 | 10 | 0 | 0 |
| 7 | 41 | 8 | 0 | 0 |
| 8 | 15 | 6 | 0 | 0 |
| 9 | 18 | 7 | 0 | 0 |
| **合計** | **330** | **93** | **0** | **0** |

自行撰寫、未使用作者 guides 的探測斷言：

```text
independentAssertions: 34
pass: 30
fail: 4
```

4 個失敗分別是下述缺陷 1，以及缺陷 2 的三種輸入。另執行了缺陷擴大重現、多結果確認操作及跨版本對照，未混入此斷言總數。

**發現 1：提前偽造的完成訊息會被採用，並洩漏必要回報預留**

嚴重度：**高，限此模型範圍**。狀態：**已執行重現**。

位置：[一般控制入列](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:174)、[處理與釋放](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:185)、[結果結算](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:203)。

重現程式；每次使用新的 world：

```js
const w = M.newWorld();
const token = {
  world: w.id,
  slot: 0,
  gen: 1,
  cmd: `${w.id}.C${w.seq + 2}`
};
w.dispatch({
  type: 'postControl', kind: 'result',
  body: { token, value: 77 }
});
w.dispatch({ type: 'startCommand', env: 'A' });
w.dispatch({ type: 'processControls', budget: 4 });
```

第一步的命令尚不存在，訊息進入一般控制佇列；第二步恰好建立該識別的命令。處理時訊息變成「有效」，但一般控制路徑沒有歸還該命令的報告預留。

實際結果：

```text
A.value = 77
report: issued=1, returned=0, live=1
errors: ["回報預留 RP.W11.C11 沒有對應義務"]
```

重複十次，每次收取回條後：

```text
commands=0, receipts=0
reportLive=10, issued=10, returned=0
nextStart="不受理：必要回報預留已滿（不承擔新義務）"
invariantErrors=10
```

這直接否定 Q135 預留不洩漏的保證。正常工作全部結束後仍可永久耗盡容量。

v3 對照也接受了這份提前偽造結果，因此**提前訊息被採用是既有缺口；新增報告池的洩漏是 v4 新後果**。重現需要自行組造可預測的未來 token，並非正常提供者的合法完成序列。

**發現 2：未宣告的接收契約可通過驗證**

嚴重度：**中**。狀態：**已執行重現**。

位置：[契約驗證](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:70)、[確認驗證](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:222)、[不變式](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:285)。

```js
w.dispatch({ type: 'closeVisuals', env: 'B' });
w.dispatch({
  type: 'enableExtra', name: 'D', contract: 'toString'
});
```

`CONTRACTS` 是普通 JavaScript 物件；`CONTRACTS[contract]` 會接受繼承屬性。三種輸入均實際通過：

```text
toString    -> "啟用 D", errors=[]
constructor -> "啟用 D", errors=[]
__proto__   -> "啟用 D", errors=[]
```

繼續在 D 啟動命令、完成、收取、關閉並送回確認：

```text
contract="toString"
control="收尾中"
receipt.handedOver=true
delivered=0
errors=[]
```

收取將它當作需確認的契約，但 `acceptAck` 只接受精確的 `explicitAck`，所以沒有正常交付完成途徑。

這違反 Q137「每份契約須明確寫明成立點」，且不變式漏檢。[README](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/README.md:11) 所述拒絕未寫明契約，超出實際保證。

**發現 3：批次交出多份結果，只提供第一份確認 token**

嚴重度：**低，操作完整性缺口**。狀態：**模型已執行；UI 影響僅原始碼審查**。

位置：[collectReceipts](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:218)、[自由操作入口](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:574)。

重現序列：

1. C 啟動兩個命令，各發布結果並處理。
2. `collectReceipts {env:'C', as:'rk'}`。
3. 使用 `@rk` 送出 `resultAck` 並處理。
4. 再次 `collectReceipts {env:'C', as:'rk'}`。

實際結果：

```text
第一次：兩份 handedOver=true，但只回傳第一份 token
第二次："交出即承接 0 張：無；已交出、待逐筆確認 0 張：無"
@rk 仍是第一份 token
第二份回條仍待確認
```

從 snapshot 取得第二份 receipt ID、自行組造 ACK 可以結清，因此不是核心狀態機完全無法前進；但現有收取／確認操作無法自行完成此批次。UI 的確認按鈕也只使用 `@rk`，此部分未開瀏覽器驗證。

**其餘契約探測結果**

| 範圍 | 已執行的獨立探測及結果 |
|---|---|
| Q135 新義務 | 報告池滿時，`startCommand`、`runQueued` 均拒絕；排隊動作保留。改派生效後的遞送在剩餘 0／1／2 格時拒絕，3 格時受理。 |
| Q135 保存與歸還 | 四種回報的 **24 種順序**，均在控制／重送容量滿載、派送停止時保存首次合法回報；恢復後結算，重複回報不造成重複釋放。 |
| Q135 循環 | **200 次**完成、確認及重複完成：`issued=200, returned=200, live=0, ignoredRelease=0`。正常序列未見無預留義務。 |
| Q136 | 關閉、反覆 `disposeClosed` 與空處理輪均保留未交付回條、收尾中狀態及啟用額度；合法交付後只歸還一次。 |
| Q137 | 未交出、外來 world、未知 receipt、缺少 receipt 的確認均未承接；合法確認承接一次，重複確認無額外釋放。 |
| Q138 | 不安全時拒絕放棄；停止派送不放棄；`disposeClosed` 只標記處置而保留責任。明確放棄留有「已放棄」紀錄，重複動作不重複結清。 |
| Q138 交接 | 同一環境的必要故障交接在一般結果被放棄後保持不變。仍執行的命令不被放棄。放棄與 ACK 的兩種先後順序都只結清一次。 |

未發現 Q136 提早結束或 Q138 自動放棄的執行反例。原始碼中沒有 timeout 動作，因此「真正逾時路徑」沒有可執行的覆蓋。

**v3 回歸與 README 審查**

實際重新執行 v3：

```text
253 actions, 72 expectation steps, 0 failures, 0 invariant errors
```

將 v3 原始 actions／expectations 原封不動套入 v4：

```text
253 actions, 76 individual assertions
69 pass, 7 fail, 0 invariant errors
```

此處將同一步的 `expect`、`expectResult` 分開計算，故是 76，不是 72。七項差異均符合 README 的刻意變更：

- 引導 1：步驟 18／23／25，未交付回條延後結束及額度歸還。
- 引導 3：步驟 20 的兩項斷言，有效租用歸還改走預留。
- 引導 5：步驟 48／67，分別是回條保留與回呼結束預留。

另八項自寫回歸檢查全部通過：不安全閒置關閉、安全後單次歸還、外來交接控制、確認後禁止改派、登記表界限、換資源不足以證明隔離、隔離後恢復，以及回條滿載先拒絕命令。

README 正確標示作者自驗、尚未獨立驗證及未做瀏覽器走查；沒有把作者證據冒充獨立證據。不過「拒絕未宣告契約」與預留完整對應義務的概括描述，須受上述反例修正。

**限制與證據界線**

- ACK 只驗證 `{world, receipt}` 與狀態。實際自行拼出同世界、已交出回條的 ACK 也能被接受；模型未建立接收者認證，不能宣稱拒絕所有冒認。
- Q136 保存的是 `terminal/disp` 回條；原始碼處置後刪除含 `value` 的 command。這不證明任意結果 payload／資源完整保留，且此安排已存在於 v3。
- 安全、隔離及擁有者動作都是 fixture 假設；未驗證真實執行緒、宿主、GPU、低記憶體、公平性或瀏覽器互動。
- 探測工具曾遇到 v3 缺少 `reportQ`、REPL 變數及一次語法錯誤；修正後重跑，以上總數不包含未完成呼叫。
- 未寫入報告或證據檔。最終 HTML SHA256：

```text
646203db08346f41e74cbaa7cf42df40b279a8b2b358b22ece9c657d77c8953d
```