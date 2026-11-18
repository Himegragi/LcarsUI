#include "list_page.hpp"
#include <algorithm>
#include <cstdio>

namespace listPage {
using namespace isoduo;

Result ImmediateSearchConnection::connect(TextFieldHandle field, std::function<Result()> refresh) {
    field_ = field;
    auto source = field_.connectSource({[this] { return value_; }, [this](auto changed) {
        changed_ = std::move(changed); return [this] { changed_ = {}; };
    }});
    if (!source) return source;
    auto write = field_.connectImmediate([this, refresh = std::move(refresh)](const Change<TextFieldHandle>& change) {
        value_ = change.value; if (changed_) changed_(); return refresh();
    });
    connected_ = bool(write);
    return write;
}
Result ImmediateSearchConnection::reset() {
    auto semantic = field_.describe();
    if (!semantic) return {semantic.status, semantic.message};
    // Reset is an owner action and remains available when the field is restricted.
    if (!semantic->enabled) field_.enable(true);
    if (semantic->readOnly) field_.readOnly(false);
    auto result = field_.setValue(u"");
    if (result) result = field_.resetToSource();
    if (semantic->readOnly) field_.readOnly(true);
    if (!semantic->enabled) field_.enable(false);
    return result;
}

Page::Page(Ui* sharedUi, ScrollState* sharedScroll, std::string prefix)
    : ownedUi_(sharedUi ? nullptr : std::make_unique<Ui>()),
      ui(sharedUi ? *sharedUi : *ownedUi_), scroll(sharedScroll ? *sharedScroll : ownedScroll_) {
    handles.title = ui.addLabel({.id = prefix + "title", .value = u"範本清單"}).value.value();
    auto titleId = handles.title.describe()->id;
    auto titleText = handles.title.describe()->value;
    handles.title.connectSource([this, titleId, titleText] {
        return clipPaddingId_ == titleId ? clipPaddingText_ : titleText;
    });
    handles.zero = ui.addButton({.id = prefix + "zero", .label = u"0 筆"}).value.value();
    handles.one = ui.addButton({.id = prefix + "one", .label = u"1 筆"}).value.value();
    handles.twenty = ui.addButton({.id = prefix + "twenty", .label = u"20 筆"}).value.value();
    handles.hundred = ui.addButton({.id = prefix + "hundred", .label = u"100 筆"}).value.value();
    handles.sort = ui.addButton({.id = prefix + "sort", .label = u"反轉排序"}).value.value();
    handles.disableEight = ui.addButton({.id = prefix + "disable", .label = u"停用／啟用 T008"}).value.value();
    handles.reset = ui.addButton({.id = prefix + "reset", .label = u"重設"}).value.value();
    handles.search = ui.addTextField({.id = prefix + "search", .label = u"搜尋名稱"}).value.value();
    handles.list = ui.addList({.id = prefix + "list", .label = u"範本"}, {}).value.value();
    handles.erase = ui.addButton({.id = prefix + "erase", .label = u"刪除選中項", .enabled = false}).value.value();
    handles.detail = ui.addLabel({.id = prefix + "detail"}).value.value();
    search_.connect(handles.search, [this] { return refresh(); });
    handles.list.connectSource({[this] { return selection_; }, [this](auto changed) {
        selectionChanged_ = std::move(changed); return [this] { selectionChanged_ = {}; };
    }});
    handles.list.connectImmediate([this](const Change<ListHandle>& change) {
        selection_ = change.value; handles.erase.enable(selection_.has_value());
        if (selectionChanged_) selectionChanged_(); return Result{};
    });
    auto detailId = handles.detail.describe()->id;
    handles.detail.connectSource([this, detailId] {
        if (clipPaddingId_ == detailId) return clipPaddingText_;
        if (!selection_) return std::u16string(u"尚未選取範本");
        auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.value == *selection_; });
        return u"選取：" + std::u16string(selection_->id.begin(), selection_->id.end()) + u"  " + (it == items_.end() ? std::u16string{} : it->label);
    });
    bindAction(handles.zero, [this] { return sample(0); }); bindAction(handles.one, [this] { return sample(1); });
    bindAction(handles.twenty, [this] { return sample(20); }); bindAction(handles.hundred, [this] { return sample(100); });
    bindAction(handles.reset, [this] { auto result = sample(20); if (result) return handles.reset.focus(); return result; });
    bindAction(handles.sort, [this] { descending_ = !descending_; return refresh(); });
    bindAction(handles.disableEight, [this] { disabledEight_ = !disabledEight_; return refresh(); });
    bindAction(handles.erase, [this] {
        if (!selection_) return Result{Status::invalidValue, u"沒有選取項目"};
        auto selected = *selection_;
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const auto& item) { return item.value == selected; }), items_.end());
        return refresh();
    });
    sample(20);
}
Result Page::bindAction(ButtonHandle button, std::function<Result()> action) {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto result = button.bind(action);
    if (result) fixtureActions_.insert_or_assign(semantic->id, std::move(action));
    return result;
}
Result Page::restoreFixtureAction(ButtonHandle button) const {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto found = fixtureActions_.find(semantic->id);
    if (found != fixtureActions_.end()) return button.bind(found->second);
    return {Status::notFound, u"No original fixture action"};
}
Result Page::setClipPadding(LabelHandle label, std::u16string text) {
    auto semantic = label.describe();
    if (!semantic || (semantic->id != handles.title.describe()->id && semantic->id != handles.detail.describe()->id))
        return {Status::invalidValue, u"Unknown clipping fixture label"};
    clipPaddingId_ = semantic->id; clipPaddingText_ = std::move(text); return {};
}
void Page::clearClipPadding() { clipPaddingId_.clear(); clipPaddingText_.clear(); }
Result Page::refresh() {
    std::vector<OptionItem> visible;
    for (auto item : items_) {
        if (item.label.find(search_.value()) == std::u16string::npos) continue;
        if (item.value.id == "T008") item.enabled = !disabledEight_;
        visible.push_back(std::move(item));
    }
    if (descending_) std::reverse(visible.begin(), visible.end());
    return handles.list.setItems(std::move(visible));
}
Result Page::sample(int count) {
    if (auto result = search_.reset(); !result) return result;
    items_.clear();
    for (int n = 1; n <= count; ++n) {
        char id[8], number[8]; std::snprintf(id, sizeof(id), "T%03d", n); std::snprintf(number, sizeof(number), "%02d", n);
        auto label = n == 5 || n == 6 ? std::u16string(u"共用範本") : u"範本 " + std::u16string(number, number + std::char_traits<char>::length(number));
        items_.push_back({{id}, std::move(label)});
    }
    descending_ = disabledEight_ = false; selection_.reset(); scroll.offset = 0; listScroll.offset = 0;
    handles.erase.enable(false); if (selectionChanged_) selectionChanged_();
    return refresh();
}
layout::Node Page::page() const {
    using namespace isoduo::layout;
    return layout::scroll(column({component(handles.title, 24),
        row({component(handles.zero), component(handles.one), component(handles.twenty), component(handles.hundred)}, 6),
        row({component(handles.sort), component(handles.disableEight), component(handles.reset)}, 6),
        component(handles.search),
        grid({layout::scroll(component(handles.list), listScroll, 220), column({component(handles.detail), component(handles.erase)}, 12)}, 400, 16)
    }, 12, 16));
}
}
