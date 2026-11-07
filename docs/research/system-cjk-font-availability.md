# 非中文 Windows 的系統繁中字型可得性

> 對應 issue #29。查證日期：2026-11-08。
> 用途：決定 v1「文字與呈現設定」規格裡的缺字型替代順序，以及字型下拉清單的來源。
> 前提：v1 不隨附字型、用系統字型、缺字型時不得當掉。目前 `src/text.cpp` 寫死 `segoeui.ttf` 與 `msjh.ttc`（`mingliu.ttc` 為選項），缺檔就 throw。

標記說明：
- **〔官方〕**：Microsoft 官方文件（learn.microsoft.com、support.microsoft.com）或 FreeType 官方文件明文寫的。
- **〔實測〕**：在本機（Windows 11 Pro **N**，build 26300，顯示語言 en-US，語言清單含 zh-Hant-TW，系統地區設定 zh-TW）量測的結果。只能當旁證，不能代表所有版本。
- **〔推論〕**：由上面兩者推出，沒有直接出處。
- **未確認**：找不到一手來源，也沒辦法在本機驗證。

---

## 結論摘要

| 問題 | 答案 |
| --- | --- |
| 1. 非繁中 Windows 有沒有 `msjh.ttc`？ | **有。**微軟正黑體（`msjh.ttc`／`msjhl.ttc`／`msjhbd.ttc`）列在 Windows 10 與 Windows 11 的**基本桌面字型**，不屬於任何 FOD。「繁體中文補充字型」FOD（`Language.Fonts.Hant`）只有 **MingLiU／PMingLiU／MingLiU_HKSCS（`mingliu.ttc`）與 DFKai-SB（`kaiu.ttf`）**。 |
| 2. 一定存在、可涵蓋繁中常用字的字型 | 基本桌面字型裡有：Microsoft JhengHei、Microsoft YaHei、SimSun、Yu Gothic、MS Gothic、Malgun Gothic。DengXian 屬於簡中 FOD，**不一定有**。`mingliub.ttc`（MingLiU-ExtB）雖然一定有，但只收 Ext-B 字，**不收常用字**。應用程式可以在執行時載入系統字型來顯示文字，但不能把字型檔隨程式散布。 |
| 3. Segoe UI／Emoji／Symbol | 三者都在 Windows 10、Windows 11 的基本桌面字型清單。`seguiemj.ttf` 是 **COLR/CPAL 向量字型（v0＋v1），沒有點陣圖表**；本機實測每個 COLR base glyph 在 `glyf` 都有自己的單色輪廓，所以 FreeType 不設 `FT_LOAD_COLOR` 時可以畫成單色。 |
| 4. 不寫死路徑的官方列舉 API | DirectWrite：`IDWriteFactory6::GetSystemFontSet(FALSE, …)`（只有本機已安裝的字型），每個 face 用 `IDWriteFontFaceReference::GetFontFile` → `IDWriteFontFile::GetLoader` → `QueryInterface(IDWriteLocalFontFileLoader)` → `GetFilePathFromKey` 取得絕對路徑。本機實測**包含** `%LOCALAPPDATA%\Microsoft\Windows\Fonts` 裡每位使用者自己安裝的字型。**注意**：不帶參數的 `IDWriteFactory3::GetSystemFontSet()` 會混入「可下載但尚未安裝」的字型（本機 592 個 face 裡有 239 個不在本機）。 |

---

## 1. 微軟正黑體在非繁中顯示語言上是否預設存在

### 1.1 基本字型 vs. Features on Demand

**〔官方〕** Windows 10 字型清單的引言寫道，Windows 10 把一部分字型移到選用、隨選安裝的套件裡，套件依文字系統劃分，「most are added automatically by Windows Update when the associated languages are enabled in language settings (for example, by enabling a keyboard)」，也可以從「設定 > 系統 > 選用功能」手動加裝。
— <https://learn.microsoft.com/en-us/typography/fonts/windows_10_font_list>

同一頁的第一張表（2004 版之後的 desktop font set）列出：

