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
// A snapshot house with resolved original values wins over the recount.
struct HousePower {
    std::int32_t output = 0, drain = 0;
    bool low() const noexcept { return output < drain; }
    // RA1 HouseClass::Power_Fraction (CnC_Red_Alert CODE/HOUSE.CPP): full when
    // output covers drain, zero without any output, output/drain otherwise.
    double fraction() const noexcept;
};

// TODO-ORIGINAL: RA2 recomputes per-house output and drain in House AI
// 004F2CC0; this prototype recounts from the snapshot instead.
HousePower house_power(const rules::IniFile& rules,
                       const session::world::Snapshot& world, std::uint32_t house_id);

// Build duration multiplier (greater than one) while low on power. Base RULES.INI [General]
// MinLowPowerProductionSpeed=.5, MaxLowPowerProductionSpeed=.8 and
// LowPowerPenaltyModifier=1 give speeds from .8 (barely short) down to
// .5 (blackout). Full power is 1. TODO-ORIGINAL: exact original arithmetic;
// guide evidence is RA1 FactoryClass (CnC_Red_Alert CODE/FACTORY.CPP), which
// divides the build rate by the clamped power fraction.
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
