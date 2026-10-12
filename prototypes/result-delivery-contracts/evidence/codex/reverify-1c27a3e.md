**再驗證結論：預留洩漏、非法契約及多回條確認操作均已修正；提前偽造結果仍會被採用，因此原發現 1 整體判定為「部分修復」。另發現新增測試動作的一項低嚴重度例外。**全程未修改檔案，也未使用作者的 `guides-author.log` 作為證據。

已先讀前次報告及指定的 Q135–Q138 契約。

**版本、執行方式與原始結果摘要**

- PowerShell 讀檔命令：工具初始化失敗。
- `wsl.exe -e git rev-parse HEAD`：失敗，`Wsl/Service/CreateInstance/E_ACCESSDENIED`。
- 改用 Node `fs.readFile`、`zlib.inflateSync` 唯讀解析 Git 物件，確認：
  - 分支：`codex/prototype-selfdraw-editing`
  - HEAD：`1c27a3e221ee4120d3e18f5fc5112457ba0963be`
  - parent：`206f0a4c1061671f18ec9baf5b73f1a7dcea54a1`
  - checkout 的 HTML、README 與 HEAD blob **逐位元組一致**。未宣稱整個工作樹皆乾淨。

實際透過 Node REPL 執行，模型及探測程式均留在記憶體：

```js
var vm = await import('node:vm');
var ctx = vm.createContext({});
new vm.Script(
  html.match(/<script id="prototype-model">([\s\S]*?)<\/script>/)[1]
).runInContext(ctx);
var M = ctx.IntegrationPrototype;
var guides = M.runGuides();
```

| 執行項目 | 實際結果 |
|---|---|
| v4 `runGuides()` | 10 組、385 步、110 個有預期檢查的步驟；失敗 0、不變式錯誤 0 |
| 其中 guide 10 | 55 步、17 個預期檢查；全部通過 |
| 兩段 inline script 編譯 | 2／2 通過 |
| 自寫探測斷言 | 556／556 通過；不變式錯誤 0 |
| 另行例外探測 | 重現下述新增 fixture 缺陷，不混入上述通過數 |

**原發現逐項判定**

| 原發現 | 判定 | 本次實際證據 |
|---|---|---|
| 1. 提前偽造結果被採用並洩漏預留 | **部分修復** | 洩漏已修；提前偽造結果仍被採用 |
| 2. 繼承屬性通過契約檢查 | **已修復** | `toString`、`constructor`、`__proto__` 全部拒絕 |
| 3. 多回條只能取得第一份確認 token | **已修復操作缺口** | 舊別名路徑仍只有第一份；新增 `ackHandedOver` 可完成其餘回條 |

原發現 1 使用自行組造的未來 token，未依賴新增的 `forgeNextResult`：

```js
const token = {
  world: w.id,
  slot: 0,
  gen: w.slots[0].gen,
  cmd: `${w.id}.C${w.seq + 2}`
};
w.dispatch({type:'postControl', kind:'result',
            body:{token, value:77}});
w.dispatch({type:'startCommand', env:'A'});
w.dispatch({type:'processControls', budget:4});
w.dispatch({type:'collectReceipts', env:'A'});
```

重複十次：

```text
A.value=77
commands=0, receipts=0
report: issued=10, returned=10, live=0, ignoredRelease=0
下一個 startCommand：成功
invariantErrors=0
```

另實際跨版本重跑單次偽造：

```text
v3       ：value=77
206f0a4  ：value=77，report live=1，出現「沒有對應義務」
1c27a3e  ：value=77，report live=0，errors=[]
```

原發現 2 先關閉 B 騰出啟用額度，再嘗試啟用 D。三個名稱皆得到：

```text
不受理：框架拒絕：接收契約未寫明承接成立點
```

另測 `null`、缺值、數字、物件、陣列及包裝字串，均拒絕；`handover`、`explicitAck` 均成功。

原發現 3 重跑「C 兩個命令完成 → 批次收取 → 用 `@rk` 確認 → 再收取」：

```text
再次收取：待逐筆確認 0 張
實際仍剩 1 張未確認回條
```

接著執行 `ackHandedOver {env:'C'}` 並處理：

```text
逐筆送出 1 則結果確認
receipts=0, delivered=2, receipt.ignoredRelease=0
```

UI 按鈕已改接新動作，這部分是[原始碼審查](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:605)，未做瀏覽器點選驗證。

**修正的交錯與容量探測**

