#include "lcars/diagnostics.hpp"
#include <cstdlib>
#include <sstream>
int main() {
    lcars::Diagnostics log;
    for (int i = 0; i < 1000; ++i) {
        lcars::RenderDiagnostic event;
        event.severity = lcars::DiagnosticSeverity::error;
        event.nativeCode = i;
        if (i == 0) log.recordFailure(event); else log.record(event);
    }
    if (!(log.firstFailure()->nativeCode == 0)) return EXIT_FAILURE;
    if (!(log.recent().size() == 64)) return EXIT_FAILURE;
    if (!(log.recent().front().nativeCode == 936)) return EXIT_FAILURE;
    lcars::Diagnostics bounded;
    lcars::RenderDiagnostic large; large.nativeMessage=std::string(5000,'x');
    bounded.recordFailure(large);
    if (bounded.firstFailure()->nativeMessage.size()>4096) return EXIT_FAILURE;
    lcars::Diagnostics details(1);
    lcars::RenderDiagnostic sample; sample.backend=u"OpenGL"; sample.nativeMessage="line\nUTF-8: \xc3\xa9";
    sample.backendDetail="future-format"; sample.componentId="button\t1"; sample.operationId="op";
    sample.nativeCode=123; sample.sourceTime=std::chrono::system_clock::time_point(std::chrono::seconds(42));
    details.record(sample);
    std::stringstream detail_stream; if (!details.save(detail_stream)) return EXIT_FAILURE;
    auto detail_loaded=lcars::Diagnostics::load(detail_stream);
    if (detail_loaded.incomplete || detail_loaded.records.size()!=1) return EXIT_FAILURE;
    auto copy=detail_loaded.records[0];
    if (copy.backend!=sample.backend || copy.nativeMessage!=sample.nativeMessage || copy.componentId!=sample.componentId || copy.operationId!=sample.operationId || copy.nativeCode!=sample.nativeCode || copy.sourceTime!=sample.sourceTime || copy.backendDetail!=sample.backendDetail || !copy.backendDetailUninterpreted) return EXIT_FAILURE;
    auto corrupted=detail_stream.str(); corrupted[corrupted.find("future") == std::string::npos ? 18 : corrupted.find("future")]='X';
    std::istringstream bad(corrupted); if (!lcars::Diagnostics::load(bad).incomplete) return EXIT_FAILURE;
    std::stringstream stream;
    if (!log.save(stream)) return EXIT_FAILURE;
    auto loaded = lcars::Diagnostics::load(stream);
    if (loaded.incomplete || loaded.records.size() != 64 || !loaded.firstFailure || loaded.firstFailure->nativeCode != 0 || loaded.records.back().nativeCode != 999) return EXIT_FAILURE;
    auto bytes = stream.str(); bytes.pop_back();
    std::istringstream truncated(bytes);
    auto partial = lcars::Diagnostics::load(truncated);
    if (!partial.incomplete || partial.records.size() != 63 || !partial.firstFailure) return EXIT_FAILURE;
}
