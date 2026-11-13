#pragma once
// The typed handle kinds and the compile-time properties they share (#94): every kind compares with itself (equal when
// two handles control the same thing), no two kinds compare with each other (a compile error, not `false`), and a user
// makes only empty handles. One list and one predicate per property, asserted by this consumer against the installed
// package and by tests/handles.cpp.
// A new handle kind is one more type in `handle_kinds::All`.
#include "lcars/core.hpp"
#include <concepts>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>

namespace handle_kinds {
using All = std::tuple<lcars::TextHandle, lcars::ButtonHandle, lcars::LabelHandle, lcars::CheckboxHandle, lcars::RadioGroupHandle,
                       lcars::SliderHandle, lcars::ProgressHandle, lcars::DropdownHandle, lcars::TabsHandle, lcars::NavigationListHandle,
                       lcars::ListHandle, lcars::DialogHandle, lcars::TooltipHandle, lcars::ContextMenuHandle>;
template <class A, class... H> constexpr bool compares_only_with_itself = ((std::is_same_v<A, H> || !std::equality_comparable_with<A, H>) && ...);
// Every kind in the list has operator== and compares with no other kind in the list.
template <class... H> constexpr bool compare_within_their_kind_only(std::tuple<H...>*) {
    return ((std::equality_comparable<H> && compares_only_with_itself<H, H...>) && ...);
}
inline constexpr bool equality_holds = compare_within_their_kind_only(static_cast<All*>(nullptr));
// A user makes only default (empty) handles; a real one comes from the framework, whose constructor stays private.
template <class... H> constexpr bool made_only_by_the_framework(std::tuple<H...>*) {
    return ((std::is_default_constructible_v<H> && !std::is_constructible_v<H, lcars::detail::UiState*, std::weak_ptr<void>, std::string>) && ...);
}
inline constexpr bool construction_holds = made_only_by_the_framework(static_cast<All*>(nullptr));
}
