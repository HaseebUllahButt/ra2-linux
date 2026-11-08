#include "map_file.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

using namespace ra2map;

int main(int argc, char** argv) {
    MapFile coordinate_check;
    coordinate_check.parse("[Waypoints]\n0=59092\n[Terrain]\n59092=TREE01\n");
    if (coordinate_check.waypoints().size() != 1 ||
        coordinate_check.waypoints()[0].cell.x != 92 ||
        coordinate_check.waypoints()[0].cell.y != 59 ||
        coordinate_check.terrain().size() != 1 ||
        coordinate_check.terrain()[0].cell.x != 92 ||
        coordinate_check.terrain()[0].cell.y != 59) {
        std::fprintf(stderr, "Original packed cell coordinate check failed\n");
        return 1;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: map_test <file.map>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    MapFile map;
    try {
        map.parse(text);
    } catch (const std::exception& e) {
        std::printf("parse failed: %s\n", e.what());
        return 1;
    }
    const auto& st = map.stats();
    std::printf("sections: %zu, entries: %zu, empty: %zu, ignored: %zu\n",
                st.section_headers, st.entries, st.empty_entries,
                st.ignored_lines);
    std::printf("theater: %s, size: %d,%d,%d,%d\n", map.theater().c_str(),
                map.size_left(), map.size_top(), map.size_right(),
                map.size_bottom());
    std::printf("structures: %zu, units: %zu, infantry: %zu, aircraft: %zu\n",
                map.structures().size(), map.units().size(),
                map.infantry().size(), map.aircraft().size());
    std::printf("terrain: %zu, waypoints: %zu\n", map.terrain().size(),
                map.waypoints().size());
    if (!map.waypoints().empty()) {
        const auto& wp = map.waypoints()[0];
        std::printf("waypoint[0]: idx=%d cell=%u,%u\n", wp.index,
                    wp.cell.x, wp.cell.y);
    }
    std::printf("iso pack: %zu bytes, overlay pack: %zu bytes\n",
                map.iso_map_pack5().size(), map.overlay_pack().size());
    const auto& packed = map.packed_terrain();
    std::printf("decoded tiles: %zu, overlay cells: %zu, overlay data cells: %zu\n",
                packed.tiles.size(), packed.overlay.size(), packed.overlay_data.size());
    if (map.iso_map_pack5().size() && packed.tiles.empty()) return 1;
    return 0;
}
