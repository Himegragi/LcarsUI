#include "diagnostic_queue.hpp"
#include <stdexcept>
using namespace lcars;
using namespace lcars::detail;
static void check(bool v) { if (!v) throw std::runtime_error("diagnostic queue"); }
int main() {
    std::size_t count=0, omitted=0; bool nested=false;
    DiagnosticQueue* ptr=nullptr;
    DiagnosticQueue queue([&](const RenderDiagnostic& e) { ++count; omitted+=e.omitted_before; if (!nested) { nested=true; ptr->drain(); } }, nullptr);
    ptr=&queue;
    for (int i=0;i<1000;++i) queue.push({});
    queue.drain(); check(count==256 && omitted==744);
    int lines=0; DiagnosticQueue output({},nullptr); output.set_output([&](const std::wstring&) { ++lines; }); output.push({}); output.drain();
#ifdef NDEBUG
    check(lines==0);
#else
    check(lines==1);
#endif
}
