#pragma once
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include <memory>
#include <map>

// V02 owns its model and can assemble into a standalone or a shared UI.
namespace list_page {
// This source changes only through its field's immediate modification entry.
// Reset uses the same connection, so there is no independent source update.
class ImmediateSearchConnection {
    std::u16string value_;
    std::function<void()> changed_;
    lcars::TextHandle field_;
    bool connected_ = false;
public:
    lcars::Result connect(lcars::TextHandle field, std::function<lcars::Result()> refresh);
    lcars::Result reset();
    const std::u16string& value() const { return value_; }
    bool owns(lcars::TextHandle field) const { return connected_ && field_ == field; }
};
class Page {
    std::vector<lcars::RadioOption> items_;
    std::optional<lcars::OptionValue> selection_;
    ImmediateSearchConnection search_;
    bool descending_ = false, disabled_eight_ = false;
    std::string clip_padding_id_;
    std::u16string clip_padding_text_;
    std::map<std::string, std::function<lcars::Result()>, std::less<>> fixture_actions_;
    lcars::Result bind_action(lcars::ButtonHandle button, std::function<lcars::Result()> action);
    std::function<void()> selection_changed_;
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
    mutable lcars::ScrollState list_scroll;
    struct Handles {
        lcars::ListHandle list;
        lcars::TextHandle search;
        lcars::ButtonHandle zero, one, twenty, hundred, sort, erase, disable_eight, reset;
        lcars::LabelHandle title, detail;
    } handles;
    lcars::layout::Node page() const;
    lcars::Result restore_fixture_action(lcars::ButtonHandle button) const;
    lcars::Result set_clip_padding(lcars::LabelHandle label, std::u16string text);
    void clear_clip_padding();
    std::optional<lcars::OptionValue> selection() const { return selection_; }
    const ImmediateSearchConnection& search_connection() const { return search_; }
};
}
