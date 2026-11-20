#include "edit_form.hpp"
#include <algorithm>
#include <string>
#include <utility>

namespace editForm {
using namespace isoduo;
namespace {
const std::u16string requiredName = u"\u8acb\u586b\u5beb\u986f\u793a\u540d\u7a31";
const std::u16string initialName = u"測試使用者";
const std::u16string shortIntro = u"改名稱只改草稿；按「套用」且名稱有效，才更新上次套用結果並計數一次。資料只在記憶體。";
const std::u16string longIntro = shortIntro +
    u"\n這是長說明，用來確認長文字換行後不會覆蓋相鄰欄位，並在小視窗中造成溢出，讓你檢查捲動、焦點揭露與指標命中。"
    u"\n名稱欄按 Enter 不會套用；中文組字確認也不會套用。Ctrl+Z、Ctrl+Y 與 Ctrl+Shift+Z 只作用於名稱欄本身的編輯歷史。"
    u"\n「名稱唯讀」仍可選取與複製；「資料區停用」時名稱與套用都不能操作，但測試入口與重設仍可使用。";

// #84: Chinese, English words, digits and punctuation in one paragraph, long enough to wrap in a normal window.
const std::u16string justifySampleText =
    u"Isoduo 是面向一般桌面應用的 UI 框架，並以讓 AI 操作應用程式作為未來整合方向。這一段中英數混排的文字，用來比較靠左、置中、靠右、"
    u"左右對齊與分散對齊：Windows 11 的 2026 年版本、Direct2D 與 OpenGL 渲染，以及 12345 這類數字，都可以在不同視窗寬度下，觀察每一行的兩端是否切齊。";

bool blank(std::u16string_view text) {
    return std::all_of(text.begin(), text.end(), [](char16_t c) { return c == u' ' || c == u'\t' || c == u'　'; });
}
std::vector<OptionItem> regions() {
    return {{{"north"}, u"\u5317\u5340"}, {{"central"}, u"\u4e2d\u5340"}, {{"south"}, u"\u5357\u5340"}};
}
constexpr auto requiredRegion = u"\u8acb\u9078\u64c7\u5730\u5340";
}

Ids defaultIds() {
    Ids i;
    i.title = "form.title"; i.intro = "form.intro";
    i.normal = "fixture.normal"; i.readOnly = "fixture.read_only"; i.disabled = "fixture.disabled";
    i.error = "fixture.error"; i.longText = "fixture.long"; i.reset = "form.reset";
    i.nameLabel = "form.name.label"; i.name = "form.name"; i.nameHelp = "form.name.help"; i.nameError = "form.name.error";
    i.volume = "form.volume"; i.volumeLatest = "form.volume.latest"; i.notices = "form.notices"; i.mode = "form.mode"; i.offlineDisabled = "fixture.offline_disabled";
    i.region = "form.region"; i.regionEmpty = "fixture.region_empty";
    i.apply = "form.apply"; i.state = "result.state"; i.draft = "result.draft"; i.result = "result.applied"; i.count = "result.count"; i.latest = "form.latest";
    return i;
}

Form::Form(Ids ids, bool pages, std::function<FontResolution(std::u16string_view)> resolveFont,
           std::optional<std::vector<FontFamily>> fontFamilies)
    : ids_(std::move(ids)), applied_(initialName), resolveFont_(std::move(resolveFont)), pages_(pages), ui() {
    const auto& i = ids_;
    handles.title = ui.addLabel({.id = i.title, .value = u"編輯表單"}).value.value();
    handles.intro = ui.addLabel({.id = i.intro}).value.value();
    handles.normal = ui.addButton({.id = i.normal, .label = u"正常"}).value.value();
    handles.readOnly = ui.addButton({.id = i.readOnly, .label = u"名稱唯讀"}).value.value();
    handles.disabled = ui.addButton({.id = i.disabled, .label = u"資料區停用"}).value.value();
    handles.error = ui.addButton({.id = i.error, .label = u"名稱錯誤"}).value.value();
    handles.longText = ui.addButton({.id = i.longText, .label = u"長說明"}).value.value();
    handles.reset = ui.addButton({.id = i.reset, .label = u"重設"}).value.value();
    handles.nameLabel = ui.addLabel({.id = i.nameLabel, .value = u"顯示名稱", .target = i.name, .labelKind = LabelKind::name}).value.value();
    handles.name = ui.addTextField({.id = i.name, .value = initialName}).value.value();
    handles.nameHelp = ui.addLabel({.id = i.nameHelp, .value = u"單行文字，可輸入繁體中文或英文。", .target = i.name, .labelKind = LabelKind::description}).value.value();
    handles.nameError = ui.addLabel({.id = i.nameError, .target = i.name, .labelKind = LabelKind::error}).value.value();
    handles.notices = ui.addCheckbox({.id = i.notices, .label = u"\u555f\u7528\u63d0\u793a"}).value.value();
    handles.mode = ui.addRadioGroup({.id = i.mode, .label = u"\u6a21\u5f0f"},
        {{{"normal"}, u"\u4e00\u822c"}, {{"focus"}, u"\u5c08\u6ce8"}, {{"offline"}, u"\u96e2\u7dda"}}).value.value();
    handles.region = ui.addDropdown({.id = i.region, .label = u"\u5730\u5340"}, regions()).value.value();
    handles.volume = ui.addSlider({.id = i.volume, .label = u"\u97f3\u91cf"}, {0, 100, 5}).value.value();
    handles.volumeLatest = ui.addButton({.id = i.volumeLatest, .label = u"\u97f3\u91cf\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.noticesLatest = ui.addButton({.id = i.notices + ".latest", .label = u"\u63d0\u793a\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.modeLatest = ui.addButton({.id = i.mode + ".latest", .label = u"\u6a21\u5f0f\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.regionLatest = ui.addButton({.id = i.region + ".latest", .label = u"\u5730\u5340\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();
    handles.offlineDisabled = ui.addButton({.id = i.offlineDisabled, .label = u"\u505c\u7528\u96e2\u7dda"}).value.value();
    handles.regionEmpty = ui.addButton({.id = i.regionEmpty, .label = u"\u6e05\u7a7a\uff0f\u9084\u539f\u5730\u5340"}).value.value();
    handles.apply = ui.addButton({.id = i.apply, .label = u"套用"}).value.value();
    handles.state = ui.addLabel({.id = i.state}).value.value();
    handles.draft = ui.addLabel({.id = i.draft}).value.value();
    handles.result = ui.addLabel({.id = i.result}).value.value();
    handles.count = ui.addLabel({.id = i.count}).value.value();
    handles.latest = ui.addButton({.id = i.latest, .label = u"\u6539\u7528\u6700\u65b0\u503c", .enabled = false}).value.value();

    handles.name.connectSource({[this] { return applied_; }, [this](std::function<void()> changed) {
        sourceChanged_ = std::move(changed);
        return [this] { sourceChanged_ = {}; };
    }});
    handles.notices.connectSource({[this] { return noticesApplied_; }, [this](std::function<void()> changed) {
        noticesChanged_ = std::move(changed); return [this] { noticesChanged_ = {}; };
    }});
    handles.mode.connectSource({[this] { return modeApplied_; }, [this](std::function<void()> changed) {
        modeChanged_ = std::move(changed); return [this] { modeChanged_ = {}; };
    }});
    handles.region.connectSource({[this] { return regionApplied_; }, [this](std::function<void()> changed) {
        regionChanged_ = std::move(changed); return [this] { regionChanged_ = {}; };
    }});
    handles.volume.connectSource({[this] { return volumeApplied_; }, [this](std::function<void()> changed) {
        volumeChanged_ = std::move(changed); return [this] { volumeChanged_ = {}; };
    }});
    auto titleId = handles.title.describe()->id;
    auto titleText = handles.title.describe()->value;
    handles.title.connectSource([this, titleId, titleText] {
        return clipPaddingId_ == titleId ? clipPaddingText_ : titleText;
    });
    handles.intro.connectSource({[this] {
        return clipPaddingId_ == ids_.intro ? clipPaddingText_ : (longIntro_ ? longIntro : shortIntro);
    }, [this](std::function<void()> changed) {
        introChanged_ = std::move(changed);
        return [this] { introChanged_ = {}; };
    }});
    handles.state.connectSource([this] {
        std::u16string state = mode_ == Mode::readOnly ? u"\u540d\u7a31\u552f\u8b80" : mode_ == Mode::disabled ? u"\u8cc7\u6599\u5340\u505c\u7528" : u"\u6b63\u5e38";
        if (handles.name.describe()->invalid) state += u"\u30fb\u540d\u7a31\u6709\u932f\u8aa4";
        if (handles.name.sourceConflict().value.value_or(false)) state += u"\u30fb\u4f86\u6e90\u5df2\u66f4\u65b0";
        return u"\u72c0\u614b\uff1a" + state;
    });
    auto modeName = [](const std::optional<OptionValue>& value) -> std::u16string {
        if (!value) return u"\u672a\u9078\u53d6";
        if (value->id == "normal") return u"\u4e00\u822c";
        if (value->id == "focus") return u"\u5c08\u6ce8";
        if (value->id == "offline") return u"\u96e2\u7dda";
        return u"\u672a\u77e5";
    };
    auto number = [](int value) { auto n = std::to_string(value); return std::u16string(n.begin(), n.end()); };
    auto regionName = [](const std::optional<OptionValue>& value) -> std::u16string {
        if (!value) return u"\u672a\u9078\u53d6";
        if (value->id == "north") return u"\u5317\u5340";
        if (value->id == "central") return u"\u4e2d\u5340";
        if (value->id == "south") return u"\u5357\u5340";
        return u"\u672a\u77e5";
    };
    handles.draft.connectSource([this, modeName, regionName, number] { return u"\u76ee\u524d\u7de8\u8f2f\u503c\uff1a" + *handles.name.draft() +
        u" / \u63d0\u793a\uff1a" + (*handles.notices.draft() ? u"\u555f\u7528" : u"\u505c\u7528") + u" / \u6a21\u5f0f\uff1a" + modeName(*handles.mode.draft()) + u" / \u5730\u5340\uff1a" + regionName(*handles.region.draft()) + u" / \u97f3\u91cf\uff1a" + number(*handles.volume.draft()); });
    handles.result.connectSource([this, modeName, regionName, number] { return u"\u4e0a\u6b21\u5957\u7528\u7d50\u679c\uff1a" + applied_ +
        u" / \u63d0\u793a\uff1a" + (noticesApplied_ ? u"\u555f\u7528" : u"\u505c\u7528") + u" / \u6a21\u5f0f\uff1a" + modeName(modeApplied_) + u" / \u5730\u5340\uff1a" + regionName(regionApplied_) + u" / \u97f3\u91cf\uff1a" + number(volumeApplied_); });
    handles.count.connectSource([this] { auto n = std::to_string(applies_); return u"\u5957\u7528\u6b21\u6578\uff1a" + std::u16string(n.begin(), n.end()); });
    handles.name.onBlur([this] { validate(); });
    ui.onChange([this] { if (handles.name.describe()->invalid) validate(); refreshConflict(); });
    ui.connectConfirmation(handles.apply, {handles.name, handles.notices, handles.mode, handles.region, handles.volume}, [this](const auto& changes) { return apply(changes); });
    bindAction(handles.normal, [this] { setMode(Mode::normal); return Result{}; });
    bindAction(handles.readOnly, [this] { setMode(Mode::readOnly); return Result{}; });
    bindAction(handles.disabled, [this] { setMode(Mode::disabled); return Result{}; });
    bindAction(handles.offlineDisabled, [this] {
        offlineDisabled_ = !offlineDisabled_;
        return handles.mode.enableOption({"offline"}, !offlineDisabled_);
    });
    bindAction(handles.regionEmpty, [this] {
        regionEmpty_ = !regionEmpty_;
        return handles.region.setItems(regionEmpty_ ? std::vector<OptionItem>{} : regions());
    });
    bindAction(handles.longText, [this] {
        longIntro_ = !longIntro_;
        if (introChanged_) introChanged_();
        return Result{};
    });
    bindAction(handles.reset, [this] { return reset(); });
    bindAction(handles.noticesLatest, [this] { auto result = handles.notices.useLatest(); refreshConflict(); return result; });
    bindAction(handles.modeLatest, [this] { auto result = handles.mode.useLatest(); refreshConflict(); return result; });
    bindAction(handles.regionLatest, [this] { auto result = handles.region.useLatest(); refreshConflict(); return result; });
    bindAction(handles.volumeLatest, [this] { auto result = handles.volume.useLatest(); refreshConflict(); return result; });
    bindAction(handles.latest, [this] { auto result = handles.name.useLatest(); refreshConflict(); return result; });
    bindAction(handles.error, [this] {
        setMode(Mode::normal);
        handles.name.setValue(u"");
        validate();
        handles.name.focus();
        return Result{};
    });
    if (pages_) {
        const auto prefix = i.title + ".pages.";
        list = std::make_unique<listPage::Page>(&ui, &scroll, prefix + "v02.");
        handles.v03Title = ui.addLabel({.id = prefix + "v03.title", .value = u"確認與說明"}).value.value();
        auto v03TitleId = handles.v03Title.describe()->id;
        auto v03TitleText = handles.v03Title.describe()->value;
        handles.v03Title.connectSource([this, v03TitleId, v03TitleText] {
            return clipPaddingId_ == v03TitleId ? clipPaddingText_ : v03TitleText;
        });
        handles.progress = ui.addProgress({.id = prefix + "v03.progress", .label = u"示範進度"}).value.value();
        handles.progressSource = ui.addLabel({.id = prefix + "v03.source"}).value.value();
        handles.progressZero = ui.addButton({.id = prefix + "v03.zero", .label = u"0%"}).value.value();
        handles.progressHalf = ui.addButton({.id = prefix + "v03.half", .label = u"50%"}).value.value();
        handles.progressTip = ui.addTooltip(prefix + "v03.half.tip", handles.progressHalf, u"\u53ea\u8abf\u6574\u793a\u7bc4\u9032\u5ea6\uff0c\u4e0d\u6703\u555f\u52d5\u80cc\u666f\u5de5\u4f5c\u3002").value.value();
        handles.progressFull = ui.addButton({.id = prefix + "v03.full", .label = u"100%"}).value.value();
        handles.progressBelow = ui.addButton({.id = prefix + "v03.below", .label = u"-10"}).value.value();
        handles.progressAbove = ui.addButton({.id = prefix + "v03.above", .label = u"110"}).value.value();
        handles.progress.connectSource({[this] { return progress_; }, [this](std::function<void()> changed) {
            progressChanged_ = std::move(changed); return [this] { progressChanged_ = {}; };
        }});
        handles.progressSource.connectSource([this] {
            auto number = std::to_string(progress_);
            return u"來源值：" + std::u16string(number.begin(), number.end());
        });
        bindAction(handles.progressZero, [this] { updateProgress(0); return Result{}; });
        bindAction(handles.progressHalf, [this] { updateProgress(50); return Result{}; });
        bindAction(handles.progressFull, [this] { updateProgress(100); return Result{}; });
        bindAction(handles.progressBelow, [this] { updateProgress(-10); return Result{}; });
        bindAction(handles.progressAbove, [this] { updateProgress(110); return Result{}; });
        handles.resetV03 = ui.addButton({.id = prefix + "v03.reset", .label = u"Reset page"}).value.value();
        bindAction(handles.resetV03, [this] {
            if (!handles.dialogTemporary.describe()) {
                auto id = ids_.title + ".pages.v03.temporary";
                auto replacement = ui.addButton({.id = std::move(id), .label = u"Temporary opener..."}).value.value();
                if (auto result = handles.tabs.addMember({"v03"}, replacement); !result) return result;
                handles.dialogTemporary = replacement;
                bindAction(handles.dialogTemporary, [this] { return handles.dialog.open(handles.dialogTemporary); });
            }
            updateProgress(50); dialogConfirmations_ = 0; dialogResult_ = u"No result"; scroll.offset = 0; return handles.resetV03.focus();
        });
        handles.dialogOpen = ui.addButton({.id = prefix + "v03.open", .label = u"Reset progress..."}).value.value();
        handles.dialogTemporary = ui.addButton({.id = prefix + "v03.temporary", .label = u"Temporary opener..."}).value.value();
        handles.dialogResult = ui.addLabel({.id = prefix + "v03.result"}).value.value();
        auto dialogResultId = handles.dialogResult.describe()->id;
        handles.dialogResult.connectSource([this, dialogResultId] {
            if (clipPaddingId_ == dialogResultId) return clipPaddingText_;
            auto count = std::to_string(dialogConfirmations_);
            return u"Result: " + dialogResult_ + u"; confirmations: " + std::u16string(count.begin(), count.end());
        });
        handles.dialogTitle = ui.addLabel({.id = prefix + "dialog.title", .value = u"\u91cd\u8a2d\u793a\u7bc4\u9032\u5ea6"}).value.value();
        handles.dialogBody = ui.addLabel({.id = prefix + "dialog.body", .value = u"\u5c07\u9032\u5ea6\u6539\u70ba 0%\u3002\u5176\u4ed6\u9801\u9762\u8cc7\u6599\u6703\u4fdd\u7559\u3002"}).value.value();
        handles.dialogCancel = ui.addButton({.id = prefix + "dialog.cancel", .label = u"\u53d6\u6d88"}).value.value();
        handles.dialogConfirm = ui.addButton({.id = prefix + "dialog.confirm", .label = u"\u91cd\u8a2d\u9032\u5ea6"}).value.value();
        handles.dialog = ui.addDialog(prefix + "dialog", {handles.dialogTitle, handles.dialogBody, handles.dialogCancel, handles.dialogConfirm}, handles.dialogCancel, handles.dialogOpen,
            [this](bool confirmed) { dialogResult_ = confirmed ? u"Confirmed" : u"Cancelled"; if (confirmed) { updateProgress(0); ++dialogConfirmations_; } }, handles.dialogTitle).value.value();
        bindAction(handles.dialogOpen, [this] { return handles.dialog.open(handles.dialogOpen); });
        bindAction(handles.dialogTemporary, [this] { return handles.dialog.open(handles.dialogTemporary); });
        bindAction(handles.dialogCancel, [this] { return handles.dialog.settle(false); });
        bindAction(handles.dialogConfirm, [this] { return handles.dialog.settle(true); });
        handles.v04Title = ui.addLabel({.id = prefix + "v04.title", .value = u"\u5448\u73fe\u8a2d\u5b9a"}).value.value();
        auto resolve = [this](std::u16string_view family) {
            if (resolveFont_) return resolveFont_(family);
            FontResolution result; result.requested = family; result.primary = family.empty() ? u"Segoe UI" : std::u16string(family); return result;
        };
        std::vector<OptionItem> fonts;
        auto addFont = [&](std::string id, std::u16string family, std::u16string label, bool available = true) {
            OptionValue value{std::move(id)};
            fonts.push_back({value, std::move(label), available});
            fontChoices_.emplace_back(std::move(value), std::move(family));
        };
        addFont("default", {}, u"\u9810\u8a2d");
        if (fontFamilies) {
            for (const auto& family : *fontFamilies)
                addFont("family:" + std::to_string(fontChoices_.size()), family.key, family.displayName);
        } else {
            for (auto [id, family] : {std::pair{"jhenghei", u"Microsoft JhengHei"}, {"segoe", u"Segoe UI"}, {"mingliu", u"MingLiU"}, {"consolas", u"Consolas"}}) {
                bool available = resolve(family).requestedAvailable;
                addFont(id, family, std::u16string(family) + (available ? u"" : u"\uff08\u672a\u5b89\u88dd\uff09"), available);
            }
        }
        addFont("missing", u"Missing test font", u"\u4e0d\u5b58\u5728\u7684\u5b57\u578b\uff08\u6e2c\u8a66\uff09");
        handles.font = ui.addDropdown({.id = prefix + "v04.font", .label = u"\u5b57\u578b"}, std::move(fonts)).value.value();
        handles.font.connectSource({[this] {
            const auto family = ui.presentation().fontFamily;
            auto choice = std::find_if(fontChoices_.begin(), fontChoices_.end(),
                                       [&](const auto& font) { return font.second == family; });
            return std::optional<OptionValue>{choice == fontChoices_.end() ? OptionValue{"missing"} : choice->first};
        }, [this](std::function<void()> changed) { fontChanged_ = std::move(changed); return [this] { fontChanged_ = {}; }; }});
        handles.font.connectImmediate([this](const Change<DropdownHandle>& change) {
            if (!change.value) return Result{Status::invalidValue, u"Font choice required"};
            auto choice = std::find_if(fontChoices_.begin(), fontChoices_.end(),
                                       [&](const auto& font) { return font.first == *change.value; });
            if (choice == fontChoices_.end()) return Result{Status::invalidValue, u"Unknown font choice"};
            auto settings = ui.presentation();
            settings.fontFamily = choice->second;
            auto result = ui.setPresentation(settings); if (result && fontChanged_) fontChanged_(); return result;
        });
        handles.fontStatus = ui.addLabel({.id = prefix + "v04.status"}).value.value();
        handles.fontStatus.connectSource([this, resolve] {
            auto result = resolve(ui.presentation().fontFamily);
            auto size = std::to_string(ui.presentation().textSize);
            return u"\u76ee\u524d\u72c0\u614b\uff1a" + result.primary + (result.requestedAvailable ? u"" : u"\uff08\u5df2\u6539\u7528\u9810\u8a2d\uff09") + u"\uff0c\u6587\u5b57\u5927\u5c0f " + std::u16string(size.begin(), size.end()) +
                (ui.presentation().variant == ThemeVariant::dark() ? u"\uff0c\u6df1\u8272" : u"\uff0c\u6dfa\u8272") +
                (ui.presentation().density == Density::compact ? u"\uff0c\u7dca\u6e4a" : u"\uff0c\u6a19\u6e96");
        });
        handles.resetV04 = ui.addButton({.id = prefix + "v04.reset", .label = u"\u91cd\u8a2d V04"}).value.value();
        bindAction(handles.resetV04, [this] {
            auto result = ui.setPresentation({}); if (!result) return result;
            if (fontChanged_) fontChanged_(); if (textSizeChanged_) textSizeChanged_(); if (darkChanged_) darkChanged_();
            if (compactChanged_) compactChanged_();
            handles.font.useLatest(); handles.textSize.useLatest(); handles.dark.useLatest(); handles.compact.useLatest(); return Result{};
        });
        handles.textSize = ui.addSlider({.id = prefix + "v04.text_size", .label = u"\u6587\u5b57\u5927\u5c0f"}, {12, 28, 1}).value.value();
        handles.dark = ui.addCheckbox({.id = prefix + "v04.dark", .label = u"\u6df1\u8272"}).value.value();
        handles.dark.connectSource({[this] { return ui.presentation().variant == ThemeVariant::dark(); }, [this](std::function<void()> changed) { darkChanged_ = std::move(changed); return [this] { darkChanged_ = {}; }; }});
        handles.dark.connectImmediate([this](const Change<CheckboxHandle>& change) {
            auto settings = ui.presentation(); settings.variant = change.value ? ThemeVariant::dark() : ThemeVariant::light();
            auto result = ui.setPresentation(settings); if (result && darkChanged_) darkChanged_(); return result;
        });
        handles.compact = ui.addCheckbox({.id = prefix + "v04.compact", .label = u"\u7dfb\u6e4a"}).value.value();
        handles.compact.connectSource({[this] { return ui.presentation().density == Density::compact; },
            [this](std::function<void()> changed) { compactChanged_ = std::move(changed); return [this] { compactChanged_ = {}; }; }});
        handles.compact.connectImmediate([this](const Change<CheckboxHandle>& change) {
            auto settings = ui.presentation(); settings.density = change.value ? Density::compact : Density::standard;
            auto result = ui.setPresentation(settings);
            if (result && compactChanged_) compactChanged_();
            return result;
        });
        handles.preview = ui.addTextField({.id = prefix + "v04.preview", .label = u"\u9810\u89bd", .value = u"\u7e41\u4e2d English \U0001f600 \U00020000 \u2764\ufe0f"}).value.value();
        handles.preview.connectSource({[this] { return preview_; }, [](std::function<void()>) { return [] {}; }});
        handles.preview.connectImmediate([this](const Change<TextFieldHandle>& change) { preview_ = change.value; return Result{}; });
        // #84: one mixed-script paragraph (Chinese, English and digits) whose alignment and justification method
        // the two dropdowns switch live, and short labels to compare distribute with left alignment.
        handles.justifySample = ui.addLabel({.id = prefix + "v04.justify.sample", .value = justifySampleText, .textAlign = TextAlign::justify}).value.value();
        handles.distributeName = ui.addLabel({.id = prefix + "v04.justify.name", .value = u"姓　名", .textAlign = TextAlign::distribute}).value.value();
        handles.distributePlain = ui.addLabel({.id = prefix + "v04.justify.plain", .value = u"姓　名"}).value.value();
        handles.justifyAlign = ui.addDropdown({.id = prefix + "v04.justify.align", .label = u"對齊"},
            {{{"left"}, u"靠左"}, {{"center"}, u"置中"}, {{"right"}, u"靠右"}, {{"justify"}, u"左右對齊"}, {{"distribute"}, u"分散對齊"}}).value.value();
        handles.justifyMethod = ui.addDropdown({.id = prefix + "v04.justify.method", .label = u"對齊方法"},
            {{{"automatic"}, u"自動"}, {{"spaces"}, u"只拉空格"}, {{"per_character"}, u"每字元"}}).value.value();
        handles.justifyAlign.connectSource({[this] {
            const auto align = handles.justifySample.textAlign().value.value_or(TextAlign::left);
            return std::optional<OptionValue>{{align == TextAlign::center ? "center" : align == TextAlign::right ? "right" : align == TextAlign::justify ? "justify" : align == TextAlign::distribute ? "distribute" : "left"}};
        }, [this](std::function<void()> changed) { justifyAlignChanged_ = std::move(changed); return [this] { justifyAlignChanged_ = {}; }; }});
        handles.justifyAlign.connectImmediate([this](const Change<DropdownHandle>& change) {
            if (!change.value) return Result{Status::invalidValue, u"Alignment choice required"};
            const auto& id = change.value->id;
            auto result = handles.justifySample.setTextAlign(id == "center" ? TextAlign::center : id == "right" ? TextAlign::right : id == "justify" ? TextAlign::justify : id == "distribute" ? TextAlign::distribute : TextAlign::left);
            if (result && justifyAlignChanged_) justifyAlignChanged_();
            return result;
        });
        handles.justifyMethod.connectSource({[this] {
            const auto method = handles.justifySample.justifyMethod().value.value_or(JustifyMethod::automatic);
            return std::optional<OptionValue>{{method == JustifyMethod::spaces ? "spaces" : method == JustifyMethod::perCharacter ? "per_character" : "automatic"}};
        }, [this](std::function<void()> changed) { justifyMethodChanged_ = std::move(changed); return [this] { justifyMethodChanged_ = {}; }; }});
        handles.justifyMethod.connectImmediate([this](const Change<DropdownHandle>& change) {
            if (!change.value) return Result{Status::invalidValue, u"Method choice required"};
            const auto& id = change.value->id;
            auto result = handles.justifySample.setJustifyMethod(id == "spaces" ? JustifyMethod::spaces : id == "per_character" ? JustifyMethod::perCharacter : JustifyMethod::automatic);
            if (result && justifyMethodChanged_) justifyMethodChanged_();
            return result;
        });
        handles.textSize.connectSource({[this] { return ui.presentation().textSize; }, [this](std::function<void()> changed) { textSizeChanged_ = std::move(changed); return [this] { textSizeChanged_ = {}; }; }});
        handles.textSize.connectImmediate([this](const Change<SliderHandle>& change) {
            auto settings = ui.presentation(); settings.textSize = change.value;
            auto result = ui.setPresentation(settings); if (result && textSizeChanged_) textSizeChanged_(); return result;
        });
        handles.tabs = ui.addTabs({.id = prefix + "tabs", .label = u"Showcase"}, {
            {{"v01"}, u"Edit form", true, {handles.title, handles.intro, handles.normal, handles.readOnly, handles.disabled,
                handles.error, handles.longText, handles.reset, handles.nameLabel, handles.name, handles.nameHelp,
                handles.nameError, handles.notices, handles.noticesLatest, handles.mode, handles.modeLatest, handles.region, handles.regionLatest, handles.volume, handles.volumeLatest,
                handles.offlineDisabled, handles.regionEmpty, handles.apply, handles.state, handles.draft, handles.result,
                handles.count, handles.latest}},
            {{"v02"}, u"List", true, {list->handles.title, list->handles.zero, list->handles.one, list->handles.twenty,
                list->handles.hundred, list->handles.sort, list->handles.disableEight, list->handles.reset,
                list->handles.search, list->handles.list, list->handles.erase, list->handles.detail}},
            {{"v03"}, u"Confirm", true, {handles.v03Title, handles.progress, handles.progressSource, handles.dialogResult,
                handles.progressZero, handles.progressHalf, handles.progressFull, handles.progressBelow, handles.progressAbove, handles.resetV03, handles.dialogOpen, handles.dialogTemporary}},
            {{"v04"}, u"\u5448\u73fe\u8a2d\u5b9a", true, {handles.v04Title, handles.font, handles.textSize, handles.dark, handles.compact, handles.preview, handles.fontStatus, handles.resetV04,
                handles.justifySample, handles.justifyAlign, handles.justifyMethod, handles.distributeName, handles.distributePlain}}
        }).value.value();
        handles.tabs.onPageChange([this](OptionValue from, OptionValue to) {
            auto index = [](const OptionValue& page) { return page.id == "v01" ? 0 : page.id == "v02" ? 1 : page.id == "v03" ? 2 : 3; };
            pageOffsets_[index(from)] = scroll.offset;
            scroll.offset = pageOffsets_[index(to)];
        });
    }
}

Result Form::bindAction(ButtonHandle button, std::function<Result()> action) {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto result = button.bind(action);
    if (result) fixtureActions_.insert_or_assign(semantic->id, std::move(action));
    return result;
}
Result Form::restoreFixtureAction(ButtonHandle button) const {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto found = fixtureActions_.find(semantic->id);
    if (found != fixtureActions_.end()) return button.bind(found->second);
    if (list) return list->restoreFixtureAction(button);
    return {Status::notFound, u"No original fixture action"};
}
Result Form::setClipPadding(LabelHandle label, std::u16string text) {
    auto semantic = label.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto id = semantic->id;
    if (list && (id == list->handles.title.describe()->id || id == list->handles.detail.describe()->id))
        return list->setClipPadding(label, std::move(text));
    if (id != handles.title.describe()->id && id != handles.intro.describe()->id &&
        (!pages_ || (id != handles.v03Title.describe()->id && id != handles.dialogResult.describe()->id && id != handles.v04Title.describe()->id)))
        return {Status::invalidValue, u"Unknown clipping fixture label"};
    const bool affectsIntro = clipPaddingId_ == ids_.intro || id == ids_.intro;
    clipPaddingId_ = std::move(id); clipPaddingText_ = std::move(text);
    if (affectsIntro && introChanged_) introChanged_();
    return {};
}
void Form::clearClipPadding() {
    const bool affectsIntro = clipPaddingId_ == ids_.intro;
    clipPaddingId_.clear(); clipPaddingText_.clear();
    if (affectsIntro && introChanged_) introChanged_();
    if (list) list->clearClipPadding();
}

bool Form::validate() {
    bool valid = !blank(*handles.name.draft());
    handles.name.setError(valid ? u"" : requiredName);
    return valid;
}
ConfirmationResult Form::apply(const std::vector<ConfirmFieldChange>& changes) {
    if (auto observer = std::exchange(nextApplyObserver_, {})) observer();
    const auto& name = std::get<Change<TextFieldHandle>>(changes[0]);
    const auto& notices = std::get<Change<CheckboxHandle>>(changes[1]);
    const auto& mode = std::get<Change<RadioGroupHandle>>(changes[2]);
    const auto& region = std::get<Change<DropdownHandle>>(changes[3]);
    const auto& volume = std::get<Change<SliderHandle>>(changes[4]);
    std::vector<ConfirmFieldError> errors;
    if (blank(name.value)) errors.push_back({name.field, requiredName});
    if (!region.value || (region.value->id != "north" && region.value->id != "central" && region.value->id != "south")) errors.push_back({region.field, requiredRegion});
    if (!errors.empty()) return {{Status::invalidValue, u"\u8acb\u4fee\u6b63\u6b04\u4f4d"}, std::move(errors)};
    if (nextFieldError_) {
        auto error = std::move(*nextFieldError_); nextFieldError_.reset();
        return {{Status::invalidValue, u"Fixture rejected apply"}, {std::move(error)}};
    }
    if (rejectNoticesConflict_) {
        rejectNoticesConflict_ = false;
        handles.notices.toggle();
        noticesApplied_ = notices.value;
        if (noticesChanged_) noticesChanged_();
        refreshConflict();
        return {{Status::invalidValue, u"Source changed during apply"}, {}};
    }
    applied_ = name.value;
    noticesApplied_ = notices.value;
    modeApplied_ = mode.value;
    regionApplied_ = region.value;
    volumeApplied_ = volume.value;
    ++applies_;
    if (sourceChanged_) sourceChanged_();
    if (noticesChanged_) noticesChanged_();
    if (modeChanged_) modeChanged_();
    if (regionChanged_) regionChanged_();
    if (volumeChanged_) volumeChanged_();
    return {};
}
void Form::updateSource(std::u16string value) {
    applied_ = std::move(value);
    if (sourceChanged_) sourceChanged_();
    refreshConflict();
}
void Form::updateVolumeSource(int value) {
    volumeApplied_ = value;
    if (volumeChanged_) volumeChanged_();
    refreshConflict();
}
void Form::updateModeSource(OptionValue value) {
    modeApplied_ = std::move(value); if (modeChanged_) modeChanged_(); refreshConflict();
}
void Form::updateRegionSource(OptionValue value) {
    regionApplied_ = std::move(value); if (regionChanged_) regionChanged_(); refreshConflict();
}
void Form::updateProgress(int value) {
    progress_ = value;
    if (progressChanged_) progressChanged_();
}
void Form::refreshConflict() {
    handles.noticesLatest.enable(mode_ != Mode::disabled && handles.notices.describe()->sourceConflict);
    handles.modeLatest.enable(mode_ != Mode::disabled && handles.mode.describe()->sourceConflict);
    handles.regionLatest.enable(mode_ != Mode::disabled && handles.region.describe()->sourceConflict);
    handles.volumeLatest.enable(mode_ != Mode::disabled && handles.volume.describe()->sourceConflict);
    handles.latest.enable(mode_ != Mode::disabled && mode_ != Mode::readOnly && handles.name.sourceConflict().value.value_or(false));
}
void Form::setMode(Mode mode) {
    mode_ = mode;
    handles.name.readOnly(mode == Mode::readOnly);
    handles.name.enable(mode != Mode::disabled);
    handles.apply.enable(mode != Mode::disabled);
    handles.notices.enable(mode != Mode::disabled);
    handles.mode.enable(mode != Mode::disabled);
    handles.region.enable(mode != Mode::disabled);
    handles.volume.enable(mode != Mode::disabled);
    refreshConflict();
}
Result Form::reset() {
    setMode(Mode::normal);
    longIntro_ = false;
    if (introChanged_) introChanged_();
    updateSource(initialName);
    noticesApplied_ = true; modeApplied_ = OptionValue{"normal"}; regionApplied_ = OptionValue{"north"}; volumeApplied_ = 40;
    regionEmpty_ = false; handles.region.setItems(regions());
    if (noticesChanged_) noticesChanged_();
    if (modeChanged_) modeChanged_();
    if (regionChanged_) regionChanged_();
    if (volumeChanged_) volumeChanged_();
    handles.notices.useLatest(); handles.mode.useLatest(); handles.region.useLatest(); handles.volume.useLatest();
    offlineDisabled_ = false; handles.mode.enableOption({"offline"}, true);
    handles.name.resetToSource();
    refreshConflict();
    applies_ = 0;
    scroll.offset = 0;
    handles.reset.focus();
    return {};
}

layout::Node Form::page() const {
    using namespace layout;
    std::vector<Node> fields{component(handles.name)};
    fields.push_back(component(handles.notices)); fields.push_back(component(handles.noticesLatest));
    fields.push_back(component(handles.mode)); fields.push_back(component(handles.modeLatest));
    fields.push_back(component(handles.region)); fields.push_back(component(handles.regionLatest));
    fields.push_back(component(handles.volume)); fields.push_back(component(handles.volumeLatest));
    fields.push_back(row({component(handles.apply), component(handles.latest), component(handles.state)}, 16));
    auto form = layout::scroll(column({
        component(handles.title, 24),
        component(handles.intro),
        grid({component(handles.normal), component(handles.readOnly), component(handles.disabled), component(handles.error), component(handles.longText), component(handles.offlineDisabled), component(handles.regionEmpty), component(handles.reset)}, 120, 8),
        grid({column(std::move(fields), 12),
              column({component(handles.draft), component(handles.result), component(handles.count)}, 8)},
             360, 32),
    }, 16, 24));
    if (!pages_) return form;
    auto active = *handles.tabs.active();
    layout::Node content;
    if (active.id == "v01") content = std::move(form);
    else if (active.id == "v02") content = list->page();
    else if (active.id == "v04") {
        std::vector<Node> items;
        bool clippingFixture = clipPaddingId_ == handles.v04Title.describe()->id;
        if (clippingFixture) items.push_back(column({}, 0, 700));
        items.push_back(component(handles.v04Title, 24));
        items.push_back(component(handles.font));
        items.push_back(component(handles.textSize));
        items.push_back(component(handles.dark));
        items.push_back(component(handles.compact));
        items.push_back(component(handles.preview));
        items.push_back(component(handles.fontStatus));
        items.push_back(component(handles.resetV04));
        // The alignment comparison: the paragraph with its two dropdowns, then "姓　名" distributed next to the same label left-aligned.
        items.push_back(component(handles.justifySample));
        items.push_back(grid({component(handles.justifyAlign), component(handles.justifyMethod)}, 200, 16));
        items.push_back(grid({component(handles.distributeName), component(handles.distributePlain)}, 120, 16));
        if (clippingFixture) items.push_back(column({}, 0, 700));
        content = layout::scroll(column(std::move(items), 16, 24));
    }

    else content = layout::scroll(column({component(handles.v03Title, 24), component(handles.progress), component(handles.progressSource), component(handles.dialogResult), component(handles.dialogOpen), component(handles.dialogTemporary),
        grid({component(handles.progressZero), component(handles.progressHalf), component(handles.progressFull),
              component(handles.progressBelow), component(handles.progressAbove), component(handles.resetV03)}, 120, 8)}, 16, 24));
    return modal(pinned(component(handles.tabs), std::move(content)),
        column({component(handles.dialogTitle, 20), component(handles.dialogBody), row({component(handles.dialogCancel), component(handles.dialogConfirm)})}, 16), handles.dialog);
}
}
