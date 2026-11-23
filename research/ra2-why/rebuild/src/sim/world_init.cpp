#include "world_init.h"
#include "cell_terrain_internal.h"
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
        if (first_pass) { cell.ramp_index=0; cell.land_type.reset(); cell.tile_replacement_resolved=false; }
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
        detail::apply_tile_terrain(cell,theater,rules,tile_count,cliff,neighbor);
    }
}
}
