#include "core_internal.hpp"
#include "component_type.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

// Context menu: one window-wide menu opened beside a component. It never moves core focus; the
// component keeps it, so "focus returns to the component" is a property of closing, not an action.
namespace isoduo {
namespace {
char16_t fold(char16_t ch) { return ch >= u'a' && ch <= u'z' ? static_cast<char16_t>(ch - u'a' + u'A') : ch; }
bool reservedId(std::string_view id) { return id.starts_with("isoduo."); }
}

bool detail::UiState::hasMenu(const Component& control) const {
    const bool builtin = componentType(control.kind).hasBuiltinMenu();
    auto attachment = menuAttachments_.find(control.id);
    if (attachment == menuAttachments_.end()) return builtin;
    switch (attachment->second.mode) {
    case MenuMode::off: return false;
    case MenuMode::replace: return !attachment->second.items.empty();
    case MenuMode::append: return builtin || !attachment->second.items.empty();
    }
    return false;
}
std::vector<detail::UiState::MenuEntry> detail::UiState::menuEntries(const Id& target) const {
    std::vector<MenuEntry> raw;
    const auto* control = find(target);
    if (!control) return raw;
    auto attachment = menuAttachments_.find(target);
    const auto mode = attachment == menuAttachments_.end() ? MenuMode::append : attachment->second.mode;
    if (mode == MenuMode::off) return raw;
    auto separator = [&] { MenuEntry entry; entry.separator = true; raw.push_back(std::move(entry)); };
    if (mode == MenuMode::append && componentType(control->kind).hasBuiltinMenu()) {
        const bool clipboardText = menu_ && menu_->target == target && menu_->clipboardText;
        componentType(control->kind).builtinMenuEntries(*this, *control, clipboardText, raw);
    }
    if (attachment != menuAttachments_.end()) {
        if (!raw.empty() && !attachment->second.items.empty()) separator();
        for (const auto& item : attachment->second.items) {
            if (item.separator) { separator(); continue; }
            raw.push_back({item.id, item.label, item.shortcutHint, item.enabled, false, item.accessKey});
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
std::optional<detail::UiState::MenuView> detail::UiState::menuView() const {
    if (!menu_) return std::nullopt;
    MenuView view;
    view.target = menu_->target; view.item = menu_->item; view.at = menu_->at;
    view.entries = menuEntries(view.target); view.highlighted = menu_->highlighted;
    view.anchorItem = view.item;
    if (!view.anchorItem)
        if (const auto* control = find(view.target)) view.anchorItem = componentType(control->kind).menuCursor(*this, *control);
    return view;
}
Result detail::UiState::validateMenuItems(const std::vector<MenuItem>& items) const {
    std::set<std::string> ids;
    for (const auto& item : items) {
        if (item.separator) continue;
        if (item.id.empty() || reservedId(item.id) || !ids.insert(item.id).second)
            return {Status::invalidValue, u"選單項目識別碼必須唯一、不能為空，也不能以 isoduo. 開頭"};
    }
    return {};
}
Query<ContextMenuHandle> detail::UiState::attachMenu(const Id& target, std::vector<MenuItem> items, MenuMode mode) {
    OperationScope operation(this);
    if (auto r = configurationAccess(target); !r) return {r.status, r.message, std::nullopt};
    if (menuAttachments_.contains(target)) return {Status::invalidValue, u"元件已經有一份選單", std::nullopt};
    if (auto r = validateMenuItems(items); !r) return {r.status, r.message, std::nullopt};
    menuAttachments_.emplace(target, MenuAttachment{mode, std::move(items)});
    return {Status::ok, {}, ContextMenuHandle(this, lifetime_, target)};
}
void detail::UiState::menuItemsChanged(const Id& target) {
    if (!menu_ || menu_->target != target) return;
    const auto* control = find(target);
    if (!control || !hasMenu(*control)) { closeMenu(); return; }
    auto entries = menuEntries(target);
    auto exists = [&](const std::string& id) {
        return !id.empty() && std::any_of(entries.begin(), entries.end(), [&](const auto& e) { return e.id == id && !e.separator; });
    };
    if (!exists(menu_->highlighted)) menu_->highlighted.clear();
    if (!exists(menu_->pressed)) menu_->pressed.clear();
    if (invalidate_) invalidate_();
}

Result detail::UiState::openMenu(std::string_view id, std::optional<MenuPosition> at, std::optional<OptionValue> item,
                                  std::optional<unsigned> textOffset) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    const auto* control = find(id);
    if (!hasMenu(*control)) return {Status::notSupported, u"元件沒有右鍵選單"};
    if (dispatching_) return {Status::busy, u"操作尚未結束"};
    if (!composing_.empty()) return {Status::busy, u"組字中，請先完成或取消組字"};
    // Opened from the keyboard, a list hands the application its current row without selecting it.
    const auto& type = componentType(control->kind);
    bool selectRow = item.has_value();
    if (!item && !at) item = type.menuCursor(*this, *control);
    if (item)
        if (auto r = type.checkMenuItem(*this, *control, *item); !r) return r;
    const Id target(id);
    menu_.reset();
    closePopup();
    if (focusable(*control)) {
        if (at) pointerPressed();  // #72: a pointer-opened menu focuses by pointer, so the focus indicator hides (the old pointer_focus_ flag)
        if (auto r = moveFocus(target); !r) return r;
        refreshTooltips();
        if (auto r = available(target); !r) return r;
    }
    if (textOffset) {  // a press on text outside the selection moves the caret there first
        if (const auto* session = edit(target)) {
            const bool inside = session->begin() != session->end() && *textOffset >= session->begin() && *textOffset <= session->end();
            if (!inside) select(target, *textOffset, *textOffset);
        }
    }
    if (item && selectRow) {
        // The row is selected first; a list that cannot be written still hands the row to the application.
        type.menuRowChosen(*this, target, *item);
        if (auto r = available(target); !r) return r;
    }
    OpenMenu menu;
    menu.target = target; menu.item = std::move(item); menu.at = at;
    if (type.hasBuiltinMenu() && clipboard_) menu.clipboardText = bool(clipboard_->read());  // its paste item needs to know
    menu_ = std::move(menu);
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::menuPointerOpen(std::string_view id, std::optional<std::size_t> option, std::optional<unsigned> textOffset, MenuPosition at) {
    OperationScope operation(this);
    if (auto r = available(id); !r) return r;
    if (!hasMenu(*find(id))) return {Status::notSupported, u"元件沒有右鍵選單"};
    std::optional<OptionValue> item;
    if (option)
        if (auto row = componentType(find(id)->kind).menuRow(*this, *find(id), *option)) {
            if (!*row) return {row->status, row->message};
            item = row->value;
        }
    return openMenu(id, at, std::move(item), textOffset);
}
Result detail::UiState::closeMenu() {
    OperationScope operation(this);
    if (!menu_) return {};
    semanticInstances_.erase(menuNodeId(menu_->target));
    menu_.reset();
    semanticMenuRows_.clear();
    if (invalidate_) invalidate_();
    return {};
}
Result detail::UiState::menuHighlight(std::string_view itemId) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    auto entries = menuEntries(menu_->target);
    auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == itemId; });
    if (entry == entries.end()) return {Status::notFound, u"沒有這個選單項目"};
    if (entry->separator) return {Status::invalidValue, u"分隔線不能取得焦點"};
    if (menu_->highlighted != itemId) { menu_->highlighted = std::string(itemId); if (invalidate_) invalidate_(); }
    return {};
}
Result detail::UiState::menuPointerDown(std::string_view itemId) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    menu_->pressed.clear();
    auto highlighted = menuHighlight(itemId);
    if (highlighted && menu_) menu_->pressed = std::string(itemId);
    return highlighted;
}
Result detail::UiState::menuPointerUp(std::string_view itemId) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    auto pressed = std::exchange(menu_->pressed, {});
    if (pressed != itemId) return {};
    return menuExecute(itemId);
}
Result detail::UiState::menuExecute(std::string_view itemId) {
    OperationScope operation(this);
    if (!menu_) return {Status::hidden, u"選單沒有開啟"};
    const Id target = menu_->target;
    if (auto r = available(target); !r) return r;
    auto entries = menuEntries(target);
    auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == itemId; });
    if (entry == entries.end()) return {Status::notFound, u"沒有這個選單項目"};
    if (entry->separator) return {Status::invalidValue, u"分隔線不能執行"};
    if (!entry->enabled) return {Status::disabled, u"此項目目前無法使用"};
    if (dispatching_) return {Status::busy, u"操作尚未結束"};
    const bool builtin = reservedId(itemId);
    std::function<Result(const MenuContext&)> action;
    if (!builtin) {
        auto attachment = menuAttachments_.find(target);
        if (attachment == menuAttachments_.end()) return {Status::notFound, u"沒有這個選單項目"};
        auto it = std::find_if(attachment->second.items.begin(), attachment->second.items.end(), [&](const auto& i) { return i.id == itemId; });
        if (it == attachment->second.items.end()) return {Status::notFound, u"沒有這個選單項目"};
        action = it->action;
    }
    auto item = menu_->item;
    closeMenu();  // the menu is gone before its command runs, so the command sees the restored focus
    if (builtin) return menuRunBuiltin(target, itemId);
    if (!action) return {};
    try { return action(MenuContext{layoutHandle(target), std::move(item)}); }
    catch (...) { stopped_ = true; return {Status::failed, u"選單項目執行失敗，介面已停止"}; }
}
Result detail::UiState::menuRunBuiltin(const Id& target, std::string_view command) {
    const auto* control = find(target);
    if (!control) return {Status::notFound, u"沒有這個選單項目"};
    return componentType(control->kind).runBuiltinMenu(*this, target, command);
}
Result detail::UiState::menuKeyDown(KeyPress key) {
    OperationScope operation(this);
    if (!menu_) return {};
    // Escape, Alt and F10 close an open menu; the Apps key does not (a standard Win32 popup menu ignores it too).
    if (key.key == Key::escape || key.key == Key::alt || key.key == Key::f10) return closeMenu();
    auto entries = menuEntries(menu_->target);
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
        return menuExecute(Id(menu_->highlighted));
    default: return {};  // the open menu owns the keyboard: shortcuts and editing keys do nothing
    }
}
Result detail::UiState::menuText(std::u16string_view text) {
    OperationScope operation(this);
    for (auto ch : text) {
        if (!menu_) break;
        if (ch < 0x20 || ch == 0x7f) continue;
        auto entries = menuEntries(menu_->target);
        std::vector<std::size_t> matches;
        std::size_t current = entries.size();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].id == menu_->highlighted) current = i;
            if (!entries[i].separator && entries[i].accessKey && fold(entries[i].accessKey) == fold(ch)) matches.push_back(i);
        }
        if (matches.empty()) continue;
        if (matches.size() == 1) {
            auto id = entries[matches.front()].id;
            menu_->highlighted = id; menu_->reveal = true;
            if (!entries[matches.front()].enabled) { if (invalidate_) invalidate_(); continue; }  // reachable, not runnable
            return menuExecute(id);
        }
        auto next = std::find_if(matches.begin(), matches.end(), [&](std::size_t i) { return current >= entries.size() || i > current; });
        menu_->highlighted = entries[next == matches.end() ? matches.front() : *next].id;
        menu_->reveal = true;
        if (invalidate_) invalidate_();
    }
    return {};
}
Result detail::UiState::deleteSelection(std::string_view id) {
    OperationScope operation(this);
    Field* field = nullptr;
    if (auto result = writable(id, field); !result) return result;
    if (field->session.begin() == field->session.end()) return {};
    return replaceSelection(id, *field, u"");
}
Query<MenuSnapshot> detail::UiState::menuInfo() const {
    if (!menu_) return {Status::hidden, u"選單沒有開啟", std::nullopt};
    if (auto r = configurationAccess(menu_->target, true); !r) return {r.status, r.message, std::nullopt};
    MenuSnapshot info;
    info.target = const_cast<UiState*>(this)->layoutHandle(menu_->target);
    info.item = menu_->item;
    for (auto& entry : menuEntries(menu_->target))
        info.items.push_back({entry.id, entry.label, entry.enabled, entry.separator, entry.id == menu_->highlighted, entry.accessKey, entry.hint});
    return {Status::ok, {}, std::move(info)};
}

