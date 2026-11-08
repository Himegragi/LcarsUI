#pragma once
// V01 minimal edit form. Application code: it owns the data, the validation policy and the
// apply action, and assembles C01-C03 only through public LcarsUI headers. Logical IDs come
// from the caller so the framework never depends on these names.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include "list_page.hpp"
#include <array>
#include <map>

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
    std::u16string preview_ = u"\u7e41\u4e2d English \U0001f600 \U00020000 \u2764\ufe0f";
    int applies_ = 0;
    int volume_applied_ = 40;
    int progress_ = 50;
    int dialog_confirmations_ = 0;
    std::u16string dialog_result_ = u"No result";
    bool notices_applied_ = true;
    std::optional<lcars::OptionValue> mode_applied_ = lcars::OptionValue{"normal"};
    std::optional<lcars::OptionValue> region_applied_ = lcars::OptionValue{"north"};
    bool region_empty_ = false;
    bool offline_disabled_ = false;
    Mode mode_ = Mode::normal;
    bool long_intro_ = false;
    bool reject_notices_conflict_ = false;
    std::optional<lcars::MixedFieldError> next_field_error_;
    std::string clip_padding_id_;
    std::u16string clip_padding_text_;
    std::map<std::string, std::function<lcars::Result()>, std::less<>> fixture_actions_;
    std::function<void()> next_apply_observer_;
    bool pages_ = false;
    std::array<int, 4> page_offsets_{};
    std::function<void()> source_changed_, notices_changed_, mode_changed_, volume_changed_, region_changed_, progress_changed_;
public:
    explicit Form(Ids ids = default_ids(), bool pages = false);
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;
    struct Handles {
        lcars::TextHandle name, preview;
        lcars::SliderHandle text_size;
        lcars::LabelHandle v04_title;
        lcars::TabsHandle tabs;
        lcars::ButtonHandle reset_v03;
        lcars::ButtonHandle dialog_open, dialog_temporary, dialog_cancel, dialog_confirm;
        lcars::DialogHandle dialog;
        lcars::ButtonHandle progress_zero, progress_half, progress_full, progress_below, progress_above;
        lcars::LabelHandle v03_title, progress_source, dialog_title, dialog_body, dialog_result;
        lcars::ProgressHandle progress;
        lcars::TooltipHandle progress_tip;
        lcars::CheckboxHandle notices, compact;
        lcars::RadioGroupHandle mode;
        lcars::SliderHandle volume;
        lcars::DropdownHandle region;
        lcars::ButtonHandle normal, read_only, disabled, error, long_text, reset, apply, latest, volume_latest, notices_latest, mode_latest, region_latest, offline_disabled, region_empty;
        lcars::LabelHandle title, intro, name_label, name_help, name_error, state, draft, result, count;
    } handles;
    // Page model must outlive Ui subscriptions and their disconnect callbacks.
    std::unique_ptr<list_page::Page> list;
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::layout::Node page() const;
    const Ids& ids() const { return ids_; }
    const std::u16string& applied() const { return applied_; }
    int applies() const { return applies_; }
    int volume_applied() const { return volume_applied_; }
    int progress() const { return progress_; }
    int dialog_confirmations() const { return dialog_confirmations_; }
    bool notices_applied() const { return notices_applied_; }
    std::optional<lcars::OptionValue> mode_applied() const { return mode_applied_; }
    std::optional<lcars::OptionValue> region_applied() const { return region_applied_; }
    // An external application change to the same model (also useful to exercise conflicts).
    void update_source(std::u16string value);
    void update_volume_source(int value);
    void update_mode_source(lcars::OptionValue value);
    void update_region_source(lcars::OptionValue value);
    void update_progress(int value);
    // Restore the application's original action after a temporary typed-handle binding.
    lcars::Result restore_fixture_action(lcars::ButtonHandle button) const;
    // Fixture: observe one invocation inside the application's confirmation modification entry.
    void observe_next_apply(std::function<void()> observer) { next_apply_observer_ = std::move(observer); }
    // Fixture: expand one existing label to exercise scroll clipping, then restore its model source.
    lcars::Result set_clip_padding(lcars::LabelHandle label, std::u16string text);
    void clear_clip_padding();
    // Fixture: reject the next apply after a concurrent boolean-source update and draft edit.
    void reject_next_apply_with_notices_source_change() { reject_notices_conflict_ = true; }
    // Fixture: reject one otherwise valid apply with an application-supplied field error.
    void reject_next_apply_for(lcars::ConfirmField field, std::u16string message) {
        next_field_error_ = lcars::MixedFieldError{std::move(field), std::move(message)};
    }
private:
    lcars::Result bind_action(lcars::ButtonHandle button, std::function<lcars::Result()> action);
    bool validate();
    lcars::MixedConfirmationResult apply(const std::vector<lcars::FieldChange>& changes);
    void refresh_conflict();
    void set_mode(Mode mode);
    lcars::Result reset();
};}
