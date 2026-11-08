#include "target.h"
#include "fp.h"
#include <limits>
#include <cmath>
namespace ra2::sim {
CandidateDecision check_candidate_prefix(const CandidatePrefix& c) {
    using S=CandidateStatus;
    const bool base=c.edition==CandidateEdition::BaseRa2;
    auto label=[base](const char* text)->std::string {
        if (!base) return text;
        const std::pair<const char*,const char*> labels[]={
            {"006F7CDB","006C55AB threat mask"},{"006F7CE8","006C55BC attacker virtual +364"},
            {"006F7CF1","006C55C5"},{"006F7CF7","006C55CB target +70"},
            {"006F7D0D","006C55DC attacker type +364"},{"006F7D13","006C55E3"},
            {"006F7D19","006C55E9 selected weapon/warhead"},
            {"006F7D3D","006C560D warhead +A0+armor*8"},
            {"006F7D90","006C5630 target +76"},{"006F7D98","006C5635"},
            {"006F7D9E","006C563B target +6C"},{"006F7DA3","006C5640"},
            {"006F7DA9","006C5646 target +1B8"},
            {"006F7DD4","006C5672 cell visibility 0047C2E0"},
            {"006F7DE9","006C5687 house identity"},{"006F7DEB","006C5689"},
            {"006F7DF1","006C568F target +32D"},{"006F7DF9","006C5697"},
            {"006F7E06","006C56A4 mission control +4"},{"006F7E0B","006C56A9"},
            {"006F7E15","006C56B3 target virtual +1B0"},{"006F7E1E","006C56BC"}
        };
        const std::string original=text;
        for (const auto& entry:labels) if (original.compare(0,8,entry.first)==0) return entry.second;
        return original;
    };
    auto need=[&](const char* field){return CandidateDecision{S::NeedsInput,label(field)};};
    auto reject=[&](const char* address){return CandidateDecision{S::Reject,label(address)};};
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
        if (base) {
            // 006C560D..006C5622: multiply by double 100, truncate, compare with one.
            if (!std::isfinite(*c.armor_verse) || std::abs(*c.armor_verse)>1000000)
                return need("006C561A: exceptional armor-verse conversion is unsupported");
            const auto percentage=fp::multiply(*c.armor_verse,100.0);
            if (static_cast<int>(percentage)==1) return reject("006C5622");
        } else {
            // Expansion comparison uses float 0.02 at 007F4E38, including unordered rejection.
            constexpr double threshold=static_cast<double>(0.02f);
            if (std::isnan(*c.armor_verse) || *c.armor_verse<=threshold) return reject("006F7D4F");
        }
    }
    if (!base && !c.attacker_type_604) return need("006F7D5F attacker type +604");
    if (!base && *c.attacker_type_604==2) {
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
    return {S::Continue,base?"006C56C2: later candidate decisions remain":"006F7E24: later candidate decisions remain"};
}
}

#include <algorithm>
#include <stdexcept>
namespace ra2::sim {
double threat_score(const ThreatScoreInputs& in) {
    if (!in.target_type_present) return 0;
    const auto& c=in.coefficients;
    double score=0;
    if (in.target_is_techno) {
        if (in.reverse_armor_verse) {
            score=fp::multiply(c.target_effectiveness,*in.reverse_armor_verse);
            if (in.target_targets_attacker) score=-score;
        }
        score=fp::add(fp::multiply(c.special_threat,in.special_threat_value),score);
        if (in.preferred_enemy_house) score=fp::add(score,in.enemy_house_bonus);
    }
    if (in.forward_armor_verse) score=fp::add(fp::multiply(c.my_effectiveness,*in.forward_armor_verse),score);
    if (in.target_strength<=0 || in.target_health<0 || in.resolved_distance_world_units<0)
        throw std::invalid_argument("0070CD10 needs resolved positive Strength and nonnegative health/distance");
    // 005F5C60 returns current HP / Strength under the same x87 control mode.
    const auto ratio=fp::divide(in.target_health,in.target_strength);
    score=fp::add(fp::multiply(ratio,c.strength),score);
    const int range=in.range_world_units/256;
    const int distance=in.explicit_scan_origin?in.resolved_distance_world_units:in.resolved_distance_world_units/256;
    const auto beyond=std::max<std::int64_t>(0,static_cast<std::int64_t>(distance)-range);
    if (beyond>2147483647) throw std::range_error("0070CD10 distance subtraction overflow");
    score=fp::add(fp::multiply(static_cast<double>(beyond),c.distance),score);
    return fp::add(score,100000.0); // Original double at 007F4E90.
}
}
