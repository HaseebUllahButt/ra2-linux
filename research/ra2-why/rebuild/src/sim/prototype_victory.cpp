#include "prototype_victory.h"
#include <algorithm>
#include <cctype>
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

void require_houses(const session::world::Snapshot& world,
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
}
bool same_type(const std::string& a, const std::string& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](unsigned char x,unsigned char y){
        return std::tolower(x)==std::tolower(y);
    });
}
// House +228, +230, +234, +238 and the two BaseUnit counts of 004E4110.
struct Owned { int units=0, buildings=0, infantry=0, aircraft=0, base_units=0; };
bool fallen(const Owned& owned, bool short_game) {
    if (short_game) return owned.buildings==0 && owned.base_units<1;
    return owned.buildings==0 && owned.aircraft==0 && owned.infantry==0 && owned.units<=0;
}
}

namespace {
Owned owned_by(const session::world::Snapshot& world, std::uint32_t house, const VictoryRules& rules) {
    Owned owned;
    for (const auto& entity:world.entities) {
        if (entity.house_id!=house || entity.lifetime!=session::world::Lifetime::Placed) continue;
        if (entity.resolved_hp && *entity.resolved_hp<=0) continue;
        switch (entity.kind) {
            case session::world::Kind::Structure: ++owned.buildings; break;
            case session::world::Kind::Infantry: ++owned.infantry; break;
            case session::world::Kind::Aircraft: ++owned.aircraft; break;
            case session::world::Kind::Unit:
                ++owned.units;
                for (std::size_t i=0;i<rules.base_units.size() && i<2;++i)
                    if (same_type(entity.type,rules.base_units[i])) { ++owned.base_units; break; }
                break;
            default: break;
        }
    }
    return owned;
}
}

bool original_house_fallen(const session::world::Snapshot& world, std::uint32_t house,
    const VictoryRules& rules) {
    return fallen(owned_by(world,house,rules),rules.short_game);
}

Victory evaluate_original_victory(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house, const VictoryRules& rules) {
    require_houses(world,player_house,enemy_house);
    const bool player_alive=!original_house_fallen(world,player_house,rules);
    const bool enemy_alive=!original_house_fallen(world,enemy_house,rules);
    if (player_alive && enemy_alive) return Victory::InProgress;
    if (player_alive) return Victory::PlayerWon;
    if (enemy_alive) return Victory::PlayerLost;
    return Victory::Draw;
}

Victory evaluate_prototype_victory(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house) {
    require_houses(world,player_house,enemy_house);
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

Victory VictoryState::update(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house, const VictoryRules& rules) {
    if (!settled()) result=evaluate_original_victory(world,player_house,enemy_house,rules);
    return result;
}

bool VictoryState::fallen(std::uint32_t house) const noexcept {
    return std::find(fallen_houses.begin(),fallen_houses.end(),house)!=fallen_houses.end();
}

Victory VictoryState::update(const session::world::Snapshot& world, std::uint32_t player_house,
    const std::vector<std::uint32_t>& enemy_houses, const VictoryRules& rules) {
    if (settled()) return result;
    if (enemy_houses.empty()) throw std::invalid_argument("victory needs one enemy house or more");
    for (const auto enemy:enemy_houses) require_houses(world,player_house,enemy);
    const auto check=[&](std::uint32_t house) {
        if (!fallen(house) && original_house_fallen(world,house,rules)) fallen_houses.push_back(house);
        return fallen(house);
    };
    const bool player_fallen=check(player_house);
    bool enemies_fallen=true;
    for (const auto enemy:enemy_houses) enemies_fallen=check(enemy) && enemies_fallen;
    if (player_fallen && enemies_fallen) result=Victory::Draw;
    else if (player_fallen) result=Victory::PlayerLost;
    else if (enemies_fallen) result=Victory::PlayerWon;
    return result;
}
}
