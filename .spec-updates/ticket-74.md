# 規格更新：#74 所有可點元件加上 hover 狀態

協調者套用到未追蹤的規格後移除本資料夾。以下每項是「檔案 → 位置 → 舊 → 新」。

## 規格 1（`docs/specs/v1-draw-list-gpu-rendering.md`）

### 1. 繪製角色清單（第 103 行）

舊：

> **繪製角色**（規格提案）：`content`、`focus_indicator`、`caret`、`selection`、`composition`、`error_text`、`error_indicator`。

新（加入 `hover`）：

> **繪製角色**（規格提案）：`content`、`focus_indicator`、`caret`、`selection`、`composition`、`error_text`、`error_indicator`、`hover`。`hover` 是指標停留的疊色：一個半透明 FillRect 或圓角（顏色為 `Palette::hover`，alpha 為 `Palette::hover_alpha`），不透明度低於 255 的唯一一類指令（另有遮罩）。

### 2. 元件繪製表「標記角色的部分」欄（C02–C09 各列尾端追加）

| 元件 | 追加於該列 |
| --- | --- |
| C02 按鈕 | 指標停留時內圓角之上多一個圓角疊色 → `hover` |
| C03 文字欄位 | 指標停留時，外框外觀（Web）在內圓角之上、底線外觀（Material）在表面圓角之上多一個圓角疊色 → `hover` |
| C04 核取方塊 | 同 C02（方框與勾不被蓋住：疊色在方框之前） |
| C05 單選群組 | 指標停留的選項列多一個 FillRect 疊色 → `hover`（只有游標下那一項） |
| C06 下拉選單 | 本體：內圓角之上多一個圓角疊色 → `hover`；展開面板：游標下的選項列多一個 FillRect 疊色 → `hover`（面板有游標項時本體沒有） |
| C07 滑桿 | 游標在滑桿任何位置，把手之上多一個圓形疊色 → `hover` |
| C08 清單 | 游標下的列在選取底色之後多一個 FillRect 疊色 → `hover` |
| C09 分頁 | 游標下的頁籤在頁籤底色之後、選中底線之前多一個 FillRect 疊色 → `hover` |
| 導覽清單 | 游標下的項目在選取底色之後多一個圓角疊色 → `hover`（選取中的項目同時有 `selection` 與 `hover`，`hover` 在後） |

停用元件與項目、強制回應阻擋中的元件、觸控／觸控筆訊息、`PresentationSettings::hover == off` 時，任何元件都不產生 `hover` 指令。

### 3. DL02（第 416 行）

舊：

> 以假文字量測對每個元件跑一個狀態迴圈（一般、停用、聚焦、錯誤、選取、組字），清單只出現上表 6 種指令；

新：

> 以假文字量測對每個元件跑一個狀態迴圈（一般、停用、聚焦、錯誤、選取、組字、指標停留），清單只出現上表 6 種指令加 `hover` 疊色（帶 alpha 的 FillRect／圓角）；

### 4. 新增 DL04

| DL04 | 通用驅動走過所有可點元件與其項目 × 兩套外觀 × 淺色／深色 × 標準／緊湊（各做一次下拉展開）：游標停在某元件或某項目時，只有該元件（或展開面板）的繪圖清單有 `hover` 指令，且在該元件或該項目的範圍內、顏色與 alpha 等於該外觀、變體的 `Palette::hover`／`hover_alpha`；停用、模態阻擋、關閉開關時沒有。滑桿游標在軌道遠離把手處時，把手有 `hover`。已選取的導覽項目同時有 `selection` 與 `hover`，`hover` 在後。 | 核心（`hover` CTest）＋原生（`DF-hover-native`，含 CPU／GPU 疊色比對） |

### 5. 區域比對（第 338 行）

在「對每個帶 `focus_indicator`、`caret`、… 角色的指令」的角色清單中加入 `hover`（疊色的 CPU 參考在區域內須有非透明像素，且 GPU 與 CPU 差 ≤ 8）。

## 規格 2（`docs/specs/v1-text-and-presentation-settings.md`）

### 1. `PresentationSettings`（第 92–98 行）

舊：

```cpp
    Density density = Density::standard;
    bool operator==(const PresentationSettings&) const = default;
```

新：

```cpp
    Density density = Density::standard;
    Hover hover = Hover::on;       // 指標停留疊色開關；off 時任何來源都不畫，提示框照常跟隨指標
    bool operator==(const PresentationSettings&) const = default;
```

