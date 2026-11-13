# Spec updates for #96 (public names follow the glossary; `add_x` sets the component type)

The specs under `docs/specs/` are untracked; apply these by hand, then delete this folder. Only text that states the
current interface changes. The "現況" (state at a past commit) tables quote the code as it was then and stay as they are.
Every old name is gone without an alias (#96 Q6).

## docs/specs/v1-draw-list-gpu-rendering.md

Section with the `desktop.hpp` interface block (around line 254):

- `struct DesktopOptions {` → `struct DesktopConfig {`
- `struct DesktopCreation { Status status; ...` → `struct CreateResult { Status status; ...`
- `static DesktopCreation create(Ui&, const Appearance&, std::function<layout::Node()> page,` →
  `static CreateResult create(Ui&, const Appearance&, std::function<layout::Node()> page,`
- `ScrollState&, DesktopOptions = {});` → `ScrollState&, DesktopConfig = {});`
- `RunResult run_desktop(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&, DesktopOptions = {});` →
  `RunResult run_desktop(Ui&, const Appearance&, std::function<layout::Node()>, ScrollState&, DesktopConfig = {});`

## docs/specs/v1-packaging-and-delivery.md

Header table, row `include/lcars/diagnostics.hpp` (around line 131):
`` `desktop.hpp` 的 `DesktopOptions::on_diagnostic` 用到 `` → `` `desktop.hpp` 的 `DesktopConfig::on_diagnostic` 用到 ``

## docs/specs/v1-settings-demo-app.md

- Cross-spec table, row 規格 1 (around line 42): `` `DesktopOptions::on_renderer_failed` `` → `` `DesktopConfig::on_renderer_failed` ``
- Table row (around line 101): `` 規格 1 的 `DesktopOptions`／`Desktop` `` → `` 規格 1 的 `DesktopConfig`／`Desktop` ``
- 應用外框 (around line 107): `（沿用 `DesktopOptions`）` → `（沿用 `DesktopConfig`）`
- Navigation list interface block (around line 206):
  - `enum class Kind { /* 規格 3 S1 後的值 */, navigation_list };` → `enum class ComponentKind { /* 規格 3 S1 後的值 */, navigation_list };`
  - `Query<Semantics> describe() const;` → `Query<ComponentSnapshot> describe() const;`
  - `Query<NavigationListHandle> add_navigation_list(Control control, std::vector<NavigationPage> pages, OptionValue initial);` →
    `Query<NavigationListHandle> add_navigation_list(ComponentConfig config, std::vector<NavigationPage> pages, OptionValue initial);`
- Close request interface block (around line 394): `struct DesktopOptions {` → `struct DesktopConfig {`
- 交付 table, row S1 (around line 509): `` `Kind::navigation_list` `` → `` `ComponentKind::navigation_list` ``
- Item dialog table, row `items.dialog.note` (around line 164): `` `Control::rich` `` → `` `ComponentConfig::rich` ``, and
  (after `.spec-updates/ticket-95.md` item 2 is applied) `` `connect_mixed_confirmation` `` → `` `connect_confirmation` ``
  (#96 Q5).

## docs/specs/v1-semantic-layer-and-uia.md

- 識別碼 contract table, row 指定方式 (around line 96): `` 的 `Control::id` 指定 `` → `` 的 `ComponentConfig::id` 指定 ``
- `SemanticNode` interface block (around line 136): `struct SemanticNode {` → `struct NodeSnapshot {`; inside it
  `// 以 Control::rich 建立的文字欄位（#81）` → `// 以 ComponentConfig::rich 建立的文字欄位（#81）`
- `SemanticTree` block (around line 166): `Query<SemanticNode> get(const NodeRef& ref) const;` → `Query<NodeSnapshot> get(const NodeRef& ref) const;`
- 名稱規則 (around line 217): `` 名稱取 `TextRole::name` 標籤，沒有就取 `Control::label` `` →
  `` 名稱取 `LabelKind::name` 標籤，沒有就取 `ComponentConfig::label` ``
- 富文字 bullet (around line 248): `` （`Control::rich`，一定是多行欄位） `` → `` （`ComponentConfig::rich`，一定是多行欄位） ``
- Out of scope list (around line 578): `` 第一版以現有 `Semantics`／`Operation` 為暫時來源 `` →
  `` 第一版以現有 `ComponentSnapshot`／`Operation` 為暫時來源 ``
- 動詞 bullets (around lines 245-247): `` `SemanticNode::verbs` `` → `` `NodeSnapshot::verbs` ``, `` `SemanticNode::scroll` `` →
  `` `NodeSnapshot::scroll` ``, `` `SemanticNode::text_align` `` → `` `NodeSnapshot::text_align` ``.
- Any other present-tense mention of `SemanticNode` in this spec (search the file) → `NodeSnapshot`; `SemanticTree`,
  `NodeRef`, `describe()` and `Ui::semantics()` keep their names (#96 Q20, Q23). Leave the "現況" table (lines 60-75) as it is.

## docs/specs/first-component-batch.md

多行文字欄位 row (around line 146): `` `Control::multiline` 與 `visible_lines` 在建立時決定，之後不能改；仍是 `Kind::text` `` →
`` `ComponentConfig::multiline` 與 `visible_lines` 在建立時決定，之後不能改；仍是 `ComponentKind::text` ``
