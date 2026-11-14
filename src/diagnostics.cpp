#include "lcars/diagnostics.hpp"
namespace lcars {
Diagnostics::Diagnostics(std::size_t capacity) : capacity_(capacity) {}
void Diagnostics::record(const RenderDiagnostic& event) {
    if (!capacity_) return;
    if (recent_.size() == capacity_) recent_.pop_front();
    recent_.push_back(event);
}
void Diagnostics::record_failure(const RenderDiagnostic& event) {
    if (!first_) {
        first_ = event;
        constexpr std::size_t limit = 4096;
        auto trim=[](std::string& value) { if (value.size()>limit) { std::size_t end=limit; while (end && (static_cast<unsigned char>(value[end]) & 0xc0)==0x80) --end; value.resize(end); } };
        if (first_->backend.size()>limit) first_->backend.resize(limit);
        trim(first_->code); trim(first_->native_message); trim(first_->component_id);
        trim(first_->operation_id); trim(first_->backend_detail);
    }
    record(event);
}
const std::optional<RenderDiagnostic>& Diagnostics::first_failure() const { return first_; }
const std::deque<RenderDiagnostic>& Diagnostics::recent() const { return recent_; }
}
#include <istream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <charconv>

namespace lcars {
namespace {
std::uint32_t crc32(const std::string& text) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char c : text) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
std::string hex(const std::string& s) {
    constexpr char digits[] = "0123456789abcdef"; std::string result;
    for (unsigned char c : s) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result.empty() ? "-" : result;
}
bool unhex(const std::string& s, std::string& out) {
    if (s == "-") return true;
    if (s.size() % 2) return false;
    auto digit=[](char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; };
    for (std::size_t i=0; i<s.size(); i+=2) { int a=digit(s[i]), b=digit(s[i+1]); if (a<0 || b<0) return false; out += static_cast<char>(a*16+b); }
    return true;
}
std::string pack(const RenderDiagnostic& e) {
    std::string backend;
    for (char16_t c : e.backend) { backend += static_cast<char>(c >> 8); backend += static_cast<char>(c & 255); }
    auto ticks=[](auto t) { return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count(); };
    std::ostringstream s;
    s << hex(backend) << ' ' << static_cast<int>(e.severity) << ' ' << static_cast<int>(e.kind) << ' ' << hex(e.code) << ' ' << e.native_code << ' ' << hex(e.native_message) << ' ' << ticks(e.received_at) << ' ' << (e.source_time ? 1 : 0) << ' ' << (e.source_time ? ticks(*e.source_time) : 0) << ' ' << hex(e.component_id) << ' ' << hex(e.operation_id) << ' ' << hex(e.backend_detail) << ' ' << e.omitted_before;
    return s.str();
}
bool unpack(const std::string& line, RenderDiagnostic& e) {
    std::istringstream s(line); std::string b,c,m,component,operation,detail,extra; int severity,category,has; long long received,source;
    if (!(s>>b>>severity>>category>>c>>e.native_code>>m>>received>>has>>source>>component>>operation>>detail>>e.omitted_before) || (s>>extra) || severity<0 || severity>2 || category<0 || category>5 || (has!=0 && has!=1)) return false;
    std::string backend;
    if (!unhex(b,backend) || backend.size()%2 || !unhex(c,e.code) || !unhex(m,e.native_message) || !unhex(component,e.component_id) || !unhex(operation,e.operation_id) || !unhex(detail,e.backend_detail)) return false;
    for (std::size_t i=0;i<backend.size();i+=2) e.backend += static_cast<char16_t>((static_cast<unsigned char>(backend[i])<<8)|static_cast<unsigned char>(backend[i+1]));
    e.severity=static_cast<DiagnosticSeverity>(severity); e.kind=static_cast<DiagnosticKind>(category);
    e.received_at=std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(received)));
    if (has) e.source_time=std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(source)));
    e.backend_detail_uninterpreted=!e.backend_detail.empty();
    return true;
}
}
bool Diagnostics::save(std::ostream& out) const {
    out << "LCARSDIAG 1 0\n";
    auto write=[&](char kind, const RenderDiagnostic& e) { auto line=std::string(1,kind)+" "+pack(e); out << line << ' ' << std::hex << std::setw(8) << std::setfill('0') << crc32(line) << std::dec << '\n'; };
    if (first_) write('F',*first_);
    for (const auto& e : recent_) write('R',e);
    return bool(out);
}
DiagnosticLoadResult Diagnostics::load(std::istream& in) {
    DiagnosticLoadResult result; std::string line;
    if (!std::getline(in,line) || line != "LCARSDIAG 1 0" || in.eof()) { result.incomplete=true; return result; }
    while (std::getline(in,line)) {
        if (in.eof()) { result.incomplete=true; break; }
        auto pos=line.rfind(' '); std::uint32_t expected=0;
        if (pos==std::string::npos || line.size()-pos-1!=8) { result.incomplete=true; break; }
        auto converted=std::from_chars(line.data()+pos+1,line.data()+line.size(),expected,16);
        auto body=line.substr(0,pos); RenderDiagnostic event;
        if (converted.ec!=std::errc{} || converted.ptr!=line.data()+line.size() || crc32(body)!=expected || body.size()<2 || body[1]!=' ' || (body[0]!='F' && body[0]!='R') || !unpack(body.substr(2),event)) { result.incomplete=true; break; }
        if (body[0]=='F') {
            if (result.first_failure || !result.records.empty()) { result.incomplete=true; break; }
            result.first_failure=std::move(event);
        } else result.records.push_back(std::move(event));
    }
    if (in.bad()) result.incomplete=true;
    return result;
}
}
