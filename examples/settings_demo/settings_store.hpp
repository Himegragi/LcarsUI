#pragma once
#include "settings_json.hpp"
#include <filesystem>
namespace settings_demo {
enum class LoadKind { missing, loaded, corrupt, unreadable };
struct LoadResult { LoadKind kind; std::optional<SettingsDocument> document; std::u16string reason; bool renamed=false; };
class SettingsStore {
    std::filesystem::path directory_;
    std::function<unsigned(std::string_view)> fault_;
    unsigned fault(std::string_view step) const { return fault_ ? fault_(step) : 0; }
public:
    explicit SettingsStore(std::filesystem::path directory, std::function<unsigned(std::string_view)> fault = {}): directory_(std::move(directory)), fault_(std::move(fault)) {}
    std::filesystem::path path() const { return directory_/"settings.json"; }
    LoadResult load() const;
    std::u16string save(const SettingsDocument&) const;
};
}
