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
