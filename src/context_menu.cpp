#include "core_internal.hpp"
#include "component_type.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

// Context menu: one window-wide menu opened beside a component. It never moves core focus; the
// component keeps it, so "focus returns to the component" is a property of closing, not an action.
namespace lcars {
namespace {
char16_t fold(char16_t ch) { return ch >= u'a' && ch <= u'z' ? static_cast<char16_t>(ch - u'a' + u'A') : ch; }
bool reserved_id(std::string_view id) { return id.starts_with("lcars."); }
}

bool detail::UiState::has_menu(const Control& control) const {
    const bool builtin = component_type(control.kind).has_builtin_menu();
    auto attachment = menu_attachments_.find(control.id);
    if (attachment == menu_attachments_.end()) return builtin;
    switch (attachment->second.mode) {
    case MenuMode::off: return false;
    case MenuMode::replace: return !attachment->second.items.empty();
    case MenuMode::append: return builtin || !attachment->second.items.empty();
    }
    return false;
}
std::vector<detail::UiState::MenuEntry> detail::UiState::menu_entries(const Id& target) const {
    std::vector<MenuEntry> raw;
    const auto* control = find(target);
    if (!control) return raw;
    auto attachment = menu_attachments_.find(target);
    const auto mode = attachment == menu_attachments_.end() ? MenuMode::append : attachment->second.mode;
    if (mode == MenuMode::off) return raw;
    auto separator = [&] { MenuEntry entry; entry.separator = true; raw.push_back(std::move(entry)); };
    if (mode == MenuMode::append && component_type(control->kind).has_builtin_menu()) {
        const bool clipboard_text = menu_ && menu_->target == target && menu_->clipboard_text;
        component_type(control->kind).builtin_menu_entries(*this, *control, clipboard_text, raw);
    }
    if (attachment != menu_attachments_.end()) {
        if (!raw.empty() && !attachment->second.items.empty()) separator();
        for (const auto& item : attachment->second.items) {
            if (item.separator) { separator(); continue; }
            raw.push_back({item.id, item.label, item.shortcut_hint, item.enabled, false, item.access_key});
        }
    }
    std::vector<MenuEntry> entries;
    for (auto& entry : raw) {
        if (entry.separator && (entries.empty() || entries.back().separator)) continue;
        entries.push_back(std::move(entry));
    }
    while (!entries.empty() && entries.back().separator) entries.pop_back();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].separator) entries[i].id = "separator:" + std::to_string(i);
    return entries;
}
std::optional<detail::UiState::MenuView> detail::UiState::menu_view() const {
    if (!menu_) return std::nullopt;
    MenuView view;
    view.target = menu_->target; view.item = menu_->item; view.at = menu_->at;
    view.entries = menu_entries(view.target); view.highlighted = menu_->highlighted;
    view.anchor_item = view.item;
    if (!view.anchor_item)
        if (const auto* control = find(view.target)) view.anchor_item = component_type(control->kind).menu_cursor(*this, *control);
    return view;
}
Result detail::UiState::validate_menu_items(const std::vector<MenuItem>& items) const {
    std::set<std::string> ids;
    for (const auto& item : items) {
        if (item.separator) continue;
        if (item.id.empty() || reserved_id(item.id) || !ids.insert(item.id).second)
            return {Status::invalid_value, u"選單項目識別碼必須唯一、不能為空，也不能以 lcars. 開頭"};
    }
    return {};
}
Query<ContextMenuHandle> detail::UiState::attach_menu(const Id& target, std::vector<MenuItem> items, MenuMode mode) {
    OperationScope operation(this);
    if (auto r = configuration_access(target); !r) return {r.status, r.message, std::nullopt};
    if (menu_attachments_.contains(target)) return {Status::invalid_value, u"元件已經有一份選單", std::nullopt};
    if (auto r = validate_menu_items(items); !r) return {r.status, r.message, std::nullopt};
    menu_attachments_.emplace(target, MenuAttachment{mode, std::move(items)});
    return {Status::ok, {}, ContextMenuHandle(this, lifetime_, target)};
}
void detail::UiState::menu_items_changed(const Id& target) {
    if (!menu_ || menu_->target != target) return;
    const auto* control = find(target);
    if (!control || !has_menu(*control)) { close_menu(); return; }
    auto entries = menu_entries(target);
    auto exists = [&](const std::string& id) {
        return !id.empty() && std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.id == id && !e.separator; });
    };
    if (!exists(menu_->highlighted)) menu_->highlighted.clear();
    if (!exists(menu_->pressed)) menu_->pressed.clear();
    if (invalidate_) invalidate_();
}

