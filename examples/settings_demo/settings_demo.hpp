#pragma once
#include "lcars/layout.hpp"
#include "lcars/fonts.hpp"
namespace settings_demo {
// note and note_spans together are the item's rich text description (lcars::RichText): the text and its style spans.
struct Item { lcars::OptionValue id; std::u16string name, note; std::vector<lcars::StyleSpan> note_spans; bool operator==(const Item&) const = default; };
struct SettingsDocument {
    std::u16string name;
    bool notices;
    std::optional<lcars::OptionValue> mode, region;
    int volume;
    std::vector<Item> items;
    int next_item_id;
    lcars::PresentationSettings presentation;
    std::string accent = "default";  // "default", "green" or "orange": the palette override set chosen on the appearance page
    std::string focus_color = "default";  // "default" (the ring follows the accent), "green" or "orange": overrides the palette's `focus` role
    bool operator==(const SettingsDocument&) const = default;
};
struct AboutLicense { lcars::OptionValue id; std::u16string name, text; };
struct AboutInfo {
    bool diagnostics = false;
    std::vector<AboutLicense> licenses;
    std::vector<lcars::FontFamily> fonts;
    std::function<lcars::FontResolution(const lcars::PresentationSettings&)> resolve_font;
    std::u16string appearance, renderer, settings_path, build;
};
class Demo {
public:
    lcars::LabelHandle about_vertical() const { return about_vertical_; }
private:
    std::u16string name_value_ = u"測試使用者";
    bool notices_value_ = true;
    std::optional<lcars::OptionValue> mode_value_ = lcars::OptionValue{"normal"}, region_value_ = lcars::OptionValue{"north"};
    int volume_value_ = 40;
    std::function<void()> name_changed_, notices_changed_, mode_changed_, region_changed_, volume_changed_;
    lcars::LabelHandle title_, status_, name_label_, name_help_, name_error_;
    std::vector<lcars::LabelHandle> about_labels_;
    lcars::LabelHandle license_text_;
    lcars::LabelHandle about_vertical_;  // vertical writing sample (display only)
    AboutInfo about_;
    std::optional<lcars::OptionValue> license_selection_ = lcars::OptionValue{"lcars"};
    std::function<void()> license_selection_changed_;
    lcars::ButtonHandle undo_, redo_, revert_, save_;
    lcars::LabelHandle close_title_, close_message_;
    std::function<void()> close_handler_;
    lcars::LabelHandle load_title_, load_message_, error_title_, error_message_;
    lcars::ButtonHandle load_ok_, error_ok_;
    lcars::DialogHandle load_dialog_, error_dialog_;
    std::function<std::u16string(const SettingsDocument&)> save_handler_;
    std::u16string settings_path_;
    bool save_failed_ = false;
    std::vector<Item> items_;
    std::u16string search_value_, item_name_source_;
    lcars::RichText item_note_source_;
    std::string note_color_value_ = "default";  // the color last applied from the dropdown above the note
    std::function<void()> note_color_changed_;
    std::function<void()> item_name_changed_, item_note_changed_;
    std::optional<lcars::OptionValue> selection_;
    std::function<void()> search_changed_, selection_changed_;
    lcars::LabelHandle items_count_;
    lcars::LabelHandle item_title_, item_name_label_, item_name_error_, item_note_label_, delete_title_, delete_message_;
    bool editing_ = false;
    int next_item_id_ = 21;
    lcars::ButtonHandle item_cancel_, delete_cancel_, delete_confirm_;
    lcars::Result refresh_items();
    lcars::Result save_document();
    SettingsDocument snapshot() const;
    void apply(const SettingsDocument&);
    void dirty(bool coalesce = false);
    std::optional<SettingsDocument> current_, saved_;
    std::vector<SettingsDocument> undo_history_, redo_history_;
    bool saved_once_ = false, dirty_state_ = false;
    std::function<void()> text_size_changed_, dark_changed_, compact_changed_, justify_left_changed_;
    lcars::LabelHandle preview_, font_status_;
    std::function<void()> font_changed_;
    void refresh_fonts();
    lcars::ButtonHandle reset_appearance_;
    std::string accent_value_ = "default", focus_color_value_ = "default";
    std::function<void()> accent_changed_, focus_color_changed_;
    // One palette override list stands for both choices (set_palette_overrides replaces the whole list).
    lcars::Result apply_palette(const std::string& accent_id, const std::string& focus_id);
    lcars::Result apply_accent(const std::string& id);
    lcars::Result apply_focus_color(const std::string& id);
    // The focus ring controls (#90): the callbacks that tell each control's data source the presentation changed.
    std::function<void()> focus_mode_changed_, focus_width_changed_, focus_radius_same_changed_, focus_radius_changed_;
    int focus_radius_memory_ = 6;  // the radius the slider shows again after "same as the component" was unchecked
    void presentation_changed();   // every control that mirrors the presentation re-reads it
    void refresh_focus_controls(); // with the indicator off, the ring's width, radius and color controls are disabled (values kept)
    std::function<void(bool)> dirty_changed_;
    void update_history_controls();
    // A slider drag is one undo step: while a slider keeps the focus its changes join the first one. drag_group_ is the id of the
    // slider whose changes are joining (empty = none), so dirty(false) and apply() end every slider's group with one assignment.
    std::string drag_group_;
    void connect_slider(lcars::SliderHandle& slider, const char* id, std::function<lcars::Result(const lcars::NumericChange&, bool join)> change);
public:
    lcars::Ui ui;
    lcars::ScrollState scroll;
    mutable lcars::ScrollState license_scroll;
    lcars::NavigationListHandle nav;
    lcars::TextHandle name, items_search;
    lcars::ListHandle items_list;
    lcars::ContextMenuHandle items_menu;  // Edit / Delete on every row
    lcars::ListHandle licenses;
    lcars::ButtonHandle items_add, items_edit, items_delete, item_ok;
    lcars::ButtonHandle item_note_bold, item_note_italic, item_note_underline;  // style buttons above the note
    lcars::DropdownHandle item_note_color;
    lcars::TextHandle item_name, item_note;
    lcars::DialogHandle item_dialog, delete_dialog, close_dialog;
    lcars::ButtonHandle close_save, close_discard, close_cancel;
    void on_close(std::function<void()> callback) { close_handler_ = std::move(callback); }
    bool request_close();
    bool is_dirty() const;
    bool blank_name_draft() const;
    std::u16string save_failed_renderer();
    lcars::CheckboxHandle notices, dark, compact, justify_left;  // justify_left: PresentationSettings::justify_as_left (#84)
    lcars::RadioGroupHandle mode;
    lcars::DropdownHandle region, font, accent;
    lcars::SliderHandle volume, text_size;
    // The focus ring (#90): when and how it shows. Width, radius and color are disabled while the indicator is off.
    lcars::DropdownHandle focus_mode, focus_color;    // PresentationSettings::focus_indicator; the palette's `focus` role
    lcars::SliderHandle focus_width, focus_radius;    // focus_ring_width (2-6 DIP); focus_ring_radius (0-32 DIP, shown unless "same as the component")
    lcars::CheckboxHandle focus_radius_same;          // checked = focus_ring_radius left empty
    void on_save(std::function<std::u16string(const SettingsDocument&)> handler, std::u16string path = {}) { save_handler_ = std::move(handler); settings_path_ = std::move(path); }
    lcars::Result save() { return save_.invoke(); }
    void load_document(const SettingsDocument&);
    void show_load_notice(std::u16string title, std::u16string message);
    SettingsDocument document() const { return snapshot(); }
    void on_dirty_changed(std::function<void(bool)> callback);
    lcars::Result set_presentation(const lcars::PresentationSettings& settings, bool coalesce = false);
    // Accent choices: "default" clears the palette overrides; "green" and "orange" override accent,
    // on_accent and selection for every linked built-in appearance in light and dark.
    lcars::Result set_accent(const std::string& id);
    const std::string& accent_id() const { return accent_value_; }
    // The palette overrides one accent choice stands for (appearances that are not linked are skipped).
    static std::vector<lcars::PaletteOverride> accent_overrides(const std::string& id);
    // Ring colors: "default" overrides nothing (the ring follows the accent); "green" and "orange" override `focus`
    // for every linked built-in appearance in light and dark, each passing the 3:1 rule on every surface.
    lcars::Result set_focus_color(const std::string& id);
    const std::string& focus_color_id() const { return focus_color_value_; }
    static std::vector<lcars::PaletteOverride> focus_color_overrides(const std::string& id);
    explicit Demo(AboutInfo about = {});
    void set_renderer(std::u16string value);
    lcars::layout::Node page() const;
};
}
