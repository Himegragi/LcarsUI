#include "linux_preferences.hpp"
#include <gio/gio.h>
#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

namespace isoduo::detail {
namespace {
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
using Settings = std::unique_ptr<GSettings, decltype(&g_object_unref)>;
using Bus = std::unique_ptr<GDBusConnection, decltype(&g_object_unref)>;

Settings gnomeSettings(const char* schemaName, const char* key) {
    auto source = g_settings_schema_source_get_default();
    if (!source) return {nullptr, g_object_unref};
    std::unique_ptr<GSettingsSchema, decltype(&g_settings_schema_unref)> schema(
        g_settings_schema_source_lookup(source, schemaName, TRUE), g_settings_schema_unref);
    if (!schema || !g_settings_schema_has_key(schema.get(), key)) return {nullptr, g_object_unref};
    return {g_settings_new_full(schema.get(), nullptr, nullptr), g_object_unref};
}

constexpr auto gnomeInterface = "org.gnome.desktop.interface";
constexpr auto gnomeAccessibility = "org.gnome.desktop.a11y.interface";
constexpr auto gnomeAnimationsKey = "enable-animations";
constexpr auto gnomeReducedMotion = "reduced-motion";
constexpr auto xfconfService = "org.xfce.Xfconf";
constexpr auto xfconfPath = "/org/xfce/Xfconf";
constexpr auto xfceChannel = "xsettings";
constexpr auto xfceAnimationsKey = "/Gtk/EnableAnimations";

Variant gnomeSetting(const char* schema, const char* key) {
    auto settings = gnomeSettings(schema, key);
    return {settings ? g_settings_get_value(settings.get(), key) : nullptr, g_variant_unref};
}
std::optional<bool> gnomeAnimations() {
    auto enabled = gnomeSetting(gnomeInterface, gnomeAnimationsKey);
    auto reduced = gnomeSetting(gnomeAccessibility, gnomeReducedMotion);
    if (reduced && g_variant_is_of_type(reduced.get(), G_VARIANT_TYPE_STRING) &&
        std::string_view(g_variant_get_string(reduced.get(), nullptr)) == "reduce") return false;
    if (enabled && g_variant_is_of_type(enabled.get(), G_VARIANT_TYPE_BOOLEAN))
        return g_variant_get_boolean(enabled.get()) != FALSE;
    return {};
}

Bus xfceBus() {
    if (!std::getenv("DBUS_SESSION_BUS_ADDRESS") && !std::getenv("XDG_RUNTIME_DIR")) return {nullptr, g_object_unref};
    return {g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_object_unref};
}
std::optional<bool> xfceAnimations() {
    auto bus = xfceBus();
    if (!bus) return {};
    Variant reply(g_dbus_connection_call_sync(bus.get(), xfconfService, xfconfPath,
        xfconfService, "GetProperty", g_variant_new("(ss)", xfceChannel, xfceAnimationsKey),
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

struct LinuxPreferenceMonitor::Impl {
    GMainContext* context = g_main_context_ref(LinuxPreferenceMonitor::context());
    Settings enabled{nullptr, g_object_unref}, reduced{nullptr, g_object_unref};
    Bus bus{nullptr, g_object_unref};
    guint subscription = 0;
    bool pending = false, ready = false;
    std::vector<GPollFD> sources;
    std::vector<pollfd> descriptors;

    ~Impl() {
        if (subscription) g_dbus_connection_signal_unsubscribe(bus.get(), subscription);
        enabled.reset();
        reduced.reset();
        g_main_context_unref(context);
    }
    void watch(Settings& settings, const char* schema, const char* key) {
        settings = gnomeSettings(schema, key);
        if (!settings) return;
        g_signal_connect(settings.get(), "changed", G_CALLBACK(+[](GSettings*, const char* changedKey, gpointer data) {
            const std::string_view key(changedKey);
            if (key == gnomeAnimationsKey || key == gnomeReducedMotion) static_cast<Impl*>(data)->pending = true;
        }), this);
        // GSettings delivers subsequent changes after the value has first been read.
        g_variant_unref(g_settings_get_value(settings.get(), key));
    }
    void watchXfce() {
        bus = xfceBus();
        if (!bus) return;
        subscription = g_dbus_connection_signal_subscribe(bus.get(), xfconfService, xfconfService, nullptr,
            xfconfPath, xfceChannel, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const char*, const char*, const char*, const char* signal, GVariant* params, gpointer data) {
                if (std::string_view(signal) != "PropertyChanged" && std::string_view(signal) != "PropertyRemoved") return;
                if (g_variant_n_children(params) < 2) return;
                Variant path(g_variant_get_child_value(params, 1), g_variant_unref);
                if (g_variant_is_of_type(path.get(), G_VARIANT_TYPE_STRING) &&
                    std::string_view(g_variant_get_string(path.get(), nullptr)) == xfceAnimationsKey)
                    static_cast<Impl*>(data)->pending = true;
            }, this, nullptr);
    }
};

LinuxPreferenceMonitor::LinuxPreferenceMonitor() {
    impl_ = std::make_unique<Impl>();
    const auto* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (!desktop) return;
    const bool gnome = desktopIs(desktop, "GNOME");
    if (!gnome && !desktopIs(desktop, "XFCE")) return;
    g_main_context_push_thread_default(impl_->context);
    if (gnome) {
        impl_->watch(impl_->enabled, gnomeInterface, gnomeAnimationsKey);
        impl_->watch(impl_->reduced, gnomeAccessibility, gnomeReducedMotion);
    } else impl_->watchXfce();
    g_main_context_pop_thread_default(impl_->context);
}
LinuxPreferenceMonitor::~LinuxPreferenceMonitor() = default;

bool LinuxPreferenceMonitor::changed() {
    // Run provider callbacks only after the native display has finished its poll
    // (Wayland may have a prepared display read outstanding inside poll).
    if (std::exchange(impl_->ready, false)) {
        g_main_context_acquire(impl_->context);
        g_main_context_dispatch(impl_->context);
        g_main_context_release(impl_->context);
    } else g_main_context_iteration(impl_->context, FALSE);
    return std::exchange(impl_->pending, false);
}

GMainContext* LinuxPreferenceMonitor::context() {
    // Desktop hosts created on one UI thread share a dispatch context.
    thread_local std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> threadContext(
        g_main_context_new(), g_main_context_unref);
    return threadContext.get();
}

void LinuxPreferenceMonitor::wake() { g_main_context_wakeup(impl_->context); }

int LinuxPreferenceMonitor::poll(std::span<pollfd> native, int timeout) {
    auto& monitor = *impl_;
    // The UI thread owns this context and integrates its sources with the native display poll.
    g_main_context_acquire(monitor.context);
    int priority = 0, wait = -1;
    g_main_context_prepare(monitor.context, &priority);
    int count;
    while ((count = g_main_context_query(monitor.context, priority, &wait, monitor.sources.data(),
                static_cast<int>(monitor.sources.size()))) > static_cast<int>(monitor.sources.size()))
        monitor.sources.resize(count);
    if (wait >= 0 && (timeout < 0 || wait < timeout)) timeout = wait;
    monitor.descriptors.assign(native.begin(), native.end());
    for (int index = 0; index < count; ++index) {
        const auto& source = monitor.sources[index];
        monitor.descriptors.push_back({source.fd, static_cast<short>(source.events), 0});
    }
    const int result = ::poll(monitor.descriptors.data(), monitor.descriptors.size(), timeout);
    const int error = errno;
    if (result >= 0) {
        std::copy_n(monitor.descriptors.begin(), native.size(), native.begin());
        for (int index = 0; index < count; ++index)
            monitor.sources[index].revents = monitor.descriptors[native.size() + index].revents;
        monitor.ready = g_main_context_check(monitor.context, priority, monitor.sources.data(), count);
    }
    g_main_context_release(monitor.context);
    errno = error;
    return result;
}
}
