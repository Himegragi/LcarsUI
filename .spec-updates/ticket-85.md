# 規格更新提案：#85 右鍵選單元件

協調者把下列文字套到未納入版本控制的規格檔，然後移除 `.spec-updates/` 資料夾。

## 規格 3：`docs/specs/v1-semantic-layer-and-uia.md`

### 1. D3 列（表格「D3」）

舊：`語意層操作與通知：13 個框架動詞（含 `confirm`，見已決事項 2）、…`

新：`語意層操作與通知：14 個框架動詞（含 `confirm`，見已決事項 2；#85 加上 `context_menu`）、…`

### 2. 「C++ 形狀」的 `Role`、`Verb`（約第 114–118 行）

舊：

```cpp
enum class Role { text, button, label, checkbox, radio_group, radio_option, slider,
                  dropdown, dropdown_option, list, list_item, tabs, tab_item,
                  progress, dialog, tooltip };
```

新（新增三個角色）：

```cpp
enum class Role { text, button, label, checkbox, radio_group, radio_option, slider,
                  dropdown, dropdown_option, list, list_item, tabs, tab_item,
                  progress, dialog, tooltip, menu, menu_item, menu_separator };
```

`Verb` 新增 `context_menu`（排在 `confirm` 之後）。`Argument` 新增 `MenuPosition { int x, y; }`（視窗客戶區實體像素）。
`SemanticNode` 新增 `std::u16string access_key;`（快速鍵字母，例：`U`）；`accelerator_key` 對選單項目是「快捷鍵提示文字」（例：`Ctrl+X`，只顯示）。
`SemanticEventKind` 新增 `menu_opened`、`menu_closed`。

### 3. 「動詞對照表」新增列與修改列

| 元件類型 | 節點 | 動詞與參數 | 對應的既有 Handle 操作 |
| --- | --- | --- | --- |
| C03 文字欄位（舊列，加一個動詞） | `form.name` | 舊動詞，另加 `context_menu`（參數：無，或 `MenuPosition`） | 舊，另加 `Ui::open_context_menu` |
| C08 清單（舊列，加一個動詞） | `{…v02.list, T006}` | 舊動詞，項目上另加 `context_menu`（先選取該列，再開選單）；元件上 `context_menu` 可帶 `OptionValue` 指名那一列 | 舊，另加 `Ui::open_context_menu` |
| **新列** 掛了右鍵選單的任何元件 | 該元件節點 | `context_menu`；沒有選單（沒有掛、模式為關閉、文字欄位以外的元件沒掛）時不列此動詞，開啟回 `not_supported` | `Ui::open_context_menu` |
| **新列** 選單 | `{<元件識別碼>/menu}` | `cancel`（關閉） | `Ui::close_context_menu` |
| **新列** 選單項目 | `{<元件識別碼>/menu, <項目識別碼>}` | `invoke`（可用者）、`focus`（把亮顯移到該項） | `Ui::invoke_menu_item` |
| **新列** 分隔線 | `{<元件識別碼>/menu, separator:<序號>}` | 無 | — |

說明：選單開著時，「選單」節點是該元件的子節點，「選單項目」與「分隔線」是選單的子節點；關閉後這些節點消失。選單開著時鍵盤焦點屬於亮顯的項目：元件節點 `focused = false`，亮顯項目 `focused = true`（核心焦點仍在元件，關閉後元件的 `focused` 恢復並發出焦點事件）。
開啟的准入檢查與型別化入口相同：停用回 `disabled`；強制回應阻擋的背景元件回 `blocked_by_modal`；隱藏回 `hidden`；渲染器失效回 `renderer_failed`；沒有選單回 `not_supported`；組字中回 `busy`。

### 4. 「每個節點的共同屬性」表

新增一列：

| `AccessKey` | 選單項目的 `access_key`（例：`U`）；其他節點為空字串 |
| --- | --- |

並把 `AcceleratorKey` 列的來源擴為「按鈕的快捷鍵；選單項目的快捷鍵提示文字」。

### 5. 「控制類型與 pattern 表」新增三列

| 元件 | ControlType | Pattern | 驗收控制項 | 要點 |
| --- | --- | --- | --- | --- |
| 右鍵選單 | Menu | — | `{items.list/menu}` | 開啟時是該元件的子節點；`Name` 為「右鍵選單」 |
| 選單項目 | MenuItem | Invoke | `{items.list/menu, items.edit}` | `Name` 含快速鍵字母，例「編輯(E)」；`AccessKey = E`；`AcceleratorKey` 為快捷鍵提示（`Enter`）；不可用項 `IsEnabled = false`，`Invoke()` 回 `UIA_E_ELEMENTNOTENABLED` |
| 選單分隔線 | Separator | — | `{items.search/menu, separator:2}` | 不可聚焦 |

### 6. 「事件對照」新增列

| 語意層事件 | UIA 事件 |
| --- | --- |
| `menu_opened`、`menu_closed` | `UIA_MenuOpenedEventId`（發在選單節點）、`UIA_MenuClosedEventId`（選單節點已不存在，發在原本的元件）；亮顯移到項目時發 `UIA_AutomationFocusChangedEventId`；關閉後元件的焦點事件照常發出 |

## 規格 4：`docs/specs/v1-settings-demo-app.md`

### `items.list` 列（約第 150 行）

舊：`依識別碼排序；項目值 `T001`…。焦點在清單時 Enter＝「編輯…」、Delete＝「刪除…」（範圍限定快捷鍵）。`

新：`依識別碼排序；項目值 `T001`…。焦點在清單時 Enter＝「編輯…」、Delete＝「刪除…」（範圍限定快捷鍵）。每一列掛右鍵選單「編輯(E)」（提示 `Enter`）、「刪除(D)」（提示 `Del`），執行的是 `items.edit`、`items.delete` 按鈕自己的動作；右鍵先選取被點的列；沒有選取時兩項停用，與按鈕一致。所有文字欄位自動有內建右鍵選單（復原／重做／剪下／複製／貼上／刪除／全選）。`

### 使用者故事 5 之後新增一則

`N. 作為操作使用者，我在項目清單某一列按右鍵，該列先被選取，選單的「刪除(D)」問我要不要刪的就是那一列；在文字欄位按右鍵可以剪下、複製、貼上。`