Result detail::UiState::open_menu(std::string_view id, std::optional<MenuPosition> at, std::optional<OptionValue> item,
                                  std::optional<unsigned> text_offset) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    const auto* control = find(id);
    if (!has_menu(*control)) return {Status::not_supported, u"元件沒有右鍵選單"};
    if (dispatching_) return {Status::busy, u"操作尚未結束"};
    if (!composing_.empty()) return {Status::busy, u"組字中，請先完成或取消組字"};
    // Opened from the keyboard, a list hands the application its current row without selecting it.
    const auto& type = component_type(control->kind);
    bool select_row = item.has_value();
    if (!item && !at) item = type.menu_cursor(*this, *control);
    if (item)
        if (auto r = type.check_menu_item(*this, *control, *item); !r) return r;
    const Id target(id);
    menu_.reset();
    close_popup();
    if (focusable(*control)) {
        if (at) pointer_pressed();  // #72: a pointer-opened menu focuses by pointer, so the focus indicator hides (the old pointer_focus_ flag)
        if (auto r = move_focus(target); !r) return r;
        refresh_tooltips();
        if (auto r = available(target); !r) return r;
    }
    if (text_offset) {  // a press on text outside the selection moves the caret there first
        if (const auto* session = edit(target)) {
            const bool inside = session->begin() != session->end() && *text_offset >= session->begin() && *text_offset <= session->end();
            if (!inside) select(target, *text_offset, *text_offset);
        }
    }
    if (item && select_row) {
        // The row is selected first; a list that cannot be written still hands the row to the application.
        type.menu_row_chosen(*this, target, *item);
        if (auto r = available(target); !r) return r;
    }
    OpenMenu menu;
    menu.target = target; menu.item = std::move(item); menu.at = at;
    if (type.has_builtin_menu() && clipboard_) menu.clipboard_text = bool(clipboard_->read());  // its paste item needs to know
    menu_ = std::move(menu);
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::menu_pointer_open(std::string_view id, std::optional<std::size_t> option, std::optional<unsigned> text_offset, MenuPosition at) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    if (!has_menu(*find(id))) return {Status::not_supported, u"元件沒有右鍵選單"};
    std::optional<OptionValue> item;
    if (option)
        if (auto row = component_type(find(id)->kind).menu_row(*this, *find(id), *option)) {
            if (!*row) return {row->status, row->message};
            item = row->value;
        }
    return open_menu(id, at, std::move(item), text_offset);
}
Result detail::UiState::close_menu() {
    OperationScope operation(this);
    if (!menu_) return {};
    semantic_instances_.erase(menu_node_id(menu_->target));
    menu_.reset();
    semantic_menu_rows_.clear();
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::menu_highlight(std::string_view item_id) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    auto entries = menu_entries(menu_->target);
    auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == item_id; });
    if (entry == entries.end()) return {Status::not_found, u"沒有這個選單項目"};
    if (entry->separator) return {Status::invalid_value, u"分隔線不能取得焦點"};
    if (menu_->highlighted != item_id) { menu_->highlighted = std::string(item_id); if (invalidate_) invalidate_(); }
    return {};
}
Result detail::UiState::menu_pointer_down(std::string_view item_id) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    menu_->pressed.clear();
    auto highlighted = menu_highlight(item_id);
    if (highlighted && menu_) menu_->pressed = std::string(item_id);
    return highlighted;
}
Result detail::UiState::menu_pointer_up(std::string_view item_id) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    auto pressed = std::exchange(menu_->pressed, {});
    if (pressed != item_id) return {};
    return menu_execute(item_id);
}
Result detail::UiState::menu_execute(std::string_view item_id) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    const Id target = menu_->target;
    if (auto r = available(target); !r) return r;
    auto entries = menu_entries(target);
    auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == item_id; });
    if (entry == entries.end()) return {Status::not_found, u"沒有這個選單項目"};
    if (entry->separator) return {Status::invalid_value, u"分隔線不能執行"};
    if (!entry->enabled) return {Status::disabled, u"此項目目前無法使用"};
    if (dispatching_) return {Status::busy, u"操作尚未結束"};
    const bool builtin = reserved_id(item_id);
    std::function<Result(const MenuContext&)> action;
    if (!builtin) {
        auto attachment = menu_attachments_.find(target);
        if (attachment == menu_attachments_.end()) return {Status::not_found, u"沒有這個選單項目"};
        auto it = std::find_if(attachment->second.items.begin(), attachment->second.items.end(), [&](const auto& i) { return i.id == item_id; });
        if (it == attachment->second.items.end()) return {Status::not_found, u"沒有這個選單項目"};
        action = it->action;
    }
    auto item = menu_->item;
    close_menu();  // the menu is gone before its command runs, so the command sees the restored focus
    if (builtin) return menu_run_builtin(target, item_id);
    if (!action) return {};
    try { return action(MenuContext{layout_handle(target), std::move(item)}); }
    catch (...) { stopped_ = true; return {Status::failed, u"選單項目執行失敗，介面已停止"}; }
}
Result detail::UiState::menu_run_builtin(const Id& target, std::string_view command) {
    const auto* control = find(target);
    if (!control) return {Status::not_found, u"沒有這個選單項目"};
    return component_type(control->kind).run_builtin_menu(*this, target, command);
}
Result detail::UiState::menu_key_down(KeyPress key) {
    OperationScope operation(this);
    if (!menu_) return {};
    // Escape, Alt and F10 close an open menu; the Apps key does not (a standard Win32 popup menu ignores it too).
    if (key.key == Key::escape || key.key == Key::alt || key.key == Key::f10) return close_menu();
    auto entries = menu_entries(menu_->target);
    std::vector<std::size_t> selectable;
    std::size_t current = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].separator) continue;
        if (entries[i].id == menu_->highlighted) current = selectable.size();
        selectable.push_back(i);
    }
    auto highlight = [&](std::size_t position) {
        menu_->highlighted = entries[selectable[position]].id;
        menu_->reveal = true;
        if (invalidate_) invalidate_();
        return Result{};
    };
    const auto count = selectable.size();
    switch (key.key) {
    case Key::down:
        if (!count) return {};
        return highlight(current >= count ? 0 : (current + 1) % count);
    case Key::up:
        if (!count) return {};
        return highlight(current >= count ? count - 1 : (current + count - 1) % count);
    case Key::home: return count ? highlight(0) : Result{};
    case Key::end: return count ? highlight(count - 1) : Result{};
    case Key::enter:
    case Key::space:
        if (key.repeat || menu_->highlighted.empty()) return {};
        return menu_execute(Id(menu_->highlighted));
    default: return {};  // the open menu owns the keyboard: shortcuts and editing keys do nothing
    }
}
Result detail::UiState::menu_text(std::u16string_view text) {
    OperationScope operation(this);
    for (auto ch : text) {
        if (!menu_) break;
        if (ch < 0x20 || ch == 0x7f) continue;
        auto entries = menu_entries(menu_->target);
        std::vector<std::size_t> matches;
        std::size_t current = entries.size();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].id == menu_->highlighted) current = i;
            if (!entries[i].separator && entries[i].access_key && fold(entries[i].access_key) == fold(ch)) matches.push_back(i);
        }
        if (matches.empty()) continue;
        if (matches.size() == 1) {
            auto id = entries[matches.front()].id;
            menu_->highlighted = id; menu_->reveal = true;
            if (!entries[matches.front()].enabled) { if (invalidate_) invalidate_(); continue; }  // reachable, not runnable
            return menu_execute(id);
        }
        auto next = std::find_if(matches.begin(), matches.end(), [&](std::size_t i) { return current >= entries.size() || i > current; });
        menu_->highlighted = entries[next == matches.end() ? matches.front() : *next].id;
        menu_->reveal = true;
        if (invalidate_) invalidate_();
    }
    return {};
}
Result detail::UiState::delete_selection(std::string_view id) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.begin() == field->session.end()) return {};
    return replace_selection(id, *field, u"");
}
Query<MenuInfo> detail::UiState::menu_info() const {
    if (!menu_) return {Status::hidden, u"選單沒有開啟", std::nullopt};
    if (auto r = configuration_access(menu_->target, true); !r) return {r.status, r.message, std::nullopt};
    MenuInfo info;
    info.target = const_cast<UiState*>(this)->layout_handle(menu_->target);
    info.item = menu_->item;
    for (auto& entry : menu_entries(menu_->target))
        info.items.push_back({entry.id, entry.label, entry.enabled, entry.separator, entry.id == menu_->highlighted, entry.access_key, entry.hint});
    return {Status::ok, {}, std::move(info)};
}

