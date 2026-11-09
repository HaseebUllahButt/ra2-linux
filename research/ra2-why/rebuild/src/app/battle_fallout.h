#pragma once
#include "../sim/damage_runtime.h"
#include "../sim/prototype_victory.h"
namespace ra2::app {
// Resolve every house before the result screen, shroud, effects and score.
inline sim::PrototypeCombatTick resolve_fallen_houses(session::world::Snapshot& world,
    sim::VictoryState& victory,sim::PrototypeCombatRuntime& combat,std::uint32_t player,
    const std::vector<std::uint32_t>& enemies,const sim::VictoryRules& rules,std::uint64_t frame) {
    victory.update(world,player,enemies,rules);
    sim::PrototypeCombatTick result;
    for(const auto house:victory.fallen_houses) {
        auto deaths=combat.destroy_house(world,house,frame);
        result.destroyed+=deaths.destroyed;
        result.events.insert(result.events.end(),deaths.events.begin(),deaths.events.end());
        result.unresolved.insert(result.unresolved.end(),deaths.unresolved.begin(),deaths.unresolved.end());
    }
    return result;
}
}