| 探測 | 已執行結果 |
|---|---|
| 四類回報的 24 種排列；控制／重送滿載、停止派送 | 首份合法回報皆保存；恢復後每份只結算、歸還一次 |
| 四類回報 × 四種通道時序，共 16 組 | 一般通道單獨結算、一般先到再入預留、預留先到再入一般、一般已處理後再到訊息，均只歸還一次 |
| 200 輪完成、確認及重複完成 | `issued=returned=200, live=0, ignoredRelease=0` |
| 不同命令／不同交接／改派前後的預留 | 未誤釋放或刪除其他義務的副本 |
| ACK 容量矩陣 | 控制占用 0–4、重送占用 0–8、派送開／關，共 90 組，每組 9 張回條；均可在明確重試後結清一次 |

通道順序有一個重要結果：**處理器優先取預留佇列**。一般通道的偽造值 77 先到、合法預留值 88 後到，但尚未處理時，最終套用 **88**；若一般通道已先結算，後到結果便被忽略。

「一般訊息直接結算時，預留副本仍排隊」在正常派送優先順序下不會先發生。因此另對四類回報執行了**直接呼叫 `apply` 的方法層測試**：自己的副本被移除，另一義務的副本保留，後續 drain 不重複釋放。此部分不是公開 dispatch 路徑覆蓋。

以記憶體 wrapper 稽核 1,137 次 `releaseReport` 呼叫：

```text
釋放時義務仍 live：0
釋放不存在的 reservation：0
刪除其他 key 的 queued copy：0
```

控制與重送均滿、派送停止時，`ackHandedOver` 的實際結果是 **9 則均「未保留」**，不是已承接；9 張回條與收尾中狀態仍保留。恢復、清空通道、再次確認後，`delivered=9`。README 已明確記載這個限制。

**較廣的契約與回歸結果**

- **Q135：**報告池滿時拒絕新命令及排隊動作的啟動，排隊動作保留；交接遞送剩餘 0／1／2 格拒絕，3 格受理。
- **Q136：**不安全收尾保留結果；安全處置、重複處置及空處理輪仍保留未交付回條；交付後才結束，額度只歸還一次。
- **Q137：**未交出、外來 world、未知或缺少 receipt 的 ACK 均不承接；合法及重複 ACK 只結清一次。
- **Q138：**不安全時拒絕放棄；停止派送不放棄；仍執行的命令及同環境必要故障交接不受影響；放棄／ACK 兩種先後順序均只留一筆結清紀錄。
- 前次八項自寫回歸亦重跑通過，包括不安全閒置關閉、單次歸還、外來交接 ACK、確認後禁止改派、登記表界限、資源替換與隔離、恢復及回條滿載拒絕。

v3 本身重新執行：

```text
253 actions, 72 expectation steps
0 failures, 0 invariant errors
```

將 v3 原 actions／expectations 套入本版：

```text
253 actions, 76 individual assertions
69 pass, 7 fail, 0 invariant errors
```

七項差異與前次完全相同：g1 步驟 18／23／25、g3 步驟 20 的兩項斷言、g5 步驟 48／67，均對應 README 明列的回條延後結束及必要回報預留變更，沒有新增差異。

**新增發現：滿結果槽位時，新增偽造探測動作會拋例外**

嚴重度：**低，限測試 fixture**。狀態：**已執行重現**。

位置：[integrated-v4.html:231](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/integrated-v4.html:231)。

```js
const w = M.newWorld();
for (let i=0; i<6; i++)
  w.dispatch({type:'startCommand', env:'A'});
w.dispatch({type:'forgeNextResult', value:77});
```

實際結果：

```text
TypeError: Cannot read properties of undefined (reading 'gen')
```

`findIndex` 回傳 `-1`，隨後讀取 `this.slots[-1].gen`。例外後 `resultLive=6, reportLive=6, errors=[]`；沒有觀察到容量損壞。這是新增探測動作的容量邊界缺陷，並非本次發現的新 Q135–Q138 核心結算缺陷。

**README 與限制**

[README 的修正及限制聲明](C:/Users/user/.codex/worktrees/lcars-build-prototype/LcarsUI/prototypes/result-delivery-contracts/README.md:33)與本次結果相符，未發現新的驗證誇大。作者數字可独立重現，也清楚承認提前偽造、ACK 滿載、接收者認證、payload 保存及逾時覆蓋限制。「再驗證尚未做」是此次審查前的狀態；未修改文件。

本次仍不證明真實宿主、執行緒、資源安全、公平性或瀏覽器互動；未做完整狀態空間窮舉。安全旗標是 fixture 宣告。ACK 認證與提前偽造缺口仍存在。

最終再次核對 HEAD、HTML、README 均未變。HTML SHA256：

```text
5f56d224c7704cbed7fb8e91ecf31dc4f4438978fff3e79a06bac75c1ec80887
```