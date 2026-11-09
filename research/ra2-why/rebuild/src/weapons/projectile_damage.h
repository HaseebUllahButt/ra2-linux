#pragma once
#include <optional>
namespace ra2::weapons {
// Resolved inputs at 006CAB7C. These are not the target armor calculation.
struct ProjectileDamageInput {
    int weapon_damage; // WeaponType +A4.
    bool sonic, fire_particles; // WeaponType +10C and +105.
    double house_firepower, object_firepower; // House +D0, Techno +128.
    bool veteran, elite, veteran_firepower, elite_firepower;
    double veteran_multiplier; // Rules +590.
};
// Returns the original projectile's raw damage. Exceptional inputs are
// unresolved. This does not run target damage or spawn the projectile.
std::optional<int> base_projectile_damage(const ProjectileDamageInput&);
}
