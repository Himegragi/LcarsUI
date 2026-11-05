#pragma once
// V01 minimal edit form. Application code: it owns the data, the validation policy and the
// apply action, and assembles C01-C03 only through public LcarsUI headers. Logical IDs come
// from the caller so the framework never depends on these names.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include <array>

namespace edit_form {
struct Ids {
    std::string title, intro;
    std::string normal, read_only, disabled, error, long_text, reset;  // recoverable test entries
    std::string name_label, name, name_help, name_error, apply;
    std::string notices, mode, volume, volume_latest, offline_disabled, region, region_empty;
    std::string state, draft, result, count, latest;                        // displayed results
};
Ids default_ids();

class Form {
private:
    enum class Mode { normal, read_only, disabled };
    Ids ids_;
    std::u16string applied_;
    int applies_ = 0;
    int volume_applied_ = 40;
    bool notices_applied_ = true;
    std::optional<lcars::OptionValue> mode_applied_ = lcars::OptionValue{"normal"};
    std::optional<lcars::OptionValue> region_applied_ = lcars::OptionValue{"north"};
    bool region_empty_ = false;
    bool offline_disabled_ = false;
    Mode mode_ = Mode::normal;
    bool long_intro_ = false;
    bool pages_ = false, sample_selected_ = false;
    std::u16string search_source_;
    std::array<int, 3> page_offsets_{};
    std::function<void()> source_changed_, notices_changed_, mode_changed_, volume_changed_, region_changed_;
public:
    explicit Form(Ids ids = default_ids(), bool pages = false);
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;
    struct Handles {
        lcars::TextHandle name, search;
        lcars::TabsHandle tabs;
        lcars::ButtonHandle choose_sample, reset_v02, reset_v03;
        lcars::LabelHandle v02_title, v02_result, v03_title;
        lcars::CheckboxHandle notices;
        lcars::RadioGroupHandle mode;
        lcars::SliderHandle volume;
        lcars::DropdownHandle region;
        lcars::ButtonHandle normal, read_only, disabled, error, long_text, reset, apply, latest, volume_latest, offline_disabled, region_empty;
        lcars::LabelHandle title, intro, name_label, name_help, name_error, state, draft, result, count;
    } handles;
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::layout::Node page() const;
    const Ids& ids() const { return ids_; }
    const std::u16string& applied() const { return applied_; }
    int applies() const { return applies_; }
    bool sample_selected() const { return sample_selected_; }
    int volume_applied() const { return volume_applied_; }
    bool notices_applied() const { return notices_applied_; }
    std::optional<lcars::OptionValue> mode_applied() const { return mode_applied_; }
    std::optional<lcars::OptionValue> region_applied() const { return region_applied_; }
    // An external application change to the same model (also useful to exercise conflicts).
    void update_source(std::u16string value);
    void update_volume_source(int value);
private:
    bool validate();
    lcars::MixedConfirmationResult apply(const std::vector<lcars::FieldChange>& changes);
    void refresh_conflict();
    void set_mode(Mode mode);
    lcars::Result reset();
};}
