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
std::optional<unsigned> overlay_bridge_direction(std::int32_t type_index,
    std::uint8_t cell_ramp, std::int32_t placement_mode, bool placement_allowed) {
    // 005DA970 calls 004732D0 for 24/25 and 00473700 for 237/238.
    if (!placement_allowed || (placement_mode!=1 && placement_mode!=3) || cell_ramp>4) return {};
    if (type_index==24 || type_index==237) return 0;
    if (type_index==25 || type_index==238) return 6;
    return {};
}
}