| 家族 | 檔案 | 出現在哪裡 |
| --- | --- | --- |
| Microsoft JhengHei / JhengHei UI（Light、Regular、Bold） | `Msjhl.ttc`、`Msjh.ttc`、`MSJHBD.ttc` | **基本字型** |
| Microsoft YaHei / YaHei UI | `Msyhl.ttc`、`Msyh.ttc`、`Msyhbd.ttc` | 基本字型 |
| SimSun / NSimSun、SimSun-ExtB | `Simsun.ttc`、`Simsunb.ttf` | 基本字型 |
| MingLiU-ExtB / PMingLiU-ExtB / MingLiU_HKSCS-ExtB | `Mingliub.ttc` | 基本字型 |
| Yu Gothic / Yu Gothic UI | `YuGothL/R/M/B.ttc` | 基本字型 |
| MS Gothic / MS PGothic / MS UI Gothic | `Msgothic.ttc` | 基本字型 |
| Malgun Gothic | `Malgun*.ttf` | 基本字型 |
| Segoe UI、Segoe UI Emoji、Segoe UI Symbol | `Segoeui*.ttf`、`Seguiemj.ttf`、`Seguisym.ttf` | 基本字型 |

FOD 區段：

- **Chinese (Traditional) Supplemental Fonts**：DFKai-SB（`Kaiu.ttf`）、MingLiU／MingLiU_HKSCS／PMingLiU（`Mingliu.ttc`）。**只有這兩個家族。**
- **Chinese (Simplified) Supplemental Fonts**：DengXian（`Deng*.ttf`）、FangSong、KaiTi、SimHei。
- **Japanese Supplemental Fonts**：Meiryo、MS Mincho、Yu Mincho、BIZ UD*、UD Digi Kyokasho。
- **Korean Supplemental Fonts**：Batang、Gulim、Dotum、Gungsuh。

**〔官方〕** Windows 11 字型清單的基本表也一樣列出 Microsoft JhengHei（`Msjh*.ttc` 6.14／6.13）、YaHei、SimSun、MingLiU-ExtB、Yu Gothic、Malgun Gothic、Segoe UI、Segoe UI Emoji（1.33）、Segoe UI Symbol，另外加上 Segoe UI Variable（`SegUIVar.ttf`）與 Segoe Fluent Icons。FOD 區段的繁中、簡中內容跟 Windows 10 相同。
— <https://learn.microsoft.com/en-us/typography/fonts/windows_11_font_list>（ms.date 2022-04-21；頁面寫「fonts that shipped with Windows 11」，**沒有按 23H2／24H2 分版**）

**〔官方〕** Microsoft JhengHei 字型頁：「distributed with Windows Vista as default interface font」，供應產品列出 Windows 11／10／8.1／8／7、Vista、Server 2008。
— <https://learn.microsoft.com/en-us/typography/font-list/microsoft-jhenghei>

**結論**：英文、日文等非繁中顯示語言的 Windows 10 22H2／Windows 11 23H2+，依官方清單**預設就有** `msjh.ttc`、`msjhl.ttc`、`msjhbd.ttc`。「繁體中文補充字型」FOD **不含**正黑體。程式目前把 `mingliu.ttc` 當選項，那個檔**才是**FOD 的內容，非繁中環境可能沒有。

**〔實測〕** 本機（Windows 11 Pro N 26300）`C:\Windows\Fonts` 有 `msjh.ttc`、`msjhl.ttc`、`msjhbd.ttc`，也有 `mingliu.ttc`。本機語言清單有 zh-Hant-TW，所以 Hant FOD 有裝，這點符合官方描述。

### 1.2 FOD 何時會自動安裝

**〔官方〕** Language FOD 頁的 Font FOD 段落寫道：「These additional fonts are not required for the Windows to display a language. However, prior to Windows 10, most of these fonts were installed by default on every system…」；表格把 `zh-TW` 對應到 `Language.Fonts.Hant~~~und-HANT~0.0.1.0`，`zh-CN` 對應到 `Language.Fonts.Hans~~~und-HANS~0.0.1.0`；給 OEM 的建議是「If a PC will be sold to a certain region, install that region's font」。
— <https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/features-on-demand-language-fod>

**〔官方〕** 同一頁（Windows 11 版）：標準使用者可以從「時間與語言」設定取得 Language FOD；IT 可以用群組原則／MDM `RestrictLanguagePacksAndFeaturesInstall` **禁止**使用者取得。

**〔官方〕** FOD 總覽頁：「When Windows 11 or Windows Server needs a new feature, it can request the feature package from Windows Update」；DISM `/add-capability` 依序找 `/Source` → 群組原則指定位置 → Windows Update，加上 `/LimitAccess` 就不會去找 Windows Update 或 WSUS。
— <https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/features-on-demand-v2--capabilities>

歸納：

