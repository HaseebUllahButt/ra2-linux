#include "prototype_economy.h"
#include "fs/mix_bootstrap.h"
#include <cassert>
#include <iostream>
using namespace ra2;
namespace sw=session::world;
sw::Snapshot fixture(const char* type,int overlay) {
    sw::Snapshot w;w.setup_progress=sw::SetupProgress::PrototypeComplete;w.cell_order=std::vector<std::uint32_t>{};
    sw::House h;h.id=1;h.cash=0;w.houses.push_back(h);
    for(int y=1;y<=8;++y) for(int x=1;x<=12;++x) {
        unsigned k=y*512+x;auto& c=w.cells[k];c.allocated=true;c.heads=weapons::CellContentHeads{};
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.terrain=sim::original_default_terrain_cell({std::uint16_t(x),std::uint16_t(y)});
        c.terrain->ramp_index=0;c.overlay_data_11e=11;w.cell_order->push_back(k);
    }
    for(int x=7;x<=11;++x) w.cells[4*512+x].terrain->overlay_type_index=overlay;
    sw::Entity e;e.id=1;e.house_id=1;e.type=type;e.lifetime=sw::Lifetime::Placed;e.resolved_hp=100;
    e.cell=ra2map::Cell{3,4};e.position=sim::Coord{896,1152,0};e.layer=sw::Layer::Ground;e.next=0;
    w.entities.push_back(e);w.cells[4*512+3].heads->ground_head=1;w.cells[4*512+3].ground_occupancy_124=32;
    e.id=2;e.type="GAREFN";e.kind=sw::Kind::Structure;e.cell=ra2map::Cell{2,4};w.entities.push_back(e);
    return w;
}
int main(int argc,char** argv) {
    fs::MixFileSystem files(argc>1?argv[1]:"/path/to/user/ra2/game");
    files.mount_language(fs::GameVariant::RedAlert2);files.bootstrap(fs::GameVariant::RedAlert2);files.mount_resources();
    auto bytes=files.read_file("rules.ini");assert(bytes);rules::IniFile rules;rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
    assert(rules.read_integer("CMIN","Storage")==20);assert(rules.read_integer("HARV","Storage")==40);
    for(auto type:{"CMIN","HARV"}) for(int overlay:{105,28}) {
        auto w=fixture(type,overlay);sim::PrototypeEconomy economy(rules);
        int first=0;
        for(unsigned frame=0;frame<5000 && !first;++frame) {
            economy.tick(w,frame);auto cash=w.houses[0].cash;int load=economy.load(1);
            economy.tick(w,frame);assert(cash==w.houses[0].cash && load==economy.load(1));first=*cash;
        }
        assert(first==rules.read_integer(type,"Storage")*rules.read_integer(overlay==28?"Cruentus":"Riparius","Value"));
    }
    auto w=fixture("CMIN",105);w.entities.pop_back();sim::PrototypeEconomy idle(rules);
    for(unsigned f=0;f<100;++f) { idle.tick(w,f); }
    assert(idle.load(1)==0);
    rules.parse("[Riparius]\nGrowth=2\nSpread=2\n[General]\nTiberiumGrows=yes\nTiberiumSpreads=yes\n");
    sim::PrototypeEconomy growth(rules);w=fixture("CMIN",105);w.entities.clear();w.cells[4*512+7].overlay_data_11e=10;
    growth.tick(w,0);growth.tick(w,2);assert(*w.cells[4*512+7].overlay_data_11e==11);assert(w.cells[3*512+7].terrain->overlay_type_index==105);
    std::cout<<"prototype economy: passed real base rules, miners, gems, credits, growth, spread, duplicate frames, no refinery\n";
}
