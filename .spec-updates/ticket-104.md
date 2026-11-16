# Ticket #104 exact specification updates

Apply these edits to the author-machine specs; no files under `docs/` are changed by this branch.

## `docs/specs/first-component-batch.md`, common capabilities table, G07

Old:

> 移除受理後拒絕新操作，結構在安全點提交；來源失效後取消其提示計時與失效回呼。

New:

> 標籤、文字欄位、按鈕、核取方塊、單選群組、滑桿、進度條、下拉選單與清單的型別化 Handle 提供 `remove()`，只由應用程式碼發起，沒有對應語意動詞。受理後所有操作回 `not_found`，讀取來源通知不再影響畫面與語意；在安全點解除訂閱並拆除，直排、橫排、格線均收合空間與間距。舊 Handle 與已排入的來源回呼不會因同識別碼新元件建立而復活。來源失效後取消其提示計時與失效回呼。對話框成員、初始／備用焦點目標、對話框名稱標籤拒絕移除（`invalid_value`）。移除聚焦欄位不呼叫 `on_blur`，宿主焦點移動通知照發；未選定的組字丟棄，不回寫。一般焦點移動的 E01 預設不變。

## `docs/specs/first-component-batch.md`, common capabilities table, G11

Old:

> 框架不做交易回滾。

New:

> 框架不做交易回滾。移除欄位時退出確認群組並丟棄其草稿，之後只提交其餘成員的變更紀錄；最後成員移除時群組解散，按鈕回到未綁動作的普通按鈕，可重新 `bind` 或接新群組，框架不改變其可用狀態。

## `docs/specs/first-component-batch.md`, C01 capability text

Old:

> 標題、說明、欄位名稱、錯誤文字、顯示換行與欄位關聯

New:

> 標題、說明、欄位名稱、錯誤文字、顯示換行與欄位關聯；名稱標籤移除時欄位名稱退回 `ComponentConfig::label`，發出一次名稱 `property_changed`。目標移除時標籤留在畫面上，關聯以元件識別碼為準；同識別碼的新元件自動重新取得標籤。

## `docs/specs/v1-semantic-layer-and-uia.md`, SEM01 acceptance row

Old:

> 提交前重建回 `invalid_value`，提交後成功且舊 Handle 回 `not_found`。

New:

> 提交前重建回 `invalid_value`，提交後成功且舊 Handle 回 `not_found`。此規則適用於所有九種可移除元件；移除的結構通知沿用語意樹比對。名稱標籤移除後，目標退回備用名稱並只發出一次名稱 `property_changed`；被移除元件的來源通知在受理後即被忽略。

## Verification follow-up

Coordinator runs the idle desktop clipboard, IME and DPI checks for the behavioral row/grid layout change and removal
composition policy. No demo controls or version changes; version remains 0.4.0. Tabs, navigation lists and overlays
remain outside this ticket's removal scope.
