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
