# Settings demo

The web and material executables share navigation, settings persistence, undo and redo, About information, semantic and UIA access, and renderer recovery. Clipboard, IME and DPI checks require an idle desktop.


The appearance page has an accent-color dropdown (`appearance.accent`: default, green, orange). Each choice is a palette override list (`Ui::set_palette_overrides`) that sets `accent`, `on_accent` and `selection` for both appearances in light and dark; every choice passes the contrast rules. The choice is saved as `presentation.accent` in `settings.json`; files without that field load as the default, and "reset appearance" resets it too.

The appearance page also has the checkbox 「左右對齊文字改為靠左」 (`appearance.justify_left`). It is `PresentationSettings::justify_as_left` (WCAG 1.4.8, see "Justified text" in the top-level README): while it is on, every justified or distributed text is drawn left-aligned. It is saved as `presentation.justify_as_left` in `settings.json` (files without it load as off) and "還原預設外觀" turns it off. The About page's license text is left-aligned.

Right-clicking a row on the Items page selects it and opens a menu with Edit(E) and Delete(D) (hints Enter and Del); they run the Edit and Delete buttons own actions, so they match the shortcuts. Every text field has the built-in edit menu. Opening a menu with the real mouse, keyboard and Narrator is an author-run check.
