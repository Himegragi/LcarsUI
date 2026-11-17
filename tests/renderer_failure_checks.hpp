#pragma once
#include "isoduo/core.hpp"
#include "isoduo/semantics.hpp"
// The semantic Verb list retains confirm on a connected confirmed field while failed.
// Both seams check that focus, editing and selection actions are absent.
inline bool rendererFailureOmitsInteractiveVerbs(const isoduo::ComponentSnapshot& snapshot) {
    return std::all_of(snapshot.verbs.begin(), snapshot.verbs.end(), [](isoduo::Verb verb) { return verb == isoduo::Verb::confirm; });
}
inline bool rendererFailurePreservesDraft(const isoduo::TextFieldHandle& field, std::u16string_view expected) {
    return field.setValue(u"unexpected change").status == isoduo::Status::rendererFailed &&
        field.draft().value == expected;
}
template<class ReadApplied>
inline bool rendererFailureRejectsInvoke(isoduo::SemanticTree& tree, const isoduo::NodeRef& ref, ReadApplied readApplied) {
    return tree.perform(ref, isoduo::Verb::invoke).status == isoduo::Status::rendererFailed && readApplied() == 0;
}
