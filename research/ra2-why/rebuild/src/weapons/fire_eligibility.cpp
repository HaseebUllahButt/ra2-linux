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
