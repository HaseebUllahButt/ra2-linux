#pragma once
#include "../session/world/world.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
namespace ra2::app {
inline ra2map::Cell reserve_formation_cell(const session::world::Snapshot& world,ra2map::Cell goal,
    std::uint32_t actor,const std::map<std::uint32_t,ra2map::Cell>& pending,std::set<std::uint32_t>& destinations) {
    // TODO-ORIGINAL: reserve separate group destinations, then retry
    // temporary obstructions. Original formation planning remains separate.
    for(int r=0;r<=8;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
        if(std::max(std::abs(dx),std::abs(dy))!=r)continue;
        const int x=int(goal.x)+dx,y=int(goal.y)+dy;
        if(x<0||y<0||x>=512||y>=512)continue;
        const auto key=std::uint32_t(y)*512+x;
        const auto at=world.cells.find(key);
        if(at==world.cells.end()||destinations.count(key))continue;
        bool reserved=false;
        for(const auto& [id,destination]:pending)
            if(id!=actor && destination.x==x && destination.y==y) {reserved=true;break;}
        if(reserved)continue;
        const auto& c=at->second;
        if(c.allocated!=true||c.cell_flag_100!=false||!c.heads||c.heads->ground_head||c.ground_occupancy_124!=0||c.unit_land_speed.value_or(0)<=0)continue;
        destinations.insert(key);
        return {std::uint16_t(x),std::uint16_t(y)};
    }
    return goal;
}
}
