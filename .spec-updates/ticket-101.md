# Ticket #101 spec updates (0.4.0)

Apply to the untracked author-machine specs under `E:\LcarsUI\docs\specs`.
The final scroll type is TextFieldScroll after merging #100 (which removed TextScroll).
Historical inventory rows can stay historical; the following describe the current contract.

## v1-semantic-layer-and-uia.md

Section: 語意層讀取（S2）, immediately before the C++ shape.

Old: `規格提案的 C++ 形狀（新標頭 include/lcars/semantics.hpp，屬 lcars_core）` (the existing Markdown puts backticks around the paths).

New: `元件快照（ComponentSnapshot，Handle::describe）與語意節點快照（NodeSnapshot，SemanticTree::get）維持兩個型別。同名欄位同義；元件節點的共同欄位及元件層級 verbs 與 describe() 完全相等。元件快照保留 items、labels、source_conflict、candidate；節點快照保留 bounds、instance、labelled_by／described_by 等語意樹專用欄位。選項維持 items 與 {元件識別碼, 選項值} 子節點兩種表示法。Verb 與 TextFieldScroll 宣告在 core.hpp；NodeSnapshot 與 SemanticTree 在 semantics.hpp。共同 struct 的抽取另由 #102 實作。`

Section: S2, `NodeSnapshot` code block.

Old: `bool visible;           // 未隱藏，且與最近一次排列的裁切範圍相交（沿用現有意義）`

New: `bool visible;           // 最近一次排版後畫面上仍有面積大於 0 的部分，且未被隱藏；與 ComponentSnapshot 共用同一份裁切後 bounds 資料，首次排版前為 false`

Old: `std::vector<Verb> verbs;             // 目前會被接受的動詞；受限者不列（沿用 core.hpp:49 的規則）；hover 永不列入`

New: `std::vector<Verb> verbs;             // 元件節點與 ComponentSnapshot 的元件層級清單相同；選項節點各自列出；hover 永不列入`

Section: S2, after the code block. Old: no paragraph defining the aligned snapshot fields.

New paragraph: `兩種快照中，標籤的 name 與 value 都是顯示文字；focused 指此刻鍵盤輸入送到哪裡，右鍵選單開著時元件為 false。選項 enabled 只代表選項自己的設定，不併入所屬元件；能否選取看 verbs 是否有 select。UIA IsEnabled 由轉接合併所屬元件與選項設定；所屬元件 enabled 改變時，轉接仍發出受影響選項的 IsEnabled 事件。writing_mode、vertical_digits 為 optional，只有標籤有值；滑桿統一用 range；多行文字欄位統一用 optional<TextFieldScroll> scroll（first_line、visible_lines、line_count）。舊 ComponentSnapshot::operations、slider_range、visible_lines、scroll_line、line_count 不保留別名。`

Section: 語意層操作（S3）, 富文字（#81） paragraph.

Old: `Operation 新增 style（富文字且可編輯的欄位才列出）；set_style、set_typing_style 只在 Operation::style 被准入時出現在 verbs。` (identifiers are backticked in the source).

New: `公開動作詞彙只有 Verb；富文字且可編輯的欄位在兩種快照的 verbs 同時列出 set_style、set_typing_style。內部分類不出現在公開快照。`

Section: S3, word selection paragraph.

Old: `（Operation::select 被准入）` (identifier backticked).

New: `（文字欄位允許 select 時）`.

Section: 範圍外 / deferred capability-description bullet.

Old: `公開能力描述的宣告工具（ADR 0008）；第一版以現有 ComponentSnapshot／Operation 為暫時來源，之後替換時語意層對外形狀不變（ADR 0011）。` (identifiers backticked).

New: `公開能力描述的宣告工具（ADR 0008）；目前以 ComponentSnapshot 為暫時來源，公開動作詞彙只有 Verb，Operation 已退出公開介面；之後替換來源時語意層對外形狀不變（ADR 0011，#101）。`

Section: 驗收. Old: no cross-snapshot matrix requirement.

New: `snapshots CTest 以每個狀態一個通用 driver，跑過全部內建元件類型；狀態包括一般、停用、唯讀（依類型適用性）、隱藏、對話框阻擋、右鍵選單開啟、多行文字捲動及捲出畫面。每次比對 describe 與 get 的全部共同欄位，及每個 items 與子節點的 enabled、selected、invalid。另覆蓋附屬錯誤標籤：核取方塊的錯誤列在畫面上時，兩種快照的 visible 都為 true（舊元件快照為 false）。`

## v1-packaging-and-delivery.md

Sections: P02 example; 版本; CS10. Apply the current release override below while retaining the original 0.2.0 release history.

Old P02 request: `find_package(LcarsUI 0.2 REQUIRED COMPONENTS Desktop AppearanceMaterial)`.

New P02 request: `find_package(LcarsUI 0.4 REQUIRED COMPONENTS Desktop AppearanceMaterial)`.

Old current-version rule: `第一版標 0.2.0、COMPATIBILITY SameMinorVersion（find_package(LcarsUI 0.2) 接受 0.2.x，拒絕 0.1.x、0.3.x、1.x）` (identifiers backticked).

New current-version rule: `#26 結構重構（四）版本為 0.4.0；COMPATIBILITY SameMinorVersion；find_package(LcarsUI 0.4) 接受 0.4.x，拒絕 0.1.x、0.2.x、0.3.x、1.x。`.

Old CS10: `版本：要求 0.2 成功、0.1 與 1.0 失敗；framework_version 等於 LcarsUI_VERSION（static_assert）。` (identifiers backticked).

New CS10: `版本：要求 0.4 成功、0.1／0.2／0.3 與 1.0 失敗；framework_version 等於 LcarsUI_VERSION，版本字串與巨集／常數都為 0.4.0（static_assert）。`.
