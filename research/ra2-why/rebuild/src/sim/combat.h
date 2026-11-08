#pragma once
#include <cstdint>
#include <optional>
#include <vector>
namespace ra2::sim {
// Resolved inputs for 00489180. These are not guessed warhead defaults.
struct DamageInputs {
    int damage=0, distance=0, maximum=0;
    float cell_spread=0, percent_at_max=0;
    double armor_verse=0;
    bool scenario_inert=false, warhead_present=true;
};
// Shared armor/range arithmetic only. No projectile or derived damage effects.
// Throws on unverified precision and overflow paths.
int total_damage(const DamageInputs&);
struct ResolvedCandidate { std::uint32_t id; bool eligible; int score; };
// 006F8DF0 global-table reduction. Eligibility and scoring need 006F7CA0 first.
std::optional<std::uint32_t> select_resolved_table_target(const std::vector<ResolvedCandidate>&);
// 006F8DF0's threat-to-category flag conversion, before candidate scans.
std::uint32_t threat_categories(std::uint32_t threat);
}
