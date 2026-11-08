#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace ra2::sim {
struct Coord {
    std::int32_t x=0,y=0,z=0;
};
struct GroundGuards {
    // Resolved virtual results on the linked Foot object, in original call order.
    bool slot_37c=false, slot_380=false, slot_1d4=false, slot_1d8=false;
};
struct GroundSample { std::int32_t x,y; std::uint8_t facing; };
// A typed slice of the drive locomotor. Offsets refer to the complete object.
// The interface passed to 004AFD40 is at object+4, so its fields appear four bytes earlier.
struct GroundMotion {
    std::optional<Coord> destination; // +34..3C
    std::optional<Coord> anchor;      // +40..48
    int track=-1;                    // +58; row of table at 007E7B28
    int sample=0;                    // +5C
    int remainder=0;                 // +4C
    bool second_track=false;         // +60
    bool segment_active=false;       // +63
    std::array<std::int32_t,24> path; // linked Foot +5E0..63C
    GroundMotion();
    // 004AFD40. Inputs include resolved guards and the resolved cell bridge adjustment.
    // nullopt represents the original runtime empty-coordinate sentinel.
    bool move_to(std::optional<Coord> point,const GroundGuards&,int bridge_adjustment=0);
    // 004B4780. Transform one original curve-relative coordinate and facing.
    GroundSample transform(Coord relative,std::uint8_t facing) const;
    // 004B2630, 23-entry path consumption before LAB_004B460C.
    void consume_one_path_direction();
};
// 00743270: map unit coordinates use a 256-unit cell and a 128-unit center offset.
Coord ground_cell_center(std::int16_t x,std::int16_t y);
} // namespace ra2::sim
