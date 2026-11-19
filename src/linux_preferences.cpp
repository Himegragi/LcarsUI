#include "linux_preferences.hpp"
#include <gio/gio.h>
#include <cstdlib>
#include <memory>
#include <string_view>

namespace isoduo::detail {
namespace {
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

Variant gnomeSetting(const char* schemaName, const char* key) {
    auto source = g_settings_schema_source_get_default();
    if (!source) return {nullptr, g_variant_unref};
    std::unique_ptr<GSettingsSchema, decltype(&g_settings_schema_unref)> schema(
        g_settings_schema_source_lookup(source, schemaName, TRUE), g_settings_schema_unref);
    if (!schema || !g_settings_schema_has_key(schema.get(), key)) return {nullptr, g_variant_unref};
    std::unique_ptr<GSettings, decltype(&g_object_unref)> settings(
        g_settings_new_full(schema.get(), nullptr, nullptr), g_object_unref);
    return {g_settings_get_value(settings.get(), key), g_variant_unref};
}

std::optional<bool> gnomeAnimations() {
    auto enabled = gnomeSetting("org.gnome.desktop.interface", "enable-animations");
    auto reduced = gnomeSetting("org.gnome.desktop.a11y.interface", "reduced-motion");
    if (reduced && g_variant_is_of_type(reduced.get(), G_VARIANT_TYPE_STRING) &&
        std::string_view(g_variant_get_string(reduced.get(), nullptr)) == "reduce") return false;
    if (enabled && g_variant_is_of_type(enabled.get(), G_VARIANT_TYPE_BOOLEAN))
        return g_variant_get_boolean(enabled.get()) != FALSE;
    return {};
}

std::optional<bool> xfceAnimations() {
    if (!std::getenv("DBUS_SESSION_BUS_ADDRESS") && !std::getenv("XDG_RUNTIME_DIR")) return {};
    std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus(
        g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_object_unref);
    if (!bus) return {};
    Variant reply(g_dbus_connection_call_sync(bus.get(), "org.xfce.Xfconf", "/org/xfce/Xfconf",
        "org.xfce.Xfconf", "GetProperty", g_variant_new("(ss)", "xsettings", "/Gtk/EnableAnimations"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 250, nullptr, nullptr), g_variant_unref);
    if (!reply) return {};
    GVariant* property = nullptr;
    g_variant_get(reply.get(), "(v)", &property);
    Variant value(property, g_variant_unref);
    if (g_variant_is_of_type(value.get(), G_VARIANT_TYPE_BOOLEAN)) return g_variant_get_boolean(value.get()) != FALSE;
    if (g_variant_is_of_type(value.get(), G_VARIANT_TYPE_INT32)) {
        auto integer = g_variant_get_int32(value.get());
        if (integer == 0 || integer == 1) return integer == 1;
    }
    return {};
}

bool desktopIs(std::string_view desktop, std::string_view name) {
    while (!desktop.empty()) {
        const auto separator = desktop.find(':');
        const auto token = desktop.substr(0, separator);
        if (token.size() == name.size() && g_ascii_strncasecmp(token.data(), name.data(), name.size()) == 0) return true;
        if (separator == desktop.npos) break;
        desktop.remove_prefix(separator + 1);
    }
    return false;
}
}

LinuxDesktopPreferences readLinuxDesktopPreferences() {
    const auto desktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (!desktop) return {};
    // GNOME and Xfce do not publish an always-show-widget-focus preference.
    if (desktopIs(desktop, "GNOME")) return {gnomeAnimations(), {}};
    if (desktopIs(desktop, "XFCE")) return {xfceAnimations(), {}};
    return {};
}
}
