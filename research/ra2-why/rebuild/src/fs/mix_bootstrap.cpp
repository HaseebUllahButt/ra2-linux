#include "mix_bootstrap.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace ra2::fs {
namespace {
std::string upper(std::string_view name) {
    std::string result(name);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c == '\\') c = '/';
    }
    return result;
}
std::string numbered(std::string_view prefix, int number) {
    std::ostringstream name;
    name << prefix << std::setfill('0') << std::setw(2) << number << ".MIX";
    return name.str();
}
bool matches(std::string_view name, std::string_view pattern) {
    const auto star = pattern.find('*');
    if (star == std::string_view::npos) return name == pattern;
    const auto suffix = pattern.substr(star + 1);
    return name.size() >= star + suffix.size() && name.substr(0, star) == pattern.substr(0, star) &&
           name.substr(name.size() - suffix.size()) == suffix;
}
} // namespace

MixFileSystem::MixFileSystem(std::filesystem::path directory)
    : directory_(std::filesystem::absolute(std::move(directory))) {}

// Resolve each component separately to reproduce Windows ASCII-insensitive
// names on Linux, including the original optional MIXFILES subdirectory.
std::optional<std::filesystem::path> MixFileSystem::loose_path(std::string_view name) const {
    std::filesystem::path relative(upper(name)), current = directory_;
    if (relative.is_absolute()) throw MixError("Expected a relative filename");
    for (const auto& component : relative) {
        if (component == "..") throw MixError("Parent components are not supported");
        if (component == ".") continue;
        std::vector<std::filesystem::path> candidates;
        std::error_code error;
        std::filesystem::directory_iterator it(current, error);
        if (error) throw MixError(current.string() + ": " + error.message());
        for (const auto& item : it)
            if (upper(item.path().filename().string()) == component.string()) candidates.push_back(item.path());
        if (candidates.empty()) return std::nullopt;
        if (candidates.size() > 1) throw MixError("Ambiguous case-insensitive filename: " + std::string(name));
        current = candidates.front();
    }
    return std::filesystem::is_regular_file(current) ? std::optional(current) : std::nullopt;
}

// 0x005B4430 walks MIXes from its first node. Loose files take precedence
// at CCFile's open layer (0x00473D10), before it calls this archive lookup.
std::optional<MixLocation> MixFileSystem::locate(std::string_view name, FilenameHash type) const {
    const auto id = filename_hash(name, type);
    for (const auto& archive : mixes_)
        if (const auto* entry = archive->find(id)) return MixLocation{archive, *entry};
    return std::nullopt;
}
std::shared_ptr<MixArchive> MixFileSystem::mount(std::string_view name, bool required) {
    const auto key = upper(name);
    if (const auto found = mounted_.find(key); found != mounted_.end()) return found->second;
    std::shared_ptr<MixArchive> archive;
    if (const auto path = loose_path(name)) archive = MixArchive::open(*path);
    else if (const auto location = locate(name)) archive = location->archive->open_nested(location->entry, std::string(name));
    if (!archive) {
        if (required) throw MixError("Required MIX archive is missing: " + std::string(name));
        return nullptr;
    }
    mixes_.push_back(archive); mounted_.emplace(key, archive);
    return archive;
}
std::shared_ptr<MixArchive> MixFileSystem::generic(std::string_view name, bool required) {
    auto archive = mount(name, required);
    if (archive) generics_[upper(name)] = archive;
    return archive;
}
void MixFileSystem::bootstrap() {
    for (int i = 99; i >= 0; --i)
        if (auto archive = mount(numbered("EXPANDMD", i), false))
            if (std::find(array_.begin(), array_.end(), archive) == array_.end()) array_.push_back(archive);
    for (const char* name : {"RA2MD.MIX", "RA2.MIX", "CACHEMD.MIX", "CACHE.MIX", "LOCALMD.MIX", "LOCAL.MIX"})
        generic(name);
    // 0x005B43E0 and 0x005B43F0 are unconditional-success stubs in this executable.
}

