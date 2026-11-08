#include "target.h"
#include <cmath>
namespace ra2::sim {
CandidateDecision check_candidate_prefix(const CandidatePrefix& c) {
    using S=CandidateStatus;
    auto need=[](const char* field){return CandidateDecision{S::NeedsInput,field};};
    auto reject=[](const char* address){return CandidateDecision{S::Reject,address};};
    if (!c.threat) return need("006F7CDB threat mask");
    if (!(*c.threat&0x18200)) {
        if (!c.fire_status) return need("006F7CE8 attacker virtual +3BC");
        if (*c.fire_status==5) return reject("006F7CF1");
    }
    if (!c.initial_health) return need("006F7CF7 target +70");
    if (*c.initial_health<1) {
        if (!c.attacker_type_394) return need("006F7D0D attacker type +394");
        if (*c.attacker_type_394==2) return reject("006F7D13");
    }
    if (!c.weapon_warhead_present) return need("006F7D19 selected weapon/warhead");
    if (*c.weapon_warhead_present) {
        if (!c.armor_verse) return need("006F7D3D warhead +A0+armor*8");
        // Original comparison uses the float at 007F4E38, including unordered rejection.
        constexpr double threshold=static_cast<double>(0.02f);
        if (std::isnan(*c.armor_verse) || *c.armor_verse<=threshold) return reject("006F7D4F");
    }
    if (!c.attacker_type_604) return need("006F7D5F attacker type +604");
    if (*c.attacker_type_604==2) {
        if (!c.target_slot_50) return need("006F7D6B target virtual +50");
        if (*c.target_slot_50) {
            if (!c.target_cell_ec) return need("006F7D7C target cell +EC");
            if (*c.target_cell_ec!=2) return reject("006F7D82");
        }
    }
    if (!c.target_flag_81) return need("006F7D90 target +81");
    if (*c.target_flag_81) return reject("006F7D98");
    if (!c.current_health) return need("006F7D9E target +6C");
    if (*c.current_health==0) return reject("006F7DA3");
    if (!c.target_state_220) return need("006F7DA9 target +220");
    if (*c.target_state_220==2) {
        if (!c.cell_visible) return need("006F7DD4 cell visibility 004870D0");
        if (!*c.cell_visible) {
            if (!c.same_house) return need("006F7DE9 target/attacker house identity");
            if (!*c.same_house) return reject("006F7DEB");
        }
    }
    if (!c.target_flag_3d5) return need("006F7DF1 target +3D5");
    if (!*c.target_flag_3d5) return reject("006F7DF9");
    if (!c.mission_control_flag_4) return need("006F7E06 mission control +4");
    if (*c.mission_control_flag_4) return reject("006F7E0B");
    if (!c.target_height) return need("006F7E15 target virtual +1C8");
    if (*c.target_height<-20) return reject("006F7E1E");
    return {S::Continue,"006F7E24: zone, diplomacy, range, visibility and score decisions remain"};
}
}
