#include "linux_accessibility.hpp"
#include "linux_accessibility_protocol.hpp"
#include "linux_window.hpp"
#include "core_internal.hpp"
#include "utf.hpp"
#include "word_break.hpp"
#include <gio/gio.h>
#include <pango/pango.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>
#include <clocale>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace isoduo::detail {
namespace {
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
using Bus = std::unique_ptr<GDBusConnection, decltype(&g_object_unref)>;
using Schema = std::unique_ptr<GDBusNodeInfo, decltype(&g_dbus_node_info_unref)>;
constexpr unsigned app = 0, window = 1;
constexpr auto objectEvents = "org.a11y.atspi.Event.Object";
constexpr auto windowEvents = "org.a11y.atspi.Event.Window";

std::string justification(TextAlign align, bool asLeft) {
    switch (align) {
    case TextAlign::left: return "left";
    case TextAlign::center: return "center";
    case TextAlign::right: return "right";
    case TextAlign::justify: case TextAlign::distribute: return asLeft ? "left" : "fill";
    }
    return "left";
}

struct ContextScope {
    GMainContext* context;
    explicit ContextScope(GMainContext* c) : context(c) { g_main_context_push_thread_default(context); }
    ~ContextScope() { g_main_context_pop_thread_default(context); }
};

Bus connectAccessibilityBus() {
    Bus session(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_object_unref);
    if (!session) return {nullptr, g_object_unref};
    Variant address(g_dbus_connection_call_sync(session.get(), "org.a11y.Bus", "/org/a11y/bus",
        "org.a11y.Bus", "GetAddress", nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE,
        500, nullptr, nullptr), g_variant_unref);
    if (!address) return {nullptr, g_object_unref};
    const char* value;
    g_variant_get(address.get(), "(&s)", &value);
    return {g_dbus_connection_new_for_address_sync(value,
        GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, nullptr), g_object_unref};
}

// D-Bus strings must be UTF-8 and cannot contain embedded NUL.
std::string wireString(std::string_view value) {
    std::string result;
    while (!value.empty()) {
        auto count = value.find('\0');
        auto* valid = g_utf8_make_valid(value.data(), count == value.npos ? value.size() : count);
        result += valid;
        g_free(valid);
        if (count == value.npos) break;
        result += "\xef\xbf\xbd";
        value.remove_prefix(count + 1);
    }
    return result;
}
std::string wireText(std::u16string_view value) { return wireString(utf8FromUtf16(value)); }

struct RoleInfo { atspi::Role code; const char* name; };
RoleInfo roleInfo(Role role) {
    switch (role) {
    case Role::textField: return {atspi::entry, "entry"};
    case Role::button: return {atspi::button, "push button"};
    case Role::label: return {atspi::label, "label"};
    case Role::checkbox: return {atspi::checkBox, "check box"};
    case Role::radioGroup: return {atspi::panel, "panel"};
    case Role::radioOption: return {atspi::radioButton, "radio button"};
    case Role::slider: return {atspi::slider, "slider"};
    case Role::dropdown: return {atspi::comboBox, "combo box"};
    case Role::dropdownOption: case Role::listItem: return {atspi::listItem, "list item"};
    case Role::list: return {atspi::list, "list"};
    case Role::tabs: return {atspi::pageTabList, "page tab list"};
    case Role::tabItem: return {atspi::pageTab, "page tab"};
    case Role::progress: return {atspi::progressBar, "progress bar"};
    case Role::dialog: return {atspi::dialog, "dialog"};
    case Role::tooltip: return {atspi::toolTip, "tool tip"};
    case Role::menu: return {atspi::popupMenu, "popup menu"};
    case Role::menuItem: return {atspi::menuItem, "menu item"};
    case Role::menuSeparator: return {atspi::separator, "separator"};
    }
    return {atspi::panel, "panel"};
}
bool selectionContainer(Role role) {
    return role == Role::radioGroup || role == Role::dropdown || role == Role::list || role == Role::tabs;
}
struct Action { Verb verb; const char* name; };
std::vector<Action> primaryActions(const NodeSnapshot& node) {
    switch (node.role) {
    case Role::button: case Role::menuItem: return {{Verb::invoke, "click"}};
    case Role::checkbox: return {{Verb::toggle, "toggle"}};
    case Role::radioOption: case Role::dropdownOption: case Role::listItem: case Role::tabItem:
        return {{Verb::select, "select"}};
    case Role::dropdown: return {{Verb::open, "expand"}, {Verb::cancel, "collapse"}};
    case Role::dialog: case Role::menu: return {{Verb::cancel, "dismiss"}};
    default: return {};
    }
}
bool hasVerb(const NodeSnapshot& node, Verb verb) {
    return std::ranges::find(node.verbs, verb) != node.verbs.end();
}
std::vector<Action> actions(const NodeSnapshot& node) {
    auto result = primaryActions(node);
    std::erase_if(result, [&](const Action& action) { return !hasVerb(node, action.verb); });
    if (hasVerb(node, Verb::contextMenu)) result.push_back({Verb::contextMenu, "show-menu"});
    return result;
}
int integer(GVariant* tuple, unsigned index = 0) {
    int value;
    g_variant_get_child(tuple, index, "i", &value);
    return value;
}
const char* string(GVariant* tuple, unsigned index = 0) {
    const char* value;
    g_variant_get_child(tuple, index, "&s", &value);
    return value;
}
unsigned natural(GVariant* tuple, unsigned index) {
    unsigned value;
    g_variant_get_child(tuple, index, "u", &value);
    return value;
}
GVariant* emptyArray(const char* elementType) {
    return g_variant_new_array(G_VARIANT_TYPE(elementType), nullptr, 0);
}

struct NodeKey {
    NodeRef ref;
    std::uint64_t instance;
    bool operator<(const NodeKey& rhs) const {
        auto key = [](const NodeKey& node) {
            return std::tuple{std::string_view(node.ref.id), node.ref.item.has_value(),
                node.ref.item ? std::string_view(node.ref.item->id) : std::string_view{}, node.instance};
        };
        return key(*this) < key(rhs);
    }
};
}

struct LinuxAccessibility::Impl {
    Ui& ui;
    LinuxWindow& native;
    const Frame& frame;
    const bool& closing;
    Bus bus{nullptr, g_object_unref};
    Schema schema{nullptr, g_dbus_node_info_unref};
    guint subtree = 0, cache = 0;
    Subscription subscription;
    std::string title;
    int applicationId = 0;
    // Paths are never reused on this connection, including after a removed component is recreated.
    unsigned nextId = 2;
    std::map<NodeKey, unsigned> ids;
    std::map<unsigned, NodeKey> nodes;
    std::map<unsigned, std::vector<unsigned>> tree;
    std::map<unsigned, unsigned> parents;
    std::map<unsigned, std::u16string> priorText;
    struct GeometryState {
        std::optional<Rect> bounds;
        bool focusable = false;
        bool operator==(const GeometryState&) const = default;
    };
    std::map<unsigned, GeometryState> priorGeometry;
    std::optional<unsigned> focused;
    bool layoutPending = false, registered = false;

