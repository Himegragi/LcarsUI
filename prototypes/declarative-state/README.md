# C++ 宣告式 API 與持久狀態原型

可丟棄原型，回答：重新宣告欄位時，能否保留焦點、選取與草稿，並避免資料刷新反覆觸發 change？
Windows 原生控制項只是觀察狀態的測試載體；本原型不驗證 LcarsUI 自繪文字、樣式或 GPU。

## 執行

在 worktree 根目錄執行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/declarative-state/run.ps1 -Interactive
```

不加 `-Interactive` 會建置、執行情境與截取本程式視窗後結束。沿用已驗證的 clang-cl／CMake，無 package manager、無網路下載。
互動模式先執行相同情境，再保留視窗；右側顯示模型、草稿、dirty/conflict、instance、選取與事件計數。
資料僅存於記憶體；「儲存名稱」只更新原型模型，不寫入磁碟。

## API 提案

```cpp
probe::Value name{L"Alice", 0};
probe::Value email{L"alice@example.com", 0};
probe::Form form{
    probe::TextField("profile.name", L"名稱", name),
    probe::TextField("profile.email", L"電子郵件", email)
};
runtime.apply(form);
```

應用模型持有已確認值與 revision；宣告是模型的快照，沒有 HWND、OpenGL handle 或 event callback。
Runtime 持有暫存草稿、選取與識別生命週期；應用在稍後事件派送階段接收變更，沒有在宣告過程執行應用回呼。
未來描述檔前端可產生同樣的 Form，但本輪未實作 parser、資料綁定格式或一般元件樹。

## 實際驗證

2026-10-08，本機 Windows 11、clang-cl22.1.8、CMake4.3.1-msvc1、Debug x64。

| 情境 | 結果 |
| --- | --- |
| C++26／獨立 state_model library | pack indexing 與 consumer 通過；state_model 無 Win32／GPU 依賴 |
| 輸入後插入其他欄位及反轉順序 | 同 key 的 instance 與 HWND 保持不變 |
| 原生焦點與文字選取 | 名稱欄位保持焦點，EM_GETSEL 仍為 2:7 |
| 鍵盤導覽 | 原生 Tab 順序隨宣告欄位順序更新 |
| 未儲存文字 | 重排後仍為 Alice edited |
| dirty 欄位遇到外部更新 | draft 保留，conflict=true，新的 model 值仍可見 |
| clean 欄位遇到外部更新 | 直接同步新 model 值 |
| 事件回寫／刷新 | 程式刷新不新增 change，實際輸入只有一個事件 |
| 重複 key | 在修改 runtime 前拒絕，原 order 和 draft 不變 |
| 刪除 focused 欄位 | 清除邏輯焦點；其草稿生命週期結束 |
| 同 key 再建立 | 新 instance，以最新模型初始化 |
| 舊生命週期的排隊事件 | 被丟棄，不能套用到新 instance |

`evidence/walkthrough.log` 保留情境輸出；`evidence/state-walkthrough.txt` 為最後狀態。
`evidence/state-window.bmp` 是此程式的 PrintWindow 畫面。

## 手動體驗

1. 選取名稱的一段文字並輸入新值，按「重排欄位」或「插入／移除備註」；檢查名稱仍是同一個 instance 且保持草稿。
2. 名稱有 dirty 草稿時按「外部更新名稱」；右側應同時顯示新的 model 與舊 draft，以及 conflict=true。
3. 「儲存名稱」代表明確以草稿覆寫模型；「復原名稱」代表採最新模型。這是待使用者檢視的衝突操作提案，尚未成為框架預設。
4. 按「重複 ID」；原畫面不應被破壞。移除再重建名稱則會產生新 instance，不保留已刪除草稿。

## 職責與限制

- state_model：宣告套用、穩定 key、編輯草稿與延後事件；原型以可讀資料結構暴露快照，非正式封裝設計。
- native_probe：Win32 建立／更新／刪除控制項，映射 native focus/selection，抑制程式寫入導致的原生通知。
- application：提供模型、明確儲存／復原與處理 change 通知。
- 所有存取與派送在同一 UI 執行緒；本輪没有驗證跨執行緒安全。EN_CHANGE 只排隊，以 PostMessage 在 callback 返回後派送。
- 只驗證平面 TextField 清單；尚未驗證巢狀 key scope、元件型別變更、一般 tree diff、undo、捲動位置、IME 組字、Unicode grapheme、UIA 或兩套外觀。
- 原生 Edit 的輸入能力不等於未來自繪文字引擎已支援；後續文字議題需獨立驗證。
- stable key 與 lifetime instance 都有必要；不能用陣列索引或僅相同 key 判定舊事件仍有效。

## 待檢視的決策

建議採「應用模型＋宣告快照＋持久 runtime 草稿」；有未保存草稿時保留並標示外部衝突，刪除則結束生命週期。
這些是原型提案，尤其草稿移除、衝突處置及 API 名称仍待使用者檢視，不視為正式規格。