- **會自動裝**：在語言設定加入 zh-TW、zh-HK（或啟用該語言的鍵盤），而且裝置能連到 Windows Update 時〔官方，「most are added automatically」〕。OEM 出貨到台灣、香港時預先裝好〔官方建議〕。
- **不會裝**：純英文、日文等環境沒有加中文語言時，**MingLiU／DFKai-SB 不會有**〔推論，依清單分類〕。
- **可能裝不上**：IT 設了 `RestrictLanguagePacksAndFeaturesInstall`〔官方〕。離線或由 WSUS 管理的環境，自動取得的實際行為**未確認**（官方只寫 DISM 的來源順序，沒寫語言設定觸發的自動安裝在 WSUS 下會怎樣）。

### 1.3 N、LTSC、Server 的差異

- **N 版**〔官方〕：Windows 10/11 N「includes the same functionality as other Windows 10/11 editions, except for Windows Media Player and related technologies」；排除清單只有 Media Player、Media Foundation、WPD、各種影音編解碼器與幾個媒體 app，**沒有提到字型**。
  — <https://support.microsoft.com/windows/media-feature-pack-for-windows-n-8622b390-4ce6-43c9-9b42-549e5328e407>
  〔實測〕本機就是 Windows 11 Pro **N**，正黑體、Segoe UI、Segoe UI Emoji、Segoe UI Symbol 都在。
- **LTSC**：**未確認**。官方字型清單沒有另外寫 LTSC 的版本，也找不到 LTSC 排除字型的官方說法。〔推論〕LTSC 是 Enterprise 桌面版，應該跟 desktop font set 一樣，但沒有一手來源。
- **Server**〔官方，部分〕：Windows 10 字型清單引言寫，除了 UWP 建議字型集以外，「A number of additional fonts are available for Desktop and Server」，而且字型 FOD 機制同樣適用；Microsoft JhengHei 頁的供應產品有列 Windows Server 2008。Server 2016 以後（含桌面體驗）是否等同 Windows 10 的 desktop font set：**未確認**（沒有 Server 專屬字型清單）。**Server Core** 的字型內容：**未確認**（Server Core App Compatibility FOD 的官方頁沒列字型）。
- 非桌面版（Xbox、HoloLens、Surface Hub）〔官方〕：「Not all of the Desktop fonts will be in non-desktop editions」。這不在 LcarsUI 的目標範圍內。

---

## 2. 一定存在、可涵蓋繁中常用字的字型

### 2.1 官方分類與字型頁資訊

| 字型 | 是否一定存在（Win10 2004+／Win11 桌面版） | 官方 `meta` dlng／code page | 說明 |
| --- | --- | --- | --- |
| Microsoft JhengHei（`msjh.ttc`） | 是（基本） | dlng `Bopo`,`Hant`；CP 950 | 繁中 UI 字型，首選 |
| MingLiU／PMingLiU（`mingliu.ttc`） | **否**（Hant FOD） | dlng `Bopo`,`Hant`；CP 950 | 只在加了繁中語言時才有 |
| MingLiU-ExtB（`mingliub.ttc`） | 是（基本） | — | 只收 CJK Ext-B，常用字一個都沒有（見實測） |
| Microsoft YaHei（`msyh.ttc`） | 是（基本） | dlng `Hans`；CP 936 | 簡中字型 |
| SimSun（`simsun.ttc`） | 是（基本） | dlng `Hans`；CP 936 | 簡中宋體 |
| DengXian（`deng.ttf`） | **否**（Hans FOD） | — | 不能當保底 |
| Yu Gothic（`YuGoth*.ttc`） | 是（基本） | dlng `Jpan`…；CP 932 | 日文 |
| Malgun Gothic（`malgun.ttf`） | 是（基本） | — | 韓文 |

出處：<https://learn.microsoft.com/en-us/typography/font-list/microsoft-yahei>、<https://learn.microsoft.com/en-us/typography/font-list/mingliu>、<https://learn.microsoft.com/en-us/typography/font-list/yu-gothic>、<https://learn.microsoft.com/en-us/typography/font-list/simsun>，以及 1.1 節的兩份字型清單。

### 2.2 字形涵蓋（實測）

**〔實測〕** 用本機字型檔的 `cmap`（TTC 取第一個 face），對照 Big5 第一級常用字（5401 字）與第二級次常用字（7650 字）轉成 Unicode 後的集合：

