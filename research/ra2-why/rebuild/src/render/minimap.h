#pragma once

#include "tmp_decode.h"
#include "../fs/mix_bootstrap.h"
#include "../pack/map_pack.h"
#include <map>

namespace ra2::render {
struct Rectangle { int x = 0, y = 0, width = 0, height = 0; };
struct MapGeometry {
    Rectangle size, local;
    static MapGeometry from_ini(const rules::IniFile& ini);
};
struct TileSetRange { std::uint32_t first, count; };

// Mount onto a dedicated, bootstrapped filesystem for each theater, so files
// from a previously loaded theater cannot win resource lookups.
class TheaterArt {
public:
    static TheaterArt load(fs::MixFileSystem& files, std::string_view theater);
    const DecodedTile& tile(std::uint32_t index, std::uint8_t subtile);
    const TmpRecord& metadata(std::uint32_t index, std::uint8_t subtile);
    const TmpRecord& resolved_metadata(std::uint32_t index, std::uint8_t subtile);
    std::size_t resolved_subtile_count(std::uint32_t index);
    bool resolved_subtile_present(std::uint32_t index, std::uint8_t subtile);
    // Wire-to-runtime translation. Preserve the original clear sentinel.
    std::uint32_t translate_map_index(std::uint32_t index) const;
    const std::vector<TileSetRange>& tile_sets() const noexcept { return tile_sets_; }
    const rules::IniFile& settings() const noexcept { return settings_; }
    const std::string& theater_name() const noexcept { return theater_name_; }
    // TileType GetLandType: original 00544BE0 and base 00529300.
    static std::int32_t terrain_land_type(std::uint8_t raw_terrain);
    int tile_width() const noexcept { return 60; }
    int tile_height() const noexcept { return 30; }
    std::size_t loaded_containers() const noexcept { return containers_.size(); }
    std::size_t decoded_subtiles() const noexcept { return decoded_.size(); }
    const std::string& palette_name() const noexcept { return palette_name_; }
    const std::string& palette_archive() const noexcept { return palette_archive_; }
private:
    std::pair<std::uint32_t, std::uint8_t> resolve(std::uint32_t index,
                                                std::uint8_t subtile) const;
    TmpImage& container(std::uint32_t resolved_index);
    fs::MixFileSystem* files_ = nullptr; // Must outlive this object.
    gfx::Palette palette_;
    std::string palette_name_, palette_archive_;
    std::string theater_name_;
    rules::IniFile settings_;
    std::vector<TileSetRange> tile_sets_;
    std::vector<std::string> filenames_;
    std::vector<bool> urban_fallback_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> shifts_;
    std::uint32_t clear_ = 0;
    std::map<std::uint32_t, TmpImage> containers_;
    std::map<std::pair<std::uint32_t, std::uint8_t>, DecodedTile> decoded_;
};

// Native-resolution terrain artwork, clipped to LocalSize. Includes elevation
// and TMP extras/depth planes. Does not draw objects, overlays or map lighting.
Bitmap render_minimap(const pack::Terrain& terrain, const MapGeometry& geometry,
                      TheaterArt& art);
void write_ppm(const Bitmap& image, const std::filesystem::path& path);
} // namespace ra2::render
