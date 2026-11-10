#pragma once
// Prototype power and build tech for the first playable native Linux
// skirmish. It reads base rules.ini and one session world snapshot and never
// changes it. All checks are name based (INI strings), not heap indexes.
// Sidebar order follows the rules type registries.
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ra2::sim::tech {

// Per-house power. Output and drain come from live buildings: the signed
// rules Power= value adds to output when positive and to drain when negative.
// Output is health scaled and truncated: game.exe 0x00449D40 returns
// F2I(Health/Strength * Power), drain 0x00449DD0 returns Type Power= unscaled.
// The recount matches HouseClass::UpdatePower 0x004F2CC0. A snapshot house
// with resolved original values wins over the recount.
struct HousePower {
    std::int32_t output = 0, drain = 0;
    bool low() const noexcept { return output < drain; }
    // RA1 HouseClass::Power_Fraction (CnC_Red_Alert CODE/HOUSE.CPP): full when
    // output covers drain, zero without any output, output/drain otherwise.
    double fraction() const noexcept;
};

// TODO-ORIGINAL 0x004F2CC0: the original zeroes the output while a spy
// blackout timer (house +0x1ec) runs; the snapshot has no blackout state yet.
// TODO-ORIGINAL 0x00449D40: turbine upgrades (Type PowerUpgradeBonus) and
// building powerups add to the scaled sum; not in the snapshot yet.
HousePower house_power(const rules::IniFile& rules,
                       const session::world::Snapshot& world, std::uint32_t house_id);

// Build duration multiplier (greater than one) while low on power. Recovered
// from game.exe 0x006C22A0: speed = 1 - (1 - PowerFraction 0x004E8330) *
// LowPowerPenaltyModifier, clamped to MinLowPowerProductionSpeed (always) and
// MaxLowPowerProductionSpeed (only when the fraction is below 1), 0.01 when
// zero. Build frames are F2I(cost / speed) 0x0077F110; the factory clock Rate
// is frames/54 clamped 1..255 (0x004B9EB0). Returns 1/speed. Full power is 1.
double production_multiplier(const rules::IniFile& rules, const HousePower& power);

// Powered=yes buildings stop working below full power.
bool defenses_online(const HousePower& power);

// Sidebar lists of type names, each in its rules registry order.
struct BuildList {
    std::vector<std::string> buildings, units, infantry, aircraft;
};

// Uses checked HouseClass::Can_Build 004E3660 with prototype snapshot defaults.
// TODO-ORIGINAL 005D5FC0: captured factory ownership and aircraft dock capacity.
// TODO-ORIGINAL 004E3660: upgrade slots and queued limits need factory state.
// Missing tech level defaults to 10; stolen tech defaults to false.
// Missing lifetime build counters default to zero. Owner uses the country mask.
BuildList buildable_types(const rules::IniFile& rules,
                          const session::world::Snapshot& world,
                          std::uint32_t house_id);

bool can_build(const rules::IniFile& rules,
               const session::world::Snapshot& world, std::uint32_t house_id,
               const std::string& type);

// Side name of a house country from rules [Sides]; empty when unlisted.
std::string side_of(const rules::IniFile& rules,
                    const session::world::Snapshot& world, std::uint32_t house_id);

} // namespace ra2::sim::tech

namespace ra2::sim::tech {
// Recount each frame, including after damage, capture, placement, or destruction.
void update(const rules::IniFile&, session::world::Snapshot&);
bool building_online(const rules::IniFile&, const HousePower&, const std::string& type);
bool radar_online(const rules::IniFile&, const session::world::Snapshot&, std::uint32_t house_id);
}
