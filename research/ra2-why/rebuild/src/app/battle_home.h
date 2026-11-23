#pragma once
#include "../session/world/world.h"
#include "../rules/ini_file.h"
namespace ra2::app {
inline const session::world::Entity* home_building(const session::world::Snapshot& world,
    std::uint32_t house,const rules::IniFile& rules) {
    // Base executable 0051C140 (CenterBase; name at 0051C0D0) prefers
    // ConstructionYard (+1353), then another owned building. The export
    // does not include this command's body.
    const session::world::Entity* fallback=nullptr;
    for(const auto& e:world.entities) {
        if(e.house_id!=house || e.kind!=session::world::Kind::Structure ||
           e.lifetime!=session::world::Lifetime::Placed || e.resolved_hp.value_or(0)<=0 || !e.cell || !e.position)continue;
        if(rules.read_bool(e.type,"ConstructionYard"))return &e;
        if(!fallback)fallback=&e;
    }
    return fallback;
}
}
