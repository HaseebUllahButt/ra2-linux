#include "battle_radar.h"
#include "../session/world/setup.h"
#include "../sim/prototype_tech.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::render;
int main(int argc,char** argv) {
    assert(argc==2);
    // Real matches carry clear markers even before a powered radar exists.
    auto assets=session::world::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    session::world::PrototypeBattleSetup setup;
    setup.players={{"Player","Americans",10000,0,0,2,true},
                   {"Computer","Russians",10000,1,1,2,false}};
    auto battle=session::world::prototype_battle(assets,setup,sim::Random(42).state());
    assert(battle);
    std::size_t clear_cells=0;
    for(const auto& [key,cell]:battle->snapshot.cells) {
        (void)key;
        if(cell.terrain&&cell.terrain->resolved_tile_index==0xffffu)++clear_cells;
    }
    assert(clear_cells>0);
    const auto style=battle_radar_style(assets.rules());
    RadarView real_view{MapGeometry::from_ini(assets.map_ini()),212,96,{}};
    const auto real_radar=draw_battle_radar(battle->snapshot,assets.theater(),real_view,style,true);
    assert(real_radar.width==212&&real_radar.height==96&&real_radar.rgba.size()==212u*96u*4u);
    // The Allied sidebar must offer the original Air Force Command radar.
    auto& world=battle->snapshot;
    const auto player=world.houses.front().id;
    auto add_building=[&](const char* type) {
        session::world::Entity e;
        e.id=1000+world.entities.size();e.house_id=player;e.type=type;
        e.kind=session::world::Kind::Structure;e.lifetime=session::world::Lifetime::Placed;
        e.resolved_hp=1000;e.maximum_hp=1000;
        world.entities.push_back(e);
    };
    for(const auto* type:{"GACNST","GAPOWR","GAPILE","GAREFN"})add_building(type);
    const auto american=sim::tech::buildable_types(assets.rules(),world,player);
    assert(std::find(american.buildings.begin(),american.buildings.end(),"AMRADR")!=american.buildings.end());
    world.houses.front().country="British";world.houses.front().country_index.reset();
    const auto eligible=sim::tech::buildable_types(assets.rules(),world,player);
    assert(std::find(eligible.buildings.begin(),eligible.buildings.end(),"GAAIRC")!=eligible.buildings.end());
    add_building("GAAIRC");
    sim::tech::update(assets.rules(),world);
    assert(sim::tech::radar_online(assets.rules(),world,player));
    std::cout<<"MP02T2 powered radar handles "<<clear_cells<<" clear Cells\n";
}
