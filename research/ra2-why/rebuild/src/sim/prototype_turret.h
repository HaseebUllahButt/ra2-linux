#pragma once
// Prototype turret turning and infantry fire steps for the first playable
// skirmish. Simplified from TechnoClass::Can_Fire, TechnoClass::Fire_At and
// the UnitClass turret AI in ref/CnC_Red_Alert/CODE/{TECHNO,TURRET,UNIT}.CPP.
// The combat prototype calls aim and ready_to_fire once per shooter per frame.
#include "../session/world/world.h"
#include <cstdint>

namespace ra2::sim {
// Facings use the original 0..255 clockwise scale, 0 = north.
constexpr std::int32_t facings = 256;

// Shortest signed facing distance between two facings, -128..128 steps.
std::int32_t facing_difference(std::int32_t from, std::int32_t to);

// One frame of turret rotation toward the desired facing at the ROT rate.
// The step never overshoots the desired facing. A zero or negative ROT rate
// holds the current facing.
std::int32_t aim(std::int32_t current, std::int32_t desired, std::int32_t rot);

// Facing in 0..255 steps from one world position to another. Equal positions
// keep the current facing. TODO-ORIGINAL 0048A1E0 Direction(): this atan2
// version rounds differently by at most one step.
std::int32_t direction_to(const Coord& from, const Coord& to);

// Planar lepton distance between two world positions. TODO-ORIGINAL: the
// original ::Distance also weighs the Z height component.
std::int64_t lepton_distance(const Coord& from, const Coord& to);

// One weapon fire gate check per frame. The caller resolves the authored
// weapon RateOfFire and Range into frames and leptons first.
struct FireControl {
    std::uint64_t frame=0;      // Current game frame.
    std::uint64_t last_shot=0;  // Frame of the last shot; 0 before the first.
    std::int32_t rof=0;         // Weapon RateOfFire in frames.
    std::int32_t range=0;       // Weapon Range in leptons (cells * 256).
    std::int32_t distance=0;    // Shooter-to-target distance in leptons.
};

// Rearm and range gate. True when the weapon has rearmed and the target is
// within range. This is not the original eligibility chain: it skips cloak,
// falling, ammo and anti-air checks that TechnoClass::Can_Fire performs.
bool ready_to_fire(const FireControl& control);
}
