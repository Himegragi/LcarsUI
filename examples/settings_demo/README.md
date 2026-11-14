# Settings demo

The web and material executables share navigation, settings persistence, undo and redo, About information, semantic and UIA access, and renderer recovery. Clipboard, IME and DPI checks require an idle desktop.


The appearance page has an accent-color dropdown (`appearance.accent`: default, green, orange). Each choice is a palette override list (`Ui::set_palette_overrides`) that sets `accent`, `on_accent` and `selection` for both appearances in light and dark; every choice passes the contrast rules. The choice is saved as `presentation.accent` in `settings.json`; files without that field load as the default, and "reset appearance" resets it too.

The appearance page also has four components for the focus ring (#90): 「焦點標示」 (`appearance.focus_mode`: automatic, always, off = `FocusIndicatorMode::never`), the slider 「焦點外框粗細」 (`appearance.focus_width`, 2-6 DIP), 「焦點外框圓角跟元件一樣」 (`appearance.focus_radius_same`; unchecking it shows the slider 「焦點外框圓角」, `appearance.focus_radius`, 0-32 DIP) and the dropdown 「焦點外框顏色」 (`appearance.focus_color`: default follows the accent, green and orange override the palette role `focus`, each passing the 3:1 rule on every surface). With the indicator off the other three are disabled and keep their values. They are saved in `presentation` as `focus_indicator`, `focus_ring_width` / `focus_ring_radius` (a number, or `null` for the appearance's / component's own) and `focus_color`; files without them load the defaults, and an unknown or out-of-range value makes the whole file corrupt, as for the accent. 「還原預設外觀」 resets them too.

The appearance page also has the checkbox 「左右對齊文字改為靠左」 (`appearance.justify_left`). It is `PresentationSettings::justify_as_left` (WCAG 1.4.8, see "Justified text" in the top-level README): while it is on, every justified or distributed text is drawn left-aligned. It is saved as `presentation.justify_as_left` in `settings.json` (files without it load as off) and "還原預設外觀" turns it off. The About page's license text is left-aligned.

Right-clicking a row on the Items page selects it and opens a menu with Edit(E) and Delete(D) (hints Enter and Del); they run the Edit and Delete buttons own actions, so they match the shortcuts. Every text field has the built-in edit menu. Opening a menu with the real mouse, keyboard and Narrator is an author-run check.
## Vertical label trial (author-run)

The About page shows a vertical-writing label (`about.vertical`, the sentence 「LcarsUI 設定管理示範（第 12 版），謹此致謝。」). Automated checks cover the geometry, the pixels of every piece in three JhengHei weights, the CPU/OpenGL agreement and the UIA name and box; looking at it on the author's machine is the author's confirmation. In both executables (`lcars_settings_demo_web`, `lcars_settings_demo_material`), light and dark theme, open 關於 and check:

1. Punctuation: the comma and the period sit in the middle of their cells; the parentheses face up and down.
2. Latin and digits: `LcarsUI` is turned clockwise and reads downward; `12` sits side by side in one cell, no wider than a character.
3. Columns run from right to left; the first column starts with `LcarsUI`.
4. Change the text size and the display scale (move the window between monitors, or change the scale in Windows settings): the columns reflow and nothing is cut off; when the window is narrow the label scrolls sideways (wheel, Shift+wheel, thumb, Left/Right after Tab focus).