| 檔案 | Big5 常用字（5401） | Big5 次常用字（7650） |
| --- | --- | --- |
| `msjh.ttc` | 100% | 100% |
| `mingliu.ttc` | 100% | 100% |
| `msyh.ttc` | 100% | 100% |
| `simsun.ttc` | 100% | 100% |
| `YuGothR.ttc` | 97.7% | 55.1% |
| `msgothic.ttc` | 97.2% | 54.9% |
| `malgun.ttf` | 88.6% | 25.6% |
| `mingliub.ttc` | 0% | 0% |
| `segoeui.ttf` | 0% | 0% |

注意：

- 這是**本機**檔案版本的量測結果；官方清單上的版本號跟本機不一定一樣。
- YaHei、SimSun 的**碼位**涵蓋完整，但它們是簡中設計（dlng 為 `Hans`）。〔推論〕部分字的字形會依大陸規範，跟台灣教育部標準字體不同（例如筆畫寫法）。這屬於「看得到字，但寫法不道地」，比豆腐方塊好，但不適合當主字型。實際有哪些字不同：**未確認**（沒有逐字比對）。
- 日文、韓文字型會缺不少次常用字，只適合當最後幾層保底。

### 2.3 授權：應用程式能不能在執行時載入

**〔官方〕** 字型散布 FAQ：

- 「Windows comes with a collection of fonts that are installed as system-wide resources. Any application installed on your Windows computer has access to these fonts. A Windows application can use the fonts to render content to a screen…」→ 在使用者電腦上執行時讀取 `C:\Windows\Fonts` 的字型來繪製文字是允許的。
- 這些規則也適用於 symbol 和 emoji 字型；但「Hidden fonts… which may be embedded in code… should not be extracted and used」。
- 「Can I embed the fonts into a game, application or device…? No」；轉成點陣字型再隨程式附帶也不行；除了文件嵌入以外，「you may not redistribute the Windows fonts」。
- Segoe UI Variable「is not available for licensing or use outside of Microsoft products or on non-Windows platforms」（只限制在非 Windows 平台或 Microsoft 產品以外使用；在 Windows 上由系統提供則適用前面那條）。

— <https://learn.microsoft.com/en-us/typography/fonts/font-faq>（ms.date 2024-11-19）

**對 LcarsUI 的意義**〔推論〕：在執行時用 FreeType 開啟系統字型檔、自己點陣化顯示文字，屬於「application can use the fonts to render content to a screen」。**不能**把 `msjh.ttc` 等檔案放進安裝包或 repo，也**不能**預先烘焙成字型圖集（atlas）再隨程式散布。執行時產生、只放在記憶體裡的 glyph cache 是不是也算「conversion」：FAQ 沒有明講，**未確認**；不過 DirectWrite、GDI 本身也是這樣做的。

---

## 3. Segoe UI、Segoe UI Emoji、Segoe UI Symbol

### 3.1 是否一定存在

**〔官方〕** 三者都在 Windows 10（2004）與 Windows 11 的基本字型表，不在任何 FOD 區段：`Segoeui.ttf` 等 12 個檔、`Seguiemj.ttf`（Win10 1.29、Win11 1.33）、`Seguisym.ttf`（6.23）。
— 1.1 節的兩份字型清單；<https://learn.microsoft.com/en-us/typography/font-list/segoe-ui-emoji>

Server／LTSC 的情況同 1.3 節，**未確認**。

### 3.2 Segoe UI Emoji 的字形資料格式

**〔實測〕** 解析本機 `C:\Windows\Fonts\seguiemj.ttf`（Windows 11 26300）的表：

- 表有 `COLR`、`CPAL`、`glyf`、`loca`、`GSUB`、`GPOS` 等；**沒有** `CBDT`／`CBLC`／`sbix`／`SVG `，也就是**沒有彩色點陣，也沒有 SVG**。
- `COLR` version = **1**；v0 `BaseGlyphRecord` 3487 筆，v1 `BaseGlyphList` 3487 筆。
- 3487 個 base glyph **全部**在 `glyf` 都有非空的輪廓資料（簡單輪廓或複合字形）。
- 抽查：U+1F600、U+2764、U+263A、U+1F44D、U+2600、U+1F680 的 base glyph 都有輪廓。

Windows 10 22H2 那一版（1.29）的表結構：**未確認**（本機只有 Windows 11）。

**〔官方〕OpenType COLR 規格**：「If the base glyph ID has an outline, that may be rendered as a non-color glyph instead.」（原文是講 v1 graph 解析失敗時的退路）。規格**沒有要求** base glyph 一定要有單色輪廓，所以單色退路要看字型本身。
— <https://learn.microsoft.com/en-us/typography/opentype/spec/colr>

