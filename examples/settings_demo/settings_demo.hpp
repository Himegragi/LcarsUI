#pragma once
#include "lcars/layout.hpp"
namespace settings_demo {
class Demo {
    std::u16string name_value_ = u"測試使用者";
    bool notices_value_ = true;
    std::optional<lcars::OptionValue> mode_value_ = lcars::OptionValue{"normal"}, region_value_ = lcars::OptionValue{"north"};
    int volume_value_ = 40;
    std::function<void()> name_changed_, notices_changed_, mode_changed_, region_changed_, volume_changed_;
    lcars::LabelHandle title_, status_, name_label_, name_help_, name_error_;
    lcars::ButtonHandle undo_, redo_, revert_, save_;
    struct Item { lcars::OptionValue id; std::u16string name, note; };
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
    void dirty();
public:
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::NavigationListHandle nav;
    lcars::TextHandle name, items_search;
    lcars::ListHandle items_list;
    lcars::ButtonHandle items_add, items_edit, items_delete, item_ok;
    lcars::TextHandle item_name, item_note;
    lcars::DialogHandle item_dialog, delete_dialog;
    lcars::CheckboxHandle notices;
    lcars::RadioGroupHandle mode;
    lcars::DropdownHandle region;
    lcars::SliderHandle volume;
    Demo();
    lcars::layout::Node page() const;
};
}
