#pragma once
#include "isoduo/core.hpp"
#include "isoduo/layout.hpp"
#include <memory>
#include <map>

// V02 owns its model and can assemble into a standalone or a shared UI.
namespace listPage {
// This source changes only through its field's immediate modification entry.
// Reset uses the same connection, so there is no independent source update.
class ImmediateSearchConnection {
    std::u16string value_;
    std::function<void()> changed_;
    isoduo::TextFieldHandle field_;
    bool connected_ = false;
public:
    isoduo::Result connect(isoduo::TextFieldHandle field, std::function<isoduo::Result()> refresh);
    isoduo::Result reset();
    const std::u16string& value() const { return value_; }
    bool owns(isoduo::TextFieldHandle field) const { return connected_ && field_ == field; }
};
class Page {
    std::vector<isoduo::OptionItem> items_;
    std::optional<isoduo::OptionValue> selection_;
    ImmediateSearchConnection search_;
    bool descending_ = false, disabledEight_ = false;
    std::string clipPaddingId_;
    std::u16string clipPaddingText_;
    std::map<std::string, std::function<isoduo::Result()>, std::less<>> fixtureActions_;
    isoduo::Result bindAction(isoduo::ButtonHandle button, std::function<isoduo::Result()> action);
    std::function<void()> selectionChanged_;
    isoduo::Result refresh();
    isoduo::Result sample(int count);
    std::unique_ptr<isoduo::Ui> ownedUi_;
    isoduo::ScrollState ownedScroll_;
public:
    explicit Page(isoduo::Ui* sharedUi = nullptr, isoduo::ScrollState* sharedScroll = nullptr, std::string prefix = {});
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    isoduo::Ui& ui;
    isoduo::ScrollState& scroll;
    mutable isoduo::ScrollState listScroll;
    struct Handles {
        isoduo::ListHandle list;
        isoduo::TextFieldHandle search;
        isoduo::ButtonHandle zero, one, twenty, hundred, sort, erase, disableEight, reset;
        isoduo::LabelHandle title, detail;
    } handles;
    isoduo::layout::Node page() const;
    isoduo::Result restoreFixtureAction(isoduo::ButtonHandle button) const;
    isoduo::Result setClipPadding(isoduo::LabelHandle label, std::u16string text);
    void clearClipPadding();
    std::optional<isoduo::OptionValue> selection() const { return selection_; }
    const ImmediateSearchConnection& searchConnection() const { return search_; }
};
}