    Impl(GMainContext* context, Ui& ui, LinuxWindow& native, const Frame& frame, std::u16string_view title, const bool& closing)
        : ui(ui), native(native), frame(frame), closing(closing), title(wireText(title)) {
        ContextScope current(context);
        bus = connectAccessibilityBus();
        if (!bus) return;
        schema.reset(g_dbus_node_info_new_for_xml(atspi::interfaces, nullptr));
        static const GDBusSubtreeVTable dispatchTree{enumerate, introspect, dispatch, {}};
        subtree = g_dbus_connection_register_subtree(bus.get(), atspi::objectBase, &dispatchTree,
            G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES, this, nullptr, nullptr);
        cache = g_dbus_connection_register_object(bus.get(), atspi::cachePath, info("Cache"), &vtable(),
            this, nullptr, nullptr);
        if (!subtree || !cache) return;
        refreshTree(false);
        subscription = ui.semantics().subscribe([this](const SemanticEvent& event) { observe(event); });
        // Registry discovery may set Application.Id on this same connection.
        g_dbus_connection_call(bus.get(), atspi::registry, atspi::applicationPath, "org.a11y.atspi.Socket",
            "Embed", g_variant_new("(@(so))", reference(app)), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000,
            nullptr, nullptr, nullptr);
    }

    ~Impl() {
        subscription.reset();
        if (!bus) return;
        if (registered) {
            signal(window, windowEvents, "Destroy");
            g_dbus_connection_flush_sync(bus.get(), nullptr, nullptr);
        }
        if (subtree) g_dbus_connection_unregister_subtree(bus.get(), subtree);
        if (cache) g_dbus_connection_unregister_object(bus.get(), cache);
        // The registry removes the application when its private connection closes.
        g_dbus_connection_close(bus.get(), nullptr, nullptr, nullptr);
    }

    static std::string path(unsigned id) {
        return id == app ? atspi::applicationPath : std::string(atspi::objectBase) + "/n" + std::to_string(id);
    }
    static std::optional<unsigned> idOf(std::string_view child) {
        if (child == "root") return app;
        if (!child.starts_with('n')) return {};
        unsigned id;
        const auto end = child.data() + child.size();
        const auto result = std::from_chars(child.data() + 1, end, id);
        if (result.ec != std::errc{} || result.ptr != end || id < window ||
            child != "n" + std::to_string(id)) return {};
        return id;
    }
    unsigned idOf(const NodeSnapshot& node) {
        NodeKey key{node.ref, node.instance};
        const auto [where, inserted] = ids.try_emplace(key, nextId);
        if (inserted) nodes.emplace(nextId++, std::move(key));
        return where->second;
    }
    Query<NodeSnapshot> snapshot(unsigned id) {
        auto entry = nodes.find(id);
        if (entry == nodes.end()) return {Status::notFound, {}, {}};
        auto node = ui.semantics().get(entry->second.ref);
        if (node && node->instance == entry->second.instance) return node;
        return {Status::notFound, {}, {}};
    }
    GVariant* reference(std::optional<unsigned> id) const {
        return id ? g_variant_new("(so)", g_dbus_connection_get_unique_name(bus.get()), path(*id).c_str())
                  : g_variant_new("(so)", "", atspi::nullPath);
    }
    static std::optional<unsigned> at(const std::vector<unsigned>& items, int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return {};
        return items[index];
    }
    const std::vector<unsigned>& children(unsigned id) const {
        static const std::vector<unsigned> empty;
        const auto it = tree.find(id);
        return it == tree.end() ? empty : it->second;
    }
    std::optional<unsigned> parent(unsigned id) const {
        const auto it = parents.find(id);
        return it == parents.end() ? std::nullopt : std::optional{it->second};
    }
    std::vector<unsigned> selected(unsigned id) {
        std::vector<unsigned> result;
        for (auto child : children(id)) if (auto n = snapshot(child); n && n->selected) result.push_back(child);
        return result;
    }
    GDBusInterfaceInfo* info(std::string_view name) const {
        return g_dbus_node_info_lookup_interface(schema.get(), (atspi::prefix + std::string(name)).c_str());
    }
    std::vector<const char*> interfacesFor(unsigned id, const NodeSnapshot* node) {
        std::vector<const char*> result{"Accessible"};
        if (id == app) result.push_back("Application");
        if (id == window || node) result.push_back("Component");
        if (node) {
            // Keep the role's interface stable when its actions become unavailable.
            if (!primaryActions(*node).empty() || hasVerb(*node, Verb::contextMenu)) result.push_back("Action");
            if (selectionContainer(node->role)) result.push_back("Selection");
            if (node->range || node->role == Role::progress) result.push_back("Value");
            if (node->role == Role::textField) {
                result.push_back("Text");
                result.push_back("EditableText");
            }
        }
        return result;
    }
    static gchar** enumerate(GDBusConnection*, const gchar*, const gchar*, gpointer data) {
        auto& self = *static_cast<Impl*>(data);
        auto names = g_new0(gchar*, self.nodes.size() + 3);
        names[0] = g_strdup("root");
        names[1] = g_strdup("n1");
        unsigned count = 2;
        for (const auto& [id, key] : self.nodes)
            if (self.parents.contains(id)) names[count++] = g_strdup(("n" + std::to_string(id)).c_str());
        return names;
    }
    static GDBusInterfaceInfo** introspect(GDBusConnection*, const gchar*, const gchar*,
                                           const gchar* child, gpointer data) {
        auto& self = *static_cast<Impl*>(data);
        auto id = child ? idOf(child) : std::nullopt;
        if (!id || (*id > window && !self.nodes.contains(*id))) return nullptr;
        auto node = self.snapshot(*id);
        auto interfaces = self.interfacesFor(*id, node ? &*node : nullptr);
        auto out = g_new0(GDBusInterfaceInfo*, interfaces.size() + 1);
        for (std::size_t i = 0; i < interfaces.size(); ++i) out[i] = g_dbus_interface_info_ref(self.info(interfaces[i]));
        return out;
    }
    static const GDBusInterfaceVTable& vtable() {
        static const GDBusInterfaceVTable callbacks{call, getProperty, setProperty, {}};
        return callbacks;
    }
    static const GDBusInterfaceVTable* dispatch(GDBusConnection*, const gchar*, const gchar*,
        const gchar*, const gchar*, gpointer* out, gpointer data) {
        *out = data;
        return &vtable();
    }

