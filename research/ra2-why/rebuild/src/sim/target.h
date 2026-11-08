#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace ra2::sim {
// Typed inputs for 006F7CA0's prefix, ending before 006F7E24.
// Offset names preserve uncertain field meanings instead of inventing gameplay roles.
enum class CandidateEdition { BaseRa2, YuriRevenge };
struct CandidatePrefix {
    CandidateEdition edition=CandidateEdition::BaseRa2;
    std::optional<std::uint32_t> threat;
    std::optional<int> fire_status, initial_health, attacker_type_394;
    std::optional<bool> weapon_warhead_present;
    std::optional<double> armor_verse;
    std::optional<int> attacker_type_604;
    std::optional<bool> target_slot_50;
    std::optional<int> target_cell_ec;
    std::optional<bool> target_flag_81;
    std::optional<int> current_health, target_state_220;
    std::optional<bool> cell_visible, same_house;
    std::optional<bool> target_flag_3d5, mission_control_flag_4;
    std::optional<int> target_height;
};
enum class CandidateStatus { Reject, Continue, NeedsInput };
struct CandidateDecision { CandidateStatus status; std::string evidence; };
// Continue means only that the prefix passed. It never means the target is eligible.
CandidateDecision check_candidate_prefix(const CandidatePrefix&);
}

namespace ra2::sim {
struct ThreatCoefficients {
    double my_effectiveness, target_effectiveness, special_threat, strength, distance;
};
// Resolved field/virtual outputs for 0070CD10. No candidate eligibility is implied.
struct ThreatScoreInputs {
    ThreatCoefficients coefficients;
    bool target_type_present=true, target_is_techno=true, target_targets_attacker=false;
    std::optional<double> reverse_armor_verse, forward_armor_verse;
    double special_threat_value=0, enemy_house_bonus=0;
    bool preferred_enemy_house=false;
    int target_health=0, target_strength=0;
    int range_world_units=0, resolved_distance_world_units=0;
    bool explicit_scan_origin=false;
};
// The original routine returns a floating score; its caller truncates it.
double threat_score(const ThreatScoreInputs&);
}
