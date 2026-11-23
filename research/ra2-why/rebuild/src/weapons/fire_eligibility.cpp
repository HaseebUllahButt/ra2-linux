#include "fire_eligibility.h"
namespace ra2::weapons {
FireEligibilityPrefixResult base_fire_eligibility_prefix(const FireEligibilityPrefixInput& in) {
    auto need=[](const char* field) { return FireEligibilityPrefixResult{{},false,field}; };
    auto reject=[](int code) { return FireEligibilityPrefixResult{code,false,{}}; };
    if (!in.target_present) return need("target presence");
    if (!*in.target_present) return reject(5);
    if (!in.shooter_virtual_1c0) return need("shooter virtual +1C0");
    if (*in.shooter_virtual_1c0) return reject(3);
    if (!in.shooter_virtual_1bc) return need("shooter virtual +1BC");
    if (*in.shooter_virtual_1bc) return reject(5);
    if (!in.shooter_flag_325) return need("shooter +325");
    if (*in.shooter_flag_325) return reject(5);
    if (!in.shooter_virtual_1c4) return need("shooter virtual +1C4");
    if (*in.shooter_virtual_1c4) {
        if (!in.attachment_present) return need("shooter +208 attachment");
        if (*in.attachment_present) {
            if (!in.target_is_attachment) return need("attachment target");
            if (*in.target_is_attachment) return reject(3);
        }
    }
    if (!in.target_is_object) return need("target Object flag");
    if (!in.target_cell_resolved || !*in.target_cell_resolved) return need("target position and cell");
    if (*in.target_is_object) {
        if (!in.target_limbo) return need("target +76");
        if (*in.target_limbo) return reject(5);
        if (!in.target_flag_210) return need("target +210");
        if (*in.target_flag_210) return reject(5);
        if (!in.target_virtual_64) return need("target virtual +64");
        if (*in.target_virtual_64==5) {
            if (!in.target_cell_visible) return need("target cell visibility");
            if (!*in.target_cell_visible) {
                if (!in.averaged_weapon_damage) return need("averaged weapon damage");
                if (*in.averaged_weapon_damage>0) return reject(6);
                if (!in.target_house_allied) return need("target house alliance");
                if (!*in.target_house_allied) return reject(6);
            }
        }
    }
    if (!in.shooter_flag_7a) return need("shooter +7A");
    if (*in.shooter_flag_7a) return reject(5);
    if (!in.shooter_virtual_324) return need("shooter virtual +324");
    if (*in.shooter_virtual_324) {
        if (!in.shooter_original_kind) return need("shooter original kind");
        if (*in.shooter_original_kind!=1) return reject(6);
        if (!in.unit_type_bb1) return need("UnitType +BB1");
        if (!*in.unit_type_bb1) {
            if (!in.unit_type_bb0) return need("UnitType +BB0");
            if (!*in.unit_type_bb0) return reject(6);
        }
    }
    return {{},true,{}};
}
}

#include <cmath>
#include <stdexcept>
namespace ra2::weapons {
FireEligibilityPrefixResult base_ordinary_unit_fire_check(const OrdinaryUnitFireInput& in) {
    auto r=base_fire_eligibility_prefix(in.prefix);
    if (!r.continued) return r;
    auto need=[](const char* field) { return FireEligibilityPrefixResult{{},false,field}; };
    auto done=[](int code) { return FireEligibilityPrefixResult{code,false,{}}; };
    if (!in.shooter_is_unit || !in.target_is_unit || !*in.shooter_is_unit || !*in.target_is_unit ||
        !in.prefix.target_is_object || !*in.prefix.target_is_object)
        return need("ordinary Unit kinds");
    if (!in.primary_weapon_index_zero || !*in.primary_weapon_index_zero)
        return need("primary weapon index");
    if (!in.weapon_present) return need("selected weapon");
    if (!*in.weapon_present) return done(6);
    if (!in.weapon_special_flags_clear || !*in.weapon_special_flags_clear)
        return need("special weapon eligibility flags");
    if (!in.warhead_present) return need("selected warhead");
    if (*in.warhead_present) {
        if (!in.target_virtual_1bc) return need("target virtual +1BC");
        if (*in.target_virtual_1bc) {
            if (!in.warhead_flag_157) return need("warhead +157");
            if (!*in.warhead_flag_157) return done(5);
        }
    }
    if (!in.alternate_weapon_effects_clear || !*in.alternate_weapon_effects_clear)
        return need("alternate weapon effect state");
    if (!in.target_virtual_54) return need("target virtual +54");
    if (*in.target_virtual_54) return need("air target eligibility");
    if (!in.target_cell_land_type) return need("target cell land type");
    if (*in.target_cell_land_type==2 || *in.target_cell_land_type==6)
        return need("water target eligibility");
    if (!in.target_virtual_50) return need("target virtual +50");
    if (*in.target_virtual_50) {
        if (!in.shooter_type_54c) return need("shooter type +54C");
        if (*in.shooter_type_54c==1) return done(5);
    }
    if (!in.barrel_override_active) return need("burst and barrel override");
    if (*in.barrel_override_active) return need("burst and barrel override result");
    if (!in.timer || !in.frame) return need("weapon timer");
    try { if (!base_fire_timer_ready(*in.timer,*in.frame)) return done(3); }
    catch (const std::exception&) { return need("weapon timer arithmetic"); }
    if (!in.ammo_field_254) return need("shooter +254");
    if (*in.ammo_field_254==0) return done(1);
    if (!in.weapon_flag_10f) return need("weapon DecloakToFire flag");
    if (*in.weapon_flag_10f) {
        if (!in.shooter_state_1b8) return need("shooter +1B8 cloak state");
        if (*in.shooter_state_1b8!=0) return done(9);
    }
    if (!in.shooter_type_af2) return need("shooter type +AF2");
    if (*in.shooter_type_af2) return done(8);
    if (!*in.warhead_present) return need("warhead required for armor check");
    if (!in.warhead_special_flags_clear || !*in.warhead_special_flags_clear)
        return need("special warhead eligibility flags");
    if (!in.warhead_armor_verse || !std::isfinite(*in.warhead_armor_verse))
        return need("warhead armor verse");
    if (*in.warhead_armor_verse==0.0) return done(5);
    if (!in.target_flag_325) return need("target +325");
    if (*in.target_flag_325) return done(5);
    if (!in.matching_bridge_flags || !*in.matching_bridge_flags)
        return need("different bridge layers");
    if (!in.shooter_type_b47) return need("shooter type +B47");
    if (*in.shooter_type_b47) {
        if (!in.shooter_virtual_328) return need("shooter virtual +328");
        if (*in.shooter_virtual_328) return done(5);
    }
    return done(0);
}
}
