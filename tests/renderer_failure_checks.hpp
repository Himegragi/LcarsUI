#pragma once
#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
inline bool renderer_failure_preserves_draft(const lcars::TextHandle& field, std::u16string_view expected) {
    return field.set_value(u"unexpected change").status == lcars::Status::renderer_failed &&
        field.draft().value == expected;
}
template<class ReadApplied>
inline bool renderer_failure_rejects_invoke(lcars::SemanticTree& tree, const lcars::NodeRef& ref, ReadApplied read_applied) {
    return tree.perform(ref, lcars::Verb::invoke).status == lcars::Status::renderer_failed && read_applied() == 0;
}
