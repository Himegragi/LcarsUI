#pragma once
// V01 minimal edit form. Application code: it owns the data, the validation policy and the
// apply action, and assembles C01-C03 only through public LcarsUI headers. Logical IDs come
// from the caller so the framework never depends on these names.
#include "lcars/core.hpp"
#include "lcars/layout.hpp"

namespace edit_form {
struct Ids {
    lcars::Id title, intro;
    lcars::Id normal, read_only, disabled, error, long_text, reset;  // recoverable test entries
    lcars::Id name_label, name, name_help, name_error, apply;
    lcars::Id state, draft, result, count, latest;                        // displayed results
};
Ids default_ids();

class Form {
private:
    enum class Mode { normal, read_only, disabled };
    Ids ids_;
    std::u16string applied_;
    int applies_ = 0;
    Mode mode_ = Mode::normal;
    bool long_intro_ = false;
    std::function<void()> source_changed_;
public:
    explicit Form(Ids ids = default_ids());
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::layout::Node page() const;
    const Ids& ids() const { return ids_; }
    const std::u16string& applied() const { return applied_; }
    int applies() const { return applies_; }
    // An external application change to the same model (also useful to exercise conflicts).
    void update_source(std::u16string value);
private:
    lcars::TextHandle name_;
    lcars::ButtonHandle apply_, latest_, reset_;
    bool validate();
    lcars::ConfirmationResult apply(const std::vector<lcars::TextChange>& changes);
    void refresh_conflict();
    void set_mode(Mode mode);
    lcars::Result reset();
};}