void detail::UiState::menu_frame(const Frame& frame) {
    semantic_menu_rows_.clear();
    const auto* panel = frame.menu_panel();
    if (!panel) return;
    auto bounds = intersect(panel->bounds, panel->clip);
    if (bounds.w > 0 && bounds.h > 0) semantic_bounds_[panel->id] = bounds;
    for (const auto& row : panel->menu_rows) {
        auto visible = intersect(row.bounds, panel->menu_inner);
        if (visible.w > 0 && visible.h > 0) semantic_menu_rows_[row.id] = visible;
    }
}
std::optional<Query<SemanticNode>> detail::UiState::menu_node(const NodeRef& ref) {
    if (!menu_) return std::nullopt;
    const auto menu_id = menu_node_id(menu_->target);
    if (ref.id != menu_id) return std::nullopt;
    SemanticNode node;
    node.ref = ref;
    auto [instance, inserted] = semantic_instances_.try_emplace(menu_id, 0);
    if (inserted) instance->second = ++next_semantic_instance_;
    node.instance = instance->second;
    if (!ref.item) {
        node.role = Role::menu; node.name = u"右鍵選單";
        if (auto bounds = semantic_bounds_.find(menu_id); bounds != semantic_bounds_.end()) node.bounds = bounds->second;
        node.visible = node.bounds.has_value();
        if (!renderer_failed_) node.verbs.push_back(Verb::cancel);
        return Query<SemanticNode>{Status::ok, {}, std::move(node)};
    }
    auto entries = menu_entries(menu_->target);
    auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == ref.item->id; });
    if (entry == entries.end()) return Query<SemanticNode>{Status::not_found, {}, std::nullopt};
    node.role = entry->separator ? Role::menu_separator : Role::menu_item;
    node.name = entry->label; node.accelerator_key = entry->hint;
    if (entry->access_key) node.access_key = std::u16string(1, entry->access_key);
    node.enabled = entry->enabled;
    node.focusable = !entry->separator;
    node.focused = !entry->separator && entry->id == menu_->highlighted;
    if (auto bounds = semantic_menu_rows_.find(entry->id); bounds != semantic_menu_rows_.end()) node.bounds = bounds->second;
    node.visible = node.bounds.has_value();
    if (!renderer_failed_ && !entry->separator) {
        node.verbs.push_back(Verb::focus);
        if (entry->enabled) node.verbs.push_back(Verb::invoke);
    }
    return Query<SemanticNode>{Status::ok, {}, std::move(node)};
}
std::optional<std::vector<NodeRef>> detail::UiState::menu_children(const NodeRef& parent) const {
    if (!menu_ || parent.item || parent.id != menu_node_id(menu_->target)) return std::nullopt;
    std::vector<NodeRef> children;
    for (const auto& entry : menu_entries(menu_->target)) children.push_back({parent.id, OptionValue{entry.id}});
    return children;
}

