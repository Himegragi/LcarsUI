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

Out of scope here: double and triple click selection, growing with content, horizontal scrolling or a no-wrap mode, Tab characters, alignment and line spacing and vertical text. Styled text is the [rich text option](#rich-text-fields) below.

## Rich text fields

A multi-line field created with `rich = true` holds **plain text plus style spans** (`lcars::RichText`): bold, italic, underline and a named color. The option exists for multi-line fields only (`invalid_value` on anything else) and is fixed at creation; a plain field behaves exactly as before. Labels stay plain text.

```cpp
auto note = ui.add_text({.id = "item.note", .kind = lcars::Kind::text, .multiline = true, .visible_lines = 3, .rich = true}).value.value();
note.set_colors({{"red", {176, 0, 32}, {255, 138, 148}}});          // name, light value, dark value
note.connect_rich_source({[&] { return model; }, subscribe});        // RichTextSource: read() -> RichText
note.connect_rich_confirmed([&](const lcars::RichTextChange& c) { model = c.value; return lcars::Result{}; });
note.set_style(0, 2, {.bold = true});                                // a range of the committed text
note.toggle_style(lcars::StyleAttribute::underline);                 // what Ctrl+U does: the selection, or the typing style at a caret
note.apply_style({.color = "red"});                                  // set the selection's color (caret only: the typing style)
auto value = note.rich_value();                                      // Query<RichText>: text + spans in normal form
```

- **Value.** `RichText { std::u16string text; std::vector<StyleSpan> spans; }`, a span being `{start, length, bold, italic, underline, color}` in UTF-16 offsets. The text follows the multi-line rule (LF only) and `max_length` counts text characters only. Only styled text is listed. `normalize_rich_text` (applied to everything that enters a rich field) sorts the spans, drops empty and plain ones and merges adjacent spans of equal style, so equal content always reads back as an equal value; spans that overlap, run past the text or start/end inside a surrogate pair are `Status::invalid_value` and leave the field unchanged. The framework parses no markup language.
- **Colors.** A color is a name in the value; the field's list (`set_colors`, replaceable at run time, repainted at once) gives each name a light and a dark value, so a theme switch recolors the text. No name means the theme's normal text color. A name that is not in the list is drawn in the normal color and kept in the value (an application that removed a color, or text pasted from another application, loses nothing). A disabled field is drawn muted.
- **Keys.** Ctrl+B / Ctrl+I / Ctrl+U are taken by a focused rich field before any application shortcut on the same chord (any other field leaves them to the application). With a selection the attribute is applied to all of it unless all of it already has it, then removed. With a caret only, the next typed text changes: it ends when the caret moves or text is inserted. There is no built-in toolbar or menu and no color shortcut: an application calls `toggle_style` / `apply_style` from its own buttons.
- **Typed text** takes the style of the character before the caret, or of the one after it at the start of a paragraph (an empty paragraph is plain); a temporary typing style from Ctrl+B etc. wins. `typing_style()` / `set_typing_style()` read and set it. An IME preedit is shown in the style it will take.
- **Faces.** A family's real Bold / Italic / Bold Italic file is used when the system has one (Microsoft JhengHei Bold, Segoe UI Bold and so on); otherwise the regular face is synthesized (heavier outline and wider advance for bold, a sheared outline for italic), fallback fonts included. Wrapping and caret stops use the real widths, and the glyph cache keeps the four looks of a glyph apart. Underline is a thin rectangle under the run.
- **Clipboard.** Copy and cut write plain text (CRLF) and, beside it, the framework's private payload (`Clipboard::write_rich`; the Windows host registers the format `LcarsUI.RichText.1`). Pasting between rich fields keeps the styles; text from elsewhere is pasted as plain text in the typing style. A payload that does not belong to the plain text beside it is ignored. Plain fields only ever take the plain text. A custom `Clipboard` that does not override `write_rich` / `read_rich` carries plain text only.
- **History and data connections.** One style application is one undo entry. Rich fields connect with `connect_rich_source` / `connect_rich_immediate` / `connect_rich_confirmed` (the plain `connect_*` answer `not_supported`, and these answer `not_supported` on a plain field); a confirmation group takes them through `Ui::connect_mixed_confirmation` (a `RichTextChange` per rich field). A source value that does not normalize is ignored like any invalid source value; a source that differs only in styles counts as a different value (conflicts, `use_latest`, `reset_to_source`).
- **Semantic layer and UI Automation.** `SemanticNode::rich / styles / typing_style`; `Verb::set_style` (`StyleEdit{begin, end, change}`), `Verb::set_typing_style` (`TextStyle`) and `Verb::set_value` with a `RichText`, all admitted like the typed calls (`Operation::style`); property event `styles`; `SemanticTree::text_color(ref, name)` gives the color a name paints in right now. The text pattern reports `UIA_FontWeightAttributeId` (400 / 700), `UIA_IsItalicAttributeId`, `UIA_UnderlineStyleAttributeId` (0 / single) and `UIA_ForegroundColorAttributeId` of the current theme, `UiaGetReservedMixedAttributeValue` for a range that mixes values, and `FindAttribute` for the four attributes.

Out of scope: rich labels and single-line rich fields, per-run alignment, size or font family, strikethrough, super/subscript, links, lists, images, RTF / HTML / Markdown input or output, a built-in toolbar or context menu, free RGB colors.

## Desktop support

The settings demo includes navigation, persistence, history, diagnostics and accessibility support; see its [README](examples/settings_demo/README.md).

The native host targets Windows and OpenGL 3.3 and exposes a UI Automation provider. MCP and full grapheme and RTL support remain outside v1; see the form documentation for text and IME boundaries. Third-party sources are listed in [third_party/SOURCES.md](third_party/SOURCES.md); licenses are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Palette overrides

An application can change individual colors of the built-in appearances without writing an appearance of its own. `Ui::set_palette_overrides` takes one list for the whole UI environment; each entry names an appearance (`Appearance::name()`), a theme variant, a palette role and a value. Colors that the list does not mention keep their built-in value, and a variant that is not listed is untouched (overriding light never changes dark).

```cpp
auto green = lcars::Color{20, 110, 50};
auto result = ui.set_palette_overrides({
    {std::string(lcars::web_appearance().name()), lcars::ThemeVariant::light(), "accent", green},
    {std::string(lcars::web_appearance().name()), lcars::ThemeVariant::light(), "scrim_alpha", 140},
});
```

- Roles are the names in `Palette` (`lcars::palette_roles()`: `background`, `panel`, `surface`, `text`, `muted`, `border`, `accent`, `on_accent`, `selection`, `error`, `scrim`, plus `scrim_alpha` with an `int` value 0-255). A color added to `Palette` later becomes overridable without further changes.
- The list is replaced as a whole and takes effect on the next frame; `set_palette_overrides({})` restores the built-in colors. Overrides apply only to the built-in appearances whose module is linked; a custom `Appearance` is never affected.
- Each affected appearance and variant is checked against WCAG contrast (`text` on `background`, `panel`, `surface` and `selection`, `muted` and `error` on `surface`, `on_accent` on `accent`: 4.5:1; `accent` on `surface`: 3:1). Any failure, an unknown appearance, variant or role, a value of the wrong kind, `scrim_alpha` outside 0-255 or a repeated entry returns `Status::invalid_value` with a message naming the pair and its ratio, and the previous list stays active. `lcars::check_palette_contrast(palette)` runs the same rules so an application can pre-check a palette.
- `Ui::palette_overrides()` reads the list; `Ui::effective_palette(appearance, variant)` and `SemanticTree::effective_palette()` (read-only; the Desktop host binds its appearance, a core-only host calls `Ui::bind_appearance`) report the colors that actually paint.

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
