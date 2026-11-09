#include "prototype_economy.h"
#include "fs/mix_bootstrap.h"
#include "pack/map_pack.h"
#include <algorithm>
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
    const auto* registry=rules.find_section("OverlayTypes");assert(registry);
    auto overlay_index=[&](const char* name) { for(std::size_t i=0;i<registry->entries.size();++i) if(registry->entries[i].value==name) return int(i);return -1; };
    int ore=overlay_index("TIB01"),gem=overlay_index("GEM01");assert(ore==102 && gem==27);
    for(auto type:{"CMIN","HARV"}) for(int overlay:{ore,gem}) {
        auto w=fixture(type,overlay);sim::PrototypeEconomy economy(rules);
        int first=0;bool teleported=false;
        for(unsigned frame=0;frame<5000 && !first;++frame) {
            auto before=*w.entities[0].cell;
            economy.tick(w,frame);
            auto after=*w.entities[0].cell;
            teleported |= std::abs(int(before.x)-after.x)>1 || std::abs(int(before.y)-after.y)>1;
            auto cash=w.houses[0].cash;int load=economy.load(1);
            economy.tick(w,frame);assert(cash==w.houses[0].cash && load==economy.load(1));first=*cash;
        }
        assert(teleported==(std::string(type)=="CMIN"));
        assert(first==rules.read_integer(type,"Storage")*rules.read_integer(overlay==gem?"Cruentus":"Riparius","Value"));
    }
    // Row 7 puts the new field beyond the previous adjacent-only scan.
    for(auto type:{"CMIN","HARV"}) for(int ore_y:{4,5,7}) {
        auto nearby=fixture(type,-1);
        nearby.cells[4*512+11].terrain->overlay_type_index=ore;
        sim::PrototypeEconomy economy(rules);
        economy.tick(nearby,0); // Choose the distant, initially sole field.
        nearby.cells[ore_y*512+4].terrain->overlay_type_index=ore;
        for(unsigned frame=1;frame<200 && !economy.load(1);++frame) economy.tick(nearby,frame);
        assert(economy.load(1)==1);
        assert(nearby.entities[0].cell->x==4 && nearby.entities[0].cell->y==ore_y);
        assert(nearby.cells[4*512+11].overlay_data_11e==11);
    }
    // 00475DB0: data zero still contains one bail. Removal restores TMP land.
    auto last_bail=fixture("CMIN",-1);
    auto& last_cell=last_bail.cells.at(4*512+3);
    last_cell.terrain->overlay_type_index=ore;last_cell.overlay_data_11e=0;
    last_cell.terrain->tmp_terrain=9;last_cell.terrain->land_type=5;
    sim::PrototypeEconomy depletion(rules);depletion.tick(last_bail,0);
    assert(depletion.load(1)==1 && last_cell.overlay_data_11e==0);
    assert(last_cell.terrain->overlay_type_index==-1 && last_cell.terrain->land_type==2);
    auto destroyed=fixture("HARV",105);destroyed.entities[0].resolved_hp=0;
    sim::PrototypeEconomy dead(rules);dead.tick(destroyed,0);assert(dead.load(1)==0);
    auto incomplete=fixture("CMIN",105);incomplete.setup_progress=sw::SetupProgress::Unknown;
    sim::PrototypeEconomy guard(rules);guard.tick(incomplete,0);assert(guard.load(1)==0);
    auto w=fixture("CMIN",105);w.entities.pop_back();sim::PrototypeEconomy idle(rules);
    for(unsigned f=0;f<100;++f) { idle.tick(w,f); }
    assert(idle.load(1)==0);
    auto text=std::string(reinterpret_cast<const char*>(bytes->data()),bytes->size());
    for(const auto& field:{std::string("Growth=2200"),std::string("Spread=2200")}) {
        auto position=text.find(field);assert(position!=std::string::npos);
        text.replace(position,field.size(),field.substr(0,field.find('=')+1)+"2");
    }
    rules.parse(text);
    sim::PrototypeEconomy growth(rules);w=fixture("CMIN",ore);w.entities.clear();w.cells[4*512+7].overlay_data_11e=10;
    growth.tick(w,0);growth.tick(w,2);assert(*w.cells[4*512+7].overlay_data_11e==11);assert(w.cells[3*512+7].terrain->overlay_type_index==ore);
    // Read actual map overlay bytes, including MP02T2 ore 102 and gems 27.
    auto map_bytes=files.read_file("MP02T2.MAP");assert(map_bytes);
    rules::IniFile map;map.parse(std::string_view(reinterpret_cast<const char*>(map_bytes->data()),map_bytes->size()));
    auto terrain=pack::load_terrain(map);
    assert(std::count(terrain.overlay.begin(),terrain.overlay.end(),ore)==303);
    assert(std::count(terrain.overlay.begin(),terrain.overlay.end(),gem)==59);
    for(int overlay:{ore,gem}) {
        auto k=std::size_t(std::find(terrain.overlay.begin(),terrain.overlay.end(),overlay)-terrain.overlay.begin());
        auto actual=fixture("CMIN",-1);auto& cell=actual.cells[4*512+3];
        cell.terrain->overlay_type_index=terrain.overlay[k];cell.overlay_data_11e=terrain.overlay_data[k];
        sim::PrototypeEconomy from_map(rules);from_map.tick(actual,0);assert(from_map.load(1)==1);
        const auto& harvested=actual.cells.at(4*512+3);
        assert(harvested.overlay_data_11e==terrain.overlay_data[k]-1 || harvested.terrain->overlay_type_index==-1);
    }
    // Preserve MP02T2 coordinates and every packed overlay, rather than
    // transplanting a resource byte into the small movement fixture.
    // Terrain is flat and clear here; this checks selection, not terrain pathing.
    auto actual=fixture("CMIN",-1);actual.cells.clear();actual.cell_order->clear();
    for(const auto& tile:terrain.tiles) {
        const unsigned k=tile.y*512u+tile.x;
        auto& c=actual.cells[k];c.allocated=true;c.heads=weapons::CellContentHeads{};
        c.ground_occupancy_124=0;c.cell_flag_100=false;
        c.terrain=sim::original_default_terrain_cell({tile.x,tile.y});
        c.terrain->overlay_type_index=terrain.overlay[k]==255?-1:int(terrain.overlay[k]);
        c.overlay_data_11e=terrain.overlay_data[k];actual.cell_order->push_back(k);
    }
    const int waypoint=map.read_integer("Waypoints","0",0);
    const ra2map::Cell start{std::uint16_t(waypoint%1000),std::uint16_t(waypoint/1000)};
    auto& miner=actual.entities[0];miner.cell=start;
    miner.position=sim::Coord{start.x*256+128,start.y*256+128,0};
    actual.cells.at(start.y*512u+start.x).heads->ground_head=1;
    actual.cells.at(start.y*512u+start.x).ground_occupancy_124=32;
    actual.entities[1].cell=ra2map::Cell{std::uint16_t(start.x-1),start.y};
    int nearest=1000000;
    for(const auto& [k,c]:actual.cells) if(c.terrain->overlay_type_index==ore || c.terrain->overlay_type_index==gem) {
        int dx=int(k%512)-start.x,dy=int(k/512)-start.y;
        nearest=std::min(nearest,dx*dx+dy*dy);
    }
    sim::PrototypeEconomy original_map(rules);
    for(unsigned frame=0;frame<3000 && !original_map.load(1);++frame) original_map.tick(actual,frame);
    assert(original_map.load(1)==1);
    const auto reached=*actual.entities[0].cell;
    const int dx=int(reached.x)-start.x,dy=int(reached.y)-start.y;
    assert(dx*dx+dy*dy==nearest);
    std::cout<<"MP02T2 start "<<start.x<<","<<start.y<<" first ore "<<reached.x<<","<<reached.y<<" squared distance "<<nearest<<'\n';
    const unsigned harvested_key=reached.y*512u+reached.x;
    const int packed_overlay=terrain.overlay[harvested_key];
    for(unsigned frame=3000;frame<3400 && actual.cells.at(harvested_key).terrain->overlay_type_index!=-1;++frame)
        original_map.tick(actual,frame);
    assert(actual.cells.at(harvested_key).terrain->overlay_type_index==-1);
    assert(terrain.overlay[harvested_key]==packed_overlay);
    std::cout<<"MP02T2 cell "<<reached.x<<","<<reached.y<<" packed overlay "<<packed_overlay
             <<" live overlay -1 after depletion; packed art input stays unchanged\n";
    // A reordered list proves that numeric INI keys do not define indexes.
    rules::IniFile reordered;
    reordered.parse("[OverlayTypes]\n88=GEM01\n77=OTHER\n44=TIB01\n[GEM01]\nTiberium=yes\n[TIB01]\nTiberium=yes\n[Tiberiums]\n4=Rich\n9=Poor\n[Rich]\nImage=2\nValue=70\n[Poor]\nImage=1\nValue=30\n");
    for(int overlay:{0,2}) {
        auto custom=fixture("CMIN",overlay);sim::PrototypeEconomy remapped(reordered);
        for(unsigned frame=0;frame<4000 && *custom.houses[0].cash==0;++frame) remapped.tick(custom,frame);
        assert(*custom.houses[0].cash==20*(overlay==0?70:30));
    }
    std::cout<<"prototype economy: passed real base rules, miners, gems, credits, growth, spread, duplicate frames, no refinery\n";
}
