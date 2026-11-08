#include "settings_json.hpp"
#include <json.hpp>
#include <algorithm>
#include <codecvt>
#include <locale>
#include <set>
namespace settings_demo {
namespace {
using json = nlohmann::ordered_json;
std::string utf8(const std::u16string& s) { return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(s); }
std::u16string utf16(const std::string& s) { return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(s); }
bool string(const json& o, const char* key, std::string& out) {
    auto i=o.find(key); if (i==o.end() || !i->is_string()) return false; out=i->get<std::string>(); return true;
}
bool integer(const json& o, const char* key, int& out) {
    auto i=o.find(key); if (i==o.end() || !i->is_number_integer()) return false;
    if (*i < 0 || *i > 999999) return false; out=i->get<int>(); return true;
}
bool text(const json& o, const char* key, std::u16string& out, size_t max, bool required) {
    std::string s; if (!string(o,key,s)) return false; out=utf16(s);
    return lcars::valid_single_line(out,max) && (!required || std::any_of(out.begin(),out.end(),[](char16_t c){return c!=u' ' && c!=u'\t' && c!=u'\u3000';}));
}
}
std::optional<SettingsDocument> parse_settings(const std::string& bytes) {
    if (bytes.size()>1024*1024) return {};
    auto j=json::parse(bytes,nullptr,false); if (!j.is_object()) return {};
    try {
        (void)j.dump(); // Validate UTF-8 even in ignored fields.
        SettingsDocument d{}; std::string v; int n;
        if (!string(j,"format",v) || v!="lcars-settings-demo" || !integer(j,"version",n) || n!=1) return {};
        if (!j.contains("general") || !j["general"].is_object() || !j.contains("presentation") || !j["presentation"].is_object() || !j.contains("items") || !j["items"].is_array()) return {};
        auto& g=j["general"]; auto& p=j["presentation"];
        if (!text(g,"name",d.name,64,true) || !g.contains("notices") || !g["notices"].is_boolean()) return {};
        d.notices=g["notices"].get<bool>();
        if (!string(g,"mode",v) || (v!="normal" && v!="focus" && v!="offline")) return {}; d.mode=lcars::OptionValue{v};
        if (!string(g,"region",v) || (v!="north" && v!="central" && v!="south")) return {}; d.region=lcars::OptionValue{v};
        if (!integer(g,"volume",d.volume) || d.volume>100 || d.volume%5) return {};
        if (j["items"].size()>1000) return {}; std::set<std::string> ids; int max_id=0;
        for (auto& x:j["items"]) {
            Item item; if (!x.is_object() || !string(x,"id",v) || v.size()<4 || v.size()>7 || v[0]!='T' || !std::all_of(v.begin()+1,v.end(),[](char c){return c>='0'&&c<='9';}) || !ids.insert(v).second) return {};
            max_id=std::max(max_id,std::stoi(v.substr(1))); item.id={v};
            if (!text(x,"name",item.name,64,true) || !text(x,"note",item.note,200,false)) return {}; d.items.push_back(std::move(item));
        }
        if (!integer(j,"next_item_id",d.next_item_id) || d.next_item_id<=max_id) return {};
        if (!string(p,"font_family",v)) return {}; d.presentation.font_family=utf16(v);
        if (d.presentation.font_family.size()>128 || !integer(p,"text_size",d.presentation.text_size) || d.presentation.text_size<12 || d.presentation.text_size>28) return {};
        if (!string(p,"variant",v) || (v!="light" && v!="dark")) return {}; d.presentation.variant={v};
        if (!string(p,"density",v) || (v!="standard" && v!="compact")) return {}; d.presentation.density=v=="compact"?lcars::Density::compact:lcars::Density::standard;
        return d;
    } catch (const std::exception&) { return {}; }
}
std::string write_settings(const SettingsDocument& d) {
    json j; j["format"]="lcars-settings-demo"; j["version"]=1;
    j["general"]={{"name",utf8(d.name)},{"notices",d.notices},{"mode",d.mode->id},{"region",d.region->id},{"volume",d.volume}};
    j["items"]=json::array(); for (auto& x:d.items) j["items"].push_back({{"id",x.id.id},{"name",utf8(x.name)},{"note",utf8(x.note)}});
    j["next_item_id"]=d.next_item_id;
    j["presentation"]={{"font_family",utf8(d.presentation.font_family)},{"text_size",d.presentation.text_size},{"variant",d.presentation.variant.name},{"density",d.presentation.density==lcars::Density::compact?"compact":"standard"}};
    return j.dump(2)+"\n";
}
}
