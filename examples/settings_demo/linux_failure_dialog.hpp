#pragma once
#include <span>
#include <string_view>

namespace settingsDemo::recovery {
enum class Step { retry, save, saveError };
enum class Choice { retry = 1, quit, cancel, discard, save, acknowledge, dismissed };

struct Button {
    Choice choice;
    const char* label;
};

inline constexpr char helperName[] = "isoduo_settings_recovery_dialog";
inline constexpr std::string_view dismissedToken = "dismissed";
inline constexpr Button retryButtons[] = {{Choice::quit, "結束"}, {Choice::retry, "重試"}};
inline constexpr Button saveButtons[] = {{Choice::cancel, "取消"}, {Choice::discard, "捨棄"}, {Choice::save, "儲存"}};
inline constexpr Button errorButtons[] = {{Choice::acknowledge, "確定"}};

inline std::span<const Button> buttons(Step step) {
    switch (step) {
    case Step::retry: return retryButtons;
    case Step::save: return saveButtons;
    case Step::saveError: return errorButtons;
    }
    return {};
}
}
