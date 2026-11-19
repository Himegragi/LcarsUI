#include "diagnostic_queue.hpp"
#include "check.hpp"

using namespace isoduo;
using namespace isoduo::detail;

int main() {
    std::size_t delivered = 0, omitted = 0;
    bool receivedFollowUp = false;
    DiagnosticQueue* active = nullptr;
    DiagnosticQueue queue([&](const RenderDiagnostic& event) {
        ++delivered;
        omitted += event.omittedBefore;
        if (delivered == 1) {
            RenderDiagnostic followUp;
            followUp.code = "follow-up";
            active->push(std::move(followUp));
            active->drain();
        }
        if (event.code == "follow-up") receivedFollowUp = true;
    }, nullptr);
    active = &queue;
    for (int i = 0; i < 1000; ++i) queue.push({});
    queue.drain();
    test::check(delivered == 256 && omitted == 744, "Bound the queue and report all omitted diagnostics");
    test::check(!receivedFollowUp, "Callback dispatch cannot reenter");
    queue.drain();
    test::check(delivered == 257 && receivedFollowUp, "Deliver callback-enqueued diagnostics on the next drain");

    int lines = 0;
    DiagnosticQueue output({}, nullptr);
    output.setOutput([&](const std::wstring&) { ++lines; });
    output.push({});
    output.drain();
#ifdef NDEBUG
    test::check(lines == 0, "Release stays quiet without a receiver");
#else
    test::check(lines == 1, "Debug reports to the developer sink without a receiver");
#endif
}
