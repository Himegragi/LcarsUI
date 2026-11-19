# Isoduo

A C++20 desktop UI framework. The public API is still evolving.

Code: [isoduo/Isoduo](https://github.com/isoduo/Isoduo).
Private tracker: [isoduo/Isoduo-issues](https://github.com/isoduo/Isoduo-issues).
Historical numbered issue links retain their original destinations.

This project was renamed from LcarsUI to Isoduo. Consumers now use `<isoduo/...>`,
`isoduo::`, `find_package(Isoduo)` and `Isoduo::` CMake targets; build options use
`ISODUO_*`. Configure a fresh build directory and install into a clean prefix.
The settings-demo data directory is now `%APPDATA%/Isoduo/SettingsDemo`; migrate
existing settings with `powershell -NoProfile -File tools/migrate-settings.ps1` before first launch.
The script also copies a legacy settings directory containing only backups and
keeps each backup at its original relative path.
Close both applications first. The script copies `%APPDATA%/LcarsUI/SettingsDemo` only when
`%APPDATA%/Isoduo/SettingsDemo` does not exist, preserves the old directory as a backup,
and refuses to merge or overwrite an existing destination. Optional `-AppDataRoot` selects
a different roaming profile root. Old settings headers, rich clipboard payloads and
diagnostic exports remain readable; new output uses `isoduo-settings-demo`,
`Isoduo.RichText.1` and `ISODUODIAG 1 0`. Old menu command IDs have changed to `isoduo.*`.

## Linux core build and tests

With CMake 3.25 or newer, Ninja, and Clang with C++20 standard-library headers and a linker already on the Linux system, run from the checkout:

```sh
sh tools/test-linux-core.sh
```

The script configures a Release build with the desktop host and installation disabled, builds against the checked-in sources, and runs the headless CTest suite. Each run leaves a fresh directory under `${TMPDIR:-/tmp}` with `ctest-tests.json` and `ctest-results.xml`. Set `CC` and `CXX` to select different compiler executables, or `CMAKE_BUILD_PARALLEL_LEVEL` to set build parallelism.

On Linux or WSL with unprivileged user and network namespaces enabled, configure, build and test without network access using:

```sh
unshare --user --map-current-user --net env -u DISPLAY -u WAYLAND_DISPLAY sh tools/test-linux-core.sh
```

`word_select` exercises the portable selection and fallback behavior. `word_select_icu`
uses the installed system dictionary and skips when it is unavailable. To require
that the real dictionary cases run, invoke `isoduo_word_select_tests --require-icu`.
Linux Core builds and installed consumers need only the platform dynamic loader.
Dictionary mode optionally loads the system ICU runtime (`libicuuc.so.60` through
`libicuuc.so.99`, or `libicuuc.so` with unversioned exports) and its CJK break data.
Ubuntu 26.04 provides these through `libicu78`. Core needs no ICU development
package, and can build and run offline with or without the runtime.

## Linux native desktop build

Install CMake, Ninja, Clang or GCC with C++20 support, pkg-config, and development
packages for EGL 1.5, X11, X11-XCB, XCB, Wayland 1.22 or newer, xkbcommon,
Fontconfig, GIO, and Pango. The build also needs `wayland-scanner` and the protocol XML
from `wayland-protocols` 1.31 or newer. On Ubuntu these packages are `libegl-dev`,
`libx11-dev`, `libx11-xcb-dev`, `libxcb1-dev`, `libwayland-dev`,
`libwayland-bin`, `wayland-protocols`, `libxkbcommon-dev`,
`libfontconfig-dev`, `libglib2.0-dev`, and `libpango1.0-dev`.
Install `zenity` to use the Linux settings demo's renderer recovery dialogs.

The Linux host reads the desktop's animation preference at creation. GNOME
provides enable-animations and reduced-motion settings. Xfce can provide the
optional `Gtk/EnableAnimations` setting through Xfconf. When the preference is
unavailable, animations stay enabled. Focus indicators follow Isoduo's automatic
rules unless the application sets a different focus indicator mode.

```sh
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DISODUO_BUILD_DESKTOP=ON
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
./build-linux/isoduo_edit_form_web
```

`DesktopConfig::backend` defaults to `DesktopBackend::automatic`. On Linux this
uses `XDG_SESSION_TYPE` when it names Wayland or X11, then checks for a configured
Wayland connection before checking `DISPLAY`. An explicit `wayland` or `x11`
selects that native connection. If it fails, `StartupFailure::backend` identifies
the selected backend. Linux `nativeWindow()` returns a borrowed tagged reference:
a `wl_display*` and `wl_surface*` on Wayland, or a `Display*` and XID on X11.
It is valid while the native window lives, including across a renderer rebuild.

Run the following probes in a GNOME Wayland session and an Xfce Xorg session,
using the corresponding `wayland` or `x11` argument as `BACKEND`.

- `isoduo_linux_desktop_native BACKEND close`: record context capabilities and
  native window identity. Request a close, activate the button in the same
  window, then request a second close and check the exit status.
- `isoduo_linux_desktop_native BACKEND program`: follow the same steps to
  exercise a programmatic close inside the second close callback.
- `isoduo_linux_desktop_native BACKEND wrapper`: activate the button and close
  the window created by `runDesktop`.
- `isoduo_linux_desktop_native BACKEND faults`: exercise structured startup
  failures, optional context capabilities, wrong-thread close and explicit
  renderer rebuild. Use a Wayland client trace for the surface commit and
  `xprop -id XID` for the X11 window.

After changes to system font layout or desktop preference acquisition, check
both native sessions with an installed family that has real bold and italic
faces and a regular-only font in the user's font directory. Check mixed-script
fallback, missing-family and missing-glyph diagnostics, and the session's
animation setting or its enabled default. The headless `linux_fonts` and
`linux_preferences` tests cover the shared font and settings fixtures.

## S1 minimal edit form

[The V01 edit form](examples/edit_form/README.md) composes labels, buttons, and a single-line field with typed handles and explicit application data connections. The same form runs with Minimal Web and Material-inspired appearances.

```powershell
.\build.ps1 -Configuration Release
.\build\Release\isoduo_edit_form_web.exe
.\build\Release\isoduo_edit_form_material.exe
.\build.ps1 -Configuration Release -CoreOnly
```

`build.ps1` runs all CTests by default. Use `-SkipTests` for a build only or `-Tests <regex>` to select CTests. It can also be launched from Git Bash; the script prioritizes Windows `tar` over Git's GNU `tar` for configure. For the foreground platform checks, use `verify-idle.ps1` on an idle desktop with the required display scales; see its parameters for selecting runs.

The Windows build requires Visual Studio C++ Build Tools, the Windows SDK, clang-cl, CMake and Ninja. Core-only builds omit the native host, OpenGL and font dependencies. The [independent consumer](examples/consumer/main.cpp) shows assembly, validation and layout using only public headers.

Applications retain the handles returned by `Ui::addTextField`, `addButton` and `addLabel`; the layout accepts those handles. Each `Ui::addX` makes a component of its own type (`ComponentKind`), so the `ComponentConfig` it takes names none: write `ui.addButton({.id = "profile.save"})`. A read source (`isoduo::Source<T>`, here `Source<std::u16string>`) supplies the application-owned value and notification subscription, while a separate explicit immediate, confirmed, or confirmation-group connection supplies the write action. For example:

```cpp
std::u16string model = u"Initial";
std::function<void()> notify_source;
isoduo::Ui ui;
auto name = ui.addTextField({.id = "profile.name"}).value.value();
auto save = ui.addButton({.id = "profile.save"}).value.value();
name.connectSource({[&] { return model; }, [&](std::function<void()> notify) {
    notify_source = std::move(notify);
    return [&] { notify_source = {}; };
}});
ui.connectConfirmation(save, {name}, [&](const std::vector<isoduo::ConfirmFieldChange>& changes) {
    model = std::get<isoduo::Change<isoduo::TextFieldHandle>>(changes[0]).value;
    if (notify_source) notify_source();
    return isoduo::ConfirmationResult{};
});
auto changed = name.setValue(u"Station");
auto saved = save.invoke();
auto description = name.describe();
```

Check `Result::status` and query status before using results. Read-only, disabled, expired, wrong-thread and other refusals are observable. Drafts and field history are separate from applied application data. See the consumer for complete connection setup and the [verification record](examples/edit_form/evidence/rework-verification.md) for test scope and platform limits.

A list keeps no draft, so it only writes immediately, and its write gets a change record like every other field: `value` is the newly selected item, `origin` the item the source held just before. When `setItems` drops the selected item (leaves it out, or keeps it but disabled), the write gets no value and that item as `origin`. A label reads its text from a read function, or from a full read source when background work changes the text, so that a notification repaints it:

```cpp
std::optional<isoduo::OptionValue> selection;
std::function<void()> selectionChanged;
list.connectSource({[&] { return selection; }, [&](std::function<void()> notify) {
    selectionChanged = std::move(notify);
    return [&] { selectionChanged = {}; };
}});
list.connectImmediate([&](const isoduo::Change<isoduo::ListHandle>& change) {
    selection = change.value;  // change.origin: the selection before
    if (selectionChanged) selectionChanged();
    return isoduo::Result{};
});
title.connectSource([&] { return u"Templates"; });                      // text changes only through the screen
clock.connectSource({[&] { return now_text(); }, subscribe_to_clock});  // a notification repaints
```

Every handle kind (`TextFieldHandle`, `ButtonHandle`, ..., `DialogHandle`, `TooltipHandle`, `ContextMenuHandle`) compares with `==` against handles of its own kind: two handles are equal when they control the same thing now. Two default-constructed handles are equal and differ from every real one; a button removed and created again under the same id gives a different handle (the old one keeps answering `notFound`); the same id in another `Ui` is a different handle; a context menu attached again to a recreated target is the same handle, since the old handle controls the new attachment. Comparing handles of two kinds (`TextFieldHandle == CheckboxHandle`) does not compile, and `ComponentHandle` and `ConfirmField` compare through their alternatives. Handles have no hash.

Application code can permanently remove a label, text field, button, checkbox, radio group, slider, progress display,
dropdown or list with `handle.remove()`. Acceptance immediately makes every operation on that instance answer
`notFound` and ignores source notifications; the next safe point cancels its subscription and releases its state.
Columns, rows and grids collapse its space and gaps. Reusing its id creates a new instance; old handles and saved source
callbacks stay invalid. Removal has no semantic verb, and tabs, navigation lists and overlays have no removal entry point.

Removing a field discards its draft and IME preedit, skips its `onBlur`, and still reports the focus move to the host.
A confirmation group submits only its remaining fields; removing the last field releases the group action and leaves the
button's enabled state unchanged, ready for `bind` or another group. Dialog members, initial/fallback focus targets and
dialog name labels refuse removal with `invalidValue`. Removing a name label restores its target's fallback name with
one semantic name-change event. Removing its target leaves the label on screen, and the label names a new component
created under the same id.

## Component and semantic snapshots

`handle.describe()` returns a `ComponentSnapshot` for application code; `ui.semantics().get(ref)` returns a
`NodeSnapshot` for accessibility, tests and AI operations. Both are copies of the state at query time. Their common
fields have the same meaning and values for a component, including the component-level `verbs` (`Verb` is the only
public action vocabulary). Items stay in `ComponentSnapshot::items` and in semantic child nodes identified by
`{component id, option value}`. Application-only fields such as `sourceConflict`, `candidate` and `labels` stay in
the component snapshot; bounds, instance identity and label relationships stay in the node snapshot.

The 24 common fields are declared once in the owning `CommonState` base in `core.hpp`; both snapshots inherit it,
so field access stays `snapshot.name` or `snapshot.verbs`. Each complete snapshot has its own defaulted `==`,
including its base and its own fields (`items` for components, `bounds` for nodes). Comparing the two
`const CommonState&` parts compares every common field, including ones added later. Snapshots keep their copied
values after the queried component changes or is removed.

- `visible` means there is positive on-screen area after the latest arrangement and the component is not hidden.
  Both snapshots use the same clipped bounds, including attached label lines. Before the first arrangement it is false.
- A label's `name` and `value` both contain its displayed text. `focused` means keyboard input goes there now;
  while a context menu is open, components report false.
- An item's `enabled` is its own setting. Parent restrictions determine whether its `verbs` offers `select`;
  the UIA adapter combines parent and item availability for `IsEnabled`.
- `writingMode` and `verticalDigits` are optional and present only on labels. Sliders expose `range`.
  Multi-line fields expose one optional `TextFieldScroll` as `scroll`, with `firstLine`, `visibleLines` and `lineCount`.

The `snapshots` CTest compares every common field and item state across all built-in kinds, with one driver per state.

## Multi-line text fields

Public names distinguish writable fields (`Change<Field, Value>`, `ConfirmField`, `ConfirmFieldChange`,
`ConfirmFieldError`) from the text-field component (`ComponentKind::textField`, `Ui::addTextField`,
`TextFieldHandle`, `Role::textField`). General text layout and styling keep names such as `TextAlign` and `TextStyle`.
An appearance arranges a text field with `arrangeTextField(width, TextFieldHeights, scale[, density])`, returning
`TextFieldLayout`; `Placement::textField` holds that geometry and `Placement::view` holds its multi-line `TextFieldView`.
Multi-line scroll snapshots are `TextFieldScroll`, and `ScrollRegion::textField` identifies a text field's scrollbar.
The unused `Appearance::field()` and `FieldParts` are removed. Renamed public names keep no aliases.

A text field is single-line unless it is created with `multiline`. The choice is fixed at creation; everything else about the field (data connections, confirmation groups, history, semantic role `Role::textField`) is unchanged, and a single-line field behaves exactly as before.

```cpp
auto note = ui.addTextField({.id = "item.note", .maxLength = 200,
                         .multiline = true, .visibleLines = 3}).value.value();
note.setVisibleLines(5);                         // reflows at once
note.setScrollLine(2);                           // first visible display line
auto first = note.scrollLine();                   // Query<unsigned>
ui.semantics().perform({"item.note"}, isoduo::Verb::scroll, 4);  // the same operation, same Status values
```

- **Value.** A line break is one LF (`\n`) and counts as one character toward `maxLength`. A value containing CR, or any other control character, is invalid wherever a value enters (initial value, source, `setValue`, semantic `setValue`). Pasted CRLF, lone CR and lone LF each become one LF; copy and cut write CRLF to the clipboard. Single-line fields still turn pasted breaks into spaces. ComponentSnapshot and UI Automation report the value with its LF characters.
- **Keys.** Enter and Shift+Enter insert a line break (one undo entry each) and never reach Enter shortcuts; Ctrl+Enter does, so an application can make it "OK". Tab still moves focus. Enter while an input method is composing belongs to the input method. Up/Down move by display line (a wrapped piece counts) and keep their column; Home/End go to the line's start/end, Ctrl+Home/Ctrl+End to the text's; PageUp/PageDown move `visibleLines` lines. Shift extends the selection.
- **Layout.** The height is exactly `visibleLines` lines. Long lines wrap at the field's edge like labels; there is no horizontal scrolling. The scrollbar's width is always reserved, so the first lines never re-wrap when the scrollbar appears; the thumb shows only when the text overflows.
- **Scrolling.** The scroll position is the index of the first visible display line, so it keeps its meaning across text size and density changes (the same text stays on top). It follows the caret when the caret moves, never moves the caret, and never snaps back after the wheel or the thumb scrolled it. The wheel scrolls the field first and passes to the page at the ends. Inside an open dialog the dialog's own scrollable content (this field included) takes the wheel and the thumb; the page behind still does not. A position past the last page is `Status::invalidValue` for both entry points.
- **Pointer.** A click puts the caret on the pointed line and column; a drag selects across lines and, above or below the field, scrolls a line per step (the desktop host repeats it on a timer).
- **Layout data for application and host code.** After `arrange`, `Placement::multiline` fields carry `Placement::view` (`TextFieldView`: every display line with its UTF-16 range and caret stops, the first visible line, the track and thumb). `Frame::textOffset(handle, x, y)`, `Placement::caretX/caret_y/caret_rect` and `Frame::selectTo` read it. A host that can lay out again on demand registers `UiState::onRelayout` (the desktop host does) so cursor keys never see stale lines.
- **UI Automation.** The text pattern reads and moves by `TextUnit_Line` (display lines) and `TextUnit_Paragraph` (LF-separated lines); a vertical-only scroll pattern reads and sets the scroll position.

Out of scope here: growing with content, horizontal scrolling or a no-wrap mode, Tab characters, alignment and line spacing and vertical text. Styled text is the [rich text option](#rich-text-fields) below.

## Word and paragraph selection

Text fields (single- and multi-line) select by word and paragraph. Double click selects the word under the pointer and the spaces after it (a punctuation mark or a run of spaces on its own; at the end of a line, that line's last word). Triple click selects the whole text of a single-line field, or the LF-separated paragraph of a multi-line one. Holding the button after either and dragging grows the selection by words or paragraphs, and the first word or paragraph stays selected. Ctrl+Left/Right move to the previous/next word start (spaces skipped, each punctuation mark and each LF a stop), Ctrl+Shift+Left/Right extend, and Ctrl+Backspace/Delete delete up to the same stop (only the selection when there is one; one undo step each).

```cpp
note.setWordSegmentation(isoduo::WordSegmentation::perCharacter);  // dictionary (default) | perCharacter | scriptRun
auto word = note.wordAt(12);                 // Query<TextSpan>: what a double click there selects
auto stop = note.nextWordStop(12);          // where Ctrl+Right goes
note.doubleClick(12);                        // select it, and arm a drag that grows by words
ui.semantics().perform({"item.note"}, isoduo::Verb::selectParagraph, 12);  // the same operations, same Status values
```

- **Where words are.** `dictionary` (the default) asks system ICU for `zh_TW` word boundaries, so Chinese is cut into words. ICU is loaded on first use from System32 on Windows and from the system's shared libraries on Linux. If the dictionary is unavailable, the field falls back to `perCharacter`. `wordSegmentation()` reports the requested mode and `wordSegmentationInUse()` reports the effective mode. `perCharacter` gives each Chinese or Japanese character its own word and keeps runs of Latin letters and digits together. `scriptRun` also keeps Chinese or Japanese runs together. The two rule modes split each punctuation mark and keep runs of spaces together. Dictionary boundaries follow the installed ICU version. Every mode treats each LF as a separate word.
- **Clicks.** The desktop host reads `GetDoubleClickTime()` and `SM_CXDOUBLECLK/SM_CYDOUBLECLK` on each press and lets core count: a press counts as the next click when it comes within the double-click time of the previous one and inside the box centered on the first press; the fourth press is a single click again. Core takes an injectable clock and settings (`UiState::setClickClock/set_click_settings`) and the press entry `UiState::textPointerDown`, so tests never depend on a real clock.
- **Read-only and disabled.** A read-only field selects and moves by words, and Ctrl+Backspace/Delete answer `Status::readOnly`. A disabled field does nothing (`Status::disabled`). Clicks during input-method composition behave like a single click does.
- **Programmatic control.** `TextFieldHandle::setWordSegmentation / wordSegmentation / wordSegmentationInUse / wordAt / paragraphAt / nextWordStop / previousWordStop / selectWord / selectParagraph / doubleClick / tripleClick`, and the semantic `Verb::setWordSegmentation / selectWord / selectParagraph / doubleClick / tripleClick` with `SemanticTree::textUnit` and `wordStop`, share one set of functions.
- **UI Automation.** `TextUnit_Word` uses the same boundaries (a word keeps the spaces after it; each punctuation mark, each LF and a run of spaces on its own are units) and `TextUnit_Paragraph` the LF-separated paragraphs, through `ExpandToEnclosingUnit`, `Move` and `MoveEndpointByUnit`. `TextUnit_Line`, `Page` and `Format` are unchanged.

## Rich text fields

A multi-line field created with `rich = true` holds **plain text plus style spans** (`isoduo::RichText`): bold, italic, underline and a named color. The option exists for multi-line fields only (`invalidValue` on anything else) and is fixed at creation; a plain field behaves exactly as before. Labels stay plain text.

```cpp
auto note = ui.addTextField({.id = "item.note", .multiline = true, .visibleLines = 3, .rich = true}).value.value();
note.setColors({{"red", {176, 0, 32}, {255, 138, 148}}});          // name, light value, dark value
note.connectRichSource({[&] { return model; }, subscribe});        // Source<RichText>: read() -> RichText
note.connectRichConfirmed([&](const isoduo::Change<isoduo::TextFieldHandle, isoduo::RichText>& c) { model = c.value; return isoduo::Result{}; });
note.setStyle(0, 2, {.bold = true});                                // a range of the committed text
note.toggleStyle(isoduo::StyleAttribute::underline);                 // what Ctrl+U does: the selection, or the typing style at a caret
note.applyStyle({.color = "red"});                                  // set the selection's color (caret only: the typing style)
auto value = note.richValue();                                      // Query<RichText>: text + spans in normal form
```

- **Value.** `RichText { std::u16string text; std::vector<StyleSpan> spans; }`, a span being `{start, length, bold, italic, underline, color}` in UTF-16 offsets. The text follows the multi-line rule (LF only) and `maxLength` counts text characters only. Only styled text is listed. `normalizeRichText` (applied to everything that enters a rich field) sorts the spans, drops empty and plain ones and merges adjacent spans of equal style, so equal content always reads back as an equal value; spans that overlap, run past the text or start/end inside a surrogate pair are `Status::invalidValue` and leave the field unchanged. The framework parses no markup language.
- **Colors.** A color is a name in the value; the field's list (`setColors`, replaceable at run time, repainted at once) gives each name a light and a dark value, so a theme switch recolors the text. No name means the theme's normal text color. A name that is not in the list is drawn in the normal color and kept in the value (an application that removed a color, or text pasted from another application, loses nothing). A disabled field is drawn muted.
- **Keys.** Ctrl+B / Ctrl+I / Ctrl+U are taken by a focused rich field before any application shortcut on the same chord (any other field leaves them to the application). With a selection the attribute is applied to all of it unless all of it already has it, then removed. With a caret only, the next typed text changes: it ends when the caret moves or text is inserted. There is no built-in toolbar or menu and no color shortcut: an application calls `toggleStyle` / `applyStyle` from its own buttons.
- **Typed text** takes the style of the character before the caret, or of the one after it at the start of a paragraph (an empty paragraph is plain); a temporary typing style from Ctrl+B etc. wins. `typingStyle()` / `setTypingStyle()` read and set it. An IME preedit is shown in the style it will take.
- **Faces.** A family's real Bold / Italic / Bold Italic file is used when the system has one (Microsoft JhengHei Bold, Segoe UI Bold and so on); otherwise the regular face is synthesized (heavier outline and wider advance for bold, a sheared outline for italic), fallback fonts included. Wrapping and caret stops use the real widths, and the glyph cache keeps the four looks of a glyph apart. Underline is a thin rectangle under the run.
- **Clipboard.** Copy and cut write plain text (CRLF) and, beside it, the framework's private payload (`Clipboard::writeRich`; the Windows host registers the format `Isoduo.RichText.1`). Pasting between rich fields keeps the styles; text from elsewhere is pasted as plain text in the typing style. A payload that does not belong to the plain text beside it is ignored. Plain fields only ever take the plain text. A custom `Clipboard` that does not override `writeRich` / `readRich` carries plain text only.
- **History and data connections.** One style application is one undo entry. Rich fields connect with `connectRichSource` / `connectRichImmediate` / `connectRichConfirmed` (the plain `connect_*` answer `notSupported`, and these answer `notSupported` on a plain field); a confirmation group takes them through `Ui::connectConfirmation` (a `Change<TextFieldHandle, RichText>` per rich field). A source value that does not normalize is ignored like any invalid source value; a source that differs only in styles counts as a different value (conflicts, `useLatest`, `resetToSource`).
- **Semantic layer and UI Automation.** `NodeSnapshot::rich / styles / typingStyle`; `Verb::setStyle` (`StyleEdit{begin, end, change}`), `Verb::setTypingStyle` (`TextStyle`) and `Verb::setValue` with a `RichText`, all admitted like the typed calls and listed in snapshot `verbs`; property event `styles`; `SemanticTree::textColor(ref, name)` gives the color a name paints in right now. The text pattern reports `UIA_FontWeightAttributeId` (400 / 700), `UIA_IsItalicAttributeId`, `UIA_UnderlineStyleAttributeId` (0 / single) and `UIA_ForegroundColorAttributeId` of the current theme, `UiaGetReservedMixedAttributeValue` for a range that mixes values, and `FindAttribute` for the four attributes.

Out of scope: rich labels and single-line rich fields, per-run alignment, size or font family, strikethrough, super/subscript, links, lists, images, RTF / HTML / Markdown input or output, a built-in toolbar or context menu, free RGB colors.

## Justified text

Labels and text fields take `TextAlign::justify`; labels also take `TextAlign::distribute`. Both are set at creation (`ComponentConfig::textAlign`) or at run time (`setTextAlign`, semantic verb `setTextAlign`).

- **justify** stretches every display line to the full width except the last line of a paragraph (the line before an LF, or the end of the text), which stays left-aligned. A single-line text field therefore looks left-aligned. **distribute** also stretches the last line and a one-line text (a form label such as `姓　名`); a text field refuses it with `Status::invalidValue` and stays unchanged.
- **Where the extra width goes** is `JustifyMethod` (`ComponentConfig::justifyMethod`, `setJustifyMethod`, semantic verb `setJustifyMethod`, read back with `justifyMethod()`; only used while the alignment is justify or distribute). `automatic` (default): around every CJK character and full-width punctuation mark and after every ASCII space or ideographic space (U+3000, which stays with the character before it); punctuation is never compressed and a Latin word is never widened inside. `spaces`: only after ASCII spaces, so a Chinese-only line stays left-aligned. `perCharacter`: between any two characters, Latin letters included; such a line is drawn piece by piece, so ligatures such as `fi` do not form.
- **Shared rules.** Width is added only between whole characters (a base with its variation selector, or an emoji sequence, is never split; the unit is the shaping cluster). Trailing spaces neither count toward the width nor receive any. A line with nowhere to stretch is left-aligned. The added width sits on top of the font's own spacing and kerning.
- **Caret, selection and pointer.** All of them use the stretched, drawn positions: `TextFieldLine::run` stops include the added width, `TextFieldLine::segments` / `Line::segments` list the drawn pieces (each starts at its own x). Typing re-spreads the line being typed in, which is expected. A caret at a gap is drawn at the start of the next character.
- **Rich text.** A justified line of a rich field is also cut at every style boundary (no width is added there), so each drawn piece lies inside one run and keeps its face and color; a run's underline spans the gaps added inside it. Caret, selection, hits and drawn glyphs read the same positions as in plain text.
- **Vertical labels.** Justify and distribute read as top alignment there; the justification method is ignored.
- **User setting (WCAG 1.4.8).** `PresentationSettings::justifyAsLeft` (default off, applied by `Ui::setPresentation`) draws every justified and distributed text left-aligned. The property values do not change; UI Automation then reports `HorizontalTextAlignment_Left`, otherwise both values report `HorizontalTextAlignment_Justified`. The settings demo's Appearance page has the checkbox and saves the setting.
- **Not included:** punctuation compression or squeeze-before-stretch (Mainland style), hyphenation, a separate last-line alignment property, soft line breaks, justified vertical text and per-run alignment in rich text.

## Context menus

A context menu is a short list of commands opened beside a component by the right mouse button (on release), the menu key or Shift+F10. One menu is open per window; it is drawn inside the window like a dropdown's list (flipped left and up near the window's edges, scrollable when the window is too small) and never changes which component has focus.

- **Built-in items.** Every text field has Undo(U), Redo(R), Cut(T), Copy(C), Paste(P), Delete(D) and Select All(A), which run exactly the operations of Ctrl+Z / Ctrl+Y / Ctrl+X / Ctrl+C / Ctrl+V / Delete / Ctrl+A. They disable themselves when they cannot act (nothing selected, empty clipboard, nothing to undo, read-only field).
- **Attaching a menu.** `ui.addContextMenu(component, items, mode)` works on any component. `MenuItem` has an id (not starting with `isoduo.`), a label, enabled, an optional quick-key letter, a shortcut hint (display only: it registers no shortcut) and the code to run, which receives the component and, for a list, the clicked row. `MenuItem::divider()` is a separator. `MenuMode::append` (default) puts the items after the built-in ones, `replace` shows only yours, `off` disables the field's menu. The returned `ContextMenuHandle` adds, removes, updates and enables items at any time, also while the menu is open.
- **From code.** `ui.openContextMenu(component, position, row)`, `closeContextMenu()`, `contextMenu()` (items with their availability), `invokeMenuItem(id)`. A disabled component returns `disabled`; a background component behind a modal dialog returns `blockedByModal`.
- **Keyboard.** Up/Down (skipping separators, wrapping), Home/End, Enter and Space run the highlighted item; a quick-key letter runs its item, or when several share it moves to the next one. Esc, Alt, F10, a click outside or the window losing activation close the menu; the click that closes it reaches nothing underneath. Disabled items can be highlighted but not run.
- **Semantic layer and UIA.** While open, the component has a `Role::menu` child (`menuNodeId(id)`, `"<id>/menu"`) with `menuItem` and `menuSeparator` children; `Verb::contextMenu` opens it, `invoke` on an item runs it, `cancel` on the menu closes it. UIA sees Menu / MenuItem / Separator, `AccessKey`, `AcceleratorKey`, `IsEnabled`, the Invoke pattern, and `MenuOpened` / `MenuClosed` events.

Out of scope here: submenus, check marks and icons, menus outside the application window, an application menu bar and touch long-press.
## Vertical labels (display only)

A label can be set vertically: text runs top to bottom, columns from right to left, laid out by the Taiwan Ministry of Education rules. Editable vertical text and read-only text fields are not covered.

```cpp
auto note = ui.addLabel({.id = "about.note", .value = u"Isoduo 設定管理示範（第 12 版），謹此致謝。",
                          .writingMode = isoduo::WritingMode::vertical}).value.value();
auto page = isoduo::layout::column({isoduo::layout::verticalText(note, 200)});  // the height is yours, in DIP
note.setWritingMode(isoduo::WritingMode::horizontal);     // changes at run time; the next arrangement follows
note.setVerticalDigits(2);                               // 0-3 (default 3); anything else is Status::invalidValue
ui.semantics().perform({"about.note"}, isoduo::Verb::setWritingMode, isoduo::WritingMode::vertical);  // same checks, same Status
```

- **Size.** The height comes from `layout::verticalText(label, height)`; characters fill a column to that height, then continue in the next column to the left, and a line break (`\n`) starts a new column. The width is the number of columns times the column width (the line height of the text). `verticalText(label, 0)` never wraps.
- **Too many columns.** When the columns are wider than the space the layout offers, the label scrolls sideways: it starts at the right edge (the start of the text), the wheel (also with Shift) moves toward the end of the text, the horizontal wheel and the bar's thumb move both ways, and a scrolling label takes focus, where Left and Right (and Home and End) scroll it. Nothing is cut off beyond reach.
- **Characters** follow UAX #50 (data in `src/vertical_orientation_data.inc`, source and version in `THIRD_PARTY_NOTICES.txt`): CJK, full-width letters and emoji stand upright; Latin words and sentences, single letters and runs of four or more digits turn 90 degrees clockwise; two to three half-width digits (the limit set by `verticalDigits`) are set side by side in one cell, squeezed to the cell width; one digit stands upright.
- **Punctuation** uses the Taiwan placements: the full-width comma, period, enumeration comma, semicolon, colon, exclamation and question marks sit in the middle of the cell, drawn from the horizontal glyph (the font's vertical alternates differ between weights); quotes and brackets face up and down (the font's vertical form, else a quarter turn); dashes and ellipses are centered lines over two cells; middle dots sit in the middle of the cell. Mainland China and Japanese placements, avoiding punctuation at the start or end of a column, and underline-style marks are not provided.
- **Accessibility.** The name is the label text in reading order, the bounding box is the drawn box; no text pattern is added.
- **Alignment and spacing.** The label's `textAlign`, `lineSpacing` and `paragraphSpacing` apply to vertical text too, read in one place (`verticalParams` in `src/vertical.hpp`): left, center and right place each column at the top, middle or bottom of the label's height; line spacing multiplies the distance between columns; paragraph spacing (DIP) is added between the last column of one paragraph and the first of the next. The defaults (left, 1.0, 0) are top, 1.0 and 0.

## Desktop support

The settings demo includes navigation, persistence, history, diagnostics and accessibility support; see its [README](examples/settings_demo/README.md).

The native host targets Windows and OpenGL 3.3 and exposes a UI Automation provider. MCP and full grapheme and RTL support remain outside v1; see the form documentation for text and IME boundaries. Third-party sources are listed in [third_party/SOURCES.md](third_party/SOURCES.md); licenses are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Motion (animations)

The framework has one internal animation clock (`src/animation.hpp`). A component registers a display property
going from A to B over a duration; the host schedules the next frame only while something is still running, so an
idle UI never repaints. Today only the slider thumb uses it: arrow keys, Home/End, a track click and the release
of a drag slide the thumb over 150 ms (cubic ease-out), turning from the position currently shown when the target
changes mid-way. Values, semantics and UI Automation update at once; only the picture moves. Application code,
`setValue` (semantic or UIA), Ctrl+Z / Ctrl+Y, data-source changes and dragging itself land at once.

- `PresentationSettings::motion`: `Motion::automatic` (default) follows Windows "Show animations in Windows"
  (`SPI_GETCLIENTAREAANIMATION`, re-read on `WM_SETTINGCHANGE`); `Motion::off` never plays. There is no
  "always on". Switching it with `Ui::setPresentation` takes effect at once and lands running transitions.
- `Ui::animating()` / `SemanticTree::animating()` answer whether any transition is still running (one query,
  one admission rule). Wait for `false` before comparing screenshots.
- A minimized, cloaked or fully covered window jumps running transitions to their end and draws nothing for them.
  "Fully covered" is best effort: one visible opaque window above ours that spans the whole client area.
- A core-only host that installs no time source (`detail::UiState::animationTime`) never animates. Tests use
  that seam as a fake clock, and `setSystemAnimations` / `DesktopHost::systemAnimationSource` to simulate the
  Windows setting. Tests: `animation` (core) and `isoduo_host_verify --animation-only` (native host).

## Palette overrides

An application can change individual colors of the built-in appearances without writing an appearance of its own. `Ui::setPaletteOverrides` takes one list for the whole UI environment; each entry names an appearance (`Appearance::name()`), a theme variant, a palette role and a value. Colors that the list does not mention keep their built-in value, and a variant that is not listed is untouched (overriding light never changes dark).

```cpp
auto green = isoduo::Color{20, 110, 50};
auto result = ui.setPaletteOverrides({
    {std::string(isoduo::webAppearance().name()), isoduo::ThemeVariant::light(), "accent", green},
    {std::string(isoduo::webAppearance().name()), isoduo::ThemeVariant::light(), "scrim_alpha", 140},
});
```

- Roles are the names in `Palette` (`isoduo::paletteRoles()`: `background`, `panel`, `surface`, `text`, `muted`, `border`, `outline`, `accent`, `on_accent`, `selection`, `error`, `scrim`, `hover`, `focus`, plus `scrim_alpha` and `hover_alpha` with an `int` value 0-255). A color added to `Palette` later becomes overridable without further changes.
- `outline` is the edge that identifies a component (text field box or underline, dropdown box, checkbox box, radio circle, slider track, scrollbar thumb, and a progress track that shows no value text); disabled components draw it with `border` instead. `border` stays the light color for decorative lines (button edge, list divider, tooltip edge).
- `focus` is the focus ring's color. `Palette::focus` is an `std::optional<Color>`: empty (the default, and the built-in palettes) means "the accent", so the ring follows a replaced `accent` for every appearance, built-in or custom, even one that assigns `accent` after building its palette; setting `focus` (in a custom palette, or by overriding the `focus` role) pins the ring to its own color. Read the ring color with `Palette::focusColor()`; `Ui::effectivePalette` always returns `focus` set.
- The list is replaced as a whole and takes effect on the next frame; `setPaletteOverrides({})` restores the built-in colors. Overrides apply only to the built-in appearances whose module is linked; a custom `Appearance` is never affected.
- Each affected appearance and variant is checked against WCAG contrast (`text` on `background`, `panel`, `surface` and `selection`, `muted` and `error` on `surface`, `on_accent` on `accent`: 4.5:1; `accent` and `outline` on `surface`, `outline` on `background` and `panel`, and `focus` (the focus ring) on `surface`, `background` and `panel`: 3:1; `outline` is also checked on those three surfaces with the hover overlay laid over them; `border` is only a decorative divider and is not checked). Any failure, an unknown appearance, variant or role, a value of the wrong kind, an alpha role outside 0-255 or a repeated entry returns `Status::invalidValue` with a message naming the pair and its ratio, and the previous list stays active. `isoduo::checkPaletteContrast(palette)` runs the same rules so an application can pre-check a palette.
- `Ui::paletteOverrides()` reads the list; `Ui::effectivePalette(appearance, variant)` and `SemanticTree::effectivePalette()` (read-only; the Desktop host binds its appearance, a core-only host calls `Ui::bindAppearance`) report the colors that actually paint.

## Pointer hover

Buttons, checkboxes, radio options, dropdowns (body and open options), list rows, tabs, navigation items, sliders and text fields change while the pointer rests on them. Item-based components change one item at a time; on a slider the thumb marks the hover wherever the pointer is on it. The change is an overlay of `Palette::hover` at `Palette::hoverAlpha` (Material-inspired: the text color at 4% in light, where 8% would leave the muted field label on the hovered surface at 4.22:1, and at 8% in dark; Minimal Web: 6% black in light, 8% white in dark; the values are proposals). It appears and disappears at once, is added on top of selected and focused items, and does not change the cursor shape.

- Nothing hovers for a disabled component or item, behind an open modal dialog (components inside the dialog hover as usual), or from touch and pen input. While a slider, a scrollbar thumb or a text selection is dragged the hover stays on it; the release looks at the pointer again. Leaving the client area, losing activation and opening a dialog clear the hover. Scrolling and reflow (text size, density, appearance, components added or removed) re-evaluate the last real pointer position at once.
- `PresentationSettings::hover` (`Hover::on` by default, `Hover::off`) switches the overlay for every source through `Ui::setPresentation`. Tooltips keep following the pointer with the switch off.
- `Ui::hover()` reports the hovered component (and item). `Ui::simulateHover(handle, item)` acts exactly like the real mouse resting there (overlay, tooltip delay, the exclusions above; `Status::disabled`, `blockedByModal`, `notFound`, `busy` while a drag holds the hover); the next real mouse move takes over, and `Ui::clearSimulatedHover()` ends it. `SemanticTree::hover()` and `perform(ref, Verb::hover)` are the same entries by id and item value. UI Automation does not report hover.
- Paint code asks `ComponentPaint::hovered()` / `hovered(item)` and draws through `ComponentPaint::hover(rect, radius)` (`PaintRole::hover`); a custom component type follows the same two calls.
## Focus indicator

The focus indicator is one closed rounded ring in the palette's `focus` color (by default the accent), drawn inside the
focused component's edge so it takes no room and a scroll area cannot clip it. It frames the whole component; in a
component made of rows it frames the row that has the focus: the selected option of a radio group (the first usable one
while none is selected), the candidate row of a list, the candidate tab (the selected tab keeps its underline inside the
ring), the selected row of a navigation list; a slider is framed around its title and track, a text field around its whole
box, a scrolling or vertical label around its viewport. A component with an error uses the `error` color; unfocused, it
keeps its thin error frame. Every component keeps a ring's thickness of room around what the ring frames, whether or
not it is focused, so focus never moves anything.

Focus indicators are drawn only while the
user is working with the keyboard, like `:focus-visible` in browsers. Any key without Ctrl or Alt turns them on
(a modifier on its own, and the F10 that only closes an open menu, change nothing);
a pointer press anywhere in the window, with either mouse button, turns them off. Moving focus from code (`focus()`, semantic
operations, UI Automation `SetFocus`) never changes this state, and semantics and UIA always report the real
focus. Editable text fields always draw the indicator while focused (unless the mode is `never`). A focused slider also
enlarges its thumb.

* `PresentationSettings::focusIndicator` is `FocusIndicatorMode::automatic` (default), `always` or `never`; switch it
  with `Ui::setPresentation`. `never` draws no focus indicator on any component (editable text fields included) and the
  slider thumb does not grow; the text caret is not a focus indicator and still draws, and `focused`, UIA's
  `HasKeyboardFocus` and the focus events are untouched. `never` also wins over the Windows setting below.
* `PresentationSettings::focusRingWidth` (DIP, 2-6) and `focusRingRadius` (DIP, 0-32) are empty by default: the
  appearance's own 3 DIP and the component's own corner radius (Web 6, Material 16; a radius larger than half the
  ring's shorter side makes a capsule). A value outside the range makes `setPresentation` return `invalidValue` and
  apply nothing, like a text size outside 12-28. A new value takes effect on the next frame and the layout reflows once
  (the room around the ring follows the thickness). The ring color is the palette role `focus`, see "Palette overrides".
* Windows "Always underline access keys and show keyboard focus" (`SPI_GETKEYBOARDCUES`) forces the indicator
  on, and a change takes effect without restarting.
* `Ui::focusIndicatorVisible()` reports whether indicators are being drawn right now. Tooltips (C11) use the
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
./build/Release/isoduo_host_verify.exe --trial-screenshots --evidence build/Release/evidence/presentation-trial
```

PR04 requires coordinator review and is not marked as passed by the generator.
PS10 runs automatically in the `presentation_recovery` CTest. A report with open or failed cases causes the idle
script to return a failure status.

## Real-platform runs per change

The Windows CTest `clipboard_contract` exercises native clipboard formats in a
private window station. It requires Windows Secondary Logon to start the local
worker and fails if it cannot establish isolation. The `serialized_contract`
test checks the same rich-text wire round trip without Windows.

Production changes to layout or to the desktop host need runs that only a real, idle desktop can give (real clipboard,
input method, display scales, foreground window, frame timing). The core-seam twin of each property runs in CTest; this table
lists, per change, which idle-desktop runs the author still has to do before a release. Commands are
`./verify-idle.ps1 -Only <name>` unless the row says otherwise; `isoduo_host_verify` is `build/Release/isoduo_host_verify.exe`.

| Change | Idle-desktop runs |
| --- | --- |
| PR #108 retained frames after component removal | `-Only clipboard,ime,dpi`; real wheel, Shift-wheel, horizontal wheel and thumb/track drag while a text field, scrolling label or dropdown is removed before repaint. CTest `removals` covers pending/committed removal and ID reuse through public handles and frames; host drag routing also retains the source instance across repaint. |
| #105 camelCase C++ names (0.4.0) | none beyond CTest: host, UIA adapter and layout edits are renames only, including semantic property-event keys; `public_names` rejects new snake_case declarations, the installed consumer rejects old names, and all `demo_render_compare` artifacts stay byte-identical |
| #72 focus indicator | `-Only dpi` (R08 real-window focus); toggle the Windows keyboard-cues setting once to see `WM_SETTINGCHANGE` re-read it; right press on empty space and a lone Alt with real input |
| #73 slider drag interface | `-Only perf` (the report-only `slider-drag-text-size` plus PM01-PM05), `-Only dpi` |
| #74 pointer hover | `isoduo_host_verify --hover-only` (needs a real cursor position), look at both appearances; `-Only demo-uia` |
| #75 slider animation | `-Only perf` (includes the gating `idle-after-animation`), `isoduo_host_verify --animation-only`, `-Only uia`; drag and release a slider at 150 % and 200 % (the 4 DIP click/drag threshold and the release slide) |
| #78 palette overrides | `-Only clipboard,ime,dpi` |
| #79 multi-line text field | `-Only ime` (a real Zhuyin composition in a multi-line field), `-Only clipboard`, `-Only dpi` at 150 % and 200 %, author trial |
| #80 text alignment and spacing | `-Only presentation` (layout), `-Only ime`, `-Only dpi` |
| #81 rich text | `isoduo_host_verify --richtext-only` (real clipboard), `isoduo_demo_keyboard --real`, `-Only ime`, `-Only dpi` |
| #82 vertical labels | `-Only dpi` |
| #83 word and paragraph selection | `-Only ime`, `-Only dpi` (the real double-click time and distance are read at every press) |
| #84 justified text | `-Only ime` (a composition on a justified line: the core test moves the caret rectangle, only a real input method shows the candidate window), `-Only dpi` (pieces are placed at whole pixels at 125 / 150 / 200 %) |
| #85 context menu | `-Only demo-uia`; the Apps key, Shift+F10 and Alt/F10 with real input (the CTest drivers post messages), right-button release over a row |
| #86 outline | none beyond `demo_render_compare` (CTest); compare the saved images |
| #90 focus ring | `-Only dpi` (the ring at 100 / 150 / 200 %), `-Only uia`; Tab through both appearances with the real keyboard (every component, light and dark, standard and compact), then try the four components on the appearance page |
| #92 error-line layout | none beyond CTest: the error-line layout of every component type at three widths is pinned by `error_lines`, and `demo_render_compare` stays byte-identical to before (compare the saved images) |
| #93 component registry dispatch | `-Only clipboard,ime,dpi,uia` (it touches the host, layout and the context menu: the text menu and its commands, the slider drag, the list row reveal and the dropdown popup); then with real input: right-click a text field and a list row, drag a slider, press and release on a list row, open a dropdown and pick with the pointer and with the keys |
| #94 shared handle base and handle equality | `isoduo_host_verify --context-menu-only` and `-Only clipboard`: the `ContextMenuHandle` operations in `src/context_menu.cpp` now run the shared liveness check (no host, layout or menu behavior change); nothing else needs a real platform |
| #95 `Source<T>` and `Change<Field, Value>` (0.3.0) | none: public type names only (no host, layout or input change); the consumer example checks the types at compile time and `demo_render_compare` stays byte-identical |
| #96 public names follow the glossary; `addX` sets the component type (0.3.0) | none: renames only (`src/desktop_host.cpp`, `src/desktop_host.hpp` and `src/uia_provider.cpp` change by type renames only, no host, layout or input change); the consumer example checks the new and removed names at compile time and the component type of every `addX` at run time, and `demo_render_compare` stays byte-identical |
| #99 list change record and label read source | none: the list write's parameter type and a second label connection (no host, layout or input change); `components` drives both through the handles, the consumer example checks the types at compile time and `demo_render_compare` stays byte-identical |
| #100 field and text-field public names | none: host and layout edits are pure renames, and the unused appearance method is removed; the consumer checks the new names and rejects the old ones, with all existing CTests and `demo_render_compare` retained |
| #102 shared snapshot fields | none: owning `CommonState` extraction and complete snapshot equality only; no host or layout changes, and option-node values are preserved for #106; `snapshots` and `demo_render_compare` cover the refactor |
| #104 individual component removal | `-Only clipboard,ime,dpi`: row/grid compaction changes layout; removal discards IME preedit and suppresses blur validation through the core focus seam. No `desktop_host.cpp` or `uia_provider.cpp` changes. `removals` and `handles` cover the code and semantic contracts; the demo adds no removal controls. |
| #101 aligned component and semantic snapshots (0.4.0) | `-Only uia,axe,demo-uia`: the UIA adapter combines item and parent `IsEnabled` and propagates parent availability events; `snapshots` is the core twin. No `desktop_host.cpp` or layout change. Check right-click and keyboard menus once with real input; component `focused` is false while the menu owns input. |
| #106 option snapshots describe their own state (0.4.0) | `-Only uia,axe,demo-uia`: options no longer inherit parent descriptions, errors, labels or selection requirements; disabled list rows are not keyboard-focusable. `option_nodes` is the core twin. No host/provider or layout code changes; `tests/uia/verify.ps1` requires the two new UIA09 property checks. |

The idle script reports a missing or failing run as a failure; the author-run items in each ticket stay open until the
author has run them.

## Using Isoduo from another CMake project

Isoduo ships as static libraries built from source, with no ABI guarantee. Rebuild your application when updating Isoduo. Install both configurations into one prefix:

```powershell
.\build.ps1 -Configuration Release -Install C:\sdk\Isoduo
.\build.ps1 -Configuration Debug -Install C:\sdk\Isoduo
```

Then in your project:

```cmake
cmake_minimum_required(VERSION 3.25)
project(StationApp LANGUAGES CXX)
find_package(Isoduo 0.4 REQUIRED COMPONENTS Desktop AppearanceWeb)
add_executable(station_app main.cpp)
target_link_libraries(station_app PRIVATE Isoduo::Desktop Isoduo::AppearanceWeb)
install(FILES "${Isoduo_THIRD_PARTY_NOTICES_FILE}" DESTINATION .)
```

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\sdk\Isoduo
cmake --build build
```

Run these commands in a Visual Studio x64 developer environment. Without `build.ps1`, the equivalent framework build and install for each configuration is:

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
ctest --test-dir build/Release --output-on-failure
cmake --install build/Release --prefix C:\sdk\Isoduo
```

Repeat with `Debug` in the build directory and build type. Requirements are Windows 10 22H2 or Windows 11 23H2 or later, x64, CMake 3.25, and a C++20 or newer compiler: clang-cl or Visual Studio 2022 MSVC with a toolset at least as new as the one used to build Isoduo. The default runtime is `/MD` (`/MDd` in Debug); consumer and framework runtime settings must match. Isoduo itself builds with clang-cl and Ninja.

Request the components you use: `Core`, `AppearanceWeb`, `AppearanceMaterial`, `Desktop`, and `Diagnostics`. Each appearance and Desktop automatically imports Core. Desktop includes the Windows host, OpenGL, text, UI Automation, FreeType and HarfBuzz. Diagnostics is available in a full desktop build. Without `COMPONENTS`, only Core is imported. All public headers are installed regardless of the selected component; using a Desktop or Diagnostics API requires linking its corresponding target. Draw lists and the rendering backend are internal.

If you link Desktop, credit FreeType in your product documentation: show `isoduo::thirdPartyNotices()` in an About box or ship `THIRD_PARTY_NOTICES.txt`. The framework license is available as `Isoduo_LICENSE_FILE`. Source archives and headers are vendored under `third_party/` and verified by SHA256 at configure time; no network access or package manager is required to build.

## Windows and WSL worktrees

Windows-created linked worktrees can contain drive-letter Git pointers that WSL
cannot resolve. `tools/repair-worktree-paths.ps1` preserves the worktree location
and uses relative pointers through NTFS junctions. Run it from the main checkout,
with Git operations on the affected worktree idle:

```powershell
$repair = @{
    Worktree = 'C:/worktrees/isoduo-task'
    Bridge = 'C:/worktrees/isoduo-task-bridge'
    Backup = 'E:/Isoduo/.git/interop-backups/isoduo-task'
}
./tools/repair-worktree-paths.ps1 @repair -Mode Plan
./tools/repair-worktree-paths.ps1 @repair -Mode Apply
# To restore the original pointer bytes:
./tools/repair-worktree-paths.ps1 @repair -Mode Restore
```

Start with a fresh backup path on the first run. The bridge must be on the
worktree's volume. Apply backs up both pointer files before writing and preserves
their Hidden attributes. It reuses a junction only when it points to the expected
directory. Verify discovery and `worktree list` in both environments after applying.

After Restore or an interrupted Apply, rerun Apply with the same inputs and
complete backup. You can also rerun Restore with those inputs. Both commands
accept any mix of original and repaired pointers, compare exact bytes, and
refuse other contents. If Apply stopped while creating the backup and both
pointers still contain their original bytes, leave the incomplete backup in place
and use a fresh backup path. If a write was interrupted within one pointer, keep
the backup and inspect the pointer before resuming. Restore keeps existing
junctions and backups.
Keep junctions in place while using repaired pointers; moving or recreating a
worktree requires rechecking its metadata. This is a per-worktree repair, not a
hook into tools that create new worktrees. A `prunable` label alone does not mean
the worktree's files are absent.

`tests/worktree_paths.ps1` exercises cross-volume and same-volume repair, Git
operations and restoration using two explicitly supplied fresh fixture roots.
The optional `-WindowsGitExecutable` takes a WSL path to Windows Git for read-only
compatibility checks. The fixtures are retained for inspection.

## Updating third-party dependencies

1. Replace the relevant vendored archive or Khronos header under `third_party/`, retaining the exact upstream bytes. Review the new source and license files, including any added or changed license obligations.
2. Update version, commit, URL, SHA256 and, where applicable, license-file and credit metadata in `cmake/ThirdPartyManifest.cmake`; update the matching human-readable entries in `third_party/SOURCES.md`. Preserve the binary attributes in `.gitattributes`.
3. Configure a build with the updated manifest, then regenerate the checked-in notices:

   ```powershell
   cmake --build build/Release --target update_third_party_notices
   ctest --test-dir build/Release -R third_party_notices --output-on-failure
   ```

4. Review `THIRD_PARTY_NOTICES.txt`, rebuild and run the full tests and installed-package verification. Commit the vendored files, manifest, source descriptions and regenerated notices together. CI-only tools and example-only dependencies do not belong in the shipped notices.
