#include "settings_json.hpp"
#include <json.hpp>
#include <algorithm>
#include "utf.hpp"
#include <set>
namespace settingsDemo {
namespace {
using json = nlohmann::ordered_json;
using isoduo::detail::utf8FromUtf16;
using isoduo::detail::utf16FromUtf8;
bool string(const json& o, const char* key, std::string& out) {
    auto i=o.find(key); if (i==o.end() || !i->is_string()) return false; out=i->get<std::string>(); return true;
}
bool integer(const json& o, const char* key, int& out) {
    auto i=o.find(key); if (i==o.end() || !i->is_number_integer()) return false;
    if (*i < 0 || *i > 999999) return false; out=i->get<int>(); return true;
}
// A multi-line value keeps its line breaks as LF (JSON writes them as \n); CR and other controls stay invalid.
bool text(const json& o, const char* key, std::u16string& out, size_t max, bool required, bool multiline = false) {
    std::string s; if (!string(o,key,s)) return false; out=utf16FromUtf8(s);
    return (multiline ? isoduo::validMultiLine(out,max) : isoduo::validSingleLine(out,max)) && (!required || std::any_of(out.begin(),out.end(),[](char16_t c){return c!=u' ' && c!=u'\t' && c!=u'\u3000';}));
}
}
// An item's note: a plain string (files written before rich text existed: no styles) or {"text": ..., "spans": [...]},
// each span {"start", "length", and any of "bold", "italic", "underline" (true) and "color" (a name)}. The spans
// are normalized (isoduo::normalizeRichText) on the way in, so a file reads back as the canonical value.
static bool noteValue(const json& item, Item& out) {
    auto it=item.find("note"); if (it==item.end()) return false;
    if (it->is_string()) return text(item,"note",out.note,200,false,true);
    if (!it->is_object()) return false;
    isoduo::RichText value;
    if (!text(*it,"text",value.text,200,false,true)) return false;
    auto list=it->find("spans"); if (list==it->end() || !list->is_array() || list->size()>1000) return false;
    for (auto& entry:*list) {
        isoduo::StyleSpan span; int start=0, length=0; std::string color;
        if (!entry.is_object() || !integer(entry,"start",start) || !integer(entry,"length",length)) return false;
        span.start=static_cast<unsigned>(start); span.length=static_cast<unsigned>(length);
        for (auto& [key,member]:entry.items()) {
            if (key=="start" || key=="length") continue;
            if (key=="bold" || key=="italic" || key=="underline") {
                if (!member.is_boolean()) return false;
                (key=="bold" ? span.bold : key=="italic" ? span.italic : span.underline)=member.get<bool>();
            } else if (key=="color") {
                if (!member.is_string() || member.get<std::string>().size()>64) return false;
                span.color=member.get<std::string>();
            } else return false;
        }
        value.spans.push_back(std::move(span));
    }
    auto normal=isoduo::normalizeRichText(std::move(value),200);
    if (!normal) return false;
    out.note=std::move(normal->text); out.noteSpans=std::move(normal->spans);
    return true;
}
std::optional<SettingsDocument> parseSettings(const std::string& bytes) {
    if (bytes.size()>1024*1024) return {};
    auto j=json::parse(bytes,nullptr,false); if (!j.is_object()) return {};
    try {
        (void)j.dump(); // Validate UTF-8 even in ignored fields.
        SettingsDocument d{}; std::string v; int n;
        if (!string(j,"format",v) || (v!="isoduo-settings-demo" && v!="lcars-settings-demo") || !integer(j,"version",n) || n!=1) return {};
        if (!j.contains("general") || !j["general"].is_object() || !j.contains("presentation") || !j["presentation"].is_object() || !j.contains("items") || !j["items"].is_array()) return {};
        auto& g=j["general"]; auto& p=j["presentation"];
        if (!text(g,"name",d.name,64,true) || !g.contains("notices") || !g["notices"].is_boolean()) return {};
        d.notices=g["notices"].get<bool>();
        if (!string(g,"mode",v) || (v!="normal" && v!="focus" && v!="offline")) return {}; d.mode=isoduo::OptionValue{v};
        if (!string(g,"region",v) || (v!="north" && v!="central" && v!="south")) return {}; d.region=isoduo::OptionValue{v};
        if (!integer(g,"volume",d.volume) || d.volume>100 || d.volume%5) return {};
        if (j["items"].size()>1000) return {}; std::set<std::string> ids; int maxId=0;
        for (auto& x:j["items"]) {
            Item item; if (!x.is_object() || !string(x,"id",v) || v.size()<4 || v.size()>7 || v[0]!='T' || !std::all_of(v.begin()+1,v.end(),[](char c){return c>='0'&&c<='9';}) || !ids.insert(v).second) return {};
            maxId=std::max(maxId,std::stoi(v.substr(1))); item.id={v};
            if (!text(x,"name",item.name,64,true) || !noteValue(x,item)) return {}; d.items.push_back(std::move(item));
        }
        if (!integer(j,"next_item_id",d.nextItemId) || d.nextItemId<=maxId) return {};
        if (!string(p,"font_family",v)) return {}; d.presentation.fontFamily=utf16FromUtf8(v);
        if (d.presentation.fontFamily.size()>128 || !integer(p,"text_size",d.presentation.textSize) || d.presentation.textSize<12 || d.presentation.textSize>28) return {};
        if (!string(p,"variant",v) || (v!="light" && v!="dark")) return {}; d.presentation.variant={v};
        if (!string(p,"density",v) || (v!="standard" && v!="compact")) return {}; d.presentation.density=v=="compact"?isoduo::Density::compact:isoduo::Density::standard;
        // Files written before the accent choice existed have no "accent": they mean the default.
        if (p.contains("accent")) { if (!string(p,"accent",v) || (v!="default" && v!="green" && v!="orange")) return {}; d.accent=v; }
        // The focus ring (#90): "focusIndicator" ("automatic", "always", "never"), "focusRingWidth" (2-6 or null = the appearance's own),
        // "focusRingRadius" (0-32 or null = the component's own) and "focusColor" ("default", "green", "orange"). Older files have
        // none of them and mean the defaults; a value out of range or not recognized makes the whole file corrupt.
        if (p.contains("focus_indicator")) {
            const auto mode = string(p,"focus_indicator",v) ? focusModeFromId(v) : std::nullopt;
            if (!mode) return {};
            d.presentation.focusIndicator=*mode;
        }
        if (p.contains("focus_ring_width") && !p["focus_ring_width"].is_null()) {
            if (!integer(p,"focus_ring_width",n) || n<isoduo::minFocusRingWidth || n>isoduo::maxFocusRingWidth) return {};
            d.presentation.focusRingWidth=n;
        }
        if (p.contains("focus_ring_radius") && !p["focus_ring_radius"].is_null()) {
            if (!integer(p,"focus_ring_radius",n) || n<isoduo::minFocusRingRadius || n>isoduo::maxFocusRingRadius) return {};
            d.presentation.focusRingRadius=n;
        }
        if (p.contains("focus_color")) { if (!string(p,"focus_color",v) || (v!="default" && v!="green" && v!="orange")) return {}; d.focusColor=v; }
        // Likewise "justifyAsLeft" (justified text drawn left-aligned): older files mean off.
        if (p.contains("justify_as_left")) { if (!p["justify_as_left"].is_boolean()) return {}; d.presentation.justifyAsLeft=p["justify_as_left"].get<bool>(); }
        return d;
    } catch (const std::exception&) { return {}; }
}
static json noteJson(const Item& item) {
    json spans=json::array();
    for (const auto& span:item.noteSpans) {
        json entry={{"start",span.start},{"length",span.length}};
        if (span.bold) entry["bold"]=true;
        if (span.italic) entry["italic"]=true;
        if (span.underline) entry["underline"]=true;
        if (!span.color.empty()) entry["color"]=span.color;
        spans.push_back(std::move(entry));
    }
    return {{"text",utf8FromUtf16(item.note)},{"spans",std::move(spans)}};
}
std::string writeSettings(const SettingsDocument& d) {
    json j; j["format"]="isoduo-settings-demo"; j["version"]=1;
    j["general"]={{"name",utf8FromUtf16(d.name)},{"notices",d.notices},{"mode",d.mode->id},{"region",d.region->id},{"volume",d.volume}};
    j["items"]=json::array(); for (auto& x:d.items) j["items"].push_back({{"id",x.id.id},{"name",utf8FromUtf16(x.name)},{"note",noteJson(x)}});
    j["next_item_id"]=d.nextItemId;
    j["presentation"]={{"font_family",utf8FromUtf16(d.presentation.fontFamily)},{"text_size",d.presentation.textSize},{"variant",d.presentation.variant.name},{"density",d.presentation.density==isoduo::Density::compact?"compact":"standard"},{"accent",d.accent},{"justify_as_left",d.presentation.justifyAsLeft}};
    const auto& focus=d.presentation;
    j["presentation"]["focus_indicator"]=focusModeId(focus.focusIndicator);
    j["presentation"]["focus_ring_width"]=focus.focusRingWidth?json(*focus.focusRingWidth):json(nullptr);
    j["presentation"]["focus_ring_radius"]=focus.focusRingRadius?json(*focus.focusRingRadius):json(nullptr);
    j["presentation"]["focus_color"]=d.focusColor;
    return j.dump(2)+"\n";
}
}

