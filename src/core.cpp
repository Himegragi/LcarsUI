#include "core_internal.hpp"
#include "component_type.hpp"
#include "utf.hpp"
#include "rich_text.hpp"
#include <stdexcept>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace lcars {
namespace {
#ifndef NDEBUG
void outputDiagnostic(const std::string& message) {
#ifdef _WIN32
    auto utf16 = detail::utf16FromUtf8(message);
    std::wstring wide(utf16.begin(), utf16.end()); OutputDebugStringW(wide.c_str());
#else
    std::fputs(message.c_str(), stderr);
#endif
}
#endif
// Committed text from typing or an IME: control characters (Enter, Tab, Ctrl+letter) never insert.
std::u16string withoutControls(std::u16string_view text) {
    std::u16string result;
    for (auto ch : text) if (ch >= 0x20 && ch != 0x7f) result += ch;
    return result;
}
}
namespace {
bool validText(std::u16string_view text, std::size_t maxLength, bool allowLineFeed) {
    if (text.size() > maxLength) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        if ((ch < 0x20 && !(allowLineFeed && ch == u'\n')) || ch == 0x7f) return false;
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (ch >= 0xdc00 && ch <= 0xdfff) return false;
    }
    return true;
}
// The value rule for one field: its kind decides whether a line feed is content.
bool validValue(const ComponentConfig& control, std::u16string_view text) { return validText(text, control.maxLength, control.multiline); }
// A multi-line box is at least one line tall; the bound only keeps layout arithmetic sane.
constexpr unsigned maxVisibleLines = 1000;
bool validVisibleLines(const ComponentConfig& control) { return !control.multiline || (control.visibleLines >= 1 && control.visibleLines <= maxVisibleLines); }
// What a type says about itself; a ComponentKind no type is registered for says nothing.
bool supportsTextFlow(ComponentKind kind) { const auto* type = detail::findComponentType(kind); return type && type->supportsTextFlow(); }
bool validTextAlign(TextAlign align) {
    return align == TextAlign::left || align == TextAlign::center || align == TextAlign::right || align == TextAlign::justify || align == TextAlign::distribute;
}
bool validJustifyMethod(JustifyMethod method) { return method == JustifyMethod::automatic || method == JustifyMethod::spaces || method == JustifyMethod::perCharacter; }
// Distributed alignment (the last line stretched too) is for labels: a field being typed in would respread its only line at every key.
bool admitsTextAlign(ComponentKind kind, TextAlign align) {
    const auto* type = detail::findComponentType(kind);
    return align != TextAlign::distribute || (type && type->admitsDistribute());
}
bool validTextFlow(const detail::Component& control) {
    if (!validTextAlign(control.textAlign) || !validJustifyMethod(control.justifyMethod) || !validLineSpacing(control.lineSpacing) || !validParagraphSpacing(control.paragraphSpacing)) return false;
    if (!admitsTextAlign(control.kind, control.textAlign)) return false;
    return supportsTextFlow(control.kind) || (control.textAlign == TextAlign::left && control.justifyMethod == JustifyMethod::automatic && control.lineSpacing == 1.0 && control.paragraphSpacing == 0);
}
// A rich field is a multi-line text field; the option exists for no other control.
bool validRichOption(const detail::Component& control) {
    const auto* type = detail::findComponentType(control.kind);
    return !control.rich || (type && type->supportsRichText() && control.multiline);
}
}
bool detail::validTextControl(const Component& control) {
    return validValue(control, control.value) && validVisibleLines(control) && validRichOption(control);
}
bool validSingleLine(std::u16string_view text, std::size_t maxLength) { return validText(text, maxLength, false); }
bool validMultiLine(std::u16string_view text, std::size_t maxLength) { return validText(text, maxLength, true); }
void EditSession::select(unsigned anchor, unsigned caret) {
    if (composing) cancelComposition();
    auto clamp = [&](unsigned pos) {
        pos = std::min(pos, static_cast<unsigned>(text.size()));
        if (pos && pos < text.size() && text[pos] >= 0xdc00 && text[pos] <= 0xdfff &&
            text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff) --pos;
        return pos;
    };
    selection = {clamp(anchor), clamp(caret)};
    caretUpstream = false;
    ++selectionChanges;
}
void EditSession::startComposition() {
    if (composing) return;
    composing = true; preedit.clear(); preeditCaret = 0;
}
void EditSession::updateComposition(std::u16string value, unsigned caret) {
    startComposition(); preedit = std::move(value);
    preeditCaret = std::min(caret, static_cast<unsigned>(preedit.size()));
}
void EditSession::cancelComposition() { composing = false; preedit.clear(); preeditCaret = 0; }
std::u16string EditSession::visible() const {
    auto value = text;
    if (composing) value.replace(begin(), end() - begin(), preedit);
    return value;
}
namespace {
// A handle query that reads one member of the handle's own description: the description's failure, or the member.
template <class Value, class Owner> Query<Value> semanticProperty(const Query<ComponentSnapshot>& semantics, Value Owner::*member) {
    if (!semantics) return {semantics.status, semantics.message, std::nullopt};
    return {Status::ok, {}, (*semantics).*member};
}
}
detail::UiState::OperationScope::OperationScope(UiState* state)
    : state_(state && std::this_thread::get_id() == state->owner_ ? state : nullptr) {
    if (state_ && ++state_->operationDepth_ == 1 && !state_->semanticSubscribers_.empty() &&
        !state_->semanticInspecting_ && !state_->committingRemovals_) {
        state_->semanticInspecting_ = true;
        state_->semanticBefore_ = state_->semanticSnapshot();
        state_->semanticInspecting_ = false;
    }
}
detail::UiState::OperationScope::~OperationScope() {
    if (state_ && --state_->operationDepth_ == 0 && !state_->committingRemovals_) {
        state_->commitRemovals();
        if (!state_->semanticSubscribers_.empty() && !state_->semanticInspecting_) {
            state_->semanticInspecting_ = true;
            state_->semanticDiff();
            state_->semanticInspecting_ = false;
        }
    }
}
// The write paths the checkbox, radio group, dropdown and slider share (#94); the text field uses connectFieldWrite too.
template <class Connection>
Result detail::UiState::connectFieldWrite(Connection& field, std::function<Result(const typename Connection::Change&)> write, bool immediate) {
    if (!field.acceptsWrite(immediate) || !write) return {Status::invalidValue, u"Write needs an ungrouped connected source"};
    (immediate ? field.immediate : field.confirmed) = std::move(write);
    return {};
}
template <class Installed, class Subscribe>
Result detail::UiState::replaceSource(Installed& installed, std::function<void()>& disconnect, Installed source, Subscribe&& subscribe) {
    std::function<void()> subscription;
    try { subscription = subscribe(std::as_const(source)); } catch (...) { return {Status::failed, u"Subscription failed"}; }
    auto previous = std::exchange(disconnect, std::move(subscription));
    installed = std::move(source);
    if (previous) {
        // The old subscription may still be live after its cancel threw: nothing guarantees valid state any more.
        try { previous(); } catch (...) { stopped_ = true; return {Status::failed, u"Unsubscription failed"}; }
    }
    return {};
}
template <class Read> Result detail::UiState::readInstalledSource(Read&& read) {
    try { read(); } catch (...) { stopped_ = true; return {Status::failed, u"Source read failed"}; }
    return {};
}
// The handle's confirm(): hand the draft to the confirmed write; a refusal shows its message on the field and focuses it.
template <class Handle> Result detail::UiState::confirmField(const Handle& handle) {
    auto& f = fieldsOf<Handle>().at(handle.id_);
    if (!f.confirmed) return {Status::invalidValue, u"No confirmed write is connected"};
    if (f.submitting || dispatching_ || invoking_) return {Status::busy, u"Operation is already executing"};
    typename std::decay_t<decltype(f)>::Change change{handle, f.value, f.origin}; f.submitting = true; invoking_ = true;
    Result result;
    try { result = f.confirmed(change); }
    catch (...) { f.submitting = false; invoking_ = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    f.submitting = false; invoking_ = false;
    if (result) { f.origin = change.value; setError(handle.id_, u""); }
    else { setError(handle.id_, result.message); focus(handle.id_); }
    auto notification = notify(); return notification ? result : notification;
}
// An edit of the value: an immediate write runs now (unless one is already running); otherwise the change is only announced.
template <class FieldType> Result detail::UiState::writeImmediately(const Id& id, FieldType& field, const typename FieldType::Change& change) {
    if (!field.immediate || field.submitting) return notify();
    field.submitting = true;
    Result result;
    try { result = field.immediate(change); } catch (...) { field.submitting = false; stopped_ = true; return {Status::failed, u"Modification failed"}; }
    field.submitting = false;
    if (result) { field.origin = change.value; setError(id, u""); }
    else setError(id, result.message);
    auto notification = notify(); return notification ? result : notification;
}
bool detail::UiState::removalPending(std::string_view id) const {
    return retiring_ == id || std::find(pendingRemovals_.begin(), pendingRemovals_.end(), id) != pendingRemovals_.end();
}
void detail::UiState::commitRemovals() {
    if (pendingRemovals_.empty()) return;
    committingRemovals_ = true;
    while (!pendingRemovals_.empty()) {
        auto id = std::move(pendingRemovals_.back());
        pendingRemovals_.pop_back();
        if (!find(id)) continue;
        retiring_ = id;
        // Removal never asks the native IME to complete preedit or validates the discarded draft.
        if (composing_ == id) compositionCancel();
        blurCallbacks_.erase(id);
        if (focus_ == id) {
            try { moveFocus({}); } catch (...) { stopped_ = true; focus_.clear(); }
        }
        if (menu_ && menu_->target == id) closeMenu();
        menuAttachments_.erase(id);
        if (keyArmed_ == id) keyArmed_.clear();
        if (pointerArmed_ == id) { pointerArmed_.clear(); pointerOption_.reset(); pointerOptionValue_.reset(); sliderDrag_.reset(); }
        sliderTargets_.erase(id); animations_.cancel(sliderAnimationKey(id));
        if (tooltipHover_ == id) tooltipHover_.clear();
        for (auto& [key, dialog] : dialogs_) if (dialog.opener == id) dialog.opener.clear();
        if (auto member = pageMembers_.find(id); member != pageMembers_.end()) {
            auto& members = tabs_.at(member->second.first).pages[member->second.second].members;
            std::erase_if(members, [&](const ComponentHandle& handle) { return layoutId(handle) == id; });
            pageMembers_.erase(member);
        }
        if (auto group = confirmationMembers_.find(id); group != confirmationMembers_.end()) {
            for (const auto& field : group->second) withConnection(field, [](auto& connection) { connection.confirmationMember = false; });
            confirmationMembers_.erase(group);
        }
        for (auto group = confirmationMembers_.begin(); group != confirmationMembers_.end();) {
            std::erase(group->second, id);
            if (group->second.empty()) {
                actions_.erase(group->first);
                confirmationButtons_.erase(group->first);
                group = confirmationMembers_.erase(group);
            } else ++group;
        }
        if (auto tip = tooltipIds_.find(id); tip != tooltipIds_.end()) { overlayIds_.erase(tip->second); tooltipIds_.erase(tip); }
        actions_.erase(id); confirmationButtons_.erase(id); errors_.erase(id); tooltips_.erase(id); diagnosedNames_.erase(id);
        componentType(find(id)->kind).retire(*this, id);
        textScrolls_.erase(id); verticalScrolls_.erase(id); visibility_.erase(id);
        semanticBounds_.erase(id); semanticOptions_.erase(id); semanticPositions_.erase(id);
        std::erase(semanticOrder_, id);
        std::erase_if(controls_, [&](const Component& control) { return control.id == id; });
        semanticInstances_.erase(id);
        componentLifetimes_.erase(id);
        retiring_.clear();
    }
    committingRemovals_ = false;
    if (invalidate_) {
        try { invalidate_(); } catch (...) { stopped_ = true; }
    }
    if (!pendingRemovals_.empty() && operationDepth_ == 0) commitRemovals();
}
void detail::UiState::focusCuesChanged() {
    refreshTooltips();
    if (invalidate_) invalidate_();
}
void detail::UiState::pointerPressed() {
    if (!keyboardCues_) return;
    keyboardCues_ = false;
    focusCuesChanged();
}
void detail::UiState::setSystemKeyboardCues(bool enabled) {
    if (systemCues_ == enabled) return;
    systemCues_ = enabled;
    focusCuesChanged();
}
Result detail::UiState::setPresentation(const PresentationSettings& settings) {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (settings.textSize < minTextSize || settings.textSize > maxTextSize ||
        (settings.density != Density::standard && settings.density != Density::compact) ||
        (settings.hover != Hover::on && settings.hover != Hover::off) ||
        (settings.focusIndicator != FocusIndicatorMode::automatic && settings.focusIndicator != FocusIndicatorMode::always &&
         settings.focusIndicator != FocusIndicatorMode::never) ||
        (settings.focusRingWidth && (*settings.focusRingWidth < minFocusRingWidth || *settings.focusRingWidth > maxFocusRingWidth)) ||
        (settings.focusRingRadius && (*settings.focusRingRadius < minFocusRingRadius || *settings.focusRingRadius > maxFocusRingRadius)) ||
        (settings.motion != Motion::automatic && settings.motion != Motion::off))
        return {Status::invalidValue, u"Invalid presentation setting"};
    if (settings.variant.name == "high-contrast") return {Status::notSupported, u"High contrast is a later phase"};
    if (settings.variant.name != "light" && settings.variant.name != "dark") return {Status::notFound, u"Unknown theme variant"};
    if (settings == presentation_) return {};
    auto reflows = presentation_;  // the focus and motion modes alone change no layout or font
    reflows.focusIndicator = settings.focusIndicator;
    reflows.motion = settings.motion;
    const bool reflow = !(reflows == settings);
    presentation_ = settings;
    if (reflow) ++layoutRevision_;
    refreshTooltips();
    if (!motionEnabled()) finishAnimations();  // switching motion off lands every running transition at once
    if (reflow && presentationChanged_) presentationChanged_();
    if (invalidate_) invalidate_();
    return {};
}
namespace {
bool isBuiltinAppearance(const Appearance& appearance) {
    const auto& list = detail::builtinAppearances();
    return std::find(list.begin(), list.end(), &appearance) != list.end();
}
Palette applyOverrides(Palette palette, const std::vector<PaletteOverride>& overrides, std::string_view appearance, const ThemeVariant& variant) {
    for (const auto& entry : overrides)
        if (entry.appearance == appearance && entry.variant == variant) setPaletteValue(palette, entry.role, entry.value);
    return palette;
}
std::u16string ascii16(const std::string& text) { return std::u16string(text.begin(), text.end()); }
}
Palette detail::UiState::effectivePalette(const Appearance& appearance, const ThemeVariant& variant) const {
    auto palette = appearance.palette(variant);
    if (!paletteOverrides_.empty() && isBuiltinAppearance(appearance)) palette = applyOverrides(palette, paletteOverrides_, appearance.name(), variant);
    palette.focus = palette.focusColor();  // settled here for every appearance: the ring is the accent unless the palette or an override pinned it
    return palette;
}
Result detail::UiState::setPaletteOverrides(std::vector<PaletteOverride> overrides) {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    std::u16string problems;
    auto reject = [&](const std::string& text) { problems += (problems.empty() ? u"" : u"; ") + ascii16(text); };
    std::vector<std::pair<const Appearance*, ThemeVariant>> affected;
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        const auto& entry = overrides[i];
        const auto* appearance = builtinAppearance(entry.appearance);
        if (!appearance) { reject("unknown appearance \"" + entry.appearance + "\""); continue; }
        if (!appearance->supports(entry.variant)) { reject(entry.appearance + " does not support theme variant \"" + entry.variant.name + "\""); continue; }
        Palette scratch = appearance->palette(entry.variant);
        if (!setPaletteValue(scratch, entry.role, entry.value)) { reject("invalid palette value for role \"" + entry.role + "\" of " + entry.appearance + " / " + entry.variant.name); continue; }
        bool repeated = false;
        for (std::size_t j = 0; j < i; ++j)
            repeated = repeated || (overrides[j].appearance == entry.appearance && overrides[j].variant == entry.variant && overrides[j].role == entry.role);
        if (repeated) { reject("role \"" + entry.role + "\" of " + entry.appearance + " / " + entry.variant.name + " is overridden twice"); continue; }
        if (std::none_of(affected.begin(), affected.end(), [&](const auto& pair) { return pair.first == appearance && pair.second == entry.variant; }))
            affected.emplace_back(appearance, entry.variant);
    }
    if (problems.empty()) {
        for (const auto& [appearance, variant] : affected) {
            auto failures = checkPaletteContrast(applyOverrides(appearance->palette(variant), overrides, appearance->name(), variant));
            for (const auto& failure : failures) {
                char numbers[64];
                std::snprintf(numbers, sizeof numbers, "%.2f:1 (needs %.1f:1)", std::floor(failure.ratio * 100) / 100, failure.required);
                reject(std::string(appearance->name()) + " / " + variant.name + ": " + failure.pair + " contrast " + numbers);
            }
        }
    }
    if (!problems.empty()) return {Status::invalidValue, u"Palette overrides rejected: " + problems};
    paletteOverrides_ = std::move(overrides);
    if (invalidate_) invalidate_();
    return {};
}
detail::UiState::UiState(std::vector<Component> controls) : controls_(std::move(controls)) {
    OperationScope operation(this);
    std::map<Id, bool> ids;
    for (const auto& control : controls_) {
        (void)detail::componentType(control.kind);
        if (detail::componentType(control.kind).requiresTypedCreation()) throw std::invalid_argument("Component requires typed creation");
        if (control.id.empty() || !ids.emplace(control.id, true).second)
            throw std::invalid_argument("Control IDs must be nonempty and unique");
        if (!validTextFlow(control)) throw std::invalid_argument("Invalid text alignment or spacing");
        componentLifetimes_.emplace(control.id, std::make_shared<int>(0));
        detail::componentType(control.kind).adopt(*this, control);
    }
    for (const auto& control : controls_)
        if (!control.target.empty() && (!detail::componentType(control.kind).canTarget() || !ids.contains(control.target)))
            throw std::invalid_argument("Only labels may target, and only known controls");
}
bool detail::UiState::drawsFocusIndicator(const ComponentSnapshot& semantics) const {
    if (presentation_.focusIndicator == FocusIndicatorMode::never) return false;
    return semantics.focused && (focusIndicatorVisible() || componentType(semantics.kind).focusAlwaysVisible(semantics));
}
detail::UiState::~UiState() {
    lifetime_.reset();
    componentLifetimes_.clear();
    for (auto& [id, field] : edits_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : bools_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : radios_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : dropdowns_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : lists_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : numerics_) if (field.disconnect) field.disconnect();
    for (auto& [id, field] : progresses_) if (field.disconnect) field.disconnect();
    for (auto& [id, bound] : providers_) if (bound.disconnect) bound.disconnect();
}
ProgressHandle detail::UiState::addProgress(Component control) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid progress component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    progresses_.emplace(id, ProgressField{});
    return ProgressHandle(this, componentLifetime(id), id);
}
Result ProgressHandle::connectSource(Source<int> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"A numeric source requires read and subscribe"};
        auto& field = ui_->progresses_.at(id_);
        auto weak = lifetime_; auto state = ui_;
        auto id = id_;
        auto changed = [weak, state, id] { if (!weak.expired() && !state->removalPending(id) && state->invalidate_) state->invalidate_(); };
        if (auto r = ui_->replaceSource(field.source, field.disconnect, std::move(source), [&](const auto& s) { return s.subscribe(changed); }); !r) return r;
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    });
}
Query<ComponentSnapshot> ProgressHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
TextFieldHandle detail::UiState::addTextField(Component control) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id) || !detail::validTextControl(control))
        throw std::invalid_argument("Invalid text component");
    Field field;
    field.session.text = control.value; field.rich = control.rich;
    Id id = control.id;
    controls_.push_back(std::move(control));
    edits_.emplace(id, std::move(field));
    return TextFieldHandle(this, componentLifetime(id), id);
}
ButtonHandle detail::UiState::addButton(Component control) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid button component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    auto token = std::make_shared<int>(0);
    componentLifetimes_.emplace(id, token);
    return ButtonHandle(this, token, std::move(id));
}
LabelHandle detail::UiState::addLabel(Component control) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid label component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    return LabelHandle(this, componentLifetime(id), id);
}
CheckboxHandle detail::UiState::addCheckbox(Component control) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid checkbox component");
    Id id = control.id;
    controls_.push_back(std::move(control));
    bools_.emplace(id, BoolField{});
    bind(id, [this, id] { return toggleCheckbox(id); });
    return CheckboxHandle(this, componentLifetime(id), id);
}
Result CheckboxHandle::toggle() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->toggleCheckbox(id_); });
}
Result CheckboxHandle::useLatest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->bools_.at(id_); if (!f.connected) return {Status::invalidValue, u"Source is not connected"};
        f.value = f.origin = f.sourceValue; ui_->setError(id_, u""); return ui_->notify();
    });
}
Result CheckboxHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result CheckboxHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result CheckboxHandle::connectSource(Source<bool> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"A boolean source requires read and subscribe"};
        auto& field = ui_->bools_.at(id_);
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        auto changed = [weak, state, id] { if (!weak.expired() && !state->removalPending(id)) state->boolSourceChanged(id); };
        if (auto r = ui_->replaceSource(field.source, field.disconnect, std::move(source), [&](const auto& s) { return s.subscribe(changed); }); !r) return r;
        field.connected = false;
        return ui_->readInstalledSource([&] { ui_->boolSourceChanged(id_); });
    });
}
// A notification fired inside the first subscribe arrives before any source is installed: nothing to read yet, and the
// connecting entry reads the new source once it is installed (replaceSource). The other *_source_changed do the same.
void detail::UiState::boolSourceChanged(const Id& id) {
    OperationScope operation(this);
    auto& field = bools_.at(id);
    if (!field.source.read) return;
    bool value = field.source.read();
    bool clean = !field.connected || field.value == field.sourceValue;
    field.sourceValue = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result CheckboxHandle::connectImmediate(std::function<Result(const Change<CheckboxHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->bools_.at(id_), std::move(write), true);
    });
}
Result CheckboxHandle::connectConfirmed(std::function<Result(const Change<CheckboxHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->bools_.at(id_), std::move(write), false);
    });
}
Result CheckboxHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirmField(*this);
    });
}
Result detail::UiState::toggleCheckbox(const Id& id) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    auto& field = bools_.at(id);
    if (!field.writable()) return {Status::invalidValue, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (field.value == field.sourceValue && !field.submitting) field.origin = field.sourceValue;
    field.value = !field.value;
    Change<CheckboxHandle> change{CheckboxHandle(this, componentLifetimes_.at(id), id), field.value, field.origin};
    return writeImmediately(id, field, change);
}
Query<ComponentSnapshot> CheckboxHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<bool> CheckboxHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<bool> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->bools_.at(id_).value};
    });
}
RadioGroupHandle detail::UiState::addRadioGroup(Component control, std::vector<OptionItem> options) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id))
        throw std::invalid_argument("Invalid radio group");
    std::set<std::string> ids;
    for (const auto& option : options)
        if (option.value.id.empty() || !ids.insert(option.value.id).second) throw std::invalid_argument("Invalid radio option");
    auto id = control.id;
    controls_.push_back(std::move(control));
    RadioField field; field.options = std::move(options); radios_.emplace(id, std::move(field));
    return RadioGroupHandle(this, componentLifetime(id), id);
}
ListHandle detail::UiState::addList(Component control, std::vector<OptionItem> items) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid list");
    std::set<std::string> ids;
    for (const auto& item : items) if (item.value.id.empty() || !ids.insert(item.value.id).second) throw std::invalid_argument("Invalid list identity");
    auto id = control.id; controls_.push_back(std::move(control));
    ListField field; field.items = std::move(items); lists_.emplace(id, std::move(field));
    return ListHandle(this, componentLifetime(id), id);
}
Result ListHandle::connectSource(Source<std::optional<OptionValue>> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"An option source requires read and subscribe"};
        auto& f = ui_->lists_.at(id_); auto weak = lifetime_; auto* state = ui_;
        auto id = id_;
        auto changed = [weak, state, id] { if (!weak.expired() && !state->removalPending(id) && state->invalidate_) state->invalidate_(); };
        return ui_->replaceSource(f.source, f.disconnect, std::move(source), [&](const auto& s) { return s.subscribe(changed); });
    });
}
Result ListHandle::connectImmediate(std::function<Result(const Change<ListHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        auto& f = ui_->lists_.at(id_); if (!f.source.read || !write) return {Status::invalidValue, u"A source and write are required"};
        f.immediate = std::move(write); return {};
    });
}
Result ListHandle::select(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->availableOption(id_, value); !r) return r;
        auto& f = ui_->lists_.at(id_);
        auto item = std::find_if(f.items.begin(), f.items.end(), [&](const auto& row) { return row.value == value; });
        if (item == f.items.end()) return ui_->find(id_)->readOnly ? Result{Status::readOnly, u"List is read only"} : Result{Status::invalidValue, u"Invalid list item"};
        if (!item->enabled) return {Status::disabled, u"List item is disabled"};
        if (ui_->find(id_)->readOnly) return {Status::readOnly, u"List is read only"};
        if (!f.source.read || !f.immediate) return {Status::invalidValue, u"Connect an immediate write"};
        if (ui_->dispatching_ || f.writing) return {Status::busy, u"Operation is already executing"};
        auto origin = f.source.read();
        if (origin == value) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); return {}; }
        f.writing = true; Result r;
        try { r = f.immediate(Change<ListHandle>{*this, value, std::move(origin)}); }
        catch (...) { f.writing = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
        f.writing = false;
        if (r) { f.cursor = value; if (ui_->invalidate_) ui_->invalidate_(); auto n = ui_->notify(); if (!n) return n; } return r;
    });
}
Result ListHandle::focus(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_, true); !result) return result;
        auto& list = ui_->lists_.at(id_);
        auto item = std::find_if(list.items.begin(), list.items.end(), [&](const auto& row) { return row.value == value; });
        if (item == list.items.end()) return {Status::notFound, {}};
        if (auto result = ui_->availableOption(id_, value); !result) return result;
        if (!item->enabled) return {Status::disabled, {}};
        if (auto result = ui_->focus(id_); !result) return result;
        list.cursor = std::move(value);
        if (ui_->revealRequested_) ui_->revealRequested_(id_);
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    }, detail::UiState::goneWithoutText);
}
Result ListHandle::setItems(std::vector<OptionItem> items) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        std::set<std::string> ids;
        for (const auto& item : items) if (item.value.id.empty() || !ids.insert(item.value.id).second) return {Status::invalidValue, u"Invalid list identity"};
        if (ui_->dispatching_) return {Status::busy, u"Operation is already executing"};
        auto& f = ui_->lists_.at(id_);
        if (f.writing) return {Status::busy, u"Operation is already executing"};
        auto exists = [&](const OptionValue& value) {
            return std::any_of(items.begin(), items.end(), [&](const auto& item) { return item.enabled && item.value == value; });
        };
        auto selected = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        if (selected && !exists(*selected)) {
            if (!f.immediate) return {Status::invalidValue, u"Cannot clear selection without a write connection"};
            f.writing = true; Result result;
            try { result = f.immediate(Change<ListHandle>{*this, std::nullopt, selected}); }
            catch (...) { f.writing = false; ui_->stopped_ = true; return {Status::failed, u"Modification failed"}; }
            f.writing = false; if (!result) return result;
        }
        if (f.cursor && !exists(*f.cursor)) {
            auto old = std::find_if(f.items.begin(), f.items.end(), [&](const auto& item) { return item.value == *f.cursor; });
            std::optional<OptionValue> replacement;
            if (old != f.items.end()) {
                for (auto it = old + 1; it != f.items.end(); ++it) if (exists(it->value)) { replacement = it->value; break; }
                if (!replacement) for (auto it = old; it != f.items.begin();) { --it; if (exists(it->value)) { replacement = it->value; break; } }
            }
            f.cursor = replacement;
        }
        f.items = std::move(items); if (ui_->invalidate_) ui_->invalidate_(); return {};
    });
}
Result ListHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result ListHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r; ui_->enable(id_, enabled); return {};
    });
}
Query<ComponentSnapshot> ListHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Result RadioGroupHandle::useLatest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->radios_.at(id_); if (!f.connected) return {Status::invalidValue, u"Source is not connected"};
        f.value = f.origin = f.sourceValue; ui_->setError(id_, u""); return ui_->notify();
    });
}
Result RadioGroupHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result RadioGroupHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result RadioGroupHandle::enableOption(OptionValue value, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        auto& options = ui_->radios_.at(id_).options;
        auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.value == value; });
        if (it == options.end()) return {Status::invalidValue, u"Unknown option"};
        it->enabled = enabled; if (ui_->invalidate_) ui_->invalidate_(); return {};
    });
}
Result RadioGroupHandle::connectSource(Source<std::optional<OptionValue>> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"An option source requires read and subscribe"};
        auto& field = ui_->radios_.at(id_);
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        auto changed = [weak, state, id] { if (!weak.expired() && !state->removalPending(id)) state->optionSourceChanged(id); };
        if (auto r = ui_->replaceSource(field.source, field.disconnect, std::move(source), [&](const auto& s) { return s.subscribe(changed); }); !r) return r;
        field.connected = false;
        return ui_->readInstalledSource([&] { ui_->optionSourceChanged(id_); });
    });
}
void detail::UiState::optionSourceChanged(const Id& id) {
    OperationScope operation(this);
    auto& field = radios_.at(id); if (!field.source.read) return;
    auto value = field.source.read();
    bool clean = !field.connected || field.value == field.sourceValue;
    field.sourceValue = value; field.connected = true;
    if (clean && !field.submitting) field.value = field.origin = value;
    if (invalidate_) invalidate_();
}
Result RadioGroupHandle::connectImmediate(std::function<Result(const Change<RadioGroupHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->radios_.at(id_), std::move(write), true);
    });
}
Result RadioGroupHandle::connectConfirmed(std::function<Result(const Change<RadioGroupHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->radios_.at(id_), std::move(write), false);
    });
}
Result RadioGroupHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirmField(*this);
    });
}
Result RadioGroupHandle::select(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->selectOption(id_, std::move(value)); });
}
Result detail::UiState::selectOption(const Id& id, OptionValue value) {
    OperationScope operation(this);
    if (auto r = availableOption(id, value); !r) return r;
    auto& field = radios_.at(id);
    auto it = std::find_if(field.options.begin(), field.options.end(), [&](const auto& o) { return o.value == value; });
    if (it == field.options.end()) return {Status::invalidValue, u"Unknown option"};
    if (!it->enabled) return {Status::disabled, u"Option is disabled"};
    if (!field.writable()) return {Status::invalidValue, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (field.value == value) return {};
    if (field.value == field.sourceValue && !field.submitting) field.origin = field.sourceValue;
    field.value = std::move(value);
    Change<RadioGroupHandle> change{RadioGroupHandle(this, componentLifetimes_.at(id), id), field.value, field.origin};
    return writeImmediately(id, field, change);
}
Query<ComponentSnapshot> RadioGroupHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<std::optional<OptionValue>> RadioGroupHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::optional<OptionValue>> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->radios_.at(id_).value};
    });
}
DropdownHandle detail::UiState::addDropdown(Component control, std::vector<OptionItem> options) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id)) throw std::invalid_argument("Invalid dropdown");
    std::set<std::string> ids;
    for (const auto& option : options)
        if (option.value.id.empty() || !ids.insert(option.value.id).second) throw std::invalid_argument("Invalid dropdown option");
    auto id = control.id; controls_.push_back(std::move(control));
    DropdownField field; field.options = std::move(options); dropdowns_.emplace(id, std::move(field));
    return DropdownHandle(this, componentLifetime(id), id);
}
void detail::UiState::closePopup() {
    OperationScope operation(this);
    if (popupSource_.empty()) return;
    if (auto it = dropdowns_.find(popupSource_); it != dropdowns_.end()) it->second.candidate.reset();
    popupSource_.clear();
    if (invalidate_) invalidate_();
}
Result DropdownHandle::open() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->dropdowns_.at(id_);
        if (!f.writable()) return {Status::invalidValue, u"Connect an explicit write timing before editing"};
        if (auto r = ui_->focus(id_); !r) return r;
        ui_->closePopup(); ui_->popupSource_ = id_;
        auto current = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.enabled && f.value && o.value == *f.value; });
        if (current == f.options.end()) current = std::find_if(f.options.begin(), f.options.end(), [](const auto& o) { return o.enabled; });
        f.reveal = true; f.scroll.offset = 0;
        f.candidate = current == f.options.end() ? std::nullopt : std::optional<OptionValue>(current->value);
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    });
}
Result DropdownHandle::cancel() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        if (ui_->popupSource_ == id_) ui_->closePopup();
        return {};
    });
}
Result DropdownHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result DropdownHandle::focus(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->availableOption(id_, value); !r) return r;
        auto& f = ui_->dropdowns_.at(id_);
        const auto item = std::find_if(f.options.begin(), f.options.end(), [&](const auto& option) { return option.value == value; });
        if (item == f.options.end()) return {Status::notFound, {}};
        if (!item->enabled) return {Status::disabled, {}};
        if (!ui_->popupOpen(id_)) return {Status::invalidValue, u"The dropdown is closed"};
        if (auto r = ui_->focus(id_); !r) return r;
        f.candidate = std::move(value); f.reveal = true;
        if (ui_->invalidate_) ui_->invalidate_();
        return {};
    });
}
Result DropdownHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result DropdownHandle::setItems(std::vector<OptionItem> options) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (ui_->dispatching_) return {Status::busy, u"Operation is already executing"};
        std::set<std::string> ids;
        for (const auto& o : options) if (o.value.id.empty() || !ids.insert(o.value.id).second) return {Status::invalidValue, u"Invalid dropdown option"};
        auto& f = ui_->dropdowns_.at(id_); f.options = std::move(options);
        auto valid = [&](const std::optional<OptionValue>& value) {
            return value && std::any_of(f.options.begin(), f.options.end(), [&](const auto& o) { return o.enabled && o.value == *value; });
        };
        if (!valid(f.value)) f.value.reset();
        if (!valid(f.candidate)) f.candidate.reset();
        if (ui_->invalidate_) ui_->invalidate_();
        return ui_->notify();
    });
}
Result DropdownHandle::connectSource(Source<std::optional<OptionValue>> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"An option source requires read and subscribe"};
        auto& f = ui_->dropdowns_.at(id_);
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        auto changed = [weak, state, id] { if (!weak.expired() && !state->removalPending(id)) state->dropdownSourceChanged(id); };
        if (auto r = ui_->replaceSource(f.source, f.disconnect, std::move(source), [&](const auto& s) { return s.subscribe(changed); }); !r) return r;
        f.connected = false;
        return ui_->readInstalledSource([&] { ui_->dropdownSourceChanged(id_); });
    });
}
void detail::UiState::dropdownSourceChanged(const Id& id) {
    OperationScope operation(this);
    auto& f = dropdowns_.at(id); if (!f.source.read) return;
    auto value = f.source.read();
    bool clean = !f.connected || f.value == f.sourceValue;
    f.sourceValue = value; f.connected = true;
    if (clean && !f.submitting) {
        auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return value && o.enabled && o.value == *value; });
        f.value = it == f.options.end() ? std::nullopt : value; f.origin = value;
    }
    if (invalidate_) invalidate_();
}
Result DropdownHandle::connectImmediate(std::function<Result(const Change<DropdownHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->dropdowns_.at(id_), std::move(write), true);
    });
}
Result DropdownHandle::connectConfirmed(std::function<Result(const Change<DropdownHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->dropdowns_.at(id_), std::move(write), false);
    });
}
Result DropdownHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirmField(*this);
    });
}
Result DropdownHandle::useLatest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->dropdowns_.at(id_); if (!f.connected) return {Status::invalidValue, u"Source is not connected"};
        auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return f.sourceValue && o.enabled && o.value == *f.sourceValue; });
        f.value = it == f.options.end() ? std::nullopt : f.sourceValue; f.origin = f.sourceValue;
        if (ui_->invalidate_) ui_->invalidate_();
        ui_->setError(id_, u""); return ui_->notify();
    });
}
Result DropdownHandle::select(OptionValue value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->selectDropdown(id_, std::move(value)); });
}
Result detail::UiState::selectDropdown(const Id& id, OptionValue value) {
    OperationScope operation(this);
    if (auto r = availableOption(id, value); !r) return r;
    auto& f = dropdowns_.at(id);
    auto it = std::find_if(f.options.begin(), f.options.end(), [&](const auto& o) { return o.value == value; });
    if (it == f.options.end()) return {Status::invalidValue, u"Unknown option"};
    if (!it->enabled) return {Status::disabled, u"Option is disabled"};
    if (!f.writable()) return {Status::invalidValue, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    if (f.value == value) return {};
    if (f.value == f.sourceValue && !f.submitting) f.origin = f.sourceValue;
    f.value = std::move(value); setError(id, u"");
    if (invalidate_) invalidate_();
    Change<DropdownHandle> change{DropdownHandle(this, componentLifetimes_.at(id), id), f.value, f.origin};
    return writeImmediately(id, f, change);
}
Query<ComponentSnapshot> DropdownHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<std::optional<OptionValue>> DropdownHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::optional<OptionValue>> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->dropdowns_.at(id_).value};
    });
}
namespace {
int snapSlider(SliderRange range, int input) {
    const long long lo = range.minimum, hi = range.maximum, step = range.step;
    const long long value = std::clamp<long long>(input, lo, hi);
    const long long lower = lo + ((value - lo) / step) * step;
    const long long upper = std::min(hi, lower + step);
    return static_cast<int>(value - lower < upper - value ? lower : upper);
}
}
SliderHandle detail::UiState::addSlider(Component control, SliderRange range) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id) || range.minimum >= range.maximum || range.step <= 0)
        throw std::invalid_argument("Invalid slider component or range");
    auto id = control.id;
    controls_.push_back(std::move(control));
    NumericField field{}; field.range = range; field.value = field.sourceValue = field.origin = range.minimum;
    numerics_.emplace(id, std::move(field));
    return SliderHandle(this, componentLifetime(id), id);
}
Result SliderHandle::setValue(int value) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->available(id_); !result) return result;
        const auto range = ui_->numerics_.at(id_).range;
        if (value < range.minimum || value > range.maximum) return {Status::invalidValue, u"Value is outside the slider range"};
        return ui_->sliderSetValue(id_, value);
    });
}
Result SliderHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result SliderHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        ui_->enable(id_, enabled); return {};
    });
}
Result SliderHandle::useLatest() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        auto& f = ui_->numerics_.at(id_);
        if (!f.connected) return {Status::invalidValue, u"Source is not connected"};
        if (f.value != f.sourceValue) ui_->sliderLand(id_);
        f.value = f.origin = f.sourceValue; ui_->setError(id_, u""); return ui_->notify();
    });
}
Result SliderHandle::connectSource(Source<int> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"A numeric source requires read and subscribe"};
        auto& f = ui_->numerics_.at(id_);
        int value = 0;
        try { value = source.read(); } catch (...) { return {Status::failed, u"Source read failed"}; }
        if (snapSlider(f.range, value) != value) return {Status::invalidValue, u"Source value is outside the slider's valid values"};
        auto weak = lifetime_; auto state = ui_; auto id = id_;
        auto changed = [weak, state, id] { if (!weak.expired() && !state->removalPending(id)) state->numericSourceChanged(id); };
        if (auto r = ui_->replaceSource(f.source, f.disconnect, std::move(source), [&](const auto& s) { return s.subscribe(changed); }); !r) return r;
        f.connected = false;
        return ui_->readInstalledSource([&] { ui_->numericSourceChanged(id_); });
    });
}
void detail::UiState::numericSourceChanged(const Id& id) {
    OperationScope operation(this);
    auto& f = numerics_.at(id); if (!f.source.read) return;
    int value = f.source.read();
    if (snapSlider(f.range, value) != value) {
        if (!f.sourceInvalid) { auto it = errors_.find(id); f.errorBeforeSource = it == errors_.end() ? u"" : it->second; }
        f.sourceInvalid = true; setError(id, u"Source value is outside the slider's valid values");
        if (invalidate_) invalidate_(); return;
    }
    if (f.sourceInvalid) { f.sourceInvalid = false; setError(id, std::move(f.errorBeforeSource)); }
    bool clean = !f.connected || f.value == f.sourceValue;
    f.sourceValue = value; f.connected = true;
    if (clean && !f.submitting) { if (f.value != value) sliderLand(id); f.value = f.origin = value; }
    if (invalidate_) invalidate_();
}
Result SliderHandle::connectImmediate(std::function<Result(const Change<SliderHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->numerics_.at(id_), std::move(write), true);
    });
}
Result SliderHandle::connectConfirmed(std::function<Result(const Change<SliderHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->configurationAccess(id_); !r) return r;
        return detail::UiState::connectFieldWrite(ui_->numerics_.at(id_), std::move(write), false);
    });
}
Result SliderHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto r = ui_->available(id_); !r) return r;
        return ui_->confirmField(*this);
    });
}
Result detail::UiState::sliderSetValue(const Id& id, int value) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    auto& f = numerics_.at(id);
    if (find(id)->readOnly) return {Status::readOnly, u"Component is read only"};
    if (!f.writable()) return {Status::invalidValue, u"Connect an explicit write timing before editing"};
    if (dispatching_) return {Status::busy, u"Operation is already executing"};
    value = snapSlider(f.range, value);
    if (f.value == value) return {};
    if (f.value == f.sourceValue && !f.submitting) f.origin = f.sourceValue;
    f.value = value;
    sliderLand(id);  // the user's own input starts its slide afterwards (sliderStartedMoving); any other change shows at once
    Change<SliderHandle> change{SliderHandle(this, componentLifetimes_.at(id), id), f.value, f.origin};
    return writeImmediately(id, f, change);
}
namespace {
double sliderShare(const SliderRange& range, int value) {
    return (static_cast<double>(value) - range.minimum) / (static_cast<double>(range.maximum) - range.minimum);
}
// Pointer movement up to this many device-independent pixels from the press still counts as a click, not a drag.
constexpr double sliderDragThresholdDip = 4;
// The thumb's place on a track as a share of it: the pointer x clamped to the track's ends.
double trackShare(double x, double origin, double width) { return std::clamp((x - origin) / width, 0.0, 1.0); }
}
Result detail::UiState::sliderPointerDown(const Id& id, int x, Rect track, float scale) {
    OperationScope operation(this);
    if (pointerArmed_ != id || track.w <= 0) return {Status::invalidValue, u"No slider drag is active"};
    // With motion the thumb stays on its tick (sliding there after a value change) until the pointer
    // moves like a drag; without motion it follows the pointer from the press, as in #73.
    sliderDrag_ = SliderDrag{id, static_cast<double>(track.x), static_cast<double>(track.w), x, x, !motionEnabled(),
                              static_cast<double>(track.x), static_cast<double>(track.w), static_cast<int>(std::lround(sliderDragThresholdDip * std::max(scale, 0.f)))};
    return sliderPointer(id, x);
}
Result detail::UiState::sliderPointer(const Id& id, int x) {
    OperationScope operation(this);
    if (pointerArmed_ != id || !sliderDrag_ || sliderDrag_->id != id) return {Status::invalidValue, u"No slider drag is active"};
    sliderDrag_->x = x;
    if (!sliderDrag_->held && std::abs(x - sliderDrag_->pressX) > sliderDrag_->threshold) {
        // A real drag: the thumb follows the hand, so a running slide to a tick is dropped.
        sliderDrag_->held = true;
        sliderLand(id);
    }
    const bool held = sliderDrag_->held;
    const auto basis = *sliderDrag_;
    auto& f = numerics_.at(id);
    const double from = sliderDisplayRatio(id);
    const int before = f.value;
    double ratio = trackShare(x, basis.origin, basis.width);
    double value = static_cast<double>(f.range.minimum) + ratio * (static_cast<double>(f.range.maximum) - f.range.minimum);
    auto result = sliderSetValue(id, static_cast<int>(std::floor(value + 0.5)));
    if (!held) sliderStartedMoving(id, from, before);
    return result;
}
std::optional<int> detail::UiState::sliderDragX(std::string_view id) const {
    if (sliderDrag_ && pointerArmed_ == id && sliderDrag_->id == id) return sliderDrag_->x;
    return std::nullopt;
}
std::optional<int> detail::UiState::sliderHeldX(std::string_view id) const {
    if (sliderDrag_ && sliderDrag_->held && pointerArmed_ == id && sliderDrag_->id == id) return sliderDrag_->x;
    return std::nullopt;
}
// A thumb transition is live only while it is still heading to the slider's current value: any
// other change (application code, undo, a semantic or UIA set, the data source) leaves it stale
// and the thumb is drawn at the new value at once.
bool detail::UiState::sliderTransitionLive(std::string_view id) const {
    auto target = sliderTargets_.find(id);
    auto field = numerics_.find(id);
    return target != sliderTargets_.end() && field != numerics_.end() && field->second.value == target->second &&
           animations_.active(sliderAnimationKey(id));
}
std::optional<double> detail::UiState::sliderAnimatedRatio(std::string_view id) const {
    if (!sliderTransitionLive(id)) return std::nullopt;
    return animations_.value(sliderAnimationKey(id));
}
// Where the thumb is shown now, as a share of the track.
double detail::UiState::sliderDisplayRatio(std::string_view id) const {
    if (auto animated = sliderAnimatedRatio(id)) return *animated;
    const auto& f = numerics_.find(id)->second;
    // The held thumb is drawn at the pointer on the track of the latest frame, so its share is measured there.
    if (sliderDrag_ && sliderDrag_->held && sliderDrag_->id == id)
        return trackShare(sliderDrag_->x, sliderDrag_->seenOrigin, sliderDrag_->seenWidth);
    return sliderShare(f.range, f.value);
}
// The value changed through the user's own input on this slider: slide the thumb from where it was shown.
void detail::UiState::sliderStartedMoving(const Id& id, double from, int valueBefore) {
    auto it = numerics_.find(id);
    if (it == numerics_.end() || it->second.value == valueBefore || !motionEnabled()) return;
    animations_.start(sliderAnimationKey(id), from, sliderShare(it->second.range, it->second.value));
    sliderTargets_.insert_or_assign(id, it->second.value);
}
// The drag ended while holding the thumb off its tick. Letting go slides it there from the pixel it was drawn at;
// an interruption (capture loss, focus move, a new press) puts it on the tick at once.
void detail::UiState::endSliderDrag(bool slide) {
    if (!sliderDrag_) return;
    const auto drag = *sliderDrag_;
    sliderDrag_.reset();
    auto it = numerics_.find(drag.id);
    if (!slide || !drag.held || it == numerics_.end() || !motionEnabled()) return;
    const double from = trackShare(drag.x, drag.seenOrigin, drag.seenWidth);
    const double to = sliderShare(it->second.range, it->second.value);
    if (std::abs(from - to) < 1e-9) return;
    animations_.start(sliderAnimationKey(drag.id), from, to);
    sliderTargets_.insert_or_assign(drag.id, it->second.value);
}
// The latest frame's track for the dragged slider: the held thumb is drawn on it, wherever layout moved it.
void detail::UiState::sliderTrackRefresh(const Frame& frame) {
    if (!sliderDrag_) return;
    if (const auto* item = findPlacement(frame, sliderDrag_->id); item && item->sliderTrack.w > 0) {
        sliderDrag_->seenOrigin = item->sliderTrack.x;
        sliderDrag_->seenWidth = item->sliderTrack.w;
    }
}
// The thumb is at its value from now on: nothing slides (any change that is not the user's own input on the slider).
void detail::UiState::sliderLand(std::string_view id) {
    sliderTargets_.erase(Id(id));
    animations_.cancel(sliderAnimationKey(id));
}
bool detail::UiState::motionEnabled() const {
    return animations_.hasTimeSource() && presentation_.motion == Motion::automatic && systemAnimations_ && !windowHidden_;
}
void detail::UiState::finishAnimations() {
    const bool wasRunning = animationRunning();
    animations_.finishAll(); sliderTargets_.clear();
    if (wasRunning && invalidate_) invalidate_();  // draw the landed frame
}
void detail::UiState::setSystemAnimations(bool enabled) {
    systemAnimations_ = enabled;
    if (!motionEnabled()) finishAnimations();
}
void detail::UiState::setWindowHidden(bool hidden) {
    windowHidden_ = hidden;
    if (!motionEnabled()) finishAnimations();
}
bool detail::UiState::animationRunning() const {
    for (const auto& [id, target] : sliderTargets_) if (sliderTransitionLive(id)) return true;
    return false;
}
Query<bool> detail::UiState::animating() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread", std::nullopt};
    if (stopped_) return {Status::failed, u"UI is stopped", std::nullopt};
    return {Status::ok, {}, animationRunning()};
}
Query<ComponentSnapshot> SliderHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<int> SliderHandle::draft() const {
    return detail::UiState::operate(*this, [&]() -> Query<int> {
        auto s = describe(); if (!s) return {s.status, s.message, std::nullopt};
        return {Status::ok, {}, ui_->numerics_.at(id_).value};
    });
}
ComponentHandle detail::UiState::layoutHandle(std::string_view id) {
    OperationScope operation(this);
    auto control = find(id);
    if (!control) return {};
    return componentType(control->kind).handle(*this, *control);
}

