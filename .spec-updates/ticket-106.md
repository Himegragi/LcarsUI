# Ticket #106 spec updates (0.4.0)

Apply to `E:\LcarsUI\docs\specs\v1-semantic-layer-and-uia.md` after the #100–#102 updates.
No ADR is added. The author-machine CONTEXT.md already defines options and error marks/messages.

## 語意層讀取（S2）: snapshot relationship paragraph from #102

Old final sentence:

> SemanticTree::get 只整塊複製一次共同部分，保留完整的來源快照供元件登記表回呼讀取；選項節點的欄位值維持 #101 完成後的原樣，#106 另行處理。

New:

> SemanticTree::get 只在元件節點整塊複製一次共同部分，保留完整的來源快照供元件登記表回呼讀取。選項節點從預設值建立，只描述選項自己；所屬元件的節點回呼不套用到選項。

## S2: add after the snapshot equality paragraph from #102

Old: no option-node contract paragraph.

New:

> **選項節點（#106）。** 單選群組、清單、下拉選單、分頁與導覽清單的選項節點只描述選項自己，沒有的性質為預設值。name 是選項的顯示文字；value、description、error 為空；selected、enabled、invalid、focused、visible、bounds 與 verbs 描述選項本身。enabled 只看選項自己的設定，verbs 仍考慮所屬元件的准入。read_only、executing、expanded 為 false；checked、range、numeric_value、scroll、writing_mode、vertical_digits 為 nullopt；text_align、justify_method、line_spacing、paragraph_spacing、rich 保持預設值。labelled_by、described_by、accelerator_key、access_key 為空，selection_required 為 false。
>
> hidden 與 blocked_by_modal 沿用所屬元件。下拉選單收合時選項仍 hidden = false，以 visible = false、沒有 bounds 表示看不到。focusable 意思是「此刻鍵盤焦點能不能停在這裡」：清單與導覽清單的列必須所屬元件 focusable 且該列自己啟用；單選選項、下拉選項與分頁項目為 false。focused 規則不變，只有清單與導覽清單的候選列可能為 true；分頁與下拉候選焦點留待 #107。
>
> invalid 是出錯標示，error 是這裡自己的錯誤訊息，可能沒有。元件上 invalid 等於 error 非空；選項沒有自己的訊息，所以 error 一律空。分頁與導覽清單的頁項目可因該頁成員出錯而 invalid = true，訊息留在出錯成員上。
>
> option_nodes 以一個狀態一個通用 driver 跑過五種元件，覆蓋一般、元件停用、選項停用、隱藏、強制回應阻擋、元件錯誤、非目前頁成員錯誤、下拉選單展開與右鍵選單開啟。從預設 CommonState 只覆蓋 name、enabled、invalid、focused、focusable、visible、verbs，再以 == 比較共同部分；另檢查選取、關聯標籤、selection_required、hidden、blocked_by_modal 與 bounds。失敗訊息含狀態、元件識別碼與選項值。

## S2: 樹的形狀 table

Old row:

> | 複合元件的子節點 | 邏輯項目：單選選項、下拉選項、清單項目、分頁項目 | `{form.mode, normal}`、`{form.title.pages.v02.list, T005}`、`{form.title.pages.tabs, v02}` |

New row:

> | 複合元件的子節點 | 邏輯項目：單選選項、下拉選項、清單項目、分頁項目、導覽清單項目 | `{form.mode, normal}`、`{form.title.pages.v02.list, T005}`、`{form.title.pages.tabs, v02}`、`{app.nav, items}` |

## UIA provider（S4–S6）: property table

Old row:

> | `Name`、`HelpText` | 節點 `name`、`description`（含提示框文字） |

New row:

> | `Name`、`HelpText` | 節點 `name`、`description`（元件含提示框文字；選項 HelpText 為空） |

Old row:

> | `LabeledBy`、`DescribedBy` | 名稱標籤；說明與錯誤標籤（例 `form.name.help`、`form.name.error`） |

New row:

> | `LabeledBy`、`DescribedBy` | 節點 labelled_by、described_by（元件為名稱、說明與錯誤標籤；選項為空） |

Old row:

> | `IsDataValidForForm` | `!invalid`（例：空白名稱套用後 `form.name` 為 false） |

New row:

> | `IsDataValidForForm` | 節點自己的 `!invalid`（例：空白名稱套用後 `form.name` 為 false；有成員出錯的頁項目也為 false，但沒有重複錯誤訊息） |

Old: no option-specific note after the table.

New:

> 選項的 IsKeyboardFocusable 直接使用節點 focusable，LabeledBy／DescribedBy 與 HelpText 不回頭查所屬元件；只有 #101 已定的 IsEnabled 合併所屬元件。UIA09 另驗證清單選項 HelpText 為空與停用列 IsKeyboardFocusable = false，tests/uia/verify.ps1 要求兩項結果都出現並通過。作者機合併後須跑 verify-idle.ps1 的 uia、axe、demo-uia（兩套外觀），再由協調者執行 /code-review；此票實作者不執行這些閒置桌面流程。
