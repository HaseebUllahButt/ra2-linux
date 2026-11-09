#include "battle_scene.h"
#include <algorithm>
#include <map>

namespace ra2::app {
sim::Snapshot battle_scene_state(const session::world::Snapshot& world,std::uint64_t frame) {
    sim::Snapshot view;
    view.frame=frame;
    std::map<std::uint32_t,std::string> names;
    for (const auto& house:world.houses) names.emplace(house.id,house.name);
    for (const auto& object:world.entities) {
        if (object.lifetime!=session::world::Lifetime::Placed || !object.cell || !object.position ||
            object.kind==session::world::Kind::Terrain) continue;
        sim::Entity e;
        e.id=object.id; e.type=object.type; e.cell=*object.cell; e.position=object.position;
        switch (object.kind) {
        case session::world::Kind::Structure:e.kind=sim::Kind::Structure;break;
        case session::world::Kind::Infantry:e.kind=sim::Kind::Infantry;break;
        case session::world::Kind::Aircraft:e.kind=sim::Kind::Aircraft;break;
        default:e.kind=sim::Kind::Unit;break;
        }
        const auto house=names.find(object.house_id);
        if (house!=names.end()) e.house=house->second;
        e.hp=object.resolved_hp;
        e.dead=object.resolved_hp && *object.resolved_hp<=0;
        e.facing=object.hull_facing.value_or(0);
        e.turret_facing=object.turret_facing.value_or(e.facing);
        e.moving=object.locomotor_moving.value_or(false);
        e.subcell=0; // TODO-ORIGINAL: retain the original Infantry subcell in setup.
        if (object.resolved_hp && object.maximum_hp && *object.maximum_hp>0)
            e.map_health=static_cast<int>(std::clamp<std::int64_t>(
                std::int64_t(*object.resolved_hp)*256 / *object.maximum_hp,0,256));
        else e.map_health=256; // TODO-ORIGINAL: resolve every authored actor's health.
        view.entities.push_back(std::move(e));
    }
    return view;
}
}
