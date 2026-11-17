#include "rich_text.hpp"
#include "settings_json.hpp"
#include "isoduo/diagnostics.hpp"
#include "check.hpp"
#include <sstream>
using test::check;
int main() {
    // Independent old wire fixture: byte lengths, UTF-8 text and all style attributes.
    const std::string oldRich = "LcarsUI.RichText.1\n6\nA\ncafe\n1\n2 4 7 3:red\n";
    auto rich = isoduo::detail::parseRichPayload(oldRich);
    check(rich && rich->text == u"A\ncafe" && rich->spans.size() == 1 && rich->spans[0].bold && rich->spans[0].italic && rich->spans[0].underline && rich->spans[0].color == "red", "old clipboard preserves text and styles");
    auto payload = isoduo::detail::serializeRichPayload(*rich);
    check(payload.starts_with("Isoduo.RichText.1\n") && isoduo::detail::parseRichPayload(payload) == rich, "clipboard rewrites with canonical header and identical value");
    check(!isoduo::detail::parseRichPayload("Other.RichText.1\n6\nA\ncafe\n0\n"), "unrelated clipboard headers are rejected");
    const std::string oldSettings = R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":"backup"}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto settings = settingsDemo::parseSettings(oldSettings);
    check(settings && settings->items[0].note == u"backup", "old settings data remains readable");
    auto bytes = settingsDemo::writeSettings(*settings);
    check(bytes.find("\"format\": \"isoduo-settings-demo\"") != std::string::npos && settingsDemo::parseSettings(bytes) == settings, "old settings save with canonical format and identical data");
    isoduo::Diagnostics diagnostics;
    isoduo::RenderDiagnostic event; event.nativeCode = 42; event.nativeMessage = "saved failure";
    diagnostics.recordFailure(event);
    std::stringstream output; check(diagnostics.save(output), "diagnostic save");
    auto canonical = output.str();
    check(canonical.starts_with("ISODUODIAG 1 0\n"), "diagnostics use canonical magic");
    auto legacy = canonical; legacy.replace(0, std::string("ISODUODIAG").size(), "LCARSDIAG");
    std::istringstream input(legacy); auto loaded = isoduo::Diagnostics::load(input);
    check(!loaded.incomplete && loaded.firstFailure && loaded.firstFailure->nativeCode == 42 && loaded.records.size() == 1, "old diagnostics retain failure and recent record");
    isoduo::Diagnostics rewritten; rewritten.recordFailure(*loaded.firstFailure);
    std::stringstream saved; check(rewritten.save(saved) && saved.str() == canonical, "old diagnostics rewrite to identical canonical export");
}