void detail::UiState::menuFrame(const Frame& frame) {
    semanticMenuRows_.clear();
    const auto* panel = frame.menuPanel();
    if (!panel) return;
    auto bounds = intersect(panel->bounds, panel->clip);
    if (bounds.w > 0 && bounds.h > 0) semanticBounds_[panel->id] = bounds;
    for (const auto& row : panel->menuRows) {
        auto visible = intersect(row.bounds, panel->menuInner);
        if (visible.w > 0 && visible.h > 0) semanticMenuRows_[row.id] = visible;
    }
}
std::optional<Query<NodeSnapshot>> detail::UiState::menuNode(const NodeRef& ref) {
    if (!menu_) return std::nullopt;
    const auto menuId = menuNodeId(menu_->target);
    if (ref.id != menuId) return std::nullopt;
    NodeSnapshot node;
    node.ref = ref;
    auto [instance, inserted] = semanticInstances_.try_emplace(menuId, 0);
    if (inserted) instance->second = ++nextSemanticInstance_;
    node.instance = instance->second;
    if (!ref.item) {
        node.role = Role::menu; node.name = u"右鍵選單";
        if (auto bounds = semanticBounds_.find(menuId); bounds != semanticBounds_.end()) node.bounds = bounds->second;
        node.visible = node.bounds.has_value();
        if (!rendererFailed_) node.verbs.push_back(Verb::cancel);
        return Query<NodeSnapshot>{Status::ok, {}, std::move(node)};
    }
    auto entries = menuEntries(menu_->target);
    auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.id == ref.item->id; });
    if (entry == entries.end()) return Query<NodeSnapshot>{Status::notFound, {}, std::nullopt};
    node.role = entry->separator ? Role::menuSeparator : Role::menuItem;
    node.name = entry->label; node.acceleratorKey = entry->hint;
    if (entry->accessKey) node.accessKey = std::u16string(1, entry->accessKey);
    node.enabled = entry->enabled;
    node.focusable = !entry->separator;
    node.focused = !entry->separator && entry->id == menu_->highlighted;
    if (auto bounds = semanticMenuRows_.find(entry->id); bounds != semanticMenuRows_.end()) node.bounds = bounds->second;
    node.visible = node.bounds.has_value();
    if (!rendererFailed_ && !entry->separator) {
        node.verbs.push_back(Verb::focus);
        if (entry->enabled) node.verbs.push_back(Verb::invoke);
    }
    return Query<NodeSnapshot>{Status::ok, {}, std::move(node)};
}
std::optional<std::vector<NodeRef>> detail::UiState::menuChildren(const NodeRef& parent) const {
    if (!menu_ || parent.item || parent.id != menuNodeId(menu_->target)) return std::nullopt;
    std::vector<NodeRef> children;
    for (const auto& entry : menuEntries(menu_->target)) children.push_back({parent.id, OptionValue{entry.id}});
    return children;
}

