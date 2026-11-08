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
    void dirty();
public:
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::NavigationListHandle nav;
    lcars::TextHandle name;
    lcars::CheckboxHandle notices;
    lcars::RadioGroupHandle mode;
    lcars::DropdownHandle region;
    lcars::SliderHandle volume;
    Demo();
    lcars::layout::Node page() const;
};
}
