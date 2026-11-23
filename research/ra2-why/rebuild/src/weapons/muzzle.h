#pragma once
#include <sim/ground.h>
#include <array>
#include <cstdint>
#include <optional>
namespace ra2::weapons {
struct FlatUnitMuzzleInput {
    sim::Coord position; // Unit virtual +A8, from stored position.
    std::uint16_t hull_facing, turret_facing;
    std::array<int,3> fire_flh; // Selected weapon slot +4/+8/+C.
    int turret_offset, burst_index; // UnitType +63C, Unit +310.
    // Prove the locomotor uses 0053D030 and has no slope transform.
    std::optional<bool> flat_drive_locomotor;
};
// Base 006C1830 for a flat drive Unit with resolved hull and turret facings.
// Missing flags or exceptional arithmetic return empty.
std::optional<sim::Coord> base_flat_unit_muzzle(const FlatUnitMuzzleInput&);
}
