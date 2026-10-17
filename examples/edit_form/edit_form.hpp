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
    lcars::Id state, draft, result, count;                        // displayed results
};
Ids default_ids();

class Form {
public:
    explicit Form(Ids ids = default_ids());
    Form(const Form&) = delete;
    Form& operator=(const Form&) = delete;
    lcars::Ui ui;
    lcars::ScrollState scroll;  // page view state; reset returns it to the top
    lcars::layout::Node page() const;
    const Ids& ids() const { return ids_; }
    const std::u16string& applied() const { return applied_; }
    int applies() const { return applies_; }
private:
    enum class Mode { normal, read_only, disabled };
    Ids ids_;
    std::u16string applied_;
    int applies_ = 0;
    Mode mode_ = Mode::normal;
    bool long_intro_ = false;
    bool validate();
    lcars::Result apply();
    void set_mode(Mode mode);
    lcars::Result reset();
};
}