    static void call(GDBusConnection*, const gchar*, const gchar* path, const gchar* iface, const gchar* method,
                     GVariant* args, GDBusMethodInvocation* invocation, gpointer data) {
        auto& self = *static_cast<Impl*>(data);
        // A prior call in the same GLib dispatch batch may already have closed the host.
        if (self.closing) {
            g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT,
                "The accessible window is closing");
            return;
        }
        try {
            if (std::string_view(path) == atspi::cachePath) {
                // A live tree: clients obtain objects through Accessible and receive change signals.
                g_dbus_method_invocation_return_value(invocation,
                    g_variant_new("(@a((so)(so)(so)iiassusau))", emptyArray("((so)(so)(so)iiassusau)")));
                return;
            }
            auto id = idOf(std::string_view(path).substr(std::char_traits<char>::length(atspi::objectBase) + 1));
            if (id) {
                const auto type = std::string_view(iface).substr(std::char_traits<char>::length(atspi::prefix));
                if (auto reply = self.method(*id, type, method, args)) {
                    g_dbus_method_invocation_return_value(invocation, reply);
                    return;
                }
            }
            g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
                "The accessible object or operation is unavailable");
        } catch (const std::exception& error) {
            g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, error.what());
        }
    }
    static GVariant* getProperty(GDBusConnection*, const gchar*, const gchar* path, const gchar*,
        const gchar* property, GError** error, gpointer data) {
        auto& self = *static_cast<Impl*>(data);
        try {
            if (std::string_view(property) == "version") return g_variant_new_uint32(1);
            auto id = idOf(std::string_view(path).substr(std::char_traits<char>::length(atspi::objectBase) + 1));
            if (id) if (auto value = self.property(*id, property)) return value;
        } catch (const std::exception& failure) {
            g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, failure.what());
            return nullptr;
        }
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Accessible property is unavailable");
        return nullptr;
    }
    static gboolean setProperty(GDBusConnection*, const gchar* sender, const gchar* path, const gchar*,
        const gchar* property, GVariant* value, GError** error, gpointer data) {
        auto& self = *static_cast<Impl*>(data);
        if (self.closing) {
            g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT, "The accessible window is closing");
            return FALSE;
        }
        try {
            if (std::string_view(property) == "Id") {
                self.applicationId = g_variant_get_int32(value);
                // Sender is the registry's unique name during the Embed handshake.
                self.registryName = sender;
                if (!std::exchange(self.registered, true)) self.signal(window, windowEvents, "Create");
                return TRUE;
            }
            auto id = idOf(std::string_view(path).substr(std::char_traits<char>::length(atspi::objectBase) + 1));
            auto node = id ? self.snapshot(*id) : Query<NodeSnapshot>{};
            auto number = g_variant_get_double(value);
            if (std::isfinite(number) && std::trunc(number) == number &&
                number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max() && node) {
                auto result = self.ui.semantics().perform(node->ref, Verb::setValue, static_cast<int>(number));
                if (result) return TRUE;
                g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, wireText(result.message).c_str());
                return FALSE;
            }
        } catch (const std::exception& failure) {
            g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, failure.what());
            return FALSE;
        }
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Value must be a supported integer");
        return FALSE;
    }
    std::string registryName = atspi::registry;

    GVariant* property(unsigned id, std::string_view key);
    GVariant* method(unsigned id, std::string_view iface, std::string_view name, GVariant* args);
    GVariant* accessible(unsigned id, const NodeSnapshot*, std::string_view name, GVariant* args);
    GVariant* component(unsigned id, const NodeSnapshot*, std::string_view name, GVariant* args);
    GVariant* text(const NodeSnapshot&, unsigned id, std::string_view name, GVariant* args);
    void walk(unsigned id, const NodeRef& ref, std::map<unsigned, std::vector<unsigned>>& next,
              std::map<unsigned, unsigned>& owners);
    void refreshTree(bool notify);
    void observe(const SemanticEvent&);
    void observeText(unsigned id, std::u16string_view value);
    void flushLayout();
    void focus(std::optional<unsigned> id);
    void signal(unsigned id, const char* iface, const char* member, const char* detail = "",
                int first = 0, int second = 0, GVariant* value = nullptr);
    void state(unsigned id, const char* detail, bool value) {
        signal(id, objectEvents, "StateChanged", detail, value);
    }
    std::optional<std::pair<int, int>> origin(unsigned id, unsigned coordinate);
    std::optional<Rect> bounds(unsigned id, const NodeSnapshot* node);
};


GVariant* LinuxAccessibility::Impl::property(unsigned id, std::string_view key) {
    auto node = snapshot(id);
    if (key == "Name") return g_variant_new_string(id == app ? "Isoduo" : id == window ? title.c_str()
        : node ? wireText(node->name).c_str() : "");
    if (key == "Description") return g_variant_new_string(node ? wireText(node->description).c_str() : "");
    if (key == "AccessibleId") {
        const auto identifier = node ? wireString(node->ref.item ? node->ref.item->id : node->ref.id) : "";
        return g_variant_new_string(identifier.c_str());
    }
    if (key == "Parent") return id == app ? g_variant_new("(so)", registryName.c_str(), atspi::applicationPath)
        : reference(parent(id));
    if (key == "ChildCount") return g_variant_new_int32(children(id).size());
    if (key == "Locale") return g_variant_new_string(std::setlocale(LC_CTYPE, nullptr));
    if (key == "Id") return g_variant_new_int32(applicationId);
    if (key == "InterfaceVersion") return g_variant_new_uint32(1);
    if (key == "ToolkitName") return g_variant_new_string("Isoduo");
    if (key == "Version" || key == "ToolkitVersion" || key == "AtspiVersion") return g_variant_new_string("");
    if (!node) return nullptr;
    if (key == "NActions") return g_variant_new_int32(actions(*node).size());
    if (key == "NSelectedChildren") return g_variant_new_int32(selected(id).size());
    if (key == "Text") return g_variant_new_string(wireText(node->value).c_str());
    if (node->range || node->role == Role::progress) {
        const auto range = node->range.value_or(SliderRange{0, 100, 0});
        if (key == "CurrentValue") return g_variant_new_double(node->numericValue.value_or(0));
        if (key == "MinimumValue") return g_variant_new_double(range.minimum);
        if (key == "MaximumValue") return g_variant_new_double(range.maximum);
        if (key == "MinimumIncrement") return g_variant_new_double(range.step);
    }
    if (key == "CharacterCount" || key == "CaretOffset") {
        auto length = std::min<std::size_t>(key == "CaretOffset" ? node->textSelection.value_or(Selection{}).caret
            : node->value.size(), node->value.size());
        return g_variant_new_int32(g_utf8_strlen(wireText(std::u16string_view(node->value).substr(0, length)).c_str(), -1));
    }
    return nullptr;
}

GVariant* LinuxAccessibility::Impl::method(unsigned id, std::string_view iface, std::string_view name, GVariant* args) {
    auto current = snapshot(id);
    auto* node = current ? &*current : nullptr;
    if (iface == "Accessible") return accessible(id, node, name, args);
    if (iface == "Application") {
        if (name == "GetLocale") return g_variant_new("(s)", std::setlocale(LC_CTYPE, nullptr));
        if (name == "GetApplicationBusAddress") return g_variant_new("(s)", "");
    }
    if (iface == "Component") return component(id, node, name, args);
    if (!node) return nullptr;
    const auto perform = [&](Verb verb, const NodeRef& ref, Argument argument = {}) {
        return g_variant_new("(b)", bool(ui.semantics().perform(ref, verb, std::move(argument))));
    };
    if (iface == "Action") {
        auto available = actions(*node);
        auto binding = wireText(node->accessKey) + ";;" + wireText(node->acceleratorKey);
        if (name == "GetActions") {
            GVariantBuilder result;
            g_variant_builder_init(&result, G_VARIANT_TYPE("a(sss)"));
            for (const auto& action : available)
                g_variant_builder_add(&result, "(sss)", action.name, action.name, binding.c_str());
            return g_variant_new("(a(sss))", &result);
        }
        const int index = integer(args);
        if (index < 0 || static_cast<std::size_t>(index) >= available.size())
            return name == "DoAction" ? g_variant_new("(b)", false) : g_variant_new("(s)", "");
        if (name == "DoAction") return perform(available[index].verb, node->ref);
        if (name == "GetKeyBinding") return g_variant_new("(s)", binding.c_str());
        return g_variant_new("(s)", available[index].name);
    }
    if (iface == "Selection") {
        if (name == "GetSelectedChild") return g_variant_new("(@(so))", reference(at(selected(id), integer(args))));
        if (name == "ClearSelection" || name == "SelectAll" || name == "DeselectChild" ||
            name == "DeselectSelectedChild") return g_variant_new("(b)", false);
        auto child = at(children(id), integer(args));
        auto target = child ? snapshot(*child) : Query<NodeSnapshot>{};
        if (name == "SelectChild")
            return target ? perform(Verb::select, target->ref) : g_variant_new("(b)", false);
        if (name == "IsChildSelected") return g_variant_new("(b)", target && target->selected);
    }
    if (iface == "Text") return text(*node, id, name, args);
    if (iface == "EditableText" && name == "SetTextContents")
        return perform(Verb::setValue, node->ref, utf16FromUtf8(string(args)));
    return nullptr;
}

