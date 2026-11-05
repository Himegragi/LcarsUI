# S1 驗收紀錄

> Historical record for the original S1 revision. Current rework results are in [rework-verification.md](rework-verification.md).

票：[#15 S1：以 C++20 元件實作雙外觀最小編輯表單](https://github.com/Himegragi/LcarsUI/issues/15)。分支 `s1/minimal-edit-form`，基底 `e6d3fb3`（既有 MVP）。下列結果對應程式碼提交 `7aa136c`；本紀錄另以後續提交保存。原型分支 `prototype/component-showcase`（`ae4dced`）的 82 項結果**未**轉算為本票通過。

狀態用語：通過／部分／未執行／受阻。截圖只佐證位置與呈現；值、結果、焦點、歷史一律以可查詢紀錄核對。

## 環境與工具鏈

| 項目 | 值 |
| --- | --- |
| OS | Windows build 26300.9711（26H2；登錄檔產品名仍寫 Windows 10 Pro N） |
| 編譯器 | clang-cl 22.1.8（LLVM `ca7933e4`），目標 x86_64-pc-windows-msvc |
| 建置工具 | Visual Studio Build Tools 2026 18.10.1、CMake 4.3.1-msvc1、Ninja 1.13.2、Windows SDK 10.0.26100.0 |
| GPU | NVIDIA GeForce RTX 3060，驅動 610.88，OpenGL 3.3 core |
| 字型 | `C:\Windows\Fonts\segoeui.ttf`（主）、`msjh.ttc` 微軟正黑體（缺字 fallback） |
| 輸入法 | 微軟注音：TIP `IMTCTIP.DLL` 6.2.26100.9711、`ChtIME.exe` 6.2.26100.9278，profile `0x0404 {B2F9C502-…}` |
| 螢幕 | 主螢幕系統縮放 150%（測試時 `GetDpiForWindow` = 144），另有一個不同縮放的副螢幕（未用於本票） |

實際語言旗標（取自三組建置的 `compile_commands.json`，只計本專案的編譯單元，不含第三方 HarfBuzz／FreeType）：

| 建置 | 編譯單元 | 旗標 |
| --- | --- | --- |
| Debug（含桌面） | 16 | 全部 `-std:c++20` |
| Release（含桌面） | 16 | 全部 `-std:c++20` |
| Debug `-CoreOnly` | 10 | 全部 `-std:c++20` |

代表指令（Release `src/core.cpp`）：`clang-cl /nologo -TP -I…\include /DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -std:c++20 -MD /utf-8 /W4 …`。基線（遷移前的 `e6d3fb3`）為 `-std:c++latest`。

## 自動化結果（提交 `7aa136c`）

| 建置 | CTest |
| --- | --- |
| Debug | 6/6：behavior_and_persistence、components、layout、edit_form、consumer_example、host_native |
| Release | 6/6（同上） |
| Debug `-CoreOnly` | 5/5（無 host_native；未建立視窗、未連結 OpenGL／字型） |
| Release `-Smoke` | 6/6，另含設定管理程式 MVP 兩套外觀的真實視窗冒煙測試與 GPU readback：全部 PASS（既有行為回歸） |

- `components`：14 案例（C01–C03 核心公開操作）。
- `layout`：9 案例（B01–B03，假字寬量測）。
- `edit_form`：7 案例 ×（Web、Material）×（出廠識別、另一組識別）＝ 28 次執行。
- `host_native`：32 項，見 [host.tsv](host.tsv)。
- 真實輸入：`lcars_host_verify --ime`，兩套外觀共 36 項全部 PASS，見 [ime.tsv](ime.tsv) 與原始 IME 訊息 [ime-events.log](ime-events.log)。此項會接管鍵盤，不在 CTest 內。

## 逐條驗收

| AC | 狀態 | 證據 |
| --- | --- | --- |
| AC01 C++20 正式入口 | 通過 | `CMakeLists.txt` 的 `lcars_target`：`CXX_STANDARD 20`、`CXX_STANDARD_REQUIRED ON`、`CXX_EXTENSIONS OFF`、`cxx_std_20`。旗標與工具版本見上表。沿用 CMake 與 repo 內附來源，沒有新增套件管理器。 |
| AC02 可重用組裝 | 通過 | `examples/consumer` 為獨立 CMake 專案，只看得到 `include/`（實際 `INCLUDES = -I…\include`），用自己的識別、資料模型、驗證與回呼組表單並執行（CTest `consumer_example`）。`edit_form` 套件以另一組完全不同的識別重跑全部案例。 |
| AC03 分離與可測性 | 通過 | `-CoreOnly` 不配置桌面相依；核心、版面、外觀、表單皆可無視窗測試。平台／文字／渲染在 `lcars_desktop`。 |
| AC04 初值、套用及重設 | 通過 | `edit_form` AC04：初值、草稿與結果分離、合法套用恰增 1；先讓欄位偏移與頁面捲動都變成非零，再重設，確認歷史、偏移、捲動歸零，焦點在重設，資料區停用時重設仍可用。 |
| AC05 標籤與錯誤 | 通過 | `components` C01：`describe` 回傳名稱、說明、錯誤與關聯標籤；錯誤只有一個來源。`layout` AC05：長標籤與多行錯誤重新量測、推開後續元件、不重疊；英文在空白處斷行。`edit_form` AC05：空白／全形空白在離焦與套用時出錯、聚焦名稱、保留舊結果、修正清錯、成功只增 1。`host_native` AC13：16 組排版皆無重疊。 |
| AC06 按鈕一次啟動 | 通過 | `components` AC06 與 `host_native` AC06：點擊、Enter、Space 各一次；長按不連發；移出放開取消；`WM_CANCELMODE` 與 `WM_KILLFOCUS` 後不留待啟動狀態；停用不啟動且略過 Tab。 |
| AC07 事件歸屬 | 通過 | 兩條路徑：(1) `host_native` AC07-R01 以投遞訊息經宿主自身的 `TranslateMessage`／`DispatchMessage` 重播；(2) `ime.tsv` AC07-real 以 `SendInput` 的真實按鍵。軌跡顯示 Space 產生的 `WM_CHAR` 在焦點仍在「名稱錯誤」按鈕時送達並被拒；放開才啟動並移焦；名稱欄只有固定案例的一筆歷史；之後真的打 Space 正常插入一次。沒有吞掉任何 Space。 |
| AC08 單行編輯 | 通過 | `components` AC08：碼位移動、Home／End、Shift 選取、刪除、全選、代理對不被切開、公開長度上限與超限整筆拒絕。`layout` AC08：100%／150% 下每個繪製游標位置以指標命中回到同一位置。`host_native` AC08：真實訊息的點擊定位、Shift 延伸、拖曳選取、系統剪貼簿複製／貼上／復原（測後還原使用者剪貼簿文字）。字型 fallback 與支援邊界見 [README](../README.md#本切片的政策與界線)。 |
| AC09 貼上及欄位歷史 | 通過 | 「全選、貼上 Alpha＋換行＋Beta、復原、重做」得到 Alpha Beta／原名稱／Alpha Beta，上次套用結果不變（`components`、`edit_form`）。一次貼上與選取取代、一次 IME 確認各一筆（真實注音 AC09-undo-commit、E01-replace-selection）；逐次輸入每次一筆；快捷鍵只作用於焦點欄位；新編輯清除 redo。 |
| AC10 唯讀與停用 | 通過 | 唯讀可聚焦、選取、複製；輸入、剪下、貼上、Delete、Backspace、復原、重做（鍵盤與語意呼叫）皆回報 `read_only`，文字與歷史位置不變。停用時焦點、指標、鍵盤、語意寫入、套用全部回報 `disabled`，恢復後資料與歷史保留（`components`、`edit_form`、`host_native` AC10-native-disabled）。狀態以文字呈現：欄位標籤加「（唯讀）」「（停用）」，狀態列寫「名稱唯讀」「資料區停用」。 |
| AC11 焦點及語意 | 通過 | Tab／Shift+Tab 到達並離開測試入口、名稱、套用並循環；`host_native` AC11 以截圖像素確認焦點環可見且只在焦點元件上。`describe` 提供識別、類型、名稱、值、停用／唯讀／焦點／錯誤狀態與目前可用操作；受限操作回傳可檢查的拒絕結果。這是公開 C++ 介面，**不是** UIA 或 MCP。 |
| AC12 兩套外觀 | 通過 | 全部表單案例在兩套外觀各跑一次，每案例結尾以該外觀實際排版並檢查角色承載；AC12 案例比對兩套外觀下每一步的操作結果、歷史深度、草稿、結果、錯誤與焦點完全一致。外觀只提供 `arrange_field`／按鈕尺寸／配色，不含驗證、編輯或事件處理。無執行中換膚。 |
| AC13 最小版面及首幀一致 | 通過（含界線） | `host_native` AC13：960×720 與 640×480 DIP × 100%／150%，兩套外觀、初始與「長說明＋錯誤」各一，共 16 組：無重疊、不超出寬度、寬窗結果在旁、窄窗結果在下、每個鍵盤入口都能捲到完整可見且命中正確。R06：真實視窗深捲動後 `SetWindowPos` 改尺寸，僅一次 `WM_PAINT` 即為已夾限的位置，與下一幀及命中一致（100% 與 150%）。**界線**：150% 是強制渲染尺度；960×720 DIP 在 150% 是 1440×1080 實體像素，超出主螢幕工作區，因此矩陣以同一份排版離屏繪製。這些都**不是**真實跨螢幕 DPI 證據（D01 屬 S5）。 |
| AC14 真實 IME | 通過（記錄範圍內） | 見下節。 |
| AC15 可重跑外部驗證 | 通過 | 案例由應用組裝後的公開入口驅動；原生事件、IME、幾何用宿主入口。本表逐條標記狀態；開發中發現的失敗與重現方式見「過程中發現並修正的問題」。 |
| AC16 完成紀錄 | 通過 | 建置／啟動／驗證方式見 [README](../README.md)；提交、工具鏈、兩套外觀結果、代表畫面與平台紀錄保存於本目錄。Standards／Spec 兩軸審查的可行動問題已在 `7aa136c` 處理。未完成的平台項目列於最後一節。 |

## AC14 與 E01：真實微軟注音

以 `SendInput` 送出真實按鍵，經作業系統輸入堆疊與微軟注音處理；按鍵本身由程式產生，不是真人手打。兩套外觀各跑一輪，每步記錄草稿、是否組字、組字內容、焦點、歷史深度、套用次數，以及原始 IME 訊息。

| 步驟 | 失焦前 | 結果（兩套外觀相同） |
| --- | --- | --- |
| 打 ㄨㄛˇ（j i 3） | — | 組字更新 ㄨ → ㄨㄛ → 我；已提交文字不變 |
| Enter 確認 | 組字「我」 | 「測試使用者我」，一筆歷史，未套用 |
| 再按 Enter | — | 未套用 |
| Ctrl+Z | — | 整個確認一步復原 |
| Esc 取消 | 組字「我」 | 組字清空；已提交文字與 redo 位置保留 |
| 全選後組字確認 | 選取全部 | 「我」取代選取，一筆歷史 |
| 組字中 Ctrl+Z | 組字「我」 | 由 IME 處理（組字被清除），已提交歷史不變 |
| 組字中 Tab | 組字「我」 | **IME 吃掉 Tab**：組字保留、焦點不動、未提交；再按 Esc 乾淨丟棄 |
| 組字中點其他按鈕 | 組字「我」 | 宿主請 IME 完成組字 → 同步收到結果 → 「測試使用者我」一筆歷史 → 焦點移到被點按鈕；未套用 |
| 組字中點同一欄位 | 組字「我」 | 先提交（一筆歷史），再放置游標；焦點留在名稱 |
| 組字中視窗失去前景 | 組字「我」 | 注音在 `WM_KILLFOCUS` 後**自行提交**「我」，一筆歷史；回到視窗時焦點仍在名稱、無殘留組字 |

**本切片的失焦預設：離焦即提交。** 這是依上表的真實事件決定的：注音在視窗失焦時本來就會提交，宿主在視窗內移焦前也請 IME 完成組字，核心的公開 `focus()` 採同一政策，三條路徑一致。原型的「離焦取消」只作參考，未採用。

記錄範圍：只有微軟注音一種輸入法、一個音節；未測候選清單中以方向鍵／數字選字、未測以 Alt+Tab 或點擊其他程式造成的失焦（以本程序的另一個視窗取得前景代替）、未測切頁與開對話框（S3／S4）。

## 過程中發現並修正的問題

| 問題 | 重現 | 處理 |
| --- | --- | --- |
| 核心與 IME 組字不同步 | 名稱欄組字中，以程式呼叫 `select()`：核心默默取消組字，IME 仍在組字，下一個音節變成「我我」 | `select()` 在組字中回報 `busy`；宿主發現 IME 有組字而核心沒有時通知 IME 取消 |
| 程式移焦後 IME 關聯未跟上 | 焦點在按鈕時由程式把焦點移到名稱欄，立刻打 j：字母直接進入欄位 | 宿主在每個訊息派送後偵測焦點變化並同步 IME 關聯 |
| 核心離焦丟棄組字、原生路徑提交 | 組字中呼叫 `focus()` 與用滑鼠點別處結果不同 | 核心離焦改為提交（E01 預設） |
| 焦點通知遞迴 | `on_focus` 回呼中再呼叫 `focus()` | 通知排隊，回呼不重入；回呼例外回報 `failed` |
| 非文字種類空指標 | 版面中放入 `toggle` 或 `navigation` 元件 | 依種類分為標籤／指令／文字欄位三類 |
| 英文單字被從中間斷行 | 長說明中的「Ctrl+Shift+Z」 | 在空白或中日韓字元旁斷行，單字過長才硬切 |

## 尚未完成或不在本票範圍

- **U01 使用者試用**：沒有使用者手動試用回報；自動測試不代表操作感受已獲確認。
- **D01 真實跨螢幕 DPI**：未做 100%→150% 的跨螢幕遷移（S5）。本票的 150% 只是強制渲染尺度。
- **A01 無障礙**：沒有 UIA provider、螢幕閱讀器或 MCP；語意查詢只是 C++ 介面（#10）。
- **E01 其他路徑**：組字中切頁（S3）、開對話框（S4）、完整矩陣（S5）。
- **T01**：完整 grapheme、RTL、彩色 emoji、多行不支援。
- 只驗證過這台機器的 Windows 組建、clang-cl、NVIDIA 驅動與微軟注音。
