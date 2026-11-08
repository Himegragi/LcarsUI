#pragma once
#include "lcars/layout.hpp"
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
    bool operator==(const SettingsDocument&) const = default;
};
struct AboutLicense { lcars::OptionValue id; std::u16string name, text; };
struct AboutInfo {
    std::vector<AboutLicense> licenses;
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
    SettingsDocument snapshot() const;
    void apply(const SettingsDocument&);
    void dirty(bool coalesce = false);
    std::optional<SettingsDocument> current_, saved_;
    std::vector<SettingsDocument> undo_history_, redo_history_;
    bool saved_once_ = false, volume_group_ = false, dirty_state_ = false;
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
    lcars::DialogHandle item_dialog, delete_dialog;
    lcars::CheckboxHandle notices;
    lcars::RadioGroupHandle mode;
    lcars::DropdownHandle region;
    lcars::SliderHandle volume;
    void on_dirty_changed(std::function<void(bool)> callback);
    lcars::Result set_presentation(const lcars::PresentationSettings& settings);
    explicit Demo(AboutInfo about = {});
    void set_renderer(std::u16string value);
    lcars::layout::Node page() const;
};
}
