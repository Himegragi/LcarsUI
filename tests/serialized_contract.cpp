#include "rich_text_contract.hpp"
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
    check(test::canonicalRichRoundTrip(payload, *rich), "clipboard rewrites with canonical header and identical value");
    check(!isoduo::detail::parseRichPayload("Other.RichText.1\n6\nA\ncafe\n0\n"), "unrelated clipboard headers are rejected");
    const std::string oldSettings = R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":"backup"}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto settings = settingsDemo::parseSettings(oldSettings);
    check(settings && settings->items[0].note == u"backup", "old settings data remains readable");
    auto bytes = settingsDemo::writeSettings(*settings);
    check(bytes.find("\"format\": \"isoduo-settings-demo\"") != std::string::npos && settingsDemo::parseSettings(bytes) == settings, "old settings save with canonical format and identical data");
    // Export captured from the LcarsUI writer at 8895a63 with fixed timestamps.
    constexpr std::string_view records =
        "F 0047004c 2 4 7265736574 42 7361766564206661696c757265 3000000000 1 2000000000 70616e656c 64726177 6f7061717565 7 fe7134bc\n"
        "R 0047004c 2 4 7265736574 42 7361766564206661696c757265 3000000000 1 2000000000 70616e656c 64726177 6f7061717565 7 50aa30d9\n"
        "R 0047004c 2 4 6c61746572 43 7361766564206661696c757265 3000000000 1 2000000000 70616e656c 64726177 6f7061717565 7 19054ed5\n";
    std::istringstream input("LCARSDIAG 1 0\n" + std::string(records));
    auto loaded = isoduo::Diagnostics::load(input);
    check(!loaded.incomplete && loaded.firstFailure && loaded.records.size() == 2,
          "old diagnostics retain first failure and both recent records");
    const auto& first = *loaded.firstFailure;
    using namespace std::chrono;
    check(first.backend == u"GL" && first.severity == isoduo::DiagnosticSeverity::error &&
          first.kind == isoduo::DiagnosticKind::deviceReset && first.code == "reset" &&
          first.nativeCode == 42 && first.nativeMessage == "saved failure" &&
          first.receivedAt == system_clock::time_point(seconds(3)) &&
          first.sourceTime == system_clock::time_point(seconds(2)) &&
          first.componentId == "panel" && first.operationId == "draw" &&
          first.backendDetail == "opaque" && first.backendDetailUninterpreted &&
          first.omittedBefore == 7, "old first failure fields retain their meaning");
    check(loaded.records[0].code == "reset" && loaded.records[0].nativeCode == 42 &&
          loaded.records[1].code == "later" && loaded.records[1].nativeCode == 43,
          "old recent records retain their order and values");
    // recordFailure also adds a recent record. Consume that slot on replay by
    // limiting the buffer to the legacy record count.
    isoduo::Diagnostics replay(loaded.records.size());
    replay.recordFailure(first);
    for (const auto& record : loaded.records) replay.record(record);
    std::ostringstream saved;
    check(replay.save(saved) && saved.str() == "ISODUODIAG 1 0\n" + std::string(records),
          "old diagnostics rewrite with canonical magic and identical records");
}
