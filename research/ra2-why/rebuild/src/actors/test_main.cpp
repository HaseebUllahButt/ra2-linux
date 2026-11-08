#include "actors.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace ra2;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
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
        auto standing=actor.draw("Ready",0,0,pal);
        assert(!standing.pixels.empty());
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
    for (auto type : {"GAPILE", "NAHAND"}) {
        auto actor=actors::ActorArt::load(files,art,rules,type,actors::Kind::Building);
        auto frame=actor.draw("",0,0,pal); assert(!frame.pixels.empty());
        std::cout<<type<<' '<<actor.filename()<<" passed\n";
    }
}