GVariant* LinuxAccessibility::Impl::accessible(unsigned id, const NodeSnapshot* node, std::string_view name, GVariant* args) {
    if (name == "GetApplication") return g_variant_new("(@(so))", reference(app));
    if (name == "GetChildAtIndex")
        return g_variant_new("(@(so))", reference(at(children(id), integer(args))));
    if (name == "GetChildren") {
        GVariantBuilder list;
        g_variant_builder_init(&list, G_VARIANT_TYPE("a(so)"));
        for (auto child : children(id)) g_variant_builder_add_value(&list, reference(child));
        return g_variant_new("(a(so))", &list);
    }
    if (name == "GetIndexInParent") {
        auto owner = parent(id);
        if (!owner) return g_variant_new("(i)", -1);
        auto& siblings = children(*owner);
        auto position = std::ranges::find(siblings, id);
        return g_variant_new("(i)", position == siblings.end() ? -1 : static_cast<int>(position - siblings.begin()));
    }
    if (name == "GetInterfaces") {
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("as"));
        for (const auto* iface : interfacesFor(id, node))
            g_variant_builder_add(&result, "s", (atspi::prefix + std::string(iface)).c_str());
        return g_variant_new("(as)", &result);
    }
    if (name == "GetRole" || name == "GetRoleName" || name == "GetLocalizedRoleName") {
        const auto role = id == app ? RoleInfo{atspi::application, "application"}
            : id == window ? RoleInfo{atspi::frame, "frame"} : node ? roleInfo(node->role) : RoleInfo{atspi::panel, "panel"};
        return name == "GetRole" ? g_variant_new("(u)", role.code) : g_variant_new("(s)", role.name);
    }
    if (name == "GetState") {
        guint32 bits[2]{};
        auto set = [&](atspi::State state, bool on) { if (on) bits[state / 32] |= 1u << (state % 32); };
        set(atspi::defunct, id > window && !node);
        if (id == window) {
            set(atspi::enabled, true);
            set(atspi::visible, true);
            set(atspi::showing, true);
        }
        if (node) {
            set(atspi::enabled, node->enabled);
            bool sensitive = node->enabled && !node->blockedByModal;
            if (node->ref.item) {
                auto owner = ui.semantics().get({node->ref.id});
                sensitive = sensitive && owner && owner->enabled;
            }
            set(atspi::sensitive, sensitive);
            set(atspi::focusable, node->focusable && !node->hidden && !node->blockedByModal);
            set(atspi::focused, focused == id);
            set(atspi::visible, !node->hidden);
            set(atspi::showing, node->visible);
            set(atspi::checked, node->checked.value_or(node->role == Role::radioOption && node->selected));
            set(atspi::selectable, node->ref.item && hasVerb(*node, Verb::select));
            set(atspi::selected, node->selected);
            set(atspi::expandable, node->role == Role::dropdown);
            set(atspi::expanded, node->expanded);
            set(atspi::modal, node->role == Role::dialog && !node->hidden);
            set(atspi::invalidEntry, node->invalid);
            set(atspi::readOnly, node->readOnly || node->role == Role::progress);
            if (node->role == Role::textField) {
                set(atspi::editable, hasVerb(*node, Verb::setValue));
                set(atspi::multiLine, node->scroll.has_value());
                set(atspi::singleLine, !node->scroll);
            }
        }
        return g_variant_new("(@au)", g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, bits, 2, sizeof(guint32)));
    }
    if (name == "GetAttributes") {
        GVariantBuilder values;
        g_variant_builder_init(&values, G_VARIANT_TYPE("a{ss}"));
        if (node) {
            g_variant_builder_add(&values, "{ss}", "id", wireString(node->ref.id).c_str());
            if (node->ref.item)
                g_variant_builder_add(&values, "{ss}", "item-id", wireString(node->ref.item->id).c_str());
            if (node->role == Role::label)
                g_variant_builder_add(&values, "{ss}", "justification",
                    justification(node->textAlign, ui.presentation().justifyAsLeft).c_str());
            if (node->selectionRequired)
                g_variant_builder_add(&values, "{ss}", "selection-required", "true");
            if (!node->error.empty())
                g_variant_builder_add(&values, "{ss}", "error", wireText(node->error).c_str());
        }
        return g_variant_new("(a{ss})", &values);
    }
    if (name == "GetRelationSet") {
        GVariantBuilder relations;
        g_variant_builder_init(&relations, G_VARIANT_TYPE("a(ua(so))"));
        auto add = [&](atspi::Relation kind, const std::vector<NodeRef>& targets) {
            if (targets.empty()) return;
            GVariantBuilder list;
            g_variant_builder_init(&list, G_VARIANT_TYPE("a(so)"));
            for (auto& ref : targets) if (auto target = ui.semantics().get(ref))
                g_variant_builder_add_value(&list, reference(idOf(*target)));
            g_variant_builder_add(&relations, "(ua(so))", kind, &list);
        };
        if (node) {
            add(atspi::labelledBy, node->labelledBy);
            add(atspi::describedBy, node->describedBy);
            if (node->role == Role::label) {
                std::vector<NodeRef> labels, descriptions;
                for (auto& [child, owner] : parents) if (auto other = snapshot(child)) {
                    if (std::ranges::find(other->labelledBy, node->ref) != other->labelledBy.end()) labels.push_back(other->ref);
                    if (std::ranges::find(other->describedBy, node->ref) != other->describedBy.end()) descriptions.push_back(other->ref);
                }
                add(atspi::labelFor, labels);
                add(atspi::descriptionFor, descriptions);
            }
        }
        return g_variant_new("(a(ua(so)))", &relations);
    }
    return nullptr;
}