std::optional<Id> detail::UiState::layoutId(const ComponentHandle& handle) const {
    return std::visit([this](const auto& item) -> std::optional<Id> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
        else return item.ui_ == this && !item.lifetime_.expired() ? std::optional<Id>(item.id_) : std::nullopt;
    }, handle);
}
bool detail::UiState::retiredComponent(const ComponentHandle& handle) const {
    return std::visit([this](const auto& h) {
        if constexpr (std::is_same_v<std::decay_t<decltype(h)>, std::monostate>) return false;
        else return h.ui_ == this && (h.lifetime_.expired() || removalPending(h.id_));
    }, handle);
}
TabsHandle detail::UiState::addTabs(Component control, std::vector<TabPage> pages) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id) || pages.empty() || !pages.front().enabled)
        throw std::invalid_argument("Invalid tabs component");
    std::set<std::string> values;
    std::set<Id> members;
    for (const auto& page : pages) {
        if (page.value.id.empty() || !values.insert(page.value.id).second) throw std::invalid_argument("Duplicate page identity");
        for (const auto& handle : page.members) {
            auto member = layoutId(handle);
            if (!member || removalPending(*member) || pageMembers_.contains(*member) || !members.insert(*member).second)
                throw std::invalid_argument("Invalid or duplicate page member");
        }
    }
    Id id = control.id;
    controls_.push_back(std::move(control));
    for (std::size_t i = 0; i < pages.size(); ++i)
        for (const auto& handle : pages[i].members) pageMembers_.emplace(*layoutId(handle), std::make_pair(id, i));
    tabs_.emplace(id, TabsState{std::move(pages), 0, 0, false, {}});
    bind(id, [this, id] { auto& t = tabs_.at(id); return activateTab(id, t.pages[t.candidate].value); });
    return TabsHandle(this, componentLifetime(id), id);
}
NavigationListHandle detail::UiState::addNavigationList(Component control, std::vector<NavigationPage> pages, OptionValue initial) {
    OperationScope operation(this);
    if (control.id.empty() || find(control.id) || pages.empty())
        throw std::invalid_argument("Invalid navigation list");
    auto selected = std::find_if(pages.begin(), pages.end(), [&](const auto& p) { return p.value == initial && p.enabled; });
    if (selected == pages.end()) throw std::invalid_argument("Invalid initial page");
    auto index = static_cast<std::size_t>(selected - pages.begin());
    std::set<std::string> values;
    std::set<Id> members;
    for (const auto& page : pages) {
        if (page.value.id.empty() || page.value.id.find_first_not_of(" \t\r\n") == std::string::npos || !values.insert(page.value.id).second) throw std::invalid_argument("Invalid page identity");
        for (const auto& handle : page.members) {
            auto member = layoutId(handle);
            if (!member || !find(*member) || removalPending(*member) || pageMembers_.contains(*member) ||
                dialogMember(*member) || !members.insert(*member).second)
                throw std::invalid_argument("Invalid page member");
        }
    }
    Id id = control.id;
    controls_.push_back(std::move(control));
    for (std::size_t i = 0; i < pages.size(); ++i)
        for (const auto& handle : pages[i].members) pageMembers_.emplace(*layoutId(handle), std::make_pair(id, i));
    tabs_.emplace(id, TabsState{std::move(pages), index, index, false, {}});
    return NavigationListHandle(this, componentLifetime(id), id);
}
TooltipHandle detail::UiState::addTooltip(ComponentHandle source, std::u16string text, unsigned delayMs) {
    OperationScope operation(this);
    auto id = layoutId(source);
    if (!id || !find(*id) || removalPending(*id) || text.empty() || tooltips_.contains(*id))
        throw std::invalid_argument("Invalid tooltip source or text");
    tooltips_.emplace(*id, TooltipState{std::move(text), delayMs});
    refreshTooltips();
    auto sourceLifetime = std::visit([](const auto& h) -> std::weak_ptr<void> {
        if constexpr (std::is_same_v<std::decay_t<decltype(h)>, std::monostate>) return {};
        else return h.lifetime_;
    }, source);
    return TooltipHandle(this, sourceLifetime, *id);
}
void detail::UiState::refreshTooltips() {
    OperationScope operation(this);
    if (rendererFailed_) return;
    if (tooltipClock_) tooltipNow_ = tooltipClock_();
    bool changed = false;
    for (auto& [id, tip] : tooltips_) {
        auto visible = visibility_.find(id);
        bool onScreen = !hasFrameVisibility_ || (visible != visibility_.end() && visible->second);
        bool eligible = onScreen && windowActive_ && !modal_ && ((focus_ == id && keyboardFocusCues()) || tooltipHover_ == id) && bool(available(id));
        if (eligible != tip.eligible) {
            tip.eligible = eligible;
            tip.visible = false;
            tip.dismissed = false;
            if (eligible) tip.deadline = tooltipNow_ + tip.delayMs;
            changed = true;
        }
        if (eligible && !tip.dismissed && !tip.visible && tooltipNow_ >= tip.deadline) {
            tip.visible = true; changed = true;
        }
    }
    if (changed && invalidate_) invalidate_();
}
void detail::UiState::rendererFailed(bool failed) {
    if (rendererFailed_ == failed) return;
    if (failed) {
        closeMenu();
        if (tooltipClock_) tooltipPausedAt_ = tooltipClock_();
        rendererFailed_ = true;
        return;
    }
    if (tooltipPausedAt_ && tooltipClock_) {
        auto now = tooltipClock_();
        auto elapsed = now >= *tooltipPausedAt_ ? now - *tooltipPausedAt_ : 0;
        for (auto& [id, tip] : tooltips_) if (tip.eligible && !tip.visible && !tip.dismissed) tip.deadline += elapsed;
        tooltipNow_ = now;
    }
    tooltipPausedAt_.reset();
    rendererFailed_ = false;
}
void detail::UiState::frameVisibility(std::map<Id, bool, std::less<>> visibility) {
    visibility_ = std::move(visibility);
    hasFrameVisibility_ = true;
    refreshTooltips();
}
void detail::UiState::scrolled() {
    OperationScope operation(this);
    closePopup();
    closeMenu();
    if (tooltipClock_) tooltipNow_ = tooltipClock_();
    bool changed = false;
    for (auto& [id, tip] : tooltips_) {
        if (!tip.eligible || tip.dismissed) continue;
        tip.visible = false;
        tip.deadline = tooltipNow_ + tip.delayMs;
        changed = true;
    }
    if (changed && invalidate_) invalidate_();
}
std::optional<std::uint64_t> detail::UiState::tooltipDeadline() const {
    std::optional<std::uint64_t> deadline;
    for (const auto& [id, tip] : tooltips_) {
        if (tip.eligible && !tip.visible && !tip.dismissed && (!deadline || tip.deadline < *deadline))
            deadline = tip.deadline;
    }
    return deadline;
}
const std::u16string* detail::UiState::tooltipText(std::string_view id) const {
    auto it = tooltips_.find(id);
    return it != tooltips_.end() && it->second.visible ? &it->second.text : nullptr;
}
void detail::UiState::tooltipTime(std::uint64_t now) {
    OperationScope operation(this);
    tooltipNow_ = now;
    refreshTooltips();
}
void detail::UiState::tooltipHover(std::string_view id) {
    OperationScope operation(this);
    if (tooltipHover_ == id) return;
    tooltipHover_ = Id(id);
    if (auto it = tooltips_.find(tooltipHover_); it != tooltips_.end() && it->second.dismissed) {
        it->second.eligible = false;
        it->second.dismissed = false;
    }
    refreshTooltips();
}
void detail::UiState::windowActive(bool active) {
    OperationScope operation(this);
    if (windowActive_ == active) return;
    windowActive_ = active;
    if (!active) { tooltipHover_.clear(); hoverClear(); closeMenu(); }  // a menu does not outlive window activation
    refreshTooltips();
}
bool detail::UiState::hoverLocked() const {
    return hoverCapture_ || (sliderDrag_ && pointerArmed_ == sliderDrag_->id);
}
bool detail::UiState::hoverAdmitted(const HoverTarget& target) const {
    if (menuOpen() || !available(target.id)) return false;  // an open context menu takes the pointer: its own row highlight is the hover
    if (!target.item) return true;
    auto semantics = describe(target.id);
    if (!semantics) return false;
    auto option = std::find_if(semantics->items.begin(), semantics->items.end(), [&](const auto& o) { return o.value == *target.item; });
    return option != semantics->items.end() && option->enabled;
}
void detail::UiState::hoverSet(std::optional<HoverTarget> target) {
    if (hover_ == target) return;
    hover_ = std::move(target);
    if (invalidate_) invalidate_();
}
void detail::UiState::hoverClear() {
    hoverSimulated_ = hoverCapture_ = false;
    hoverPosition_.reset();
    hoverSet(std::nullopt);
}
void detail::UiState::hoverFromFrame(const Frame& frame, int x, int y) {
    const auto* hit = frame.hit(x, y);
    tooltipHover(hit && !hit->popup ? std::string_view(hit->id) : std::string_view{});
    if (!windowActive_ || hoverLocked()) return;
    std::optional<HoverTarget> target;
    // Labels and progress bars ignore the pointer, so they are never hovered (the tooltip above still sees them).
    if (hit && !menuOpen() && componentType(hit->kind).acceptsPointer()) {
        target = HoverTarget{hit->id, std::nullopt};
        if (auto index = frame.optionAt(*hit, x, y)) {
            auto semantics = describe(hit->id);
            // A disabled item hovers nothing: the target keeps the component without the item.
            if (semantics && *index < semantics->items.size() && semantics->items[*index].enabled)
                target->item = semantics->items[*index].value;
        }
        if (!hoverAdmitted(*target)) target.reset();
    }
    hoverSet(std::move(target));
}
void detail::UiState::pointerOver(const Frame& frame, int x, int y, bool touch) {
    if (touch) return;
    OperationScope operation(this);
    if (windowActive_) { hoverPosition_ = {x, y}; hoverSimulated_ = false; }
    hoverFromFrame(frame, x, y);
}
void detail::UiState::pointerLeft() {
    OperationScope operation(this);
    tooltipHover({});
    hoverClear();
}
void detail::UiState::hoverCapture(std::string_view id) {
    OperationScope operation(this);
    if (!id.empty()) { hoverSimulated_ = false; hoverSet(HoverTarget{Id(id), std::nullopt}); }
    hoverCapture_ = true;
}
void detail::UiState::hoverRelease() { hoverCapture_ = false; }
void detail::UiState::hoverRefresh(const Frame& frame) {
    if (hoverSimulated_ || hoverLocked() || !hoverPosition_ || !windowActive_) return;
    OperationScope operation(this);
    hoverFromFrame(frame, hoverPosition_->first, hoverPosition_->second);
}
std::optional<HoverTarget> detail::UiState::hoverTarget() const {
    if (hover_ && hoverAdmitted(*hover_)) return hover_;
    return std::nullopt;
}
bool detail::UiState::hoverDrawn(std::string_view id, const OptionValue* item) const {
    if (presentation_.hover != Hover::on || !hover_ || hover_->id != id) return false;
    if (item ? !hover_->item || !(*hover_->item == *item) : hover_->item.has_value()) return false;
    return hoverAdmitted(*hover_);
}
Query<std::optional<HoverTarget>> detail::UiState::hoverQuery() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread", std::nullopt};
    if (stopped_) return {Status::failed, u"UI is stopped", std::nullopt};
    return {Status::ok, {}, hoverTarget()};
}
Result detail::UiState::simulateHover(std::string_view id, const std::optional<OptionValue>& item) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    if (menuOpen()) return {Status::blockedByModal, u"A context menu is open"};
    if (hoverLocked()) return {Status::busy, u"A pointer drag holds the hover"};
    const auto* control = find(id);
    if (item) {
        auto semantics = describe(id);
        if (!semantics) return {Status::notFound, u"Component no longer exists"};
        auto option = std::find_if(semantics->items.begin(), semantics->items.end(), [&](const auto& o) { return o.value == *item; });
        if (option == semantics->items.end()) return {Status::notFound, u"Item no longer exists"};
        if (!option->enabled) return {Status::disabled, u"目前無法執行"};
    }
    hoverSimulated_ = true;
    hoverSet(HoverTarget{control->id, item});
    // An item of an open dropdown lives in its popup, which never starts a tooltip.
    tooltipHover(item && componentType(control->kind).opensPopup() ? std::string_view{} : std::string_view(control->id));
    return {};
}
Result detail::UiState::clearSimulatedHover() {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!hoverSimulated_) return {};
    hoverSimulated_ = false;
    hoverSet(std::nullopt);
    tooltipHover({});
    return {};
}
Query<bool> TooltipHandle::visible() const {
    return detail::UiState::operate(*this, [&]() -> Query<bool> {
        if (auto r = ui_->configurationAccess(id_, true); !r) return {r.status, r.message, std::nullopt};
        auto it = ui_->tooltips_.find(id_);
        if (it == ui_->tooltips_.end()) return {Status::notFound, u"Tooltip no longer exists", std::nullopt};
        return {Status::ok, {}, it->second.visible};
    }, detail::UiState::uiGone);
}
bool detail::UiState::dialogMember(std::string_view id) const {
    for (const auto& [key, dialog] : dialogs_)
        if (std::find(dialog.members.begin(), dialog.members.end(), id) != dialog.members.end()) return true;
    return false;
}
DialogHandle detail::UiState::addDialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial, ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name) {
    OperationScope operation(this);
    auto first = layoutId(initial), backup = layoutId(fallback);
    if (!first || !backup || !find(*first) || !find(*backup) || removalPending(*first) || removalPending(*backup) || !result || members.empty() || !componentType(find(*first)->kind).canFocus()) throw std::invalid_argument("Invalid dialog");
    std::set<Id> unique;
    for (const auto& member : members) {
        auto item = layoutId(member);
        if (!item || !find(*item) || removalPending(*item) || pageMembers_.contains(*item) || dialogMember(*item) || !unique.insert(*item).second)
            throw std::invalid_argument("Invalid dialog member");
    }
    if (!unique.contains(*first) || unique.contains(*backup)) throw std::invalid_argument("Invalid dialog focus targets");
    auto nameId = name ? layoutId(*name) : std::optional<Id>{};
    if (name && (!nameId || !unique.contains(*nameId))) throw std::invalid_argument("Dialog name must be a member label");
    DialogState dialog;
    dialog.members.assign(unique.begin(), unique.end());
    dialog.initial = *first; dialog.fallback = *backup; dialog.result = std::move(result); dialog.configured = true;
    if (nameId) dialog.nameLabel = *nameId;
    for (const auto& member : dialog.members) const_cast<Component*>(find(member))->page = "dialog";
    dialogs_.emplace(id, std::move(dialog));
    return DialogHandle(this, lifetime_, std::move(id));
}
Result detail::UiState::openDialog(const Id& id, const ComponentHandle* opener) {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return {Status::notFound, u"Dialog not found"};
    if (rendererFailed_) return {Status::rendererFailed, u"Renderer failed"};
    auto& dialog = it->second;
    if (modal_ || dialogTransitioning_) return {Status::busy, u"Dialog already open or transitioning"};
    Id source = focus_;
    if (opener) {
        auto selected = layoutId(*opener);
        if (!selected) return {Status::notFound, u"Invalid opener"};
        if (auto r = available(*selected); !r) return r;
        source = *selected;
    }
    if (source.empty()) source = dialog.fallback;
    const auto* initial = find(dialog.initial);
    if (!initial || !initial->enabled) return {Status::disabled, u"Initial focus is unavailable"};
    dialog.transitioning = true; dialogTransitioning_ = true;
    struct Guard { bool& local; bool& global; ~Guard() { local = false; global = false; } } guard{dialog.transitioning, dialogTransitioning_};
    dialog.opener = source;
    if (auto r = moveFocus(dialog.initial); !r) return r;
    activeDialog_ = id;
    modal(true);
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::settleDialog(const Id& id, bool confirmed) {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return {Status::notFound, u"Dialog not found"};
    if (rendererFailed_) return {Status::rendererFailed, u"Renderer failed"};
    auto& dialog = it->second;
    if (!modal_ || activeDialog_ != id) return {Status::hidden, u"Dialog is not open"};
    if (dialogTransitioning_) return {Status::busy, u"Dialog is transitioning"};
    dialog.transitioning = true; dialogTransitioning_ = true;
    struct Guard { bool& local; bool& global; ~Guard() { local = false; global = false; } } guard{dialog.transitioning, dialogTransitioning_};
    modal(false); activeDialog_.clear();
    auto source = std::exchange(dialog.opener, {});
    auto target = find(source);
    if (!target || !focusable(*target)) target = find(dialog.fallback);
    if (target && focusable(*target)) moveFocus(target->id); else moveFocus({});
    try { dialog.result(confirmed); }
    catch (...) { stopped_ = true; return {Status::failed, u"Dialog result failed"}; }
    if (invalidate_) invalidate_();
    return {};
}
Result DialogHandle::open(ComponentHandle opener) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->openDialog(id_, &opener); }, detail::UiState::uiGone);
}
Result DialogHandle::open() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->openDialog(id_, nullptr); }, detail::UiState::uiGone);
}
Result DialogHandle::settle(bool confirmed) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->settleDialog(id_, confirmed); }, detail::UiState::uiGone);
}
Query<bool> DialogHandle::isOpen() const {
    return detail::UiState::operate(*this, [&]() -> Query<bool> {
        auto it = ui_->dialogs_.find(id_);
        if (it == ui_->dialogs_.end()) return {Status::notFound, u"Dialog not found", std::nullopt};
        if (auto r = ui_->configurationAccess(it->second.initial, true); !r) return {r.status, r.message, std::nullopt};
        return {Status::ok, {}, ui_->modal_ && ui_->activeDialog_ == id_};
    }, detail::UiState::uiGone);
}
Result detail::UiState::activateTab(const Id& id, OptionValue page) {
    OperationScope operation(this);
    if (auto r = availableOption(id, page); !r) return r;
    auto& t = tabs_.at(id);
    if (t.switching) return {Status::busy, u"Page change is already in progress"};
    t.switching = true;
    struct SwitchingGuard { bool& flag; ~SwitchingGuard() { flag = false; } } guard{t.switching};
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalidValue, u"Unknown page"};
    if (!it->enabled) return {Status::disabled, u"Page is disabled"};
    auto next = static_cast<std::size_t>(it - t.pages.begin());
    if (next == t.active) { t.candidate = next; return {}; }
    // Finish an edit while its page is still admitted.
    if (auto member = pageMembers_.find(focus_); member != pageMembers_.end() && member->second.first == id && member->second.second == t.active)
        if (auto r = moveFocus(id); !r) return r;
    closePopup();
    closeMenu();
    auto old = t.pages[t.active].value;
    t.active = t.candidate = next;
    refreshTooltips();
    if (t.changed) {
        try { t.changed(old, page); }
        catch (...) { stopped_ = true; return {Status::failed, u"Page change notification failed"}; }
    }
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::focusError(const Id& id) {
    OperationScope operation(this);
    if (auto member = pageMembers_.find(id); member != pageMembers_.end()) {
        auto& tabs = tabs_.at(member->second.first);
        auto page = tabs.pages[member->second.second].value;
        if (auto result = activateTab(member->second.first, page); !result) return result;
    }
    return focus(id);
}
Result detail::UiState::focusPage(const Id& id, OptionValue page) {
    if (auto r = availableOption(id, page); !r) return r;
    auto& t = tabs_.at(id);
    const auto target = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& item) { return item.value == page; });
    if (target == t.pages.end()) return {Status::notFound, {}};
    if (!target->enabled) return {Status::disabled, {}};
    const auto index = static_cast<std::size_t>(target - t.pages.begin());
    if (auto r = focus(id); !r) return r;
    // Blur callbacks can change page admission while focus moves.
    if (auto r = availableOption(id, page); !r) return r;
    if (!target->enabled) return {Status::disabled, {}};
    t.candidate = index;
    if (revealRequested_) revealRequested_(id);
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::addPageMember(const Id& id, OptionValue page, ComponentHandle component) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto member = layoutId(component);
    if (!member || !find(*member) || removalPending(*member) || pageMembers_.contains(*member) ||
        dialogMember(*member))
        return {Status::invalidValue, u"Invalid page member"};
    auto& t = tabs_.at(id);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalidValue, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    it->members.push_back(std::move(component));
    pageMembers_.emplace(*member, std::make_pair(id, index));
    if (invalidate_) invalidate_();
    return {};
}
// Tabs and a navigation list are one component inside (#94): both handles run these operations, and differ only in the
// names of activate / select and active / selected.
Result detail::UiState::enablePage(const Id& id, const OptionValue& page, bool enabled) {
    if (auto r = configurationAccess(id); !r) return r;
    auto& t = tabs_.at(id);
    auto it = std::find_if(t.pages.begin(), t.pages.end(), [&](const auto& p) { return p.value == page; });
    if (it == t.pages.end()) return {Status::invalidValue, u"Unknown page"};
    auto index = static_cast<std::size_t>(it - t.pages.begin());
    if (!enabled && index == t.active) return {Status::invalidValue, u"Active page cannot be disabled"};
    it->enabled = enabled;
    if (!enabled && index == t.candidate) t.candidate = t.active;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::onPageChange(const Id& id, std::function<void(OptionValue, OptionValue)> callback) {
    if (auto r = configurationAccess(id); !r) return r;
    tabs_.at(id).changed = std::move(callback);
    return {};
}
Query<OptionValue> detail::UiState::activePage(const Id& id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    const auto& t = tabs_.at(id);
    return {Status::ok, {}, t.pages[t.active].value};
}
Result TabsHandle::activate(OptionValue page) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->activateTab(id_, std::move(page)); });
}
Result TabsHandle::addMember(OptionValue page, ComponentHandle component) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->addPageMember(id_, std::move(page), std::move(component)); });
}
Result TabsHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result TabsHandle::focus(OptionValue page) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focusPage(id_, std::move(page)); });
}
Result TabsHandle::enablePage(OptionValue page, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->enablePage(id_, page, enabled); });
}
Result TabsHandle::onPageChange(std::function<void(OptionValue, OptionValue)> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->onPageChange(id_, std::move(callback)); });
}
Query<ComponentSnapshot> TabsHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<OptionValue> TabsHandle::active() const {
    return detail::UiState::operate(*this, [&]() -> Query<OptionValue> { return ui_->activePage(id_); });
}
Result NavigationListHandle::select(OptionValue page) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->activateTab(id_, std::move(page)); });
}
Result NavigationListHandle::addMember(OptionValue page, ComponentHandle component) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->addPageMember(id_, std::move(page), std::move(component)); });
}
Result NavigationListHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result NavigationListHandle::focus(OptionValue page) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focusPage(id_, std::move(page)); });
}
Result NavigationListHandle::enablePage(OptionValue page, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->enablePage(id_, page, enabled); });
}
Result NavigationListHandle::onPageChange(std::function<void(OptionValue, OptionValue)> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->onPageChange(id_, std::move(callback)); });
}
Query<ComponentSnapshot> NavigationListHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<OptionValue> NavigationListHandle::selected() const {
    return detail::UiState::operate(*this, [&]() -> Query<OptionValue> { return ui_->activePage(id_); });
}
Result LabelHandle::connectSource(std::function<std::u16string()> read) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (!read) return {Status::invalidValue, u"A label source requires a reader"};
        return ui_->bindText(id_, std::move(read));
    });
}
Result LabelHandle::connectSource(Source<std::u16string> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"A label source requires read and subscribe"};
        return ui_->bindText(id_, std::move(source.read), std::move(source.subscribe));
    });
}
Result detail::UiState::writingSupport(const Component& control) {
    if (control.verticalDigits < 0 || control.verticalDigits > maxVerticalDigits)
        return {Status::invalidValue, u"The digit limit of vertical writing is 0-3"};
    if (!detail::componentType(control.kind).supportsVerticalWriting() && control.writingMode == WritingMode::vertical)
        return {Status::invalidValue, u"Only labels support vertical writing"};
    return {};
}
Result LabelHandle::setWritingMode(WritingMode mode) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (mode != WritingMode::horizontal && mode != WritingMode::vertical) return {Status::invalidValue, u"Unknown writing mode"};
        ui_->setLabelWriting(id_, mode, std::nullopt);
        return {};
    });
}
Query<WritingMode> LabelHandle::writingMode() const {
    return detail::UiState::operate(*this, [&]() -> Query<WritingMode> {
        if (auto result = ui_->configurationAccess(id_, true); !result) return {result.status, result.message, std::nullopt};
        return {Status::ok, {}, ui_->find(id_)->writingMode};
    });
}
Result LabelHandle::setVerticalDigits(int digits) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (digits < 0 || digits > maxVerticalDigits) return {Status::invalidValue, u"The digit limit of vertical writing is 0-3"};
        ui_->setLabelWriting(id_, std::nullopt, digits);
        return {};
    });
}
Query<int> LabelHandle::verticalDigits() const {
    return detail::UiState::operate(*this, [&]() -> Query<int> {
        if (auto result = ui_->configurationAccess(id_, true); !result) return {result.status, result.message, std::nullopt};
        return {Status::ok, {}, ui_->find(id_)->verticalDigits};
    });
}
void detail::UiState::setLabelWriting(std::string_view id, std::optional<WritingMode> mode, std::optional<int> digits) {
    OperationScope operation(this);
    auto* control = const_cast<Component*>(find(id));
    if (!control) return;
    bool changed = (mode && *mode != control->writingMode) || (digits && *digits != control->verticalDigits);
    if (mode) control->writingMode = *mode;
    if (digits) control->verticalDigits = *digits;
    if (changed && invalidate_) invalidate_();
}
Result ButtonHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result ButtonHandle::bind(std::function<Result()> action) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (!action) return {Status::invalidValue, u"An action is required"};
        if (ui_->confirmationButtons_.contains(id_)) return {Status::invalidValue, u"Confirmation action cannot be rebound"};
        ui_->bind(id_, std::move(action));
        return {};
    });
}
Result ButtonHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        ui_->enable(id_, enabled);
        return {};
    });
}
Result detail::UiState::removeComponent(const Id& id) {
    if (auto r = configurationAccess(id); !r) return r;
    if (dialogMember(id) || std::any_of(dialogs_.begin(), dialogs_.end(), [&](const auto& entry) {
        return entry.second.initial == id || entry.second.fallback == id || entry.second.nameLabel == id;
    })) return {Status::invalidValue, u"Dialog references must remain alive"};
    pendingRemovals_.push_back(id);
    return {};
}
Result ButtonHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result TextFieldHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result LabelHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result CheckboxHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result RadioGroupHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result SliderHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result ProgressHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result DropdownHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}
Result ListHandle::remove() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->removeComponent(id_); });
}

