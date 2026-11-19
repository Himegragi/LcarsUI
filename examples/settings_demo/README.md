# Settings demo

The web and material executables share navigation, settings persistence, undo and redo, About information and semantic access. Windows also provides UIA access and the native renderer recovery prompts. Clipboard, IME and DPI checks require an idle desktop.

## Running on Linux

Build with the [Linux native desktop configuration](../../README.md#linux-native-desktop-build), then launch `isoduo_settings_demo_web` or `isoduo_settings_demo_material` from its build directory in a Wayland or X11 session. Both appearances use `$XDG_CONFIG_HOME/Isoduo/SettingsDemo/settings.json`. If `XDG_CONFIG_HOME` is empty, unset or relative, the root is `$HOME/.config`.

Use `--settings-dir "path with spaces"` to read and save settings in a separate directory. About displays the selected file. A missing file starts with defaults. A malformed file is moved to `settings.json.bad` when possible. If a save fails, the dialog shows the reason and you can retry with your edits intact.

To copy a legacy Linux settings directory, run `python3 tools/migrate-settings.py` from the source root. It copies `LcarsUI/SettingsDemo` to `Isoduo/SettingsDemo` under the same XDG configuration root, including nested and hidden backups. The source is retained. Use `--config-root <directory>` to select a separate configuration root. Migration refuses an existing destination and leaves backup-only inputs as backups. The Windows migration command remains `tools/migrate-settings.ps1`.


The appearance page has an accent-color dropdown (`appearance.accent`: default, green, orange). Each choice is a palette override list (`Ui::setPaletteOverrides`) that sets `accent`, `on_accent` and `selection` for both appearances in light and dark; every choice passes the contrast rules. The choice is saved as `presentation.accent` in `settings.json`; files without that field load as the default, and "reset appearance" resets it too.

The appearance page also has four components for the focus ring (#90): 「焦點標示」 (`appearance.focusMode`: automatic, always, off = `FocusIndicatorMode::never`), the slider 「焦點外框粗細」 (`appearance.focusWidth`, 2-6 DIP), 「焦點外框圓角跟元件一樣」 (`appearance.focusRadiusSame`; unchecking it shows the slider 「焦點外框圓角」, `appearance.focusRadius`, 0-32 DIP) and the dropdown 「焦點外框顏色」 (`appearance.focusColor`: default follows the accent, green and orange override the palette role `focus`, each passing the 3:1 rule on every surface). With the indicator off the other three are disabled and keep their values. They are saved in `presentation` as `focusIndicator`, `focusRingWidth` / `focusRingRadius` (a number, or `null` for the appearance's / component's own) and `focusColor`; files without them load the defaults, and an unknown or out-of-range value makes the whole file corrupt, as for the accent. 「還原預設外觀」 resets them too.

The appearance page also has the checkbox 「左右對齊文字改為靠左」 (`appearance.justifyLeft`). It is `PresentationSettings::justifyAsLeft` (WCAG 1.4.8, see "Justified text" in the top-level README): while it is on, every justified or distributed text is drawn left-aligned. It is saved as `presentation.justifyAsLeft` in `settings.json` (files without it load as off) and "還原預設外觀" turns it off. The About page's license text is left-aligned.

Right-clicking a row on the Items page selects it and opens a menu with Edit(E) and Delete(D) (hints Enter and Del); they run the Edit and Delete buttons own actions, so they match the shortcuts. Every text field has the built-in edit menu. Opening a menu with the real mouse, keyboard and Narrator is an author-run check.
## Vertical label trial (author-run)

The About page shows a vertical-writing label (`about.vertical`, the sentence 「Isoduo 設定管理示範（第 12 版），謹此致謝。」). Automated checks cover the geometry, the pixels of every piece in three JhengHei weights, the CPU/OpenGL agreement and the UIA name and box; looking at it on the author's machine is the author's confirmation. In both executables (`isoduo_settings_demo_web`, `isoduo_settings_demo_material`), light and dark theme, open 關於 and check:

1. Punctuation: the comma and the period sit in the middle of their cells; the parentheses face up and down.
2. Latin and digits: `Isoduo` is turned clockwise and reads downward; `12` sits side by side in one cell, no wider than a character.
3. Columns run from right to left; the first column starts with `Isoduo`.
4. Change the text size and the display scale (move the window between monitors, or change the scale in Windows settings): the columns reflow and nothing is cut off; when the window is narrow the label scrolls sideways (wheel, Shift+wheel, thumb, Left/Right after Tab focus).
