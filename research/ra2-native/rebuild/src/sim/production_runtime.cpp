#include "production_runtime.h"
#include <cstdlib>

namespace ra2::sim::production {
namespace {
std::int32_t counter(const std::map<std::int32_t,std::int32_t>& values,std::int32_t index) {
    // 00491D10 grows the vector with zeroes before reading a missing index.
    const auto it=values.find(index); return it==values.end()?0:it->second;
}
// 0x1 << (country & 31), as x86 SHL uses CL modulo 32.
std::uint32_t country_bit(std::int32_t country) { return 1u<<(static_cast<std::uint32_t>(country)&31u); }
}

CanBuild original_can_build(const CanBuildType& type,const CanBuildHouse& house,const CanBuildRules& rules,
                            bool build_limit_only,bool include_queued) {
    if (!build_limit_only) {
        if (type.constructor_flag_aa4) return CanBuild::Cannot;                 // 004E367B.
        bool overridden=false;
        for (const auto index:type.prerequisite_override) {                    // 004E36B6.
            if (index<0) return CanBuild::Unsupported;                          // 00491D10 would read before its array.
            if (!house.active_buildings) return CanBuild::NeedsInput;
            if (counter(*house.active_buildings,index)>0) { overridden=true; break; }
        }
        if (!overridden) {
            if (type.tech_level==-1) return CanBuild::Cannot;                   // 004E36D2.
            if (!house.tech_level) return CanBuild::NeedsInput;
            if (type.requires_stolen_allied) {                                  // 004E3722.
                if (!house.stolen_allied) return CanBuild::NeedsInput;
                if (!*house.stolen_allied) return CanBuild::Cannot;
            }
            if (type.requires_stolen_soviet) {                                  // 004E3768.
                if (!house.stolen_soviet) return CanBuild::NeedsInput;
                if (!*house.stolen_soviet) return CanBuild::Cannot;
            }
            if (type.required_houses!=0xFFFFFFFFu) {                            // 004E377C.
                if (!house.country) return CanBuild::NeedsInput;
                if (!(type.required_houses&country_bit(*house.country))) {
                    if (!house.stolen_masks) return CanBuild::NeedsInput;
                    const auto& m=*house.stolen_masks;
                    const auto r=type.required_houses;
                    const bool stolen=((m[0]&r) && type.rtti==Rtti::InfantryType) ||
                        ((m[1]&r) && type.rtti==Rtti::UnitType) ||
                        ((m[2]&r) && type.rtti==Rtti::AircraftType) ||
                        ((m[3]&r) && type.rtti==Rtti::BuildingType);
                    if (!stolen) return CanBuild::Cannot;
                }
            }
            if (type.forbidden_houses!=0xFFFFFFFFu) {                           // 004E380F.
                if (!house.country) return CanBuild::NeedsInput;
                if (type.forbidden_houses&country_bit(*house.country)) return CanBuild::Cannot;
            }
            if (!rules.super_weapons_allowed) return CanBuild::NeedsInput;      // 004E3832.
            if (!*rules.super_weapons_allowed && type.rtti==Rtti::BuildingType && type.super_weapon!=-1) {
                const auto it=house.super_weapon_disabled.find(type.super_weapon);
                if (it==house.super_weapon_disabled.end()) return CanBuild::NeedsInput;
                if (it->second) return CanBuild::Cannot;
            }
            if (type.tech_level>*house.tech_level) return CanBuild::Cannot;    // 004E386C.
            if (!rules.session_type || !house.flag_134) return CanBuild::NeedsInput;
            bool human=*house.flag_134;                                         // 004E3878.
            if (*rules.session_type==0 && !human) {
                if (!house.flag_135) return CanBuild::NeedsInput;
                human=*house.flag_135;
            }
            // A computer house skips prerequisites and the build limit.
            if (!human) return CanBuild::Can;
            for (const auto requirement:type.prerequisite) {                     // 004E390C.
                if (requirement>=-6 && requirement<=-1) {
                    if (!rules.generic_prerequisites) return CanBuild::NeedsInput;
                    const auto& group=(*rules.generic_prerequisites)[static_cast<unsigned>(-requirement-1)];
                    if (group.empty()) return CanBuild::Cannot;
                    bool found=false;
                    for (const auto index:group) {
                        if (index<0) return CanBuild::Unsupported;
                        if (!house.active_buildings) return CanBuild::NeedsInput;
                        if (counter(*house.active_buildings,index)>=1) { found=true; break; }
                    }
                    if (!found) return CanBuild::Cannot;
                } else {
                    if (requirement<0) return CanBuild::Unsupported;            // Registry read before its start.
                    const auto upgrade=rules.powers_up_building.find(requirement);
                    if (upgrade==rules.powers_up_building.end()) return CanBuild::NeedsInput;
                    if (!upgrade->second) {                                     // 004E3B66.
                        if (!house.active_buildings) return CanBuild::NeedsInput;
                        if (counter(*house.active_buildings,requirement)==0) return CanBuild::Cannot;
                        continue;
                    }
                    // 004E3B05: only the last eligible building is inspected.
                    if (!house.buildings) return CanBuild::NeedsInput;
                    const HouseBuilding* eligible=nullptr;
                    for (auto it=house.buildings->rbegin();it!=house.buildings->rend();++it)
                        if (!it->limbo && it->flag_530 && it->mission!=0x13 && it->mission_a0!=0x13) { eligible=&*it; break; }
                    if (!eligible) return CanBuild::Cannot;
                    bool has=false;
                    for (const auto& slot:eligible->upgrades) if (slot && *slot==requirement) has=true;
                    if (!has) return CanBuild::Cannot;
                }
            }
        }
    }
    // 004E3C5A: build limits by RTTI.
    const std::optional<std::map<std::int32_t,std::int32_t>>* built=nullptr;
    const std::optional<std::map<std::int32_t,std::int32_t>>* owned=nullptr;
    switch (type.rtti) {
    case Rtti::UnitType: built=&house.built_units; owned=&house.owned_units; break;
    case Rtti::BuildingType: built=&house.built_buildings; owned=&house.owned_buildings; break;
    case Rtti::AircraftType: built=&house.built_aircraft; owned=&house.owned_aircraft; break;
    case Rtti::InfantryType: built=&house.built_infantry; owned=&house.owned_infantry; break;
    default: return CanBuild::Can;
    }
    const std::int64_t limit=type.build_limit;
    if (limit<=0) {
        if (!*built) return CanBuild::NeedsInput;
        // CDQ/XOR/SUB absolute value; INT_MIN stays negative.
        const std::int32_t magnitude=limit==INT32_MIN?INT32_MIN:static_cast<std::int32_t>(std::llabs(limit));
        if (!(counter(**built,type.heap_index)<magnitude)) return CanBuild::Cannot;
    }
    std::int32_t count=0;
    if (type.rtti==Rtti::InfantryType) {
        if (!*owned) return CanBuild::NeedsInput;
        count=counter(**owned,type.heap_index);                                 // 004E3D8D.
        if (type.infantry_flag_c14) {
            if (!house.infantry_c14_matches) return CanBuild::NeedsInput;
            count+=*house.infantry_c14_matches;
        }
        if (limit<=0 || count<limit) return CanBuild::Can;
        if (!include_queued || count!=limit) return CanBuild::LimitReached;
    } else {
        if (limit<=0) return CanBuild::Can;
        if (!*owned) return CanBuild::NeedsInput;
        if (counter(**owned,type.heap_index)<limit) return CanBuild::Can;
        if (!include_queued) return CanBuild::LimitReached;
    }
    if (!house.factory_objects) return CanBuild::NeedsInput;
    for (const auto& object:*house.factory_objects)
        if (object && object->first==type.rtti && object->second==type.heap_index) return CanBuild::Can;
    return CanBuild::LimitReached;
}

const std::vector<CellXY>& original_foundation_cells(std::int32_t foundation) {
    // Dumped by executing 00454EF0; row-major offsets before the 7FFF terminator.
    static const std::vector<std::vector<CellXY>> lists=[] {
        std::vector<std::vector<CellXY>> out(22);
        auto rect=[](int w,int h){ std::vector<CellXY> v; for (int y=0;y<h;++y) for (int x=0;x<w;++x) v.push_back({std::int16_t(x),std::int16_t(y)}); return v; };
        out[0]=rect(1,1); out[1]=rect(2,1); out[2]=rect(1,2); out[3]=rect(2,2);
        out[4]=rect(2,3); out[5]=rect(3,2); out[6]=rect(3,3); out[7]=rect(3,5);
        out[8]=rect(4,2); out[9]={{0,0},{1,0},{2,0},{0,1},{1,1},{0,2},{1,2},{2,2}};
        out[10]=rect(1,3); out[11]=rect(3,1); out[12]=rect(4,3); out[13]=rect(1,4);
        out[14]=rect(1,5); out[15]=rect(2,6); out[16]=rect(2,5); out[17]=rect(5,3);
        out[18]=rect(4,4); out[19]=rect(3,4); out[20]=rect(6,4); out[21]={};
        return out;
    }();
    static const std::vector<CellXY> none;
    return foundation>=0 && foundation<22?lists[static_cast<std::size_t>(foundation)]:none;
}

std::uint32_t packed_cell(CellXY c) {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(c.y))<<16|static_cast<std::uint16_t>(c.x);
}

