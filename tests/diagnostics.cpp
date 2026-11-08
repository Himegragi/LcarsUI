#include "lcars/diagnostics.hpp"
#include <cstdlib>
#include <sstream>
int main() {
    lcars::Diagnostics log;
    for (int i = 0; i < 1000; ++i) {
        lcars::RenderDiagnostic event;
        event.severity = lcars::DiagnosticSeverity::error;
        event.native_code = i;
        if (i == 0) log.record_failure(event); else log.record(event);
    }
    if (!(log.first_failure()->native_code == 0)) return EXIT_FAILURE;
    if (!(log.recent().size() == 64)) return EXIT_FAILURE;
    if (!(log.recent().front().native_code == 936)) return EXIT_FAILURE;
    lcars::Diagnostics bounded;
    lcars::RenderDiagnostic large; large.native_message=std::string(5000,'x');
    bounded.record_failure(large);
    if (bounded.first_failure()->native_message.size()>4096) return EXIT_FAILURE;
    lcars::Diagnostics details(1);
    lcars::RenderDiagnostic sample; sample.backend=u"OpenGL"; sample.native_message="line\nUTF-8: \xc3\xa9";
    sample.backend_detail="future-format"; sample.component_id="button\t1"; sample.operation_id="op";
    sample.native_code=123; sample.source_time=std::chrono::system_clock::time_point(std::chrono::seconds(42));
    details.record(sample);
    std::stringstream detail_stream; if (!details.save(detail_stream)) return EXIT_FAILURE;
    auto detail_loaded=lcars::Diagnostics::load(detail_stream);
    if (detail_loaded.incomplete || detail_loaded.records.size()!=1) return EXIT_FAILURE;
    auto copy=detail_loaded.records[0];
    if (copy.backend!=sample.backend || copy.native_message!=sample.native_message || copy.component_id!=sample.component_id || copy.operation_id!=sample.operation_id || copy.native_code!=sample.native_code || copy.source_time!=sample.source_time || copy.backend_detail!=sample.backend_detail || !copy.backend_detail_uninterpreted) return EXIT_FAILURE;
    auto corrupted=detail_stream.str(); corrupted[corrupted.find("future") == std::string::npos ? 18 : corrupted.find("future")]='X';
    std::istringstream bad(corrupted); if (!lcars::Diagnostics::load(bad).incomplete) return EXIT_FAILURE;
    std::stringstream stream;
    if (!log.save(stream)) return EXIT_FAILURE;
    auto loaded = lcars::Diagnostics::load(stream);
    if (loaded.incomplete || loaded.records.size() != 64 || !loaded.first_failure || loaded.first_failure->native_code != 0 || loaded.records.back().native_code != 999) return EXIT_FAILURE;
    auto bytes = stream.str(); bytes.pop_back();
    std::istringstream truncated(bytes);
    auto partial = lcars::Diagnostics::load(truncated);
    if (!partial.incomplete || partial.records.size() != 63 || !partial.first_failure) return EXIT_FAILURE;
}
