#pragma once
#include "lcars/layout.hpp"
#include "lcars/fonts.hpp"
namespace settings_demo {
struct Item { lcars::OptionValue id; std::u16string name, note; bool operator==(const Item&) const = default; };
struct SettingsDocument {
    std::u16string name;
    bool notices;
    std::optional<lcars::OptionValue> mode, region;
    int volume;
    std::vector<Item> items;
    int next_item_id;
    lcars::PresentationSettings presentation;
    std::string accent = "default";  // "default", "green" or "orange": the palette override set chosen on the appearance page
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
    std::u16string name_value_ = u"測試使用者";
    bool notices_value_ = true;
    std::optional<lcars::OptionValue> mode_value_ = lcars::OptionValue{"normal"}, region_value_ = lcars::OptionValue{"north"};
    int volume_value_ = 40;
    std::function<void()> name_changed_, notices_changed_, mode_changed_, region_changed_, volume_changed_;
    lcars::LabelHandle title_, status_, name_label_, name_help_, name_error_;
    std::vector<lcars::LabelHandle> about_labels_;
    lcars::LabelHandle license_text_;
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
    std::u16string search_value_, item_name_source_, item_note_source_;
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
    bool saved_once_ = false, volume_group_ = false, text_size_group_ = false, dirty_state_ = false;
    std::function<void()> text_size_changed_, dark_changed_, compact_changed_;
    lcars::LabelHandle preview_, font_status_;
    std::function<void()> font_changed_;
    void refresh_fonts();
    lcars::ButtonHandle reset_appearance_;
    std::string accent_value_ = "default";
    std::function<void()> accent_changed_;
    lcars::Result apply_accent(const std::string& id);
    std::function<void(bool)> dirty_changed_;
    void update_history_controls();
public:
    lcars::Ui ui;
    lcars::ScrollState scroll;
    mutable lcars::ScrollState license_scroll;
    lcars::NavigationListHandle nav;
    lcars::TextHandle name, items_search;
    lcars::ListHandle items_list;
    lcars::ListHandle licenses;
    lcars::ButtonHandle items_add, items_edit, items_delete, item_ok;
    lcars::TextHandle item_name, item_note;
    lcars::DialogHandle item_dialog, delete_dialog, close_dialog;
    lcars::ButtonHandle close_save, close_discard, close_cancel;
    void on_close(std::function<void()> callback) { close_handler_ = std::move(callback); }
    bool request_close();
    bool is_dirty() const;
    bool blank_name_draft() const;
    std::u16string save_failed_renderer();
    lcars::CheckboxHandle notices, dark, compact;
    lcars::RadioGroupHandle mode;
    lcars::DropdownHandle region, font, accent;
    lcars::SliderHandle volume, text_size;
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
    explicit Demo(AboutInfo about = {});
    void set_renderer(std::u16string value);
    lcars::layout::Node page() const;
};
}
