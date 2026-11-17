#pragma once
// V01 minimal edit form. Application code: it owns the data, the validation policy and the
// apply action, and assembles C01-C03 only through public Isoduo headers. Logical IDs come
// from the caller so the framework never depends on these names.
#include "isoduo/core.hpp"
#include "isoduo/fonts.hpp"
#include "isoduo/layout.hpp"
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
    std::optional<isoduo::OptionValue> modeApplied_ = isoduo::OptionValue{"normal"};
    std::optional<isoduo::OptionValue> regionApplied_ = isoduo::OptionValue{"north"};
    bool regionEmpty_ = false;
    bool offlineDisabled_ = false;
    Mode mode_ = Mode::normal;
    bool longIntro_ = false;
    bool rejectNoticesConflict_ = false;
    std::optional<isoduo::ConfirmFieldError> nextFieldError_;
    std::string clipPaddingId_;
    std::u16string clipPaddingText_;
    std::map<std::string, std::function<isoduo::Result()>, std::less<>> fixtureActions_;
    std::function<void()> nextApplyObserver_;
    std::function<isoduo::FontResolution(std::u16string_view)> resolveFont_;
    bool pages_ = false;
    std::array<int, 4> pageOffsets_{};
    std::function<void()> sourceChanged_, noticesChanged_, modeChanged_, volumeChanged_, regionChanged_, progressChanged_, fontChanged_, textSizeChanged_, darkChanged_, justifyAlignChanged_, justifyMethodChanged_;
public:
    explicit Form(Ids ids = defaultIds(), bool pages = false,
                  std::function<isoduo::FontResolution(std::u16string_view)> resolveFont = {});
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;
    struct Handles {
        isoduo::TextFieldHandle name, preview;
        isoduo::SliderHandle textSize;
        isoduo::LabelHandle v04Title, fontStatus;
        // #84: a mixed-script paragraph with its alignment and justification-method dropdowns, and short labels to compare distribute with left.
        isoduo::LabelHandle justifySample, distributeName, distributePlain;
        isoduo::DropdownHandle justifyAlign, justifyMethod;
        isoduo::DropdownHandle font;
        isoduo::ButtonHandle resetV04;
        isoduo::CheckboxHandle dark;
        isoduo::TabsHandle tabs;
        isoduo::ButtonHandle resetV03;
        isoduo::ButtonHandle dialogOpen, dialogTemporary, dialogCancel, dialogConfirm;
        isoduo::DialogHandle dialog;
        isoduo::ButtonHandle progressZero, progressHalf, progressFull, progressBelow, progressAbove;
        isoduo::LabelHandle v03Title, progressSource, dialogTitle, dialogBody, dialogResult;
        isoduo::ProgressHandle progress;
        isoduo::TooltipHandle progressTip;
        isoduo::CheckboxHandle notices, compact;
        isoduo::RadioGroupHandle mode;
        isoduo::SliderHandle volume;
        isoduo::DropdownHandle region;
        isoduo::ButtonHandle normal, readOnly, disabled, error, longText, reset, apply, latest, volumeLatest, noticesLatest, modeLatest, regionLatest, offlineDisabled, regionEmpty;
        isoduo::LabelHandle title, intro, nameLabel, nameHelp, nameError, state, draft, result, count;
    } handles;
    // Page model must outlive Ui subscriptions and their disconnect callbacks.
    std::unique_ptr<listPage::Page> list;
    isoduo::Ui ui;
    isoduo::ScrollState scroll;
    isoduo::layout::Node page() const;
    const Ids& ids() const { return ids_; }
    const std::u16string& applied() const { return applied_; }
    int applies() const { return applies_; }
    int volumeApplied() const { return volumeApplied_; }
    int progress() const { return progress_; }
    int dialogConfirmations() const { return dialogConfirmations_; }
    bool noticesApplied() const { return noticesApplied_; }
    std::optional<isoduo::OptionValue> modeApplied() const { return modeApplied_; }
    std::optional<isoduo::OptionValue> regionApplied() const { return regionApplied_; }
    // An external application change to the same model (also useful to exercise conflicts).
    void updateSource(std::u16string value);
    void updateVolumeSource(int value);
    void updateModeSource(isoduo::OptionValue value);
    void updateRegionSource(isoduo::OptionValue value);
    void updateProgress(int value);
    // Restore the application's original action after a temporary typed-handle binding.
    isoduo::Result restoreFixtureAction(isoduo::ButtonHandle button) const;
    // Fixture: observe one invocation inside the application's confirmation modification entry.
    void observeNextApply(std::function<void()> observer) { nextApplyObserver_ = std::move(observer); }
    // Fixture: expand one existing label to exercise scroll clipping, then restore its model source.
    isoduo::Result setClipPadding(isoduo::LabelHandle label, std::u16string text);
    void clearClipPadding();
    // Fixture: reject the next apply after a concurrent boolean-source update and draft edit.
    void rejectNextApplyWithNoticesSourceChange() { rejectNoticesConflict_ = true; }
    // Fixture: reject one otherwise valid apply with an application-supplied field error.
    void rejectNextApplyFor(isoduo::ConfirmField field, std::u16string message) {
        nextFieldError_ = isoduo::ConfirmFieldError{std::move(field), std::move(message)};
    }
private:
    isoduo::Result bindAction(isoduo::ButtonHandle button, std::function<isoduo::Result()> action);
    bool validate();
    isoduo::ConfirmationResult apply(const std::vector<isoduo::ConfirmFieldChange>& changes);
    void refreshConflict();
    void setMode(Mode mode);
    isoduo::Result reset();
};}
