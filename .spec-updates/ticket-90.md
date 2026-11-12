# #90 規格更新（協調者套用到未進版控的 `docs/specs`，合併前移除本資料夾）

來源：#90 Agent Brief（2026-11-12 拷問 21 題）。焦點標示改成一個封閉的圓角外框；粗細、圓角、顏色、開關都由程式碼控制；示範程式「外觀」頁加四個控制項。

## 規格 1（`first-component-batch.md`）與 DL03（`v1-draw-list-gpu-rendering.md`）

### C02、C03、C04、C05、C06、C07、C08、C09 的「焦點可辨認」描述

在 C05／C09 等寫到「焦點與選取可辨認」「候選焦點」的列，後面加：

> 焦點標示是一個封閉的圓角外框（`focus_indicator` 角色，兩個圓角矩形：外層 `focus` 色、內層為被框住物的底色），畫在元件或該列邊緣的**裡面**，不佔元件間距、不被捲動區域裁掉、不蓋到內容。框住的範圍：按鈕、核取方塊、下拉選單、文字欄位（單行、多行、富文字，含底線樣式）整個元件；單選群組選中的那一列（都沒選時第一個可用選項）；清單候選列（已選列的 `selection` 底色不變）；分頁候選分頁（選中分頁底下的 `selection` 橫線保留）；導覽清單選中列；滑桿的標題加軌道（不再有標題圓點，把手聚焦時照樣放大）；可捲動或直書的標籤的可視範圍。元件有錯誤（`invalid`）時外框用 `error` 色（沒聚焦時維持原本的細框）。

### V01–V03 的畫面表

凡是畫面表裡寫「聚焦時 `focus_indicator`」「焦點條／底線／頂線／圓點」之處，改為「聚焦時（焦點標示顯示中）一個封閉的圓角外框，3 DIP，顏色 `focus`」。V01-a（名稱欄）：外框框住整個欄位（Web 的外框盒、Material 的整塊底色）；V01-e（地區下拉）：下拉盒整個；V02-a／V02-b（清單）：候選列外框，不再是左側直條；V03-b（滑桿）：標題加軌道外框，無圓點。

### 內距規則（新增，元件通則）

> 每個元件在外框框住的範圍內，**平常**（聚焦與否都一樣）就保留至少「外框粗細」的內距，所以聚焦不改變任何位置；內距不夠的地方（緊密密度的按鈕高度、單選／清單／分頁／導覽列高度、文字欄位上下、滑桿標題與軌道兩端、下拉盒的名稱列、可捲動標籤的左右與直書標籤的欄與捲軸）隨「外框粗細」設定加大。

### DL03（`v1-draw-list-gpu-rendering.md` 第 428 列）

old：聚焦且焦點標示顯示中的元件在自身範圍內至少有一個 `focus_indicator`

new：聚焦且焦點標示顯示中的元件，其 `focus_indicator` 是**一對圓角矩形（外層 `focus` 色、內層底色）**，四邊厚度相同，在自身範圍內，不是單邊的條；`FocusIndicatorMode::never` 時所有元件都沒有 `focus_indicator`（文字游標 `caret` 照畫）。

### 規格 4 的畫面角色表（`v1-draw-list-gpu-rendering.md` 第 105、121、330–341 列附近）

- 第 105 列：`focus_indicator`（#72）只在「焦點標示顯示中」時畫出 → 加「`never` 模式下一律不畫，且優先於 Windows「一律顯示鍵盤焦點」」；刪除「可編輯文字欄位聚焦時一律畫出」的例外後面加「（`never` 除外）」。
- 第 121 列（導覽清單）：「鍵盤焦點 → `focus_indicator`」不變，括號補「（選中列外圍的圓角環，內側是 `selection` 底）」。
- V01-a 等列的「`focus_indicator`」角色欄不變（仍有 `focus_indicator` 標記，現在是成對的兩個圓角矩形）。

## 規格 2（`v1-text-and-presentation-settings.md`）

### 列舉與 `PresentationSettings`（第 83、101 行）