**〔官方〕FreeType `FT_LOAD_COLOR`**：COLR 支援是「experimental」，`FT_Render_Glyph` 的預設混色「works only for glyphs in 'COLR' v0 tables. There is no rendering support for 'COLR' v1」；要完整控制圖層，就用 `FT_Get_Color_Glyph_Layer` 搭配 `FT_Palette_Select`。
— <https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html>

〔推論〕不設 `FT_LOAD_COLOR` 時，FreeType 會照一般 TrueType 字形載入該 glyph ID 的 `glyf` 輪廓，所以 `seguiemj.ttf` 可以畫成**單色**。如果要彩色，可以用 v0 圖層（`FT_LOAD_COLOR` 或 `FT_Get_Color_Glyph_Layer`）。v1 的漸層等效果 FreeType 本身畫不出來。這個推論沒有實際用 FreeType 跑過，建議實作時加一個單元測試確認。

---

## 4. 不寫死路徑、列出已安裝字型與檔案位置的官方 API

### 4.1 建議做法：DirectWrite 字型集

**〔官方〕** 「Most of the time, apps use the fonts that are installed locally on the system. DirectWrite provides access to these fonts using the IDWriteFactory3::GetSystemFontSet or IDWriteFactory::GetSystemFontCollection methods. In some cases, apps may also want to use fonts that are included as part of Windows 10 but that are not currently installed… Such fonts can be accessed from the Windows font service by using the GetSystemFontSet method, or by calling IDWriteFactory3::GetSystemFontCollection with includeDownloadableFonts set to TRUE.」
— <https://learn.microsoft.com/en-us/windows/win32/directwrite/custom-font-sets-win10>

**〔官方〕** `IDWriteFactory6::GetSystemFontSet(BOOL includeDownloadableFonts, IDWriteFontSet1**)`：「`false` if you only want locally installed fonts.」最低需求 Windows 10 build 16299（Win10 22H2、Win11 都符合）。
— <https://learn.microsoft.com/en-us/windows/win32/api/dwrite_3/nf-dwrite_3-idwritefactory6-getsystemfontset>

**〔官方〕** `IDWriteLocalFontFileLoader::GetFilePathFromKey`：「Obtains the absolute font file path from the font file reference key.」
— <https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritelocalfontfileloader-getfilepathfromkey>

流程：

1. `DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6), …)`
2. `factory->GetSystemFontSet(FALSE, &fontSet)`：只取本機已安裝的字型。
3. 下拉清單用的家族名稱：`fontSet->GetPropertyValues(DWRITE_FONT_PROPERTY_ID_WIN32_FAMILY_NAME 或 _TYPOGRAPHIC_FAMILY_NAME, locale, &stringList)`。
4. 某個 face 的檔案路徑：`fontSet->GetFontFaceReference(i, &ref)` → `ref->GetFontFile(&file)` → `file->GetLoader(&loader)` → `loader->QueryInterface(IDWriteLocalFontFileLoader)` → `file->GetReferenceKey(&key,&size)` → `GetFilePathLengthFromKey`／`GetFilePathFromKey`。TTC 的 face index 用 `ref->GetFontFaceIndex()`，可以直接交給 FreeType `FT_New_Face(path, faceIndex)`。
5. 系統字型資料夾本身可以用 `SHGetKnownFolderPath(FOLDERID_Fonts)` 取得，不必寫死 `C:\Windows\Fonts`（只能當最後的退路，因為這樣拿不到每位使用者自己安裝的字型）。

**〔實測〕** 本機用 clang++ 編了一個小 probe（只用到上述 API）：