std::optional<std::pair<int, int>> LinuxAccessibility::Impl::origin(unsigned id, unsigned coordinate) {
    if (coordinate == atspi::window) return std::pair{0, 0};
    if (coordinate == atspi::screen) return native.screenOrigin();
    if (coordinate == atspi::parent) {
        auto owner = parent(id);
        if (!owner || *owner <= window) return std::pair{0, 0};
        auto n = snapshot(*owner);
        if (n && n->bounds) return std::pair{-n->bounds->x, -n->bounds->y};
    }
    return {};
}
std::optional<Rect> LinuxAccessibility::Impl::bounds(unsigned id, const NodeSnapshot* node) {
    if (id == window) {
        const auto size = native.geometry();
        return Rect{0, 0, size.width, size.height};
    }
    return node ? node->bounds : std::nullopt;
}
GVariant* LinuxAccessibility::Impl::component(unsigned id, const NodeSnapshot* node, std::string_view name, GVariant* args) {
    if (name == "GrabFocus")
        return g_variant_new("(b)", node && bool(ui.semantics().perform(node->ref, Verb::focus)));
    if (name == "GetAlpha") return g_variant_new("(d)", 1.0);
    if (name == "GetLayer") {
        if (id == window) return g_variant_new("(u)", 7u);
        if (node && node->role == Role::tooltip) return g_variant_new("(u)", 6u);
        if (node && (node->role == Role::menu || node->role == Role::menuItem ||
                     node->role == Role::menuSeparator || node->role == Role::dropdownOption))
            return g_variant_new("(u)", 5u);
        return g_variant_new("(u)", 3u);
    }
    if (name == "GetMDIZOrder") return g_variant_new("(n)", 0);
    auto rect = bounds(id, node);
    if (name == "GetSize") return g_variant_new("(ii)", rect ? rect->w : 0, rect ? rect->h : 0);
    const bool point = name == "Contains" || name == "GetAccessibleAtPoint";
    auto shift = origin(id, natural(args, point ? 2 : 0));
    if (point) {
        const auto x = shift ? static_cast<long long>(integer(args)) - shift->first : 0;
        const auto y = shift ? static_cast<long long>(integer(args, 1)) - shift->second : 0;
        auto contains = [&](const Rect& box) {
            return x >= box.x && y >= box.y && x < static_cast<long long>(box.x) + box.w &&
                   y < static_cast<long long>(box.y) + box.h;
        };
        if (name == "Contains") return g_variant_new("(b)", shift && rect && contains(*rect));
        std::optional<unsigned> match;
        if (shift && x >= 0 && y >= 0 && x < native.geometry().width && y < native.geometry().height) {
            // Popup children can be outside their logical parent's bounds.
            const auto inside = [&](unsigned candidate) {
                for (auto ancestor = std::optional{candidate}; ancestor; ancestor = parent(*ancestor))
                    if (*ancestor == id) return true;
                return false;
            };
            const auto fromRef = [&](const NodeRef& ref) -> std::optional<unsigned> {
                auto n = ui.semantics().get(ref);
                if (!n || !n->bounds || !contains(*n->bounds)) return {};
                auto candidate = idOf(*n);
                return inside(candidate) ? std::optional{candidate} : std::nullopt;
            };
            const int clientX = static_cast<int>(x), clientY = static_cast<int>(y);
            if (const auto* menu = frame.menuPanel(); menu && frame.menuContains(clientX, clientY)) {
                if (auto item = frame.menuItemAt(clientX, clientY)) match = fromRef({menu->id, OptionValue{*item}});
                if (!match) match = fromRef({menu->id});
            }
            if (const auto* nativeHit = frame.hit(clientX, clientY); !match && nativeHit && nativeHit->dropdownPanel) {
                if (auto owner = ui.semantics().get({nativeHit->id})) {
                    for (auto child : children(idOf(*owner))) {
                        auto option = snapshot(child);
                        if (option && option->bounds && contains(*option->bounds) && inside(child)) {
                            match = child;
                            break;
                        }
                    }
                }
            }
        }
        if (!match && shift && rect && contains(*rect)) {
            // The last sibling is above earlier siblings.
            auto hit = [&](auto&& self, unsigned candidate) -> unsigned {
                for (auto child : children(candidate) | std::views::reverse) {
                    if (auto n = snapshot(child); n && n->bounds && contains(*n->bounds)) return self(self, child);
                }
                return candidate;
            };
            match = hit(hit, id);
        }
        return g_variant_new("(@(so))", reference(match));
    }
    const Rect result = rect && shift ? Rect{rect->x + shift->first, rect->y + shift->second, rect->w, rect->h}
                                      : Rect{-1, -1, 0, 0};
    if (name == "GetPosition") return g_variant_new("(ii)", result.x, result.y);
    if (name == "GetExtents") return g_variant_new("((iiii))", result.x, result.y, result.w, result.h);
    return nullptr;
}

void LinuxAccessibility::Impl::walk(unsigned id, const NodeRef& ref,
    std::map<unsigned, std::vector<unsigned>>& next, std::map<unsigned, unsigned>& owners) {
    auto children = ui.semantics().children(ref);
    if (!children) return;
    for (auto& child : *children) {
        auto node = ui.semantics().get(child);
        if (!node || node->hidden) continue;
        const auto childId = idOf(*node);
        if (!owners.emplace(childId, id).second) continue;
        next[id].push_back(childId);
        if (node->role == Role::textField) priorText.try_emplace(childId, node->value);
        if (!child.item) walk(childId, child, next, owners);
    }
}
void LinuxAccessibility::Impl::refreshTree(bool notify) {
    std::map<unsigned, std::vector<unsigned>> next{{app, {window}}};
    std::map<unsigned, unsigned> owners{{window, app}};
    walk(window, {}, next, owners);
    if (notify) {
        // Apply per-parent diffs as a sequence of edits so subscribed clients can update cached indices.
        for (auto& [owner, old] : tree) {
            const auto found = next.find(owner);
            const std::vector<unsigned> empty;
            const auto& current = found == next.end() ? empty : found->second;
            for (std::size_t i = old.size(); i-- > 0;) {
                if (std::ranges::find(current, old[i]) != current.end()) continue;
                signal(owner, objectEvents, "ChildrenChanged", "remove", i, 0, reference(old[i]));
                old.erase(old.begin() + i);
            }
        }
        for (auto& [owner, current] : next) {
            auto& old = tree[owner];
            for (std::size_t i = 0; i < current.size(); ++i) {
                if (i < old.size() && old[i] == current[i]) continue;
                const auto moved = std::find(old.begin() + i, old.end(), current[i]);
                if (moved != old.end()) {
                    signal(owner, objectEvents, "ChildrenChanged", "remove", moved - old.begin(), 0, reference(*moved));
                    old.erase(moved);
                }
                signal(owner, objectEvents, "ChildrenChanged", "add", i, 0, reference(current[i]));
                old.insert(old.begin() + i, current[i]);
            }
        }
    }
    tree = std::move(next);
    parents = std::move(owners);
}
void LinuxAccessibility::Impl::signal(unsigned id, const char* iface, const char* member, const char* detail,
                                      int first, int second, GVariant* value) {
    g_dbus_connection_emit_signal(bus.get(), nullptr, path(id).c_str(), iface, member,
        g_variant_new("(siiv@a{sv})", detail, first, second, value ? value : g_variant_new_int32(0),
                      emptyArray("{sv}")), nullptr);
}
void LinuxAccessibility::Impl::focus(std::optional<unsigned> id) {
    if (focused == id) return;
    if (focused) state(*focused, "focused", false);
    focused = id;
    if (focused) state(*focused, "focused", true);
}

