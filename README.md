# LcarsUI

A C++20 desktop UI framework. The public API is still evolving.

## S1 minimal edit form

[The V01 edit form](examples/edit_form/README.md) composes labels, buttons, and a single-line field with typed handles and explicit application data connections. The same form runs with Minimal Web and Material-inspired appearances.

```powershell
.\build.ps1 -Configuration Release
.\build\Release\lcars_edit_form_web.exe
.\build\Release\lcars_edit_form_material.exe
.\build.ps1 -Configuration Release -CoreOnly
```

`build.ps1` runs all CTests by default. Use `-SkipTests` for a build only or `-Tests <regex>` to select CTests. It can also be launched from Git Bash; the script prioritizes Windows `tar` over Git's GNU `tar` for configure. For the foreground platform checks, use `verify-idle.ps1` on an idle desktop with the required display scales; see its parameters for selecting runs.

The build requires Visual Studio C++ Build Tools, the Windows SDK, clang-cl, CMake and Ninja. Core-only builds omit the native host, OpenGL and font dependencies. The [independent consumer](examples/consumer/main.cpp) shows assembly, validation and layout using only public headers.

Applications retain the handles returned by `Ui::add_text`, `add_button` and `add_label`; the layout accepts those handles. A text source supplies the application-owned value and notification subscription, while a separate explicit immediate, confirmed, or confirmation-group connection supplies the write action. For example:

```cpp
std::u16string model = u"Initial";
std::function<void()> notify_source;
lcars::Ui ui;
auto name = ui.add_text({.id = "profile.name", .kind = lcars::Kind::text}).value.value();
auto save = ui.add_button({.id = "profile.save", .kind = lcars::Kind::button}).value.value();
name.connect_source({[&] { return model; }, [&](std::function<void()> notify) {
    notify_source = std::move(notify);
    return [&] { notify_source = {}; };
}});
ui.connect_confirmation(save, {name}, [&](const std::vector<lcars::TextChange>& changes) {
    model = changes[0].value;
    if (notify_source) notify_source();
    return lcars::ConfirmationResult{};
});
auto changed = name.set_value(u"Station");
auto saved = save.invoke();
auto description = name.describe();
```

Check `Result::status` and query status before using results. Read-only, disabled, expired, wrong-thread and other refusals are observable. Drafts and field history are separate from applied application data. See the consumer for complete connection setup and the [verification record](examples/edit_form/evidence/rework-verification.md) for test scope and platform limits.

## Multi-line text fields

A text field is single-line unless it is created with `multiline`. The choice is fixed at creation; everything else about the field (data connections, confirmation groups, history, semantic role `text`) is unchanged, and a single-line field behaves exactly as before.

```cpp
auto note = ui.add_text({.id = "item.note", .kind = lcars::Kind::text, .max_length = 200,
                         .multiline = true, .visible_lines = 3}).value.value();
note.set_visible_lines(5);                         // reflows at once
note.set_scroll_line(2);                           // first visible display line
auto first = note.scroll_line();                   // Query<unsigned>
ui.semantics().perform({"item.note"}, lcars::Verb::scroll, 4);  // the same operation, same Status values
```

- **Value.** A line break is one LF (`\n`) and counts as one character toward `max_length`. A value containing CR, or any other control character, is invalid wherever a value enters (initial value, source, `set_value`, semantic `set_value`). Pasted CRLF, lone CR and lone LF each become one LF; copy and cut write CRLF to the clipboard. Single-line fields still turn pasted breaks into spaces. Semantics and UI Automation report the value with its LF characters.
- **Keys.** Enter and Shift+Enter insert a line break (one undo entry each) and never reach Enter shortcuts; Ctrl+Enter does, so an application can make it "OK". Tab still moves focus. Enter while an input method is composing belongs to the input method. Up/Down move by display line (a wrapped piece counts) and keep their column; Home/End go to the line's start/end, Ctrl+Home/Ctrl+End to the text's; PageUp/PageDown move `visible_lines` lines. Shift extends the selection.
- **Layout.** The height is exactly `visible_lines` lines. Long lines wrap at the field's edge like labels; there is no horizontal scrolling. The scrollbar's width is always reserved, so the first lines never re-wrap when the scrollbar appears; the thumb shows only when the text overflows.
- **Scrolling.** The scroll position is the index of the first visible display line, so it keeps its meaning across text size and density changes (the same text stays on top). It follows the caret when the caret moves, never moves the caret, and never snaps back after the wheel or the thumb scrolled it. The wheel scrolls the field first and passes to the page at the ends. Inside an open dialog the dialog's own scrollable content (this field included) takes the wheel and the thumb; the page behind still does not. A position past the last page is `Status::invalid_value` for both entry points.
- **Pointer.** A click puts the caret on the pointed line and column; a drag selects across lines and, above or below the field, scrolls a line per step (the desktop host repeats it on a timer).
- **Layout data for application and host code.** After `arrange`, `Placement::multiline` fields carry `Placement::view` (`FieldView`: every display line with its UTF-16 range and caret stops, the first visible line, the track and thumb). `Frame::text_offset(handle, x, y)`, `Placement::caret_x/caret_y/caret_rect` and `Frame::select_to` read it. A host that can lay out again on demand registers `UiState::on_relayout` (the desktop host does) so cursor keys never see stale lines.
- **UI Automation.** The text pattern reads and moves by `TextUnit_Line` (display lines) and `TextUnit_Paragraph` (LF-separated lines); a vertical-only scroll pattern reads and sets the scroll position.

