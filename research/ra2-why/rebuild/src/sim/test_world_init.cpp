#include "world_init.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace ra2::sim;
namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
std::uint32_t digest(const TerrainCells& cells) {
    std::uint32_t hash=2166136261u;
    for (const auto& cell:cells) for (auto value:{*cell.resolved_tile_index,static_cast<std::uint32_t>(*cell.ramp_index),static_cast<std::uint32_t>(*cell.land_type),static_cast<std::uint32_t>(cell.subtile)})
        hash=(hash^value)*16777619u;
    return hash;
}
}
int main() try {
    const unsigned order_golden[][4]={{2,1,3,0x60f95c5b},{3,2,10,0xc43f5a1f},{80,102,16218,0xf5c43f0b},{105,110,22990,0x94212cff},{94,53,9911,0xbece1cd3}};
    for (const auto& test:order_golden) {
        const auto order=original_map_cell_order(test[0],test[1]); std::uint32_t hash=2166136261u;
        for (const auto cell:order) for (auto n:{cell.x,cell.y}) hash=(hash^n)*16777619u;
        require(order.size()==test[2] && hash==test[3],"original map iterator order differs");
    }
    bool rejected=false;
    try { (void)original_map_cell_order(std::numeric_limits<int>::max(),std::numeric_limits<int>::max()); }
    catch(const std::invalid_argument&) { rejected=true; }
    require(rejected,"map dimensions outside the recovered grid domain were accepted");
    TheaterInputs theater; theater.theater_name="TEMPERATE";
    const char* names[]={"RampBase","RampSmooth","RoughTile","SandTile","GreenTile","PaveTile","ClearToRoughLat","ClearToSandLat","ClearToGreenLat","ClearToPaveLat","MiscPaveTile","ShorePieces","PavedRoads","Medians","WaterBridge"};
    std::string settings="[General]\n";
    for (unsigned i=0;i<15;++i) settings+=std::string(names[i])+"="+std::to_string(i+1)+"\n";
    theater.settings.parse(settings);
    for (unsigned i=0;i<16;++i) theater.tile_sets.push_back({i*100,100});
    theater.tile_sets.push_back({1600,448});
    theater.resolved_metadata=[](std::uint32_t index,std::uint8_t subtile)->std::optional<TileMetadata> {
        if (index>=2048) return {};
        const auto raw=index==600 || (index>=1000 && index<=1015) || (index>=1100 && index<=1113)?12:index==300?14:0;
        const auto ramp=index>=100 && index<=119?index-99:index>=200 && index<=211?(index-200)/3+1:0;
        return TileMetadata{static_cast<std::uint8_t>(raw),static_cast<std::uint8_t>(ramp),subtile==0 && index!=504};
    };
    constexpr int coords[][2]={{1,3},{2,2},{3,1},{2,3},{3,2},{2,4},{3,3},{4,2},{3,4},{4,3}};
    const unsigned indexes[][10]={{100,100,100,100,100,100,100,100,100,100},{100,101,102,103,300,600,700,900,1100,0xffff},{600,4096,504,0xffff,600,600,600,600,600,600}};
    const unsigned subtiles[][10]={{},{},{2,5,0,7,0,0,0,0,0,0}};
    const unsigned heights[][10]={{},{0,0,0,0,0,0,4,0,0,0},{}};
    const std::uint32_t expected[][2]={{0x51b86d71,0x3ed322b2},{0x3f6d0fc3,0x22342bbc},{0xac56b3d0,0xac56b3d0}};
    for (unsigned fixture=0;fixture<3;++fixture) {
        TerrainCells cells;
        for (unsigned i=0;i<10;++i) {
            TerrainCell cell; cell.cell={static_cast<std::uint16_t>(coords[i][0]),static_cast<std::uint16_t>(coords[i][1])};
            cell.tile_index=indexes[fixture][i]; cell.resolved_tile_index=cell.tile_index;
            cell.subtile=subtiles[fixture][i]; cell.height_level=heights[fixture][i]; cell.overlay_type_index=-1;
            cells.push_back(cell);
        }
        initialize_original_tile_pass(cells,theater,3,2,2,true);
        if (digest(cells)!=expected[fixture][0]) std::cerr<<"fixture "<<fixture<<" phase 1 hash "<<std::hex<<digest(cells)<<std::dec<<"\n";
        if (digest(cells)!=expected[fixture][0]) for (unsigned i=0;i<cells.size();++i) std::cerr<<i<<" "<<*cells[i].resolved_tile_index<<" "<<*cells[i].ramp_index<<" "<<*cells[i].land_type<<" "<<int(cells[i].subtile)<<"\n";
        require(digest(cells)==expected[fixture][0],"original first tile pass differs");
        initialize_original_tile_pass(cells,theater,3,2,2,false);
        if (digest(cells)!=expected[fixture][1]) std::cerr<<"fixture "<<fixture<<" phase 2 hash "<<std::hex<<digest(cells)<<std::dec<<"\n";
        require(digest(cells)==expected[fixture][1],"original second tile pass differs");
    }
    std::cout<<"5 original map orders and 6 ordered tile-pass snapshots match\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
