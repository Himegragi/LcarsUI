#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Throwaway, flat-form API experiment. No platform/render/IME dependency.
namespace probe {
struct Value { std::wstring text; unsigned revision = 0; };
struct Field { std::string key; std::wstring label; Value value; };
inline Field TextField(std::string key, std::wstring label, Value value) {
    return {std::move(key),std::move(label),std::move(value)};
}
using Form = std::vector<Field>;
struct Node {
    Field declared;
    std::uint64_t instance;
    std::wstring draft;
    bool dirty = false, conflict = false;
    unsigned selection_begin = 0, selection_end = 0;
};
struct Change { std::string key; std::uint64_t instance; std::wstring text; };
class Runtime {
    std::uint64_t next_instance = 1;
    std::deque<Change> events;
public:
    std::map<std::string, Node> nodes;
    std::vector<std::string> order;
    std::string focus;
    void apply(const Form& form) {
        // Validate identity before changing anything; duplicate keys are rejected atomically.
        std::set<std::string> keys;
        for (const auto& f:form)
            if(f.key.empty() || !keys.insert(f.key).second) throw std::invalid_argument("empty/duplicate key");
        std::erase_if(nodes,[&](const auto& entry){return !keys.contains(entry.first);});
        if(!keys.contains(focus)) focus.clear();
        order.clear();
        for(const auto& f:form) {
            order.push_back(f.key);
            auto it=nodes.find(f.key);
            if(it==nodes.end()) { nodes.emplace(f.key,Node{f,next_instance++,f.value.text}); continue; }
            auto& n=it->second;
            const bool changed=n.declared.value.revision!=f.value.revision || n.declared.value.text!=f.value.text;
            if(changed) {
                if(n.dirty && n.draft!=f.value.text) n.conflict=true;
                else { n.draft=f.value.text; n.dirty=false; n.conflict=false; }
            }
            n.declared=f;
            n.selection_begin=std::min(n.selection_begin,static_cast<unsigned>(n.draft.size()));
            n.selection_end=std::min(n.selection_end,static_cast<unsigned>(n.draft.size()));
        }
    }
    void edit(const std::string& key,std::wstring text) {
        auto& n=nodes.at(key);
        if(n.draft==text) return;
        n.draft=std::move(text); n.dirty=n.draft!=n.declared.value.text;
        if(!n.dirty) n.conflict=false;
        events.push_back({key,n.instance,n.draft});
    }
    void revert(const std::string& key) {
        auto& n=nodes.at(key); n.draft=n.declared.value.text; n.dirty=false; n.conflict=false;
        n.selection_begin=n.selection_end=0;
    }
    std::optional<Change> next_event() {
        while(!events.empty()) {
            auto event=std::move(events.front()); events.pop_front();
            auto it=nodes.find(event.key);
            if(it!=nodes.end() && it->second.instance==event.instance) return event;
        }
        return {};
    }
};
int language_probe();
}