| 呼叫 | face 數 | 不在本機（`GetLocality() != DWRITE_LOCALITY_LOCAL`） | 路徑不在 `C:\Windows\Fonts` 的 face |
| --- | --- | --- | --- |
| `IDWriteFactory6::GetSystemFontSet(FALSE)` | 353 | 0 | 33（全部在 `C:\USERS\…\APPDATA\LOCAL\MICROSOFT\WINDOWS\FONTS\`） |
| `IDWriteFactory6::GetSystemFontSet(TRUE)` | 592 | 239 | 33 |
| `IDWriteFactory3::GetSystemFontSet()` | 592 | **239**（例如 Sitka Small／Sitka Text） | — |
| `IDWriteFactory::GetSystemFontCollection(…, FALSE)` | 121 個家族；找得到 per-user 的「Agave Nerd Font」與「Microsoft JhengHei」 | — | — |

重點：

- **DirectWrite 系統字型集包含每位使用者自己安裝的字型**〔實測〕，`GetFilePathFromKey` 會回傳 `%LOCALAPPDATA%\Microsoft\Windows\Fonts\…` 的絕對路徑（回傳的字串是全大寫，比對時要不分大小寫）。官方文件**沒有明文**寫系統字型集會包含 per-user 字型，這點在官方層級**未確認**，只有實測支持。
- **不帶參數的 `IDWriteFactory3::GetSystemFontSet()` 會混入可下載但沒安裝的字型**〔實測，與官方說法一致〕。如果拿來做下拉清單或替代鏈，會選到開不了檔的字型。所以要用 `IDWriteFactory6::GetSystemFontSet(FALSE)`，或用 `GetLocality()` 過濾。
- 同一份字型集也能直接拿來實作「缺字替代」的查詢，例如用 `IDWriteFontSet::GetMatchingFonts` 或 `IDWriteFontFallback::MapCharacters` 找出系統認為能顯示某字元的字型，再把那個檔案路徑交給 FreeType。這等於只用 DirectWrite 當字型目錄，排版和點陣化還是 HarfBuzz＋FreeType。〔推論〕這比自己寫死替代順序更穩，但會在專案裡新增 DirectWrite 依賴，需要另外決定。

### 4.2 登錄檔（不建議當主要來源）

**〔官方，舊文件〕** `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts` 的值名稱是字型顯示名稱，值是檔名（相對於字型資料夾）或完整路徑。這是 Windows Server 2003 時代的文件，目前沒有對應的現行文件。
— <https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2003/cc787587(v=ws.10)>

**〔實測〕** 本機 `HKCU\Software\Microsoft\Windows NT\CurrentVersion\Fonts` 列出每位使用者自己安裝的字型，值是 `%LOCALAPPDATA%\Microsoft\Windows\Fonts\…` 的**完整路徑**。這個 HKCU 位置在官方文件裡的地位：**未確認**（找不到現行官方文件）。登錄檔的值名稱是「顯示名稱 + (TrueType)」，不是家族名稱，TTC 也不會拆成各個 face，做下拉清單還得自己打開檔案讀 `name` 表。所以只適合當 DirectWrite 不能用時的退路。

### 4.3 GDI

`EnumFontFamiliesEx` 可以列家族名稱，但**拿不到檔案路徑**〔官方 API 本身就沒有這個輸出〕，所以不適合 FreeType 管線。

---

## 對 v1 規格的建議（推論，供決策參考）

1. **主字型**：Latin 用 Segoe UI（`segoeui.ttf`），CJK 用 Microsoft JhengHei UI（`msjh.ttc`）。兩者在 Win10 2004+／Win11 桌面版（含 N）都是基本字型。
2. **CJK 替代順序**：Microsoft JhengHei → MingLiU（若有裝）→ Microsoft YaHei → SimSun → Yu Gothic → Malgun Gothic → 豆腐方塊（不 throw）。前兩個是繁中字形；YaHei、SimSun 碼位完整，但字形是簡中寫法。
3. **符號／emoji**：Segoe UI Symbol → Segoe UI Emoji（單色輪廓）。
4. **字型來源**：用 `IDWriteFactory6::GetSystemFontSet(FALSE)` 取家族清單與檔案路徑，不再用 `GetWindowsDirectory` 拼路徑。任何一個檔案開不起來就跳到下一層，全部失敗時退回「沒有字型也能跑」的模式（例如只畫框線），而不是 throw。
5. `mingliu.ttc` 不能當保證存在的字型。

---

## 未確認事項清單

- Windows 11 23H2／24H2／25H2 各版的字型清單差異（官方 Windows 11 清單沒有分版）。
- LTSC、Windows Server（含桌面體驗）、Server Core 的基本字型是否跟桌面版相同。
- 離線或 WSUS 管理的環境，加入繁中語言後 Hant FOD 會不會自動安裝。
- Windows 10 22H2 的 `seguiemj.ttf`（1.29）是否也是每個 base glyph 都有輪廓（本機只測了 Windows 11）。
- FreeType 不設 `FT_LOAD_COLOR` 載入 `seguiemj.ttf` 的實際結果（還沒用 FreeType 實跑）。
- DirectWrite 系統字型集包含 per-user 字型這件事，官方文件沒有明文（只有實測）。
- `HKCU\…\Fonts` 的官方地位。
- YaHei／SimSun 跟台灣標準寫法不同的具體字數。
- 執行時的記憶體內 glyph cache 在字型 FAQ 裡是否算「conversion」（FAQ 沒有明講）。
