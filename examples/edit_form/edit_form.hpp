#pragma once
// V01 minimal edit form. Application code: it owns the data, the validation policy and the
// apply action, and assembles C01-C03 only through public LcarsUI headers. Logical IDs come
// from the caller so the framework never depends on these names.
#include "lcars/core.hpp"
#include "lcars/fonts.hpp"
#include "lcars/layout.hpp"
#include "list_page.hpp"
#include <array>
#include <map>

namespace editForm {
struct Ids {
    std::string title, intro;
    std::string normal, readOnly, disabled, error, longText, reset;  // recoverable test entries
    std::string nameLabel, name, nameHelp, nameError, apply;
    std::string notices, mode, volume, volumeLatest, offlineDisabled, region, regionEmpty;
    std::string state, draft, result, count, latest;                        // displayed results
};
Ids defaultIds();

class Form {
private:
    enum class Mode { normal, readOnly, disabled };
    Ids ids_;
    std::u16string applied_;
    std::u16string preview_ = u"\u7e41\u4e2d English \U0001f600 \U00020000 \u2764\ufe0f";
    int applies_ = 0;
    int volumeApplied_ = 40;
    int progress_ = 50;
    int dialogConfirmations_ = 0;
    std::u16string dialogResult_ = u"No result";
    bool noticesApplied_ = true;
    std::optional<lcars::OptionValue> modeApplied_ = lcars::OptionValue{"normal"};
    std::optional<lcars::OptionValue> regionApplied_ = lcars::OptionValue{"north"};
    bool regionEmpty_ = false;
    bool offlineDisabled_ = false;
    Mode mode_ = Mode::normal;
    bool longIntro_ = false;
    bool rejectNoticesConflict_ = false;
    std::optional<lcars::ConfirmFieldError> nextFieldError_;
    std::string clipPaddingId_;
    std::u16string clipPaddingText_;
    std::map<std::string, std::function<lcars::Result()>, std::less<>> fixtureActions_;
    std::function<void()> nextApplyObserver_;
    std::function<lcars::FontResolution(std::u16string_view)> resolveFont_;
    bool pages_ = false;
    std::array<int, 4> pageOffsets_{};
    std::function<void()> sourceChanged_, noticesChanged_, modeChanged_, volumeChanged_, regionChanged_, progressChanged_, fontChanged_, textSizeChanged_, darkChanged_, justifyAlignChanged_, justifyMethodChanged_;
public:
    explicit Form(Ids ids = defaultIds(), bool pages = false,
                  std::function<lcars::FontResolution(std::u16string_view)> resolveFont = {});
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;
    struct Handles {
        lcars::TextFieldHandle name, preview;
        lcars::SliderHandle textSize;
        lcars::LabelHandle v04Title, fontStatus;
        // #84: a mixed-script paragraph with its alignment and justification-method dropdowns, and short labels to compare distribute with left.
        lcars::LabelHandle justifySample, distributeName, distributePlain;
        lcars::DropdownHandle justifyAlign, justifyMethod;
        lcars::DropdownHandle font;
        lcars::ButtonHandle resetV04;
        lcars::CheckboxHandle dark;
        lcars::TabsHandle tabs;
        lcars::ButtonHandle resetV03;
        lcars::ButtonHandle dialogOpen, dialogTemporary, dialogCancel, dialogConfirm;
        lcars::DialogHandle dialog;
        lcars::ButtonHandle progressZero, progressHalf, progressFull, progressBelow, progressAbove;
        lcars::LabelHandle v03Title, progressSource, dialogTitle, dialogBody, dialogResult;
        lcars::ProgressHandle progress;
        lcars::TooltipHandle progressTip;
        lcars::CheckboxHandle notices, compact;
        lcars::RadioGroupHandle mode;
        lcars::SliderHandle volume;
        lcars::DropdownHandle region;
        lcars::ButtonHandle normal, readOnly, disabled, error, longText, reset, apply, latest, volumeLatest, noticesLatest, modeLatest, regionLatest, offlineDisabled, regionEmpty;
        lcars::LabelHandle title, intro, nameLabel, nameHelp, nameError, state, draft, result, count;
    } handles;
    // Page model must outlive Ui subscriptions and their disconnect callbacks.
    std::unique_ptr<listPage::Page> list;
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::layout::Node page() const;
    const Ids& ids() const { return ids_; }
    const std::u16string& applied() const { return applied_; }
    int applies() const { return applies_; }
    int volumeApplied() const { return volumeApplied_; }
    int progress() const { return progress_; }
    int dialogConfirmations() const { return dialogConfirmations_; }
    bool noticesApplied() const { return noticesApplied_; }
    std::optional<lcars::OptionValue> modeApplied() const { return modeApplied_; }
    std::optional<lcars::OptionValue> regionApplied() const { return regionApplied_; }
    // An external application change to the same model (also useful to exercise conflicts).
    void updateSource(std::u16string value);
    void updateVolumeSource(int value);
    void updateModeSource(lcars::OptionValue value);
    void updateRegionSource(lcars::OptionValue value);
    void updateProgress(int value);
    // Restore the application's original action after a temporary typed-handle binding.
    lcars::Result restoreFixtureAction(lcars::ButtonHandle button) const;
    // Fixture: observe one invocation inside the application's confirmation modification entry.
    void observeNextApply(std::function<void()> observer) { nextApplyObserver_ = std::move(observer); }
    // Fixture: expand one existing label to exercise scroll clipping, then restore its model source.
    lcars::Result setClipPadding(lcars::LabelHandle label, std::u16string text);
    void clearClipPadding();
    // Fixture: reject the next apply after a concurrent boolean-source update and draft edit.
    void rejectNextApplyWithNoticesSourceChange() { rejectNoticesConflict_ = true; }
    // Fixture: reject one otherwise valid apply with an application-supplied field error.
    void rejectNextApplyFor(lcars::ConfirmField field, std::u16string message) {
        nextFieldError_ = lcars::ConfirmFieldError{std::move(field), std::move(message)};
    }
private:
    lcars::Result bindAction(lcars::ButtonHandle button, std::function<lcars::Result()> action);
    bool validate();
    lcars::ConfirmationResult apply(const std::vector<lcars::ConfirmFieldChange>& changes);
    void refreshConflict();
    void setMode(Mode mode);
    lcars::Result reset();
};}