namespace {
const PlacementCell* lookup(const PlacementMap& map,CellXY c) {
    // 00548070: index y*512+x in [0,3FFFF] with an allocated pointer.
    const std::int32_t index=std::int32_t(c.y)*0x200+c.x;
    if (index>=0 && index<=0x3FFFF) {
        const auto it=map.cells.find(packed_cell(c));
        if (it!=map.cells.end()) return &it->second;
    }
    return map.fallback?&*map.fallback:nullptr;
}
std::optional<bool> in_local_map(const PlacementMap& map,CellXY c,const PlacementCell& cell) {
    // 0055A740 with argument one.
    if (!cell.height || !cell.ramp) return std::nullopt;
    const std::int64_t x=c.x,y=c.y,w=map.width;
    std::int64_t h=*cell.height;
    if (*cell.ramp && x+y<w+4+std::int64_t(map.local_y)*2+h) ++h;
    return w+std::int64_t(map.local_y)*2+h<x+y &&
        x+y<=w+2+(std::int64_t(map.local_height)+map.local_y)*2+h &&
        x-y<(std::int64_t(map.local_width)+map.local_x)*2-w &&
        y-x<w-std::int64_t(map.local_x)*2;
}
}

Placement original_clear_to_build(const PlacementMap& map,CellXY c,const PlacementBuilding& building) {
    if (!map.scenario_init) return Placement::NeedsInput;
    if (*map.scenario_init!=0) return Placement::Legal;                         // 004718B0 first test.
    if (building.laser_fence || building.laser_fence_post || building.gate || building.to_tile)
        return Placement::Unsupported;
    const auto* cell=lookup(map,c);
    if (!cell || !cell->ground || !map.object_scan) return Placement::NeedsInput;
    for (const auto& o:*cell->ground) if (*map.object_scan && o.rtti==Rtti::Aircraft) return Placement::Illegal;
    for (const auto& o:*cell->ground) if (o.techno) return Placement::Illegal;   // 00471660.
    for (const auto& o:*cell->ground) if (*map.object_scan && o.rtti==Rtti::Terrain) return Placement::Illegal; // 00471760(24).
    if (!cell->occupancy_124) return Placement::NeedsInput;
    if (*cell->occupancy_124&0x3F) return Placement::Illegal;
    if (!map.editor_mode) return Placement::NeedsInput;
    if (*map.editor_mode) return Placement::Unsupported;                       // 0054AAB0 editor path.
    const auto local=in_local_map(map,c,*cell);
    if (!local) return Placement::NeedsInput;
    if (!*local) return Placement::Illegal;
    if (!cell->overlay) return Placement::NeedsInput;
    if (*cell->overlay!=-1) {
        // Wall and fence exceptions need the building's overlay identity.
        if (building.to_overlay) return Placement::Unsupported;
        return Placement::Illegal;                                              // 00471BDF, editor off.
    }
    if (building.speed_type!=-1 || building.naval) return Placement::Unsupported;
    if (!cell->flags_140) return Placement::NeedsInput;
    if (*cell->flags_140&0x100 || *cell->flags_140&0x400) return Placement::Illegal;
    if (*cell->ramp) return Placement::Illegal;
    if (!cell->land_type || !map.buildable) return Placement::NeedsInput;
    if (*cell->land_type<0 || *cell->land_type>=12) return Placement::Unsupported;
    return (*map.buildable)[static_cast<std::size_t>(*cell->land_type)]?Placement::Legal:Placement::Illegal;
}

