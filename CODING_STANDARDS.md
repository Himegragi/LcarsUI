# Coding standards for review

- A property asserted through more than one test seam uses one shared predicate.
- A real-platform test (IME, DPI, clipboard) has a core-seam twin wherever the property is observable without the platform.
- Production changes to layout or the desktop host list which real-platform runs they require.
- Tests should exercise observable behavior through established public or host seams, with independent expected results.
- Preserve user data and make platform failures distinguishable from unsupported or absent functionality.
- Keep evidence, limits and reproducible verification steps clear and current.

## 公開名稱 (public names)

All Isoduo C++ names across the repository (public headers, implementation, tests, examples, demo and showcase), and
comments referring to them, follow the rules below. A renamed name keeps no alias. The
rules come from the author's glossary (`CONTEXT.md`, not in the repository), but this section is complete without it:
the words public names use are these.

- Terms. A component is one UI element the application creates with a `Ui::addX` function (a button, a text field, a
  list); its component type is its `ComponentKind`. An item is one entry a user can choose: an option of a radio group,
  a dropdown or a list (`OptionItem`, identified by an `OptionValue`), or a context menu's `MenuItem`. A handle is a
  typed reference to one thing that does not keep it alive (`XxxHandle`). A read source is how a component reads the
  application's value and hears that it changed (`Source<T>`); a change record is what a write hands the application:
  the field, the new value and the value it started from (`Change<Field, Value>`); a confirmation group is a set of
  fields one button writes together (`connectConfirmation`).
- Existing patterns. Types are PascalCase; functions, fields, enumerators and constants are camelCase
  (`addRadioGroup`, `readOnly`, `Status::notFound`, `defaultMaxTextLength`, `menuUndo`). Single-word names stay as they
  are (`focus`). Acronyms are ordinary words (`componentId`, `apiMisuse`, `uiEnvironment`). Private members keep their
  trailing underscore (`componentLifetimes_`). One component type is one word everywhere: its `ComponentKind`
  enumerator, the suffix of its `Ui::addX` function and the prefix of its handle
  (`ComponentKind::radioGroup`, `Ui::addRadioGroup`, `RadioGroupHandle`). A typed reference to one thing is
  `XxxHandle`; what an operation hands back, when it is more than a `Result`, is `XxxResult` (`ConfirmationResult`,
  `CreateResult`, `RunResult`).
- Boundaries. Semantic property-event strings follow their C++ names (`"readOnly"`, `"numericValue"`). Standard-library
  and foreign API names keep their required spelling. CMake targets, file names, persisted demo `settings.json` keys,
  macros (`ISODUO_OP`), menu command identifiers (`"isoduo.undo"`) and other runtime text are outside the C++ naming rules.
  Built-in menu identifiers reserve `isoduo.*`; serializers emit canonical Isoduo headers,
  with legacy names permitted only in readers and migration fixtures. CTest
  `public_names` checks public header declarations (including the generated version header template); its fixtures
  reject snake_case and exclude comments, literals, `std::` names, macros and private trailing underscores.
- A. An enumeration that says which kind of thing something is, is `XxxKind` (`ComponentKind`, `LabelKind`,
  `DiagnosticKind`, `layout::Node::Kind`, `SemanticEventKind`). Kind only classifies a thing: a choice of how something
  behaves is `XxxMode` or `XxxMethod` (`MenuMode`, `FocusIndicatorMode`, `WritingMode`, `JustifyMethod`), and `Density`,
  `Motion` and `Hover` keep their names. The accessibility `Role` is not a kind and keeps its name. `WordSegmentation`,
  `TextAlign`, `DebugContext` and `layout::VerticalForm` predate these rules and have not been reviewed against rule A;
  they are not precedents for new names.
- B. A public name never calls a UI component a control: it is a component (`ComponentConfig`, `layout::component`,
  `layout::Node::Kind::component`), in names and in header comments. The word stays where it means something else:
  Unicode control characters, and the verb ("two handles that control the same thing").
- C. Parameters given once, when something is created, are `XxxConfig` (`ComponentConfig`, `DesktopConfig`); ones that
  can change while it runs are `XxxSettings` (`PresentationSettings`). "Option" only means an item the user can choose
  (`OptionItem`, `OptionValue`, `OptionSnapshot`), never a setting.
- D. The same thing takes the same word. The options of a component are its items: set with `setItems`
  (`DropdownHandle`, `ListHandle`, `ContextMenuHandle`), passed as `items` (`addRadioGroup`, `addDropdown`,
  `addList`), listed as `ComponentSnapshot::items`; a scroll state anchored on one names it `anchorItem`.
  `layout::Placement::options` and `optionStart` keep their names: they hold the bounds of the placed options, and of
  tab and navigation pages, as laid out, not a component's items.
- E. `Field` means a writable field (a text field, checkbox, radio group, slider, dropdown or list); a list writes
  immediately only, and a navigation list is not a field. `TextField` means only the text-field component
  (`ComponentKind::textField`, `Ui::addTextField`, `TextFieldHandle`). Names beginning with `Text` otherwise mean
  general text layout or styling shared by labels and text fields (`TextAlign`, `TextRun`, `TextStyle`, `TextSpan`,
  `TextMeasure`, `TextSegment`). Public header comments use "text field" for that component and "field" for writable
  fields in general. `ConfirmFieldChange` and `ConfirmFieldError` belong to confirmation groups.
- F. A read-only snapshot is `XxxSnapshot`: a struct that describes the whole state of one component, option, menu, menu
  item or semantic tree node at one moment, and is only read back, never passed in (`ComponentSnapshot`,
  `OptionSnapshot`, `MenuSnapshot`, `MenuItemSnapshot`, `NodeSnapshot`). A struct that holds a part of such state, or
  that the application passes in, keeps its own name (`CommonState`, `Placement`, `Frame`, `Palette`, `TextSpan`, `TextFieldScroll`,
  `HoverTarget`, `OptionValue`); so do `describe()`, `SemanticTree`, `NodeRef` and `Ui::semantics()`.
