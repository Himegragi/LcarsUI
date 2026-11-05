#pragma once
#include "lcars/core.hpp"
#include "lcars/layout.hpp"

// Standalone V02: state belongs to this page, so a later tab shell can retain it unchanged.
namespace list_page {
class Page {
    std::vector<lcars::RadioOption> items_;
    std::optional<lcars::OptionValue> selection_;
    std::u16string search_;
    bool descending_ = false, disabled_eight_ = false;
    std::function<void()> search_changed_, selection_changed_;
    lcars::Result refresh();
    lcars::Result sample(int count);
public:
    Page();
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    struct Handles {
        lcars::ListHandle list;
        lcars::TextHandle search;
        lcars::ButtonHandle zero, one, twenty, hundred, sort, erase, disable_eight, reset;
        lcars::LabelHandle title, detail;
    } handles;
    lcars::Ui ui;
    lcars::ScrollState scroll;
    lcars::layout::Node page() const;
    std::optional<lcars::OptionValue> selection() const { return selection_; }
};
}