old：

```cpp
enum class FocusIndicatorMode { automatic, always };   // #72：預設 automatic；不提供「一律隱藏」
...
    FocusIndicatorMode focus_indicator = FocusIndicatorMode::automatic;  // #72；預設 automatic，不提供「一律隱藏」
```

new：

```cpp
enum class FocusIndicatorMode { automatic, always, never };   // #72、#90：預設 automatic；never = 所有元件都不畫焦點標示
...
    FocusIndicatorMode focus_indicator = FocusIndicatorMode::automatic;  // #72、#90
    std::optional<int> focus_ring_width;    // #90；DIP，2–6；空 = 外觀預設 3 DIP
    std::optional<int> focus_ring_radius;   // #90；DIP，0–32；空 = 元件本身的圓角（Web 6、Material 16）
```

並加常數 `min_focus_ring_width = 2`、`max_focus_ring_width = 6`、`min_focus_ring_radius = 0`、`max_focus_ring_radius = 32`。

### `set_presentation` 結果表（第 124–128 列附近）

- 「各項皆合法……」列：括號改成「只改 `focus_indicator` 時只重畫、不重排；改 `focus_ring_width`／`focus_ring_radius` 時重排一次（內距隨粗細變）」。
- 「`focus_indicator` 不是兩個列舉值之一（#72）」→ 「不是三個列舉值之一（#72、#90）」。
- 新增兩列：`focus_ring_width` 不在 2–6、`focus_ring_radius` 不在 0–32 → `invalid_value`，整組不變（比照字級 12–28）。

### `focus_indicator_visible()` 說明（第 118 行）

加：`never` 時一律回 `false`（優先於系統設定與按鍵）。提示框「來源取得鍵盤焦點才顯示」用的是另一個內部判斷（鍵盤操作中），不受 `never` 影響。

### 外觀表的「鍵盤焦點」列

（規格 2 外觀表／規格 5 示範應用外觀表 `v1-settings-demo-app.md` 第 243 列）

old：`選取項外圍 2 DIP accent 框（兩個圓角矩形疊出，focus_indicator 角色）` ／ `膠囊外圍 2 DIP accent 環`

new：Web：`選取列外圍 3 DIP focus 色圓角環（半徑 6 DIP，內側 selection 底與左側 3 DIP accent 條）`；Material：`膠囊（半徑＝高度一半）3 DIP focus 色環`。預設粗細 3 DIP、緊密密度也一樣；可由 `focus_ring_width`／`focus_ring_radius` 改。

### NL08（`v1-settings-demo-app.md` 第 530 列）

「焦點標示顯示中且聚焦時有 `focus_indicator`」→ 「……時有一個封閉的圓角環 `focus_indicator`（成對的兩個圓角矩形，四邊同厚）」。

### 調色盤角色 `focus`（規格 2 的調色盤／#78 段）

- 角色名單加 `focus`（顏色）；內建兩套外觀的值等於各自的 `accent`。
- 語意：`Palette::focus` 是 `std::optional<Color>`，空的（預設）就等於當下的 `accent`（`Palette::focus_color()`）。因此「沒被單獨換掉的 `focus` 跟著 `accent`」對所有外觀都成立：內建外觀被 `accent` 覆寫換色時外框跟著變；自訂外觀在建構之後才指定 `accent` 也一樣。清單換 `focus` 時只換外框色；自訂外觀自己設了 `focus` 就釘在那個色。`Ui::effective_palette` 回傳的 `focus` 一律已決定（有值）。
- `set_palette_overrides` 檢查加三條：`focus/surface`、`focus/background`、`focus/panel` 各 ≥ 3:1（WCAG 1.4.11），不過整筆回 `invalid_value`，原清單不變；每一套內建調色盤（兩外觀 × 淺／深）都通過。

## 規格 3（`v1-semantic-layer-and-uia.md` 或規格 3 的對比規則段，#86 outline／#74 hover）

對比規則表加一列：

