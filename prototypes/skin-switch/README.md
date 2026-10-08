# 外觀切換原型（用完即丟）

**這不是正式框架實作，也不代表決定要做執行中換膚。** 目的只是回答：邏輯欄位與編輯狀態保持不變，只重建視覺部分時，哪些狀態留下來、成本落在哪裡。

## 內容

- `skin_switch.cpp`：單一檔案。重用 text-pipeline 的 `TextEngine`（HarfBuzz 排版＋FreeType）、`editing::Model` 與 GL 啟動程式。
- 邏輯狀態 `Page`：欄位 id、標籤、`editing::Model`（文字、選取、undo/redo、model 內的 preedit fixture）、焦點欄位、revision。切換時完全不碰。
- 視覺樹 `Node`：每次結構切換整棵丟棄、重建、重新排版。
  - A：Container → Label（在上）→ Outline（外框）→ Edit。每欄4個節點。
  - B：Container → Icon → Label（在旁邊）→ Edit → Underline（底線）→ Adornment（×）。每欄6個節點。
    `×` 只是裝飾，**沒有**點擊功能，不是清除按鈕。
- 只換顏色的對照組：同一棵樹、不重建、不排版，只用另一組 token 重新點陣化。
- **沒有** layout 快取、**沒有** glyph atlas：結構切換會把所有已建立的欄位重新排版；每個畫面都在 CPU 上把可見列重新點陣化。
- 固定 viewport（視窗客戶區），只點陣化可見列；所有已建立欄位都會排版。這不是資料虛擬化。

## 執行

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/skin-switch/run.ps1 -BuildOnly
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/skin-switch/run.ps1 -Benchmark
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/skin-switch/run.ps1 -Interactive
```

建置用 text-pipeline 的 build 目錄（可選的 CMake 目標 `LCARS_SKIN_SWITCH_PROTOTYPE`），重用已解壓的 HarfBuzz／FreeType，只產生 `skin_switch.exe`。輸出寫到 `prototypes/skin-switch/evidence/`。

## 互動操作（限制很多）

F5 結構切換、F6 只換顏色、Tab 換焦點欄位、滑鼠點擊編輯區、左右鍵（Shift 選取）、Backspace、BMP 字元輸入、Ctrl+Z／Ctrl+Y、Esc 關閉。
標題列顯示目前外觀、token 組、上一次切換時間與「state preserved／CHANGED」。
**沒有** IME、補充平面字元、Delete、Home/End、拖曳選取、UIA；這不是 text-pipeline 完整編輯器的移植。互動版每次編輯也會整棵重建（只是方便）。
互動版在 WM_PAINT 時重畫、在 WM_SIZE 時重新排列節點；benchmark 時不經過這兩條路徑。選取與游標會被裁在 Edit 範圍內，沒有捲動。

## Benchmark 工作量

- 欄位數 24／128／512。每次：初始化畫面（不計），第一次 A→B（單獨列出），A→B 與 B→A 各30次，一次回到 A 的正規化切換（寫入 CSV，**不列入**統計），第一次只換顏色（單獨列出），再60次只換顏色。
- fixture 在計時前先斷言：每欄的標籤、文字與選取都是預期值。每3欄有一筆已知的 redo 紀錄，第1欄是 model 內的組字 fixture。
- 每次切換後，在計時外比對：欄位 id、model 物件位址（身分）、文字、選取、preedit、preedit 游標、組字旗標、焦點、revision；並在 model 複本上把 undo 全部走完、再把 redo 全部走完，逐步比對。任何不同就讓 benchmark 失敗。原頁面不被修改。
- 時間是 CPU wall time（steady_clock），分成：銷毀舊節點＋建立並擺放新節點、HarfBuzz 排版、CPU 點陣化、上傳貼圖＋送出繪製、SwapBuffers 呼叫。計時路徑中沒有 glFinish 或讀回；**不是** GPU 或實際顯示延遲。沒有設定也沒有量測 swap interval。
- 計時之外另外截圖：`evidence/skin-A-v2.bmp`、`evidence/skin-B-v2.bmp`（24欄）。

第一版結果因 fixture 字串錯誤（未定義行為）而**無效**，見 `evidence/INVALID-FIXTURE-v1.txt`。

## 實測 v2（2026-10-09，RTX 3060，客戶區 938×584，144 DPI，`evidence/skin-switch-summary-v2.log`、`skin-switch-samples-v2.csv`）

| 欄位 | 路徑 | 重建節點 | 排版字形 | 銷毀＋建立＋擺放 p50 | HarfBuzz p50/p95 | 點陣化 p50 |
| --- | --- | --- | --- | --- | --- | --- |
| 24 | 結構 A→B | 96→144 | 555 | 0.03 ms | 0.22/0.25 ms | 2.3 ms |
| 128 | 結構 A→B | 512→768 | 3107 | 0.19 ms | 0.90/1.11 ms | 2.2 ms |
| 512 | 結構 A→B | 2048→3072 | 13091 | 0.67 ms | 3.4/3.8 ms | 2.2 ms |
| 512 | 只換顏色 | 0 | 0 | 0 | 0 | 1.8 ms |

- 結構切換的成本隨已建立欄位數增加，主要落在 HarfBuzz 排版；點陣化只跟可見列有關，大致固定。
- 總時間大多約16 ms，看起來受畫面節奏（pacing）限制；本次這段等待出現在「上傳＋送出」階段（約9–15 ms），SwapBuffers 本身很短。這只是觀察，沒有量測 swap interval，**不能**當作 vsync 已驗證。因此總時間與上傳階段都不能直接比較兩條路徑。
- 只報告元素數量與 vector 容量（例如 512欄：節點2048、字形12579），不推算記憶體用量。

## 回答了什麼、沒回答什麼

- 有回答：在這個寫法下，換結構時邏輯狀態可以完全不動（文字、選取、preedit、焦點、undo/redo 都保留）；重建本身很便宜，排版才是隨欄位數成長的部分；只換顏色可以完全避開排版。
- 沒回答：有快取或 atlas 時的成本、GPU 時間、實際顯示延遲、IME／UIA 在切換時的行為、動畫、捲動中的切換、真正的 skin 系統設計、Windows 10。這些數據不是正式效能結論，也沒有通過門檻。