Result TextFieldHandle::focus() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->focus(id_); });
}
Result SliderHandle::onBlur(std::function<void()> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (callback) ui_->blurCallbacks_.insert_or_assign(id_, std::move(callback));
        else ui_->blurCallbacks_.erase(id_);
        return {};
    });
}
Result TextFieldHandle::onBlur(std::function<void()> callback) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (callback) ui_->blurCallbacks_.insert_or_assign(id_, std::move(callback));
        else ui_->blurCallbacks_.erase(id_);
        return {};
    });
}
Result TextFieldHandle::setError(std::u16string message) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setError(id_, std::move(message)); });
}
Result TextFieldHandle::enable(bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        ui_->enable(id_, enabled);
        return {};
    });
}
Result TextFieldHandle::readOnly(bool isReadOnly) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        ui_->setReadOnly(id_, isReadOnly);
        return {};
    });
}
Result TextFieldHandle::resetToSource() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->resetToSource(id_); });
}
Result detail::UiState::resetToSource(const Id& id) {
    OperationScope operation(this);
    if (auto result = configurationAccess(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.connected)
        return {Status::invalidValue, u"Text source is not connected"};
    replaceRich(id, RichText{it->second.sourceValue, it->second.sourceSpans});
    it->second.origin = it->second.sourceValue;
    it->second.originSpans = it->second.sourceSpans;
    return setError(id, u"");
}
Result ButtonHandle::invoke() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->invoke(id_); });
}
Query<bool> ButtonHandle::executing() const { return semanticProperty(describe(), &ComponentSnapshot::executing); }
Result TextFieldHandle::paste(std::u16string_view text) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->paste(id_, text); });
}
Result TextFieldHandle::undo() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->undo(id_); });
}
Result TextFieldHandle::redo() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->redo(id_); });
}
Result TextFieldHandle::select(unsigned anchor, unsigned caret) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->select(id_, anchor, caret); });
}
Result TextFieldHandle::copy() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->copy(id_); });
}
Result TextFieldHandle::cut() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->cut(id_); });
}
Result detail::UiState::configurationAccess(std::string_view id, bool readOnly) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (!find(id) || removalPending(id)) return {Status::notFound, u"Component no longer exists"};
    if (rendererFailed_ && !readOnly) return {Status::rendererFailed, u"Renderer failed"};
    return {};
}
// A confirmation group member's value and last written value as its change record; a rich text field's carries its styles.
template <class Handle, class FieldState>
static ConfirmFieldChange pendingChange(const Handle& handle, const FieldState& f) {
    if constexpr (std::is_same_v<Handle, TextFieldHandle>) {
        if (f.rich) return Change<TextFieldHandle, RichText>{handle, RichText{f.session.text, f.spans}, RichText{f.origin, f.originSpans}};
        return Change<TextFieldHandle>{handle, f.session.text, f.origin};
    } else return typename FieldState::Change{handle, f.value, f.origin};
}
// After the group's write succeeds: the value its change record handed over becomes the member's origin.
template <class FieldState>
static void adoptWritten(FieldState& f, const ConfirmFieldChange& written) {
    if constexpr (std::is_same_v<typename FieldState::Change, Change<TextFieldHandle>>) {
        if (const auto* rich = std::get_if<Change<TextFieldHandle, RichText>>(&written)) {
            f.origin = rich->value.text; f.originSpans = rich->value.spans; return;
        }
    }
    f.origin = std::get<typename FieldState::Change>(written).value;
}
Result detail::UiState::connectConfirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<ConfirmationResult(const std::vector<ConfirmFieldChange>&)> submit) {
    OperationScope operation(this);
    if (button.ui_ != this || button.lifetime_.expired() || !submit || fields.empty())
        return {Status::invalidValue, u"Invalid confirmation group"};
    if (auto r = configurationAccess(button.id_); !r) return r;
    if (actions_.contains(button.id_)) return {Status::invalidValue, u"Confirmation button already has an action"};
    std::set<Id> members;
    for (const auto& member : fields) {
        bool valid = std::visit([&](const auto& h) {
            if (h.ui_ != this || h.lifetime_.expired() || removalPending(h.id_) || !members.insert(h.id_).second) return false;
            auto& state = fieldsOf<std::decay_t<decltype(h)>>();
            auto it = state.find(h.id_);
            return it != state.end() && it->second.joinable();
        }, member);
        if (!valid) return {Status::invalidValue, u"Confirmation fields must be distinct, connected and ungrouped"};
    }
    for (const auto& member : fields) std::visit([&](const auto& h) { fieldsOf<std::decay_t<decltype(h)>>().at(h.id_).confirmationMember = true; }, member);
    confirmationButtons_.insert(button.id_);
    confirmationMembers_[button.id_] = {members.begin(), members.end()};
    bind(button.id_, [this, fields = std::move(fields), submit = std::move(submit)] {
        // The action's handles retain identity, while membership can shrink between invocations.
        auto remaining = fields;
        std::erase_if(remaining, [&](const auto& member) {
            return std::visit([&](const auto& h) { return h.lifetime_.expired() || removalPending(h.id_); }, member);
        });
        if (remaining.empty()) return Result{};
        std::vector<ConfirmFieldChange> changes;
        for (const auto& member : remaining) std::visit([&](const auto& h) {
            auto& f = fieldsOf<std::decay_t<decltype(h)>>().at(h.id_);
            changes.push_back(pendingChange(h, f));
            f.submitting = true;
        }, member);
        ConfirmationResult outcome;
        try { outcome = submit(changes); }
        catch (...) {
            for (const auto& member : remaining) std::visit([&](const auto& h) { fieldsOf<std::decay_t<decltype(h)>>().at(h.id_).submitting = false; }, member);
            throw;
        }
        for (std::size_t i = 0; i < remaining.size(); ++i) std::visit([&](const auto& h) {
            auto& f = fieldsOf<std::decay_t<decltype(h)>>().at(h.id_);
            f.submitting = false;
            if (outcome.result) { adoptWritten(f, changes[i]); setError(h.id_, u""); }
        }, remaining[i]);
        if (!outcome.result) {
            std::optional<std::size_t> first;
            for (const auto& error : outcome.errors) {
                auto id = std::visit([&](const auto& h) { return layoutId(ComponentHandle{h}); }, error.field);
                if (!id) continue;
                for (std::size_t i = 0; i < remaining.size(); ++i) {
                    auto fieldId = std::visit([&](const auto& h) { return h.id_; }, remaining[i]);
                    if (fieldId == *id) { setError(*id, error.message); if (!error.message.empty() && (!first || i < *first)) first = i; break; }
                }
            }
            if (first) std::visit([&](const auto& h) { focusError(h.id_); }, remaining[*first]);
        }
        auto notification = notify(); return notification ? outcome.result : notification;
    });
    return {};
}
Result TextFieldHandle::connectImmediate(std::function<Result(const Change<TextFieldHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (ui_->isRich(id_)) return {Status::notSupported, u"A rich field connects with connect_rich_immediate"};
        return ui_->connectWrite(id_, std::move(write), true);
    });
}
Result TextFieldHandle::connectConfirmed(std::function<Result(const Change<TextFieldHandle>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (ui_->isRich(id_)) return {Status::notSupported, u"A rich field connects with connect_rich_confirmed"};
        return ui_->connectWrite(id_, std::move(write), false);
    });
}
Result TextFieldHandle::connectRichImmediate(std::function<Result(const Change<TextFieldHandle, RichText>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->connectRichWrite(id_, std::move(write), true); });
}
Result TextFieldHandle::connectRichConfirmed(std::function<Result(const Change<TextFieldHandle, RichText>&)> write) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->connectRichWrite(id_, std::move(write), false); });
}
// The shared write path hands over a plain Change<TextFieldHandle>; a rich write reads the field's spans at that moment.
Result detail::UiState::connectRichWrite(const Id& id, std::function<Result(const Change<TextFieldHandle, RichText>&)> write, bool immediate) {
    OperationScope operation(this);
    if (auto result = configurationAccess(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::notSupported, u"Not a rich text field"};
    if (!write) return {Status::invalidValue, u"Write needs an ungrouped connected source"};
    return connectWrite(id, [this, write = std::move(write)](const Change<TextFieldHandle>& change) {
        auto& field = edits_.at(change.field.id_);
        return write(Change<TextFieldHandle, RichText>{change.field, RichText{change.value, field.spans}, RichText{change.origin, field.originSpans}});
    }, immediate);
}
Result detail::UiState::connectWrite(const Id& id, std::function<Result(const Change<TextFieldHandle>&)> write, bool immediate) {
    OperationScope operation(this);
    if (auto result = configurationAccess(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalidValue, u"Write needs an ungrouped connected source"};
    return connectFieldWrite(it->second, std::move(write), immediate);
}
Result TextFieldHandle::confirm() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->confirm(id_); });
}
Result detail::UiState::confirm(const Id& id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.confirmed)
        return {Status::invalidValue, u"No confirmed write is connected"};
    auto& field = it->second;
    if (field.submitting || field.session.composing || dispatching_ || invoking_)
        return {Status::busy, u"Operation is already executing"};
    return submitField(id, field, field.confirmed, true);
}
Result TextFieldHandle::connectSource(Source<std::u16string> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (ui_->isRich(id_)) return {Status::notSupported, u"A rich field connects with connect_rich_source"};
        return ui_->connectSource(id_, std::move(source));
    });
}
Result TextFieldHandle::connectRichSource(Source<RichText> source) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        if (auto result = ui_->configurationAccess(id_); !result) return result;
        if (!ui_->isRich(id_)) return {Status::notSupported, u"Not a rich text field"};
        if (!source.read || !source.subscribe) return {Status::invalidValue, u"A rich text source requires read and subscribe"};
        return ui_->connectSource(id_, std::move(source));
    });
}
Result detail::UiState::connectSource(const Id& id, TextFieldSource source) {
    OperationScope operation(this);
    if (auto result = configurationAccess(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notFound, u"Component no longer exists"};
    if (!std::visit([](const auto& s) { return s.read && s.subscribe; }, source))
        return {Status::invalidValue, u"A text source requires read and subscribe"};
    auto weak = std::weak_ptr<void>(componentLifetimes_.at(id));
    auto changed = [weak, this, id] { if (!weak.expired() && !removalPending(id)) sourceChanged(id); };
    auto& field = it->second;
    if (auto r = replaceSource(field.source, field.disconnect, std::move(source),
                                [&](const TextFieldSource& next) { return std::visit([&](const auto& s) { return s.subscribe(changed); }, next); }); !r)
        return r;
    field.connected = false;
    return readInstalledSource([&] { sourceChanged(id); });
}
void detail::UiState::sourceChanged(const Id& id) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || !std::visit([](const auto& s) { return bool(s.read); }, it->second.source)) return;
    auto& field = it->second;
    RichText latest;
    if (const auto* rich = std::get_if<Source<RichText>>(&field.source)) {  // only a value in normal form (or one that normalizes) is adopted
        auto read = normalizeRichText(rich->read(), find(id)->maxLength);
        if (!read) return;
        latest = std::move(*read);
    } else {
        latest.text = std::get<Source<std::u16string>>(field.source).read();
        if (!validValue(*find(id), latest.text)) return;
    }
    const bool clean = !field.connected || sourceMatches(field);
    field.sourceValue = latest.text;
    field.sourceSpans = latest.spans;
    field.connected = true;
    if (clean && !field.submitting && !field.session.composing) {
        field.origin = latest.text;
        field.originSpans = latest.spans;
        replaceRich(id, std::move(latest));
    }
    if (invalidate_) invalidate_();
}
Query<bool> TextFieldHandle::sourceConflict() const { return semanticProperty(describe(), &ComponentSnapshot::sourceConflict); }
Result TextFieldHandle::useLatest() const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->useLatest(id_); });
}
Result detail::UiState::useLatest(const Id& id) {
    OperationScope operation(this);
    if (auto result = configurationAccess(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.connected) return {Status::invalidValue, u"Text source is not connected"};
    auto latest = it->second.sourceValue;
    auto latestSpans = it->second.sourceSpans;
    const bool wasSubmitting = it->second.submitting;
    it->second.submitting = true;
    auto result = it->second.rich ? setRichValue(id, RichText{latest, latestSpans}) : setValue(id, latest);
    it->second.submitting = wasSubmitting;
    if (result) {
        it->second.origin = std::move(latest);
        it->second.originSpans = std::move(latestSpans);
        setError(id, u"");
    }
    return result;
}
Result TextFieldHandle::setValue(std::u16string value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setValue(id_, std::move(value)); });
}
Result TextFieldHandle::setRichValue(RichText value) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setRichValue(id_, std::move(value)); });
}
Query<RichText> TextFieldHandle::richValue() const {
    return detail::UiState::operate(*this, [&]() -> Query<RichText> { return ui_->richValue(id_); });
}
Result TextFieldHandle::setStyle(unsigned begin, unsigned end, StyleChange change) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setStyle(id_, begin, end, change); });
}
Result TextFieldHandle::toggleStyle(StyleAttribute attribute) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->toggleStyle(id_, attribute); });
}
Result TextFieldHandle::applyStyle(StyleChange change) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->applyStyle(id_, change); });
}
Query<TextStyle> TextFieldHandle::typingStyle() const {
    return detail::UiState::operate(*this, [&]() -> Query<TextStyle> { return ui_->typingStyle(id_); });
}
Result TextFieldHandle::setTypingStyle(TextStyle style) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setTypingStyle(id_, std::move(style)); });
}
Result TextFieldHandle::setColors(std::vector<NamedColor> colors) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setColors(id_, std::move(colors)); });
}
Query<std::vector<NamedColor>> TextFieldHandle::colors() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::vector<NamedColor>> { return ui_->colors(id_); });
}
Query<ComponentSnapshot> TextFieldHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Result TextFieldHandle::setVisibleLines(unsigned lines) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setVisibleLines(id_, lines); });
}
Query<unsigned> TextFieldHandle::visibleLines() const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> {
        if (auto r = ui_->configurationAccess(id_, true); !r) return {r.status, r.message, std::nullopt};
        const auto* control = ui_->find(id_);
        if (!control->multiline) return {Status::notSupported, u"Not a multi-line field", std::nullopt};
        return {Status::ok, {}, control->visibleLines};
    });
}
Result TextFieldHandle::setScrollLine(unsigned firstVisibleLine) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setScrollLine(id_, firstVisibleLine); });
}
Query<unsigned> TextFieldHandle::scrollLine() const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> { return ui_->scrollLine(id_); });
}
Result TextFieldHandle::setWordSegmentation(WordSegmentation mode) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setWordSegmentation(id_, mode); });
}
Query<WordSegmentation> TextFieldHandle::wordSegmentation() const {
    return detail::UiState::operate(*this, [&]() -> Query<WordSegmentation> { return ui_->wordSegmentation(id_); });
}
Query<WordSegmentation> TextFieldHandle::wordSegmentationInUse() const {
    return detail::UiState::operate(*this, [&]() -> Query<WordSegmentation> { return ui_->wordSegmentationInUse(id_); });
}
Query<TextSpan> TextFieldHandle::wordAt(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<TextSpan> { return ui_->wordAt(id_, offset); });
}
Query<TextSpan> TextFieldHandle::paragraphAt(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<TextSpan> { return ui_->paragraphAt(id_, offset); });
}
Query<unsigned> TextFieldHandle::nextWordStop(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> { return ui_->wordStop(id_, offset, true); });
}
Query<unsigned> TextFieldHandle::previousWordStop(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Query<unsigned> { return ui_->wordStop(id_, offset, false); });
}
Result TextFieldHandle::selectWord(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->selectWord(id_, offset); });
}
Result TextFieldHandle::selectParagraph(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->selectParagraph(id_, offset); });
}
Result TextFieldHandle::doubleClick(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->doubleClick(id_, offset); });
}
Result TextFieldHandle::tripleClick(unsigned offset) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->tripleClick(id_, offset); });
}
Result detail::UiState::setVisibleLines(std::string_view id, unsigned lines) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto* control = const_cast<Component*>(find(id));
    if (!control->multiline) return {Status::notSupported, u"Not a multi-line field"};
    if (lines < 1 || lines > maxVisibleLines) return {Status::invalidValue, u"A multi-line field shows at least one line"};
    control->visibleLines = lines;
    auto& m = edits_.find(id)->second.multi;
    if (m.placed) {  // keep the position valid for the new page size until the next arrange settles it
        m.view.visible = lines;
        const auto count = static_cast<unsigned>(m.view.lines.size());
        scrollLinesTo(id, std::min(m.first, count > lines ? count - lines : 0));
    }
    if (invalidate_) invalidate_();
    return {};
}
// Text flow (#80): owner configuration shared by the typed handles and the semantic verbs.
Result detail::UiState::setTextAlign(std::string_view id, TextAlign align) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto* control = const_cast<Component*>(find(id));
    if (!supportsTextFlow(control->kind)) return {Status::notSupported, u"Only labels and text fields align their text"};
    if (!validTextAlign(align)) return {Status::invalidValue, u"Unknown alignment"};
    if (!admitsTextAlign(control->kind, align)) return {Status::invalidValue, u"Only a label can distribute its text"};
    control->textAlign = align;
    ++layoutRevision_;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::setJustifyMethod(std::string_view id, JustifyMethod method) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto* control = const_cast<Component*>(find(id));
    if (!supportsTextFlow(control->kind)) return {Status::notSupported, u"Only labels and text fields justify their text"};
    if (!validJustifyMethod(method)) return {Status::invalidValue, u"Unknown justification method"};
    control->justifyMethod = method;
    ++layoutRevision_;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::setLineSpacing(std::string_view id, double multiple) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto* control = const_cast<Component*>(find(id));
    if (!supportsTextFlow(control->kind)) return {Status::notSupported, u"Only labels and text fields have line spacing"};
    if (!validLineSpacing(multiple)) return {Status::invalidValue, u"Line spacing is 1.0 to 3.0 times the line height"};
    control->lineSpacing = multiple;
    ++layoutRevision_;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::setParagraphSpacing(std::string_view id, int dip) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto* control = const_cast<Component*>(find(id));
    if (!supportsTextFlow(control->kind)) return {Status::notSupported, u"Only labels and text fields have paragraph spacing"};
    if (!validParagraphSpacing(dip)) return {Status::invalidValue, u"Paragraph spacing is 0 to 48 DIP"};
    control->paragraphSpacing = dip;
    ++layoutRevision_;
    if (invalidate_) invalidate_();
    return {};
}
// Text flow (#80, #84): one implementation for the text field and the label (#94). The setters run the UiState operations
// above, the getters read the handle's own description; both handles declare the same eight operations.
Result TextFieldHandle::setTextAlign(TextAlign align) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setTextAlign(id_, align); });
}
Query<TextAlign> TextFieldHandle::textAlign() const { return semanticProperty(describe(), &ComponentSnapshot::textAlign); }
Result TextFieldHandle::setJustifyMethod(JustifyMethod method) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setJustifyMethod(id_, method); });
}
Query<JustifyMethod> TextFieldHandle::justifyMethod() const { return semanticProperty(describe(), &ComponentSnapshot::justifyMethod); }
Result TextFieldHandle::setLineSpacing(double multiple) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setLineSpacing(id_, multiple); });
}
Query<double> TextFieldHandle::lineSpacing() const { return semanticProperty(describe(), &ComponentSnapshot::lineSpacing); }
Result TextFieldHandle::setParagraphSpacing(int dip) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setParagraphSpacing(id_, dip); });
}
Query<int> TextFieldHandle::paragraphSpacing() const { return semanticProperty(describe(), &ComponentSnapshot::paragraphSpacing); }
Result LabelHandle::setTextAlign(TextAlign align) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setTextAlign(id_, align); });
}
Query<TextAlign> LabelHandle::textAlign() const { return semanticProperty(describe(), &ComponentSnapshot::textAlign); }
Result LabelHandle::setJustifyMethod(JustifyMethod method) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setJustifyMethod(id_, method); });
}
Query<JustifyMethod> LabelHandle::justifyMethod() const { return semanticProperty(describe(), &ComponentSnapshot::justifyMethod); }
Result LabelHandle::setLineSpacing(double multiple) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setLineSpacing(id_, multiple); });
}
Query<double> LabelHandle::lineSpacing() const { return semanticProperty(describe(), &ComponentSnapshot::lineSpacing); }
Result LabelHandle::setParagraphSpacing(int dip) const {
    return detail::UiState::operate(*this, [&]() -> Result { return ui_->setParagraphSpacing(id_, dip); });
}
Query<int> LabelHandle::paragraphSpacing() const { return semanticProperty(describe(), &ComponentSnapshot::paragraphSpacing); }
Result detail::UiState::setScrollLine(std::string_view id, unsigned first) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    if (!find(id)->multiline) return {Status::notSupported, u"Not a multi-line field"};
    auto& m = edits_.find(id)->second.multi;
    if (!m.placed) { m.pendingFirst = first; if (invalidate_) invalidate_(); return {}; }  // clamped by the first arrange
    const auto count = static_cast<unsigned>(m.view.lines.size());
    if (first > (count > m.view.visible ? count - m.view.visible : 0)) return {Status::invalidValue, u"No such scroll position"};
    scrollLinesTo(id, first);
    return {};
}
Query<unsigned> detail::UiState::scrollLine(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    if (!find(id)->multiline) return {Status::notSupported, u"Not a multi-line field", std::nullopt};
    const auto& m = edits_.find(id)->second.multi;
    return {Status::ok, {}, m.pendingFirst.value_or(m.first)};
}
Query<std::u16string> TextFieldHandle::draft() const { return semanticProperty(describe(), &ComponentSnapshot::value); }
Query<ComponentSnapshot> ButtonHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
Query<ComponentSnapshot> LabelHandle::describe() const {
    return detail::UiState::operate(*this, [&]() -> Query<ComponentSnapshot> { return ui_->handleDescribe(id_); });
}
// What every component handle's describe() answers once its lifetime has been checked.
Query<ComponentSnapshot> detail::UiState::handleDescribe(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    return {Status::ok, {}, describe(id)};
}
std::optional<ComponentSnapshot> detail::UiState::describe(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return std::nullopt;
    ComponentSnapshot s;
    s.id = control->id; s.kind = control->kind; s.target = control->target; s.labelKind = control->labelKind;
    s.enabled = control->enabled; s.readOnly = control->readOnly;
    // The arranged, clipped bounds include attached label lines as well as standalone components.
    // Both public snapshots use this one record; visibility_ remains the host's tooltip eligibility record.
    s.visible = semanticBounds_.contains(control->id) && available(id, true).status != Status::hidden;
    auto textScroll = textScrolls_.find(id);
    s.focusable = bool(available(id, true)) && (componentType(control->kind).canFocus() || (textScroll != textScrolls_.end() && textScroll->second.maximum > 0));
    // An open context menu owns keyboard input; focus_ only remembers its component for restoration.
    s.focused = focus_ == control->id && !menu_;
    s.executing = executing_ == control->id;
    s.name = control->label; s.value = text(id);
    s.textAlign = control->textAlign; s.justifyMethod = control->justifyMethod; s.lineSpacing = control->lineSpacing; s.paragraphSpacing = control->paragraphSpacing;
    if (auto it = errors_.find(id); it != errors_.end()) s.error = it->second;
    s.invalid = !s.error.empty();
    for (const auto& other : controls_) {
        if (other.target != id || removalPending(other.id)) continue;
        s.labels.push_back(other.id);
        if (other.labelKind == LabelKind::name) s.name = text(other.id);
        else if (other.labelKind == LabelKind::description) s.description = text(other.id);
    }
    if (auto tip = tooltips_.find(id); tip != tooltips_.end()) {
        if (!s.description.empty()) s.description += u"\n";
        s.description += tip->second.text;
    }
    componentType(control->kind).describe(*this, *control, s);
    s.verbs = snapshotVerbs(*control, s);
    return s;
}
std::vector<Verb> detail::UiState::snapshotVerbs(const Component& control, const ComponentSnapshot& s, bool item) const {
    std::vector<Verb> verbs;
    if (!s.enabled || available(control.id, true).status == Status::hidden || (modal_ && control.page != "dialog")) return verbs;
    const auto& type = componentType(control.kind);
    std::vector<Admission> admissions;
    if (s.focusable && !rendererFailed_) {
        admissions.push_back(Admission::focus);
        type.describeAdmissions(*this, control, s, admissions);
    }
    type.verbs({*this, control, s, admissions, item}, verbs);
    if (!item && type.confirmPending(*this, control) && !s.readOnly) verbs.push_back(Verb::confirm);
    return verbs;
}
std::u16string detail::UiState::text(std::string_view id) const {
    const auto* control = find(id);
    if (!control) return {};
    if (auto shown = componentType(control->kind).displayText(*this, *control)) return *shown;
    return control->value;
}
Result detail::UiState::bindText(std::string_view id, std::function<std::u16string()> provider,
                                  decltype(Source<std::u16string>::subscribe) subscribe) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (!control || !componentType(control->kind).acceptsTextProvider() || !provider) throw std::invalid_argument("Text binding requires a label and a provider");
    auto [entry, added] = providers_.try_emplace(control->id);
    auto& bound = entry->second;
    if (subscribe) {
        std::weak_ptr<void> weak = componentLifetimes_.at(control->id); auto* state = this; auto target = control->id;
        auto changed = [weak, state, target] { if (!weak.expired() && !state->removalPending(target) && state->invalidate_) state->invalidate_(); };
        auto result = replaceSource(bound.read, bound.disconnect, std::move(provider), [&](const auto&) { return subscribe(changed); });
        if (added && !bound.read) providers_.erase(entry);  // a first binding whose subscribe threw leaves the label unbound
        return result;
    }
    bound.read = std::move(provider);
    if (auto disconnect = std::exchange(bound.disconnect, {})) {
        try { disconnect(); } catch (...) { stopped_ = true; return {Status::failed, u"Unsubscription failed"}; }
    }
    return {};
}
const detail::Component* detail::UiState::find(std::string_view id) const {
    auto it = std::find_if(controls_.begin(), controls_.end(), [&](const auto& control) { return control.id == id; });
    return it == controls_.end() ? nullptr : &*it;
}
EditSession* detail::UiState::edit(std::string_view id) {
    OperationScope operation(this);
    auto it = edits_.find(id); return it == edits_.end() ? nullptr : &it->second.session;
}
Result detail::UiState::available(std::string_view id, bool ignoreRendererFailure) const {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    auto control = find(id);
    if (!control || removalPending(id)) return {Status::notFound, u"Component no longer exists"};
    if (rendererFailed_ && !ignoreRendererFailure) return {Status::rendererFailed, u"Renderer failed"};
    if (auto member = pageMembers_.find(id); member != pageMembers_.end() && tabs_.at(member->second.first).active != member->second.second)
        return {Status::hidden, u"Page is hidden"};
    if (control->page == "dialog" && (!modal_ || (dialogMember(id) && (activeDialog_.empty() || std::find(dialogs_.at(activeDialog_).members.begin(), dialogs_.at(activeDialog_).members.end(), id) == dialogs_.at(activeDialog_).members.end())))) return {Status::hidden, u"對話框尚未開啟"};
    if (!control->enabled) return {Status::disabled, u"目前無法執行"};
    if (modal_ && control->page != "dialog") return {Status::blockedByModal, u"請先關閉對話框"};
    return {};
}
Result detail::UiState::availableOption(std::string_view id, const OptionValue& value) const {
    auto result = available(id);
    if (result.status == Status::blockedByModal) {
        auto snapshot = describe(id);
        if (snapshot) {
            auto option = std::find_if(snapshot->items.begin(), snapshot->items.end(), [&](const auto& o) { return o.value == value; });
            if (option != snapshot->items.end() && !option->enabled) return {Status::disabled, {}};
        }
    }
    return result;
}
Result detail::UiState::setValue(std::string_view id, std::u16string value) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.text == value && field->spans.empty()) return {};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    // On a rich field the new text is unstyled: the inserted spans are empty, not the typing style.
    static const std::vector<StyleSpan> unstyled;
    return replaceRange(id, *field, 0, static_cast<unsigned>(field->session.text.size()), value, field->rich ? &unstyled : nullptr);
}
Result detail::UiState::writable(std::string_view id, Field*& field) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalidValue, u"元件不是文字欄位"};
    if (find(id)->readOnly) return {Status::readOnly, u"欄位為唯讀"};
    if (!it->second.writable())
        return {Status::invalidValue, u"Connect an explicit write timing before editing"};
    if (it->second.session.composing || dispatching_) return {Status::busy, u"操作尚未結束"};
    field = &it->second;
    return {};
}
// Every content change funnels through here so history, value mirror and notification agree.
Result detail::UiState::replaceSelection(std::string_view id, Field& field, std::u16string_view insertion) {
    OperationScope operation(this);
    return replaceRange(id, field, field.session.begin(), field.session.end(), insertion, nullptr);
}
Result detail::UiState::replaceRange(std::string_view id, Field& field, unsigned first, unsigned last, std::u16string_view insertion,
                                      const std::vector<StyleSpan>* insertedSpans) {
    OperationScope operation(this);
    auto& session = field.session;
    auto next = session.text;
    next.replace(first, last - first, insertion);
    const auto& control = *find(id);
    const auto limit = control.maxLength;
    if (!validValue(control, next)) {
        auto digits = std::to_string(limit);
        return {Status::invalidValue, std::u16string(control.multiline ? u"請輸入有效文字（最多 " : u"請輸入有效單行文字（最多 ") + std::u16string(digits.begin(), digits.end()) + u" 個 UTF-16 單位）"};
    }
    auto caret = first + static_cast<unsigned>(insertion.size());
    // Typed or plain-pasted text takes the typing style; inserted spans (a rich paste, a rich value) bring their own.
    std::vector<StyleSpan> nextSpans;
    if (field.rich) {
        const auto length = static_cast<unsigned>(insertion.size());
        nextSpans = detail::spliceSpans(field.spans, first, last - first, length,
                                          insertedSpans ? *insertedSpans : detail::uniformSpans(length, insertionStyle(field, first, last)));
    }
    if (next == session.text && nextSpans == field.spans) { session.select(caret, caret); return {}; }
    if (field.connected && !field.submitting && sourceMatches(field)) {
        field.origin = field.sourceValue;
        field.originSpans = field.sourceSpans;
    }
    field.past.push_back({session.text, session.selection, field.spans});
    field.future.clear();
    session.text = std::move(next);
    field.spans = std::move(nextSpans);
    field.typing.reset();
    ++session.revision;
    session.select(caret, caret);
    return finishEdit(id, field);
}
// The style of text inserted at [first, last): the temporary typing style while the caret has not moved, else the
// character before (a paragraph's first character takes the one after it; an empty paragraph is plain).
TextStyle detail::UiState::insertionStyle(const Field& field, unsigned first, unsigned last) const {
    if (field.typing && field.typingMoves == field.session.selectionChanges) return *field.typing;
    const auto& text = field.session.text;
    if (first > 0 && text[first - 1] != u'\n') return detail::styleAt(field.spans, first - 1);
    if (last < text.size() && text[last] != u'\n') return detail::styleAt(field.spans, last);
    return {};
}
bool detail::UiState::isRich(std::string_view id) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.rich;
}
Result detail::UiState::richWritable(std::string_view id, Field*& field) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    if (!isRich(id)) return {Status::notSupported, u"Not a rich text field"};
    return writable(id, field);
}
Result detail::UiState::commitStyle(std::string_view id, Field& field, std::vector<StyleSpan> spans) {
    OperationScope operation(this);
    if (spans == field.spans) return {};
    auto& session = field.session;
    if (field.connected && !field.submitting && sourceMatches(field)) {
        field.origin = field.sourceValue;
        field.originSpans = field.sourceSpans;
    }
    field.past.push_back({session.text, session.selection, field.spans});
    field.future.clear();
    field.spans = std::move(spans);
    ++session.revision;
    return finishEdit(id, field);
}
Result detail::UiState::setRichValue(std::string_view id, RichText value) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = richWritable(id, field); !result) return result;
    auto normal = normalizeRichText(std::move(value), find(id)->maxLength);
    if (!normal) return {normal.status, normal.message};
    field->session.select(0, static_cast<unsigned>(field->session.text.size()));
    return replaceRange(id, *field, 0, static_cast<unsigned>(field->session.text.size()), normal->text, &normal->spans);
}
Query<RichText> detail::UiState::richValue(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::notSupported, u"Not a rich text field", std::nullopt};
    return {Status::ok, {}, RichText{it->second.session.text, it->second.spans}};
}
Result detail::UiState::setStyle(std::string_view id, unsigned begin, unsigned end, const StyleChange& change) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = richWritable(id, field); !result) return result;
    const auto& text = field->session.text;
    auto boundary = [&](unsigned pos) { return pos <= text.size() && !(pos > 0 && pos < text.size() && text[pos] >= 0xdc00 && text[pos] <= 0xdfff && text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff); };
    if (begin > end || !boundary(begin) || !boundary(end)) return {Status::invalidValue, u"No such text range"};
    return commitStyle(id, *field, detail::applyStyleChange(field->spans, begin, end, change));
}
Query<TextStyle> detail::UiState::typingStyle(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::notSupported, u"Not a rich text field", std::nullopt};
    const auto& session = it->second.session;
    return {Status::ok, {}, insertionStyle(it->second, session.begin(), session.end())};
}
Result detail::UiState::setTypingStyle(std::string_view id, TextStyle style) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = richWritable(id, field); !result) return result;
    field->typing = std::move(style);
    field->typingMoves = field->session.selectionChanges;
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::applyStyle(std::string_view id, const StyleChange& change) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = richWritable(id, field); !result) return result;
    const auto& session = field->session;
    if (session.begin() != session.end()) return setStyle(id, session.begin(), session.end(), change);
    return setTypingStyle(id, detail::changed(insertionStyle(*field, session.begin(), session.end()), change));
}
Result detail::UiState::toggleStyle(std::string_view id, StyleAttribute attribute) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = richWritable(id, field); !result) return result;
    const auto& session = field->session;
    auto flip = [&](bool on) { StyleChange change; (attribute == StyleAttribute::bold ? change.bold : attribute == StyleAttribute::italic ? change.italic : change.underline) = on; return change; };
    if (session.begin() != session.end())
        return setStyle(id, session.begin(), session.end(), flip(!detail::allHave(field->spans, session.text, session.begin(), session.end(), attribute)));
    auto current = insertionStyle(*field, session.begin(), session.end());
    bool on = attribute == StyleAttribute::bold ? current.bold : attribute == StyleAttribute::italic ? current.italic : current.underline;
    return setTypingStyle(id, detail::changed(current, flip(!on)));
}
Result detail::UiState::setColors(std::string_view id, std::vector<NamedColor> colors) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::notSupported, u"Not a rich text field"};
    std::set<std::string> names;
    for (const auto& color : colors)
        if (color.name.empty() || !names.insert(color.name).second) return {Status::invalidValue, u"Color names must be non-empty and distinct"};
    it->second.colors = std::move(colors);
    if (invalidate_) invalidate_();
    return {};
}
Query<std::vector<NamedColor>> detail::UiState::colors(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {Status::notSupported, u"Not a rich text field", std::nullopt};
    return {Status::ok, {}, it->second.colors};
}
std::optional<Color> detail::UiState::namedColor(std::string_view id, std::string_view name) const {
    auto it = edits_.find(id);
    if (it == edits_.end() || name.empty()) return std::nullopt;
    for (const auto& color : it->second.colors)
        if (color.name == name) return presentation_.variant == ThemeVariant::dark() ? color.dark : color.light;
    return std::nullopt;
}
const std::vector<StyleSpan>* detail::UiState::styleSpans(std::string_view id) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.rich ? &it->second.spans : nullptr;
}
std::vector<StyleSpan> detail::UiState::visibleStyleSpans(std::string_view id) const {
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.rich) return {};
    const auto& field = it->second;
    const auto& session = field.session;
    if (!session.composing) return field.spans;
    return detail::compositionSpans(field.spans, session.begin(), session.end(), static_cast<unsigned>(session.preedit.size()),
                                     insertionStyle(field, session.begin(), session.end()));
}
Result detail::UiState::travel(std::string_view id, bool forward) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    auto& from = forward ? field->future : field->past;
    auto& to = forward ? field->past : field->future;
    if (from.empty()) return {Status::emptyHistory, forward ? u"沒有可重做的編輯" : u"沒有可復原的編輯"};
    auto& session = field->session;
    if (field->connected && !field->submitting && sourceMatches(*field)) {
        field->origin = field->sourceValue;
        field->originSpans = field->sourceSpans;
    }
    to.push_back({session.text, session.selection, field->spans});
    auto snapshot = std::move(from.back());
    from.pop_back();
    if (session.text != snapshot.text || field->spans != snapshot.spans) ++session.revision;
    session.text = std::move(snapshot.text);
    field->spans = std::move(snapshot.spans);
    field->typing.reset();
    session.select(snapshot.selection.anchor, snapshot.selection.caret);
    return finishEdit(id, *field);
}
Result detail::UiState::submitField(std::string_view id, Field& field,
        const std::function<Result(const Change<TextFieldHandle>&)>& write, bool command) {
    auto change = Change<TextFieldHandle>{TextFieldHandle(this, componentLifetimes_.at(Id(id)), Id(id)), field.session.text, field.origin};
    field.submitting = true;
    if (command) invoking_ = true;
    Result result;
    try { result = write(change); }
    catch (...) {
        field.submitting = false;
        if (command) invoking_ = false;
        stopped_ = true;
        return {Status::failed, u"Modification failed"};
    }
    if (command) invoking_ = false;
    field.submitting = false;
    if (result) { field.origin = change.value; field.originSpans = field.spans; setError(id, u""); }
    else { setError(id, result.message); if (command) focus(id); }
    auto notification = notify();
    return notification ? result : notification;
}
Result detail::UiState::finishEdit(std::string_view id, Field& field) {
    OperationScope operation(this);
    if (field.immediate && !field.submitting) return submitField(id, field, field.immediate, false);
    return notify();
}
Result detail::UiState::undo(std::string_view id) {
    OperationScope operation(this); return travel(id, false); }