Out of scope here: double and triple click selection, growing with content, horizontal scrolling or a no-wrap mode, Tab characters, alignment and line spacing, rich text and vertical text.

## Context menus

A context menu is a short list of commands opened beside a component by the right mouse button (on release), the menu key or Shift+F10. One menu is open per window; it is drawn inside the window like a dropdown's list (flipped left and up near the window's edges, scrollable when the window is too small) and never changes which component has focus.

- **Built-in items.** Every text field has Undo(U), Redo(R), Cut(T), Copy(C), Paste(P), Delete(D) and Select All(A), which run exactly the operations of Ctrl+Z / Ctrl+Y / Ctrl+X / Ctrl+C / Ctrl+V / Delete / Ctrl+A. They disable themselves when they cannot act (nothing selected, empty clipboard, nothing to undo, read-only field).
- **Attaching a menu.** `ui.add_context_menu(component, items, mode)` works on any component. `MenuItem` has an id (not starting with `lcars.`), a label, enabled, an optional quick-key letter, a shortcut hint (display only: it registers no shortcut) and the code to run, which receives the component and, for a list, the clicked row. `MenuItem::divider()` is a separator. `MenuMode::append` (default) puts the items after the built-in ones, `replace` shows only yours, `off` disables the field's menu. The returned `ContextMenuHandle` adds, removes, updates and enables items at any time, also while the menu is open.
- **From code.** `ui.open_context_menu(component, position, row)`, `close_context_menu()`, `context_menu()` (items with their availability), `invoke_menu_item(id)`. A disabled component returns `disabled`; a background component behind a modal dialog returns `blocked_by_modal`.
- **Keyboard.** Up/Down (skipping separators, wrapping), Home/End, Enter and Space run the highlighted item; a quick-key letter runs its item, or when several share it moves to the next one. Esc, Alt, F10, a click outside or the window losing activation close the menu; the click that closes it reaches nothing underneath. Disabled items can be highlighted but not run.
- **Semantic layer and UIA.** While open, the component has a `Role::menu` child (`menu_node_id(id)`, `"<id>/menu"`) with `menu_item` and `menu_separator` children; `Verb::context_menu` opens it, `invoke` on an item runs it, `cancel` on the menu closes it. UIA sees Menu / MenuItem / Separator, `AccessKey`, `AcceleratorKey`, `IsEnabled`, the Invoke pattern, and `MenuOpened` / `MenuClosed` events.

Out of scope here: submenus, check marks and icons, menus outside the application window, an application menu bar and touch long-press.

## Desktop support

The settings demo includes navigation, persistence, history, diagnostics and accessibility support; see its [README](examples/settings_demo/README.md).

The native host targets Windows and OpenGL 3.3 and exposes a UI Automation provider. MCP and full grapheme and RTL support remain outside v1; see the form documentation for text and IME boundaries. Third-party sources are listed in [third_party/SOURCES.md](third_party/SOURCES.md); licenses are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Motion (animations)

The framework has one internal animation clock (`src/animation.hpp`). A component registers a display property
going from A to B over a duration; the host schedules the next frame only while something is still running, so an
idle UI never repaints. Today only the slider thumb uses it: arrow keys, Home/End, a track click and the release
of a drag slide the thumb over 150 ms (cubic ease-out), turning from the position currently shown when the target
changes mid-way. Values, semantics and UI Automation update at once; only the picture moves. Application code,
`set_value` (semantic or UIA), Ctrl+Z / Ctrl+Y, data-source changes and dragging itself land at once.

