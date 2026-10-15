# LcarsUI

C++26 桌面 UI 框架的第一個可執行 MVP，以 Windows 設定管理程式驗證核心、元件外觀、文字與應用邏輯的分離。現階段介面仍可調整，尚未宣告穩定 API 或完成整份架構決策地圖。

## 執行

需要已安裝的 Visual Studio C++ Build Tools／Windows SDK、clang-cl、CMake 與 Ninja。建置腳本只在自己的程序匯入工具環境，沒有安裝工具或使用套件管理器，也沒有 configure-time 下載。

在 PowerShell 執行：

~~~powershell
.\build.ps1 -Configuration Release -Smoke
.\build\Release\lcars_settings_web.exe
~~~

另一套外觀使用同一份應用與宿主來源碼，僅在組裝時連結不同外觀：

~~~powershell
.\build\Release\lcars_settings_material.exe
~~~

設定預設儲存在 %LOCALAPPDATA%\LcarsUI\settings.conf。兩個版本共用這個位置；開啟程式不會自動寫入。可使用獨立檔案試用：

~~~powershell
.\build\Release\lcars_settings_web.exe --data .\build\my-settings.conf
~~~

若同時開啟多個實例，最後一次成功儲存的內容生效；MVP 沒有跨程序變更合併。儲存使用同目錄、獨占建立的 .tmp 檔案，完成寫入與 flush 後才替換目的檔案。若先前異常結束留下 .tmp，程式會明確回報失敗，不會自行刪除它。此機制不是所有檔案系統及斷電情境下的交易保證。

## 已提供的操作

- 一般設定、使用偏好、設定範本三個頁面；以設定名稱搜尋所有頁面，中文採直接比對，英文忽略 ASCII 大小寫。
- 自繪單行文字輸入、游標與選取、滑鼠拖曳、複製／剪下／貼上、中文 IMM 組字接點。
- 顯示名稱與工作區驗證、UTF-8 設定檔保存、下次啟動載入、錯誤訊息、未儲存狀態。
- 最多 100 筆設定復原／重做。還原預設、放棄變更與套用範本都可復原；搜尋與頁面切換不列入設定歷史。
- 視窗內確認對話框；滑鼠、鍵盤及語意寫值／命令均遵守 modal 限制。未儲存就關閉視窗時，確認放棄後才關閉。
- 正黑體／細明體選擇、16–26 的文字大小，重新量測與 DPI 縮放。字型使用 Windows 安裝的檔案，沒有隨專案散布。
- Minimal Web 的標籤在輸入框外；Material-inspired 的標籤在填色欄位內並帶底線。兩者共用行為與資料。

「自動儲存偏好」及「顯示通知偏好」是設定管理情境的示範值，沒有背景自動存檔或發送系統通知。文字大小目前套用到編輯欄位與偏好列；標題、導覽與工具列保留自己的排版大小。

| 按鍵 | 操作 |
| --- | --- |
| Tab / Shift+Tab | 在可用元件間移動焦點；對話框開啟時焦點限制於其中 |
| Ctrl+S / Enter（編輯欄位） | 儲存設定 |
| Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z | 設定復原／重做 |
| Ctrl+A / C / X / V | 文字全選／複製／剪下／貼上 |
| 方向鍵、Shift+方向鍵、Home、End | 游標與選取 |
| Enter / Space（按鈕） | 執行動作 |
| Esc | 取消組字或對話框；無對話框時清除搜尋 |
| 滑鼠滾輪 | 捲動內容區 |

## 程式結構

| 位置 / target | 責任 |
| --- | --- |
| include/lcars/core.hpp / lcars_core | 穩定 ID、文字工作階段、語意查詢／寫值／命令、可用狀態與 UI 執行緒檢查；不依賴 Win32、OpenGL、字型庫或外觀 |
| include/lcars/appearance.hpp | 色彩角色、幾何值與欄位部件契約，幾何使用 DIP |
| lcars_appearance_web / lcars_appearance_material | 各自獨立的外觀實作，不依賴應用或 GPU |
| src/text.* / src/renderer_opengl.* / lcars_desktop | HarfBuzz＋FreeType 文字與 CPU raster image → OpenGL 3.3 core 呈現 |
| examples/settings/settings.* / lcars_settings | C++ 宣告、設定驗證、歷史、持久化與應用命令 |
| examples/settings/desktop.* / lcars_settings_host | 示範應用的頁面版面、Win32 輸入及繪製協作，設定專用 ID 留在示範程式 |
| examples/settings/main.cpp | 啟動組裝；兩個執行檔各自僅連結一套外觀 |

