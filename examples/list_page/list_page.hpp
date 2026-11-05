#pragma once
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include <memory>

// V02 owns its model and can assemble into a standalone or a shared UI.
namespace list_page {
class Page {
    std::vector<lcars::RadioOption> items_;
    std::optional<lcars::OptionValue> selection_;
    std::u16string search_;
    bool descending_ = false, disabled_eight_ = false;
    std::function<void()> search_changed_, selection_changed_;
    lcars::Result refresh();
    lcars::Result sample(int count);
    std::unique_ptr<lcars::Ui> owned_ui_;
    lcars::ScrollState owned_scroll_;
public:
    explicit Page(lcars::Ui* shared_ui = nullptr, lcars::ScrollState* shared_scroll = nullptr, std::string prefix = {});
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    lcars::Ui& ui;
    lcars::ScrollState& scroll;
    struct Handles {
        lcars::ListHandle list;
        lcars::TextHandle search;
        lcars::ButtonHandle zero, one, twenty, hundred, sort, erase, disable_eight, reset;
        lcars::LabelHandle title, detail;
    } handles;
    lcars::layout::Node page() const;
    std::optional<lcars::OptionValue> selection() const { return selection_; }
};
}
