#include "linux_failure_dialog.hpp"
#include <gtk/gtk.h>
#include <iostream>

int main(int argc, char** argv) {
    using namespace settingsDemo::recovery;
    if (argc != 4 || argv[1][0] < '0' || argv[1][0] > '2' || argv[1][1] != '\0') return 2;
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
