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
    // Base rules.ini has no HarvesterLoadRate, OreDumpRate or HarvestRate.
    // 00640B80 defaults HarvesterLoadRate to 2. 007025A0 calls 007013A0 when
    // Animation.Value reaches 9, so one bail takes 9 * 2 = 18 frames.
    assert(!rules.find("General","HarvesterLoadRate") && !rules.find("General","OreDumpRate") && !rules.find("HARV","HarvestRate"));
    for(auto type:{"CMIN","HARV"}) {
        auto w=fixture(type,-1);auto& c=w.cells[4*512+3];c.terrain->overlay_type_index=ore;c.overlay_data_11e=11;
        sim::PrototypeEconomy economy(rules);std::vector<unsigned> bails;
        for(unsigned frame=0;frame<100;++frame) {
            const int before=economy.load(1);economy.tick(w,frame);
            if(economy.load(1)!=before) bails.push_back(frame);
        }
        assert(bails.size()>=3 && bails[0]==18 && bails[1]==36 && bails[2]==54);
    }
    // 00701580: the docked miner gives the whole first non-empty resource
    // after Animation.Value reaches HarvesterDumpRate * 900 frames, then
    // resets the count for the next resource. OreDumpRate is not a key here.
    {
        rules::IniFile timed;
        timed.parse("[General]\nHarvesterLoadRate=3\nHarvesterDumpRate=.025\nOreDumpRate=1\n"
                    "[HARV]\nStorage=2\nHarvestRate=1\n"
                    "[OverlayTypes]\n0=GEM01\n1=TIB01\n[GEM01]\nTiberium=yes\n[TIB01]\nTiberium=yes\n"
                    "[Tiberiums]\n0=Poor\n1=Rich\n[Poor]\nImage=1\nValue=30\n[Rich]\nImage=2\nValue=70\n");
        auto w=fixture("HARV",-1);
        auto& here=w.cells[4*512+3];here.terrain->overlay_type_index=1;here.overlay_data_11e=0;
        auto& gem_cell=w.cells[4*512+5];gem_cell.terrain->overlay_type_index=0;gem_cell.overlay_data_11e=11;
        sim::PrototypeEconomy economy(timed);
        std::vector<std::pair<unsigned,int>> bails,cash;
        for(unsigned frame=0;frame<2000 && cash.size()<2;++frame) {
            const int load=economy.load(1),money=*w.houses[0].cash;
            economy.tick(w,frame);
            if(economy.load(1)>load) bails.push_back({frame,economy.load(1)});
            if(*w.houses[0].cash!=money) cash.push_back({frame,*w.houses[0].cash});
        }
        assert(bails.size()==2 && bails[0].first==27);
        assert(cash.size()==2 && cash[0].second==30 && cash[1].second==100);
        assert(cash[1].first-cash[0].first==23 && cash[0].first>=bails[1].first+27+23);
    }
    // Purifier share, base 00701580: each Ore Purifier adds PurifierBonus of
    // the load, a computer house adds its virtual purifiers, and low power
    // removes only the real ones.
    {
        rules::IniFile bonus;
        bonus.parse("[General]\nHarvesterLoadRate=1\nHarvesterDumpRate=.01\nCloseEnough=2.25\nBlockagePathDelay=60\n"
                    "PurifierBonus=.25\nAIVirtualPurifiers=4,2,0\n"
                    "[HARV]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=4\nStorage=2\n[GAOREP]\nOrePurifier=yes\n"
                    "[OverlayTypes]\n0=TIB01\n[TIB01]\nTiberium=yes\n[Tiberiums]\n0=Poor\n[Poor]\nImage=1\nValue=30\n");
        assert(sim::PrototypeEconomy::virtual_purifiers(bonus,0)==4 && sim::PrototypeEconomy::virtual_purifiers(bonus,1)==2 &&
               sim::PrototypeEconomy::virtual_purifiers(bonus,2)==0 && sim::PrototypeEconomy::virtual_purifiers(bonus,3)==0 &&
               sim::PrototypeEconomy::virtual_purifiers(bonus,-1)==0);
        const auto first_unload=[&](int virtual_count,int real,bool low_power) {
            auto w=fixture("HARV",0);
            for(int i=0;i<real;++i) {
                sw::Entity purifier;purifier.id=50+std::uint32_t(i);purifier.house_id=1;purifier.type="GAOREP";
                purifier.kind=sw::Kind::Structure;purifier.lifetime=sw::Lifetime::Placed;purifier.resolved_hp=900;
                purifier.cell=ra2map::Cell{12,std::uint16_t(8-i)};purifier.position=sim::Coord{12*256+128,(8-i)*256+128,0};
                purifier.layer=sw::Layer::Ground;purifier.next=0;
                w.entities.push_back(purifier);
            }
            if(low_power) { w.houses[0].power_output=10;w.houses[0].power_drain=20; }
            sim::PrototypeEconomy economy(bonus);
            if(virtual_count) economy.set_virtual_purifiers(1,virtual_count);
            for(unsigned frame=0;frame<3000 && *w.houses[0].cash==0;++frame) economy.tick(w,frame);
            return *w.houses[0].cash;
        };
        assert(first_unload(0,0,false)==60);        // Two bails of 30.
        assert(first_unload(0,1,false)==75);        // One purifier: +25%.
        assert(first_unload(4,0,false)==120);       // Hardest computer: +100%.
        assert(first_unload(2,1,false)==105);       // Medium computer with one purifier: +75%.
        assert(first_unload(2,1,true)==90);         // Low power: the building does not count.
    }
    // Guards stand on every Cell beside the refinery when the full miner
    // comes back. The miner heads for a Cell that friendly soldiers hold,
    // they step aside, and it unloads. The old code waited for an empty
    // Cell: the full miner never moved and the House got no credits
    // (MP02T2, Medium computer, HARV at 87,123 from frame 10039).
    {
        rules::IniFile guarded;
        guarded.parse("[General]\nHarvesterLoadRate=1\nHarvesterDumpRate=.01\nCloseEnough=2.25\nBlockagePathDelay=60\n"
                      "[HARV]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=4\nStorage=2\n[E2]\nSpeed=4\n"
                      "[OverlayTypes]\n0=TIB01\n[TIB01]\nTiberium=yes\n[Tiberiums]\n0=Poor\n[Poor]\nImage=1\nValue=30\n");
        auto w=fixture("HARV",0);
        auto& miner=w.entities[0];
        w.cells[4*512+3].heads->ground_head=0;w.cells[4*512+3].ground_occupancy_124=0;
        miner.cell=ra2map::Cell{6,4};miner.position=sim::Coord{6*256+128,4*256+128,0};
        w.cells[4*512+6].heads->ground_head=1;w.cells[4*512+6].ground_occupancy_124=32;
        std::uint32_t id=10;
        for(int y=3;y<=5;++y) for(int x=1;x<=3;++x) if(x!=2 || y!=4) {
            sw::Entity guard;guard.id=id++;guard.house_id=1;guard.type="E2";guard.kind=sw::Kind::Infantry;
            guard.lifetime=sw::Lifetime::Placed;guard.resolved_hp=125;guard.layer=sw::Layer::Ground;guard.next=0;
            guard.cell=ra2map::Cell{std::uint16_t(x),std::uint16_t(y)};guard.position=sim::Coord{x*256+64,y*256+64,0};
            w.entities.push_back(guard);
            w.cells[unsigned(y)*512+unsigned(x)].heads->ground_head=guard.id;w.cells[unsigned(y)*512+unsigned(x)].ground_occupancy_124=4;
        }
        sim::PrototypeEconomy economy(guarded);
        for(unsigned frame=0;frame<3000 && *w.houses[0].cash==0;++frame) economy.tick(w,frame);
        assert(*w.houses[0].cash==2*30);
        const int dx=int(w.entities[0].cell->x)-2,dy=int(w.entities[0].cell->y)-4;
        assert(dx*dx+dy*dy<=2);
        for(const auto& e:w.entities) assert(e.resolved_hp.value_or(0)>0 && e.lifetime==sw::Lifetime::Placed);
    }
    // Blocked dock regressions for a full CMIN. The Chrono Miner cannot
    // drive through the guards, so it warps to a dock Cell that only live
    // friendly Infantry of its House hold, they step aside, and it unloads.
    // The old fallback never fired: pass 1 skipped every CMIN and the
    // soldier chain check rejected the end of the chain, so the full CMIN
    // stayed stuck with a full cargo.
    {
        rules::IniFile guarded;
        guarded.parse("[General]\nHarvesterLoadRate=1\nHarvesterDumpRate=.01\nCloseEnough=2.25\nBlockagePathDelay=60\n"
                      "[CMIN]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=4\nStorage=2\n[E2]\nSpeed=4\n[1TNK]\nSpeed=4\n"
                      "[OverlayTypes]\n0=TIB01\n[TIB01]\nTiberium=yes\n[Tiberiums]\n0=Poor\n[Poor]\nImage=1\nValue=30\n");
        // kind 0 enemy Infantry, 1 friendly vehicle, 2 building,
        // 3 friendly Infantry on an impassable Cell.
        const auto blocked=[&](int kind) {
            auto w=fixture("CMIN",0);
            auto& miner=w.entities[0];
            w.cells[4*512+3].heads->ground_head=0;w.cells[4*512+3].ground_occupancy_124=0;
            miner.cell=ra2map::Cell{6,4};miner.position=sim::Coord{6*256+128,4*256+128,0};
            w.cells[4*512+6].heads->ground_head=1;w.cells[4*512+6].ground_occupancy_124=32;
            std::uint32_t id=10;
            for(int y=3;y<=5;++y) for(int x=1;x<=3;++x) if(x!=2 || y!=4) {
                sw::Entity blocker;blocker.id=id++;blocker.house_id=kind==0?2u:1u;
                blocker.type=kind==1?"1TNK":kind==2?"GAWALL":"E2";
                blocker.kind=kind==1?sw::Kind::Unit:kind==2?sw::Kind::Structure:sw::Kind::Infantry;
                blocker.lifetime=sw::Lifetime::Placed;blocker.resolved_hp=125;blocker.layer=sw::Layer::Ground;blocker.next=0;
                blocker.cell=ra2map::Cell{std::uint16_t(x),std::uint16_t(y)};blocker.position=sim::Coord{x*256+64,y*256+64,0};
                w.entities.push_back(blocker);
                auto& c=w.cells[unsigned(y)*512+unsigned(x)];
                c.heads->ground_head=blocker.id;c.ground_occupancy_124=(kind==1||kind==2)?32u:4u;
                if(kind==3) c.unit_land_speed=0.f;
            }
            return w;
        };
        // No warp onto an enemy soldier, a vehicle, a building, or an
        // impassable Cell: the miner stays full and every blocker keeps its
        // Cell, chain head and occupancy bits.
        for(int kind:{0,1,2,3}) {
            auto w=blocked(kind);
            sim::PrototypeEconomy economy(guarded);
            for(unsigned frame=0;frame<3000;++frame) economy.tick(w,frame);
            assert(*w.houses[0].cash==0);
            assert(w.entities[0].cell && w.entities[0].cell->x==7 && w.entities[0].cell->y==4);
            std::uint32_t id=10;
            for(int y=3;y<=5;++y) for(int x=1;x<=3;++x) if(x!=2 || y!=4) {
                const auto& blocker=w.entities[id-10+2];const auto& c=w.cells[unsigned(y)*512+unsigned(x)];
                assert(blocker.id==id++ && blocker.resolved_hp.value_or(0)>0 && blocker.lifetime==sw::Lifetime::Placed);
                assert(c.heads->ground_head==blocker.id);
                assert(*c.ground_occupancy_124==((kind==1||kind==2)?32u:4u));
            }
        }
        // A full CMIN warps to a dock Cell held by friendly soldiers. The
        // soldier chain stays intact: the miner joins at the tail, the old
        // Cell keeps its remaining occupants and bits, and the House gets
        // the credits.
        auto w=blocked(-1);
        sim::PrototypeEconomy economy(guarded);
        for(unsigned frame=0;frame<3000 && *w.houses[0].cash==0;++frame) economy.tick(w,frame);
        assert(*w.houses[0].cash==2*30);
        assert(w.entities[0].cell && w.entities[0].cell->x==1 && w.entities[0].cell->y==3);
        const auto& dock=w.cells[3*512+1];
        assert(dock.heads->ground_head==10);
        const auto& guard=w.entities[2];
        assert(guard.id==10 && guard.next && *guard.next==1);
        assert(!w.entities[0].next || !*w.entities[0].next);
        assert(*dock.ground_occupancy_124==(4u|0x20u));
        const auto& vacated=w.cells[4*512+7];
        assert(vacated.heads->ground_head==0 && *vacated.ground_occupancy_124==0);
        for(const auto& e:w.entities) if(e.kind==sw::Kind::Infantry) assert(e.resolved_hp.value_or(0)>0 && e.lifetime==sw::Lifetime::Placed);
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
    sim::PrototypeEconomy depletion(rules);for(unsigned f=0;f<18;++f) depletion.tick(last_bail,f);
    assert(depletion.load(1)==0 && last_cell.overlay_data_11e==0);depletion.tick(last_bail,18);
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
    // The edited growth rules above would grow this cell during the load wait.
    rules::IniFile base_rules;base_rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
    for(int overlay:{ore,gem}) {
        auto k=std::size_t(std::find(terrain.overlay.begin(),terrain.overlay.end(),overlay)-terrain.overlay.begin());
        auto actual=fixture("CMIN",-1);auto& cell=actual.cells[4*512+3];
        cell.terrain->overlay_type_index=terrain.overlay[k];cell.overlay_data_11e=terrain.overlay_data[k];
        sim::PrototypeEconomy from_map(base_rules);for(unsigned f=0;f<=18;++f) from_map.tick(actual,f);assert(from_map.load(1)==1);
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
