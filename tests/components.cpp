#include "core_internal.hpp"
#include "component_type.hpp"
// C01-C03 behavior through the public core interface only; no window, GPU or font backend.
#include "check.hpp"
#include "isoduo/core.hpp"
#include <thread>
using namespace isoduo;
using test::check;

namespace {
// Application-owned source fixtures give legacy host input tests explicit write timing.
void populate(Ui& ui, std::vector<detail::Component> controls) {
    for (auto& control : controls) {
        if (control.kind == ComponentKind::textField) {
            auto model = std::make_shared<std::u16string>(control.value);
            auto field = ui.addTextField(std::move(control)).value.value();
            check(bool(field.connectSource({[model] { return *model; }, [](std::function<void()>) { return [] {}; }})), "Connect fixture source");
            check(bool(field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; })), "Connect fixture write");
        } else if (control.kind == ComponentKind::button) ui.addButton(std::move(control)).value.value();
        else ui.addLabel(std::move(control)).value.value();
    }
}
std::vector<detail::Component> nameForm() {
    return {
        {ComponentKind::label, {.id = "name.label", .value = u"顯示名稱", .target = "name", .labelKind = LabelKind::name}},
        {ComponentKind::textField, {.id = "name", .value = u"測試使用者"}},
        {ComponentKind::label, {.id = "name.help", .value = u"單行文字", .target = "name", .labelKind = LabelKind::description}},
        {ComponentKind::label, {.id = "name.error", .target = "name", .labelKind = LabelKind::error}},
        {ComponentKind::label, {.id = "note", .value = u"第一行\n第二行"}},
    };
}

void labelsAssociateByTargetId() {
    Ui ui; populate(ui, nameForm());
    auto field = detail::access(ui).describe("name");
    check(field && field->name == u"顯示名稱", "Field name comes from its name label");
    check(field->description == u"單行文字", "Description label is queryable from the field");
    check(field->labels == std::vector<Id>{"name.label", "name.help", "name.error"}, "Field lists every associated label");
    check(!field->invalid && field->error.empty(), "No error initially");
    auto error = detail::access(ui).describe("name.error");
    check(error && error->target == "name" && error->labelKind == LabelKind::error, "Error label points at its field");
    check(detail::access(ui).describe("note")->value == u"第一行\n第二行", "Plain labels keep multi-line display text");
}

void fieldErrorHasOneSource() {
    Ui ui; populate(ui, nameForm());
    check(bool(detail::access(ui).setError("name", u"請填寫顯示名稱")), "Owner sets a field error");
    auto field = detail::access(ui).describe("name");
    check(field->invalid && field->error == u"請填寫顯示名稱", "Field reports invalid with its message");
    check(detail::access(ui).text("name.error") == u"請填寫顯示名稱", "Error label displays the field error");
    check(detail::access(ui).setError("note", u"x").status == Status::notSupported, "Labels cannot carry errors");
    check(detail::access(ui).setError("missing", u"x").status == Status::notFound, "Unknown target is reported");
    check(bool(detail::access(ui).setError("name", u"")) && !detail::access(ui).describe("name")->invalid && detail::access(ui).text("name.error").empty(),
          "Empty message clears the error and its label");
}

std::vector<detail::Component> formWithButtons() {
    auto controls = nameForm();
    controls.push_back({ComponentKind::button, {.id = "apply", .label = u"套用"}});
    controls.push_back({ComponentKind::button, {.id = "reset", .label = u"重設"}});
    return controls;
}
KeyPress press(Key key, bool shift = false) { return {.key = key, .shift = shift}; }

void tabOrderSkipsLabelsAndDisabled() {
    Ui ui; populate(ui, formWithButtons());
    check(detail::access(ui).focused().empty(), "Nothing focused initially");
    std::vector<Id> forward;
    for (int i = 0; i < 4; ++i) { detail::access(ui).keyDown(press(Key::tab)); forward.push_back(detail::access(ui).focused()); }
    check(forward == std::vector<Id>{"name", "apply", "reset", "name"}, "Tab walks fields and buttons, wraps, skips labels");
    detail::access(ui).keyDown(press(Key::tab, true));
    check(detail::access(ui).focused() == "reset", "Shift+Tab walks backwards and wraps");
    check(detail::access(ui).describe("reset")->focused && !detail::access(ui).describe("name")->focused, "Focus is queryable");
    detail::access(ui).enable("apply", false);
    detail::access(ui).focus("name");
    detail::access(ui).keyDown(press(Key::tab));
    check(detail::access(ui).focused() == "reset", "Disabled button leaves the Tab order");
    check(detail::access(ui).focus("apply").status == Status::disabled && detail::access(ui).focused() == "reset", "Disabled control refuses focus");
    check(detail::access(ui).focus("note").status == Status::notSupported, "Labels never take focus");
    check(detail::access(ui).focus("missing").status == Status::notFound, "Unknown focus target is reported");
}

void buttonActivatesOncePerPress() {
    Ui ui; populate(ui, formWithButtons());
    int applies = 0, resets = 0;
    detail::access(ui).bind("apply", [&] { ++applies; return Result{}; });
    detail::access(ui).bind("reset", [&] { ++resets; return Result{}; });
    detail::access(ui).focus("apply");
    detail::access(ui).keyDown(press(Key::enter));
    detail::access(ui).keyDown({.key = Key::enter, .repeat = true});
    detail::access(ui).keyUp(Key::enter);
    check(applies == 1, "Enter activates on press; auto-repeat does not repeat it");
    detail::access(ui).keyDown(press(Key::space));
    detail::access(ui).keyDown({.key = Key::space, .repeat = true});
    check(applies == 1, "Space arms on press");
    detail::access(ui).keyUp(Key::space);
    detail::access(ui).keyUp(Key::space);
    check(applies == 2, "Space activates once on release; a stray release does nothing");
    detail::access(ui).pointerDown("apply");
    detail::access(ui).pointerUp("reset");
    check(applies == 2 && resets == 0, "Pointer released outside cancels and does not activate the other button");
    detail::access(ui).pointerDown("apply");
    detail::access(ui).pointerUp("apply");
    check(applies == 3, "Pointer pressed and released inside activates once");
    detail::access(ui).keyDown(press(Key::space));
    detail::access(ui).cancelInput();
    detail::access(ui).keyUp(Key::space);
    detail::access(ui).pointerDown("apply");
    detail::access(ui).cancelInput();
    detail::access(ui).pointerUp("apply");
    check(applies == 3, "Window deactivation or capture loss leaves nothing armed");
    detail::access(ui).keyDown(press(Key::space));
    detail::access(ui).focus("reset");
    detail::access(ui).keyUp(Key::space);
    check(applies == 3 && resets == 0, "Moving focus drops the armed press; the release is not delivered to the new focus");
    detail::access(ui).enable("apply", false);
    check(detail::access(ui).pointerDown("apply").status == Status::disabled, "Disabled button refuses pointer press");
    detail::access(ui).pointerUp("apply");
    check(detail::access(ui).invoke("apply").status == Status::disabled && applies == 3, "Disabled button never activates");
}

KeyPress ctrl(char16_t letter, bool shift = false) { return {.shift = shift, .ctrl = true, .letter = letter}; }
std::vector<detail::Component> twoFields() {
    auto controls = formWithButtons();
    controls.push_back({ComponentKind::textField, {.id = "alias", .value = u"別名"}});
    return controls;
}

void typingRecordsOneHistoryEntryPerInput() {
    Ui ui; populate(ui, twoFields());
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 5, 5);
    for (auto ch : std::u16string(u"AB")) detail::access(ui).textInput(std::u16string(1, ch));
    detail::access(ui).textInput(u"\r");
    check(detail::access(ui).text("name") == u"測試使用者AB", "Committed characters insert at the caret; control characters are ignored");
    detail::access(ui).focus("alias");
    detail::access(ui).textInput(u"X");
    detail::access(ui).focus("name");
    detail::access(ui).keyDown(ctrl(u'Z'));
    check(detail::access(ui).text("name") == u"測試使用者A" && detail::access(ui).text("alias") == u"X別名", "Ctrl+Z undoes one input of the focused field only");
    detail::access(ui).keyDown(ctrl(u'Z', true));
    check(detail::access(ui).text("name") == u"測試使用者AB", "Ctrl+Shift+Z redoes");
    detail::access(ui).keyDown(ctrl(u'Z'));
    detail::access(ui).keyDown(ctrl(u'Z'));
    detail::access(ui).keyDown(ctrl(u'Y'));
    check(detail::access(ui).text("name") == u"測試使用者A", "Ctrl+Y redoes");
    detail::access(ui).textInput(u"C");
    check(detail::access(ui).redo("name").status == Status::emptyHistory && detail::access(ui).text("name") == u"測試使用者AC", "A new edit clears the field's redo");
    check(detail::access(ui).undo("alias") && detail::access(ui).text("alias") == u"別名", "Each field keeps its own history");
}

Selection selection(Ui& ui, std::string_view id) { return detail::access(ui).edit(id)->selection; }
bool at(Ui& ui, unsigned anchor, unsigned caret) {
    auto s = selection(ui, detail::access(ui).focused());
    return s.anchor == anchor && s.caret == caret;
}

void caretMovesByCodePointAndExtendsWithShift() {
    // "A", U+20000 (two UTF-16 units), "B", "中": UTF-16 length 5.
    Ui ui; populate(ui, {{ComponentKind::textField, {.id = "t", .value = u"A\U00020000B中"}}});
    detail::access(ui).focus("t");
    detail::access(ui).keyDown(press(Key::home));
    check(at(ui, 0, 0), "Home puts the caret at the start");
    detail::access(ui).keyDown(press(Key::right));
    detail::access(ui).keyDown(press(Key::right));
    check(at(ui, 3, 3), "Right steps over a surrogate pair as one character");
    detail::access(ui).keyDown(press(Key::right, true));
    detail::access(ui).keyDown(press(Key::end, true));
    check(at(ui, 3, 5), "Shift extends from the anchor; Shift+End reaches the end");
    detail::access(ui).keyDown(press(Key::left));
    check(at(ui, 3, 3), "Left without Shift collapses to the selection start");
    detail::access(ui).keyDown(press(Key::backspace));
    check(detail::access(ui).text("t") == u"AB中" && at(ui, 1, 1), "Backspace removes the whole surrogate pair");
    detail::access(ui).keyDown(press(Key::del));
    check(detail::access(ui).text("t") == u"A中", "Delete removes the next character");
    detail::access(ui).keyDown(ctrl(u'A'));
    check(at(ui, 0, 2), "Ctrl+A selects all");
    detail::access(ui).keyDown(press(Key::backspace));
    check(detail::access(ui).text("t").empty(), "Backspace removes a selection");
    detail::access(ui).undo("t"); detail::access(ui).undo("t"); detail::access(ui).undo("t");
    check(detail::access(ui).text("t") == u"A\U00020000B中", "Each deletion was one undo entry");
    check(detail::access(ui).select("t", 2, 2).status == Status::invalidValue, "Selection inside a surrogate pair is rejected");
}

struct MemoryClipboard final : Clipboard {
    std::optional<std::u16string> content;
    bool failRead = false;
    bool write(std::u16string_view text) override { content = std::u16string(text); return true; }
    Query<std::u16string> read() override {
        if (failRead) return {Status::failed, u"Clipboard unavailable", std::nullopt};
        if (!content) return {Status::notSupported, u"Clipboard has no text", std::nullopt};
        return {Status::ok, {}, content};
    }
};

void clipboardReportsUnavailableSeparatelyFromEmpty() {
    Ui ui; populate(ui, nameForm());
    MemoryClipboard clipboard;
    auto& host = detail::access(ui);
    host.setClipboard(&clipboard); host.focus("name");
    check(host.keyDown(ctrl(u'V')).status == Status::notSupported, "An open clipboard with no text is reported as empty");
    clipboard.failRead = true;
    check(host.keyDown(ctrl(u'V')).status == Status::failed && host.text("name") == u"\u6e2c\u8a66\u4f7f\u7528\u8005",
          "A clipboard that cannot be opened reports failure without editing the draft");
}

void pasteTurnsEachLineBreakIntoOneSpace() {
    Ui ui; populate(ui, nameForm());
    MemoryClipboard clipboard;
    detail::access(ui).setClipboard(&clipboard);
    detail::access(ui).focus("name");
    clipboard.content = u"Alpha\r\nBeta";
    detail::access(ui).keyDown(ctrl(u'A'));
    detail::access(ui).keyDown(ctrl(u'V'));
    check(detail::access(ui).text("name") == u"Alpha Beta", "CRLF pastes as one space");
    detail::access(ui).keyDown(ctrl(u'Z'));
    check(detail::access(ui).text("name") == u"測試使用者", "Select-all plus paste is one undo entry");
    detail::access(ui).keyDown(ctrl(u'Y'));
    check(detail::access(ui).text("name") == u"Alpha Beta", "Redo restores the paste");
    check(detail::access(ui).paste("name", u"a\rb\nc\r\n\nd") && detail::access(ui).text("name") == u"Alpha Betaa b c  d",
          "Lone CR and LF each become one space; semantic paste inserts at the caret");
    detail::access(ui).select("name", 0, 5);
    detail::access(ui).keyDown(ctrl(u'C'));
    check(clipboard.content == u"Alpha" && detail::access(ui).text("name") == u"Alpha Betaa b c  d", "Copy writes the selection only");
    detail::access(ui).keyDown(ctrl(u'X'));
    check(clipboard.content == u"Alpha" && detail::access(ui).text("name") == u" Betaa b c  d", "Cut writes then removes the selection");
    detail::access(ui).undo("name");
    check(detail::access(ui).text("name") == u"Alpha Betaa b c  d", "Cut is one undo entry");
    check(detail::access(ui).paste("name", u"x\ty").status == Status::invalidValue && detail::access(ui).text("name") == u"Alpha Betaa b c  d",
          "Other control characters reject the whole paste and keep the text");
}