void LinuxAccessibility::Impl::observe(const SemanticEvent& event) {
    if (event.kind == SemanticEventKind::layoutChanged || event.kind == SemanticEventKind::structureChanged) {
        layoutPending = true;
        return;
    }
    auto node = ui.semantics().get(event.ref);
    auto id = node ? std::optional{idOf(*node)} : std::nullopt;
    if (!id && event.kind == SemanticEventKind::menuClosed) {
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
            if (it->second.ref == event.ref) { id = it->first; break; }
        if (id) state(*id, "showing", false);
        return;
    }
    if (!id) return;
    switch (event.kind) {
    case SemanticEventKind::focusChanged:
        focus(id);
        break;
    case SemanticEventKind::menuOpened:
        state(*id, "showing", true);
        break;
    case SemanticEventKind::textChanged:
        observeText(*id, node->value);
        break;
    case SemanticEventKind::textSelectionChanged: {
        signal(*id, objectEvents, "TextSelectionChanged");
        if (node->textSelection) {
            const auto caret = std::min<unsigned>(node->textSelection->caret, node->value.size());
            const auto prefix = wireText(std::u16string_view(node->value).substr(0, caret));
            signal(*id, objectEvents, "TextCaretMoved", "", g_utf8_strlen(prefix.c_str(), -1));
        }
        break;
    }
    case SemanticEventKind::propertyChanged: {
        const auto& property = event.property;
        if (property == "name") {
            signal(*id, objectEvents, "PropertyChange", "accessible-name", 0, 0,
                   g_variant_new_string(wireText(node->name).c_str()));
        } else if (property == "numericValue" && node->numericValue) {
            signal(*id, objectEvents, "PropertyChange", "accessible-value", 0, 0,
                   g_variant_new_double(*node->numericValue));
        } else if (property == "value" && !node->numericValue) {
            signal(*id, objectEvents, "PropertyChange", "accessible-value", 0, 0,
                   g_variant_new_string(wireText(node->value).c_str()));
        } else if (property == "enabled" || property == "blockedByModal") {
            state(*id, "enabled", node->enabled);
            state(*id, "sensitive", node->enabled && !node->blockedByModal);
            layoutPending = true;
        } else if (property == "checked") state(*id, "checked", node->checked.value_or(false));
        else if (property == "selected") {
            state(*id, "selected", node->selected);
            if (node->role == Role::radioOption) state(*id, "checked", node->selected);
            if (auto owner = ui.semantics().get({node->ref.id}); owner && node->ref.item)
                signal(idOf(*owner), objectEvents, "SelectionChanged");
        } else if (property == "expanded") {
            state(*id, "expanded", node->expanded);
            layoutPending = true;
        } else if (property == "invalid") state(*id, "invalid-entry", node->invalid);
        else if (property == "readOnly") {
            state(*id, "read-only", node->readOnly);
            state(*id, "editable", hasVerb(*node, Verb::setValue));
        } else if (property == "hidden" && event.hidden) {
            state(*id, "visible", !*event.hidden);
            state(*id, "showing", !*event.hidden);
            if (node->role == Role::dialog) {
                state(*id, "modal", !*event.hidden);
                signal(*id, windowEvents, *event.hidden ? "Destroy" : "Create");
            }
        } else if (property == "styles" || property == "textAlign" || property == "justifyMethod" ||
                   property == "lineSpacing" || property == "paragraphSpacing") {
            signal(*id, objectEvents, "TextAttributesChanged");
        }
        break;
    }
    default: break;
    }
}

void LinuxAccessibility::Impl::flushLayout() {
    if (!bus || !subtree) return;
    const auto outer = bounds(window, nullptr);
    if (const auto old = priorGeometry.find(window); old == priorGeometry.end() || old->second.bounds != outer) {
        priorGeometry[window] = {outer};
        const auto r = *outer;
        signal(window, objectEvents, "BoundsChanged", "", 0, 0, g_variant_new("(iiii)", r.x, r.y, r.w, r.h));
    }
    if (!std::exchange(layoutPending, false)) return;
    refreshTree(true);
    std::optional<unsigned> actualFocus;
    for (auto it = nodes.begin(); it != nodes.end();) {
        const auto id = it->first;
        const auto node = snapshot(id);
        if (!node) {
            ids.erase(it->second);
            priorText.erase(id);
            priorGeometry.erase(id);
            it = nodes.erase(it);
            continue;
        }
        const GeometryState current{node->bounds, node->focusable && !node->hidden && !node->blockedByModal};
        const auto [old, inserted] = priorGeometry.try_emplace(id, current);
        if (!inserted && old->second != current) {
            if (old->second.focusable != current.focusable) state(id, "focusable", current.focusable);
            if (old->second.bounds != current.bounds) {
                if (old->second.bounds.has_value() != current.bounds.has_value())
                    state(id, "showing", current.bounds.has_value());
                const auto r = current.bounds.value_or(Rect{-1, -1, 0, 0});
                signal(id, objectEvents, "BoundsChanged", "", 0, 0, g_variant_new("(iiii)", r.x, r.y, r.w, r.h));
            }
            old->second = current;
        }
        if (node->focused && !node->hidden &&
            (!actualFocus || node->ref.item || node->role == Role::menuItem))
            actualFocus = id;
        ++it;
    }
    focus(actualFocus);
    signal(window, objectEvents, "VisibleDataChanged");
}


LinuxAccessibility::LinuxAccessibility(GMainContext* context, Ui& ui, LinuxWindow& native,
                                       const Frame& frame, std::u16string_view title, const bool& closing)
    : impl_(std::make_unique<Impl>(context, ui, native, frame, title, closing)) {}
LinuxAccessibility::~LinuxAccessibility() = default;
void LinuxAccessibility::flushLayout() { impl_->flushLayout(); }
void LinuxAccessibility::title(std::u16string_view value) {
    auto& bridge = *impl_;
    auto next = wireText(value);
    if (bridge.title == next) return;
    bridge.title = std::move(next);
    if (bridge.bus && bridge.subtree)
        bridge.signal(window, objectEvents, "PropertyChange", "accessible-name", 0, 0,
                      g_variant_new_string(bridge.title.c_str()));
}

