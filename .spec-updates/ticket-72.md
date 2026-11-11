# 規格更新：票 #72（焦點標示只在鍵盤操作時顯示；滑桿焦點改為標題左側圓點）

協調者套用到未追蹤的規格後刪除本資料夾。以下每項為「檔案／位置：舊 → 新」。

## 規格 1：`v1-draw-list-gpu-rendering.md`

**繪製角色說明（第 103 行附近）** 新增一句：
> `focus_indicator` 只在「焦點標示顯示中」時畫出：不帶 Ctrl／Alt 的按鍵開啟，在視窗內按下指標關閉；Windows「一律顯示鍵盤焦點」或呈現設定「一律顯示」強制為開啟；可編輯的 C03 聚焦時一律畫出。下表與驗收中的「聚焦」皆指「聚焦且焦點標示顯示中」。

**元件指令表（第 108–115 行）**
- C02：`聚焦時外圓角 → focus_indicator` → `聚焦且焦點標示顯示中時外圓角 → focus_indicator`
- C03：`聚焦外框 → focus_indicator` → `聚焦外框（可編輯時一律；唯讀依焦點標示顯示狀態）→ focus_indicator`（其餘項目不變；`selection`、`composition`、`caret` 仍只看是否聚焦）
- C04、C06：`聚焦外框／聚焦框 → focus_indicator` → 前面加「焦點標示顯示中且」
- C05：`聚焦左側條與選中底線 → focus_indicator` → 前面加「焦點標示顯示中且」
- **C07 滑桿：`FillRect、圓角（把手）、GlyphRun` ／ `聚焦底線 → focus_indicator；錯誤行 → error_text`** → `FillRect、圓角（把手）、GlyphRun；顯示中另有圓角（圓點）` ／ `焦點標示顯示中：標題文字左側 accent 實心圓點 → focus_indicator（直徑約字級 40%，水平空間永遠預留，兩套外觀相同；把手僅此時放大）；錯誤行 → error_text`。軌道下方的底線取消。
- C08、C09：`游標列左側條／候選上緣條 → focus_indicator` → 前面加「焦點標示顯示中且」

**情境表（第 322–331 行）** 表後加註：
> V01-a、V01-e、V02-a、V03-b 的設定先以按鍵（或測試接縫／一律顯示）開啟焦點標示顯示狀態，再比對；V01-a 的名稱欄為可編輯，聚焦時一律畫焦點標示。

**DL03（第 417 行）**
- `聚焦元件在自身範圍內至少有一個 focus_indicator` → `聚焦且焦點標示顯示中的元件在自身範圍內至少有一個 focus_indicator（驅動須先以按鍵或「一律顯示」開啟顯示狀態）；顯示狀態關閉時，非可編輯文字元件沒有 focus_indicator`
- 「聚焦且可編輯、視窗有焦點的 C03 恰有一個 `caret`」不變。

**GC03（第 438 行）**
- `不畫焦點框時必須在 focus_indicator 區域失敗` → `不畫焦點框時必須在 focus_indicator 區域失敗（比對情境須在焦點標示顯示中的前提下執行）`

## 規格 2：`v1-settings-demo-app.md`

**導覽清單外觀表「鍵盤焦點」列（第 238 行）**
- 欄名／列名 `鍵盤焦點` → `鍵盤焦點（焦點標示顯示中）`；內容不變。

**NL08（第 521 行）**
- `聚焦時有 focus_indicator` → `焦點標示顯示中且聚焦時有 focus_indicator（驅動先以按鍵或「一律顯示」開啟顯示狀態）`

## 第一批元件規格：`first-component-batch.md`

**C11（第 110 行）**
- `停留或來源取得鍵盤焦點後可顯示` → `停留，或來源取得焦點且焦點標示顯示中後可顯示（滑鼠點擊取得的焦點不顯示；點擊後按任一不帶 Ctrl／Alt 的按鍵即成為顯示中）`

## 規格 3：`v1-text-and-presentation-settings.md`

**第 82–97 行 公開介面**：在 `enum class Density` 後加
```cpp
enum class FocusIndicatorMode { automatic, always };   // 預設 automatic；不提供「一律隱藏」
```
`struct PresentationSettings` 在 `density` 後加：
```cpp
    FocusIndicatorMode focus_indicator = FocusIndicatorMode::automatic;
```
`operator==` 照常涵蓋新欄位。

**第 115 行附近錯誤表**：新增一列 `focus_indicator 不是兩個列舉值之一 → invalid_value，整組不變`。

**新增 `Ui::focus_indicator_visible() const`**（第 106–107 行附近）：回報「焦點標示目前是否顯示中」（最近真實輸入為按鍵、系統「一律顯示鍵盤焦點」或呈現設定為 always）；不含可編輯文字欄位的例外。只改 `focus_indicator` 時 `set_presentation` 立即重畫，但不重新排版也不觸發字型／捲動揭示。

## 規格 4：`v1-settings-demo-app.md`（示範應用）

示範應用設定頁**不**新增焦點標示選項，設定檔格式不變（確認項，無文字修改）。
