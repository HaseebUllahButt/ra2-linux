#include "projectile_damage.h"
#include <sim/fp.h>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ra2::weapons {
std::optional<int> base_projectile_damage(const ProjectileDamageInput& input) {
    if (input.sonic || input.fire_particles) return 0;
    if (input.weapon_damage<=0) return input.weapon_damage;
    if (!std::isfinite(input.house_firepower) || !std::isfinite(input.object_firepower) ||
        input.house_firepower<0 || input.object_firepower<0) return {};
    try {
        const auto integer=[](double n)->std::optional<int> {
            if (!std::isfinite(n) || n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max()) return {};
            return static_cast<int>(n);
        };
        auto damage=integer(sim::fp::multiply(sim::fp::multiply(input.house_firepower,input.object_firepower),input.weapon_damage));
        if (!damage) return {};
        if ((input.veteran && input.veteran_firepower) ||
            (input.elite && (input.veteran_firepower || input.elite_firepower))) {
            if (!std::isfinite(input.veteran_multiplier) || input.veteran_multiplier<0) return {};
            damage=integer(sim::fp::multiply(*damage,input.veteran_multiplier));
        }
        return damage;
    } catch (const std::exception&) { return {}; }
}
}