namespace {
// AT-SPI counts Unicode scalar values; semantic and layout offsets count UTF-16 code units.
class TextOffsets {
public:
    explicit TextOffsets(std::u16string_view text) {
        unsigned offset = 0;
        while (offset < text.size()) {
            auto unit = text[offset++];
            if (unit >= 0xd800 && unit <= 0xdbff && offset < text.size() &&
                text[offset] >= 0xdc00 && text[offset] <= 0xdfff) ++offset;
            units_.push_back(offset);
        }
    }
    int size() const { return static_cast<int>(units_.size() - 1); }
    bool valid(int offset) const { return offset >= 0 && offset <= size(); }
    unsigned units(int offset) const { return units_[offset]; }
    int characters(unsigned offset) const {
        auto at = std::upper_bound(units_.begin(), units_.end(), offset);
        return static_cast<int>(at - units_.begin() - 1);
    }
private:
    std::vector<unsigned> units_{0};
};

std::pair<int, int> textBoundaryRange(std::u16string_view text, const TextOffsets& offsets,
                                     const Placement* placement, atspi::TextBoundaryKind kind,
                                     int index, std::string_view method) {
    using Boundary = atspi::TextBoundaryKind;
    constexpr std::pair<int, int> absent{-1, -1};
    const bool before = method == "GetTextBeforeOffset", after = method == "GetTextAfterOffset";
    if (!offsets.valid(index) || (index == offsets.size() && !before)) return absent;
    if (kind == Boundary::character) {
        index += static_cast<int>(after) - static_cast<int>(before);
        return index >= 0 && index < offsets.size() ? std::pair{index, index + 1} : absent;
    }

    std::vector<int> bounds{0};
    const auto append = [&](int boundary) {
        if (boundary > bounds.back()) bounds.push_back(boundary);
    };
    const bool line = kind == Boundary::lineStart || kind == Boundary::lineEnd;
    if (line && placement && placement->multiline) {
        for (const auto& row : placement->view.lines)
            append(offsets.characters(kind == Boundary::lineEnd ? row.end : row.begin));
    } else if (line || kind == Boundary::paragraphStart) {
        for (auto boundary : paragraphBounds(text)) {
            if (kind == Boundary::lineEnd && boundary > 0 && text[boundary - 1] == u'\n') --boundary;
            append(offsets.characters(boundary));
        }
    } else if (!text.empty()) {
        // Word and sentence starts and ends are linguistic boundaries. Pango reports
        // them in Unicode character offsets, which also match the AT-SPI wire offsets.
        const auto utf8 = wireText(text);
        std::vector<PangoLogAttr> attrs(static_cast<std::size_t>(offsets.size()) + 1);
        pango_get_log_attrs(utf8.c_str(), static_cast<int>(utf8.size()), -1, pango_language_get_default(),
                            attrs.data(), static_cast<int>(attrs.size()));
        for (int at = 1; at < offsets.size(); ++at) {
            const auto& attr = attrs[at];
            const bool boundary = kind == Boundary::wordStart ? attr.is_word_start
                : kind == Boundary::wordEnd ? attr.is_word_end
                : kind == Boundary::sentenceStart ? attr.is_sentence_start
                : attr.is_sentence_end;
            if (boundary) append(at);
        }
    }
    append(offsets.size());

    // A separator belongs to the range between its neighboring delimiters.
    // Before/after choose adjacent ranges from the same set of delimiters.
    auto end = std::upper_bound(bounds.begin(), bounds.end(), index);
    if (before) --end;
    if (after && end != bounds.end()) ++end;
    return end == bounds.begin() || end == bounds.end() ? absent : std::pair{*(end - 1), *end};
}

struct TextGeometry {
    const Placement& placement;
    const EditSession* composition;

    Rect clip() const {
        auto editor = placement.textField.editor;
        editor.x += placement.bounds.x;
        editor.y += placement.bounds.y;
        return intersect(placement.multiline ? placement.view.text : editor, placement.clip);
    }
    unsigned committed(unsigned offset) const {
        if (!composition || offset <= composition->begin()) return offset;
        const auto start = composition->begin();
        const auto after = start + static_cast<unsigned>(composition->preedit.size());
        return offset < after ? start : offset - after + composition->end();
    }
    Rect range(unsigned start, unsigned end) const {
        const auto& p = placement;
        const auto clipping = clip();
        Rect result{};
        auto addRect = [&](Rect rect) {
            rect = intersect(rect, clipping);
            if (rect.w <= 0 || rect.h <= 0) return;
            if (result.w == 0) { result = rect; return; }
            const int x = std::min(result.x, rect.x), y = std::min(result.y, rect.y);
            result = {x, y, std::max(result.x + result.w, rect.x + rect.w) - x,
                           std::max(result.y + result.h, rect.y + rect.h) - y};
        };
        auto addRange = [&](unsigned from, unsigned to) {
            if (from >= to) return;
            if (!p.multiline) {
                const int x = p.caretX(from), right = p.caretX(to);
                addRect({std::min(x, right), p.bounds.y + p.textField.editor.y,
                         std::abs(right - x), p.textField.editor.h});
                return;
            }
            const auto& view = p.view;
            const auto last = std::min<std::size_t>(view.lines.size(), view.first + view.visible);
            for (unsigned line = view.first; line < last; ++line) {
                const auto& positions = view.lines[line];
                const unsigned begin = std::max(positions.begin, from), end = std::min(positions.end, to);
                if (begin >= end) continue;
                const int x = view.text.x + std::lround(view.xInLine(line, begin));
                const int right = view.text.x + std::lround(view.xInLine(line, end));
                addRect({std::min(x, right), view.rowY(line), std::abs(right - x), view.rowHeight});
            }
        };
        if (!composition) addRange(start, end);
        else {
            const auto begin = composition->begin(), finish = composition->end();
            if (start < begin) addRange(start, std::min(end, begin));
            if (end > finish) {
                const auto after = begin + static_cast<unsigned>(composition->preedit.size());
                addRange(std::max(start, finish) - finish + after, end - finish + after);
            }
        }
        return result;
    }
};

using TextAttributes = std::map<std::string, std::string>;
GVariant* attributes(const TextAttributes& values) {
    GVariantBuilder result;
    g_variant_builder_init(&result, G_VARIANT_TYPE("a{ss}"));
    for (auto& [name, value] : values) g_variant_builder_add(&result, "{ss}", name.c_str(), value.c_str());
    return g_variant_builder_end(&result);
}
}

void LinuxAccessibility::Impl::observeText(unsigned id, std::u16string_view value) {
    auto& old = priorText[id];
    const TextOffsets before(old), after(value);
    auto scalar = [](std::u16string_view text, const TextOffsets& offsets, int index) {
        return text.substr(offsets.units(index), offsets.units(index + 1) - offsets.units(index));
    };
    int first = 0, lastBefore = before.size(), lastAfter = after.size();
    while (first < lastBefore && first < lastAfter && scalar(old, before, first) == scalar(value, after, first))
        ++first;
    while (first < lastBefore && first < lastAfter &&
           scalar(old, before, lastBefore - 1) == scalar(value, after, lastAfter - 1)) {
        --lastBefore;
        --lastAfter;
    }
    const auto change = [&](const char* detail, std::u16string_view text, const TextOffsets& offsets, int last) {
        if (first == last) return;
        const auto slice = text.substr(offsets.units(first), offsets.units(last) - offsets.units(first));
        signal(id, objectEvents, "TextChanged", detail, first, last - first,
               g_variant_new_string(wireText(slice).c_str()));
    };
    change("delete", old, before, lastBefore);
    change("insert", value, after, lastAfter);
    old = value;
}