// Original Win32 enumeration order is unspecified. Sort for reproducible Linux
// precedence; callers can explicitly mount an overlay first when order matters.
std::vector<std::string> MixFileSystem::discover(std::string_view pattern) const {
    const std::filesystem::path target(upper(pattern));
    auto parent = directory_;
    if (target.has_parent_path()) {
        // Only MIXFILES is used by the recovered mounting routines.
        std::vector<std::filesystem::path> parents;
        for (const auto& entry : std::filesystem::directory_iterator(directory_))
            if (entry.is_directory() && upper(entry.path().filename().string()) == target.parent_path().string())
                parents.push_back(entry.path());
        if (parents.empty()) return {};
        if (parents.size() > 1) throw MixError("Ambiguous archive directory");
        parent = parents.front();
    }
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        const auto filename = entry.path().filename().string();
        if (entry.is_regular_file() && matches(upper(filename), target.filename().string()))
            result.push_back((target.parent_path() / filename).generic_string());
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return upper(a) < upper(b); });
    return result;
}
void MixFileSystem::mount_overlays() {
    for (const char* pattern : {"ECACHE*.MIX", "ELOCAL*.MIX"})
        for (const auto& name : discover(pattern)) {
            auto archive = mount(name);
            if (std::find(array_.begin(), array_.end(), archive) == array_.end()) array_.push_back(archive);
        }
}
void MixFileSystem::mount_resources(ResourceOptions options) {
    generic("CONQMD.MIX");
    for (const char* name : {"GENERMD.MIX", "GENERIC.MIX", "ISOGENMD.MIX", "ISOGEN.MIX"}) generic(name, false);
    for (const char* name : {"CONQUER.MIX", "CAMEOMD.MIX", "CAMEO.MIX"}) generic(name);
    maps_.clear(); movies_.clear();
    std::shared_ptr<MixArchive> primary_map;
    if (options.installed) {
        auto names = discover("MAPSMD*.MIX");
        if (names.empty()) names = discover("MAPS*.MIX");
        for (const auto& name : names) {
            auto archive = generic(name);
            if (!primary_map) primary_map = archive;
            else maps_.push_back(archive);
        }
    } else {
        primary_map = generic(numbered("MAPSMD", options.media_index + 1), false);
        auto base_map = generic(numbered("MAPS", options.media_index + 1), false);
        if (!primary_map) primary_map = base_map;
    }
    if (!primary_map) throw MixError("No maps MIX archive found");
    generic("MULTIMD.MIX");
    if (!generic("THEMEMD.MIX", false)) generic("THEME.MIX", false);
    if (options.installed) {
        std::string pattern = "MOVMD*.MIX";
        for (const auto& probe : {"MOVIES01.MIX", "MIXFILES/MOVIES01.MIX", "MOVMD01.MIX",
                                  "MIXFILES/MOVMD01.MIX", "MOVMD03.MIX", "MIXFILES/MOVMD03.MIX"})
            if (loose_path(probe)) {
                const std::string name(probe);
                pattern = name.substr(0, name.size() - 6) + "*.MIX";
            }
        auto names = discover(pattern);
        if (names.empty()) names = discover("MOVIES*.MIX");
        bool first = true;
        for (const auto& name : names) {
            auto archive = generic(name);
            if (!first) movies_.push_back(archive);
            first = false;
        }
        if (first) throw MixError("No movies MIX archive found");
    } else {
        if (!generic(numbered("MOVMD", options.media_index), false))
            generic(numbered("MOVIES", options.media_index));
    }
}
std::optional<std::vector<std::uint8_t>> MixFileSystem::read_file(std::string_view name) const {
    if (const auto path = loose_path(name)) {
        std::ifstream input(*path, std::ios::binary);
        if (!input) throw MixError("Cannot open " + path->string());
        std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), {});
        if (input.bad()) throw MixError("Cannot read " + path->string());
        return data;
    }
    if (const auto location = locate(name)) return location->archive->read(location->entry);
    return std::nullopt;
}
} // namespace ra2::fs
