#include "list_page.hpp"
#include <algorithm>
#include <cstdio>

namespace list_page {
using namespace lcars;

Page::Page() : ui({}) {
    handles.title = ui.add_label({.id = "title", .kind = Kind::label, .value = u"範本清單"});
    handles.zero = ui.add_button({.id = "zero", .kind = Kind::button, .label = u"0 筆"});
    handles.one = ui.add_button({.id = "one", .kind = Kind::button, .label = u"1 筆"});
    handles.twenty = ui.add_button({.id = "twenty", .kind = Kind::button, .label = u"20 筆"});
    handles.hundred = ui.add_button({.id = "hundred", .kind = Kind::button, .label = u"100 筆"});
    handles.sort = ui.add_button({.id = "sort", .kind = Kind::button, .label = u"反轉排序"});
    handles.disable_eight = ui.add_button({.id = "disable", .kind = Kind::button, .label = u"停用／啟用 T008"});
    handles.reset = ui.add_button({.id = "reset", .kind = Kind::button, .label = u"重設"});
    handles.search = ui.add_text({.id = "search", .kind = Kind::text, .label = u"搜尋名稱"});
    handles.list = ui.add_list({.id = "list", .kind = Kind::list, .label = u"範本"}, {});
    handles.erase = ui.add_button({.id = "erase", .kind = Kind::button, .label = u"刪除選中項", .enabled = false});
    handles.detail = ui.add_label({.id = "detail", .kind = Kind::label});
    handles.search.connect_source({[this] { return search_; }, [this](auto changed) {
        search_changed_ = std::move(changed); return [this] { search_changed_ = {}; };
    }});
    handles.search.connect_immediate([this](const TextChange& change) {
        search_ = change.value; if (search_changed_) search_changed_(); return refresh();
    });
    handles.list.connect_source({[this] { return selection_; }, [this](auto changed) {
        selection_changed_ = std::move(changed); return [this] { selection_changed_ = {}; };
    }});
    handles.list.connect_immediate([this](std::optional<OptionValue> value) {
        selection_ = std::move(value); handles.erase.enable(selection_.has_value());
        if (selection_changed_) selection_changed_(); return Result{};
    });
    handles.detail.connect_source([this] {
        if (!selection_) return std::u16string(u"尚未選取範本");
        auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.value == *selection_; });
        return u"選取：" + std::u16string(selection_->id.begin(), selection_->id.end()) + u"  " + (it == items_.end() ? std::u16string{} : it->label);
    });
    handles.zero.bind([this] { return sample(0); }); handles.one.bind([this] { return sample(1); });
    handles.twenty.bind([this] { return sample(20); }); handles.hundred.bind([this] { return sample(100); });
    handles.reset.bind([this] { return sample(20); });
    handles.sort.bind([this] { descending_ = !descending_; return refresh(); });
    handles.disable_eight.bind([this] { disabled_eight_ = !disabled_eight_; return refresh(); });
    handles.erase.bind([this] {
        if (!selection_) return Result{Status::invalid_value, u"沒有選取項目"};
        auto selected = *selection_;
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const auto& item) { return item.value == selected; }), items_.end());
        return refresh();
    });
    sample(20);
}
Result Page::refresh() {
    std::vector<RadioOption> visible;
    for (auto item : items_) {
        if (item.label.find(search_) == std::u16string::npos) continue;
        if (item.value.id == "T008") item.enabled = !disabled_eight_;
        visible.push_back(std::move(item));
    }
    if (descending_) std::reverse(visible.begin(), visible.end());
    return handles.list.set_items(std::move(visible));
}
Result Page::sample(int count) {
    items_.clear();
    for (int n = 1; n <= count; ++n) {
        char id[8], number[8]; std::snprintf(id, sizeof(id), "T%03d", n); std::snprintf(number, sizeof(number), "%02d", n);
        auto label = n == 5 || n == 6 ? std::u16string(u"共用範本") : u"範本 " + std::u16string(number, number + std::char_traits<char>::length(number));
        items_.push_back({{id}, std::move(label)});
    }
    search_.clear(); descending_ = disabled_eight_ = false; selection_.reset(); scroll.offset = 0;
    handles.erase.enable(false); if (selection_changed_) selection_changed_(); if (search_changed_) search_changed_();
    handles.search.reset_to_source(); return refresh();
}
layout::Node Page::page() const {
    using namespace lcars::layout;
    return layout::scroll(column({control(handles.title, 24),
        row({control(handles.zero), control(handles.one), control(handles.twenty), control(handles.hundred)}, 6),
        row({control(handles.sort), control(handles.disable_eight), control(handles.reset)}, 6),
        control(handles.search),
        grid({control(handles.list), column({control(handles.detail), control(handles.erase)}, 12)}, 400, 16)
    }, 12, 16));
}
}
