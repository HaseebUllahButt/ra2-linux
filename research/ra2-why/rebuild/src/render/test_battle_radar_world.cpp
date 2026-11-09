#include "battle_radar.h"
#include "../session/world/setup.h"
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
    std::cout<<"MP02T2 powered radar handles "<<clear_cells<<" clear Cells\n";
}
