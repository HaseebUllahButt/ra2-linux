#include "resource_render.h"
#include "../pack/map_pack.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ra2;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char** argv) try {
    check(argc==2,"Usage: resource_render_test game-dir");
    fs::MixFileSystem files(argv[1]);
    files.mount_language(fs::GameVariant::RedAlert2);files.bootstrap(fs::GameVariant::RedAlert2);
    files.mount_overlays();files.mount_resources();
    files.mount("TEMPERAT.MIX");files.mount("TEM.MIX",false);files.mount("ISOTEMP.MIX");
    auto ini=[&](const char* name) { auto bytes=files.read_file(name);check(bool(bytes),name);
        rules::IniFile result;result.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));return result; };
    const auto rules=ini("RULES.INI"),art=ini("ART.INI"),map=ini("MP02T2.MAP");
    const auto packed=pack::load_terrain(map);
    session::world::Snapshot world;
    unsigned ore=0,gems=0;
    const auto* types=rules.find_section("OverlayTypes");check(types,"Missing overlay types");
    for(const auto& tile:packed.tiles) {
        auto key=tile.y*512u+tile.x;auto index=packed.overlay.at(key);
        if(index>=types->entries.size()) continue;
        const auto& name=types->entries[index].value;
        if(!rules.read_bool(name,"Tiberium",false)) continue;
        ore+=name.compare(0,3,"TIB")==0;gems+=name.compare(0,3,"GEM")==0;
        auto& c=world.cells[key];c.terrain=sim::TerrainCell{};
        c.terrain->overlay_type_index=index;c.terrain->floor_world_height=tile.height*104;
        c.overlay_data_11e=packed.overlay_data.at(key);
    }
    check(ore && gems,"MP02T2 must contain ore and gems");
    app::ResourceRender renderer(files,rules,art,'T');std::vector<std::string> gaps;
    auto sprites=renderer.sprites(world,{0,0},gaps);
    for(const auto& gap:gaps) std::cerr<<gap<<'\n';
    check(gaps.empty() && sprites.size()==world.cells.size(),"Installed resources failed to draw");
    for(const auto& sprite:sprites) {
        bool opaque=false;for(std::size_t p=3;p<sprite.image->pixels.size();p+=4) opaque|=sprite.image->pixels[p]!=0;
        check(opaque,"Resource sprite is empty");
    }
    const auto shifted=renderer.sprites(world,{97,43},gaps);
    check(shifted.size()==sprites.size(),"Camera movement lost resources");
    for(std::size_t i=0;i<sprites.size();++i)
        check(shifted[i].origin.x==sprites[i].origin.x-97 &&
              shifted[i].origin.y==sprites[i].origin.y-43,"Resource projection lost camera offset");
    for(auto& [key,c]:world.cells) { (void)key;c.overlay_data_11e=0; }
    auto thin=renderer.sprites(world,{0,0},gaps);check(thin.size()==sprites.size(),"Last bail vanished early");
    check(std::any_of(thin.begin(),thin.end(),[&](const auto& item) {
        auto n=&item-thin.data();return item.image->pixels!=sprites[n].image->pixels;
    }),"Density did not change artwork");
    for(auto& [key,c]:world.cells) { (void)key;c.terrain->overlay_type_index=-1; }
    check(renderer.sprites(world,{0,0},gaps).empty(),"Depleted resources remain visible");
    std::cout<<"MP02T2: "<<ore<<" ore and "<<gems<<" gems draw; density and depletion pass\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