並在 `Density` 旁新增 `enum class Hover { on, off };`。第 115 行的無效設定表新增一列：`hover` 不是兩個列舉值之一 → `invalid_value`，整組不變。

### 2. 配色表（第 245–259 行）

在說明文字「`Palette` 新增 `scrim`（遮罩色）與 `scrim_alpha`」之後補上：`Palette` 再新增 `hover`（停留疊色）與 `hover_alpha`（0–255 的 alpha）；`hover` 與 `hover_alpha` 都是 `palette_roles()` 的角色，可被 `Ui::set_palette_overrides` 覆寫。表格新增一列（**規格提案初值，待作者試用確認**）：

| 角色 | Web 淺色 | Web 深色 | Material 淺色 | Material 深色 |
| --- | --- | --- | --- | --- |
| hover／alpha | 0,0,0／15（6%） | 255,255,255／20（8%） | 35,30,44／10（4%，＝text） | 232,225,234／20（8%，＝text） |

Material 的 `hover` 取該外觀的 `text` 色（state layer 的 on-surface 色）；Web 淺色疊 6% 黑、深色疊 8% 白。自訂外觀沒有填的 `hover` 預設為 6% 黑（0,0,0／15）。

**偏離票上的提案值（請作者確認）：** 票上 Material 提案 8%（20/255）。Material 淺色的文字欄位把 `muted` 標籤畫在表面上，疊 8% 後 `muted` 對疊色表面只剩 4.22:1，低於 4.5:1（WCAG 1.4.11 的理解：停留不得讓元件與相鄰顏色失去足夠對比）。改為 4%（10/255）後是 4.56:1，其餘外觀與變體在票上數值下都通過（PV01-h 全數檢查）。作者試用若要 8%，須同時調整該外觀淺色的 `muted` 色或欄位標籤色。

### 3. PV01 之後新增 PV01-h

| PV01-h | 對兩套外觀 × 兩變體，把 `hover`／`hover_alpha` 疊在 background、panel、surface、selection 上（整數混色 `(c·a + s·(255−a))/255`），再算 `text` 對疊色結果 | ≥ 4.5:1（停留不得讓文字失去可讀對比） | CI（核心） |

`outline`（#86）對疊色後表面的 3:1 檢查：**受阻 #86**，由合併時加入（見 `.scratch/post-v1-notes/log.md` 的 #74 條目）。

## 規格 3（`docs/specs/v1-semantic-layer-and-uia.md`）

### 1. 動詞列舉（第 117–118 行）

舊：`enum class Verb { invoke, toggle, set_value, select, open, cancel, focus, undo, redo, copy, cut, paste` ＋（已決事項 2 的 `confirm`）

新：在列舉最後加入 `hover`（第 14 個動詞）。

### 2. 動詞語意

`hover`：模擬指標停留在該節點（`ref.item` 有值時是該項目）。准入與 `Status` 與 `Ui::simulate_hover` 完全相同：目標停用或項目停用 → `disabled`；強制回應對話框擋住 → `blocked_by_modal`；頁面隱藏 → `hidden`；項目不存在、或元件沒有該項目 → `not_found`；滑桿等拖曳保持停留中 → `busy`；渲染失敗 → `renderer_failed`；對話框、提示框節點 → `not_supported`。**不列入 `SemanticNode::verbs`**（停留不是節點的操作，也避免每個節點的動詞清單變動），**沒有 UIA 對應**，不改變任何節點屬性，也不發出任何語意事件。

### 3. 查詢

`SemanticTree` 新增：

```cpp
Query<std::optional<NodeRef>> hover() const;   // 目前停留的節點（與項目）；沒有則值為空。與 Ui::hover() 同答案（真實滑鼠或模擬）
Result clear_simulated_hover();                // 結束模擬；真實滑鼠位置不受影響
```

### 3. 新增 UA 案例（UIA 不回報停留）

| UA-hover | 對 `form.title.pages.v03.half` 做 `perform(..., Verb::hover)`，並讓真實滑鼠移過各元件 | `Ui::hover()` 與 `SemanticTree::hover()` 一致；UIA 樹、所有節點屬性與動詞清單與停留前相同；語意事件與 UIA 事件數為 0 |

## 規格 5（`docs/specs/v1-settings-demo-app.md`）

無更動：示範應用沒有新增設定項目。`PresentationSettings::hover` 預設 `on`，示範應用的設定存檔不寫入此欄（作者要在示範應用提供開關時另開票）。
