#include "cell_setup.h"
#include "cell_terrain_internal.h"
#include <map>
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Missing { std::string detail; };
struct Unsupported { std::string detail; };
template<class T> T require(const std::optional<T>& value,const char* detail) {
    if (!value) throw Missing{detail};
    return *value;
}
std::int32_t signed_word(std::uint32_t value) {
    return value<=0x7fffffffu?static_cast<std::int32_t>(value):-1-static_cast<std::int32_t>(0xffffffffu-value);
}
}
CellSetupResult original_overlay_free_cell_setup(const CellSetupState& in,const CellSetupStatic& guard,
    const TheaterInputs& theater,std::uint8_t cliff,const CellSetupCallbacks& cb) {
    CellSetupResult out; out.state=in;
    try {
        auto state=in; auto& cell=state.terrain;
        if (!require(state.allocated,"single-Cell receiver allocation is unresolved"))
            throw Unsupported{"single-Cell fallback receiver is outside the allocated slice"};
        if (require(cell.overlay_type_index,"single-Cell overlay index is unresolved")!=-1)
            throw Unsupported{"single-Cell overlay setup is not recovered"};
        if (require(cell.ramp_index,"single-Cell current ramp is unresolved")!=0)
            throw Unsupported{"single-Cell nonflat current ramp is outside this slice"};
        const auto height=require(cell.height_level,"single-Cell height is unresolved");
        if (height< -128 || height>127) throw Unsupported{"single-Cell height exceeds the original signed byte"};
        if (!cell.resolved_tile_index) {
            if (cell.tile_index==0xffff) cell.resolved_tile_index=0xffff;
            else throw Missing{"single-Cell live tile index is unresolved"};
        }
        if (!theater.resolved_metadata) throw Missing{"single-Cell tile metadata callback is missing"};
        std::uint32_t tile_count=0;
        for (const auto& range:theater.tile_sets) {
            if (range.first!=tile_count || range.count>65535 || tile_count+range.count>65536)
                throw Unsupported{"single-Cell TileSet catalog is not contiguous"};
            tile_count+=range.count;
        }
        if (!tile_count) throw Missing{"single-Cell TileSet catalog is empty"};
        const auto rules=TileReplacementRules::from_theater(theater);
        std::map<std::uint32_t,CellSetupNeighbor> neighbors;
        const auto neighbor=[&](int x,int y)->const TerrainCell* {
            const auto offset=static_cast<std::int64_t>(y)*512+x;
            if (offset<0 || offset>0x3ffff) return nullptr;
            // Each lookup reads the current neighbor once. No neighbor is mutated.
            const auto index=static_cast<std::uint32_t>(offset);
            auto found=neighbors.find(index);
            if (found==neighbors.end()) {
                if (!cb.neighbor) throw Missing{"single-Cell neighbor callback is missing"};
                const ra2map::Cell coord{static_cast<std::uint16_t>(index%512),static_cast<std::uint16_t>(index/512)};
                found=neighbors.emplace(index,require(cb.neighbor(coord),"single-Cell neighbor is unresolved")).first;
            }
            return found->second.allocated?&found->second.terrain:nullptr;
        };
        const auto step=detail::apply_tile_terrain(cell,theater,rules,tile_count,cliff,neighbor);
        if (cell.ramp_index!=0) throw Unsupported{"single-Cell selected ramp is outside the flat slice"};
        if (step.effect_receiver) {
            if (require(guard.receiver_tile_index,"single-Cell TileType receiver is unresolved")!=*step.effect_receiver)
                throw Unsupported{"single-Cell effect guards use a different TileType receiver"};
            if (cell.land_type==10) throw Unsupported{"single-Cell Tube creation is not recovered"};
            const bool no_animation=state.animated_20000==true || guard.animation_index==-1 ||
                (guard.animation_subtile && *guard.animation_subtile!=cell.subtile);
            if (!no_animation) {
                if (!state.animated_20000 || !guard.animation_index || !guard.animation_subtile)
                    throw Missing{"single-Cell tile animation guard is unresolved"};
                throw Unsupported{"single-Cell tile animation creation is required"};
            }
            if (guard.shadow_enabled!=false && guard.shadow_list_empty!=true) {
                if (!guard.shadow_enabled || !guard.shadow_list_empty)
                    throw Missing{"single-Cell shadow footprint guard is unresolved"};
                throw Unsupported{"single-Cell shadow footprint writes are required"};
            }
            const auto bounds=require(guard.bounds_height,"single-Cell original tile bounds are unresolved");
            // Original signed division, followed by a byte store at Cell +11D.
            const auto delta=signed_word(static_cast<std::uint32_t>(bounds)-30u);
            state.field_11d=static_cast<std::uint8_t>(delta/15);
        }
        if (!cb.category) throw Missing{"single-Cell category profile callback is missing"};
        auto category=require(cb.category(cell),"single-Cell category profile is unresolved");
        category.overlay_index=-1; category.land_type=cell.land_type;
        CoarseCategoryResult calculated;
        try { calculated=original_coarse_category(category); }
        catch(const std::invalid_argument& error) { throw Missing{error.what()}; }
        out.category=std::move(calculated);
        out.cache_height=static_cast<std::uint8_t>(height);
        out.state=std::move(state); out.status=CellSetupStatus::Complete;
    } catch(const Missing& missing) { out.detail=missing.detail; }
    catch(const Unsupported& unsupported) { out.status=CellSetupStatus::Unsupported; out.detail=unsupported.detail; }
    catch(const std::invalid_argument& error) { out.status=CellSetupStatus::Unsupported; out.detail=error.what(); }
    catch(const std::runtime_error& error) { out.detail=error.what(); }
    return out;
}
}