Result detail::UiState::menuAttachment(std::string_view target, MenuAttachment*& attachment, bool readOnly) {
    if (auto r = configurationAccess(target, readOnly); !r) return r;
    auto it = menuAttachments_.find(target);
    if (it == menuAttachments_.end()) return {Status::notFound, u"Menu no longer exists"};
    attachment = &it->second;
    return {};
}

// ---- public API
Result ContextMenuHandle::setMode(MenuMode mode) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu); !r) return r;
        menu->mode = mode;
        ui_->menuItemsChanged(id_);
        return {};
    });
}
Result ContextMenuHandle::setItems(std::vector<MenuItem> items) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu); !r) return r;
        if (auto r = ui_->validateMenuItems(items); !r) return r;
        menu->items = std::move(items);
        ui_->menuItemsChanged(id_);
        return {};
    });
}
Result ContextMenuHandle::addItem(MenuItem item, std::string before) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu); !r) return r;
        auto items = menu->items;
        auto position = items.end();
        if (!before.empty()) {
            position = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == before; });
            if (position == items.end()) return {Status::notFound, u"沒有這個選單項目"};
        }
        items.insert(position, std::move(item));
        if (auto r = ui_->validateMenuItems(items); !r) return r;
        menu->items = std::move(items);
        ui_->menuItemsChanged(id_);
        return {};
    });
}
Result ContextMenuHandle::removeItem(std::string id) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu); !r) return r;
        auto& items = menu->items;
        auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == id; });
        if (found == items.end()) return {Status::notFound, u"沒有這個選單項目"};
        items.erase(found);
        ui_->menuItemsChanged(id_);
        return {};
    });
}
Result ContextMenuHandle::updateItem(MenuItem item) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu); !r) return r;
        if (item.separator) return {Status::invalidValue, u"分隔線沒有識別碼，不能更新"};
        auto& items = menu->items;
        auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == item.id; });
        if (found == items.end()) return {Status::notFound, u"沒有這個選單項目"};
        *found = std::move(item);
        ui_->menuItemsChanged(id_);
        return {};
    });
}
Result ContextMenuHandle::enableItem(std::string id, bool enabled) const {
    return detail::UiState::operate(*this, [&]() -> Result {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu); !r) return r;
        auto& items = menu->items;
        auto found = std::find_if(items.begin(), items.end(), [&](const auto& i) { return !i.separator && i.id == id; });
        if (found == items.end()) return {Status::notFound, u"沒有這個選單項目"};
        found->enabled = enabled;
        ui_->menuItemsChanged(id_);
        return {};
    });
}
Query<std::vector<MenuItemSnapshot>> ContextMenuHandle::items() const {
    return detail::UiState::operate(*this, [&]() -> Query<std::vector<MenuItemSnapshot>> {
        detail::UiState::MenuAttachment* menu;
        if (auto r = ui_->menuAttachment(id_, menu, true); !r) return {r.status, r.message, std::nullopt};
        std::vector<MenuItemSnapshot> items;
        for (const auto& item : menu->items)
            items.push_back({item.id, item.label, item.enabled, item.separator, false, item.accessKey, item.shortcutHint});
        return {Status::ok, {}, std::move(items)};
    });
}