Result detail::UiState::redo(std::string_view id) {
    OperationScope operation(this); return travel(id, true); }
Result detail::UiState::notify() {
    OperationScope operation(this);
    dispatching_ = true;
    try { if (changed_) changed_(); }
    catch (...) { stopped_ = true; dispatching_ = false; return {Status::failed, u"資料更新失敗，介面已停止"}; }
    dispatching_ = false;
    return {};
}
Result detail::UiState::textInput(std::u16string_view text) {
    OperationScope operation(this);
    if (menu_) return menuText(text);  // quick-key letters
    auto committed = withoutControls(text);
    if (committed.empty()) return {};
    Field* field = nullptr;
    if (auto result = writable(focus_, field); !result) return result;
    return replaceSelection(focus_, *field, committed);
}
std::u16string pasteAsSingleLine(std::u16string_view text) {
    std::u16string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        result += text[i] == u'\r' || text[i] == u'\n' ? u' ' : text[i];
    }
    return result;
}
std::u16string pasteAsMultiLine(std::u16string_view text) {
    std::u16string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        result += text[i] == u'\r' ? u'\n' : text[i];
    }
    return result;
}
Result detail::UiState::paste(std::string_view id, std::u16string_view text) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    return replaceSelection(id, *field, find(id)->multiline ? pasteAsMultiLine(text) : pasteAsSingleLine(text));
}
Result detail::UiState::copy(std::string_view id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalidValue, u"元件不是文字欄位"};
    const auto& session = it->second.session;
    if (session.begin() == session.end()) return {};
    auto selected = std::u16string(session.text, session.begin(), session.end() - session.begin());
    if (find(id)->multiline) {  // the clipboard carries CRLF, as other Windows text does
        std::u16string crlf;
        for (auto ch : selected) { if (ch == u'\n') crlf += u'\r'; crlf += ch; }
        selected = std::move(crlf);
    }
    // A rich field also hands over the styles of the selection, beside the plain text.
    const bool written = it->second.rich && clipboard_
        ? clipboard_->writeRich(selected, detail::serializeRichPayload(RichText{std::u16string(session.text, session.begin(), session.end() - session.begin()),
                                                                                    detail::sliceSpans(it->second.spans, session.begin(), session.end())}))
        : clipboard_ && clipboard_->write(selected);
    if (!written) return {Status::failed, u"無法寫入剪貼簿"};
    return {};
}
// A paste from the clipboard: a rich field takes the styles of a payload that belongs to the plain text it
// came with; anything else (another program's text, a payload left from another copy, a plain field) is plain text.
Result detail::UiState::pasteClipboard(std::string_view id) {
    OperationScope operation(this);
    if (!clipboard_) return {Status::notSupported, u"剪貼簿不可用"};
    auto content = clipboard_->read();
    if (!content) return {content.status, content.message};
    if (isRich(id)) {
        auto payload = clipboard_->readRich();
        auto rich = payload ? detail::parseRichPayload(*payload) : std::nullopt;
        if (rich && rich->text == pasteAsMultiLine(*content)) {
            Field* field = nullptr;
            if (auto result = writable(id, field); !result) return result;
            return replaceRange(id, *field, field->session.begin(), field->session.end(), rich->text, &rich->spans);
        }
    }
    return paste(id, *content);
}
Result detail::UiState::cut(std::string_view id) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.begin() == field->session.end()) return {};
    if (auto result = copy(id); !result) return result;
    return replaceSelection(id, *field, u"");
}
float detail::UiState::textOffset(std::string_view id) const {
    auto it = edits_.find(id);
    return it == edits_.end() ? 0.f : it->second.offset;
}
void detail::UiState::setTextOffset(std::string_view id, float offset) {
    OperationScope operation(this);
    if (auto it = edits_.find(id); it != edits_.end()) it->second.offset = std::max(0.f, offset);
}
unsigned detail::UiState::textAnchor(std::string_view id, int textSize) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.offsetTextSize && it->second.offsetTextSize != textSize
        ? it->second.offsetAnchor : 0;
}
void detail::UiState::setTextAnchor(std::string_view id, int textSize, unsigned anchor) {
    if (auto it = edits_.find(id); it != edits_.end()) { it->second.offsetTextSize = textSize; it->second.offsetAnchor = anchor; }
}
const TextFieldView* detail::UiState::fieldView(std::string_view id) const {
    auto it = edits_.find(id);
    return it != edits_.end() && it->second.multi.placed ? &it->second.multi.view : nullptr;
}
void detail::UiState::settleLines(std::string_view id, TextFieldView& view, int textSize, int width) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || view.lines.empty()) return;
    auto& f = it->second;
    auto& m = f.multi;
    const auto& session = f.session;
    auto text = session.visible();
    const auto count = static_cast<unsigned>(view.lines.size());
    const unsigned most = count > view.visible ? count - view.visible : 0;
    auto styles = visibleStyleSpans(id);
    const bool relaid = m.text != text || m.size != textSize || m.width != width || m.styles != styles || m.revision != layoutRevision_;
    unsigned first = m.first;
    // A new wrap (resize, text size, edit) keeps the same text on top: the anchor is a text offset.
    if (m.placed && relaid) { view.upstream = false; first = view.lineOf(std::min<unsigned>(m.anchor, static_cast<unsigned>(text.size()))); }
    first = std::min(first, most);
    const unsigned caret = session.visibleCaret();
    const bool upstream = session.caretUpstream && !session.composing;
    view.upstream = upstream;
    // A caret that moved since the last arrange scrolls itself into view; scrolling alone never does.
    if (caret != m.caret || upstream != m.upstream) {
        auto line = view.lineOf(caret);
        if (line < first) first = line;
        else if (line >= first + view.visible) first = line + 1 - view.visible;
    }
    if (m.pendingFirst) { first = *m.pendingFirst; m.pendingFirst.reset(); }
    first = std::min(first, most);
    view.first = first;
    m.view = view;
    m.first = first; m.anchor = view.lines[first].begin; m.caret = caret; m.upstream = upstream; m.placed = true;
    if (relaid) { m.text = std::move(text); m.styles = std::move(styles); m.size = textSize; m.width = width; m.revision = layoutRevision_; }
    m.scroll.offset = static_cast<int>(first) * view.rowHeight;
}
bool detail::UiState::scrollLinesTo(std::string_view id, unsigned first) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end() || !it->second.multi.placed) return false;
    auto& m = it->second.multi;
    const auto count = static_cast<unsigned>(m.view.lines.size());
    first = std::min(first, count > m.view.visible ? count - m.view.visible : 0);
    if (first == m.first) return false;
    m.first = m.view.first = first;
    m.anchor = m.view.lines[first].begin;
    m.scroll.offset = static_cast<int>(first) * m.view.rowHeight;
    if (invalidate_) invalidate_();
    return true;
}
Result detail::UiState::selectPointer(std::string_view id, unsigned anchor, unsigned caret, bool upstream) {
    OperationScope operation(this);
    if (auto result = select(id, anchor, caret); !result) return result;
    auto& session = edits_.find(id)->second.session;
    session.caretUpstream = upstream && session.selection.caret == caret;
    return {};
}
Result detail::UiState::select(std::string_view id, unsigned anchor, unsigned caret) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalidValue, u"元件不是文字欄位"};
    if (it->second.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    const auto& text = it->second.session.text;
    auto boundary = [&](unsigned pos) {
        return pos <= text.size() && !(pos > 0 && pos < text.size() &&
            text[pos] >= 0xdc00 && text[pos] <= 0xdfff && text[pos - 1] >= 0xd800 && text[pos - 1] <= 0xdbff);
    };
    if (!boundary(anchor) || !boundary(caret)) return {Status::invalidValue, {}};
    it->second.session.select(anchor, caret);
    it->second.drag = {};
    return {};
}
// ---- Words, paragraphs and multi-click (#83) --------------------------------------------------
Result detail::UiState::setWordSegmentation(std::string_view id, WordSegmentation mode) {
    OperationScope operation(this);
    if (auto r = configurationAccess(id); !r) return r;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notSupported, u"Not a text field"};
    if (mode != WordSegmentation::dictionary && mode != WordSegmentation::perCharacter && mode != WordSegmentation::scriptRun)
        return {Status::invalidValue, u"Unknown word segmentation"};
    it->second.segmentation = mode;
    it->second.drag = {};
    return {};
}
Query<WordSegmentation> detail::UiState::wordSegmentation(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notSupported, u"Not a text field", std::nullopt};
    return {Status::ok, {}, it->second.segmentation};
}
Query<WordSegmentation> detail::UiState::wordSegmentationInUse(std::string_view id) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notSupported, u"Not a text field", std::nullopt};
    return {Status::ok, {}, segmentationInUse(it->second.segmentation, dictionary())};
}
Query<TextSpan> detail::UiState::wordAt(std::string_view id, unsigned offset) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notSupported, u"Not a text field", std::nullopt};
    const auto& text = it->second.session.text;
    if (offset > text.size()) return {Status::invalidValue, u"Offset is past the end of the text", std::nullopt};
    return {Status::ok, {}, wordSpan(text, wordsOf(it->second), offset)};
}
Query<TextSpan> detail::UiState::paragraphAt(std::string_view id, unsigned offset) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notSupported, u"Not a text field", std::nullopt};
    const auto& text = it->second.session.text;
    if (offset > text.size()) return {Status::invalidValue, u"Offset is past the end of the text", std::nullopt};
    return {Status::ok, {}, paragraphSpan(text, offset)};
}
Query<unsigned> detail::UiState::wordStop(std::string_view id, unsigned offset, bool forward) const {
    if (auto r = configurationAccess(id, true); !r) return {r.status, r.message, std::nullopt};
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::notSupported, u"Not a text field", std::nullopt};
    const auto& text = it->second.session.text;
    if (offset > text.size()) return {Status::invalidValue, u"Offset is past the end of the text", std::nullopt};
    return {Status::ok, {}, nextWordStop(text, wordsOf(it->second), offset, forward)};
}
std::vector<unsigned> detail::UiState::wordUnitBoundaries(std::string_view id) const {
    auto it = edits_.find(id);
    if (it == edits_.end()) return {};
    return wordUnitBounds(it->second.session.text, wordsOf(it->second));
}
std::vector<unsigned> detail::UiState::paragraphBoundaries(std::string_view id) const {
    auto it = edits_.find(id);
    return it == edits_.end() ? std::vector<unsigned>{} : paragraphBounds(it->second.session.text);
}
Result detail::UiState::selectUnit(std::string_view id, unsigned offset, Field::UnitDrag::Unit unit, bool arm) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalidValue, u"元件不是文字欄位"};
    auto& field = it->second;
    if (field.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    const auto& text = field.session.text;
    if (offset > text.size()) return {Status::invalidValue, u"Offset is past the end of the text"};
    const bool word = unit == Field::UnitDrag::Unit::word;
    const TextSpan span = word ? wordSpan(text, wordsOf(field), offset) : paragraphSpan(text, offset);
    field.session.select(span.begin, span.end);
    field.drag = {};
    if (arm) field.drag = {unit, span, field.session.selection};
    return {};
}
Result detail::UiState::selectWord(std::string_view id, unsigned offset) { return selectUnit(id, offset, Field::UnitDrag::Unit::word, false); }
Result detail::UiState::selectParagraph(std::string_view id, unsigned offset) { return selectUnit(id, offset, Field::UnitDrag::Unit::paragraph, false); }
Result detail::UiState::doubleClick(std::string_view id, unsigned offset) { return selectUnit(id, offset, Field::UnitDrag::Unit::word, true); }
Result detail::UiState::tripleClick(std::string_view id, unsigned offset) { return selectUnit(id, offset, Field::UnitDrag::Unit::paragraph, true); }
unsigned detail::UiState::countClick(const Id& id, int x, int y) {
    if (!clickClock_) return 1;
    const std::uint64_t now = clickClock_();
    const bool continues = clickRun_.count > 0 && clickRun_.count < 3 && clickRun_.id == id && now >= clickRun_.time &&
        now - clickRun_.time <= clickSettings_.intervalMs &&
        std::abs(x - clickRun_.x) <= clickSettings_.width / 2 && std::abs(y - clickRun_.y) <= clickSettings_.height / 2;
    if (continues) { ++clickRun_.count; clickRun_.time = now; }
    else clickRun_ = {id, x, y, now, 1};
    return clickRun_.count;
}
Result detail::UiState::textPointerDown(std::string_view id, TextHit caret, unsigned cell, int x, int y, bool extend) {
    OperationScope operation(this);
    const unsigned count = countClick(Id(id), x, y);
    if (count == 2) return doubleClick(id, cell);
    if (count == 3) return tripleClick(id, cell);
    auto it = edits_.find(id);
    const unsigned anchor = extend && it != edits_.end() ? it->second.session.selection.anchor : caret.offset;
    return selectPointer(id, anchor, caret.offset, caret.upstream);
}
Result detail::UiState::textPointerDrag(std::string_view id, TextHit caret, unsigned cell) {
    OperationScope operation(this);
    auto it = edits_.find(id);
    if (it == edits_.end()) return {Status::invalidValue, u"元件不是文字欄位"};
    auto& field = it->second;
    auto& drag = field.drag;
    const auto& selection = field.session.selection;
    const bool armed = drag.unit != Field::UnitDrag::Unit::character && drag.last.anchor == selection.anchor && drag.last.caret == selection.caret;
    if (!armed) return selectPointer(id, selection.anchor, caret.offset, caret.upstream);
    if (auto result = available(id); !result) return result;
    if (field.session.composing) return {Status::busy, u"組字中，請先完成或取消組字"};
    const auto& text = field.session.text;
    cell = std::min<unsigned>(cell, static_cast<unsigned>(text.size()));
    const TextSpan span = drag.unit == Field::UnitDrag::Unit::word ? wordSpan(text, wordsOf(field), cell) : paragraphSpan(text, cell);
    // The unit first selected always stays inside the selection; the pointer decides which side grows.
    const bool before = span.begin < drag.origin.begin;
    const auto kept = drag;
    field.session.select(before ? kept.origin.end : kept.origin.begin, before ? span.begin : std::max(span.end, kept.origin.end));
    field.drag = kept;
    field.drag.last = field.session.selection;
    return {};
}
void detail::UiState::endTextDrag() {
    OperationScope operation(this);
    for (auto& [id, field] : edits_) field.drag = {};
}
Result detail::UiState::invoke(std::string_view id) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    if (dispatching_ || invoking_) return {Status::busy, u"操作尚未結束"};
    for (const auto& [key, field] : edits_)
        if (field.session.composing) return {Status::busy, u"請先完成文字組字"};
    auto it = actions_.find(id);
    if (it == actions_.end()) return {Status::notSupported, u"元件沒有此動作"};
    invoking_ = true;
    executing_ = id;
    try { auto result = it->second(); invoking_ = false; executing_.clear(); return result; }
    catch (...) { stopped_ = true; invoking_ = false; executing_.clear(); return {Status::failed, u"操作失敗，介面已停止"}; }
}
void detail::UiState::bind(Id id, Action action) {
    OperationScope operation(this);
    if (!find(id) || !action) throw std::invalid_argument("Action requires a known ID and a callback");
    actions_.insert_or_assign(std::move(id), std::move(action));
}
Result detail::UiState::focusable(const Component& control) const {
    if (auto result = available(control.id); !result) return result;
    auto scroll = textScrolls_.find(control.id);
    if (!componentType(control.kind).canFocus() && (scroll == textScrolls_.end() || scroll->second.maximum <= 0)) return {Status::notSupported, u"標籤不能取得焦點"};
    return {};
}
void detail::UiState::setScrollableText(std::string id, ScrollState& state, int maximum, int lineHeight) {
    textScrolls_.insert_or_assign(std::move(id), TextScroll{&state, maximum, std::max(1, lineHeight)});
}
void detail::UiState::setVerticalScroll(const Id& id, int maximum, int columnWidth) {
    textScrolls_.insert_or_assign(id, TextScroll{&verticalScrolls_[id], maximum, std::max(1, columnWidth), true});
}
void detail::UiState::clearVerticalScroll(const Id& id) {
    if (auto it = textScrolls_.find(id); it != textScrolls_.end() && it->second.horizontal) textScrolls_.erase(it);
}
std::vector<Id> detail::UiState::focusOrder() const {
    std::vector<Id> order;
    for (const auto& control : controls_)
        if (focusable(control)) order.push_back(control.id);
    return order;
}
// The new focus is readable at once; owner notifications queue behind the one being delivered
// (ADR 0003: no direct recursion into subscribers).
Result detail::UiState::moveFocus(Id next) {
    OperationScope operation(this);
    if (next == focus_) return {};
    if (!composing_.empty() && beforeFocus_) beforeFocus_();
    if (!popupSource_.empty() && next != popupSource_) closePopup();
    if (menu_ && next != menu_->target) closeMenu();
    keyArmed_.clear(); pointerArmed_.clear(); endSliderDrag(false);
    if (!composing_.empty()) {
        // E01 default, matching the native IME on blur: leftover preedit is committed once.
        auto preedit = edits_.at(composing_).session.preedit;
        compositionCommit(preedit);
    }
    auto previous = std::exchange(focus_, std::move(next));
    refreshTooltips();
    if (auto it = lists_.find(focus_); it != lists_.end()) {
        auto& f = it->second; auto selected = f.source.read ? f.source.read() : std::optional<OptionValue>{};
        auto valid = [&](const auto& item) { return item.enabled && selected && item.value == *selected; };
        auto target = std::find_if(f.items.begin(), f.items.end(), valid);
        if (target == f.items.end()) target = std::find_if(f.items.begin(), f.items.end(), [](const auto& item) { return item.enabled; });
        f.cursor = target == f.items.end() ? std::optional<OptionValue>{} : std::optional<OptionValue>{target->value};
    }
    if (!focusMoved_ && blurCallbacks_.empty()) return {};
    pendingFocus_.emplace_back(std::move(previous), focus_);
    if (notifyingFocus_) return {};
    notifyingFocus_ = true;
    Result result;
    for (std::size_t i = 0; i < pendingFocus_.size(); ++i) {
        auto [from, to] = pendingFocus_[i];
        try {
            if (auto it = blurCallbacks_.find(from); it != blurCallbacks_.end() && !removalPending(from)) {
                auto callback = it->second;
                callback();
            }
            if (focusMoved_) focusMoved_(from, to);
        }
        catch (...) { stopped_ = true; result = {Status::failed, u"焦點通知失敗，介面已停止"}; break; }
    }
    pendingFocus_.clear();
    notifyingFocus_ = false;
    return result;
}
Result detail::UiState::compositionUpdate(std::u16string preedit, unsigned caret) {
    OperationScope operation(this);
    if (!composing_.empty()) if (auto result = available(composing_); !result) return result;
    if (composing_.empty()) {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        composing_ = focus_;
    }
    edits_.at(composing_).session.updateComposition(std::move(preedit), caret);
    return {};
}
Result detail::UiState::compositionCommit(std::u16string_view text) {
    OperationScope operation(this);
    auto target = composing_.empty() ? focus_ : composing_;
    if (auto result = available(target); !result) return result;
    compositionCancel();
    if (!edits_.contains(target)) return {Status::invalidValue, u"沒有接收組字的文字欄位"};
    auto committed = withoutControls(text);
    Field* field = nullptr;
    if (auto result = writable(target, field); !result) return result;
    if (committed.empty()) return {};
    return replaceSelection(target, *field, committed);
}
Result detail::UiState::compositionCancel() {
    OperationScope operation(this);
    if (composing_.empty()) return {};
    edits_.at(composing_).session.cancelComposition();
    composing_.clear();
    return {};
}
Result detail::UiState::focus(std::string_view id) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (auto result = available(id); !result) return result;
    if (auto result = focusable(*control); !result) return result;
    refreshTooltips();
    return moveFocus(control->id);
}
Result detail::UiState::addShortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope) {
    OperationScope operation(this);
    auto targetId = layoutId(target);
    auto scopeId = layoutId(scope);
    if (!targetId || !find(*targetId) || (!std::holds_alternative<std::monostate>(scope) && (!scopeId || !find(*scopeId))) ||
        chord.key == Key::tab || chord.key == Key::escape || (chord.key == Key::other && !chord.letter))
        return {Status::invalidValue, u"Invalid shortcut"};
    if (auto r = configurationAccess(*targetId); !r) return r;
    if (chord.letter && !chord.ctrl) return {Status::invalidValue, u"Letter shortcuts require Ctrl"};
    Id scopeName = scopeId.value_or(Id{});
    for (const auto& entry : shortcuts_)
        if (entry.scope == scopeName && entry.chord.key == chord.key && entry.chord.letter == chord.letter && entry.chord.ctrl == chord.ctrl && entry.chord.shift == chord.shift)
            return {Status::invalidValue, u"Duplicate shortcut"};
    shortcuts_.push_back({chord, *targetId, std::move(scopeName)});
    return {};
}
Result detail::UiState::keyDown(KeyPress key) {
    OperationScope operation(this);
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (rendererFailed_) return {Status::rendererFailed, u"Renderer failed"};
    // A key that uses the keyboard shows the focus indicator. A modifier on its own does not (code review 2026-11-12,
    // item 2): Ctrl chords, a lone Alt, and the F10 that only closes an open menu all leave the state alone.
    const bool modifierOnly = key.ctrl || key.key == Key::alt || (key.key == Key::f10 && menu_);
    if (!modifierOnly && !keyboardCues_) { keyboardCues_ = true; focusCuesChanged(); }
    if (!composing_.empty()) return {Status::busy, u"Composition in progress"};
    if (menu_) return menuKeyDown(key);  // an open context menu owns the keyboard
    if (key.key == Key::escape && !key.repeat) {
        for (auto& [id, tip] : tooltips_) {
            if (tip.visible) {
                tip.visible = false; tip.dismissed = true;
                if (invalidate_) invalidate_();
                return {};
            }
        }
    }
    if (key.key == Key::escape && !popupSource_.empty()) { if (!key.repeat) closePopup(); return {}; }
    if (modal_ && !activeDialog_.empty() && key.key == Key::escape) {
        if (key.repeat) return {};
        return settleDialog(activeDialog_, false);
    }
    if (key.key == Key::tab) {
        auto order = focusOrder();
        if (order.empty()) return {Status::notFound, u"沒有可聚焦的元件"};
        auto it = std::find(order.begin(), order.end(), focus_);
        auto n = static_cast<long>(order.size());
        long index = it == order.end() ? (key.shift ? 0 : -1) : static_cast<long>(it - order.begin());
        index = (index + (key.shift ? -1 : 1) + n) % n;
        return moveFocus(order[static_cast<std::size_t>(index)]);
    }
    if ((key.key == Key::menu || (key.key == Key::f10 && key.shift)) && !key.ctrl) {
        if (key.repeat || focus_.empty()) return {};
        return openMenu(focus_, std::nullopt, std::nullopt);
    }
    const auto* control = find(focus_);
    if (control) {
        if (auto result = available(focus_); !result) return result;
        if (auto it = textScrolls_.find(focus_); it != textScrolls_.end() && it->second.maximum > 0 && !key.ctrl && !key.shift) {
            auto& scroll = it->second;
            int next = scroll.state->offset;
            bool handled = true;
            // Sideways text: the offset counts from the start (right edge), so Left reads on, Right reads back.
            if (scroll.horizontal && key.key == Key::left) next += scroll.lineHeight;
            else if (scroll.horizontal && key.key == Key::right) next -= scroll.lineHeight;
            else if (scroll.horizontal && (key.key == Key::up || key.key == Key::down)) handled = false;
            else if (key.key == Key::up) next -= scroll.lineHeight;
            else if (key.key == Key::down) next += scroll.lineHeight;
            else if (key.key == Key::home) next = 0;
            else if (key.key == Key::end) next = scroll.maximum;
            else handled = false;
            if (handled) {
                scroll.state->offset = std::clamp(next, 0, scroll.maximum);
                scrolled();
                return {};
            }
        }
        const auto& type = componentType(control->kind);
        if (type.handlesKey(*this, *control, key)) return type.keyDown(*this, key);
    }
    for (int scoped = 1; scoped >= 0; --scoped) for (const auto& entry : shortcuts_) {
        if ((scoped ? entry.scope == focus_ && !entry.scope.empty() : entry.scope.empty()) && entry.chord.key == key.key && entry.chord.letter == key.letter && entry.chord.ctrl == key.ctrl && entry.chord.shift == key.shift) {
            if (key.repeat) return {};
            if (modal_) {
                const auto* target = find(entry.target);
                if (target && target->page != "dialog") return {Status::blockedByModal, u"Dialog is open"};
            }
            return invoke(entry.target);
        }
    }
    return {};
}
namespace {
// Code-point stepping is the S1 support boundary: no grapheme clusters, no RTL.
unsigned step(std::u16string_view text, unsigned pos, int direction) {
    auto low = [&](unsigned i) { return i < text.size() && text[i] >= 0xdc00 && text[i] <= 0xdfff; };
    auto high = [&](unsigned i) { return i < text.size() && text[i] >= 0xd800 && text[i] <= 0xdbff; };
    if (direction < 0) {
        if (pos == 0) return 0;
        --pos;
        return pos && low(pos) && high(pos - 1) ? pos - 1 : pos;
    }
    if (pos >= text.size()) return static_cast<unsigned>(text.size());
    return high(pos) && low(pos + 1) ? pos + 2 : pos + 1;
}
}
Result detail::UiState::editKey(KeyPress key) {
    OperationScope operation(this);
    auto& session = edits_.at(focus_).session;
    if (session.composing) return {Status::busy, u"組字中，按鍵交由輸入法處理"};
    const auto size = static_cast<unsigned>(session.text.size());
    auto move = [&](unsigned pos) { session.select(key.shift ? session.selection.anchor : pos, pos); return Result{}; };
    const bool multi = find(focus_)->multiline;
    if (multi && key.ctrl && !key.letter && (key.key == Key::home || key.key == Key::end)) return move(key.key == Key::home ? 0 : size);
    // Ctrl+arrows and Ctrl+Backspace/Delete go by words: the stops a double click's words define (#83).
    if (key.ctrl && !key.letter && (key.key == Key::left || key.key == Key::right || key.key == Key::backspace || key.key == Key::del)) {
        const bool forward = key.key == Key::right || key.key == Key::del;
        if (key.key == Key::left || key.key == Key::right)
            return move(nextWordStop(session.text, wordsOf(edits_.at(focus_)), session.selection.caret, forward));
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        if (session.begin() == session.end())
            session.select(session.selection.caret, nextWordStop(session.text, wordsOf(*field), session.selection.caret, forward));
        if (session.begin() == session.end()) return {};
        return replaceSelection(focus_, *field, u"");
    }
    if (key.ctrl) {
        if (key.letter == u'Z') return travel(focus_, key.shift);
        if (key.letter == u'Y') return travel(focus_, true);
        if (key.letter == u'A') { session.select(0, size); return {}; }
        if (key.letter == u'C') return copy(focus_);
        if (key.letter == u'X') return cut(focus_);
        if (isRich(focus_) && (key.letter == u'B' || key.letter == u'I' || key.letter == u'U'))
            return toggleStyle(focus_, key.letter == u'B' ? StyleAttribute::bold : key.letter == u'I' ? StyleAttribute::italic : StyleAttribute::underline);
        if (key.letter == u'V') return pasteClipboard(focus_);
        return {};
    }
    switch (key.key) {
    case Key::left:
    case Key::right: {
        int direction = key.key == Key::left ? -1 : 1;
        if (!key.shift && session.begin() != session.end()) return move(direction < 0 ? session.begin() : session.end());
        return move(step(session.text, session.selection.caret, direction));
    }
    case Key::home: return multi ? moveLineEdge(focus_, false, key.shift) : move(0);
    case Key::end: return multi ? moveLineEdge(focus_, true, key.shift) : move(size);
    case Key::up: case Key::down:
        return multi ? moveLines(focus_, key.key == Key::up ? -1 : 1, 1, key.shift) : Result{};
    case Key::pageUp: case Key::pageDown:
        return multi ? moveLines(focus_, key.key == Key::pageUp ? -1 : 1, find(focus_)->visibleLines, key.shift) : Result{};
    case Key::enter: {
        if (!find(focus_)->multiline) return {};
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        return replaceSelection(focus_, *field, u"\n");
    }
    case Key::backspace:
    case Key::del: {
        Field* field = nullptr;
        if (auto result = writable(focus_, field); !result) return result;
        if (session.begin() == session.end())
            session.select(session.selection.caret, step(session.text, session.selection.caret, key.key == Key::del ? 1 : -1));
        return deleteSelection(focus_);  // the Delete menu item runs the same path
    }
    default: return {};
    }
}
// The lines cursor keys move over: the last arrange's when they still describe the text, else a fresh
// arrange from the host, else the hard lines alone (no wrapping, one pixel per column).
TextFieldView detail::UiState::verticalView(const Id& id) {
    auto& field = edits_.find(id)->second;
    auto fresh = [&] {
        return field.multi.placed && field.multi.revision == layoutRevision_ && field.multi.text == field.session.visible() &&
               field.multi.styles == visibleStyleSpans(id);
    };
    if (!fresh() && relayout_) relayout_();
    TextFieldView view;
    if (fresh()) { view = field.multi.view; view.upstream = field.session.caretUpstream; return view; }
    const auto& text = field.session.text;
    for (std::size_t start = 0;;) {
        auto end = text.find(u'\n', start);
        TextFieldLine line;
        line.begin = static_cast<unsigned>(start);
        line.end = static_cast<unsigned>(end == std::u16string::npos ? text.size() : end);
        line.run.stops.push_back({0, 0.f});
        for (unsigned i = 1; i <= line.end - line.begin; ++i) line.run.stops.push_back({i, static_cast<float>(i)});
        line.run.width = static_cast<float>(line.end - line.begin);
        view.lines.push_back(std::move(line));
        if (end == std::u16string::npos) break;
        start = end + 1;
    }
    view.visible = find(id)->visibleLines;
    view.upstream = field.session.caretUpstream;
    return view;
}
Result detail::UiState::moveLineEdge(const Id& id, bool end, bool shift) {
    OperationScope operation(this);
    auto view = verticalView(id);
    auto& session = edits_.find(id)->second.session;
    const auto& line = view.lines[view.lineOf(session.selection.caret)];
    const unsigned target = end ? line.end : line.begin;
    session.select(shift ? session.selection.anchor : target, target);
    session.caretUpstream = end && line.soft;  // the caret stays on the line it was moved to the end of
    return {};
}
Result detail::UiState::moveLines(const Id& id, int direction, unsigned count, bool shift) {
    OperationScope operation(this);
    auto& field = edits_.find(id)->second;
    auto& session = field.session;
    auto view = verticalView(id);
    const unsigned from = view.lineOf(session.selection.caret);
    const bool real = field.multi.placed && field.multi.text == session.visible();
    // One run of Up/Down presses keeps the column it started at.
    if (field.multi.wantFrom != session.selection.caret || !real) field.multi.wantX = view.xInLine(from, session.selection.caret);
    const long target = std::clamp(static_cast<long>(from) + static_cast<long>(direction) * static_cast<long>(count), 0L, static_cast<long>(view.lines.size()) - 1);
    if (target == static_cast<long>(from)) return {};
    auto [offset, upstream] = view.hitInLine(static_cast<unsigned>(target), field.multi.wantX);
    session.select(shift ? session.selection.anchor : offset, offset);
    session.caretUpstream = upstream;
    field.multi.wantFrom = real ? offset : ~0u;
    if (count > 1 && real) {  // a page key moves the view as far as the caret moved
        const long moved = target - static_cast<long>(from);
        scrollLinesTo(id, static_cast<unsigned>(std::max(0L, static_cast<long>(field.multi.first) + moved)));
    }
    return {};
}
Result detail::UiState::keyUp(Key key) {
    OperationScope operation(this);
    if (key != Key::space || keyArmed_.empty() || keyArmed_ != focus_) return {};
    auto target = std::exchange(keyArmed_, {});
    return invoke(target);
}
Result detail::UiState::pointerDown(std::string_view id, std::optional<std::size_t> option) {
    OperationScope operation(this);
    pointerPressed();
    if (auto result = available(id); !result) return result;
    const auto* control = find(id);
    if (auto result = focusable(*control); !result) return result;
    if (!popupSource_.empty() && popupSource_ != id) closePopup();
    if (componentType(control->kind).ignoresPress(*this, *control, option)) return {};
    endSliderDrag(false);
    const Id target = control->id;
    if (auto result = moveFocus(target); !result) return result;
    // Focus notifications can remove, disable, hide, or add controls. Revalidate and
    // reacquire the target before dispatch instead of retaining a vector pointer.
    refreshTooltips();
    if (auto result = available(target); !result) return result;
    control = find(target);
    componentType(control->kind).pointerDown(*this, *control, option);
    return {};
}
Result detail::UiState::pointerUp(std::string_view id, std::optional<std::size_t> option) {
    OperationScope operation(this);
    if (auto result = available(id); !result) return result;
    auto armed = std::exchange(pointerArmed_, {}); endSliderDrag(true);
    auto pressedOption = std::exchange(pointerOption_, std::nullopt);
    auto pressedValue = std::exchange(pointerOptionValue_, std::nullopt);
    if (armed.empty() || armed != id) return {};
    return componentType(find(armed)->kind).pointerUp(*this, {armed, pressedOption, option, pressedValue});
}
void detail::UiState::cancelInput() {
    OperationScope operation(this); endTextDrag(); keyArmed_.clear(); pointerArmed_.clear(); pointerOption_.reset(); pointerOptionValue_.reset(); endSliderDrag(false); }
