#pragma once
#include "lcars/layout.hpp"
#include "lcars/fonts.hpp"
namespace settingsDemo {
// note and noteSpans together are the item's rich text description (lcars::RichText): the text and its style spans.
struct Item { lcars::OptionValue id; std::u16string name, note; std::vector<lcars::StyleSpan> noteSpans; bool operator==(const Item&) const = default; };
struct SettingsDocument {
    std::u16string name;
    bool notices;
    std::optional<lcars::OptionValue> mode, region;
    int volume;
    std::vector<Item> items;
    int nextItemId;
    lcars::PresentationSettings presentation;
    std::string accent = "default";  // "default", "green" or "orange": the palette override set chosen on the appearance page
    std::string focusColor = "default";  // "default" (the ring follows the accent), "green" or "orange": overrides the palette's `focus` role
    bool operator==(const SettingsDocument&) const = default;
};
// The one spelling of FocusIndicatorMode in the focus-mode dropdown and in the settings file; unknown ids have no mode.
const char* focusModeId(lcars::FocusIndicatorMode mode);
std::optional<lcars::FocusIndicatorMode> focusModeFromId(std::string_view id);
struct AboutLicense { lcars::OptionValue id; std::u16string name, text; };
struct AboutInfo {
    bool diagnostics = false;
    std::vector<AboutLicense> licenses;
    std::vector<lcars::FontFamily> fonts;
    std::function<lcars::FontResolution(const lcars::PresentationSettings&)> resolveFont;
    std::u16string appearance, renderer, settingsPath, build;
};
class Demo {
public:
    lcars::LabelHandle aboutVertical() const { return aboutVertical_; }
private:
    std::u16string nameValue_ = u"測試使用者";
    bool noticesValue_ = true;
    std::optional<lcars::OptionValue> modeValue_ = lcars::OptionValue{"normal"}, regionValue_ = lcars::OptionValue{"north"};
    int volumeValue_ = 40;
    std::function<void()> nameChanged_, noticesChanged_, modeChanged_, regionChanged_, volumeChanged_;
    lcars::LabelHandle title_, status_, nameLabel_, nameHelp_, nameError_;
    std::vector<lcars::LabelHandle> aboutLabels_;
    lcars::LabelHandle licenseText_;
    lcars::LabelHandle aboutVertical_;  // vertical writing sample (display only)
    AboutInfo about_;
    std::optional<lcars::OptionValue> licenseSelection_ = lcars::OptionValue{"lcars"};
    std::function<void()> licenseSelectionChanged_;
    lcars::ButtonHandle undo_, redo_, revert_, save_;
    lcars::LabelHandle closeTitle_, closeMessage_;
    std::function<void()> closeHandler_;
    lcars::LabelHandle loadTitle_, loadMessage_, errorTitle_, errorMessage_;
    lcars::ButtonHandle loadOk_, errorOk_;
    lcars::DialogHandle loadDialog_, errorDialog_;
    std::function<std::u16string(const SettingsDocument&)> saveHandler_;
    std::u16string settingsPath_;
    bool saveFailed_ = false;
    std::vector<Item> items_;
    std::u16string searchValue_, itemNameSource_;
    lcars::RichText itemNoteSource_;
    std::string noteColorValue_ = "default";  // the color last applied from the dropdown above the note
    std::function<void()> noteColorChanged_;
    std::function<void()> itemNameChanged_, itemNoteChanged_;
    std::optional<lcars::OptionValue> selection_;
    std::function<void()> searchChanged_, selectionChanged_;
    lcars::LabelHandle itemsCount_;
    lcars::LabelHandle itemTitle_, itemNameLabel_, itemNameError_, itemNoteLabel_, deleteTitle_, deleteMessage_;
    bool editing_ = false;
    int nextItemId_ = 21;
    lcars::ButtonHandle itemCancel_, deleteCancel_, deleteConfirm_;
    lcars::Result refreshItems();
    lcars::Result saveDocument();
    SettingsDocument snapshot() const;
    lcars::Result apply(const SettingsDocument&);  // every part is applied; the first failure is returned
    void dirty(bool coalesce = false);
    std::optional<SettingsDocument> current_, saved_;
    std::vector<SettingsDocument> undoHistory_, redoHistory_;
    bool savedOnce_ = false, dirtyState_ = false;
    std::function<void()> textSizeChanged_, darkChanged_, compactChanged_, justifyLeftChanged_;
    lcars::LabelHandle preview_, fontStatus_;
    std::function<void()> fontChanged_;
    void refreshFonts();
    lcars::ButtonHandle resetAppearance_;
    std::string accentValue_ = "default", focusColorValue_ = "default";
    std::function<void()> accentChanged_, focusColorChanged_;
    // One palette override list stands for both choices (setPaletteOverrides replaces the whole list).
    lcars::Result applyPalette(const std::string& accentId, const std::string& focusId);
    lcars::Result applyAccent(const std::string& id);
    lcars::Result applyFocusColor(const std::string& id);
    // The focus ring controls (#90): the callbacks that tell each control's data source the presentation changed.
    std::function<void()> focusModeChanged_, focusWidthChanged_, focusRadiusSameChanged_, focusRadiusChanged_;
    std::optional<int> focusRadiusMemory_;  // the radius the user last chose (or loaded): what unchecking "same as the component" brings back
    int focusRadiusStart() const;           // memory, else the bound appearance's own radius (Web 6 when none is bound): unchecking changes nothing drawn
    lcars::Result presentationChanged();   // every control that mirrors the presentation re-reads it; the first control that cannot be updated is reported
    lcars::Result refreshFocusControls(); // with the indicator off, the ring's width, radius and color controls are disabled (values kept)
    std::function<void(bool)> dirtyChanged_;
    void updateHistoryControls();
    // A slider drag is one undo step: while a slider keeps the focus its changes join the first one. dragGroup_ is the id of the
    // slider whose changes are joining (empty = none), so dirty(false) and apply() end every slider's group with one assignment.
    std::string dragGroup_;
    void connectSlider(lcars::SliderHandle& slider, const char* id, std::function<lcars::Result(const lcars::Change<lcars::SliderHandle>&, bool join)> change);
public:
    lcars::Ui ui;
    lcars::ScrollState scroll;
    mutable lcars::ScrollState licenseScroll;
    lcars::NavigationListHandle nav;
    lcars::TextFieldHandle name, itemsSearch;
    lcars::ListHandle itemsList;
    lcars::ContextMenuHandle itemsMenu;  // Edit / Delete on every row
    lcars::ListHandle licenses;
    lcars::ButtonHandle itemsAdd, itemsEdit, itemsDelete, itemOk;
    lcars::ButtonHandle itemNoteBold, itemNoteItalic, itemNoteUnderline;  // style buttons above the note
    lcars::DropdownHandle itemNoteColor;
    lcars::TextFieldHandle itemName, itemNote;
    lcars::DialogHandle itemDialog, deleteDialog, closeDialog;
    lcars::ButtonHandle closeSave, closeDiscard, closeCancel;
    void onClose(std::function<void()> callback) { closeHandler_ = std::move(callback); }
    bool requestClose();
    bool isDirty() const;
    bool blankNameDraft() const;
    std::u16string saveFailedRenderer();
    lcars::CheckboxHandle notices, dark, compact, justifyLeft;  // justifyLeft: PresentationSettings::justifyAsLeft (#84)
    lcars::RadioGroupHandle mode;
    lcars::DropdownHandle region, font, accent;
    lcars::SliderHandle volume, textSize;
    // The focus ring (#90): when and how it shows. Width, radius and color are disabled while the indicator is off.
    lcars::DropdownHandle focusMode, focusColor;    // PresentationSettings::focusIndicator; the palette's `focus` role
    lcars::SliderHandle focusWidth, focusRadius;    // focusRingWidth (2-6 DIP); focusRingRadius (0-32 DIP, shown unless "same as the component")
    lcars::CheckboxHandle focusRadiusSame;          // checked = focusRingRadius left empty
    void onSave(std::function<std::u16string(const SettingsDocument&)> handler, std::u16string path = {}) { saveHandler_ = std::move(handler); settingsPath_ = std::move(path); }
    lcars::Result save() { return save_.invoke(); }
    lcars::Result loadDocument(const SettingsDocument&);  // a part that cannot be applied is also announced in the load notice
    void showLoadNotice(std::u16string title, std::u16string message);
    SettingsDocument document() const { return snapshot(); }
    void onDirtyChanged(std::function<void(bool)> callback);
    lcars::Result setPresentation(const lcars::PresentationSettings& settings, bool coalesce = false);
    // Accent choices: "default" clears the palette overrides; "green" and "orange" override accent,
    // onAccent and selection for every linked built-in appearance in light and dark.
    lcars::Result setAccent(const std::string& id);
    const std::string& accentId() const { return accentValue_; }
    // The palette overrides one accent choice stands for (appearances that are not linked are skipped).
    static std::vector<lcars::PaletteOverride> accentOverrides(const std::string& id);
    // Ring colors: "default" overrides nothing (the ring follows the accent); "green" and "orange" override `focus`
    // for every linked built-in appearance in light and dark, each passing the 3:1 rule on every surface.
    lcars::Result setFocusColor(const std::string& id);
    const std::string& focusColorId() const { return focusColorValue_; }
    static std::vector<lcars::PaletteOverride> focusColorOverrides(const std::string& id);
    explicit Demo(AboutInfo about = {});
    void setRenderer(std::u16string value);
    lcars::layout::Node page() const;
};
}
