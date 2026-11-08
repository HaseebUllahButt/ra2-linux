#include "world_init.h"
#include "floor.h"
#include <map>
#include <stdexcept>
namespace ra2::sim {
namespace {
std::uint32_t key(int x,int y) { return static_cast<std::uint16_t>(x)|(static_cast<std::uint32_t>(static_cast<std::uint16_t>(y))<<16); }
std::string cell_name(const TerrainCell& c) { return std::to_string(c.cell.x)+","+std::to_string(c.cell.y); }
}
std::vector<ra2map::Cell> original_map_cell_order(int width,int height) {
    if (width<2 || height<1 || width>511 || height>510 || width+height>512) throw std::invalid_argument("original map iterator requires width >=2, height >=1, and width+height <=512");
    int x=1,y=width,left=width-1;
    std::vector<ra2map::Cell> out;
    // 00565C10 allocates this exact diamond. Iterator stops at its first null cell pointer.
    auto allocated=[&](){return x+y>width && x-y<width && y-x<width && x+y<=width+2*height;};
    while (allocated()) {
        out.push_back({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
        if (left) { ++x; --y; --left; }
        else {
            const auto old_x=x,old_y=y; x=old_y; y=old_x;
            if (((old_y-width-1+old_x)&1)==0) { ++x; left=width-2; }
            else { ++y; left=width-1; }
        }
    }
    if (out.size()!=static_cast<std::size_t>((2*width-1)*height)) throw std::logic_error("original map iterator shape mismatch");
    return out;
}
void initialize_original_tile_pass(TerrainCells& cells,const TheaterInputs& theater,int width,int height,std::uint8_t cliff,bool first_pass) {
    if (!theater.resolved_metadata) throw std::invalid_argument("original tile initialization needs a static metadata callback");
    const auto order=original_map_cell_order(width,height);
    std::map<std::uint32_t,TerrainCell*> by_cell;
    for (auto& cell:cells) {
        if (!by_cell.emplace(key(cell.cell.x,cell.cell.y),&cell).second) throw std::invalid_argument("duplicate original terrain cell");
        if (first_pass) { cell.ramp_index=0; cell.land_type.reset(); cell.bridge.reset(); cell.tile_replacement_resolved=false; }
        if (!cell.resolved_tile_index) {
            if (cell.tile_index==0xffff) cell.resolved_tile_index=0xffff;
            else throw std::invalid_argument("missing live tile index at "+cell_name(cell));
        }
        if (!cell.height_level || *cell.height_level< -128 || *cell.height_level>127)
            throw std::invalid_argument("missing or invalid signed Cell height at "+cell_name(cell));
        if (!cell.overlay_type_index) throw std::invalid_argument("missing original overlay index at "+cell_name(cell));
    }
    if (cells.size()!=order.size()) throw std::invalid_argument("original terrain inputs do not cover the allocated map diamond");
    std::uint32_t tile_count=0;
    for (const auto& range:theater.tile_sets) {
        if (range.first!=tile_count || range.count>65535 || tile_count+range.count>65536)
            throw std::invalid_argument("original TileSet catalog is not contiguous");
        tile_count+=range.count;
    }
    if (!tile_count) throw std::invalid_argument("original TileSet catalog is empty");
    const auto rules=TileReplacementRules::from_theater(theater);
    constexpr int cardinal[][2]={{0,-1},{1,0},{0,1},{-1,0}};
    constexpr int cliff_neighbors[][2]={{0,-1},{-1,0},{2,2},{1,1},{-1,1},{1,-1}};
    auto neighbor=[&](int x,int y)->TerrainCell* {
        // 005657A0 uses the signed linear grid offset, rather than separate X/Y checks.
        const auto offset=static_cast<std::int64_t>(y)*512+x;
        if (offset<0 || offset>0x3ffff) return nullptr;
        const auto found=by_cell.find(key(static_cast<int>(offset%512),static_cast<int>(offset/512)));
        return found==by_cell.end()?nullptr:found->second;
    };
    for (const auto coord:order) {
        const auto found=by_cell.find(key(coord.x,coord.y));
        if (found==by_cell.end()) throw std::invalid_argument("missing cell in original map iterator order");
        auto& cell=*found->second;
        if (*cell.resolved_tile_index>65535) throw std::invalid_argument("live tile index exceeds the recovered Cell domain");
        if (*cell.resolved_tile_index>=tile_count) cell.resolved_tile_index=0xffff; // 0047D2B0 bounds check.
        const bool no_overlay=*cell.overlay_type_index==-1;
        bool clear=*cell.resolved_tile_index==0xffff;
        if (!clear) {
            auto source=theater.resolved_metadata(*cell.resolved_tile_index,cell.subtile);
            if (!source) throw std::runtime_error("missing original tile metadata at "+cell_name(cell));
            if (!source->valid_subtile) {
                if (!no_overlay) throw std::runtime_error("0047D2B0 overlay-specific invalid subtile branch is not recovered at "+cell_name(cell));
                cell.resolved_tile_index=0xffff; cell.subtile=0; clear=true;
            } else {
                cell.ramp_index=source->ramp;
                TileReplacementInput input; input.index=*cell.resolved_tile_index; input.ramp=source->ramp;
                for (unsigned d=0;d<4;++d) {
                    const auto* other=neighbor(coord.x+cardinal[d][0],coord.y+cardinal[d][1]);
                    // Map lookup returns the default Cell outside the allocated diamond: 0047BBF0.
                    if (!other) input.neighbors[d]=TileNeighbor{0xffff,0};
                    else if (other->resolved_tile_index && other->ramp_index)
                        input.neighbors[d]=TileNeighbor{*other->resolved_tile_index,static_cast<std::uint8_t>(*other->ramp_index)};
                }
                const auto decision=original_tile_replacement(input,rules);
                if (!decision.index) throw std::runtime_error(decision.detail+" at "+cell_name(cell));
                cell.resolved_tile_index=*decision.index;
                const auto selected=theater.resolved_metadata(*decision.index,cell.subtile);
                if (!selected) throw std::runtime_error("missing original replacement metadata at "+cell_name(cell));
                // 0047D8A3 retains the old TileType receiver after 0047CA80 changes Cell +38.
                // The changed tile supplies terrain only when the next setup pass reads it.
                cell.tmp_terrain=source->terrain;
                cell.land_type=no_overlay?std::optional<std::int32_t>(original_tmp_land_type(source->terrain)):std::nullopt;
            }
        }
        if (clear) { cell.ramp_index=0; cell.tmp_terrain.reset(); cell.land_type=no_overlay?std::optional<std::int32_t>(0):std::nullopt; }
        if (cell.land_type && cliff==2 && (*cell.land_type==0 || *cell.land_type==2 || *cell.land_type==6 || *cell.land_type==8)) {
            for (const auto& delta:cliff_neighbors) {
                const auto* other=neighbor(coord.x+delta[0],coord.y+delta[1]);
                const int level=other?*other->height_level:0;
                if (level>=*cell.height_level+4) { cell.land_type=3; break; }
            }
        }
        if (cell.ramp_index && *cell.ramp_index>=0 && *cell.ramp_index<=20)
            cell.floor_world_height=original_floor_height(static_cast<std::int8_t>(*cell.height_level),static_cast<std::uint8_t>(*cell.ramp_index),coord.x*256+128,coord.y*256+128);
        else throw std::runtime_error("original ramp plane is outside 0..20 at "+cell_name(cell));
        cell.tile_replacement_resolved=true;
        // The supported non-overlay flat clear/road slice keeps the Cell constructor bridge bit clear.
        // Tunnel links, bridge overlays, animations, and shadow objects are separate unrecovered effects.
        if (no_overlay && cell.land_type && (*cell.land_type==0 || *cell.land_type==1) && *cell.ramp_index==0) cell.bridge=false;
        else cell.bridge.reset();
    }
}
}
