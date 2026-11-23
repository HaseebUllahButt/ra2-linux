// Focused checks for the rank-wired damage path: rank thresholds, the
// Techno armor prefix, and the veteran firepower multiplier assembly.
#include "../weapons/techno_damage.h"
#include "../weapons/projectile_damage.h"
#include "prototype_special.h"
#include <iostream>
#include <string>

using namespace ra2::sim;
namespace weapons = ra2::weapons;
using namespace ra2::weapons;
using namespace ra2::weapons;

namespace {
int failures = 0;
void require(bool ok, const std::string& what) {
    if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
}
constexpr bool close(double a, double b) { return std::abs(a - b) < 1e-9; }
} // namespace

int main() {
    // Rank thresholds: veteran at 1.0, elite at 2.0 (0074FF90/00750010).
    require(rank_of(0.0) == Rank::Rookie, "0.0 is rookie");
    require(rank_of(0.999) == Rank::Rookie, "0.999 is rookie");
    require(rank_of(1.0) == Rank::Veteran, "1.0 is veteran");
    require(rank_of(2.0) == Rank::Elite, "2.0 is elite");

    // Techno armor prefix: a promoted target divides incoming damage by
    // the [General] VeteranArmor value before the Object handler.
    ra2::weapons::TechnoDamageInput in{};
    in.raw_damage = 100;
    in.house_armor_multiplier = 1.0;
    in.object_armor_multiplier = 1.0;
    in.veteran = true;
    in.veteran_armor = true;
    in.veteran_armor_multiplier = 1.5;
    const auto promoted = ra2::weapons::base_techno_damage_prefix(in);
    require(promoted.stage == TechnoDamageStage::ObjectDamage, "veteran target stays on the Object path");
    require(promoted.damage == 66, "100 raw against VeteranArmor 1.5 leaves 66");

    // Rookie target: no armor divide.
    TechnoDamageInput rookie{};
    rookie.house_armor_multiplier = 1.0;
    rookie.object_armor_multiplier = 1.0;
    rookie.raw_damage = 100;
    const auto plain = base_techno_damage_prefix(rookie);
    require(plain.stage == TechnoDamageStage::ObjectDamage && plain.damage == 100,
            "rookie target takes full damage");

    // Projectile damage: the FIREPOWER veteran ability gates the
    // [General] VeteranCombat multiplier.
    ProjectileDamageInput shot{};
    shot.weapon_damage = 100;
    shot.house_firepower = 1.0;
    shot.object_firepower = 1.0;
    shot.veteran = true;
    shot.veteran_firepower = true;
    shot.veteran_multiplier = 1.1;
    const auto veteran_shot = base_projectile_damage(shot);
    require(veteran_shot && *veteran_shot == 110, "veteran FIREPOWER multiplies by VeteranCombat");

    // Without the FIREPOWER ability the multiplier does not apply.
    ProjectileDamageInput no_ability = shot;
    no_ability.veteran_firepower = false;
    const auto plain_shot = base_projectile_damage(no_ability);
    require(plain_shot && *plain_shot == 100, "no FIREPOWER ability keeps raw damage");

    if (failures) { std::cerr << failures << " failures\n"; return 1; }
    std::cout << "veteran damage path checks passed\n";
    return 0;
}
