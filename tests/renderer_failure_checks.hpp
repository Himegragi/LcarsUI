#pragma once
#include "lcars/core.hpp"
#include "lcars/semantics.hpp"
// The semantic Verb list retains confirm on a connected confirmed field while failed.
// Both seams check that focus, editing and selection actions are absent.
inline bool rendererFailureOmitsInteractiveVerbs(const lcars::ComponentSnapshot& snapshot) {
    return std::all_of(snapshot.verbs.begin(), snapshot.verbs.end(), [](lcars::Verb verb) { return verb == lcars::Verb::confirm; });
}
inline bool rendererFailurePreservesDraft(const lcars::TextFieldHandle& field, std::u16string_view expected) {
    return field.setValue(u"unexpected change").status == lcars::Status::rendererFailed &&
        field.draft().value == expected;
}
template<class ReadApplied>
inline bool rendererFailureRejectsInvoke(lcars::SemanticTree& tree, const lcars::NodeRef& ref, ReadApplied readApplied) {
    return tree.perform(ref, lcars::Verb::invoke).status == lcars::Status::rendererFailed && readApplied() == 0;
}
