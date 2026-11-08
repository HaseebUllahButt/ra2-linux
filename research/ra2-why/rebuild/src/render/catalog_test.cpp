#include "minimap.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: catalog_test <game-dir> <map-dir>");
        using Ini = ra2::rules::IniFile;
        std::map<std::string, std::vector<std::filesystem::path>> groups;
        for (const auto& file : std::filesystem::directory_iterator(argv[2])) {
            if (!file.is_regular_file()) continue;
            const auto ini = Ini::from_file(file.path().string());
            if (ini.find_section("IsoMapPack5"))
                groups[ini.read_string("Map", "Theater")].push_back(file.path());
        }
        std::size_t maps = 0, records = 0;
        for (const auto& [theater, paths] : groups) {
            // Isolate archive precedence. Never mount different theaters together.
            ra2::fs::MixFileSystem files(argv[1]);
            files.bootstrap(ra2::fs::GameVariant::YurisRevenge);
            auto art = ra2::render::TheaterArt::load(files, theater);
            if (art.theater_name() != theater || art.tile_sets().empty() ||
                art.settings().sections().empty() || art.translate_map_index(0xffff) != 0xffff ||
                art.translate_map_index(0xffffffff) != 0xffff)
                throw std::runtime_error("Original theater catalog state was lost");
            bool sentinel_rejected = false;
            try { art.resolved_metadata(0xffff, 0); }
            catch (const ra2::render::RenderError&) { sentinel_rejected = true; }
            if (!sentinel_rejected) throw std::runtime_error("Clear sentinel became a runtime TMP index");
            if (theater == "TEMPERATE") {
                // Original signed cell level -1 moves a flat tile down 15 pixels.
                ra2::pack::Terrain flat;
                flat.tiles.push_back({2,3,0xffff,0,0,0});
                const ra2::render::MapGeometry small{{0,0,4,4},{0,0,4,4}};
                const auto zero = ra2::render::render_minimap(flat, small, art);
                flat.tiles.front().height = 255;
                const auto below = ra2::render::render_minimap(flat, small, art);
                for (int y = 0; y < zero.height-15; ++y)
                    if (!std::equal(zero.rgba.begin()+std::size_t(y)*zero.width*4,
                                    zero.rgba.begin()+std::size_t(y+1)*zero.width*4,
                                    below.rgba.begin()+std::size_t(y+15)*zero.width*4))
                        throw std::runtime_error("Signed negative terrain height changed its original pixel offset");
            }
            std::set<std::pair<std::uint32_t, std::uint8_t>> references;
            for (const auto& path : paths) {
                const auto ini = Ini::from_file(path.string());
                const auto terrain = ra2::pack::load_terrain(ini);
                for (const auto& tile : terrain.tiles) references.emplace(tile.index, tile.subtile);
                ++maps;
                records += terrain.tiles.size();
            }
            for (const auto& [index, subtile] : references) {
                try {
                    const auto& tile = art.tile(index, subtile);
                    if (tile.diamond.bitmap.width != 60 || tile.diamond.bitmap.height != 30 ||
                        tile.diamond.bitmap.rgba.size() != 60 * 30 * 4)
                        throw std::runtime_error("Wrong original tile dimensions");
                    const auto translated = art.translate_map_index(index);
                    if (translated != 0xffff &&
                        &art.metadata(index, subtile) != &art.resolved_metadata(translated, subtile))
                        throw std::runtime_error("Resolved metadata translated the wire index twice");
                } catch (const std::exception& error) {
                    throw std::runtime_error(theater + " tile " + std::to_string(index) +
                        " subtile " + std::to_string(subtile) + ": " + error.what());
                }
            }
            std::cout << theater << ": " << paths.size() << " maps, " << references.size()
                      << " referenced subtiles, " << art.palette_name() << ".\n";
        }
        if (!maps) throw std::runtime_error("No original maps were checked");
        // No installed map uses Lunar, but its original clear terrain must load.
        ra2::fs::MixFileSystem lunar(argv[1]);
        lunar.bootstrap();
        auto lunar_art = ra2::render::TheaterArt::load(lunar, "LUNAR");
        lunar_art.tile(0xffff, 0);
        std::cout << "Terrain catalog checks passed: " << maps << " maps, " << records
                  << " tile records, and the Lunar clear tile.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