Query<ContextMenuHandle> Ui::addContextMenu(ComponentHandle target, std::vector<MenuItem> items, MenuMode mode) {
    detail::UiState::OperationScope operation(state_.get());
    auto id = state_->layoutId(target);
    if (!id) return {Status::notFound, u"Component no longer exists", std::nullopt};
    return state_->attachMenu(*id, std::move(items), mode);
}
Result Ui::openContextMenu(ComponentHandle target, std::optional<MenuPosition> at, std::optional<OptionValue> item) {
    detail::UiState::OperationScope operation(state_.get());
    auto id = state_->layoutId(target);
    if (!id) return {Status::notFound, u"Component no longer exists"};
    return state_->openMenu(*id, at, std::move(item));
}
Result Ui::closeContextMenu() {
    detail::UiState::OperationScope operation(state_.get());
    if (auto r = state_->creationAccess(); !r && r.status != Status::rendererFailed) return r;
    return state_->closeMenu();
}
Query<bool> detail::UiState::menuOpenQuery() const {
    if (std::this_thread::get_id() != owner_) return {Status::wrongThread, u"Operation requires the UI thread", std::nullopt};
    if (stopped_) return {Status::failed, u"UI is stopped", std::nullopt};
    return {Status::ok, {}, menu_.has_value()};
}
Query<bool> Ui::contextMenuOpen() const { return state_->menuOpenQuery(); }
Query<MenuSnapshot> Ui::contextMenu() const { return state_->menuInfo(); }
Result Ui::invokeMenuItem(std::string id) {
    detail::UiState::OperationScope operation(state_.get());
    if (auto r = state_->creationAccess(); !r) return r;
    return state_->menuExecute(id);
}
}
