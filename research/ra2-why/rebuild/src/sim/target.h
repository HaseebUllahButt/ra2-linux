#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace ra2::sim {
// Typed inputs for 006F7CA0's prefix, ending before 006F7E24.
// Offset names preserve uncertain field meanings instead of inventing gameplay roles.
struct CandidatePrefix {
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
