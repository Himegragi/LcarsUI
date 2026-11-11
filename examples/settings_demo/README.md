# Settings demo

The web and material executables share navigation, settings persistence, undo and redo, About information, semantic and UIA access, and renderer recovery. Clipboard, IME and DPI checks require an idle desktop.


The appearance page has an accent-color dropdown (`appearance.accent`: default, green, orange). Each choice is a palette override list (`Ui::set_palette_overrides`) that sets `accent`, `on_accent` and `selection` for both appearances in light and dark; every choice passes the contrast rules. The choice is saved as `presentation.accent` in `settings.json`; files without that field load as the default, and "reset appearance" resets it too.

Right-clicking a row on the Items page selects it and opens a menu with Edit(E) and Delete(D) (hints Enter and Del); they run the Edit and Delete buttons own actions, so they match the shortcuts. Every text field has the built-in edit menu. Opening a menu with the real mouse, keyboard and Narrator is an author-run check.
## Vertical label trial (author-run)

The About page shows a vertical-writing label (`about.vertical`, the sentence 「LcarsUI 設定管理示範（第 12 版），謹此致謝。」). Automated checks cover the geometry, the pixels of every piece in three JhengHei weights, the CPU/OpenGL agreement and the UIA name and box; looking at it on the author's machine is the author's confirmation. In both executables (`lcars_settings_demo_web`, `lcars_settings_demo_material`), light and dark theme, open 關於 and check:

1. Punctuation: the comma and the period sit in the middle of their cells; the parentheses face up and down.
2. Latin and digits: `LcarsUI` is turned clockwise and reads downward; `12` sits side by side in one cell, no wider than a character.
3. Columns run from right to left; the first column starts with `LcarsUI`.
4. Change the text size and the display scale (move the window between monitors, or change the scale in Windows settings): the columns reflow and nothing is cut off; when the window is narrow the label scrolls sideways (wheel, Shift+wheel, thumb, Left/Right after Tab focus).
