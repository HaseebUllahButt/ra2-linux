#include "test_inputs.h"
#include "minimap.h"
#include <stdexcept>
#include <memory>
#include <sstream>
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
        cells.push_back({tile.x,tile.y,tile.index,art.translate_map_index(tile.index),tile.subtile,record.terrain,record.ramp,tile.height,overlay==255?-1:overlay});
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

OriginalTestCatalog original_test_catalog(const std::string& game,const std::string& path,bool base) {
    auto files=std::make_shared<ra2::fs::MixFileSystem>(game);
    files->bootstrap(base?ra2::fs::GameVariant::RedAlert2:ra2::fs::GameVariant::YurisRevenge); files->mount_overlays();
    const auto map=ra2::rules::IniFile::from_file(path);
    auto art=std::make_shared<ra2::render::TheaterArt>(ra2::render::TheaterArt::load(*files,map.read_string("Map","Theater")));
    OriginalTestCatalog result; result.name=art->theater_name();
    std::ostringstream text;
    for (const auto& section:art->settings().sections()) {
        text<<'['<<section.name<<"]\n";
        for (const auto& entry:section.entries) text<<entry.key<<'='<<entry.value<<'\n';
    }
    result.settings=text.str();
    for (const auto& range:art->tile_sets()) result.ranges.push_back({range.first,range.count});
    result.metadata=[files,art](std::uint32_t index,std::uint8_t subtile)->std::optional<OriginalTestMetadata> {
        (void)files; // Keep the original archive owner alive with the metadata callback.
        try {
            const auto count=art->resolved_subtile_count(index);
            if (!count) return {};
            const bool valid=art->resolved_subtile_present(index,subtile);
            const auto normalized=static_cast<std::uint8_t>(subtile%count);
            if (!art->resolved_subtile_present(index,normalized)) return OriginalTestMetadata{0,0,false};
            const auto& record=art->resolved_metadata(index,normalized);
            return OriginalTestMetadata{record.terrain,record.ramp,valid};
        } catch(const ra2::render::RenderError&) { return {}; }
    };
    return result;
}
