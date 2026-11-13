#include "list_page.hpp"
#include <algorithm>
#include <cstdio>

namespace list_page {
using namespace lcars;

Result ImmediateSearchConnection::connect(TextHandle field, std::function<Result()> refresh) {
    field_ = field;
    auto source = field_.connect_source({[this] { return value_; }, [this](auto changed) {
        changed_ = std::move(changed); return [this] { changed_ = {}; };
    }});
    if (!source) return source;
    auto write = field_.connect_immediate([this, refresh = std::move(refresh)](const Change<TextHandle>& change) {
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
    if (semantic->read_only) field_.read_only(false);
    auto result = field_.set_value(u"");
    if (result) result = field_.reset_to_source();
    if (semantic->read_only) field_.read_only(true);
    if (!semantic->enabled) field_.enable(false);
    return result;
}

Page::Page(Ui* shared_ui, ScrollState* shared_scroll, std::string prefix)
    : owned_ui_(shared_ui ? nullptr : std::make_unique<Ui>()),
      ui(shared_ui ? *shared_ui : *owned_ui_), scroll(shared_scroll ? *shared_scroll : owned_scroll_) {
    handles.title = ui.add_label({.id = prefix + "title", .value = u"範本清單"}).value.value();
    auto title_id = handles.title.describe()->id;
    auto title_text = handles.title.describe()->value;
    handles.title.connect_source([this, title_id, title_text] {
        return clip_padding_id_ == title_id ? clip_padding_text_ : title_text;
    });
    handles.zero = ui.add_button({.id = prefix + "zero", .label = u"0 筆"}).value.value();
    handles.one = ui.add_button({.id = prefix + "one", .label = u"1 筆"}).value.value();
    handles.twenty = ui.add_button({.id = prefix + "twenty", .label = u"20 筆"}).value.value();
    handles.hundred = ui.add_button({.id = prefix + "hundred", .label = u"100 筆"}).value.value();
    handles.sort = ui.add_button({.id = prefix + "sort", .label = u"反轉排序"}).value.value();
    handles.disable_eight = ui.add_button({.id = prefix + "disable", .label = u"停用／啟用 T008"}).value.value();
    handles.reset = ui.add_button({.id = prefix + "reset", .label = u"重設"}).value.value();
    handles.search = ui.add_text({.id = prefix + "search", .label = u"搜尋名稱"}).value.value();
    handles.list = ui.add_list({.id = prefix + "list", .label = u"範本"}, {}).value.value();
    handles.erase = ui.add_button({.id = prefix + "erase", .label = u"刪除選中項", .enabled = false}).value.value();
    handles.detail = ui.add_label({.id = prefix + "detail"}).value.value();
    search_.connect(handles.search, [this] { return refresh(); });
    handles.list.connect_source({[this] { return selection_; }, [this](auto changed) {
        selection_changed_ = std::move(changed); return [this] { selection_changed_ = {}; };
    }});
    handles.list.connect_immediate([this](std::optional<OptionValue> value) {
        selection_ = std::move(value); handles.erase.enable(selection_.has_value());
        if (selection_changed_) selection_changed_(); return Result{};
    });
    auto detail_id = handles.detail.describe()->id;
    handles.detail.connect_source([this, detail_id] {
        if (clip_padding_id_ == detail_id) return clip_padding_text_;
        if (!selection_) return std::u16string(u"尚未選取範本");
        auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.value == *selection_; });
        return u"選取：" + std::u16string(selection_->id.begin(), selection_->id.end()) + u"  " + (it == items_.end() ? std::u16string{} : it->label);
    });
    bind_action(handles.zero, [this] { return sample(0); }); bind_action(handles.one, [this] { return sample(1); });
    bind_action(handles.twenty, [this] { return sample(20); }); bind_action(handles.hundred, [this] { return sample(100); });
    bind_action(handles.reset, [this] { auto result = sample(20); if (result) return handles.reset.focus(); return result; });
    bind_action(handles.sort, [this] { descending_ = !descending_; return refresh(); });
    bind_action(handles.disable_eight, [this] { disabled_eight_ = !disabled_eight_; return refresh(); });
    bind_action(handles.erase, [this] {
        if (!selection_) return Result{Status::invalid_value, u"沒有選取項目"};
        auto selected = *selection_;
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const auto& item) { return item.value == selected; }), items_.end());
        return refresh();
    });
    sample(20);
}
Result Page::bind_action(ButtonHandle button, std::function<Result()> action) {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto result = button.bind(action);
    if (result) fixture_actions_.insert_or_assign(semantic->id, std::move(action));
    return result;
}
Result Page::restore_fixture_action(ButtonHandle button) const {
    auto semantic = button.describe();
    if (!semantic) return {semantic.status, semantic.message};
    auto found = fixture_actions_.find(semantic->id);
    if (found != fixture_actions_.end()) return button.bind(found->second);
    return {Status::not_found, u"No original fixture action"};
}
Result Page::set_clip_padding(LabelHandle label, std::u16string text) {
    auto semantic = label.describe();
    if (!semantic || (semantic->id != handles.title.describe()->id && semantic->id != handles.detail.describe()->id))
        return {Status::invalid_value, u"Unknown clipping fixture label"};
    clip_padding_id_ = semantic->id; clip_padding_text_ = std::move(text); return {};
}
void Page::clear_clip_padding() { clip_padding_id_.clear(); clip_padding_text_.clear(); }
Result Page::refresh() {
    std::vector<OptionItem> visible;
    for (auto item : items_) {
        if (item.label.find(search_.value()) == std::u16string::npos) continue;
        if (item.value.id == "T008") item.enabled = !disabled_eight_;
        visible.push_back(std::move(item));
    }
    if (descending_) std::reverse(visible.begin(), visible.end());
    return handles.list.set_items(std::move(visible));
}
Result Page::sample(int count) {
    if (auto result = search_.reset(); !result) return result;
    items_.clear();
    for (int n = 1; n <= count; ++n) {
        char id[8], number[8]; std::snprintf(id, sizeof(id), "T%03d", n); std::snprintf(number, sizeof(number), "%02d", n);
        auto label = n == 5 || n == 6 ? std::u16string(u"共用範本") : u"範本 " + std::u16string(number, number + std::char_traits<char>::length(number));
        items_.push_back({{id}, std::move(label)});
    }
    descending_ = disabled_eight_ = false; selection_.reset(); scroll.offset = 0; list_scroll.offset = 0;
    handles.erase.enable(false); if (selection_changed_) selection_changed_();
    return refresh();
}
layout::Node Page::page() const {
    using namespace lcars::layout;
    return layout::scroll(column({component(handles.title, 24),
        row({component(handles.zero), component(handles.one), component(handles.twenty), component(handles.hundred)}, 6),
        row({component(handles.sort), component(handles.disable_eight), component(handles.reset)}, 6),
        component(handles.search),
        grid({layout::scroll(component(handles.list), list_scroll, 220), column({component(handles.detail), component(handles.erase)}, 12)}, 400, 16)
    }, 12, 16));
}
}
