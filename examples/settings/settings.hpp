#pragma once
#include "desktop.hpp"
#include <filesystem>
namespace settings {
struct Values {
    std::u16string name = u"星艦工作站";
    std::u16string workspace = u"我的專案";
    bool autosave = true, notifications = false, ming_font = false;
    int font_size = 18;
    bool operator==(const Values&) const = default;
};
lcars::Result read_file(const std::filesystem::path& path, Values& output);
lcars::Result write_file(const std::filesystem::path& path, const Values& values);
class App {
public:
    explicit App(std::filesystem::path file);
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    lcars::Ui ui;
    Screen screen() const;
    Values values() const;
    bool dirty() const { return values() != saved_; }
    lcars::Result save();
    lcars::Result undo();
    lcars::Result redo();
private:
    std::filesystem::path file_;
    Values saved_, previous_;
    std::vector<Values> undo_, redo_;
    std::string page_ = "general";
    bool ming_font_ = false;
    int font_size_ = 18;
    std::u16string status_ = u"設定已載入";
    enum class Dialog { none, defaults, discard } dialog_ = Dialog::none;
    void apply(const Values& values);
    void changed();
    void refresh_commands();
    lcars::Result defaults();
};
}
