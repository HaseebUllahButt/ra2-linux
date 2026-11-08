#include "test_inputs.h"
#include "minimap.h"
#include <stdexcept>
std::vector<OriginalTestCell> original_test_cells(const std::string& game,const std::string& path,bool base) {
    ra2::fs::MixFileSystem files(game);
    files.bootstrap(base?ra2::fs::GameVariant::RedAlert2:ra2::fs::GameVariant::YurisRevenge); files.mount_overlays();
    const auto map=ra2::rules::IniFile::from_file(path);
    auto art=ra2::render::TheaterArt::load(files,map.read_string("Map","Theater"));
    const auto terrain=ra2::pack::load_terrain(map);
    std::vector<OriginalTestCell> cells;
    for (const auto& tile:terrain.tiles) {
        const auto& record=art.metadata(tile.index,tile.subtile);
        const auto overlay=terrain.overlay.empty()?255:terrain.overlay.at(tile.y*512+tile.x);
        cells.push_back({tile.x,tile.y,tile.index,tile.subtile,record.terrain,record.ramp,tile.height,overlay==255?-1:overlay});
    }
    return cells;
}
std::string original_test_art(const std::string& game,bool base) {
    ra2::fs::MixFileSystem files(game);
    files.bootstrap(base?ra2::fs::GameVariant::RedAlert2:ra2::fs::GameVariant::YurisRevenge); files.mount_overlays();
    const auto bytes=files.read_file(base?"art.ini":"artmd.ini");
    if (!bytes) throw std::runtime_error("original art INI is missing");
    return {bytes->begin(),bytes->end()};
}
