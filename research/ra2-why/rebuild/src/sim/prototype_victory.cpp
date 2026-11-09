#include "prototype_victory.h"
#include <stdexcept>

namespace ra2::sim {
namespace {
// TODO-ORIGINAL: simplified defeat rule. The original gamemd.exe announces a
// result through HouseClass::Win 004FC9E0 and HouseClass::Lose 004FCBD0
// (community-verified YRpp bindings in rebuild/symbols-extended.csv; both
// YRpp forks agree). Known Lose callers: AcceptDefeat 004FC0B0, the trigger
// dispatcher TActionClass::Execute 006DD8B0 and team script lose 006EDE60.
// The automatic no-assets defeat check is not recovered yet.
// Guide evidence, not RA2 proof: the RA1 ancestor checks the per-house asset
// scans in HouseClass::AI after frame 0 and defeats a house with no
// buildings, aircraft, units, infantry or vessels (CnC_Red_Alert
// CODE/HOUSE.CPP, the MPlayer_Defeated call); TS keeps the same shape
// (TSpp house.h IsDefeated; MPlayer_Defeated at TS 004BF4C0).
// This prototype counts only surviving Units, Infantry and Structures of the
// two given houses; Aircraft never count here, and short-game and ally
// rules are not modeled.
bool surviving_entity(const session::world::Entity& entity) {
    if (entity.lifetime==session::world::Lifetime::Destroyed) return false;
    switch (entity.kind) {
        case session::world::Kind::Unit:
        case session::world::Kind::Infantry:
        case session::world::Kind::Structure:
            // Unknown health stays alive: staged constructors only store
            // resolved positive values. Known zero or less is dead.
            return !entity.resolved_hp || *entity.resolved_hp>0;
        default:
            return false;
    }
}
bool auxiliary_house(const session::world::House& house) {
    return house.role && (*house.role==session::world::HouseRole::Neutral ||
                          *house.role==session::world::HouseRole::Special);
}
}

Victory evaluate_prototype_victory(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house) {
    if (!player_house || !enemy_house)
        throw std::invalid_argument("victory house ids use zero as null");
    if (player_house==enemy_house)
        throw std::invalid_argument("player and enemy house ids must differ");
    bool player_found=false, enemy_found=false;
    for (const auto& house:world.houses) {
        if (house.id==player_house) {
            player_found=true;
            if (auxiliary_house(house))
                throw std::invalid_argument("player house is map neutral or special");
        }
        if (house.id==enemy_house) {
            enemy_found=true;
            if (auxiliary_house(house))
                throw std::invalid_argument("enemy house is map neutral or special");
        }
    }
    if (!player_found || !enemy_found)
        throw std::invalid_argument("victory house ids must exist in the snapshot");
    bool player_alive=false, enemy_alive=false;
    for (const auto& entity:world.entities) {
        if (!surviving_entity(entity)) continue;
        if (entity.house_id==player_house) player_alive=true;
        else if (entity.house_id==enemy_house) enemy_alive=true;
    }
    if (player_alive && enemy_alive) return Victory::InProgress;
    if (player_alive) return Victory::PlayerWon;
    if (enemy_alive) return Victory::PlayerLost;
    return Victory::Draw;
}

Victory VictoryState::update(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house) {
    if (!settled()) result=evaluate_prototype_victory(world,player_house,enemy_house);
    return result;
}
}