Result detail::UiState::setError(std::string_view id, std::u16string message) {
    OperationScope operation(this);
    const auto* control = find(id);
    if (!control) return {Status::notFound, u"Component no longer exists"};
    if (!componentType(control->kind).acceptsError()) return {Status::notSupported, u"標籤不能帶有錯誤"};
    if (message.empty()) { if (auto it = errors_.find(id); it != errors_.end()) errors_.erase(it); }
    else errors_.insert_or_assign(control->id, std::move(message));
    return {};
}
void detail::UiState::enable(std::string_view id, bool enabled) {
    OperationScope operation(this);
    auto* control = const_cast<Component*>(find(id));
    if (!control) return;
    control->enabled = enabled;
    if (!enabled && menu_ && menu_->target == id) closeMenu();
    if (!enabled && popupSource_ == id) closePopup();
    if (!enabled && focus_ == control->id) moveFocus({});
    refreshTooltips();
}
void detail::UiState::setReadOnly(std::string_view id, bool readOnly) {
    OperationScope operation(this);
    if (auto* control = const_cast<Component*>(find(id))) control->readOnly = readOnly;
}
void detail::UiState::check(std::string_view id, bool checked) {
    OperationScope operation(this);
    if (auto* control = const_cast<Component*>(find(id))) control->checked = checked;
}
void detail::UiState::replaceText(std::string_view id, std::u16string value) {
    replaceRich(id, RichText{std::move(value), {}});
}
void detail::UiState::replaceRich(std::string_view id, RichText rich) {
    OperationScope operation(this);
    auto* control = const_cast<Component*>(find(id));
    auto it = edits_.find(id);
    if (!control || it == edits_.end() || !validValue(*control, rich.text)) throw std::invalid_argument("Invalid owner refresh");
    if (composing_ == control->id) compositionCancel();
    auto& field = it->second;
    field.session.cancelComposition();
    if (field.session.text != rich.text || field.spans != rich.spans) ++field.session.revision;
    field.session.text = std::move(rich.text);
    field.spans = field.rich ? std::move(rich.spans) : std::vector<StyleSpan>{};
    field.typing.reset();
    field.session.select(field.session.selection.anchor, field.session.selection.caret);
    field.past.clear(); field.future.clear(); field.offset = 0; field.offsetTextSize = 0; field.offsetAnchor = 0;
    field.multi.first = field.multi.anchor = 0; field.multi.caret = ~0u; field.multi.pendingFirst.reset();  // show the caret afresh
}
Ui::Ui() : state_(std::make_unique<detail::UiState>(std::vector<detail::Component>{})), semantics_(new SemanticTree(*state_)) {}
SemanticTree& Ui::semantics() { return *semantics_; }
Ui::~Ui() = default;
// Controls supplied as data after construction: registered exactly as the constructor registers them.
void detail::UiState::seedTestControls(std::vector<Component> controls) {
    OperationScope operation(this);
    for (auto& control : controls) {
        const auto& type = detail::componentType(control.kind);
        if (control.id.empty() || find(control.id)) throw std::invalid_argument("Control IDs must be nonempty and unique");
        componentLifetimes_.emplace(control.id, std::make_shared<int>(0));
        controls_.push_back(std::move(control));
        type.adopt(*this, controls_.back());
    }
}
bool detail::UiState::registerable(std::string_view id) {
    const auto* existing = find(id);
    auto overlay = overlayIds_.find(id);
    if (!id.empty() && !existing && overlay == overlayIds_.end()) return true;
#ifndef NDEBUG
    const auto* registered = existing ? findComponentType(existing->kind) : nullptr;
    std::string type = existing ? std::string(registered ? registered->name() : "custom") : overlay != overlayIds_.end() ? overlay->second : "none";
    std::string message = "Invalid component ID '" + std::string(id) + "'; existing component type: " + type;
    diagnostics_.push_back(message);
    outputDiagnostic(message);
#endif
    return false;
}
Result detail::UiState::creationAccess() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread"};
    if (stopped_) return {Status::failed, u"UI is stopped"};
    if (rendererFailed_) return {Status::rendererFailed, u"Renderer failed"};
    return {};
}
const std::vector<std::string>& Ui::diagnostics() const { return state_->diagnostics_; }
// Every Ui::add_x: one component of `kind`, the same checks in the same order, then `create` (the matching
// UiState::add_x). `extra` is a check of the type's own parameters (the slider's range), made after the shared ones.
template <class Handle, class Create>
static Query<Handle> addComponent(detail::UiState& state, ComponentKind kind, ComponentConfig config, Create create, Result extra = {}) {
    detail::Component component(kind, std::move(config));
    if (auto access = state.creationAccess(); !access) return {access.status, access.message, std::nullopt};
    if (!state.registerable(component.id)) return {Status::invalidValue, u"Invalid or duplicate ID", std::nullopt};
    if (!validTextFlow(component)) return {Status::invalidValue, u"Invalid text alignment or spacing", std::nullopt};
    if (auto support = detail::UiState::writingSupport(component); !support) return {support.status, support.message, std::nullopt};
    if (!extra) return {extra.status, extra.message, std::nullopt};
    try { return {Status::ok, {}, create(std::move(component))}; }
    catch (const std::invalid_argument&) { return {Status::invalidValue, u"Invalid component", std::nullopt}; }
}
Query<TextFieldHandle> Ui::addTextField(ComponentConfig config) {
    return addComponent<TextFieldHandle>(*state_, ComponentKind::textField, std::move(config), [&](auto c) { return state_->addTextField(std::move(c)); });
}
Query<ButtonHandle> Ui::addButton(ComponentConfig config) {
    return addComponent<ButtonHandle>(*state_, ComponentKind::button, std::move(config), [&](auto c) { return state_->addButton(std::move(c)); });
}
Query<LabelHandle> Ui::addLabel(ComponentConfig config) {
    return addComponent<LabelHandle>(*state_, ComponentKind::label, std::move(config), [&](auto c) { return state_->addLabel(std::move(c)); });
}
Query<CheckboxHandle> Ui::addCheckbox(ComponentConfig config) {
    return addComponent<CheckboxHandle>(*state_, ComponentKind::checkbox, std::move(config), [&](auto c) { return state_->addCheckbox(std::move(c)); });
}
Query<RadioGroupHandle> Ui::addRadioGroup(ComponentConfig config, std::vector<OptionItem> items) {
    return addComponent<RadioGroupHandle>(*state_, ComponentKind::radioGroup, std::move(config),
        [&](auto c) { return state_->addRadioGroup(std::move(c), std::move(items)); });
}
Query<SliderHandle> Ui::addSlider(ComponentConfig config, SliderRange range) {
    Result validRange = range.minimum >= range.maximum || range.step <= 0 ? Result{Status::invalidValue, u"Invalid slider range"} : Result{};
    return addComponent<SliderHandle>(*state_, ComponentKind::slider, std::move(config),
        [&](auto c) { return state_->addSlider(std::move(c), range); }, std::move(validRange));
}
Query<ProgressHandle> Ui::addProgress(ComponentConfig config) {
    return addComponent<ProgressHandle>(*state_, ComponentKind::progress, std::move(config), [&](auto c) { return state_->addProgress(std::move(c)); });
}
Query<TabsHandle> Ui::addTabs(ComponentConfig config, std::vector<TabPage> pages) {
    return addComponent<TabsHandle>(*state_, ComponentKind::tabs, std::move(config),
        [&](auto c) { return state_->addTabs(std::move(c), std::move(pages)); });
}
Query<NavigationListHandle> Ui::addNavigationList(ComponentConfig config, std::vector<NavigationPage> pages, OptionValue initial) {
    return addComponent<NavigationListHandle>(*state_, ComponentKind::navigationList, std::move(config),
        [&](auto c) { return state_->addNavigationList(std::move(c), std::move(pages), std::move(initial)); });
}
Query<DropdownHandle> Ui::addDropdown(ComponentConfig config, std::vector<OptionItem> items) {
    return addComponent<DropdownHandle>(*state_, ComponentKind::dropdown, std::move(config),
        [&](auto c) { return state_->addDropdown(std::move(c), std::move(items)); });
}
Query<ListHandle> Ui::addList(ComponentConfig config, std::vector<OptionItem> items) {
    return addComponent<ListHandle>(*state_, ComponentKind::list, std::move(config),
        [&](auto c) { return state_->addList(std::move(c), std::move(items)); });
}
Query<TooltipHandle> Ui::addTooltip(std::string id, ComponentHandle source, std::u16string text, unsigned delayMs) {
    if (auto access=state_->creationAccess(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(id)) return {Status::invalidValue, u"Invalid or duplicate ID", std::nullopt};
    try {
        auto sourceId = state_->layoutId(source);
        auto handle = state_->addTooltip(std::move(source), std::move(text), delayMs);
        state_->tooltipIds_.emplace(*sourceId, id);
        state_->overlayIds_.emplace(std::move(id), "tooltip");
        return {Status::ok, {}, std::move(handle)};
    } catch (const std::invalid_argument&) { return {Status::invalidValue, u"Invalid tooltip", std::nullopt}; }
}
Query<DialogHandle> Ui::addDialog(std::string id, std::vector<ComponentHandle> members, ComponentHandle initial, ButtonHandle fallback, std::function<void(bool)> result, std::optional<LabelHandle> name) {
    if (auto access=state_->creationAccess(); !access) return {access.status,access.message,std::nullopt};
    if (!state_->registerable(id)) return {Status::invalidValue, u"Invalid or duplicate ID", std::nullopt};
    try {
        auto handle = state_->addDialog(id, std::move(members), initial, fallback, std::move(result), std::move(name));
        state_->overlayIds_.emplace(std::move(id), "dialog");
        return {Status::ok, {}, std::move(handle)};
    } catch (const std::invalid_argument&) { return {Status::invalidValue, u"Invalid dialog", std::nullopt}; }
}
Result Ui::addShortcut(Shortcut chord, ButtonHandle target, ComponentHandle scope) {
    return state_->addShortcut(chord, std::move(target), std::move(scope));
}
Result Ui::connectConfirmation(ButtonHandle button, std::vector<ConfirmField> fields,
        std::function<ConfirmationResult(const std::vector<ConfirmFieldChange>&)> submit) {
    return state_->connectConfirmation(std::move(button), std::move(fields), std::move(submit));
}
void Ui::onChange(std::function<void()> callback) { state_->onChange(std::move(callback)); }
Result Ui::setPresentation(const PresentationSettings& settings) { return state_->setPresentation(settings); }
Query<bool> Ui::animating() const { return state_->animating(); }
Query<std::optional<HoverTarget>> Ui::hover() const { return state_->hoverQuery(); }
Result Ui::simulateHover(const ComponentHandle& target, std::optional<OptionValue> item) {
    auto id = state_->layoutId(target);
    if (!id) return {Status::notFound, u"Component no longer exists"};
    return state_->simulateHover(*id, item);
}
Result Ui::clearSimulatedHover() { return state_->clearSimulatedHover(); }
const PresentationSettings& Ui::presentation() const { return state_->presentation(); }
Result Ui::setPaletteOverrides(std::vector<PaletteOverride> overrides) { return state_->setPaletteOverrides(std::move(overrides)); }
const std::vector<PaletteOverride>& Ui::paletteOverrides() const { return state_->paletteOverrides(); }
Query<Palette> Ui::effectivePalette(const Appearance& appearance, const ThemeVariant& variant) const {
    if (!appearance.supports(variant)) return {Status::invalidValue, u"Appearance does not support this theme variant", {}};
    return {Status::ok, {}, state_->effectivePalette(appearance, variant)};
}
void Ui::bindAppearance(const Appearance* appearance) { state_->bindAppearance(appearance); }

bool Ui::focusIndicatorVisible() const { return state_->focusIndicatorVisible(); }
detail::UiState& detail::access(Ui& ui) { return *ui.state_; }
const detail::UiState& detail::access(const Ui& ui) { return *ui.state_; }

}