核心的最小語意操作：

~~~cpp
auto* name = app.ui.find("profile.name");
auto changed = app.ui.set_value("profile.name", u"新的工作站");
auto saved = app.ui.invoke("save");
~~~

使用端須檢查 Result。不存在、唯讀、停用、組字中、modal 阻擋、錯誤執行緒及失敗會有不同狀態。元件讀取、編輯工作階段與 owner-side 設定方法都限於建立 Ui 的執行緒；set_value／invoke 額外明確拒絕錯誤執行緒。變更回呼內的新語意寫入及命令重入先回報 busy；這是 MVP 同步安排，尚未實作 ADR 所描述的完整批次通知排程。

外觀可安排 label／surface／editor 的位置，編輯工作階段、文字量測與行為仍由共用層處理。這個有限契約尚不是通用模板引擎或完整第三方元件擴充系統。所有 targets 目前為靜態函式庫，沒有 ABI 或動態插件承諾。

## 驗證

~~~powershell
.\build.ps1 -Configuration Debug -Smoke
.\build.ps1 -Configuration Release -Smoke
.\build.ps1 -Configuration Release -CoreOnly
~~~

-CoreOnly 完全跳過 OpenGL、Win32 視窗宿主及字型相依的配置／建置。一般 CMake 使用端也可設定 -DLCARS_BUILD_DESKTOP=OFF。C++26 模式設為 required，不默默退回較舊語言模式；本次沒有使用 reflection 等尚未驗證的特性。

測試分為：

- lcars_tests／CTest：語意限制、組字狀態、UTF-16、設定復原／重做、儲存後重新載入、格式錯誤、暫存檔衝突、I/O 失敗與外觀欄位幾何。
- --smoke --data <file> --screenshot <file.bmp>：不取得前景焦點的真實 Win32／OpenGL 視窗；程式送入文字訊息並驗證共用模型、復原、代理對、導覽、modal、搜尋、字型與縮小視窗。
- GPU readback：逐 RGB channel 與 CPU raster image 比對，允許差異最多 1；保存一般頁、對話框、使用偏好、字型、搜尋與縮小視窗畫面於 build/Debug/smoke 或 build/Release/smoke。

本機已建置與驗證的組合：clang-cl 22.1.8、Visual Studio 18 Build Tools、Windows SDK 10.0.26100.0、NVIDIA RTX 3060 的 OpenGL 3.3 core。這些測試不取代使用者對新整合版本的實際輸入法及跨螢幕操作驗收。

## 本次 MVP 的界線

- 文字為單行 LTR，繁中與 Latin 混排使用系統字型 fallback。游標以 HarfBuzz cluster 為邊界；尚非完整 UAX #29 grapheme、RTL、多行或彩色 emoji。無字形以缺字標記顯示；資料能保留不代表字型能畫出該字。
- IMM 接點已整合；沒有自訂 TSF text store。原型的 UIA provider 尚未移入此多元件宿主，**本版尚未提供 Windows UIA／螢幕閱讀器整合**。先前原型的驗收不直接轉算為本版通過。
- 一般更新按需繪製，不設持續重繪迴圈；目前每次更新仍完整 CPU rasterize 並上傳整張 texture，沒有 glyph atlas、局部重繪或效能承諾。
- 同步、單視窗、固定元件集合；沒有虛擬化、動態元件樹、完整非同步命令／交接／診斷排程、宿主嵌入、第二 GPU 後端、MCP、外部 UI 描述檔或執行中完整換膚。
- Windows 10、其他 GPU／驅動與完整縮放矩陣尚未驗證；任意已安裝字型的選擇器也留待後續。

既有需求與研究仍以 [架構決策地圖](https://github.com/Himegragi/LcarsUI/issues/1)、CONTEXT.md 及 docs/adr 為準。本 MVP 未將尚未實作的能力標成已完成，也未關閉既有議題。依賴來源、鎖版與授權位置見 [third_party/SOURCES.md](third_party/SOURCES.md)。
