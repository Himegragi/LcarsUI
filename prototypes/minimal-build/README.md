# LcarsUI 最小建置原型（可丟棄）

本原型回答：不用 package manager，CMake + C++26 能否建置獨立函式庫及 Windows OpenGL 3.3 core 程式？
這不是正式 UI 框架、外觀設計或效能基準。

## 一個指令執行

在此 worktree 根目錄執行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/minimal-build/run.ps1
```

預設使用 clang-cl，初始化本機 Visual Studio C++ 開發環境，優先使用 VS 隨附 CMake。
自動模式顯示短暫視窗，完成渲染／resize／兩次 context 生命週期檢查後結束；結果在終端，影像在 build 子目錄。
所有第三方標頭已附在 third_party 並由 CMake 驗 SHA256；configure/build 不需網路或套件管理器。

人工拖動視窗與檢視畫面：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File prototypes/minimal-build/run.ps1 -Interactive
```

按 Esc 或視窗關閉按鈕結束。另有 `-CoreOnly`（不建立 OpenGL target）、`-Compiler msvc`（重現當前不支援的 CMake 設定），以及 `-CMakeExecutable <path>`。
`-BuildType Release` 使用獨立的 Release 建置目錄；`-CoreOnly -VerifyCoreIsolation` 會複製只有核心及 consumer 的原始碼，驗證完全不提供 GL 程式碼及 Khronos 標頭仍能建置。
`-MsvcLanguageProbe` 單獨用 MSVC `/std:c++latest` 編譯相同 pack indexing 程式，僅用來定位 compiler／CMake 差異，不會放寬正常建置的 CXX26 要求。
腳本以本機 LLVM 預設安裝位置為候選，尚未做通用工具探索與部署。

## 2026-10-08 實測

| 項目 | 結果 |
| --- | --- |
| 系統 | Windows 11 Pro N，10.0.26200，x64 |
| 通過的組合 | clang-cl 22.1.8 + CMake 4.3.1-msvc1 + Ninja + Windows SDK 10.0.26100.0；Debug、動態 Debug CRT `/MDd` |
| 另一已試組合 | clang-cl 22.1.8 + CMake 4.4.0-rc2 亦通過；預設採前述隨 VS 附帶的非 RC 組合 |
| C++26 證據 | `CXX_STANDARD 26` + REQUIRED；實際 `-std:c++latest`，`__cplusplus=202400`，`__cpp_pack_indexing=202311`；pack indexing 的 constexpr 結果與 library consumer 均通過 |
| MSVC 結果 | MSVC 19.51.36257.0 與兩個 CMake 組合均在 generate 階段拒絕 `CXX26`；未降低標準、未手工覆寫 CMake compiler 能力 |
| GPU | NVIDIA GeForce RTX 3060，driver 610.62 |
| Context | 實際回報 OpenGL 3.3.0 core profile、GLSL 3.30 |
| 顯示環境 | 視窗 DPI 144（150%）；不代表已測跨 DPI 切換 |
| 核心獨立建置 | `PROBE_BUILD_OPENGL=OFF`：只建立 core library + consumer；不連結 opengl32 |
| Release 補測 | clang-cl／CMake 4.3.1-msvc1 完整 GPU 探針及 Core-only 均通過；`/O2`、`/MD`，不依赖 Debug CRT |
| 原始碼隔離補測 | 只有 CMakeLists、core.hpp、core.cpp、core_consumer.cpp 的目錄，無 GL source／Khronos headers，Release build/run 通過；PE imports 無 opengl32 |
| MSVC 直接特性探針 | `/std:c++latest /Zc:__cplusplus` 仍在 `__cpp_pack_indexing` 檢查失敗（C1189，exit 2），未通過此原型的特性契約 |

MSVC 的結果只證明本機 CMake/MSVC 組合無法表達此標準要求，不能推論 MSVC 所有 C++26 特性都不支援。
後續直接編譯另證明 MSVC 未宣告本探針要求的 pack indexing feature macro；因此不僅有 CMake 對應問題，原型特性檢查本身也未通過。未據此判斷全部 C++26 功能。
clang 通過一個語言特性，不表示全部 C++26 或新增 STL 功能都可用。

## GPU 檢查做了什麼

1. Win32 視窗與 WGL bootstrap，要求 3.3 core；查驗 version/profile，集中載入並檢查函式指標。
2. 編譯 GLSL 330 vertex/fragment shaders，建立 VAO 與 2×2 RGBA 紋理。
3. 藍底上畫半透明橘色紋理，用 scissor 限定中央矩形。
4. `glReadPixels` 驗證左右裁切區保持背景色，中間符合 alpha 混色；容許每色通道 3 的量化誤差。
5. 客戶区由 938×584 調為 618×424，重新渲染及比對像素。
6. 刪除 GL 資源、解除 current context、刪除 WGL context、釋放 DC／視窗；重建第二次並重跑，檢查錯誤與返回值。

實測裁切外 RGB `(25,51,76)`，中央 RGB `(140,90,54)`。下圖為 GL back buffer 直接讀回，不是桌面截圖。

![initial GL framebuffer](evidence/probe-initial.bmp)

## 證據

- `evidence/clang-cmake431.log`：非 RC CMake 的乾淨完整建置與 GPU 檢查。
- `evidence/clang-core-cmake431.log`：關閉 OpenGL 的獨立建置。
- `evidence/msvc-cmake431-failure.log`：同版本 CMake 的 MSVC 配置失敗。
- 另保留 4.4.0-rc2 的比較日誌，以及初始／resize 後影像。
- `evidence/clang-release.log`、`clang-core-release.log`：Release 補測；`msvc-direct-language.log`：直接 compiler 探針。
- `evidence/core-source-isolation.log`、`core-imports.log`：移除所有 GL 原始碼及標頭後的 Core consumer 建置與 PE imports。
- `third_party/SOURCES.md`：固定來源、commit、SHA256；上游授權保留於標頭。

## 對下一步的建議與限制

建議後續原型先用這套已測的 clang-cl／CMake 組合，保留 C++26 REQUIRED 與實際特性探針。
這是已驗證組合，不是宣稱最低 compiler／SDK 版本。OpenGL 3.3 core 足以通過這個有限探針，尚不能定為全框架的最低 GPU 保證。

尚未驗證：Windows 10 實機、其他 GPU/driver、乾淨電腦部署、跨螢幕 DPI、context 遺失、鍵盤輸入/IME/UIA、HarfBuzz/FreeType、字型排版、外觀替換、繪圖效能或 API 完整性。渲染診斷設計依使用者要求留待後續訪談，本輪沒有新增診斷系統。
使用者檢視與正式基線決策記於 GitHub 的「驗證 CMake、C++26 與 OpenGL 最小建置」議題。
簡短的本地 GL symbol table 僅供探針；不將其直接定為正式框架 loader。
