#include "unit_visibility.h"
#include <algorithm>
#include <exception>
namespace ra2::sim {
namespace {
std::optional<std::uint32_t> projected_flags(const session::world::Snapshot& world,int x,int y,const UnitVisibilityInputs& in) {
    if (!in.allocated || in.allocated->size()!=512u*512u || x<0 || y<0 || x>=512 || y>=512) return {};
    const auto index=std::uint32_t(y)*512+std::uint32_t(x);
    if (!(*in.allocated)[index]) return {};
    const auto found=world.cells.find(index);
    if (found==world.cells.end() || !found->second.allocated || !*found->second.allocated) return {};
    return found->second.cell_flags_140;
}
}
std::optional<bool> original_secondary_unit_visibility(const session::world::Snapshot& world,Coord point,const UnitVisibilityInputs& in) {
    if (!in.global_mode || !in.projection_divisor || *in.projection_divisor<=0 || point.x<0 || point.y<0 || point.z<0) return {};
    if (*in.global_mode) {
        if (!in.local_house_flag_13d) return {};
        if (*in.local_house_flag_13d) {
            if (!in.global_flag_a3d548) return {};
            if (*in.global_flag_a3d548) return false;
        }
    }
    const int height=point.z / *in.projection_divisor;
    const int offset=height/2+(height&1);
    int x=point.x/256-offset,y=point.y/256-offset;
    auto flags=projected_flags(world,x,y,in);
    if (!flags) return {};
    if (*flags&0x2u) return false;
    if (height&1) {
        flags=projected_flags(world,++x,++y,in);
        if (!flags) return {};
        if (*flags&0x2u) return false;
    }
    return true;
}
UnitVisibilityResult original_unit_insertion_visibility(session::world::Snapshot& world,std::uint32_t id,
    std::uint32_t key,const UnitVisibilityInputs& in) {
    UnitVisibilityResult result;
    if (!in.projection_divisor || !in.global_mode || !in.allocated || in.allocated->size()!=512u*512u ||
        key>=in.allocated->size() || !(*in.allocated)[key]) { result.detail="Cell visibility inputs are unresolved";return result; }
    if (*in.projection_divisor<=0) { result.status=UnitVisibilityStatus::Unsupported;result.detail="visibility projection divisor is outside the recovered domain";return result; }
    const auto target=world.cells.find(key);
    if (target==world.cells.end() || !target->second.allocated || !*target->second.allocated || !target->second.terrain ||
        !target->second.terrain->floor_world_height || !target->second.terrain->ramp_index) { result.detail="visibility receiver terrain is unresolved";return result; }
    const auto& terrain=*target->second.terrain;
    if (*terrain.ramp_index!=0 || *terrain.floor_world_height<0) { result.status=UnitVisibilityStatus::Unsupported;result.detail="visibility needs a nonnegative flat Cell";return result; }
    const auto found_unit=std::find_if(world.entities.begin(),world.entities.end(),[&](const auto& e){return e.id==id;});
    if (found_unit==world.entities.end() || found_unit->kind!=session::world::Kind::Unit ||
        found_unit->lifetime!=session::world::Lifetime::Placed || !found_unit->cell ||
        found_unit->cell->x!=key%512 || found_unit->cell->y!=key/512) {
        result.status=UnitVisibilityStatus::Unsupported;result.detail="visibility needs a Unit on the inserted Cell";return result;
    }
    const int height=*terrain.floor_world_height / *in.projection_divisor;
    const int offset=height/2+(height&1);
    int x=int(key%512)-offset,y=int(key/512)-offset;
    const auto flags=[&](int px,int py)->std::optional<std::uint8_t> {
        if (px<0 || py<0 || px>=512 || py>=512) return {};
        const auto index=std::uint32_t(py)*512+std::uint32_t(px);
        if (!(*in.allocated)[index]) return {};
        const auto it=world.cells.find(index);
        if (it==world.cells.end() || !it->second.allocated || !*it->second.allocated) return {};
        return it->second.cell_field_12c;
    };
    auto first=flags(x,y);
    if (!first) { result.detail="visibility projected Cell or byte +12C is unresolved";return result; }
    bool visible=(*first&8u)==0;
    if ((height&1) && visible) {
        first=flags(++x,++y);
        if (!first) { result.detail="visibility projected neighbor or byte +12C is unresolved";return result; }
        visible=(*first&8u)==0;
    }
    result.first_visibility=visible;
    auto staged=world;
    try {
        bool can_skip=visible;
        if (!visible) {
            const Coord center{int(key%512)*256+128,int(key/512)*256+128,*terrain.floor_world_height};
            const auto second=in.secondary?in.secondary(staged,center):original_secondary_unit_visibility(staged,center,in);
            if (!second) { result.detail="secondary Cell visibility is unresolved";return result; }
            can_skip=*second;
        }
        if (!(can_skip && !*in.global_mode)) {
            if (!in.seen) { result.detail="Unit visibility virtual is unresolved";return result; }
            const auto done=in.seen(staged,id);
            if (!done) { result.detail="Unit visibility virtual is unresolved";return result; }
            result.seen_called=true; // Original ignores the virtual's return value.
        }
        if (!session::world::validate(staged)) { result.detail="visibility callback produced invalid Snapshot state";return result; }
        world=std::move(staged);result.status=UnitVisibilityStatus::Complete;return result;
    } catch(const std::exception& e) { result.status=UnitVisibilityStatus::Unsupported;result.detail=e.what();return result; }
}
}
