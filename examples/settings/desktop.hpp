#pragma once
#include "lcars/core.hpp"
#include "lcars/appearance.hpp"
#include <filesystem>
namespace settings {
struct Screen {
    std::string page = "general";
    std::u16string heading, description, status;
    std::u16string dialog_title, dialog_message;
    int font_size = 18;
    bool ming_font = false;
    bool dirty = false;
};
struct DesktopOptions {
    bool smoke_test = false;
    std::filesystem::path screenshot;
};
// Settings demo shell. Page layout and command names belong to the example.
int run_desktop(lcars::Ui& ui, const lcars::Appearance& appearance,
                std::function<Screen()> screen, DesktopOptions options = {});
}
