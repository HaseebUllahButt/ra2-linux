#include "bridge_footprint.h"
namespace ra2::session::world {
std::optional<std::array<BridgeCell,4>> bridge_flag_cells(BridgeCell root, unsigned direction) {
    // Base 004732D0 and 00473700 use the eight offsets at 00851A70.
    // The root, two forward cells, and one backward cell receive bit 100.
    static constexpr int dx[]={0,1,1,1,0,-1,-1,-1};
    static constexpr int dy[]={-1,-1,0,1,1,1,0,-1};
    if (direction>=8 || root.x<2 || root.y<2 || root.x>509 || root.y>509) return {};
    const auto cell=[&](int distance) {
        return BridgeCell{static_cast<std::int16_t>(root.x+dx[direction]*distance),
                          static_cast<std::int16_t>(root.y+dy[direction]*distance)};
    };
    return std::array<BridgeCell,4>{cell(0),cell(1),cell(2),cell(-1)};
}
}
