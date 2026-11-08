#include "lcars/core.hpp"
#include <stdexcept>
#include <string>
using namespace lcars;
static void check(bool ok) { if (!ok) throw std::runtime_error("SEM01 failed"); }
int main() {
    Ui ui;
    auto first = ui.add_text({.id = "form.name", .kind = Kind::text});
    check(bool(first));
    auto duplicate = ui.add_button({.id = "form.name", .kind = Kind::button});
    check(duplicate.status == Status::invalid_value && !duplicate.value && bool(first.value->describe()));
    auto empty = ui.add_list({.id = "", .kind = Kind::list}, {});
    check(empty.status == Status::invalid_value && !empty.value);
#ifndef NDEBUG
    check(ui.diagnostics().size() == 2);
    check(ui.diagnostics()[0].find("form.name") != std::string::npos && ui.diagnostics()[0].find("text") != std::string::npos);
#endif
    auto source = ui.add_button({.id = "source", .kind = Kind::button}).value.value();
    auto tip = ui.add_tooltip("source.tip", source, u"Help");
    check(bool(tip));
    check(ui.add_button({.id = "source.tip", .kind = Kind::button}).status == Status::invalid_value);
    check(ui.add_tooltip("form.name", source, u"Help").status == Status::invalid_value);
    check(bool(source.remove()));
    check(bool(ui.add_button({.id = "source.tip", .kind = Kind::button})));
    auto button = ui.add_button({.id = "form.title.pages.v03.temporary", .kind = Kind::button}).value.value();
    Status pending = Status::ok;
    button.bind([&] {
        check(bool(button.remove()));
        pending = ui.add_button({.id = "form.title.pages.v03.temporary", .kind = Kind::button}).status;
        return Result{};
    });
    check(bool(button.invoke()) && pending == Status::invalid_value);
    auto replacement = ui.add_button({.id = "form.title.pages.v03.temporary", .kind = Kind::button});
    check(bool(replacement) && button.describe().status == Status::not_found);
}