namespace lcars {
void detail::UiState::semanticFrame(const Frame& frame) {
    OperationScope operation(this);
    semanticBounds_.clear(); semanticOptions_.clear(); semanticOrder_.clear(); semanticPositions_.clear();
    auto store = [&](const Placement& p) {
        auto rect = intersect(p.bounds, p.clip);
        // An open dropdown panel repeats its dropdown's id; only its option rectangles are kept.
        // Modal dialog members are popups too, but they are ordinary components with their own bounds.
        if (!p.dropdownPanel && rect.w > 0 && rect.h > 0) semanticBounds_[p.id] = rect;
        if (!p.dropdownPanel) { semanticOrder_.push_back(p.id); semanticPositions_[p.id] = p.bounds; }
        auto& options = semanticOptions_[p.id];
        if (p.dropdownPanel) {
            options.assign(dropdowns_.at(p.id).options.size(), {});
            for (std::size_t i=0; i<p.options.size(); ++i) options[p.optionStart+i] = intersect(p.options[i], p.clip);
        } else for (auto option : p.options) options.push_back(intersect(option, p.clip));
        for (const auto& control : controls_) {
            if (control.target != p.id || removalPending(control.id)) continue;
            const std::vector<Line>* lines = nullptr;
            if (control.labelKind == LabelKind::name) lines = &p.labelLines;
            else if (control.labelKind == LabelKind::description) lines = &p.descriptionLines;
            else if (control.labelKind == LabelKind::error) lines = &p.errorLines;
            if (!lines) continue;
            for (const auto& line : *lines) {
                auto r = intersect(line.bounds, p.clip);
                if (r.w <= 0 || r.h <= 0) continue;
                auto it = semanticBounds_.find(control.id);
                if (it == semanticBounds_.end()) semanticBounds_[control.id] = r;
                else {
                    auto old = it->second; int x = std::min(old.x, r.x), y = std::min(old.y, r.y);
                    it->second = {x, y, std::max(old.x + old.w, r.x + r.w) - x, std::max(old.y + old.h, r.y + r.h) - y};
                }
            }
        }
    };
    for (const auto& p : frame.items) store(p);
    for (const auto& p : frame.overlays) {
        if (p.tooltip) {
            auto id = tooltipIds_.find(p.id);
            if (id != tooltipIds_.end()) { auto r = intersect(p.bounds, p.clip); if (r.w > 0 && r.h > 0) semanticBounds_[id->second] = r; }
        } else if (!p.menu) store(p);
    }
    menuFrame(frame);
    if (frame.modal && !activeDialog_.empty()) semanticBounds_[activeDialog_] = frame.modalPanel;
#ifndef NDEBUG
    for (const auto& id : semanticOrder_) {
        auto c = find(id); auto s = describe(id);
        if (c && s && componentType(c->kind).needsName() && s->name.empty() && diagnosedNames_.insert(id).second)
        {
            auto message = id + " is focusable but has no name";
            diagnostics_.push_back(message);
            outputDiagnostic(message);
        }
    }
#endif
}
std::string detail::UiState::semanticPage(std::string_view id) const {
    auto c = find(id); if (!c) return {};
    if (c->page == "dialog") return "modal";
    auto it = pageMembers_.find(id);
    if (it == pageMembers_.end()) return "shared";
    return tabs_.at(it->second.first).pages[it->second.second].value.id;
}
Query<NodeSnapshot> SemanticTree::get(const NodeRef& ref) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrongThread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (auto menu = u.menuNode(ref)) return *menu;
    auto c = u.find(ref.id);
    if (!c) {
        auto overlay = u.overlayIds_.find(ref.id);
        if (overlay == u.overlayIds_.end() || ref.item) return {Status::notFound, {}, {}};
        NodeSnapshot n; n.ref = ref; n.role = overlay->second == "dialog" ? Role::dialog : Role::tooltip;
        auto [instance, inserted] = u.semanticInstances_.try_emplace(ref.id, 0);
        if (inserted) instance->second = ++u.nextSemanticInstance_;
        n.instance = instance->second;
        if (n.role == Role::dialog) {
            n.hidden = !u.modal_ || u.activeDialog_ != ref.id;
            const auto& label = u.dialogs_.at(ref.id).nameLabel;
            if (!label.empty() && u.find(label)) { n.name = u.text(label); n.labelledBy.push_back({label}); }
        }
        else for (const auto& [source, id] : u.tooltipIds_) if (id == ref.id) {
            auto tip = u.tooltips_.find(source);
            if (tip != u.tooltips_.end()) { n.name = tip->second.text; n.hidden = !tip->second.visible; }
        }
        auto bounds = u.semanticBounds_.find(ref.id);
        if (!n.hidden && bounds != u.semanticBounds_.end()) { n.bounds = bounds->second; n.visible = true; }
        if (n.role == Role::dialog && !n.hidden && !u.rendererFailed_) n.verbs.push_back(Verb::cancel);
        return {Status::ok, {}, n};
    }
    if (u.removalPending(ref.id)) return {Status::notFound, {}, {}};
    return u.semanticNode(ref, *u.describe(ref.id));
}
Query<NodeSnapshot> detail::UiState::semanticNode(const NodeRef& ref, const ComponentSnapshot& s) {
    auto& u = *this;
    const auto* c = find(ref.id);
    NodeSnapshot n;
    n.ref = ref; n.role = detail::componentType(c->kind).role();
    auto [instance, inserted] = u.semanticInstances_.try_emplace(ref.id, 0);
    if (inserted) instance->second = ++u.nextSemanticInstance_;
    n.instance = instance->second;
    n.hidden = u.available(ref.id, true).status == Status::hidden;
    n.blockedByModal = u.modal_ && c->page != "dialog";
    if (!ref.item) {
        // Component nodes copy the owning common part; options start from defaults below.
        static_cast<CommonState&>(n) = static_cast<const CommonState&>(s);
        if (!n.hidden) { auto bounds = u.semanticBounds_.find(ref.id); if (bounds != u.semanticBounds_.end()) n.bounds = bounds->second; }
        for (const auto& id : s.labels) { auto label = u.find(id); if (label && label->labelKind == LabelKind::name) n.labelledBy.push_back({id}); else if (label && (label->labelKind == LabelKind::description || label->labelKind == LabelKind::error)) n.describedBy.push_back({id}); }
        detail::componentType(c->kind).describeNode(u, *c, s, ref, n);
    } else {
        auto it = std::find_if(s.items.begin(), s.items.end(), [&](const auto& o) { return o.value == *ref.item; });
        if (it == s.items.end()) return {Status::notFound, {}, {}};
        if (auto itemRole = detail::componentType(c->kind).itemRole()) n.role = *itemRole;
        else return {Status::notFound, {}, {}};
        n.name = it->label; n.selected = it->selected; n.enabled = it->enabled; n.invalid = it->invalid;
        n.focusable = s.focusable && it->enabled && (!detail::componentType(c->kind).opensPopup() || s.expanded);
        n.focused = n.focusable && s.focused && detail::focusOption(s) == ref.item;
        n.bounds.reset(); auto pos = static_cast<size_t>(it - s.items.begin()); auto rects = u.semanticOptions_.find(ref.id);
        if (!n.hidden && (!detail::componentType(c->kind).opensPopup() || s.expanded) && rects != u.semanticOptions_.end() && pos < rects->second.size()) { auto r = rects->second[pos]; if (r.w > 0 && r.h > 0) n.bounds = r; }
        n.visible = n.bounds.has_value();
        n.verbs = it->enabled ? u.snapshotVerbs(*c, s, true) : std::vector<Verb>{};
    }
    return {Status::ok, {}, n};
}
Query<std::vector<NodeRef>> SemanticTree::children(const NodeRef& parent) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrongThread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (auto menu = u.menuChildren(parent)) return {Status::ok, {}, *menu};
    std::vector<NodeRef> result;
    if (parent.id.empty() && !parent.item) {
        for (const auto& c : u.controls_) if (c.page != "dialog" && !u.removalPending(c.id)) result.push_back({c.id});
        for (const auto& [id, type] : u.overlayIds_) result.push_back({id});
        std::stable_sort(result.begin(), result.end(), [&](const NodeRef& a, const NodeRef& b) {
            auto ai = u.semanticPositions_.find(a.id), bi = u.semanticPositions_.find(b.id);
            if (ai == u.semanticPositions_.end()) return false;
            if (bi == u.semanticPositions_.end()) return true;
            return ai->second.y != bi->second.y ? ai->second.y < bi->second.y : ai->second.x < bi->second.x;
        });
    } else {
        auto node = get(parent); if (!node) return {node.status, node.message, {}};
        if (!parent.item && node->role == Role::dialog) {
            const auto& members = u.dialogs_.at(parent.id).members;
            for (const auto& c : u.controls_)
                if (std::find(members.begin(), members.end(), c.id) != members.end() && !u.removalPending(c.id)) result.push_back({c.id});
        } else if (!parent.item) {
            auto s = u.describe(parent.id); if (s) for (const auto& o : s->items) result.push_back({parent.id, o.value});
            if (u.menu_ && u.menu_->target == parent.id) result.push_back({menuNodeId(parent.id)});
        }
    }
    return {Status::ok, {}, result};
}
Query<bool> SemanticTree::animating() const { return state_->animating(); }
Query<Palette> SemanticTree::effectivePalette() const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrongThread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (!u.boundAppearance()) return {Status::notFound, u"No appearance is bound to this Ui", {}};
    return {Status::ok, {}, u.effectivePalette(*u.boundAppearance())};
}
Query<std::optional<NodeRef>> SemanticTree::hover() const {
    auto target = state_->hoverQuery();
    if (!target) return {target.status, target.message, std::nullopt};
    if (!*target) return {Status::ok, {}, std::optional<NodeRef>{}};
    return {Status::ok, {}, std::optional<NodeRef>{NodeRef{(*target)->id, (*target)->item}}};
}
Result SemanticTree::clearSimulatedHover() { return state_->clearSimulatedHover(); }
Query<Color> SemanticTree::textColor(const NodeRef& ref, std::string_view name) const {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_) return {Status::wrongThread, {}, {}};
    if (u.stopped_) return {Status::failed, {}, {}};
    if (ref.item || !u.find(ref.id) || u.removalPending(ref.id)) return {Status::notFound, {}, {}};
    if (!u.isRich(ref.id)) return {Status::notSupported, u"Not a rich text field", {}};
    if (auto named = u.namedColor(ref.id, name)) return {Status::ok, {}, *named};
    if (!u.boundAppearance()) return {Status::notFound, u"No appearance is bound to this Ui", {}};
    return {Status::ok, {}, u.effectivePalette(*u.boundAppearance()).text};
}
Query<TextSpan> SemanticTree::textUnit(std::string_view id, TextUnitKind unit, unsigned offset) const {
    auto n = get({std::string(id)}); if (!n) return {n.status, n.message, std::nullopt};
    if (n->role != Role::textField) return {Status::notSupported, {}, std::nullopt};
    return unit == TextUnitKind::word ? state_->wordAt(id, offset) : state_->paragraphAt(id, offset);
}
Query<unsigned> SemanticTree::wordStop(std::string_view id, unsigned offset, bool forward) const {
    auto n = get({std::string(id)}); if (!n) return {n.status, n.message, std::nullopt};
    if (n->role != Role::textField) return {Status::notSupported, {}, std::nullopt};
    return state_->wordStop(id, offset, forward);
}
Query<std::u16string> SemanticTree::text(std::string_view id) const {
    auto n = get({std::string(id)}); if (!n) return {n.status, n.message, {}};
    if (n->role != Role::textField) return {Status::notSupported, {}, {}};
    return {Status::ok, {}, n->value};
}
std::vector<NodeSnapshot> detail::UiState::semanticSnapshot() {
    SemanticTree tree(*this); std::vector<NodeSnapshot> nodes;
    auto visit = [&](auto&& self, const NodeRef& parent, const ComponentSnapshot* owner) -> void {
        auto children = tree.children(parent); if (!children) return;
        for (const auto& ref : *children) {
            // Options share their owner's component description for this capture. Rebuilding
            // its entire items vector per option made event capture quadratic in allocations.
            auto node = ref.item && owner ? semanticNode(ref, *owner) : tree.get(ref);
            if (!node) continue;
            nodes.push_back(*node);
            if (ref.item) continue;  // option and menu-item nodes are leaves
            const auto description = find(ref.id) ? describe(ref.id) : std::nullopt;
            self(self, ref, description ? &*description : nullptr);
        }
    };
    visit(visit, {}, nullptr); return nodes;
}
void detail::UiState::semanticDiff() {
    const bool wasEmpty = semanticPending_.empty();
    auto after = semanticSnapshot();
    std::vector<SemanticEvent> changes;
    auto emit = [&](SemanticEventKind kind, NodeRef ref, std::string property = {}, std::optional<bool> hidden = {}) {
        auto same = [&](const SemanticEvent& e) { return e.kind == kind && e.ref == ref && e.property == property; };
        if (std::none_of(changes.begin(), changes.end(), same)) changes.push_back({kind, std::move(ref), std::move(property), hidden});
    };
    auto find = [](const auto& nodes, const NodeRef& ref) -> const NodeSnapshot* {
        auto i = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.ref == ref; });
        return i == nodes.end() ? nullptr : &*i;
    };
    auto deepestFocus = [](const auto& nodes) -> const NodeSnapshot* {
        const NodeSnapshot* component = nullptr;
        for (const auto& node : nodes) if (node.focused) {
            if (node.ref.item) return &node;
            component = &node;
        }
        return component;
    };
    const auto previousFocus = deepestFocus(semanticBefore_);
    const auto nextFocus = deepestFocus(after);
    if (nextFocus && (!previousFocus || previousFocus->ref != nextFocus->ref || previousFocus->instance != nextFocus->instance))
        emit(SemanticEventKind::focusChanged, nextFocus->ref);
    // A menu's structure changes belong to the component that owns it.
    auto ownerOf = [](const NodeRef& ref) -> NodeRef {
        if (auto target = menuTargetId(ref.id)) return NodeRef{*target};
        return ref.item ? NodeRef{ref.id} : NodeRef{};
    };
    for (const auto& old : semanticBefore_) if (!find(after, old.ref)) {
        if (old.role == Role::menu) emit(SemanticEventKind::menuClosed, old.ref);
        emit(SemanticEventKind::structureChanged, ownerOf(old.ref));
    }
    for (const auto& node : after) if (!node.ref.item) {
        std::vector<NodeRef> beforeItems, afterItems;
        for (const auto& old : semanticBefore_) if (old.ref.item && old.ref.id == node.ref.id) beforeItems.push_back(old.ref);
        for (const auto& current : after) if (current.ref.item && current.ref.id == node.ref.id) afterItems.push_back(current.ref);
        if (beforeItems != afterItems) emit(SemanticEventKind::structureChanged, node.ref);
    }
    for (const auto& node : after) {
        auto old = find(semanticBefore_, node.ref);
        if (!old) {
            if (node.role == Role::menu) emit(SemanticEventKind::menuOpened, node.ref);
            emit(SemanticEventKind::structureChanged, node.role == Role::menu ? ownerOf(node.ref) : node.ref.item ? NodeRef{node.ref.id} : NodeRef{});
            continue;
        }
        if (node.hidden != old->hidden) {
            if (node.role == Role::dialog || node.role == Role::tooltip)
                emit(SemanticEventKind::propertyChanged, node.ref, "hidden", node.hidden);
            emit(SemanticEventKind::structureChanged, {});
        }
        auto property = [&](bool changed, const char* name) { if (changed) emit(SemanticEventKind::propertyChanged, node.ref, name); };
        property(node.name != old->name, "name"); property(node.value != old->value, "value");
        property(node.enabled != old->enabled, "enabled"); property(node.readOnly != old->readOnly, "readOnly");
        property(node.checked != old->checked, "checked"); property(node.selected != old->selected, "selected");
        property(node.expanded != old->expanded, "expanded"); property(node.invalid != old->invalid, "invalid");
        property(node.blockedByModal != old->blockedByModal, "blockedByModal");
        property(node.numericValue != old->numericValue, "numericValue");
        property(node.textAlign != old->textAlign, "textAlign"); property(node.justifyMethod != old->justifyMethod, "justifyMethod");
        property(node.lineSpacing != old->lineSpacing, "lineSpacing");
        property(node.paragraphSpacing != old->paragraphSpacing, "paragraphSpacing");
        property(node.styles != old->styles, "styles"); property(node.typingStyle != old->typingStyle, "typingStyle");
        property(node.wordSegmentation != old->wordSegmentation, "wordSegmentation");
        if (node.scroll && old->scroll) {
            property(node.scroll->firstLine != old->scroll->firstLine, "scrollLine");
            property(node.scroll->visibleLines != old->scroll->visibleLines, "visibleLines");
        }
        property(node.writingMode != old->writingMode, "writingMode"); property(node.verticalDigits != old->verticalDigits, "verticalDigits");
        if (node.role == Role::textField && node.value != old->value) emit(SemanticEventKind::textChanged, node.ref);
        if (node.textSelection && old->textSelection && (node.textSelection->anchor != old->textSelection->anchor || node.textSelection->caret != old->textSelection->caret))
            emit(SemanticEventKind::textSelectionChanged, node.ref);
        if (node.bounds != old->bounds) emit(SemanticEventKind::layoutChanged, {});
    }
    semanticBefore_.clear();
    semanticPending_.insert(semanticPending_.end(), std::make_move_iterator(changes.begin()), std::make_move_iterator(changes.end()));
    if (wasEmpty && !semanticPending_.empty() && semanticSchedule_) semanticSchedule_();
}
Subscription SemanticTree::subscribe(std::function<void(const SemanticEvent&)> callback) {
    if (std::this_thread::get_id() != state_->owner_ || !callback) return Subscription{};
    auto id = ++state_->nextSemanticSubscriber_; state_->semanticSubscribers_.emplace(id, std::move(callback));
    std::weak_ptr<void> lifetime = state_->lifetime_; auto state = state_;
    return Subscription([state, lifetime, id] { if (!lifetime.expired()) state->semanticSubscribers_.erase(id); });
}
void SemanticTree::dispatchEvents() {
    auto& u = *state_;
    if (std::this_thread::get_id() != u.owner_ || u.semanticDispatching_) return;
    u.semanticDispatching_ = true;
    for (unsigned round = 0; round < 64 && !u.semanticPending_.empty(); ++round) {
        auto batch = std::exchange(u.semanticPending_, {});
        for (const auto& event : batch) {
            auto subscribers = u.semanticSubscribers_;
            for (const auto& [id, callback] : subscribers) if (u.semanticSubscribers_.contains(id)) {
                try { callback(event); } catch (...) { u.stopped_ = true; }
            }
        }
        u.commitRemovals();
    }
    u.semanticDispatching_ = false;
    if (!u.semanticPending_.empty() && u.semanticSchedule_) u.semanticSchedule_();
}
Result SemanticTree::perform(const NodeRef& ref, Verb verb, Argument argument) {
    auto node = get(ref);
    if (!node) return {node.status, node.message};
    if (state_->rendererFailed_) return {Status::rendererFailed, {}};
    if (node->role == Role::menu) return verb == Verb::cancel ? state_->closeMenu() : Result{Status::notSupported, {}};
    if (node->role == Role::menuSeparator) return {Status::notSupported, {}};
    if (node->role == Role::menuItem) {
        if (verb == Verb::invoke) return state_->menuExecute(ref.item->id);
        if (verb == Verb::focus || verb == Verb::hover) return state_->menuHighlight(ref.item->id);  // a menu row's hover is its highlight
        return {Status::notSupported, {}};
    }
    if (verb == Verb::hover) {
        if (node->role == Role::dialog || node->role == Role::tooltip) return {Status::notSupported, {}};
        return state_->simulateHover(ref.id, ref.item);
    }
    auto invalidArgument = [&]() -> Result {
        if (auto result = state_->available(ref.id); !result) return result;
        if ((verb == Verb::setValue || verb == Verb::paste) && node->readOnly) return {Status::readOnly, {}};
        if (state_->textComposing(ref.id)) return {Status::busy, {}};
        return {Status::invalidValue, {}};
    };
    if (verb == Verb::contextMenu) {
        std::optional<MenuPosition> at; std::optional<OptionValue> item = ref.item;
        if (auto position = std::get_if<MenuPosition>(&argument)) at = *position;
        else if (auto value = std::get_if<OptionValue>(&argument)) item = *value;
        else if (!std::holds_alternative<std::monostate>(argument)) return invalidArgument();
        return state_->openMenu(ref.id, at, std::move(item));
    }
    const auto* control = state_->find(ref.id);
    const detail::ComponentType* type = control ? &detail::componentType(control->kind) : nullptr;
    if (type && !ref.item && verb == Verb::select && std::holds_alternative<OptionValue>(argument) && type->itemRole()) return invalidArgument();
    if (type && !ref.item && verb == Verb::focus && (type->canFocus() || node->focusable)) return state_->focus(ref.id);
    if (node->role == Role::dialog && verb == Verb::cancel) return state_->settleDialog(ref.id, false);
    if (!type) return {Status::notSupported, {}};
    if (auto result = type->perform({*state_, ref, verb, argument, *node, invalidArgument})) return *result;
    if (verb == Verb::confirm && !ref.item && type->confirmPending(*state_, *control))
        return std::visit([](const auto& handle) -> Result {
            if constexpr (requires { handle.confirm(); }) return handle.confirm();
            else return {Status::notSupported, {}};
        }, state_->layoutHandle(ref.id));
    return {Status::notSupported, {}};
}
}
