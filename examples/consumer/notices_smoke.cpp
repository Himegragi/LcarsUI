#include <lcars/third_party_notices.hpp>
#include <lcars/desktop.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
    auto* volatile desktop_entry = &lcars::run_desktop;
    if (!desktop_entry) return 5;
    const auto notices = lcars::third_party_notices();
    if (notices.size() != 4) return 1;
    std::ifstream input(LCARS_THIRD_PARTY_NOTICES_FILE, std::ios::binary);
    const std::string file((std::istreambuf_iterator<char>(input)), {});
    const std::u16string_view names[] = {u"FreeType", u"HarfBuzz", u"Khronos OpenGL headers", u"Unicode Character Database (Vertical_Orientation)"};
    const std::string separator = "========================================================================\n";
    auto pos = file.find(separator);
    for (std::size_t i = 0; i < notices.size(); ++i) {
        if (pos == std::string::npos || notices[i].name != names[i]) return 2;
        auto start = pos + separator.size();
        auto end = file.find(separator, start);
        auto section = file.substr(start, end == std::string::npos ? end : end - start);
        // UTF-8 -> UTF-16, including non-ASCII license text.
        std::u16string decoded;
        for (std::size_t j = 0; j < section.size();) {
            unsigned char c = section[j++];
            unsigned code = c;
            int extra = 0;
            if (c >= 0xF0) { code = c & 7; extra = 3; }
            else if (c >= 0xE0) { code = c & 15; extra = 2; }
            else if (c >= 0xC0) { code = c & 31; extra = 1; }
            while (extra--) { if (j == section.size()) return 3; code = (code << 6) | (static_cast<unsigned char>(section[j++]) & 63); }
            if (code > 0xFFFF) { code -= 0x10000; decoded.push_back(static_cast<char16_t>(0xD800 + (code >> 10))); code = 0xDC00 + (code & 1023); }
            decoded.push_back(static_cast<char16_t>(code));
        }
        if (notices[i].text != decoded || (i == 0 && decoded.find(u"Credit:") == decoded.npos)) return 4;
        std::cout << std::string(names[i].begin(), names[i].end()) << "\n";
        pos = end;
    }
    return 0;
}
