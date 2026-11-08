#include "ground.h"
#include <stdexcept>
#include <limits>
namespace ra2::sim {
namespace {
// Third dword of each 12-byte row at 007E7B28, read from original gamemd.exe.
constexpr std::uint32_t flags[64]={
    0,8,8,0,0,0,10,10,15,0,8,8,0,0,0,15,
    15,15,3,11,11,0,0,0,0,12,12,4,11,11,0,0,
    0,0,12,12,4,14,14,0,0,0,0,9,9,1,14,14,
    13,0,0,0,9,9,1,13,13,13,0,0,0,10,10,2};
std::int32_t checked(std::int64_t n) {
    if (n<std::numeric_limits<std::int32_t>::min() || n>std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("ground coordinate overflow is unsupported");
    return static_cast<std::int32_t>(n);
}
}
GroundMotion::GroundMotion() { path.fill(-1); }
bool GroundMotion::move_to(std::optional<Coord> point,const GroundGuards& guards,int bridge_adjustment) {
    if (guards.slot_37c || guards.slot_380 || guards.slot_1d4 || guards.slot_1d8) return false;
    if (point) point->z=checked(static_cast<std::int64_t>(point->z)+bridge_adjustment);
    destination=point; return true;
}
GroundSample GroundMotion::transform(Coord relative,std::uint8_t facing) const {
    if (!anchor) throw std::logic_error("ground transform requires a resolved segment anchor");
    if (track<0 || track>=64) throw std::out_of_range("ground track is outside original table");
    auto x=static_cast<std::int64_t>(relative.x), y=static_cast<std::int64_t>(relative.y);
    std::uint32_t angle=facing;
    const auto bits=flags[track];
    if (bits&1) { auto old=x; x=y; y=old; angle=(-angle-64)&255; }
    if (bits&2) { x=-x; angle=(-angle)&255; }
    if (bits&4) { y=-y; angle=(-angle-128)&255; }
    // 004B4780 writes x/y only. No height is inferred.
    return {checked(anchor->x+x),checked(anchor->y+y),static_cast<std::uint8_t>(angle)};
}
void GroundMotion::consume_one_path_direction() {
    // Original copies 23 dwords forward. The last entry is unchanged.
    for (unsigned i=0;i<23;++i) path[i]=path[i+1];
}
Coord ground_cell_center(std::int16_t x,std::int16_t y) {
    return {static_cast<std::int32_t>(x)*256+128,static_cast<std::int32_t>(y)*256+128,0};
}
}
