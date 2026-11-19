#include "isoduo/diagnostics.hpp"
#include <cstdlib>
#include <sstream>
int main() {
    isoduo::Diagnostics log;
    isoduo::RenderDiagnostic observation;
    observation.severity = isoduo::DiagnosticSeverity::error;
    observation.nativeCode = -1;
    log.record(observation);
    if (log.firstFailure() || log.recent().size() != 1 || log.recent().back().nativeCode != -1) return EXIT_FAILURE;
    for (int i = 0; i < 1000; ++i) {
        isoduo::RenderDiagnostic event;
        event.severity = isoduo::DiagnosticSeverity::error;
        event.nativeCode = i;
        if (i == 0) log.recordFailure(event); else log.record(event);
    }
    if (!(log.firstFailure()->nativeCode == 0)) return EXIT_FAILURE;
    if (!(log.recent().size() == 64)) return EXIT_FAILURE;
    if (!(log.recent().front().nativeCode == 936)) return EXIT_FAILURE;
    isoduo::Diagnostics bounded;
    isoduo::RenderDiagnostic large; large.nativeMessage=std::string(5000,'x');
    bounded.recordFailure(large);
    if (bounded.firstFailure()->nativeMessage.size()>4096) return EXIT_FAILURE;
    isoduo::Diagnostics details(1);
    isoduo::RenderDiagnostic sample; sample.backend=u"OpenGL"; sample.nativeMessage="line\nUTF-8: \xc3\xa9";
    sample.backendDetail="future-format"; sample.componentId="button\t1"; sample.operationId="op";
    sample.nativeCode=123; sample.sourceTime=std::chrono::system_clock::time_point(std::chrono::seconds(42));
    sample.receivedAt=std::chrono::system_clock::time_point(std::chrono::milliseconds(42456));
    sample.uiEnvironment=&details;
    details.record(sample);
    std::stringstream detailStream; if (!details.save(detailStream)) return EXIT_FAILURE;
    auto detailLoaded=isoduo::Diagnostics::load(detailStream);
    if (detailLoaded.incomplete || detailLoaded.records.size()!=1) return EXIT_FAILURE;
    auto copy=detailLoaded.records[0];
    if (copy.receivedAt != sample.receivedAt || copy.uiEnvironment != nullptr) return EXIT_FAILURE;
    if (copy.backend!=sample.backend || copy.nativeMessage!=sample.nativeMessage || copy.componentId!=sample.componentId || copy.operationId!=sample.operationId || copy.nativeCode!=sample.nativeCode || copy.sourceTime!=sample.sourceTime || copy.backendDetail!=sample.backendDetail || !copy.backendDetailUninterpreted) return EXIT_FAILURE;
    auto corrupted=detailStream.str(); corrupted[corrupted.find("future") == std::string::npos ? 18 : corrupted.find("future")]='X';
    std::istringstream bad(corrupted); if (!isoduo::Diagnostics::load(bad).incomplete) return EXIT_FAILURE;
    std::stringstream stream;
    if (!log.save(stream)) return EXIT_FAILURE;
    auto loaded = isoduo::Diagnostics::load(stream);
    if (loaded.incomplete || loaded.records.size() != 64 || !loaded.firstFailure || loaded.firstFailure->nativeCode != 0 || loaded.records.back().nativeCode != 999) return EXIT_FAILURE;
    auto bytes = stream.str(); bytes.pop_back();
    std::istringstream truncated(bytes);
    auto partial = isoduo::Diagnostics::load(truncated);
    if (!partial.incomplete || partial.records.size() != 63 || !partial.firstFailure ||
        partial.firstFailure->nativeCode != 0 || partial.records.back().nativeCode != 998) return EXIT_FAILURE;
    bytes = stream.str();
    bytes[bytes.size() - 2] = bytes[bytes.size() - 2] == '0' ? '1' : '0';
    std::istringstream lateCorruption(bytes);
    auto verifiedPrefix = isoduo::Diagnostics::load(lateCorruption);
    if (!verifiedPrefix.incomplete || verifiedPrefix.records.size() != 63 || !verifiedPrefix.firstFailure ||
        verifiedPrefix.firstFailure->nativeCode != 0 || verifiedPrefix.records.back().nativeCode != 998)
        return EXIT_FAILURE;
}