void readOnlyAllowsViewingAndCopyingOnly() {
    Ui ui; populate(ui, formWithButtons());
    MemoryClipboard clipboard;
    detail::access(ui).setClipboard(&clipboard);
    detail::access(ui).focus("name");
    detail::access(ui).textInput(u"!");
    detail::access(ui).setReadOnly("name", true);
    check(detail::access(ui).describe("name")->readOnly && detail::access(ui).focus("name"), "Read-only field stays focusable and reports its state");
    detail::access(ui).keyDown(ctrl(u'A'));
    detail::access(ui).keyDown(ctrl(u'C'));
    check(clipboard.content == u"!測試使用者", "Read-only text can be selected and copied");
    clipboard.content = u"貼上";
    std::vector<Status> refused;
    refused.push_back(detail::access(ui).textInput(u"Y").status);
    refused.push_back(detail::access(ui).keyDown(ctrl(u'X')).status);
    refused.push_back(detail::access(ui).keyDown(ctrl(u'V')).status);
    refused.push_back(detail::access(ui).keyDown(press(Key::backspace)).status);
    refused.push_back(detail::access(ui).keyDown(ctrl(u'Z')).status);
    refused.push_back(detail::access(ui).setValue("name", u"semantic").status);
    check(refused == std::vector<Status>(6, Status::readOnly), "Every modifying path reports read_only");
    check(detail::access(ui).text("name") == u"!測試使用者" && clipboard.content == u"貼上", "Text and clipboard are untouched");
    detail::access(ui).setReadOnly("name", false);
    check(detail::access(ui).undo("name") && detail::access(ui).text("name") == u"測試使用者", "History position survived the read-only period");
}

void disabledFieldRefusesEveryEntryAndKeepsData() {
    Ui ui; populate(ui, formWithButtons());
    int applies = 0;
    detail::access(ui).bind("apply", [&] { ++applies; return Result{}; });
    detail::access(ui).focus("name");
    detail::access(ui).textInput(u"!");
    detail::access(ui).enable("name", false);
    detail::access(ui).enable("apply", false);
    check(detail::access(ui).focused().empty(), "Disabling the focused control releases focus");
    std::vector<Status> refused{
        detail::access(ui).focus("name").status, detail::access(ui).pointerDown("name").status, detail::access(ui).select("name", 0, 1).status,
        detail::access(ui).copy("name").status, detail::access(ui).paste("name", u"x").status, detail::access(ui).undo("name").status,
        detail::access(ui).setValue("name", u"x").status, detail::access(ui).invoke("apply").status, detail::access(ui).pointerDown("apply").status};
    check(refused == std::vector<Status>(refused.size(), Status::disabled), "Pointer, keyboard and semantic entries all refuse");
    check(detail::access(ui).focusOrder() == std::vector<Id>{"reset"}, "Only the recovery entry stays in the Tab order");
    detail::access(ui).enable("name", true);
    detail::access(ui).enable("apply", true);
    check(detail::access(ui).text("name") == u"!測試使用者" && applies == 0 && detail::access(ui).undo("name"), "Re-enabling keeps text and history");
}

void compositionCommitsOnceAndNeverApplies() {
    Ui ui; populate(ui, formWithButtons());
    int applies = 0;
    detail::access(ui).bind("apply", [&] { ++applies; return Result{}; });
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 5, 5);
    detail::access(ui).compositionUpdate(u"ㄨㄛˇ", 3);
    check(detail::access(ui).edit("name")->visible() == u"測試使用者ㄨㄛˇ" && detail::access(ui).text("name") == u"測試使用者",
          "Preedit is shown but not committed");
    check(detail::access(ui).keyDown(ctrl(u'Z')).status == Status::busy && detail::access(ui).keyDown(press(Key::backspace)).status == Status::busy,
          "Editing keys belong to the IME while composing");
    check(detail::access(ui).invoke("apply").status == Status::busy && applies == 0, "No command runs while composing");
    check(detail::access(ui).select("name", 0, 0).status == Status::busy && detail::access(ui).edit("name")->composing,
          "Moving the selection cannot silently drop a composition the IME still holds");
    detail::access(ui).compositionUpdate(u"我", 1);
    detail::access(ui).compositionCommit(u"我");
    detail::access(ui).keyDown(press(Key::enter));
    check(detail::access(ui).text("name") == u"測試使用者我" && !detail::access(ui).edit("name")->composing && applies == 0,
          "Commit inserts once; neither commit nor a later Enter applies");
    detail::access(ui).undo("name");
    check(detail::access(ui).text("name") == u"測試使用者", "One commit is one undo entry");
    detail::access(ui).select("name", 0, 2);
    detail::access(ui).compositionUpdate(u"工作", 2);
    detail::access(ui).compositionCommit(u"工作");
    check(detail::access(ui).text("name") == u"工作使用者", "Commit replaces the selection");
    detail::access(ui).undo("name");
    detail::access(ui).compositionUpdate(u"ㄅ", 1);
    detail::access(ui).compositionCancel();
    check(detail::access(ui).text("name") == u"測試使用者" && detail::access(ui).redo("name") && detail::access(ui).text("name") == u"工作使用者",
          "Cancel keeps committed text and history, including redo");
    detail::access(ui).select("name", 5, 5);
    detail::access(ui).compositionUpdate(u"我", 1);
    detail::access(ui).focus("apply");
    check(detail::access(ui).text("name") == u"工作使用者我" && !detail::access(ui).edit("name")->composing && applies == 0,
          "E01: focus leaving commits the preedit once, the same default the native IME showed");
    detail::access(ui).focus("name");
    detail::access(ui).undo("name");
    check(detail::access(ui).text("name") == u"工作使用者", "The blur commit is one undo entry");
    detail::access(ui).focus("apply");
    check(detail::access(ui).compositionCommit(u"遲到").status == Status::invalidValue && detail::access(ui).text("name") == u"工作使用者",
          "A commit with no composition and no focused field lands nowhere");
}

void compositionPageSwitchPointer() {
    Ui ui; populate(ui, formWithButtons());
    auto tabs = ui.addTabs({.id = "tabs"}, {
        {{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}}).value.value();
    auto& host = detail::access(ui);
    host.focus("name"); host.select("name", 5, 5);
    host.compositionUpdate(u"\u6211", 1);
    check(bool(host.pointerDown("tabs", 1)) && bool(host.pointerUp("tabs", 1)), "Tab pointer press succeeds during composition");
    check(*tabs.active() == OptionValue{"two"} && tabs.describe()->focused &&
          host.text("name") == u"\u6e2c\u8a66\u4f7f\u7528\u8005\u6211" && !host.edit("name")->composing,
          "Tab click commits composition once and switches page");
    host.focus("name"); host.undo("name");
    check(host.text("name") == u"\u6e2c\u8a66\u4f7f\u7528\u8005", "Composition is one undo entry");
}

void focusNotificationsQueueInsteadOfRecursing() {
    Ui ui; populate(ui, formWithButtons());
    std::vector<std::pair<Id, Id>> heard;
    int depth = 0, deepest = 0;
    detail::access(ui).onFocus([&](const Id& from, const Id& to) {
        deepest = std::max(deepest, ++depth);
        heard.emplace_back(from, to);
        if (to == "apply") check(bool(detail::access(ui).focus("reset")) && detail::access(ui).focused() == "reset", "A write inside the callback is readable at once");
        --depth;
    });
    detail::access(ui).focus("apply");
    check(deepest == 1, "The focus callback is never re-entered");
    check((heard == std::vector<std::pair<Id, Id>>{{"", "apply"}, {"apply", "reset"}}) && detail::access(ui).focused() == "reset",
          "The nested move is delivered after the current notification, in order");
    detail::access(ui).onFocus([](const Id&, const Id&) { throw std::runtime_error("owner bug"); });
    check(detail::access(ui).focus("name").status == Status::failed && detail::access(ui).stopped(), "A throwing focus callback is reported, not hidden");
}

void keyboardEditingObeysTheSameAdmission() {
    Ui ui; populate(ui, formWithButtons());
    detail::access(ui).focus("name");
    detail::access(ui).select("name", 2, 2);
    detail::access(ui).modal(true);
    check(detail::access(ui).keyDown(press(Key::right)).status == Status::blockedByModal && at(ui, 2, 2), "Caret keys respect the modal block");
    check(detail::access(ui).keyDown(ctrl(u'A')).status == Status::blockedByModal && at(ui, 2, 2), "Select-all respects the modal block");
    detail::access(ui).modal(false);
    MemoryClipboard clipboard;
    detail::access(ui).setClipboard(&clipboard);
    detail::access(ui).textInput(u"!");
    detail::access(ui).undo("name");
    detail::access(ui).setReadOnly("name", true);
    clipboard.content = u"x";
    std::vector<Status> refused{detail::access(ui).keyDown(ctrl(u'Y')).status, detail::access(ui).keyDown(press(Key::del)).status,
                                detail::access(ui).paste("name", u"x").status, detail::access(ui).cut("name").status, detail::access(ui).undo("name").status, detail::access(ui).redo("name").status};
    check(refused == std::vector<Status>(refused.size(), Status::readOnly), "Read-only refuses redo, Delete and semantic edits alike");
    detail::access(ui).setReadOnly("name", false);
    check(detail::access(ui).redo("name") && detail::access(ui).text("name") == u"測試!使用者", "The redo position survived");
}

void overLimitEditIsRejectedWhole() {
    Ui ui; populate(ui, {{ComponentKind::textField, {.id = "code", .value = u"AB", .maxLength = 4}}});
    check(ComponentConfig{.id = "x"}.maxLength == defaultMaxTextLength && defaultMaxTextLength == 4096,
          "The default limit is public");
    detail::access(ui).focus("code");
    detail::access(ui).select("code", 2, 2);
    detail::access(ui).textInput(u"CD");
    check(detail::access(ui).textInput(u"E").status == Status::invalidValue, "Typing past the limit is rejected");
    check(detail::access(ui).paste("code", u"XYZ").status == Status::invalidValue, "Pasting past the limit is rejected");
    check(detail::access(ui).setValue("code", u"12345").status == Status::invalidValue, "Semantic writes obey the same limit");
    check(detail::access(ui).text("code") == u"ABCD" && detail::access(ui).undo("code") && detail::access(ui).text("code") == u"AB" && !detail::access(ui).undo("code"),
          "Rejected edits keep text and history");
    bool refused = false;
    refused = ui.addTextField({.id = "x", .value = u"toolong", .maxLength = 3}).status == Status::invalidValue;
    check(refused, "Initial text must respect the field limit");
}

void invalidTextInputPreservesTheSelectionAndRedo() {
    Ui ui;
    populate(ui, {{ComponentKind::textField, {.id = "code", .value = u"A\U0001f600B"}}});
    auto& input = detail::access(ui);
    check(input.focus("code") && input.select("code", 3, 1) && input.textInput(u"x") && input.undo("code"),
          "Undo a replacement of a reverse selection across a surrogate pair");
    const std::u16string badUtf16{static_cast<char16_t>(0xd800), u'y'};
    check(input.textInput(u"x\ty").status == Status::invalidValue &&
          input.textInput(badUtf16).status == Status::invalidValue,
          "Each invalid input is rejected in full");
    const auto node = ui.semantics().get({"code"});
    check(node && node->value == u"A\U0001f600B" && node->textSelection == Selection{3, 1},
          "The draft and reverse selection are unchanged");
    check(input.redo("code") && input.text("code") == u"AxB", "The same replacement can still be redone");
}

void standaloneEditSessionPreeditReplacesAReverseSelection() {
    EditSession session;
    session.text = u"A\U0001f600BC";
    session.select(4, 2);
    check(session.selection == Selection{4, 1} && session.begin() == 1 && session.end() == 4,
          "Reverse selection snaps to the start of the surrogate pair and sorts its endpoints");
    session.updateComposition(u"中\U0001f600", 3);
    check(session.visible() == u"A中\U0001f600C" && session.visibleCaret() == 4 && session.text == u"A\U0001f600BC",
          "Preedit replaces only the visible selection and positions its caret in UTF-16 units");
    session.cancelComposition();
    check(session.visible() == u"A\U0001f600BC" && session.visibleCaret() == 1 &&
          session.selection == Selection{4, 1}, "Cancel restores both the committed text and the reverse selection");
}

void publicValidationAndPasteNormalizationAgree() {
    const auto plain = pasteAsSingleLine(u"a\r\n\U0001f600\rb\n");
    const auto multiline = pasteAsMultiLine(u"a\r\n\U0001f600\rb\n");
    check(plain == u"a \U0001f600 b " && multiline == u"a\n\U0001f600\nb\n",
          "CRLF, CR, and LF each contribute one separator");
    check(validSingleLine(plain, 7) && !validSingleLine(plain, 6) &&
          validMultiLine(multiline, 7) && !validSingleLine(multiline) && !validMultiLine(u"a\r\nb"),
          "The validators count UTF-16 units and only the multiline validator accepts LF");
    const std::u16string invalid{static_cast<char16_t>(0xdc00)};
    check(!validSingleLine(pasteAsSingleLine(invalid)) && !validMultiLine(pasteAsMultiLine(invalid)),
          "Line-break normalization does not make malformed text valid");
    check(normalizeRichText({multiline, {{.start = 3, .length = 1, .bold = true}}}).status == Status::invalidValue,
          "A rich span cannot start between the halves of the surrogate pair");
}

bool has(const std::vector<Verb>& actions, Verb operation) {
    return std::find(actions.begin(), actions.end(), operation) != actions.end();
}

void ownerHearsFocusMovesAndSemanticsListActions() {
    Ui ui; populate(ui, formWithButtons());
    std::vector<std::pair<Id, Id>> moves;
    detail::access(ui).onFocus([&](const Id& from, const Id& to) {
        moves.emplace_back(from, to);
        if (from == "name") detail::access(ui).setError("name", detail::access(ui).text("name").empty() ? u"必填" : u"");
    });
    detail::access(ui).focus("name");
    detail::access(ui).keyDown(ctrl(u'A'));
    detail::access(ui).keyDown(press(Key::backspace));
    detail::access(ui).keyDown(press(Key::tab));
    check((moves == std::vector<std::pair<Id, Id>>{{"", "name"}, {"name", "apply"}}), "Owner hears each focus move once");
    check(detail::access(ui).describe("name")->error == u"必填", "Owner can validate on blur");
    auto field = detail::access(ui).describe("name")->verbs;
    check(has(field, Verb::focus) && has(field, Verb::setValue) && has(field, Verb::paste) && has(field, Verb::undo) &&
          !has(field, Verb::redo) && !has(field, Verb::invoke), "Editable field lists what it can do now");
    detail::access(ui).setReadOnly("name", true);
    field = detail::access(ui).describe("name")->verbs;
    check(has(field, Verb::select) && has(field, Verb::copy) && !has(field, Verb::setValue) && !has(field, Verb::undo),
          "Read-only field lists only viewing actions");
    check((detail::access(ui).describe("apply")->verbs == std::vector<Verb>{Verb::focus}), "An unbound button only offers focus");
    std::get<ButtonHandle>(detail::access(ui).layoutHandle("apply")).bind([] { return Result{}; });
    check(has(detail::access(ui).describe("apply")->verbs, Verb::invoke), "A bound button offers invoke");
    detail::access(ui).enable("apply", false);
    check(detail::access(ui).describe("apply")->verbs.empty() &&
          has(detail::access(ui).describe("note")->verbs, Verb::setTextAlign), "Disabled buttons offer nothing; labels offer their configuration verbs");
    int count = 0;
    detail::access(ui).bindText("note", [&] { return u"套用 " + std::u16string(1, char16_t(u'0' + count)) + u" 次"; });
    count = 2;
    check(detail::access(ui).text("note") == u"套用 2 次" && detail::access(ui).describe("note")->value == u"套用 2 次", "Bound label reads the owner's data on demand");
}
}

