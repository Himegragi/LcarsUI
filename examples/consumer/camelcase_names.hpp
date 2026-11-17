#pragma once
#include "isoduo/core.hpp"
#include "isoduo/semantics.hpp"
#include "isoduo/version.hpp"
#include <concepts>

// #105: the installed public API has the new spellings and no old aliases.
// The snake_case tokens below are deliberate negative lookup probes.
namespace camelcaseNames {
template<class T> concept SnakeCreation = requires { &T::add_text_field; } || requires { &T::add_radio_group; } ||
    requires { &T::add_navigation_list; } || requires { &T::connect_confirmation; };
static_assert(!SnakeCreation<isoduo::Ui>);
template<class T> concept SnakeState = requires(T s) { s.read_only; } || requires(T s) { s.blocked_by_modal; } ||
    requires(T s) { s.numeric_value; } || requires(T s) { s.text_align; } || requires(T s) { s.typing_style; } ||
    requires(T s) { s.word_segmentation; } || requires(T s) { s.writing_mode; } || requires(T s) { s.vertical_digits; };
static_assert(!SnakeState<isoduo::ComponentConfig> && !SnakeState<isoduo::CommonState> &&
              !SnakeState<isoduo::ComponentSnapshot> && !SnakeState<isoduo::NodeSnapshot>);
template<class T> concept SnakeTextField = requires { &T::set_value; } || requires { &T::read_only; } ||
    requires { &T::connect_source; } || requires { &T::source_conflict; } || requires { &T::set_visible_lines; };
static_assert(!SnakeTextField<isoduo::TextFieldHandle>);
template<class T> concept SnakeEnumerators = requires { T::text_field; } || requires { T::radio_group; } ||
    requires { T::not_found; } || requires { T::read_only; } || requires { T::property_changed; } ||
    requires { T::set_value; };
static_assert(!SnakeEnumerators<isoduo::ComponentKind> && !SnakeEnumerators<isoduo::Role> &&
              !SnakeEnumerators<isoduo::Status> && !SnakeEnumerators<isoduo::SemanticEventKind> && !SnakeEnumerators<isoduo::Verb>);
static_assert(std::same_as<decltype(isoduo::CommonState::readOnly), bool> &&
              std::same_as<decltype(isoduo::CommonState::numericValue), std::optional<int>>);
static_assert(isoduo::ComponentKind::radioGroup != isoduo::ComponentKind::textField &&
              isoduo::Status::notFound != isoduo::Status::readOnly);
static_assert(isoduo::defaultMaxTextLength == 4096 && std::string_view(isoduo::menuUndo) == "isoduo.undo");
}

// As with the earlier removed-type probes, unqualified lookup finds these global
// stand-ins only when no old declaration exists in isoduo.
namespace removedBy105 { struct Name {}; }
inline removedBy105::Name default_max_text_length, menu_undo, framework_version;
namespace isoduo::consumerProbe {
static_assert(std::same_as<decltype(default_max_text_length), ::removedBy105::Name> &&
              std::same_as<decltype(menu_undo), ::removedBy105::Name> &&
              std::same_as<decltype(framework_version), ::removedBy105::Name>);
}
