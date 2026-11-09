#include "battle_radar.h"
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::render;
static RadarColor rgb(const Bitmap& b,int x,int y) {
    const auto i=(std::size_t(y)*b.width+x)*4;
    assert(b.rgba[i+3]==255);
    return {b.rgba[i],b.rgba[i+1],b.rgba[i+2]};
}
int main(int argc,char** argv) {
    fs::MixFileSystem files(argc>1?argv[1]:"/path/to/user/ra2/game");
    files.bootstrap(fs::GameVariant::RedAlert2);
    auto art=TheaterArt::load(files,"TEMPERATE");
    const auto data=files.read_file("rules.ini"); assert(data);
    rules::IniFile rules; rules.parse(std::string(data->begin(),data->end()));
    auto style=battle_radar_style(rules); assert(!style.resources.empty());
    bool distinct=false;
    for (const auto& a:style.resources) for(const auto& b:style.resources)
        distinct |= a.second!=b.second;
    assert(distinct); // Real base rules include ore and gems.
    session::world::Snapshot w;
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        session::world::CellState s; s.allocated=true;
        sim::TerrainCell t; t.cell={std::uint16_t(x),std::uint16_t(y)};
        t.tile_index=0; s.terrain=t; w.cells[y*512+x]=s;
    }
    RadarView v; v.map={{0,0,8,8},{0,0,8,8}}; v.width=32; v.height=32;
    auto b=draw_battle_radar(w,art,v,style,true);
    assert(b.width==32 && b.height==32 && b.rgba.size()==4096);
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        auto c=click_battle_radar(w,v,x,y,true); assert(c);
        const auto& m=art.metadata(0,0);
        const double center=(int(c->x)-int(c->y)+8)/2.;
        assert(rgb(b,x,y)==((x+.5)/4.<center?m.radar_left:m.radar_right));
    }
    assert(!click_battle_radar(w,v,-1,0,true));
    assert(!click_battle_radar(w,v,32,0,true));
    assert(!click_battle_radar(w,v,0,32,true));
    assert(!click_battle_radar(w,v,0,0,false));
    assert(draw_battle_radar(w,art,v,style,false).rgba.empty());
    const auto c=*click_battle_radar(w,v,16,16,true);
    auto& terrain=*w.cells[c.y*512u+c.x].terrain;
    for(const auto& r:style.resources) {
        terrain.overlay_type_index=r.first;
        b=draw_battle_radar(w,art,v,style,true); assert(rgb(b,16,16)==r.second);
    }
    terrain.overlay_type_index.reset();
    const auto clear_set=art.settings().read_integer("General","ClearTile",0);
    const auto& clear=art.resolved_metadata(art.tile_sets().at(clear_set).first,0);
    const double clear_center=(int(c.x)-int(c.y)+v.map.size.width)/2.;
    const auto expected=(v.map.local.x+16.5*v.map.local.width/v.width)<clear_center?
        clear.radar_left:clear.radar_right;
    for(const auto sentinel:{0xffffu,0xffffffffu}) {
        terrain.resolved_tile_index=sentinel; terrain.subtile=255;
        b=draw_battle_radar(w,art,v,style,true);
        assert(rgb(b,16,16)==expected);
        bool rejected=false;
        try { art.resolved_metadata(sentinel,0); }
        catch(const RenderError&) { rejected=true; }
        assert(rejected); // Only drawing resolves the clear marker.
    }
    terrain.subtile=0;
    terrain.resolved_tile_index=art.translate_map_index(0);
    style.houses[1]={250,20,40};
    session::world::Entity e; e.house_id=1; e.cell=ra2map::Cell{8,8};
    e.lifetime=session::world::Lifetime::Placed; w.entities.push_back(e);
    b=draw_battle_radar(w,art,v,style,true); assert(rgb(b,16,16)==style.houses[1]);
    w.entities[0].kind=session::world::Kind::Structure;
    b=draw_battle_radar(w,art,v,style,true); assert(rgb(b,15,15)==style.houses[1]);
    w.entities[0].lifetime=session::world::Lifetime::Destroyed;
    b=draw_battle_radar(w,art,v,style,true); assert(rgb(b,16,16)!=style.houses[1]);
    v.camera={2,2,4,4}; b=draw_battle_radar(w,art,v,style,true);
    assert((rgb(b,8,8)==RadarColor{255,255,255}));
    assert((rgb(b,23,23)==RadarColor{255,255,255}));
    w.cells.erase(c.y*512u+c.x); assert(!click_battle_radar(w,v,16,16,true));
    v.width=0; assert(!click_battle_radar(w,v,0,0,true));
    bool threw=false; try { draw_battle_radar(w,art,v,style,true); }
    catch(const RenderError&) { threw=true; } assert(threw);
    std::cout<<"battle radar tests passed (real base TMP and clear markers)\n";
}