- `PresentationSettings::motion`: `Motion::automatic` (default) follows Windows "Show animations in Windows"
  (`SPI_GETCLIENTAREAANIMATION`, re-read on `WM_SETTINGCHANGE`); `Motion::off` never plays. There is no
  "always on". Switching it with `Ui::set_presentation` takes effect at once and lands running transitions.
- `Ui::animating()` / `SemanticTree::animating()` answer whether any transition is still running (one query,
  one admission rule). Wait for `false` before comparing screenshots.
- A minimized, cloaked or fully covered window jumps running transitions to their end and draws nothing for them.
  "Fully covered" is best effort: one visible opaque window above ours that spans the whole client area.
- A core-only host that installs no time source (`detail::UiState::animation_time`) never animates. Tests use
  that seam as a fake clock, and `set_system_animations` / `DesktopHost::system_animation_source` to simulate the
  Windows setting. Tests: `animation` (core) and `lcars_host_verify --animation-only` (native host).

## Palette overrides

An application can change individual colors of the built-in appearances without writing an appearance of its own. `Ui::set_palette_overrides` takes one list for the whole UI environment; each entry names an appearance (`Appearance::name()`), a theme variant, a palette role and a value. Colors that the list does not mention keep their built-in value, and a variant that is not listed is untouched (overriding light never changes dark).

```cpp
auto green = lcars::Color{20, 110, 50};
auto result = ui.set_palette_overrides({
    {std::string(lcars::web_appearance().name()), lcars::ThemeVariant::light(), "accent", green},
    {std::string(lcars::web_appearance().name()), lcars::ThemeVariant::light(), "scrim_alpha", 140},
});
```

- Roles are the names in `Palette` (`lcars::palette_roles()`: `background`, `panel`, `surface`, `text`, `muted`, `border`, `outline`, `accent`, `on_accent`, `selection`, `error`, `scrim`, plus `scrim_alpha` with an `int` value 0-255). A color added to `Palette` later becomes overridable without further changes.
- `outline` is the edge that identifies a control (text field box or underline, dropdown box, checkbox box, radio circle, slider track, scrollbar thumb, and a progress track that shows no value text); disabled controls draw it with `border` instead. `border` stays the light color for decorative lines (button edge, list divider, tooltip edge).
- The list is replaced as a whole and takes effect on the next frame; `set_palette_overrides({})` restores the built-in colors. Overrides apply only to the built-in appearances whose module is linked; a custom `Appearance` is never affected.
- Each affected appearance and variant is checked against WCAG contrast (`text` on `background`, `panel`, `surface` and `selection`, `muted` and `error` on `surface`, `on_accent` on `accent`: 4.5:1; `accent` and `outline` on `surface`, and `outline` on `background` and `panel`: 3:1; `border` is only a decorative divider and is not checked). Any failure, an unknown appearance, variant or role, a value of the wrong kind, `scrim_alpha` outside 0-255 or a repeated entry returns `Status::invalid_value` with a message naming the pair and its ratio, and the previous list stays active. `lcars::check_palette_contrast(palette)` runs the same rules so an application can pre-check a palette.
- `Ui::palette_overrides()` reads the list; `Ui::effective_palette(appearance, variant)` and `SemanticTree::effective_palette()` (read-only; the Desktop host binds its appearance, a core-only host calls `Ui::bind_appearance`) report the colors that actually paint.

## Focus indicator

Focus indicators (the accent ring, bar or dot that marks the focused component) are drawn only while the
user is working with the keyboard, like `:focus-visible` in browsers. Any key without Ctrl or Alt turns them on;
a pointer press anywhere in the window turns them off. Moving focus from code (`focus()`, semantic
operations, UI Automation `SetFocus`) never changes this state, and semantics and UIA always report the real
focus. Editable text fields always draw the indicator while focused. The slider marks focus with a dot left of
its title; the room for the dot is always reserved so the title never shifts.

* `PresentationSettings::focus_indicator` is `FocusIndicatorMode::automatic` (default) or `always`; switch it
  with `Ui::set_presentation`. There is no "never" mode.
* Windows "Always underline access keys and show keyboard focus" (`SPI_GETKEYBOARDCUES`) forces the indicator
  on, and a change takes effect without restarting.
* `Ui::focus_indicator_visible()` reports whether indicators are being drawn right now. Tooltips (C11) use the
  same state: a pointer-focused source shows its tooltip only after the next key press.

