#include "linux_failure_dialog.hpp"
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <cstdlib>
#include <iostream>

namespace {
// Keeps the dialog above the demo's main window: on X11 the window manager stacks a transient window over
// its parent. Another client's Wayland surface cannot be a parent, so there the dialog stays unparented.
void attachToParent(GtkWidget* dialog, const char* parent) {
#ifdef GDK_WINDOWING_X11
    auto* display = gdk_display_get_default();
    if (!parent || !GDK_IS_X11_DISPLAY(display)) return;
    char* end = nullptr;
    const auto xid = std::strtoul(parent, &end, 10);
    if (!xid || !end || *end) return;
    auto* foreign = gdk_x11_window_foreign_new_for_display(display, xid);
    if (!foreign) {
        std::cerr << "Renderer recovery parent window " << parent << " is unavailable\n";
        return;
    }
    gtk_widget_realize(dialog);
    gdk_window_set_transient_for(gtk_widget_get_window(dialog), foreign);
    g_object_set_data_full(G_OBJECT(dialog), "isoduo-parent", foreign, g_object_unref);
#else
    (void)dialog;
    (void)parent;
#endif
}
}

int main(int argc, char** argv) {
    using namespace settingsDemo::recovery;
    // step, title, message and, on X11, the parent window's XID
    if ((argc != 4 && argc != 5) || argv[1][0] < '0' || argv[1][0] > '2' || argv[1][1] != '\0') return 2;
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << "Cannot initialize GTK for renderer recovery\n";
        return 2;
    }
    const auto step = static_cast<Step>(argv[1][0] - '0');
    auto* dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), argv[2]);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    // Client-side decorations can load theme images through external helpers.
    // Keep recovery usable even when the PATH that broke Zenity is incomplete.
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 8);
    auto* title = gtk_label_new(argv[2]);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(content), title);
    auto* message = gtk_label_new(argv[3]);
    gtk_label_set_line_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(message), 48);
    gtk_container_add(GTK_CONTAINER(content), message);
    gtk_widget_show_all(content);
    const auto defaultChoice = step == Step::retry ? Choice::retry :
                               step == Step::save ? Choice::cancel : Choice::acknowledge;
    for (auto button : buttons(step)) {
        auto* widget = gtk_dialog_add_button(GTK_DIALOG(dialog), button.label, static_cast<int>(button.choice));
        if (button.choice == defaultChoice) gtk_widget_grab_focus(widget);
    }
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), static_cast<int>(defaultChoice));
    attachToParent(dialog, argc == 5 ? argv[4] : nullptr);
    const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    for (auto button : buttons(step)) {
        if (response == static_cast<int>(button.choice)) {
            std::cout << button.label << '\n';
            return 0;
        }
    }
    if (response == GTK_RESPONSE_DELETE_EVENT) {
        std::cout << dismissedToken << '\n';
        return 0;
    }
    return 2;
}