void confirmedFieldRespectsSharedCommandGuard() {
    Ui ui;
    auto field = ui.addTextField({.id = "guard.field"}).value.value();
    auto button = ui.addButton({.id = "guard.button"}).value.value();
    field.connectSource({[] { return std::u16string(u"source"); }, [](std::function<void()>) { return [] {}; }});
    int writes = 0;
    Status nested = Status::ok;
    field.connectConfirmed([&](const Change<TextFieldHandle>&) { ++writes; nested = button.invoke().status; return Result{}; });
    field.setValue(u"draft");
    int commands = 0;
    button.bind([&] { ++commands; return field.confirm(); });
    check(button.invoke().status == Status::busy && writes == 0 && commands == 1, "A command cannot nest a standalone confirmation");
    check(bool(field.confirm()) && writes == 1 && nested == Status::busy && commands == 1,
          "Standalone confirmation works and blocks nested commands");
}

void publicTextOperationsObeyRestrictions() {
    Ui ui; MemoryClipboard clipboard; detail::access(ui).setClipboard(&clipboard);
    auto field = ui.addTextField({.id = "public.text"}).value.value();
    field.connectSource({[] { return std::u16string(u"alpha"); }, [](std::function<void()>) { return [] {}; }});
    field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
    check(bool(field.select(0, 5)) && bool(field.copy()) && clipboard.content == u"alpha", "Public selection and copy");
    check(bool(field.cut()) && field.describe()->value.empty(), "Public cut");
    check(bool(field.undo()) && bool(field.redo()) && field.describe()->value.empty(), "Public undo and redo");
    field.readOnly(true);
    check(field.cut().status == Status::readOnly && field.redo().status == Status::readOnly && bool(field.copy()),
          "Read-only public operations share admission rules");
}

void handleQueriesReportExpiry() {
    TextFieldHandle field; ButtonHandle button; LabelHandle label;
    {
        Ui ui;
        field = ui.addTextField({.id = "query.field"}).value.value();
        button = ui.addButton({.id = "query.button"}).value.value();
        label = ui.addLabel({.id = "query.label"}).value.value();
        check(button.describe()->kind == ComponentKind::button && label.describe()->kind == ComponentKind::label,
              "Button and label expose semantic snapshots");
    }
    check(field.describe().status == Status::notFound && field.sourceConflict().status == Status::notFound &&
          button.describe().status == Status::notFound && button.executing().status == Status::notFound &&
          label.describe().status == Status::notFound, "Expired queries return an explicit error");
}

void configurationRequiresUiThread() {
    Ui ui;
    auto field = ui.addTextField({.id = "thread.field"}).value.value();
    auto label = ui.addLabel({.id = "thread.label"}).value.value();
    field.connectSource({[] { return std::u16string(u"source"); }, [](std::function<void()>) { return [] {}; }});
    Status reset = Status::ok, connect = Status::ok;
    std::thread worker([&] {
        reset = field.resetToSource().status;
        connect = label.connectSource([] { return std::u16string(u"wrong"); }).status;
    });
    worker.join();
    check(reset == Status::wrongThread && connect == Status::wrongThread &&
          field.describe()->value == u"source" && label.describe()->value.empty(),
          "Configuration rejects another thread without changing displayed values");
    auto fault = ui.addButton({.id = "thread.fault"}).value.value();
    fault.bind([]() -> Result { throw std::runtime_error("failure"); });
    check(fault.invoke().status == Status::failed && field.resetToSource().status == Status::failed &&
          label.connectSource([] { return std::u16string(u"late"); }).status == Status::failed,
          "Stopped UI rejects both configuration paths");
}

void groupActionCannotBeReplaced() {
    Ui ui;
    auto field = ui.addTextField({.id = "group.field"}).value.value();
    auto button = ui.addButton({.id = "group.button"}).value.value();
    field.connectSource({[] { return std::u16string(u"source"); }, [](std::function<void()>) { return [] {}; }});
    int submitted = 0, replacement = 0;
    check(bool(ui.connectConfirmation(button, {field}, [&](const auto&) { ++submitted; return ConfirmationResult{}; })), "Group connects");
    auto rebind = button.bind([&] { ++replacement; return Result{}; });
    check(rebind.status == Status::invalidValue, "Rebinding a confirmation button returns an error");
    check(bool(button.invoke()) && submitted == 1 && replacement == 0, "The original confirmation still runs");
}

void unconnectedTextRejectsEdits() {
    Ui ui;
    auto field = ui.addTextField({.id = "unconnected", .value = u"initial"}).value.value();
    auto snapshot = field.describe();
    check(field.setValue(u"changed").status == Status::invalidValue && snapshot->value == u"initial" &&
          !has(snapshot->verbs, Verb::setValue), "An unconnected field rejects edits and does not advertise editing");
}

void handlesExpireWithTheirComponentTree() {
    TextFieldHandle field;
    {
        Ui ui;
        field = ui.addTextField({.id = "handle.field", .value = u"initial"}).value.value();
        field.connectSource({[] { return std::u16string(u"initial"); }, [](std::function<void()>) { return [] {}; }});
        field.connectConfirmed([](const Change<TextFieldHandle>&) { return Result{}; });
        check(bool(field.setValue(u"edited")) && field.describe()->value == u"edited",
              "Typed handle edits and describes its field");
    }
    check(field.setValue(u"late").status == Status::notFound && !field.describe(),
          "A retained handle cannot keep the component tree alive or operate after its end");
    check(field.enable(false).status == Status::notFound && field.readOnly(true).status == Status::notFound,
          "Configuration operations also report an expired component");
}

void sourceUpdatesFollowTheComponentLifetime() {
    std::u16string model = u"first";
    std::function<void()> changed;
    TextFieldHandle field;
    {
        Ui ui;
        field = ui.addTextField({.id = "source.field"}).value.value();
        field.connectSource({[&] { return model; }, [&](std::function<void()> callback) {
            changed = std::move(callback);
            return [&] { changed = {}; };
        }});
        check(field.describe()->value == u"first", "A field reads its initial value from the app model");
        model = u"second";
        changed();
        check(field.describe()->value == u"second", "A source notification refreshes an unedited field");
    }
    check(!changed, "Ending the component tree disconnects its source subscription");
}

void sourceConflictKeepsDraftUntilCancelled() {
    std::u16string model = u"original";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "conflict.field"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> callback) {
        changed = std::move(callback); return [&] { changed = {}; };
    }});
    auto confirm = ui.addButton({.id = "conflict.apply"}).value.value();
    std::optional<Change<TextFieldHandle>> submitted;
    ui.connectConfirmation(confirm, {field}, [&](const std::vector<ConfirmFieldChange>& changes) {
        submitted = std::get<Change<TextFieldHandle>>(changes.at(0));
        return ConfirmationResult{{Status::invalidValue, u"Conflict"}, {{field, u"Conflict"}}};
    });
    field.setValue(u"draft");
    model = u"newest";
    changed();
    check(field.describe()->value == u"draft" && field.describe()->sourceConflict && field.sourceConflict().value.value_or(false),
          "A different source value preserves the draft and signals the conflict");
    field.enable(false);
    check(field.describe()->sourceConflict, "Conflict remains visible in disabled semantics");
    field.enable(true);
    check(confirm.invoke().status == Status::invalidValue && submitted && submitted->field == field &&
          submitted->value == u"draft" && submitted->origin == u"original",
          "A conflicted submission retains the draft and its original starting value");
    check(bool(field.useLatest()) && field.describe()->value == u"newest" && !field.sourceConflict().value.value_or(false),
          "Choosing the latest value discards the draft and clears the conflict");
    check(bool(field.setValue(u"second draft")), "edit again from the latest value");
    model = u"another source value";
    changed();
    check(field.describe()->sourceConflict && bool(field.resetToSource()) &&
          field.describe()->value == u"another source value" && !field.describe()->sourceConflict,
          "Reset also returns a conflicted field to its current source");
}

void confirmationSubmitsDraftAndOriginAndKeepsHistory() {
    std::u16string model = u"before";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "confirm.field"}).value.value();
    auto apply = ui.addButton({.id = "confirm.apply"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> callback) {
        changed = std::move(callback); return [&] { changed = {}; };
    }});
    std::u16string receivedOrigin;
    ui.connectConfirmation(apply, {field}, [&](const std::vector<ConfirmFieldChange>& changes) {
        receivedOrigin = std::get<Change<TextFieldHandle>>(changes.at(0)).origin;
        model = std::get<Change<TextFieldHandle>>(changes.at(0)).value;
        changed();
        return ConfirmationResult{};
    });
    field.setValue(u"draft");
    check(bool(apply.invoke()) && model == u"draft" && receivedOrigin == u"before",
          "One application entry receives the draft and its origin");
    check(bool(field.undo()) && field.describe()->value == u"before" && model == u"draft",
          "Undo after apply creates a draft without undoing the model change");
}

void rejectedGroupKeepsDraftsAndFocusesFirstError() {
    std::u16string a = u"one", b = u"two";
    std::function<void()> changedA, changedB;
    Ui ui;
    auto first = ui.addTextField({.id = "reject.first"}).value.value();
    auto second = ui.addTextField({.id = "reject.second"}).value.value();
    auto apply = ui.addButton({.id = "reject.apply"}).value.value();
    first.connectSource({[&] { return a; }, [&](std::function<void()> f) { changedA = std::move(f); return [&] { changedA = {}; }; }});
    second.connectSource({[&] { return b; }, [&](std::function<void()> f) { changedB = std::move(f); return [&] { changedB = {}; }; }});
    ui.connectConfirmation(apply, {first, second}, [&](const std::vector<ConfirmFieldChange>& changes) {
        check(changes.size() == 2 && std::get<Change<TextFieldHandle>>(changes[0]).value == u"bad one" && std::get<Change<TextFieldHandle>>(changes[1]).value == u"bad two",
              "Both drafts arrive together");
        return ConfirmationResult{{Status::invalidValue, u"Invalid"}, {{second, u"second problem"}, {first, u"first problem"}}};
    });
    first.setValue(u"bad one");
    second.setValue(u"bad two");
    check(apply.invoke().status == Status::invalidValue && first.describe()->value == u"bad one" && second.describe()->value == u"bad two",
          "Rejection keeps all drafts");
    check(first.describe()->error == u"first problem" && second.describe()->error == u"second problem" && first.describe()->focused,
          "Rejection labels each field and focuses the first field in group order");
}

