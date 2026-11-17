#pragma once
#include "lcars/core.hpp"
#include "lcars/layout.hpp"
#include <memory>
#include <map>

// V02 owns its model and can assemble into a standalone or a shared UI.
namespace listPage {
// This source changes only through its field's immediate modification entry.
// Reset uses the same connection, so there is no independent source update.
class ImmediateSearchConnection {
    std::u16string value_;
    std::function<void()> changed_;
    lcars::TextFieldHandle field_;
    bool connected_ = false;
public:
    lcars::Result connect(lcars::TextFieldHandle field, std::function<lcars::Result()> refresh);
    lcars::Result reset();
    const std::u16string& value() const { return value_; }
    bool owns(lcars::TextFieldHandle field) const { return connected_ && field_ == field; }
};
class Page {
    std::vector<lcars::OptionItem> items_;
    std::optional<lcars::OptionValue> selection_;
    ImmediateSearchConnection search_;
    bool descending_ = false, disabledEight_ = false;
    std::string clipPaddingId_;
    std::u16string clipPaddingText_;
    std::map<std::string, std::function<lcars::Result()>, std::less<>> fixtureActions_;
    lcars::Result bindAction(lcars::ButtonHandle button, std::function<lcars::Result()> action);
    std::function<void()> selectionChanged_;
    lcars::Result refresh();
    lcars::Result sample(int count);
    std::unique_ptr<lcars::Ui> ownedUi_;
    lcars::ScrollState ownedScroll_;
public:
    explicit Page(lcars::Ui* sharedUi = nullptr, lcars::ScrollState* sharedScroll = nullptr, std::string prefix = {});
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    lcars::Ui& ui;
    lcars::ScrollState& scroll;
    mutable lcars::ScrollState listScroll;
    struct Handles {
        lcars::ListHandle list;
        lcars::TextFieldHandle search;
        lcars::ButtonHandle zero, one, twenty, hundred, sort, erase, disableEight, reset;
        lcars::LabelHandle title, detail;
    } handles;
    lcars::layout::Node page() const;
    lcars::Result restoreFixtureAction(lcars::ButtonHandle button) const;
    lcars::Result setClipPadding(lcars::LabelHandle label, std::u16string text);
    void clearClipPadding();
    std::optional<lcars::OptionValue> selection() const { return selection_; }
    const ImmediateSearchConnection& searchConnection() const { return search_; }
};
}