// ---- public API
Result ContextMenuHandle::set_mode(MenuMode mode) const {
    return detail::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists"};
        it->second.mode = mode;
        ui_->menu_items_changed(id_);
        return {};
    });
}
Result ContextMenuHandle::set_items(std::vector<MenuItem> items) const {
    return detail::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists"};
        if (auto r = ui_->validate_menu_items(items); !r) return r;
        it->second.items = std::move(items);
        ui_->menu_items_changed(id_);
        return {};
    });
}
Result ContextMenuHandle::add_item(MenuItem item, std::string before) const {
    return detail::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists"};
        auto items = it->second.items;
        auto position = items.end();
        if (!before.empty()) {
            position = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == before; });
            if (position == items.end()) return {Status::not_found, u"沒有這個選單項目"};
        }
        items.insert(position, std::move(item));
        if (auto r = ui_->validate_menu_items(items); !r) return r;
        it->second.items = std::move(items);
        ui_->menu_items_changed(id_);
        return {};
    });
}
Result ContextMenuHandle::remove_item(std::string id) const {
    return detail::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists"};
        auto& items = it->second.items;
        auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == id; });
        if (found == items.end()) return {Status::not_found, u"沒有這個選單項目"};
        items.erase(found);
        ui_->menu_items_changed(id_);
        return {};
    });
}
Result ContextMenuHandle::update_item(MenuItem item) const {
    return detail::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists"};
        if (item.separator) return {Status::invalid_value, u"分隔線沒有識別碼，不能更新"};
        auto& items = it->second.items;
        auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == item.id; });
        if (found == items.end()) return {Status::not_found, u"沒有這個選單項目"};
        *found = std::move(item);
        ui_->menu_items_changed(id_);
        return {};
    });
}
Result ContextMenuHandle::enable_item(std::string id, bool enabled) const {
    return detail::operate(*this, [&]() -> Result {
        if (auto r = ui_->configuration_access(id_); !r) return r;
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists"};
        auto& items = it->second.items;
        auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == id; });
        if (found == items.end()) return {Status::not_found, u"沒有這個選單項目"};
        found->enabled = enabled;
        ui_->menu_items_changed(id_);
        return {};
    });
}
Query<std::vector<MenuItemInfo>> ContextMenuHandle::items() const {
    return detail::operate(*this, [&]() -> Query<std::vector<MenuItemInfo>> {
        if (auto r = ui_->configuration_access(id_, true); !r) return {r.status, r.message, std::nullopt};
        auto it = ui_->menu_attachments_.find(id_);
        if (it == ui_->menu_attachments_.end()) return {Status::not_found, u"Menu no longer exists", std::nullopt};
        std::vector<MenuItemInfo> items;
        for (const auto& item : it->second.items)
            items.push_back({item.id, item.label, item.enabled, item.separator, false, item.access_key, item.shortcut_hint});
        return {Status::ok, {}, std::move(items)};
    });
}

