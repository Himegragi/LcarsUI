#pragma once
#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
// The semantic Verb list retains confirm on a connected confirmed field while failed.
// Both seams check that focus, editing and selection actions are absent.
inline bool renderer_failure_omits_interactive_verbs(const lcars::ComponentSnapshot& snapshot) {
    return std::all_of(snapshot.verbs.begin(), snapshot.verbs.end(), [](lcars::Verb verb) { return verb == lcars::Verb::confirm; });
}
inline bool renderer_failure_preserves_draft(const lcars::TextFieldHandle& field, std::u16string_view expected) {
    return field.setValue(u"unexpected change").status == lcars::Status::rendererFailed &&
        field.draft().value == expected;
}
template<class ReadApplied>
inline bool renderer_failure_rejects_invoke(lcars::SemanticTree& tree, const lcars::NodeRef& ref, ReadApplied read_applied) {
    return tree.perform(ref, lcars::Verb::invoke).status == lcars::Status::rendererFailed && read_applied() == 0;
}
