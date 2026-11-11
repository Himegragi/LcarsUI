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

## Vertical labels (display only)

A label can be set vertically: text runs top to bottom, columns from right to left, laid out by the Taiwan Ministry of Education rules. Editable vertical text and read-only text fields are not covered.

```cpp
auto note = ui.add_label({.id = "about.note", .kind = lcars::Kind::label, .value = u"LcarsUI 設定管理示範（第 12 版），謹此致謝。",
                          .writing_mode = lcars::WritingMode::vertical}).value.value();
auto page = lcars::layout::column({lcars::layout::vertical_text(note, 200)});  // the height is yours, in DIP
note.set_writing_mode(lcars::WritingMode::horizontal);     // changes at run time; the next arrangement follows
note.set_vertical_digits(2);                               // 0-3 (default 3); anything else is Status::invalid_value
ui.semantics().perform({"about.note"}, lcars::Verb::set_writing_mode, lcars::WritingMode::vertical);  // same checks, same Status
```

- **Size.** The height comes from `layout::vertical_text(label, height)`; characters fill a column to that height, then continue in the next column to the left, and a line break (`\n`) starts a new column. The width is the number of columns times the column width (the line height of the text). `vertical_text(label, 0)` never wraps.
- **Too many columns.** When the columns are wider than the space the layout offers, the label scrolls sideways: it starts at the right edge (the start of the text), the wheel (also with Shift) moves toward the end of the text, the horizontal wheel and the bar's thumb move both ways, and a scrolling label takes focus, where Left and Right (and Home and End) scroll it. Nothing is cut off beyond reach.
- **Characters** follow UAX #50 (data in `src/vertical_orientation_data.inc`, source and version in `THIRD_PARTY_NOTICES.txt`): CJK, full-width letters and emoji stand upright; Latin words and sentences, single letters and runs of four or more digits turn 90 degrees clockwise; two to three half-width digits (the limit set by `vertical_digits`) are set side by side in one cell, squeezed to the cell width; one digit stands upright.
- **Punctuation** uses the Taiwan placements: the full-width comma, period, enumeration comma, semicolon, colon, exclamation and question marks sit in the middle of the cell, drawn from the horizontal glyph (the font's vertical alternates differ between weights); quotes and brackets face up and down (the font's vertical form, else a quarter turn); dashes and ellipses are centered lines over two cells; middle dots sit in the middle of the cell. Mainland China and Japanese placements, avoiding punctuation at the start or end of a column, and underline-style marks are not provided.
- **Accessibility.** The name is the label text in reading order, the bounding box is the drawn box; no text pattern is added.
- **Hooks for later work.** Alignment along the column, line spacing and paragraph spacing are read in one place (`vertical_params` in `src/vertical.hpp`); the defaults are top, 1.0 and 0.

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