Placement original_legal_placement(const PlacementMap& map,CellXY origin,const PlacementBuilding& building) {
    if (building.place_anywhere) return Placement::Legal;                        // 0045D880.
    if (origin.x==0 && origin.y==0) return Placement::Illegal;                   // 00AC01E8 cleared by 006D9AF0.
    if (building.foundation<0 || building.foundation>=22) return Placement::Unsupported;
    bool any_legal=false, any_illegal=false, unknown=false, unsupported=false;
    for (const auto offset:original_foundation_cells(building.foundation)) {
        const CellXY cell{std::int16_t(origin.x+offset.x),std::int16_t(origin.y+offset.y)};
        switch (original_clear_to_build(map,cell,building)) {
        case Placement::Legal: any_legal=true; break;
        case Placement::Illegal: any_illegal=true; break;
        case Placement::NeedsInput: unknown=true; break;
        case Placement::Unsupported: unsupported=true; break;
        }
    }
    (void)any_legal; // Only ToTile types use the any-legal result.
    if (any_illegal) return Placement::Illegal;
    if (unsupported) return Placement::Unsupported;
    if (unknown) return Placement::NeedsInput;
    return Placement::Legal;
}

bool original_deploy_without_offset(bool sensor_array,bool tick_tank,bool icbm,bool artillery) {
    return sensor_array || tick_tank || icbm || artillery;                      // 0045EC50.
}
std::uint8_t original_deploy_facing(bool sensor_array,bool tick_tank,bool icbm,bool artillery) {
    const bool first=sensor_array || tick_tank || icbm;                         // 0045EC90.
    if (!first) return artillery?0:0x80;
    return artillery?0:0x40;
}
}