> `focus`（焦點外框）在 `surface`、`background`、`panel` 上各 ≥ 3:1（與 `outline`／`accent` 同級；WCAG 1.4.11）。`never` 不影響語意層與 UIA：`focused`、`HasKeyboardFocus`、焦點事件照常。

（語意層沒有新增動詞；示範程式的四個新控制項是一般的下拉選單、滑桿、核取方塊，語意層與 UIA 用既有的 `select`／`set_value`／`toggle` 操作。）

## 規格 5（`v1-settings-demo-app.md`）

### 外觀頁控制項表（第 182–184 列附近）

在 `appearance.accent` 後、`appearance.reset` 前加四項（Tab 順序依序）：

| 識別碼 | 元件 | 說明 |
| --- | --- | --- |
| `appearance.focus_mode` | C06「焦點標示」 | 項目 `automatic`「自動」、`always`「一律顯示」、`never`「關閉」；對應 `PresentationSettings::focus_indicator`。 |
| `appearance.focus_width` | C07 滑桿「焦點外框粗細」 | 2–6 DIP，步進 1；顯示 `focus_ring_width`，空時顯示 3。 |
| `appearance.focus_radius_same` | C03 核取方塊「焦點外框圓角跟元件一樣」 | 勾選 = `focus_ring_radius` 留空；取消勾選 = 設為最後一次的值（初始 6）。 |
| `appearance.focus_radius` | C07 滑桿「焦點外框圓角」 | 0–32 DIP；只在上面核取方塊取消勾選時出現在頁面。 |
| `appearance.focus_color` | C06「焦點外框顏色」 | 項目 `default`「預設」（`focus` 不覆寫，跟著強調色）、`green`「綠色」、`orange`「橘色」（覆寫 `focus`，每套外觀淺／深各一個值，沿用強調色綠／橘的顏色，通過 3:1 檢查）。 |

`appearance.focus_mode` 選「關閉」時，`focus_width`、`focus_radius_same`、`focus_radius`、`focus_color` 停用（disabled），數值保留，改回其他選項即恢復。

### `appearance.reset`（第 184 列）

「六項……回到預設」→ 「十項（字型、文字大小、深色、緊湊、左右對齊改靠左、強調色、焦點標示、焦點外框粗細、圓角、顏色）回到預設（焦點標示回 `automatic`、粗細與圓角留空、顏色回 `default`）、一步」。AP05 同步改。

### 文件與設定檔（第 283、319、338 行附近）

- 文件（`SettingsDocument`）加：焦點標示、焦點外框粗細、焦點外框圓角（皆在呈現設定裡）與焦點外框顏色（`SettingsDocument::focus_color`）。
- JSON `presentation` 加四個選用欄位：

```json
"presentation": { ..., "accent": "default", "justify_as_left": false,
  "focus_indicator": "automatic", "focus_ring_width": null, "focus_ring_radius": null, "focus_color": "default" }
```

| 欄位 | 規則 |
| --- | --- |
| `presentation.focus_indicator`（選用，#90） | `"automatic"`、`"always"` 或 `"never"`。缺少視為 `"automatic"`；存在但值不在三者之內，整份文件視為損毀。 |
| `presentation.focus_ring_width`（選用，#90） | 整數 2–6，或 `null`（= 外觀預設）。缺少視為 `null`；超出範圍或型別不對，整份文件視為損毀。 |
| `presentation.focus_ring_radius`（選用，#90） | 整數 0–32，或 `null`（= 元件本身的圓角）。規則同上。 |
| `presentation.focus_color`（選用，#90） | `"default"`、`"green"` 或 `"orange"`。缺少視為 `"default"`；存在但值不在三者之內，整份文件視為損毀（比照 `accent`）。 |

- 復原／重做歷史（第 293 列）：焦點標示、粗細（一次聚焦期間合併成一步）、圓角（同）與顏色各算一步。
- 推翻 #72「設定頁不加開關」：不是每個用 LcarsUI 的程式都有設定頁，所以程式碼一定能控制（`PresentationSettings`、調色盤）；示範程式的設定頁也提供調整。
