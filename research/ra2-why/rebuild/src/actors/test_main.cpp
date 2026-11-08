#include "actors.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace ra2;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    assert(actors::infantry_motion_sequence(actors::InfantryMotion::Standing,false,false)=="Ready");
    assert(actors::infantry_motion_sequence(actors::InfantryMotion::Walking,true,false)=="Crawl");
    assert(actors::infantry_motion_sequence(actors::InfantryMotion::Walking,false,true)=="Swim");
    assert(actors::infantry_direction(0)==7);
    assert(actors::infantry_direction(64)==5);
    assert(actors::infantry_direction(128)==3);
    assert(actors::infantry_direction(192)==1);
    assert(actors::projected_height(0)==0);
    assert(actors::projected_height(104)==15);
    assert(actors::projected_height(728)==105);
    assert(actors::projected_height(-104)==-14);
    auto infantry=actors::infantry_position({2,3},4,60);
    assert(infantry.x==704 && infantry.y==960 && infantry.z==60);
    auto draw_position=actors::building_draw_position({2,3},60);
    assert(draw_position.x==512 && draw_position.y==768 && draw_position.z==60);
    assert(actors::object_y_sort(draw_position)==1280);
    assert(actors::building_y_sort(draw_position,true,true)==1296);
    rules::IniFile sort_rules;
    sort_rules.parse("[GateType]\nImage=GateArt\nGate=yes\nTurretAnimIsVoxel=yes\n");
    assert(actors::building_y_sort(draw_position,sort_rules,"GateType")==1296);
    auto center=actors::building_center({2,3},3,2,60);
    assert(center.x==896 && center.y==1024 && center.z==60);
    auto screen=actors::project_world(center,15,{10,20});
    assert(screen.x==-25 && screen.y==77);
    auto projected=actors::project_world({896,1024,104},{10,20});
    assert(projected.x==-25 && projected.y==77);
    auto negative=actors::project_world({0,1,0},0,{0,0});
    assert(negative.x==0 && negative.y==0);
    fs::MixFileSystem files(argv[1]);
    files.bootstrap(); files.mount_overlays(); files.mount("CONQMD.MIX"); files.mount("CONQUER.MIX"); files.mount("GENERIC.MIX"); files.mount("GENERMD.MIX", false); files.mount("TEMPERAT.MIX"); files.mount("URBANN.MIX"); files.mount("ISOUBN.MIX"); files.mount("ISOGENMD.MIX", false); files.mount("ISOGEN.MIX", false); files.mount("ISOURBMD.MIX", false); files.mount("ISOURB.MIX", false); files.mount("ISOTEMP.MIX", false); files.mount("TEM.MIX", false);
    auto art_data=files.read_file("artmd.ini"); auto rules_data=files.read_file("rulesmd.ini");
    auto pal_data=files.read_file("UNITTEM.PAL"); assert(art_data && rules_data && pal_data);
    rules::IniFile art, rules;
    art.parse(std::string(art_data->begin(),art_data->end()));
    rules.parse(std::string(rules_data->begin(),rules_data->end()));
    auto pal=gfx::Palette::from_bytes(*pal_data);
    for (auto type : {"E1", "E2"}) {
        auto actor=actors::ActorArt::load(files,art,rules,type,actors::Kind::Infantry);
        assert(actor.remappable());
        assert(actor.frame("Ready",0,0)==0);
        assert(actor.frame_for_map("Walk",64,2)==actor.frame("Walk",5,2));
        assert(actor.frame_for_map("Walk",64,8)==actor.frame("Walk",5,2));
        auto standing=actor.draw("Ready",0,0,pal);
        assert(!standing.pixels.empty());
        auto origin=actor.main_canvas_origin({1000,1000});
        assert(origin.x==1000-standing.width/2 && origin.y==1000-standing.height/2);
        auto walk=actor.draw("Walk",3,2,pal); assert(!walk.pixels.empty());
        bool finished=false;
        assert(actor.frame_at_tick("Walk",3,6,finished)==actor.frame("Walk",3,2));
        assert(!finished);
        actor.frame_at_tick("Walk",3,18,finished); assert(finished);
        actor.frame_at_tick("Ready",3,1000,finished); assert(!finished);
        bool failed=false; try { actor.frame("Walk",8,0); } catch(const std::runtime_error&) { failed=true; } assert(failed);
        std::cout<<type<<' '<<actor.filename()<<" ready and walk passed\n";
    }
    ra2map::TerrainObject tree{}; tree.type="TREE01";
    auto terrain=actors::ActorArt::load(files,art,rules,tree);
    assert(terrain.filename()=="TREE01.TEM");
    assert(!terrain.draw("",0,0,pal).pixels.empty());
    auto effect=actors::ActorArt::load(files,art,rules,"S_BANG16",actors::Kind::Effect);
    assert(!effect.draw("",0,0,pal).pixels.empty());
    auto attached=actors::ActorArt::load_attached(files,art,rules,"GAPILE","ActiveAnim");
    assert(!attached.draw("",0,0,pal).pixels.empty());
    bool effect_finished=false;
    assert(attached.frame_at_tick("",0,0,effect_finished)==0 && !effect_finished);
    assert(attached.frame_at_tick("",0,42,effect_finished)==14 && !effect_finished);
    assert(attached.frame_at_tick("",0,45,effect_finished)==0 && !effect_finished);
    for (auto type : {"GAPILE", "NAHAND"}) {
        auto actor=actors::ActorArt::load(files,art,rules,type,actors::Kind::Building);
        auto frame=actor.draw("",0,0,pal); assert(!frame.pixels.empty());
        std::cout<<type<<' '<<actor.filename()<<" passed\n";
    }
}
