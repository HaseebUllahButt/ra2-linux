#pragma once

#include "mix_archive.h"
#include <map>
#include <optional>

namespace ra2::fs {
enum class GameVariant { RedAlert2, YurisRevenge };
struct MixLocation {
    std::shared_ptr<const MixArchive> archive;
    MixEntry entry;
};
struct ResourceOptions {
    bool installed = true; // Original DAT_0089E3A0 == 1: wildcard discovery.
    int media_index = 0;   // Otherwise MAPS[MD](index+1), MOV[MD](index).
};

class MixFileSystem {
public:
    explicit MixFileSystem(std::filesystem::path directory);
    std::shared_ptr<MixArchive> mount(std::string_view name, bool required = true);
    // Base: game.exe 0x00515C90; expansion: gamemd.exe 0x005301A0.
    void bootstrap(GameVariant variant = GameVariant::YurisRevenge);
    GameVariant variant() const noexcept { return variant_.value_or(GameVariant::YurisRevenge); }
    void mount_overlays();  // 0x00530000; called separately after bootstrap.
    void mount_resources(ResourceOptions options = {}); // 0x00530460
    std::optional<MixLocation> locate(std::string_view filename,
                                      FilenameHash type = FilenameHash::Crc32) const;
    std::optional<std::vector<std::uint8_t>> read_file(std::string_view filename) const;
    // 0x00ABEFD8: search order, first mounted archive wins.
    const std::vector<std::shared_ptr<MixArchive>>& mixes() const noexcept { return mixes_; }
    // 0x00884D90, 0x00884DA8, 0x00884DE0.
    const std::vector<std::shared_ptr<MixArchive>>& array() const noexcept { return array_; }
    const std::vector<std::shared_ptr<MixArchive>>& maps() const noexcept { return maps_; }
    const std::vector<std::shared_ptr<MixArchive>>& movies() const noexcept { return movies_; }
    // 0x00884DF8: named ownership slots, keyed by uppercase archive filename.
    const std::map<std::string, std::shared_ptr<MixArchive>>& generics() const noexcept { return generics_; }
    const std::filesystem::path& directory() const noexcept { return directory_; }
private:
    std::optional<std::filesystem::path> loose_path(std::string_view name) const;
    std::vector<std::string> discover(std::string_view pattern) const;
    std::shared_ptr<MixArchive> generic(std::string_view name, bool required = true);
    std::filesystem::path directory_;
    std::optional<GameVariant> variant_;
    std::vector<std::shared_ptr<MixArchive>> mixes_, array_, maps_, movies_;
    std::map<std::string, std::shared_ptr<MixArchive>> mounted_, generics_;
};
} // namespace ra2::fs