Query<ContextMenuHandle> Ui::add_context_menu(ComponentHandle target, std::vector<MenuItem> items, MenuMode mode) {
    detail::UiState::OperationScope operation(state_.get());
    auto id = state_->layout_id(target);
    if (!id) return {Status::not_found, u"Component no longer exists", std::nullopt};
    return state_->attach_menu(*id, std::move(items), mode);
}
Result Ui::open_context_menu(ComponentHandle target, std::optional<MenuPosition> at, std::optional<OptionValue> item) {
    detail::UiState::OperationScope operation(state_.get());
    auto id = state_->layout_id(target);
    if (!id) return {Status::not_found, u"Component no longer exists"};
    return state_->open_menu(*id, at, std::move(item));
}
Result Ui::close_context_menu() {
    detail::UiState::OperationScope operation(state_.get());
    if (auto r = state_->creation_access(); !r && r.status != Status::renderer_failed) return r;
    return state_->close_menu();
}
Query<bool> detail::UiState::menu_open_query() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrong_thread, u"Operation requires the UI thread", std::nullopt};
    if (stopped_) return {Status::failed, u"UI is stopped", std::nullopt};
    return {Status::ok, {}, menu_.has_value()};
}
Query<bool> Ui::context_menu_open() const { return state_->menu_open_query(); }
Query<MenuInfo> Ui::context_menu() const { return state_->menu_info(); }
Result Ui::invoke_menu_item(std::string id) {
    detail::UiState::OperationScope operation(state_.get());
    if (auto r = state_->creation_access(); !r) return r;
    return state_->menu_execute(id);
}
}