void confirmationBlocksReentryButPreservesNewEdits() {
    std::u16string model = u"start";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "busy.field"}).value.value();
    auto apply = ui.addButton({.id = "busy.apply"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    int calls = 0;
    std::u16string secondOrigin;
    ui.connectConfirmation(apply, {field}, [&](const std::vector<ConfirmFieldChange>& changes) {
        ++calls;
        if (calls == 1) {
            check(apply.executing().value.value_or(false) && apply.invoke().status == Status::busy, "An executing confirmation cannot re-trigger");
            check(bool(field.setValue(u"newer")), "The field remains editable while the action executes");
        } else secondOrigin = std::get<Change<TextFieldHandle>>(changes[0]).origin;
        model = std::get<Change<TextFieldHandle>>(changes[0]).value;
        changed();
        return ConfirmationResult{};
    });
    field.setValue(u"sent");
    auto accepted = apply.invoke();
    check(bool(accepted), "The first confirmation succeeds");
    check(!apply.executing().value.value_or(false) && calls == 1, "Execution state resets and callback runs once");
    check(model == u"sent", "The submitted value reaches the model");
    check(field.describe()->value == u"newer", "Edits during execution stay in the field");
    check(bool(apply.invoke()) && model == u"newer" && secondOrigin == u"sent",
          "The new draft starts from the submitted value");
}

void connectedTextRequiresExplicitWriteTiming() {
    std::u16string model = u"source";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "timing.field"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    check(field.setValue(u"draft").status == Status::invalidValue && field.describe()->value == u"source",
          "A connected editable field without explicit write timing rejects editing");
}

void sourceUpdateDuringCompositionKeepsPreedit() {
    std::u16string model = u"one";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "ime.source"}).value.value();
    auto apply = ui.addButton({.id = "ime.apply"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    ui.connectConfirmation(apply, {field}, [](const std::vector<ConfirmFieldChange>&) { return ConfirmationResult{}; });
    field.focus();
    detail::access(ui).compositionUpdate(u"preedit", 7);
    model = u"external";
    changed();
    check(detail::access(ui).edit("ime.source")->composing && detail::access(ui).edit("ime.source")->preedit == u"preedit",
          "Source updates never discard an active composition");
    detail::access(ui).compositionCommit(u"text");
    check(field.describe()->value == u"textone" && field.sourceConflict().value.value_or(false),
          "The committed composition remains a conflicting draft");
}

void editingAgainAfterMatchingTheSourceUsesTheNewOrigin() {
    std::u16string model = u"old";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "origin.field"}).value.value();
    auto apply = ui.addButton({.id = "origin.apply"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    std::u16string sentOrigin;
    ui.connectConfirmation(apply, {field}, [&](const std::vector<ConfirmFieldChange>& changes) {
        sentOrigin = std::get<Change<TextFieldHandle>>(changes[0]).origin;
        return ConfirmationResult{};
    });
    field.setValue(u"draft");
    model = u"latest"; changed();
    field.setValue(u"latest");
    field.setValue(u"another draft");
    apply.invoke();
    check(sentOrigin == u"latest", "A fresh draft starts from the current source, not an older conflict origin");
}

void explicitImmediateWriteKeepsARejectedDraft() {
    std::u16string model = u"initial";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "immediate.field"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    field.connectImmediate([&](const Change<TextFieldHandle>& change) {
        if (change.value == u"bad") return Result{Status::invalidValue, u"Rejected"};
        model = change.value;
        changed();
        return Result{};
    });
    check(bool(field.setValue(u"accepted")) && model == u"accepted", "Immediate writes reach the app modification entry");
    check(field.setValue(u"bad").status == Status::invalidValue && field.describe()->value == u"bad" &&
          field.describe()->error == u"Rejected" && model == u"accepted",
          "A rejected immediate write keeps the draft and its reason");
}

void cancellingARejectedDraftClearsItsErrorWithoutWriting() {
    std::u16string model = u"saved";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "cancel.field"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    int writes = 0;
    field.connectImmediate([&](const Change<TextFieldHandle>&) { ++writes; return Result{Status::invalidValue, u"Rejected"}; });
    field.setValue(u"bad");
    check(bool(field.useLatest()) && field.describe()->value == u"saved" && !field.describe()->invalid && writes == 1,
          "Cancelling restores the source, clears its rejection, and makes no new write");
}

void sourceNotificationsRequestARepaint() {
    std::u16string model = u"before";
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "paint.field"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    int invalidations = 0;
    detail::access(ui).onInvalidate([&] { ++invalidations; });
    model = u"after";
    changed();
    check(invalidations == 1 && field.describe()->value == u"after",
          "An external model update makes the new value visible to the host");
}

void ungroupedConfirmedConnectionWritesOnlyOnConfirm() {
    std::u16string model = u"before", origin;
    std::function<void()> changed;
    Ui ui;
    auto field = ui.addTextField({.id = "single.field"}).value.value();
    field.connectSource({[&] { return model; }, [&](std::function<void()> f) { changed = std::move(f); return [&] { changed = {}; }; }});
    field.connectConfirmed([&](const Change<TextFieldHandle>& change) {
        origin = change.origin;
        model = change.value;
        changed();
        return Result{};
    });
    check(bool(field.setValue(u"draft")) && model == u"before", "Confirmed mode retains the draft without an immediate write");
    check(bool(ui.semantics().perform({"single.field"}, Verb::confirm)) && model == u"draft" && origin == u"before", "Semantic confirm submits the draft and its origin");
}

void checkboxTogglesAConnectedBoolean() {
    bool model = true;
    Ui ui;
    auto box = ui.addCheckbox({.id = "notice", .label = u"Notices"}).value.value();
    box.connectSource({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    box.connectImmediate([&](const Change<CheckboxHandle>& change) { model = change.value; return Result{}; });
    check(bool(box.toggle()) && !model && !*box.draft() && box.describe()->value == u"false",
          "Toggling changes the connected boolean and its semantic value exactly once");
}

void checkboxInputAndDisabledStateAgree() {
    bool model = false;
    Ui ui;
    auto box = ui.addCheckbox({.id = "notice", .label = u"Notices"}).value.value();
    box.connectSource({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    box.connectImmediate([&](const Change<CheckboxHandle>& change) { model = change.value; return Result{}; });
    auto& host = detail::access(ui);
    box.focus(); host.keyDown({.key = Key::space}); host.keyDown({.key = Key::space, .repeat = true}); host.keyUp(Key::space);
    check(model && box.describe()->checked == true, "Space changes the value and check mark once");
    box.enable(false);
    check(box.toggle().status == Status::disabled && model && box.describe()->checked == true,
          "A disabled checkbox keeps its value and check mark");
}

void radioCanStartEmptyAndSkipDisabledOptions() {
    std::optional<OptionValue> model;
    Ui ui;
    auto group = ui.addRadioGroup({.id = "mode", .label = u"Mode"},
        {{{"normal"}, u"Normal", false}, {{"focus"}, u"Focus", true}, {{"offline"}, u"Offline", false}}).value.value();
    group.connectSource({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    group.connectImmediate([&](const Change<RadioGroupHandle>& change) { model = change.value; return Result{}; });
    check(!group.draft()->has_value(), "A radio group can initially have no selection");
    check(group.select({"normal"}).status == Status::disabled && !model, "Disabled options cannot be selected");
    group.focus();
    detail::access(ui).keyDown({.key = Key::right});
    check(model == OptionValue{"focus"} && *group.draft() == model, "Direction selects the first enabled option");
    detail::access(ui).keyDown({.key = Key::right});
    check(model == OptionValue{"focus"}, "At the end navigation does not wrap");
}

void mixedConfirmationSubmitsOneBatch() {
    std::u16string name = u"before";
    bool enabled = true;
    std::optional<OptionValue> mode = OptionValue{"normal"};
    Ui ui;
    auto text = ui.addTextField({.id = "name"}).value.value();
    auto box = ui.addCheckbox({.id = "enabled"}).value.value();
    auto radio = ui.addRadioGroup({.id = "mode"}, {{{"normal"}, u"Normal"}, {{"focus"}, u"Focus"}}).value.value();
    auto apply = ui.addButton({.id = "apply"}).value.value();
    text.connectSource({[&] { return name; }, [](std::function<void()>) { return [] {}; }});
    box.connectSource({[&] { return enabled; }, [](std::function<void()>) { return [] {}; }});
    radio.connectSource({[&] { return mode; }, [](std::function<void()>) { return [] {}; }});
    int calls = 0;
    ui.connectConfirmation(apply, {text, box, radio}, [&](const std::vector<ConfirmFieldChange>& changes) {
        ++calls;
        name = std::get<Change<TextFieldHandle>>(changes[0]).value;
        enabled = std::get<Change<CheckboxHandle>>(changes[1]).value;
        mode = std::get<Change<RadioGroupHandle>>(changes[2]).value;
        return ConfirmationResult{};
    });
    text.setValue(u"after"); box.toggle(); radio.select({"focus"});
    check(name == u"before" && enabled && mode == OptionValue{"normal"}, "All three edits stay drafts before apply");
    check(bool(apply.invoke()) && calls == 1 && name == u"after" && !enabled && mode == OptionValue{"focus"},
          "Apply submits one batch containing all three value types");
}

void mixedRejectionMarksEachFieldAndFocusesFirst() {
    Ui ui;
    auto text = ui.addTextField({.id = "text"}).value.value();
    auto box = ui.addCheckbox({.id = "box"}).value.value();
    auto radio = ui.addRadioGroup({.id = "radio"}, {{{"a"}, u"A"}}).value.value();
    auto apply = ui.addButton({.id = "apply"}).value.value();
    text.connectSource({[] { return std::u16string(u"old"); }, [](std::function<void()>) { return [] {}; }});
    box.connectSource({[] { return false; }, [](std::function<void()>) { return [] {}; }});
    radio.connectSource({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    ui.connectConfirmation(apply, {text, box, radio}, [&](const auto&) {
        return ConfirmationResult{{Status::invalidValue, u"Rejected"}, {{radio, u"Radio error"}, {box, u"Box error"}, {text, u"Text error"}}};
    });
    text.setValue(u"draft"); box.toggle(); radio.select({"a"});
    check(apply.invoke().status == Status::invalidValue && text.describe()->focused,
          "Rejection focuses the first field in the group, regardless of error ordering");
    check(text.describe()->error == u"Text error" && box.describe()->error == u"Box error" && radio.describe()->error == u"Radio error",
          "Each field exposes its own rejection reason");
    check(*text.draft() == u"draft" && *box.draft() && *radio.draft() == OptionValue{"a"}, "Rejected drafts are retained");
}

void radioAllDisabledRemainsEmptyAndTabCanLeave() {
    Ui ui;
    auto radio = ui.addRadioGroup({.id = "radio"},
        {{{"a"}, u"A", false}, {{"b"}, u"B", false}}).value.value();
    auto next = ui.addButton({.id = "next"}).value.value();
    radio.connectSource({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    radio.connectImmediate([](const Change<RadioGroupHandle>&) { return Result{}; });
    radio.focus();
    detail::access(ui).keyDown({.key = Key::right});
    detail::access(ui).keyDown({.key = Key::left});
    check(!radio.draft()->has_value(), "No direction selects a disabled option");
    detail::access(ui).keyDown({.key = Key::tab});
    check(next.describe()->focused, "Tab leaves an all-disabled group");
}

void radioSourceConflictCanBeResolved() {
    std::optional<OptionValue> model = OptionValue{"a"};
    std::function<void()> changed;
    Ui ui;
    auto group = ui.addRadioGroup({.id = "radio"}, {{{"a"}, u"A"}, {{"b"}, u"B"}, {{"c"}, u"C"}}).value.value();
    auto apply = ui.addButton({.id = "apply"}).value.value();
    group.connectSource({[&] { return model; }, [&](std::function<void()> cb) { changed = std::move(cb); return [&] { changed = {}; }; }});
    ui.connectConfirmation(apply, {group}, [](const auto&) { return ConfirmationResult{}; });
    group.select({"b"}); model = OptionValue{"c"}; changed();
    check(*group.draft() == OptionValue{"b"} && group.describe()->sourceConflict, "A new source does not overwrite the draft and is reported");
    check(bool(group.useLatest()) && *group.draft() == OptionValue{"c"} && !group.describe()->sourceConflict,
          "The latest source can replace the draft explicitly");
}

void standaloneBooleanAndOptionConfirmation() {
    bool enabled = false;
    std::optional<OptionValue> mode = OptionValue{"a"};
    Ui ui;
    auto box = ui.addCheckbox({.id = "box"}).value.value();
    auto radio = ui.addRadioGroup({.id = "radio"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
    box.connectSource({[&] { return enabled; }, [](std::function<void()>) { return [] {}; }});
    radio.connectSource({[&] { return mode; }, [](std::function<void()>) { return [] {}; }});
    box.connectConfirmed([&](const Change<CheckboxHandle>& c) { enabled = c.value; return Result{}; });
    radio.connectConfirmed([&](const Change<RadioGroupHandle>& c) { mode = c.value; return Result{}; });
    box.toggle(); radio.select({"b"});
    check(!enabled && mode == OptionValue{"a"}, "Standalone confirmed connections keep drafts until confirmed");
    check(bool(box.confirm()) && bool(radio.confirm()) && enabled && mode == OptionValue{"b"}, "Each explicit confirm writes its typed draft");
}

void unconnectedSelectionControlsDoNotAdvertiseWrites() {
    Ui ui;
    auto box = ui.addCheckbox({.id = "box"}).value.value();
    auto radio = ui.addRadioGroup({.id = "radio"}, {{{"a"}, u"A"}}).value.value();
    auto has = [](const ComponentSnapshot& s, Verb op) { return std::find(s.verbs.begin(), s.verbs.end(), op) != s.verbs.end(); };
    check(!has(*box.describe(), Verb::toggle) && ui.semantics().get({"radio", OptionValue{"a"}})->verbs.empty(),
          "Unconnected controls do not advertise unavailable modifications");
    box.connectSource({[] { return false; }, [](std::function<void()>) { return [] {}; }});
    radio.connectSource({[] { return std::optional<OptionValue>{}; }, [](std::function<void()>) { return [] {}; }});
    box.connectImmediate([](const Change<CheckboxHandle>&) { return Result{}; });
    radio.connectImmediate([](const Change<RadioGroupHandle>&) { return Result{}; });
    check(has(*box.describe(), Verb::toggle) &&
          ui.semantics().get({"radio", OptionValue{"a"}})->verbs == std::vector<Verb>{Verb::select},
          "Connected editable controls advertise their available modifications");
}

void sliderConnectedDraftIsConfirmedAsANumber() {
    int model = 40;
    Ui ui;
    auto slider = ui.addSlider({.id = "volume", .label = u"Volume"}, {0, 100, 5}).value.value();
    auto apply = ui.addButton({.id = "apply.slider"}).value.value();
    slider.connectSource({[&] { return model; }, [](std::function<void()>) { return [] {}; }});
    ui.connectConfirmation(apply, {slider}, [&](const std::vector<ConfirmFieldChange>& changes) {
        model = std::get<Change<SliderHandle>>(changes[0]).value;
        return ConfirmationResult{};
    });
    check(bool(slider.setValue(55)) && *slider.draft() == 55 && model == 40 && slider.describe()->value == u"55",
          "The numeric edit and semantic value are drafts until apply");
    check(bool(apply.invoke()) && model == 55, "Apply submits the numeric draft through the application callback");
}

void sliderNonDivisibleRangeAndKeyboard() {
    Ui ui;
    auto slider = ui.addSlider({.id = "level"}, {-3, 8, 4}).value.value();
    slider.connectSource({[] { return -3; }, [](std::function<void()>) { return [] {}; }});
    slider.connectConfirmed([](const Change<SliderHandle>&) { return Result{}; });
    check(slider.setValue(-4).status == Status::invalidValue && *slider.draft() == -3 &&
          slider.setValue(9).status == Status::invalidValue && *slider.draft() == -3,
          "Programmatic values outside the range are rejected without changing the draft");
    slider.setValue(3);
    check(*slider.draft() == 5, "A midpoint snaps to the higher step");
    slider.setValue(7);
    check(*slider.draft() == 8, "The shorter final interval snaps to the maximum");
    slider.focus();
    detail::access(ui).keyDown({.key = Key::left});
    check(*slider.draft() == 5, "Left from the maximum reaches the last regular step");
    detail::access(ui).keyDown({.key = Key::right});
    detail::access(ui).keyDown({.key = Key::right});
    check(*slider.draft() == 8, "Right reaches and stops at the maximum");
    detail::access(ui).keyDown({.key = Key::home});
    check(*slider.draft() == -3, "Home reaches the minimum");
    detail::access(ui).keyDown({.key = Key::end});
    check(*slider.draft() == 8, "End reaches the non-divisible maximum");
    slider.enable(false);
    check(slider.setValue(1).status == Status::disabled && *slider.draft() == 8, "Disabled edits are rejected");
}

void sliderRejectsInvalidRanges() {
    Ui ui; int rejected = 0;
    for (auto range : {SliderRange{1, 1, 1}, SliderRange{2, 1, 1}, SliderRange{0, 1, 0}, SliderRange{0, 1, -1}}) {
        if (ui.addSlider({.id = "invalid"}, range).status == Status::invalidValue) ++rejected;
    }
    check(rejected == 4, "Empty or reversed ranges and non-positive steps are rejected");
    bool missingRange = false;
    missingRange = ui.addSlider({.id = "missing"}, {0, 0, 1}).status == Status::invalidValue;
    check(missingRange, "A slider without a range cannot be created through the untyped constructor");
}

void progressSourceIsReadOnlyAndDisconnects() {
    int source = -10;
    std::function<void()> changed;
    ProgressHandle saved;
    {
        Ui ui;
        saved = ui.addProgress({.id = "progress", .label = u"Progress"}).value.value();
        check(saved.connectSource({{}, {}}).status == Status::invalidValue, "Incomplete sources are rejected");
        check(bool(saved.connectSource({[&] { return source; }, [&](std::function<void()> cb) {
            changed = std::move(cb); return [&] { changed = {}; };
        }})), "Progress accepts a read-only numeric source");
        auto s = saved.describe();
        check(s->readOnly && !s->focusable && s->verbs.empty() && s->value == u"0%" && source == -10,
              "Progress semantics are read-only and do not alter the out-of-range source");
        source = 110; changed();
        check(saved.describe()->value == u"100%" && source == 110, "A notified value updates the display without a write");
    }
    check(!changed && saved.describe().status == Status::notFound, "Destroying the tree disconnects and expires the handle");
}

void sliderSourceConflictAndInvalidUpdates() {
    int model = 40; std::function<void()> changed; int origin = -1;
    SliderHandle saved;
    {
        Ui ui;
        saved = ui.addSlider({.id = "volume"}, {0, 100, 5}).value.value();
        auto apply = ui.addButton({.id = "send"}).value.value();
        saved.connectSource({[&] { return model; }, [&](std::function<void()> cb) { changed = std::move(cb); return [&] { changed = {}; }; }});
        ui.connectConfirmation(apply, {saved}, [&](const auto& changes) {
            origin = std::get<Change<SliderHandle>>(changes[0]).origin;
            return ConfirmationResult{{Status::invalidValue, u"No"}, {{saved, u"Volume rejected"}}};
        });
        saved.setValue(55); model = 70; changed();
        check(*saved.draft() == 55 && saved.describe()->sourceConflict, "External updates retain a conflicting numeric draft");
        check(apply.invoke().status == Status::invalidValue && origin == 40 && saved.describe()->error == u"Volume rejected" && *saved.draft() == 55,
              "A rejected group retains the draft, origin, field error and focus");
        check(bool(saved.useLatest()) && *saved.draft() == 70 && !saved.describe()->sourceConflict, "Use latest restores the numeric source");
        model = 72; changed();
        check(*saved.draft() == 70 && saved.describe()->invalid, "An invalid notified source value cannot escape the range contract");
        model = 75; changed();
        check(*saved.draft() == 75 && !saved.describe()->invalid, "A valid source update clears its earlier source error");
    }
    check(!changed && saved.draft().status == Status::notFound, "Destroying the tree disconnects the numeric source and expires the handle");
}

void sliderGroupPreservesEditsDuringSubmission() {
    int model = 40, submittedOrigin = -1; std::function<void()> changed;
    Ui ui;
    auto slider = ui.addSlider({.id = "volume"}, {0, 100, 5}).value.value();
    auto apply = ui.addButton({.id = "apply"}).value.value();
    slider.connectSource({[&] { return model; }, [&](std::function<void()> cb) { changed = std::move(cb); return [&] { changed = {}; }; }});
    int calls = 0;
    ui.connectConfirmation(apply, {slider}, [&](const std::vector<ConfirmFieldChange>& changes) {
        const auto change = std::get<Change<SliderHandle>>(changes[0]);
        ++calls; submittedOrigin = change.origin;
        if (calls == 1) {
            check(apply.invoke().status == Status::busy, "A running confirmation cannot be reentered");
            slider.setValue(65);
        }
        model = change.value; changed();
        return ConfirmationResult{};
    });
    slider.setValue(55); apply.invoke();
    check(model == 55 && *slider.draft() == 65, "An edit during submission survives as a new draft");
    apply.invoke();
    check(model == 65 && submittedOrigin == 55, "The next submission starts from the accepted value");
}

void dropdownCandidateIsNotADraftUntilConfirmed() {
    std::optional<OptionValue> model = OptionValue{"north"};
    Ui ui;
    auto region = ui.addDropdown({.id = "region"},
        {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
    check(bool(region.connectSource({[&] { return model; }, [](std::function<void()>) { return [] {}; }})), "Connect region source");
    check(bool(region.connectConfirmed([](const Change<DropdownHandle>&) { return Result{}; })), "Connect confirmed write");
    check(bool(region.focus()) && bool(region.open()), "Open the dropdown");
    detail::access(ui).keyDown({.key = Key::down});
    check(*region.draft() == OptionValue{"north"} && region.describe()->candidate == OptionValue{"south"},
          "Arrow changes the candidate only");
    detail::access(ui).keyDown({.key = Key::escape});
    check(*region.draft() == OptionValue{"north"} && !region.describe()->expanded, "Escape cancels the candidate");
    region.open(); detail::access(ui).keyDown({.key = Key::down}); detail::access(ui).keyDown({.key = Key::enter});
    check(*region.draft() == OptionValue{"south"} && model == OptionValue{"north"}, "Enter confirms only the draft");
}

void dropdownLeavingAndReplacedOptionsAreSafe() {
    Ui ui;
    auto dropdown = ui.addDropdown({.id = "region"},
        {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
    auto next = ui.addButton({.id = "next"}).value.value();
    int clicks = 0; next.bind([&] { ++clicks; return Result{}; });
    dropdown.connectSource({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
    dropdown.connectConfirmed([](const Change<DropdownHandle>&) { return Result{}; });
    auto& host = detail::access(ui);
    dropdown.open(); host.keyDown({.key = Key::down}); host.keyDown({.key = Key::tab});
    check(!dropdown.describe()->expanded && *dropdown.draft() == OptionValue{"north"} && next.describe()->focused, "Tab cancels and advances");
    dropdown.open(); host.keyDown({.key = Key::down}); host.pointerDown("next"); host.pointerUp("next");
    check(!dropdown.describe()->expanded && *dropdown.draft() == OptionValue{"north"} && clicks == 1 && next.describe()->focused, "Outside click cancels and preserves the clicked action");
    dropdown.open(); host.pointerDown("region", 1);
    dropdown.setItems({{{"south"}, u"South"}, {{"north"}, u"North"}});
    host.pointerUp("region", 1);
    check(*dropdown.draft() == OptionValue{"north"}, "Reordered options cannot turn a press into a different selection");
    dropdown.setItems({});
    check(!*dropdown.draft() && !dropdown.describe()->candidate, "Empty options clear value and candidate");
    dropdown.setItems({{{"north"}, u"North"}, {{"south"}, u"South"}});
    check(!*dropdown.draft() && !dropdown.describe()->candidate, "Restoring options does not restore an invalidated value or candidate");
    dropdown.enable(false);
    check(dropdown.open().status == Status::disabled && dropdown.select({"north"}).status == Status::disabled, "Disabled dropdown refuses opening and selection");
    dropdown.enable(true); host.modal(true);
    check(dropdown.open().status == Status::blockedByModal && dropdown.select({"north"}).status == Status::blockedByModal, "Modal state blocks opening and selection");
}

void dropdownOptionChangesDoNotRecurseIntoNotifications() {
    Ui ui;
    auto field = ui.addDropdown({.id = "region"}, {{{"north"}, u"North"}}).value.value();
    field.connectSource({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
    field.connectConfirmed([](const Change<DropdownHandle>&) { return Result{}; });
    int calls = 0; Status nested = Status::ok;
    ui.onChange([&] { if (++calls == 1) nested = field.setItems({}).status; });
    const auto result = field.setItems({{{"north"}, u"North"}, {{"south"}, u"South"}});
    const auto snapshot = field.describe();
    check(result && calls == 1 && nested == Status::busy && snapshot && snapshot->items.size() == 2 &&
          snapshot->items[0].value == OptionValue{"north"} && snapshot->items[1].value == OptionValue{"south"},
          "An option update during a notification is refused without recursion or mutation");
}

void dropdownProgrammaticEditsRequestAFrame() {
    Ui ui;
    auto field = ui.addDropdown({.id = "region"}, {{{"north"}, u"North"}, {{"south"}, u"South"}}).value.value();
    field.connectSource({[] { return std::optional<OptionValue>{OptionValue{"north"}}; }, [](std::function<void()>) { return [] {}; }});
    field.connectConfirmed([](const Change<DropdownHandle>&) { return Result{}; });
    int frames = 0; detail::access(ui).onInvalidate([&] { ++frames; });
    field.select({"south"});
    check(frames > 0, "A public selection requests repaint");
    frames = 0; field.useLatest();
    check(frames > 0, "Restoring the source requests repaint");
}

void listMembersObeyPageScope() {
    Ui ui;
    std::optional<OptionValue> selected;
    auto list = ui.addList({.id = "list-scope"}, {{{"T006"}, u"Shared"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { selected = change.value; return Result{}; });
    auto tabs = ui.addTabs({.id = "scope-tabs"}, {
        {{"v01"}, u"One", true, {}}, {{"v02"}, u"Two", true, {list}}}).value.value();
    check(list.select({"T006"}).status == Status::hidden && !selected, "A hidden list refuses selection");
    tabs.activate({"v02"});
    check(bool(list.select({"T006"})) && selected == OptionValue{"T006"}, "The visible list accepts selection");
    tabs.activate({"v01"});
    check(list.focus().status == Status::hidden && selected == OptionValue{"T006"}, "Leaving the list retains its selection and blocks focus");
}

void tabsRequireExplicitActivationAndGuardHiddenPages() {
    Ui ui;
    auto first = ui.addButton({.id = "first"}).value.value();
    auto second = ui.addButton({.id = "second"}).value.value();
    int clicks = 0;
    second.bind([&] { ++clicks; return Result{}; });
    auto tabs = ui.addTabs({.id = "tabs"}, {
        {{"one"}, u"One", true, {first}}, {{"blocked"}, u"Blocked", false, {}},
        {{"two"}, u"Two", true, {second}}}).value.value();
    auto& host = detail::access(ui);
    check(second.invoke().status == Status::hidden, "Hidden page refuses public activation");
    check(tabs.activate({"blocked"}).status == Status::disabled, "Disabled tab refuses activation");
    tabs.focus(); host.keyDown({.key = Key::right});
    check(*tabs.active() == OptionValue{"one"} && tabs.describe()->candidate == OptionValue{"two"}, "Arrow skips disabled tab without activating");
    host.keyDown({.key = Key::enter});
    check(*tabs.active() == OptionValue{"two"} && bool(second.invoke()) && clicks == 1, "Enter activates the focused page");
    check(first.invoke().status == Status::hidden, "Previous page is hidden");
}

void tabChangeNotificationsDoNotRecurse() {
    Ui ui;
    auto tabs = ui.addTabs({.id = "tabs"}, {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}}).value.value();
    int calls = 0; Status nested = Status::ok;
    tabs.onPageChange([&](OptionValue, OptionValue) { if (++calls == 1) nested = tabs.activate({"one"}).status; });
    check(bool(tabs.activate({"two"})) && nested == Status::busy && calls == 1 && *tabs.active() == OptionValue{"two"},
          "A nested page switch is rejected while the change is being delivered");
}

void tabsRespectModalAndDisabledPointerTargets() {
    Ui ui;
    auto page = ui.addButton({.id = "page"}).value.value();
    auto tabs = ui.addTabs({.id = "tabs"}, {
        {{"one"}, u"One", true, {page}}, {{"disabled"}, u"Disabled", false, {}}, {{"two"}, u"Two", true, {}}}).value.value();
    auto& host = detail::access(ui);
    host.pointerDown("tabs", 1);
    check(host.pointerUp("tabs", 1).status == Status::disabled && *tabs.active() == OptionValue{"one"}, "Disabled pointer target cannot activate");
    tabs.focus(); host.keyDown({.key = Key::right}); host.keyDown({.key = Key::space});
    check(*tabs.active() == OptionValue{"one"}, "Space waits for release");
    host.keyUp(Key::space);
    check(*tabs.active() == OptionValue{"two"}, "Space release activates candidate");
    host.modal(true);
    check(tabs.activate({"one"}).status == Status::blockedByModal && page.invoke().status != Status::ok,
          "Modal blocks background navigation and operations");
    host.modal(false);
    check(tabs.enablePage({"two"}, false).status == Status::invalidValue, "The active page cannot be disabled");
}

void crossPageConfirmationRevealsFirstErrorAndMarksOtherPages() {
    Ui ui;
    auto first = ui.addTextField({.id = "first-field"}).value.value();
    auto second = ui.addTextField({.id = "second-field"}).value.value();
    auto submit = ui.addButton({.id = "submit"}).value.value();
    auto source = Source<std::u16string>{[] { return std::u16string(u"value"); }, [](std::function<void()>) { return [] {}; }};
    first.connectSource(source); second.connectSource(source);
    auto tabs = ui.addTabs({.id = "pages"}, {
        {{"one"}, u"One", true, {first, submit}}, {{"two"}, u"Two", true, {second}}}).value.value();
    ui.connectConfirmation(submit, {second, first}, [=](const auto&) {
        return ConfirmationResult{{Status::invalidValue, u"Invalid"}, {{first, u"First error"}, {second, u"Second error"}}};
    });
    check(submit.invoke().status == Status::invalidValue, "Application rejects the batch");
    check(*tabs.active() == OptionValue{"two"} && second.describe()->focused, "First invalid field's page opens and receives focus");
    check(tabs.describe()->items[0].invalid && first.describe()->invalid && second.describe()->invalid,
          "Errors on the other page remain discoverable from its tab");
}

void listSelectionUsesStableIdentity() {
    std::optional<OptionValue> selected;
    Ui ui;
    auto list = ui.addList({.id = "list"},
        {{{"T005"}, u"Shared"}, {{"T006"}, u"Shared"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { selected = change.value; return Result{}; });
    check(bool(list.select({"T006"})) && selected == OptionValue{"T006"}, "Select the second item with a shared name");
    list.setItems({{{"T006"}, u"Shared"}, {{"T005"}, u"Shared"}});
    check(list.describe()->items[0].selected && selected == OptionValue{"T006"}, "Reordering preserves the selected identity");
}

void listFocusAndKeyboardUseEnabledItems() {
    std::optional<OptionValue> selected;
    Ui ui;
    auto list = ui.addList({.id = "list"},
        {{{"T007"}, u"Seven"}, {{"T008"}, u"Eight", false}, {{"T009"}, u"Nine"}}).value.value();
    auto next = ui.addButton({.id = "next"}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { selected = change.value; return Result{}; });
    list.focus();
    check(list.describe()->candidate == OptionValue{"T007"} && !selected, "Entry focuses the first enabled item without selecting");
    detail::access(ui).keyDown({.key = Key::down, .shift = true, .ctrl = true});
    check(selected == OptionValue{"T009"} && list.describe()->candidate == selected, "Down skips disabled rows and modifiers do not add selections");
    check(list.select({"T008"}).status == Status::disabled && selected == OptionValue{"T009"}, "Disabled rows reject public selection");
    detail::access(ui).keyDown({.key = Key::tab});
    check(next.describe()->focused, "Tab exits the list");
}

void listRecoversCursorWhenDataChanges() {
    std::optional<OptionValue> selected;
    Ui ui;
    auto list = ui.addList({.id = "list"},
        {{{"T008"}, u"Eight"}, {{"T009"}, u"Nine"}, {{"T010"}, u"Ten"}, {{"T018"}, u"Eighteen"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { selected = change.value; return Result{}; });
    list.focus(); list.select({"T009"});
    list.setItems({{{"T010"}, u"Ten"}, {{"T018"}, u"Eighteen"}});
    check(!selected && list.describe()->candidate == OptionValue{"T010"}, "Filtering clears selection and chooses the surviving next neighbor from the old order");
}
void listWriteCannotReenterItself() {
    std::optional<OptionValue> selected; Status nested = Status::ok;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) {
        if (!selected) { selected = change.value; nested = list.select({"b"}).status; }
        return Result{};
    });
    check(bool(list.select({"a"})) && nested == Status::busy && selected == OptionValue{"a"}, "Reentrant selection is rejected without changing the accepted identity");
}

// #99: a list's write hands a change record; its origin is the selection the source held just before the write. The one
// predicate every seam asserts: `count` writes so far, the last handing this list, the new item and that origin.
bool lastListChangeIs(const std::vector<Change<ListHandle>>& changes, std::size_t count, const ListHandle& list,
                         const std::optional<OptionValue>& value, const std::optional<OptionValue>& origin) {
    return count > 0 && changes.size() == count && changes.back().field == list && changes.back().value == value &&
           changes.back().origin == origin;
}
void listUserSelectionHandsAChangeRecord() {
    std::optional<OptionValue> selected{OptionValue{"a"}}; std::vector<Change<ListHandle>> changes;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}, {{"b"}, u"B"}, {{"c"}, u"C"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { changes.push_back(change); selected = change.value; return Result{}; });
    list.focus(); detail::access(ui).keyDown({.key = Key::down});
    check(lastListChangeIs(changes, 1, list, OptionValue{"b"}, OptionValue{"a"}),
          "A keyboard selection hands the list, the new item and the item selected before");
    auto& host = detail::access(ui);
    host.pointerDown("list", 2); host.pointerUp("list", 2);
    check(lastListChangeIs(changes, 2, list, OptionValue{"c"}, OptionValue{"b"}),
          "A pointer selection's origin is the selection the source held just before");
    selected.reset();
    check(bool(list.select({"a"})) && lastListChangeIs(changes, 3, list, OptionValue{"a"}, std::nullopt),
          "Selecting with nothing selected hands an empty origin");
}
void listItemsThatDropTheSelectionHandAnEmptyChange() {
    std::optional<OptionValue> selected{OptionValue{"b"}}; std::vector<Change<ListHandle>> changes;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { changes.push_back(change); selected = change.value; return Result{}; });
    check(bool(list.setItems({{{"a"}, u"A"}, {{"b"}, u"B"}, {{"d"}, u"D"}})) && changes.empty(), "Items that keep the selection write nothing");
    check(bool(list.setItems({{{"a"}, u"A"}})) && lastListChangeIs(changes, 1, list, std::nullopt, OptionValue{"b"}) && !selected,
          "Items without the selected one hand no selection, with the item that disappeared as origin");
}
// #99 Q1, kept as it was: a selected item that stays listed but disabled counts as dropped, so the write hands no
// selection with that still-listed item as origin.
void listItemsThatDisableTheSelectionHandAnEmptyChange() {
    std::optional<OptionValue> selected{OptionValue{"b"}}; std::vector<Change<ListHandle>> changes;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { changes.push_back(change); selected = change.value; return Result{}; });
    check(bool(list.setItems({{{"a"}, u"A"}, {{"b"}, u"B", false}})) &&
          lastListChangeIs(changes, 1, list, std::nullopt, OptionValue{"b"}) && !selected,
          "Disabling the selected item hands no selection, with that item as origin");
    auto s = list.describe();
    check(s->items.size() == 2 && s->items[1].value == OptionValue{"b"} && !s->items[1].enabled && !s->items[1].selected,
          "The disabled item stays listed, unselected");
}
// #99 keeps these list behaviors as they were.
void listSelectingTheSelectedItemWritesNothing() {
    std::optional<OptionValue> selected{OptionValue{"a"}}; int writes = 0;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>& change) { ++writes; selected = change.value; return Result{}; });
    check(bool(list.select({"a"})) && writes == 0 && selected == OptionValue{"a"}, "Selecting the selected item writes nothing");
    list.focus(); detail::access(ui).keyDown({.key = Key::space});
    check(writes == 0, "Space on the selected row writes nothing either");
}
void listRejectedWriteKeepsTheSelection() {
    std::optional<OptionValue> selected{OptionValue{"a"}}; int writes = 0;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}, {{"b"}, u"B"}}).value.value();
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    list.connectImmediate([&](const Change<ListHandle>&) { ++writes; return Result{Status::invalidValue, u"Not now"}; });
    auto result = list.select({"b"});
    check(writes == 1 && result.status == Status::invalidValue && result.message == u"Not now",
          "A rejected write returns the application's result as is");
    auto s = list.describe();
    check(selected == OptionValue{"a"} && s->items[0].selected && !s->items[1].selected, "A rejected write leaves the selection unchanged");
}
void listWriteRequiresASourceFirst() {
    std::optional<OptionValue> selected; int writes = 0;
    Ui ui;
    auto list = ui.addList({.id = "list"}, {{{"a"}, u"A"}}).value.value();
    check(list.connectImmediate([&](const Change<ListHandle>&) { ++writes; return Result{}; }).status == Status::invalidValue,
          "A list without a read source refuses a write connection");
    list.connectSource({[&] { return selected; }, [](std::function<void()>) { return [] {}; }});
    check(list.select({"a"}).status == Status::invalidValue && writes == 0, "The refused write was not kept");
}
// #99: a label also takes a full read source; a notification asks for a repaint and the text is read again when drawn.
void labelSourceNotificationRepaintsTheNewText() {
    std::u16string clock = u"10:00"; std::function<void()> changed;
    Ui ui;
    auto label = ui.addLabel({.id = "clock"}).value.value();
    int frames = 0; detail::access(ui).onInvalidate([&] { ++frames; });
    check(bool(label.connectSource({[&] { return clock; }, [&](std::function<void()> callback) {
        changed = std::move(callback); return [&] { changed = {}; };
    }})) && label.describe()->value == u"10:00", "A label reads its text from the source");
    clock = u"10:01"; frames = 0; changed();
    check(frames == 1 && label.describe()->value == u"10:01", "A notification asks for a repaint that shows the new text");
}
void labelSourceIsReplacedAndRejectsIncompleteSources() {
    std::function<void()> first, second;
    Ui ui;
    auto label = ui.addLabel({.id = "status"}).value.value();
    auto subscribeTo = [](std::function<void()>& slot) {
        return [&slot](std::function<void()> callback) { slot = std::move(callback); return [&slot] { slot = {}; }; };
    };
    label.connectSource({[] { return std::u16string(u"one"); }, subscribeTo(first)});
    check(bool(label.connectSource({[] { return std::u16string(u"two"); }, subscribeTo(second)})) && !first && second &&
          label.describe()->value == u"two", "Connecting again replaces the source and cancels the old subscription");
    check(bool(label.connectSource([] { return std::u16string(u"three"); })) && !second && label.describe()->value == u"three",
          "The short form also cancels the subscription it replaces");
    label.connectSource({[] { return std::u16string(u"four"); }, subscribeTo(first)});
    std::function<std::u16string()> read = [] { return std::u16string(u"five"); };
    check(label.connectSource({{}, {}}).status == Status::invalidValue && label.connectSource({read, {}}).status == Status::invalidValue &&
          label.connectSource({{}, subscribeTo(second)}).status == Status::invalidValue,
          "A source without read or subscribe is refused");
    check(first && !second && label.describe()->value == u"four", "A refused source keeps the current connection");
}
void labelSourceNotificationsEndWithTheUi() {
    std::function<void()> changed, kept; int frames = 0;
    LabelHandle label;
    {
        Ui ui;
        label = ui.addLabel({.id = "progress-text"}).value.value();
        detail::access(ui).onInvalidate([&] { ++frames; });
        label.connectSource({[] { return std::u16string(u"busy"); }, [&](std::function<void()> callback) {
            changed = callback; kept = std::move(callback); return [&] { changed = {}; };
        }});
    }
    check(!changed, "Ending the Ui cancels the label's subscription");
    frames = 0; kept();
    check(frames == 0 && label.describe().status == Status::notFound, "A notification after the Ui is gone is ignored");
}
// #99 keeps the label's short form (a read function only) as it was.
void labelReadFunctionIsReadOnDemandAndReplaced() {
    std::u16string first = u"first", second = u"second";
    Ui ui;
    auto label = ui.addLabel({.id = "label"}).value.value();
    check(bool(label.connectSource([&] { return first; })) && label.describe()->value == u"first", "A label reads its text from the function");
    first = u"changed";
    check(label.describe()->value == u"changed", "The function is read again on every read");
    check(bool(label.connectSource([&] { return second; })) && label.describe()->value == u"second", "Connecting again replaces the function");
    check(label.connectSource(std::function<std::u16string()>{}).status == Status::invalidValue, "An empty function is refused");
}

void pointerDispatchRechecksARemovedFocusTarget() {
    Ui ui;
    auto button = ui.addButton({.id = "button"}).value.value();
    auto& host = detail::access(ui);
    host.onFocus([&](const auto&, const auto& to) { if (to == "button") button.remove(); });
    check(host.pointerDown("button").status == Status::notFound, "A target removed by focus notification is rejected before pointer dispatch");
    check(button.describe().status == Status::notFound, "The accepted removal commits after pointer handling");
}

void removingConfirmationButtonReleasesItsFields() {
    Ui ui;
    auto field = ui.addTextField({.id = "field"}).value.value();
    field.connectSource({[] { return std::u16string(u"value"); }, [](std::function<void()>) { return [] {}; }});
    auto owner = ui.addButton({.id = "owner"}).value.value();
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> released = capture;
    check(bool(ui.connectConfirmation(owner, {field}, [capture](const auto&) { return ConfirmationResult{}; })), "Connect first group");
    capture.reset();
    check(bool(owner.remove()) && released.expired(), "Removing owner releases confirmation callback");
    auto next = ui.addButton({.id = "next"}).value.value();
    check(bool(ui.connectConfirmation(next, {field}, [](const auto&) { return ConfirmationResult{}; })),
          "Removed group releases the field for another connection");
}

void repeatedRemovalReclaimsComponentStorage() {
    Ui ui;
    auto tabs = ui.addTabs({.id = "tabs"}, {{{"page"}, u"Page", true, {}}}).value.value();
    auto& host = detail::access(ui);
    const auto baseline = host.storageCounts();
    ButtonHandle stale;
    TooltipHandle oldTip;
    for (int i = 0; i < 1000; ++i) {
        auto button = ui.addButton({.id = "temporary"}).value.value();
        check(bool(tabs.addMember({"page"}, button)), "Attach replacement");
        auto tip = ui.addTooltip("form.title.pages.v03.half.tip", button, u"Tip").value.value();
        auto capture = std::make_shared<int>(42);
        std::weak_ptr<int> released = capture;
        button.bind([capture] { return Result{}; });
        capture.reset();
        host.setError("temporary", u"error");
        if (i == 0) { stale = button; oldTip = tip; }
        check(bool(button.remove()), "Remove replacement");
        check(host.storageCounts() == baseline && released.expired(), "All structural entries and action captures are reclaimed");
        check(stale.describe().status == Status::notFound && oldTip.visible().status == Status::notFound,
              "Old button and tooltip remain invalid after ID reuse");
    }
}

void pageAcceptsNewMembersOfDifferentTypes() {
    Ui ui, other;
    auto tabs = ui.addTabs({.id = "tabs"}, {{{"one"}, u"One", true, {}}, {{"two"}, u"Two", true, {}}}).value.value();
    auto label = ui.addLabel({.id = "label", .value = u"Label"}).value.value();
    auto button = ui.addButton({.id = "button"}).value.value();
    auto foreign = other.addButton({.id = "foreign"}).value.value();
    check(bool(tabs.addMember({"two"}, label)) && bool(tabs.addMember({"two"}, button)), "A page accepts a label and a button");
    check(detail::access(ui).available("button").status == Status::hidden, "New members obey page visibility");
    check(tabs.addMember({"one"}, button).status == Status::invalidValue &&
          tabs.addMember({"one"}, foreign).status == Status::invalidValue, "Duplicate and foreign members are rejected");
    check(bool(tabs.activate({"two"})) && bool(detail::access(ui).available("button")), "New members follow activation");
}

void removalWaitsForActionAndStaleHandleStaysInvalid() {
    Ui ui;
    auto button = ui.addButton({.id = "temporary"}).value.value();
    auto& host = detail::access(ui);
    bool aliveInCallback = false, deniedInCallback = false;
    Status requested = Status::failed;
    button.bind([&] {
        requested = button.remove().status;
        aliveInCallback = host.find("temporary") != nullptr;
        deniedInCallback = button.invoke().status == Status::notFound;
        return Result{};
    });
    check(bool(button.invoke()), "The action finishes normally");
    check(requested == Status::ok && aliveInCallback && deniedInCallback, "Removal is accepted, dispatch is denied, and storage survives the callback");
    check(!host.find("temporary") && button.invoke().status == Status::notFound, "Removal commits after dispatch");
    auto replacement = ui.addButton({.id = "temporary"}).value.value();
    check(bool(replacement.describe()) && button.describe().status == Status::notFound,
          "A stale handle cannot address a replacement with the same ID");
}

// Applicability belongs to the registered type, independently of a snapshot's current state.
void componentTypeReportsStateApplicability() {
    using S = detail::ComponentState;
    struct Row { ComponentKind kind; std::vector<S> states; };
    const Row rows[] = {
        {ComponentKind::label, {S::normal}},
        {ComponentKind::button, {S::normal, S::disabled, S::executing}},
        {ComponentKind::textField, {S::normal, S::disabled, S::readOnly, S::error, S::conflict}},
        {ComponentKind::checkbox, {S::normal, S::disabled, S::selected, S::error, S::conflict}},
        {ComponentKind::radioGroup, {S::normal, S::disabled, S::selected, S::error, S::conflict}},
        {ComponentKind::dropdown, {S::normal, S::disabled, S::candidate, S::error, S::conflict}},
        {ComponentKind::slider, {S::normal, S::disabled, S::error, S::conflict}},
        {ComponentKind::list, {S::normal, S::disabled, S::selected, S::candidate}},
        {ComponentKind::tabs, {S::normal, S::disabled, S::selected, S::candidate, S::error}},
        {ComponentKind::progress, {S::normal}},

    };
    constexpr S all[] = {S::normal, S::disabled, S::readOnly, S::selected, S::candidate, S::error, S::conflict, S::executing};
    for (const auto& row : rows) for (auto state : all) {
        bool expected = std::find(row.states.begin(), row.states.end(), state) != row.states.end();
        check(detail::componentType(row.kind).supportsState(state) == expected,
              "The registered component type reports its applicable states");
    }
}

// ADR 0003, PR #103 review item 8: every component kind whose connectSource subscribes to the application's source, driven
// generically. A probe connects a source that reads one of the kind's three values (A1, A2, B) and shows what the
// component holds through its description.
using Subscribe = std::function<std::function<void()>(std::function<void()>)>;
struct SourceProbe {
    std::function<Result(std::function<int()> pick, Subscribe subscribe)> connect;
    std::function<std::u16string()> shown;
};
std::u16string shownBy(const Query<ComponentSnapshot>& s) {
    if (!s) return u"<" + s.message + u">";
    std::u16string shown = s->value + u"|";
    if (s->numericValue) for (char c : std::to_string(*s->numericValue)) shown += char16_t(c);
    if (s->checked) shown += *s->checked ? u"|checked" : u"|unchecked";
    for (const auto& item : s->items) if (item.selected) shown += u"|" + item.label;
    return shown;
}
template <class T, class Handle, class Connect> SourceProbe probeOf(Handle handle, std::array<T, 3> values, Connect connect) {
    return {[handle, values, connect](std::function<int()> pick, Subscribe subscribe) {
                return connect(handle, Source<T>{[values, pick] { return values[pick()]; }, std::move(subscribe)});
            },
            [handle] { return shownBy(handle.describe()); }};
}
template <class T, class Handle> SourceProbe probeOf(Handle handle, std::array<T, 3> values) {
    return probeOf(handle, values, [](const Handle& h, Source<T> source) { return h.connectSource(std::move(source)); });
}
// When connectSource first reads the new source: never (the value is read when shown), before the old connection is
// replaced (a throwing read keeps it), or after (the old connection is gone, so a throwing read stops the Ui).
enum class FirstRead { whenShown, beforeReplacing, afterReplacing };
struct SourceKind {
    const char* name; FirstRead firstRead; SourceProbe (*build)(Ui&);
    SourceProbe make(Ui& ui) const {
        try { return build(ui); } catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": cannot add the component: " + e.what()); }
    }
};
const std::vector<OptionItem> compass{{{"north"}, u"North"}, {{"south"}, u"South"}, {{"east"}, u"East"}};
using Pick = std::optional<OptionValue>;
const SourceKind sourceKinds[] = {
    {"progress", FirstRead::whenShown, [](Ui& ui) { return probeOf<int>(ui.addProgress({.id = "probe"}).value.value(), {10, 40, 90}); }},
    // A boolean has two values: B only has to differ from A1, the value it must not replace.
    {"checkbox", FirstRead::afterReplacing, [](Ui& ui) { return probeOf<bool>(ui.addCheckbox({.id = "probe"}).value.value(), {false, true, true}); }},
    {"list", FirstRead::whenShown, [](Ui& ui) { return probeOf<Pick>(ui.addList({.id = "probe"}, compass).value.value(), {Pick{{"north"}}, Pick{{"south"}}, Pick{{"east"}}}); }},
    {"radio group", FirstRead::afterReplacing, [](Ui& ui) { return probeOf<Pick>(ui.addRadioGroup({.id = "probe"}, compass).value.value(), {Pick{{"north"}}, Pick{{"south"}}, Pick{{"east"}}}); }},
    {"dropdown", FirstRead::afterReplacing, [](Ui& ui) { return probeOf<Pick>(ui.addDropdown({.id = "probe"}, compass).value.value(), {Pick{{"north"}}, Pick{{"south"}}, Pick{{"east"}}}); }},
    {"slider", FirstRead::beforeReplacing, [](Ui& ui) { return probeOf<int>(ui.addSlider({.id = "probe"}, {0, 100, 5}).value.value(), {10, 40, 90}); }},
    {"text", FirstRead::afterReplacing, [](Ui& ui) { return probeOf<std::u16string>(ui.addTextField({.id = "probe"}).value.value(), {u"alpha", u"beta", u"gamma"}); }},
    {"rich text", FirstRead::afterReplacing, [](Ui& ui) {
        return probeOf<RichText>(ui.addTextField({.id = "probe", .multiline = true, .rich = true}).value.value(), {RichText{u"alpha"}, RichText{u"beta"}, RichText{u"gamma"}},
                                  [](const TextFieldHandle& h, Source<RichText> source) { return h.connectRichSource(std::move(source)); });
    }},
    {"label", FirstRead::whenShown, [](Ui& ui) { return probeOf<std::u16string>(ui.addLabel({.id = "probe"}).value.value(), {u"alpha", u"beta", u"gamma"}); }},
};
// A subscription the test can see: `slot` holds the framework's callback while the subscription is live. The slot
// must outlive the Ui, whose end cancels the subscription it still holds.
Subscribe subscribedInto(std::function<void()>& slot) {
    return [&slot](std::function<void()> callback) { slot = std::move(callback); return [&slot] { slot = {}; }; };
}
void throwingSubscribeKeepsThePreviousConnection() {
    for (const auto& kind : sourceKinds) {
        const std::string name = kind.name;
        auto expect = [&](bool condition, const char* what) { check(condition, (name + ": " + what).c_str()); };
        int aValue = 0; std::function<void()> notifyA;
        Ui ui;
        auto probe = kind.make(ui);
        auto throwing = [](std::function<void()>) -> std::function<void()> { throw std::runtime_error("subscribe failed"); };
        const auto showsUnconnected = probe.shown();
        Result first; bool firstEscaped = false;
        try { first = probe.connect([] { return 2; }, throwing); } catch (...) { firstEscaped = true; }
        expect(!firstEscaped && first.status == Status::failed && !detail::access(ui).stopped(),
               "A throwing first subscribe fails the call without escaping or stopping the Ui");
        expect(probe.shown() == showsUnconnected, "A component whose first subscribe threw stays unconnected");
        expect(bool(probe.connect([&] { return aValue; }, subscribedInto(notifyA))) && notifyA, "Source A connects");
        const auto showsA1 = probe.shown();
        Result result; bool escaped = false;
        try {
            result = probe.connect([] { return 2; }, throwing);
        } catch (...) { escaped = true; }
        expect(!escaped, "A throwing subscribe does not escape connect_source");
        expect(result.status == Status::failed && !result.message.empty(), "A throwing subscribe fails the call with a message");
        expect(!detail::access(ui).stopped(), "The Ui keeps running: the component stayed valid");
        expect(bool(notifyA) && probe.shown() == showsA1, "The component keeps source A, its value and its live subscription");
        aValue = 1; notifyA();
        const auto showsA2 = probe.shown();
        expect(showsA2 != showsA1, "Notifying source A still updates the component");
        expect(bool(probe.connect([] { return 2; }, [](std::function<void()>) { return [] {}; }))  && !notifyA,
               "A later source still replaces A and cancels its subscription");
        expect(probe.shown() != showsA1, "Source B's value was never shown before (it differs from A1)");
    }
}
// A subscribe that notifies before it returns: the first connection and a replacing one both end on the new value.
void synchronousNotificationDuringSubscribeIsSafe() {
    for (const auto& kind : sourceKinds) {
        const std::string name = kind.name;
        auto expect = [&](bool condition, const char* what) { check(condition, (name + ": " + what).c_str()); };
        auto notifying = [](std::function<void()>& slot) -> Subscribe {
            return [&slot](std::function<void()> callback) { callback(); slot = std::move(callback); return [&slot] { slot = {}; }; };
        };
        std::function<void()> first, second, quiet;
        Ui ui, reference;
        auto probe = kind.make(ui), expected = kind.make(reference);
        expect(bool(probe.connect([] { return 0; }, notifying(first))) && first && !detail::access(ui).stopped(),
               "A first source that notifies inside subscribe connects");
        expected.connect([] { return 0; }, subscribedInto(quiet));
        expect(probe.shown() == expected.shown(), "The first source's value is shown");
        expect(bool(probe.connect([] { return 1; }, notifying(second))) && second && !first && !detail::access(ui).stopped(),
               "A replacing source that notifies inside subscribe connects and cancels the old one");
        expected.connect([] { return 1; }, subscribedInto(quiet));
        expect(probe.shown() == expected.shown(), "The replacing source's value is shown");
    }
}
// A source whose read throws while connectSource reads it: before the old connection is replaced the call fails and
// keeps it; after, the call fails and stops the Ui (the established outcome of a callback that threw after state changed).
void throwingReadWhileConnecting() {
    for (const auto& kind : sourceKinds) {
        if (kind.firstRead == FirstRead::whenShown) continue;  // connectSource reads nothing; drawing failures are out of scope
        const std::string name = kind.name;
        auto expect = [&](bool condition, const char* what) { check(condition, (name + ": " + what).c_str()); };
        std::function<void()> notifyA;
        Ui ui;
        auto probe = kind.make(ui);
        auto throwingRead = []() -> int { throw std::runtime_error("read failed"); };
        auto noUpdates = [](std::function<void()>) { return [] {}; };
        {
            Ui fresh;
            auto first = kind.make(fresh).connect(throwingRead, noUpdates);
            expect(first.status == Status::failed && !first.message.empty(), "A throwing first read fails the connection");
        }
        probe.connect([] { return 0; }, subscribedInto(notifyA));
        const auto showsA = probe.shown();
        Result result; bool escaped = false;
        try {
            result = probe.connect(throwingRead, noUpdates);
        } catch (...) { escaped = true; }
        expect(!escaped && result.status == Status::failed && !result.message.empty(), "A throwing read fails the call and does not escape");
        if (kind.firstRead == FirstRead::beforeReplacing)
            expect(!detail::access(ui).stopped() && notifyA && probe.shown() == showsA, "A read before replacing keeps the old connection and the Ui");
        else
            expect(detail::access(ui).stopped(), "A read after replacing stops the Ui");
    }
}
int main() {
    const test::Case cases[] = {
        {"#103 throwing read while connecting", throwingReadWhileConnecting},
        {"#103 throwing subscribe keeps the connection", throwingSubscribeKeepsThePreviousConnection},
        {"#103 synchronous notification during subscribe", synchronousNotificationDuringSubscribeIsSafe},
        {"X04 registered component state applicability", componentTypeReportsStateApplicability},
        {"G07 revalidate after focus callback", pointerDispatchRechecksARemovedFocusTarget},
        {"G07 removal releases confirmation connection", removingConfirmationButtonReleasesItsFields},
        {"G07 bounded removal storage", repeatedRemovalReclaimsComponentStorage},
        {"General page membership", pageAcceptsNewMembersOfDifferentTypes},
        {"G07 deferred removal and stale identity", removalWaitsForActionAndStaleHandleStaysInvalid},
        {"C08 C09 list page admission", listMembersObeyPageScope},
        {"G07 page changes do not recurse", tabChangeNotificationsDoNotRecurse},
        {"C09 disabled and modal admission", tabsRespectModalAndDisabledPointerTargets},
        {"G11 cross-page rejection", crossPageConfirmationRevealsFirstErrorAndMarksOtherPages},
        {"C09 manual tabs and hidden admission", tabsRequireExplicitActivationAndGuardHiddenPages},
        {"L03 list selection survives reorder", listSelectionUsesStableIdentity},
        {"C08 list focus and keyboard", listFocusAndKeyboardUseEnabledItems},
        {"R11 list neighbor follows old identity order", listRecoversCursorWhenDataChanges},
        {"G07 list write cannot reenter", listWriteCannotReenterItself},
        {"#99 list selection hands a change record", listUserSelectionHandsAChangeRecord},
        {"#99 list items dropping the selection", listItemsThatDropTheSelectionHandAnEmptyChange},
        {"#99 list items disabling the selection", listItemsThatDisableTheSelectionHandAnEmptyChange},
        {"#99 list reselection writes nothing",listSelectingTheSelectedItemWritesNothing},
        {"#99 list rejection keeps the selection", listRejectedWriteKeepsTheSelection},
        {"#99 list write requires a source", listWriteRequiresASourceFirst},
        {"#99 label source repaints", labelSourceNotificationRepaintsTheNewText},
        {"#99 label source replaced and validated", labelSourceIsReplacedAndRejectsIncompleteSources},
        {"#99 label source ends with the Ui", labelSourceNotificationsEndWithTheUi},
        {"#99 label read function",labelReadFunctionIsReadOnDemandAndReplaced},
        {"C06 candidate and draft are separate", dropdownCandidateIsNotADraftUntilConfirmed},
        {"F07 dropdown exit and option identity", dropdownLeavingAndReplacedOptionsAreSafe},
        {"G07 dropdown changes do not recurse", dropdownOptionChangesDoNotRecurseIntoNotifications},
        {"G07 dropdown programmatic edits repaint", dropdownProgrammaticEditsRequestAFrame},
        {"G11 edits during numeric submission", sliderGroupPreservesEditsDuringSubmission},
        {"C12 progress source lifecycle", progressSourceIsReadOnlyAndDisconnects},
        {"G10 numeric source and conflict lifecycle", sliderSourceConflictAndInvalidUpdates},
        {"N04 non-divisible steps and keys", sliderNonDivisibleRangeAndKeyboard},
        {"N04 invalid slider ranges", sliderRejectsInvalidRanges},
        {"C07 numeric draft and confirmation", sliderConnectedDraftIsConfirmedAsANumber},
        {"G04 selection operations reflect write admission", unconnectedSelectionControlsDoNotAdvertiseWrites},
        {"G10 standalone typed confirmations", standaloneBooleanAndOptionConfirmation},
        {"G11 radio conflict and latest", radioSourceConflictCanBeResolved},
        {"G11 mixed rejection", mixedRejectionMarksEachFieldAndFocusesFirst},
        {"N03 all disabled options", radioAllDisabledRemainsEmptyAndTabCanLeave},
        {"G11 mixed confirmation batch", mixedConfirmationSubmitsOneBatch},
        {"C05 empty selection and disabled options", radioCanStartEmptyAndSkipDisabledOptions},
        {"C04 keyboard and disabled semantics", checkboxInputAndDisabledStateAgree},
        {"C04 connected checkbox toggles", checkboxTogglesAConnectedBoolean},
        {"Standalone confirmation shares command guard", confirmedFieldRespectsSharedCommandGuard},
        {"Public text operations obey restrictions", publicTextOperationsObeyRestrictions},
        {"Handle queries report expiry", handleQueriesReportExpiry},
        {"Configuration requires UI thread", configurationRequiresUiThread},
        {"G11 confirmation action cannot be replaced", groupActionCannotBeReplaced},
        {"G10 unconnected text rejects edits", unconnectedTextRejectsEdits},
        {"G10 standalone confirmed connection", ungroupedConfirmedConnectionWritesOnlyOnConfirm},
        {"G10 source notifications request a repaint", sourceNotificationsRequestARepaint},
        {"G10 cancel clears rejection without writing", cancellingARejectedDraftClearsItsErrorWithoutWriting},
        {"G10 explicit immediate write retains rejected draft", explicitImmediateWriteKeepsARejectedDraft},
        {"G11 a fresh draft starts at the current source", editingAgainAfterMatchingTheSourceUsesTheNewOrigin},
        {"G11 source updates preserve composition", sourceUpdateDuringCompositionKeepsPreedit},
        {"G10 connected text requires explicit write timing", connectedTextRequiresExplicitWriteTiming},
        {"G11 confirmation blocks reentry but preserves new edits", confirmationBlocksReentryButPreservesNewEdits},
        {"G11 rejected group retains drafts and focuses first error", rejectedGroupKeepsDraftsAndFocusesFirstError},
        {"G11 confirmation submits draft and origin and keeps history", confirmationSubmitsDraftAndOriginAndKeepsHistory},
        {"G11 source conflicts preserve drafts until cancel", sourceConflictKeepsDraftUntilCancelled},
        {"G10 source updates follow component lifetime", sourceUpdatesFollowTheComponentLifetime},
        {"G10 typed handles expire with their component tree", handlesExpireWithTheirComponentTree},
        {"C01 labels associate by target id", labelsAssociateByTargetId},
        {"C01 field error has one source", fieldErrorHasOneSource},
        {"AC11 tab order skips labels and disabled", tabOrderSkipsLabelsAndDisabled},
        {"AC06 button activates once per press", buttonActivatesOncePerPress},
        {"AC09 typing records one history entry per input", typingRecordsOneHistoryEntryPerInput},
        {"AC08 caret moves by code point and extends with Shift", caretMovesByCodePointAndExtendsWithShift},
        {"Clipboard unavailable differs from no text", clipboardReportsUnavailableSeparatelyFromEmpty},
        {"AC09 paste turns each line break into one space", pasteTurnsEachLineBreakIntoOneSpace},
        {"AC10 read-only allows viewing and copying only", readOnlyAllowsViewingAndCopyingOnly},
        {"AC10 disabled field refuses every entry and keeps data", disabledFieldRefusesEveryEntryAndKeepsData},
        {"AC14 composition commits once and never applies", compositionCommitsOnceAndNeverApplies},
        {"E01 composition page switch by pointer", compositionPageSwitchPointer},
        {"G07 focus notifications queue instead of recursing", focusNotificationsQueueInsteadOfRecursing},
        {"AC10 keyboard editing obeys the same admission", keyboardEditingObeysTheSameAdmission},
        {"AC08 over-limit edit is rejected whole", overLimitEditIsRejectedWhole},
        {"invalid text input preserves the selection and redo", invalidTextInputPreservesTheSelectionAndRedo},
        {"standalone preedit replaces a reverse selection", standaloneEditSessionPreeditReplacesAReverseSelection},
        {"public validation and paste normalization agree", publicValidationAndPasteNormalizationAgree},
        {"AC11 owner hears focus moves and semantics list actions", ownerHearsFocusMovesAndSemanticsListActions},
    };
    return test::run("components", cases);
}
