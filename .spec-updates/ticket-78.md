# Ticket #78 spec updates (規格 3：`E:\LcarsUI\docs\specs\v1-semantic-layer-and-uia.md`)

The coordinator applies these to the untracked spec and removes this folder.

## 1. 「語意層讀取（S2）」的 C++ 形狀：`class SemanticTree`

Old:

```cpp
    Query<std::u16string> text(std::string_view id) const;              // 已確定文字，不含組字
    Result perform(const NodeRef& ref, Verb verb, Argument argument = {});
```

New (insert one line between `text` and `perform`):

```cpp
    Query<std::u16string> text(std::string_view id) const;              // 已確定文字，不含組字
    Query<Palette> effective_palette() const;                           // 唯讀：目前外觀與主題變體下實際生效的顏色（#78）
    Result perform(const NodeRef& ref, Verb verb, Argument argument = {});
```

## 2. 「語意層讀取（S2）」新增一個段落（放在「文字（#10 Q11）」之前）

New paragraph:

> **生效顏色（#78，唯讀）：** `SemanticTree::effective_palette()` 回傳目前繫結的外觀在目前主題變體下實際繪製用的整組 `Palette`（內建值套用應用以 `Ui::set_palette_overrides` 設定的覆寫後的結果），與 `Ui::effective_palette(appearance, variant)` 的型別化入口結果一致。語意層**沒有**修改顏色的動詞或入口（比照呈現設定，顏色只由應用程式碼透過 `Ui` 設定）。`Status`：不在 UI 執行緒 `wrong_thread`；環境已停止 `failed`；尚未繫結外觀（`Ui::bind_appearance`，桌面宿主自動繫結）`not_found`。它不是節點樹的一部分：不產生節點、不發出 `SemanticEvent`，也不經 UIA 回報。

## 3. 「語意層操作（S3）」動詞表

No row changes. State in the paragraph under the table that no `Verb` changes colors (the palette is read-only through the semantic layer), i.e. append one sentence to the paragraph that follows the verb table:

> 顏色（`Palette`）不是任何節點的屬性，沒有對應動詞；覆寫只能由應用程式碼呼叫 `Ui::set_palette_overrides`（#78）。

# 規格 5（`v1-settings-demo-app.md`，「外觀與文字」表，約第 170–181 行）— #78 示範應用

## 4. 表格新增一列（放在 `appearance.dark`、`appearance.compact` 之後）

| 識別碼 | 元件 | 內容與行為 |
| --- | --- | --- |
| `appearance.accent` | C06「強調色」 | 項目 `default`「預設」、`green`「綠色」、`orange`「橘色」。選擇即呼叫 `Ui::set_palette_overrides`：預設為空清單；綠、橘各為兩套外觀 × 淺色／深色的 `accent`、`on_accent`、`selection`（只含實際連結的外觀），皆通過對比度檢查。寫入文件、標為未儲存、可復原，與 `appearance.dark` 同。 |

## 5. `appearance.reset` 列

Old: `四項回到預設，算一步。`
New: `五項（字型、文字大小、深色、緊湊、強調色）回到預設，算一步。`

## 6. 設定檔格式（規格 5 描述 `settings.json` 之處）

`presentation` 物件新增選用欄位 `"accent"`：`"default"`、`"green"` 或 `"orange"`。缺少此欄位的舊檔視為 `"default"`；欄位存在但值不在三者之內時，整份文件視為損毀（與其他欄位相同）。`version` 維持 1。
