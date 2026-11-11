# Ticket #86 spec updates

The coordinator applies these to the untracked specs and removes this folder. 規格 1 (`v1-draw-list-gpu-rendering.md`) has no per-component color table (DL02/DL03 only name command kinds and roles), so nothing changes there; the new paint rules are documented in the 規格 4 table and a new row PV10 below.

## 規格 4：`E:\LcarsUI\docs\specs\v1-text-and-presentation-settings.md`

### 1. 「深色／淺色（A5）」的配色表：在 `border` 列之後新增 `outline` 列

New row (after the `border` row):

| 角色 | Web 淺色 | Web 深色 | Material 淺色 | Material 深色 |
| --- | --- | --- | --- | --- |
| outline | 118,128,145 | 128,138,156 | 121,116,126 | 147,143,153 |

### 2. 同節核算段落（「撰寫時以 WCAG 相對亮度公式核算……」）最後一句

Old:

> border 對 surface 約 1.4–1.9:1，與淺色相同，邊框不得是辨識狀態的唯一依據（第一批 G04）。

New:

> `border` 是裝飾線（分隔線、按鈕外框、提示框的邊）用的淡色，對 surface 約 1.4–1.9:1，不檢查對比；辨認元件所需的邊界（文字欄位外框／底線、下拉選單外框、核取方塊方框、單選鈕圓圈、滑桿軌道、捲軸滑塊、沒有值文字的進度條底軌）改用 `outline`，對 surface、background、panel ≥ 3:1（WCAG 2.2 SC 1.4.11）：Web 淺色 3.99／3.72／3.99、Web 深色 4.52／5.34／4.90、Material 淺色 3.69／4.18／4.44、Material 深色 4.57／5.87／5.39。停用的元件改畫 `border`（淡色），停用元件不受 1.4.11 要求。邊框仍不得是辨識狀態的唯一依據（第一批 G04）。

### 3. PV01 驗收列

Old:

> | PV01 | 對兩套外觀 × 兩變體的 `Palette` 計算 WCAG 對比度 | text、muted、error 對 background／panel／surface ≥ 4.5:1；text 對 selection ≥ 4.5:1；on_accent 對 accent ≥ 4.5:1。 | CI（核心） |

New:

> | PV01 | 對兩套外觀 × 兩變體的 `Palette` 計算 WCAG 對比度 | text、muted、error 對 background／panel／surface ≥ 4.5:1；text 對 selection ≥ 4.5:1；on_accent 對 accent ≥ 4.5:1；outline 對 surface、background、panel ≥ 3:1。 | CI（核心） |

### 4. 新增驗收列 PV10（放在 PV01 附近或表末）

New row:

> | PV10 | 一個通用驅動走過文字欄位、下拉、核取方塊、單選鈕、滑桿、進度條（有／無值文字）、按鈕、導覽清單 × 兩套外觀 × 淺色／深色，比對繪製顏色（`tests/outline.cpp`，ctest `outline`） | 可用狀態：辨認用的邊界畫 `outline`，裝飾線（按鈕外框、導覽分隔線、有值文字的進度條底軌）畫 `border`；停用：邊界畫 `border`、不畫 `outline`；聚焦（`accent`）、錯誤（`error`）邊界顏色不變；應用以 #78 覆寫 `outline` 後邊界跟著變。 | CI（核心） |

## 規格 5：`E:\LcarsUI\docs\specs\v1-settings-demo-app.md`

No change needed: the side navigation divider is a decorative line and stays `border` (line 235), disabled items stay `muted` text.