## Presentation verification

The presentation matrix runs in `edit_form` with fake measurement and in
`presentation_matrix_native` with shaped Windows text. Sizes are in DIP; each
is converted to physical pixels at the requested scale. Both use the same
component inventory and drivers. Native comparison uses the CPU reference and
GPU backend; failures save CPU, GPU and difference images.

On an idle author desktop, run `./verify-idle.ps1 -Only presentation`. PR01
requires Microsoft Bopomofo, Microsoft JhengHei and MingLiU; PR02 requires 100%
and 150% monitors. The report keeps PR03 and PR04 open: install a per-user font
and record its catalog entry, file path and display (PR03), then collect user
feedback and the first frame time after changing fonts (PR04). Generate the
16 V01 trial images and manifest with:

```powershell
./build/Release/lcars_host_verify.exe --trial-screenshots --evidence build/Release/evidence/presentation-trial
```

PR04 requires coordinator review and is not marked as passed by the generator.
PS10 runs automatically in the `presentation_recovery` CTest. A report with open or failed cases causes the idle
script to return a failure status.

## Using LcarsUI from another CMake project

LcarsUI ships as static libraries built from source, with no ABI guarantee. Rebuild your application when updating LcarsUI. Install both configurations into one prefix:

```powershell
.\build.ps1 -Configuration Release -Install C:\sdk\LcarsUI
.\build.ps1 -Configuration Debug -Install C:\sdk\LcarsUI
```

Then in your project:

```cmake
cmake_minimum_required(VERSION 3.25)
project(StationApp LANGUAGES CXX)
find_package(LcarsUI 0.2 REQUIRED COMPONENTS Desktop AppearanceWeb)
add_executable(station_app main.cpp)
target_link_libraries(station_app PRIVATE LcarsUI::Desktop LcarsUI::AppearanceWeb)
install(FILES "${LcarsUI_THIRD_PARTY_NOTICES_FILE}" DESTINATION .)
```

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\sdk\LcarsUI
cmake --build build
```

Run these commands in a Visual Studio x64 developer environment. Without `build.ps1`, the equivalent framework build and install for each configuration is:

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
ctest --test-dir build/Release --output-on-failure
cmake --install build/Release --prefix C:\sdk\LcarsUI
```

Repeat with `Debug` in the build directory and build type. Requirements are Windows 10 22H2 or Windows 11 23H2 or later, x64, CMake 3.25, and a C++20 or newer compiler: clang-cl or Visual Studio 2022 MSVC with a toolset at least as new as the one used to build LcarsUI. The default runtime is `/MD` (`/MDd` in Debug); consumer and framework runtime settings must match. LcarsUI itself builds with clang-cl and Ninja.

Request the components you use: `Core`, `AppearanceWeb`, `AppearanceMaterial`, `Desktop`, and `Diagnostics`. Each appearance and Desktop automatically imports Core. Desktop includes the Windows host, OpenGL, text, UI Automation, FreeType and HarfBuzz. Diagnostics is available in a full desktop build. Without `COMPONENTS`, only Core is imported. All public headers are installed regardless of the selected component; using a Desktop or Diagnostics API requires linking its corresponding target. Draw lists and the rendering backend are internal.

If you link Desktop, credit FreeType in your product documentation: show `lcars::third_party_notices()` in an About box or ship `THIRD_PARTY_NOTICES.txt`. The framework license is available as `LcarsUI_LICENSE_FILE`. Source archives and headers are vendored under `third_party/` and verified by SHA256 at configure time; no network access or package manager is required to build.

## Updating third-party dependencies

1. Replace the relevant vendored archive or Khronos header under `third_party/`, retaining the exact upstream bytes. Review the new source and license files, including any added or changed license obligations.
2. Update version, commit, URL, SHA256 and, where applicable, license-file and credit metadata in `cmake/ThirdPartyManifest.cmake`; update the matching human-readable entries in `third_party/SOURCES.md`. Preserve the binary attributes in `.gitattributes`.
3. Configure a build with the updated manifest, then regenerate the checked-in notices:

   ```powershell
   cmake --build build/Release --target update_third_party_notices
   ctest --test-dir build/Release -R third_party_notices --output-on-failure
   ```

4. Review `THIRD_PARTY_NOTICES.txt`, rebuild and run the full tests and installed-package verification. Commit the vendored files, manifest, source descriptions and regenerated notices together. CI-only tools and example-only dependencies do not belong in the shipped notices.
