# #75 規格更新提案（由協調者套用到未追蹤的規格後移除本資料夾）

## 規格 3 `E:\LcarsUI\docs\specs\v1-semantic-layer-and-uia.md`

### 「樹的形狀」上方的 `SemanticTree` 介面區塊（約第 134–140 行）

舊：

```cpp
    Query<std::u16string> text(std::string_view id) const;              // 已確定文字，不含組字
    Result perform(const NodeRef& ref, Verb verb, Argument argument = {});
```

新：

```cpp
    Query<std::u16string> text(std::string_view id) const;              // 已確定文字，不含組字
    Query<bool> animating() const;                                      // #75：是否仍有畫面過場動畫進行中
    Result perform(const NodeRef& ref, Verb verb, Argument argument = {});
```

並在該區塊後新增一列說明（語意層／型別化入口的對照表若有，同列補上）：

| 查詢 | 型別化入口 | 語意入口 | 結果 |
| --- | --- | --- | --- |
| 動畫進行中 | `Ui::animating()` | `ui.semantics().animating()` | `Query<bool>`：進行中為 `true`；非 UI 執行緒 `wrong_thread`；UI 已停止 `failed`；其他情況 `ok`。兩個入口共用同一個內部查詢，所以准入與 `Status` 相同。語意值、UIA 值立即更新、不等動畫，所以此查詢只代表畫面尚未穩定。 |

## 規格 5（呈現設定）`E:\LcarsUI\docs\specs\v1-text-and-presentation-settings.md`

### `PresentationSettings`（約第 92–98 行）

舊：

```cpp
    Density density = Density::standard;
    bool operator==(const PresentationSettings&) const = default;
```

新：

```cpp
    Density density = Density::standard;
    Motion motion = Motion::automatic;  // #75；enum class Motion { automatic, off }
    bool operator==(const PresentationSettings&) const = default;
```

### 「整組替換」段落（約第 122 行）

舊：「「整組替換」指四項一起驗證、一起生效，不會出現字級已變而配色未變的中間畫面。第一版不做切換過渡動畫。」

新：「「整組替換」指各項一起驗證、一起生效，不會出現字級已變而配色未變的中間畫面。切換這些設定本身不做過渡動畫。`motion`（#75）控制元件的過場動畫：`automatic`（預設）跟隨 Windows「在 Windows 中顯示動畫」，`off` 一律不播；不提供強制開啟。切到 `off` 時進行中的動畫立即跳到結尾。未知的 `motion` 值回 `invalid_value`。」

## 規格 1（閒置量測）

於效能量測情境列表（`verify-idle.ps1 -Only perf`，1080 解析度）新增一列：

| 情境 | 內容 | 判定 |
| --- | --- | --- |
| `idle-after-animation`（#75） | 示範應用「一般」頁，聚焦音量滑桿，按一次 →（播 150 ms 把手動畫），等動畫結束後再量測 10 秒 | 與 `idle` 相同：CPU 時間 ≤ 20 ms、無重畫 |