GVariant* LinuxAccessibility::Impl::text(const NodeSnapshot& node, unsigned id, std::string_view name, GVariant* args) {
    const TextOffsets offsets(node.value);
    const auto selection = node.textSelection.value_or(Selection{});
    const bool hasSelection = selection.anchor != selection.caret;
    const auto value = [&](unsigned start, unsigned end) {
        return wireText(std::u16string_view(node.value).substr(start, end - start));
    };
    const auto resultFor = [&](int start, int end) {
        if (!offsets.valid(start) || !offsets.valid(end) || start > end)
            return g_variant_new("(sii)", "", -1, -1);
        return g_variant_new("(sii)", value(offsets.units(start), offsets.units(end)).c_str(), start, end);
    };
    const auto select = [&](int start, int end) {
        return g_variant_new("(b)", offsets.valid(start) && offsets.valid(end) &&
            bool(ui.semantics().perform(node.ref, Verb::select, Selection{offsets.units(start), offsets.units(end)})));
    };
    if (name == "GetNSelections") return g_variant_new("(i)", hasSelection ? 1 : 0);
    if (name == "GetSelection") {
        if (!hasSelection || integer(args) != 0) return g_variant_new("(ii)", -1, -1);
        return g_variant_new("(ii)", offsets.characters(std::min(selection.anchor, selection.caret)),
            offsets.characters(std::max(selection.anchor, selection.caret)));
    }
    if (name == "SetSelection")
        return integer(args) == 0 && hasSelection ? select(integer(args, 1), integer(args, 2)) : g_variant_new("(b)", false);
    if (name == "AddSelection")
        return hasSelection ? g_variant_new("(b)", false) : select(integer(args), integer(args, 1));
    if (name == "RemoveSelection")
        return hasSelection && integer(args) == 0 ? select(offsets.characters(selection.caret), offsets.characters(selection.caret))
            : g_variant_new("(b)", false);
    if (name == "SetCaretOffset") return select(integer(args), integer(args));

    const auto* placement = node.hidden ? nullptr : findPlacement(frame, node.ref.id);
    const auto* edit = access(ui).edit(node.ref.id);
    const auto* composition = edit && edit->composing ? edit : nullptr;
    if (name == "GetOffsetAtPoint") {
        auto shift = origin(id, natural(args, 2));
        if (!shift || !placement) return g_variant_new("(i)", -1);
        const auto x = static_cast<long long>(integer(args)) - shift->first;
        const auto y = static_cast<long long>(integer(args, 1)) - shift->second;
        const TextGeometry geometry{*placement, composition};
        const auto clip = geometry.clip();
        if (x < clip.x || y < clip.y || x >= clip.x + clip.w || y >= clip.y + clip.h)
            return g_variant_new("(i)", -1);
        const auto hit = placementTextCell(frame, node.ref.id, static_cast<int>(x), static_cast<int>(y));
        return g_variant_new("(i)", offsets.characters(geometry.committed(hit)));
    }
    if (name == "GetCharacterExtents" || name == "GetRangeExtents") {
        const int start = integer(args);
        const bool character = name == "GetCharacterExtents";
        const int end = character ? (start < offsets.size() ? start + 1 : -1) : integer(args, 1);
        auto shift = origin(id, natural(args, character ? 1 : 2));
        if (placement && shift && offsets.valid(start) && offsets.valid(end) && start < end) {
            auto rect = TextGeometry{*placement, composition}.range(offsets.units(start), offsets.units(end));
            if (rect.w > 0) return g_variant_new("(iiii)", rect.x + shift->first, rect.y + shift->second, rect.w, rect.h);
        }
        return g_variant_new("(iiii)", -1, -1, 0, 0);
    }
    if (name == "ScrollSubstringTo") {
        const int start = integer(args), end = integer(args, 1);
        if (!placement || !placement->multiline || composition || !offsets.valid(start) ||
            !offsets.valid(end) || start > end) return g_variant_new("(b)", false);
        const auto& view = placement->view;
        const auto first = view.lineOf(offsets.units(start)), last = view.lineOf(offsets.units(end));
        auto target = first;
        // Scroll to the first line unless the range fits and bottom alignment was requested.
        const auto type = natural(args, 2);
        if (type == 1 || type == 3) target = last >= view.visible ? last - view.visible + 1 : 0;
        if (type > 6) return g_variant_new("(b)", false);
        return g_variant_new("(b)", bool(ui.semantics().perform(node.ref, Verb::scroll, static_cast<int>(target))));
    }

    if (name == "GetText") {
        const int start = integer(args), requestedEnd = integer(args, 1);
        const int end = requestedEnd == -1 ? offsets.size() : requestedEnd;
        return g_variant_new("(s)", offsets.valid(start) && offsets.valid(end) && start <= end
            ? value(offsets.units(start), offsets.units(end)).c_str() : "");
    }
    if (name == "GetCharacterAtOffset") {
        const int index = integer(args);
        if (index < 0 || index >= offsets.size()) return g_variant_new("(i)", 0);
        return g_variant_new("(i)", static_cast<int>(g_utf8_get_char(value(offsets.units(index), offsets.units(index + 1)).c_str())));
    }
    if (name == "GetStringAtOffset" || name == "GetTextAtOffset" ||
        name == "GetTextBeforeOffset" || name == "GetTextAfterOffset") {
        using Boundary = atspi::TextBoundaryKind;
        // Modern granularities select starts. The legacy numbers distinguish starts and ends.
        constexpr std::array granularities{Boundary::character, Boundary::wordStart, Boundary::sentenceStart,
                                           Boundary::lineStart, Boundary::paragraphStart};
        const auto requested = natural(args, 1);
        const bool modern = name == "GetStringAtOffset";
        if (modern ? requested >= granularities.size() : requested > static_cast<unsigned>(Boundary::lineEnd))
            return resultFor(-1, -1);
        const auto kind = modern ? granularities[requested] : static_cast<Boundary>(requested);
        const auto [first, last] = textBoundaryRange(node.value, offsets, composition ? nullptr : placement,
                                                     kind, integer(args), name);
        return resultFor(first, last);
    }

    TextAttributes defaults{{"justification", justification(node.textAlign, ui.presentation().justifyAsLeft)},
        {"line-height", std::to_string(node.lineSpacing)}, {"paragraph-spacing", std::to_string(node.paragraphSpacing)}};
    if (name == "GetDefaultAttributes" || name == "GetDefaultAttributeSet")
        return g_variant_new("(@a{ss})", attributes(defaults));
    if (name == "GetAttributes" || name == "GetAttributeRun" || name == "GetAttributeValue") {
        const int index = integer(args);
        TextAttributes run;
        unsigned first = 0, last = node.value.size();
        if (!offsets.valid(index) || index >= offsets.size()) {
            if (name == "GetAttributeValue") return g_variant_new("(s)", "");
            return g_variant_new("(@a{ss}ii)", attributes(run), -1, -1);
        }
        const auto offset = offsets.units(index);
        TextStyle style;
        for (auto& span : node.styles) {
            const auto end = span.start + span.length;
            if (end <= offset) { first = end; continue; }
            if (span.start > offset) { last = span.start; break; }
            first = span.start; last = end; style = span.style();
            break;
        }
        run["weight"] = style.bold ? "700" : "400";
        run["style"] = style.italic ? "italic" : "normal";
        run["underline"] = style.underline ? "single" : "none";
        if (auto color = ui.semantics().textColor(node.ref, style.color))
            run["fg-color"] = std::to_string(color->r) + "," + std::to_string(color->g) + "," + std::to_string(color->b);
        gboolean includeDefaults = false;
        if (name == "GetAttributeRun") g_variant_get_child(args, 1, "b", &includeDefaults);
        if (includeDefaults || name == "GetAttributeValue") run.merge(defaults);
        if (name == "GetAttributeValue") {
            auto found = run.find(string(args, 1));
            return g_variant_new("(s)", found == run.end() ? "" : found->second.c_str());
        }
        return g_variant_new("(@a{ss}ii)", attributes(run), offsets.characters(first), offsets.characters(last));
    }
    return nullptr;
}

}
