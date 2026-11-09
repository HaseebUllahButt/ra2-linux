#include "production_runtime.h"
#include <algorithm>
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
            count=std::int32_t(std::uint32_t(count)+std::uint32_t(*house.infantry_c14_matches)); // INC EBP wraps.
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
struct Receiver { const PlacementCell* cell; CellXY xy; };
Receiver lookup(const PlacementMap& map,CellXY c) {
    // 00548070: index y*512+x in [0,3FFFF]. An allocated slot is the canonical
    // Cell for that index, whose own +24/+26 coordinates later tests read.
    // Otherwise the fallback Cell receives the query coordinates (00A6FA1C).
    const std::int32_t index=std::int32_t(c.y)*0x200+c.x;
    if (index>=0 && index<=0x3FFFF) {
        const CellXY canonical{std::int16_t(index&0x1FF),std::int16_t(index>>9)};
        const auto it=map.cells.find(packed_cell(canonical));
        if (it!=map.cells.end()) return {&it->second,canonical};
    }
    return {map.fallback?&*map.fallback:nullptr,c};
}
std::optional<bool> in_local_map(const PlacementMap& map,CellXY c,const PlacementCell& cell) {
    // 0055A740 with argument one, on the receiver Cell coordinates.
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
    const auto receiver=lookup(map,c);
    const auto* cell=receiver.cell;
    if (!cell || !cell->ground || !map.object_scan) return Placement::NeedsInput;
    for (const auto& o:*cell->ground) if (*map.object_scan && o.rtti==Rtti::Aircraft) return Placement::Illegal;
    for (const auto& o:*cell->ground) if (o.techno) return Placement::Illegal;   // 00471660.
    for (const auto& o:*cell->ground) if (*map.object_scan && o.rtti==Rtti::Terrain) return Placement::Illegal; // 00471760(24).
    if (!cell->occupancy_124) return Placement::NeedsInput;
    if (*cell->occupancy_124&0x3F) return Placement::Illegal;
    if (!map.editor_mode) return Placement::NeedsInput;
    if (*map.editor_mode) return Placement::Unsupported;                       // 0054AAB0 editor path.
    const auto local=in_local_map(map,receiver.xy,*cell);
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

// ===========================================================================
// Catalog: original reader semantics for the fields production uses.
namespace ra2::sim::production {
namespace {
using rules::IniFile;
bool same_text(const std::string& a,const std::string& b) {   // 00781F20 stricmp.
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i) {
        auto up=[](unsigned char c){ return c>='a'&&c<='z'?c-32:c; };
        if (up(a[i])!=up(b[i])) return false;
    }
    return true;
}
// Rules values are read from the rules file, then the map overlays them.
const IniFile::Entry* entry(const IniFile& rules,const IniFile& map,const std::string& section,const std::string& key) {
    if (const auto* e=map.find(section,key)) return e;
    return rules.find(section,key);
}
std::optional<std::string> text(const IniFile& rules,const IniFile& map,const std::string& s,const std::string& k) {
    const auto* e=entry(rules,map,s,k); if (!e) return std::nullopt;
    return std::string(IniFile::trim(e->value));
}
template<class T> void read_int(const IniFile& r,const IniFile& m,const std::string& s,const char* k,T& out) {
    if (const auto v=text(r,m,s,k)) if (const auto n=IniFile::integer_value(*v)) out=static_cast<T>(*n);
}
void read_bool(const IniFile& r,const IniFile& m,const std::string& s,const char* k,bool& out) {
    if (const auto v=text(r,m,s,k)) if (const auto b=IniFile::bool_value(*v)) out=*b;
}
std::vector<std::string> registry(const IniFile& rules,const IniFile& map,const char* list) {
    // Base list order, then names first seen in the map list (Find_Or_Make).
    std::vector<std::string> out;
    for (const auto* ini:{&rules,&map}) if (const auto* s=ini->find_section(list))
        for (const auto& e:s->entries) {
            const auto name=std::string(IniFile::trim(e.value));
            if (name.empty()) continue;
            if (std::none_of(out.begin(),out.end(),[&](const std::string& n){ return same_text(n,name); })) out.push_back(name);
        }
    return out;
}
}

const TypeFacts* Catalog::find(const std::string& id) const {
    for (const auto* list:{&buildings,&units,&infantry,&aircraft})
        for (const auto& t:*list) if (t.id==id) return &t;
    return nullptr;
}
const TypeFacts* Catalog::building(std::int32_t heap) const {
    return heap>=0 && heap<std::int32_t(buildings.size())?&buildings[std::size_t(heap)]:nullptr;
}

Catalog Catalog::from(const IniFile& rules,const IniFile& art,const IniFile& map) {
    Catalog c;
    c.countries=registry(rules,map,"Countries");
    const auto building_names=registry(rules,map,"BuildingTypes");
    auto country_bits=[&](const std::string& value) {
        // 0046A590 / 00482DE0 / 004F94D0: comma tokens, <random> is -2.
        std::uint32_t bits=0;
        for (const auto& token:IniFile::split_list(value)) {
            std::int32_t index=-1;
            if (same_text(token,"<random>")) index=-2;
            else for (std::size_t i=0;i<c.countries.size();++i) if (same_text(c.countries[i],token)) { index=std::int32_t(i); break; }
            if (index!=-1) bits|=1u<<(std::uint32_t(index)&31u);
        }
        return bits;
    };
    auto building_index=[&](const std::string& name)->std::int32_t {
        for (std::size_t i=0;i<building_names.size();++i) if (same_text(building_names[i],name)) return std::int32_t(i);
        return -1;
    };
    auto prerequisite_list=[&](const std::string& value) {
        // 0046C520: POWER..PROC map to -1..-6; unknown building names are skipped.
        static const char* generic[]={"POWER","FACTORY","BARRACKS","RADAR","TECH","PROC"};
        std::vector<std::int32_t> out;
        for (const auto& token:IniFile::split_list(value)) {
            bool matched=false;
            for (int g=0;g<6;++g) if (same_text(token,generic[g])) { out.push_back(-1-g); matched=true; }
            if (!matched) { const auto i=building_index(token); if (i!=-1) out.push_back(i); }
        }
        return out;
    };
    auto load=[&](const std::vector<std::string>& names,Rtti rtti,std::vector<TypeFacts>& out) {
        for (std::size_t i=0;i<names.size();++i) {
            TypeFacts t; t.id=names[i]; t.rtti=rtti; t.heap_index=std::int32_t(i);
            // Constructor values from the executed 00457AC0/006D9B40.
            t.can.rtti=rtti; t.can.heap_index=t.heap_index; t.can.tech_level=255; t.can.build_limit=0x7FFFFFFF;
            const auto& id=t.id;
            read_int(rules,map,id,"TechLevel",t.can.tech_level);
            read_int(rules,map,id,"BuildLimit",t.can.build_limit);
            read_int(rules,map,id,"Cost",t.cost);
            read_int(rules,map,id,"Strength",t.strength);
            if (const auto v=text(rules,map,id,"Owner")) t.owner=country_bits(*v);
            if (const auto v=text(rules,map,id,"RequiredHouses")) t.can.required_houses=country_bits(*v);
            if (const auto v=text(rules,map,id,"ForbiddenHouses")) t.can.forbidden_houses=country_bits(*v);
            read_bool(rules,map,id,"RequiresStolenAlliedTech",t.can.requires_stolen_allied);
            read_bool(rules,map,id,"RequiresStolenSovietTech",t.can.requires_stolen_soviet);
            read_bool(rules,map,id,"DoubleOwned",t.double_owned);
            if (const auto v=text(rules,map,id,"Prerequisite")) t.can.prerequisite=prerequisite_list(*v);
            if (const auto v=text(rules,map,id,"PrerequisiteOverride")) t.can.prerequisite_override=prerequisite_list(*v);
            bool naval=false; read_bool(rules,map,id,"Naval",naval); t.placement.naval=naval;
            if (rtti==Rtti::UnitType) if (const auto v=text(rules,map,id,"DeploysInto")) t.deploys_into=*v;
            if (rtti==Rtti::BuildingType) {
                if (const auto v=text(rules,map,id,"SuperWeapon")) if (!v->empty() && !same_text(*v,"<none>")) t.can.super_weapon=0;
                read_int(rules,map,id,"Adjacent",t.adjacent);
                read_bool(rules,map,id,"BaseNormal",t.base_normal);
                read_bool(rules,map,id,"EligibileForAllyBuilding",t.eligible_ally);
                read_bool(rules,map,id,"ConstructionYard",t.construction_yard);
                read_bool(rules,map,id,"PlaceAnywhere",t.placement.place_anywhere);
                read_bool(rules,map,id,"Gate",t.placement.gate);
                read_bool(rules,map,id,"LaserFence",t.placement.laser_fence);
                read_bool(rules,map,id,"LaserFencePost",t.placement.laser_fence_post);
                read_bool(rules,map,id,"Wall",t.wall);
                read_bool(rules,map,id,"SensorArray",t.sensor_array);
                read_bool(rules,map,id,"TickTank",t.tick_tank);
                read_bool(rules,map,id,"ICBMLauncher",t.icbm);
                read_bool(rules,map,id,"Artillary",t.artillery);
                if (const auto v=text(rules,map,id,"ToOverlay")) t.placement.to_overlay=!v->empty() && !same_text(*v,"<none>");
                if (const auto v=text(rules,map,id,"ToTile")) t.placement.to_tile=!v->empty();
                if (const auto v=text(rules,map,id,"PowersUpBuilding")) t.powers_up=!v->empty();
                if (const auto v=text(rules,map,id,"FreeUnit")) t.free_unit=*v;
                bool waterbound=false; read_bool(rules,map,id,"WaterBound",waterbound);
                t.placement.speed_type=waterbound?5:-1;                        // 00459ADE.
                if (const auto v=text(rules,map,id,"Factory")) {                // 0046A4B0 table 007CD978.
                    static const std::pair<const char*,Rtti> kinds[]={{"BuildingType",Rtti::BuildingType},
                        {"UnitType",Rtti::UnitType},{"InfantryType",Rtti::InfantryType},{"AircraftType",Rtti::AircraftType}};
                    for (const auto& k:kinds) if (same_text(*v,k.first)) t.factory=k.second;
                    if (!t.factory && !v->empty()) c.gaps.push_back(id+" Factory="+*v+" uses another RTTI");
                }
                if (const auto v=text(rules,map,id,"BuildCat")) {               // 0046A520 table 007CFC40.
                    static const std::pair<const char*,int> cats[]={{"DontCare",0},{"Tech",1},{"Power",3},
                        {"Resource",2},{"Infrastructure",4},{"Combat",5}};
                    t.build_cat=0; for (const auto& k:cats) if (same_text(*v,k.first)) t.build_cat=k.second;
                }
                std::int32_t power=0, extra=0;                                  // 0045A8F3 / 0045A933.
                read_int(rules,map,id,"Power",power); read_int(rules,map,id,"ExtraPower",extra);
                if (power>=0) t.power_output=power; else t.power_drain=-power;
                if (extra>=0) t.extra_output=extra; else t.extra_drain=-extra;
                // Foundation: art [Image] then art [ID] (0045AA8F, 0045AAA0).
                static const char* foundations[]={"1x1","2x1","1x2","2x2","2x3","3x2","3x3","3x5","4x2",
                    "3x3Refinery","1x3","3x1","4x3","1x4","1x5","2x6","2x5","5x3","4x4","3x4","6x4","0x0"};
                auto parse=[&](const std::string& section,std::int32_t current) {
                    const auto* e=art.find(section,"Foundation"); if (!e) return current;
                    const std::string v(IniFile::trim(e->value));
                    for (int f=0;f<22;++f) if (same_text(v,foundations[f])) return f;
                    return 0;
                };
                std::string image=id; if (const auto v=text(rules,map,id,"Image")) image=*v;
                t.placement.foundation=parse(id,parse(image,0));
            }
            out.push_back(std::move(t));
        }
    };
    load(building_names,Rtti::BuildingType,c.buildings);
    load(registry(rules,map,"VehicleTypes"),Rtti::UnitType,c.units);
    load(registry(rules,map,"InfantryTypes"),Rtti::InfantryType,c.infantry);
    load(registry(rules,map,"AircraftTypes"),Rtti::AircraftType,c.aircraft);
    static const char* generic_keys[]={"PrerequisitePower","PrerequisiteFactory","PrerequisiteBarracks",
        "PrerequisiteRadar","PrerequisiteTech","PrerequisiteProc"};
    for (int g=0;g<6;++g) if (const auto v=text(rules,map,"General",generic_keys[g]))
        for (const auto& token:IniFile::split_list(*v)) { const auto i=building_index(token); if (i!=-1) c.generic_prerequisites[std::size_t(g)].push_back(i); }
    // LandType records 0064D610; a missing section keeps an unresolved value.
    static const char* lands[]={"Clear","Road","Water","Rock","Wall","Tiberium","Beach","Rough","Ice","Railroad","Tunnel","Weeds"};
    std::array<bool,12> buildable{}; bool all=true;
    for (int l=0;l<12;++l) {
        if (!rules.find_section(lands[l]) && !map.find_section(lands[l])) { all=false; c.gaps.push_back(std::string("LandType section missing: ")+lands[l]); continue; }
        bool b=false; read_bool(rules,map,lands[l],"Buildable",b); buildable[std::size_t(l)]=b;
    }
    if (all) c.buildable=buildable;
    for (const char* k:{"BuildSpeed","MinLowPowerProductionSpeed","MaxLowPowerProductionSpeed","LowPowerPenaltyModifier",
                        "MultipleFactory","WallBuildSpeedCoefficient"})
        if (!text(rules,map,"General",k)) c.gaps.push_back(std::string("General.")+k+" missing; Rules constructor value not recovered");
    if (const auto v=text(rules,map,"General","BuildSpeed")) if (const auto d=IniFile::double_value(*v)) c.build_speed=*d;
    auto read_float=[&](const char* k,float& out){ if (const auto v=text(rules,map,"General",k)) if (const auto d=IniFile::double_value(*v)) out=float(*d); };
    read_float("MinLowPowerProductionSpeed",c.minimum_low_power);
    read_float("MaxLowPowerProductionSpeed",c.maximum_low_power);
    read_float("LowPowerPenaltyModifier",c.low_power_penalty);
    read_float("MultipleFactory",c.multiple_factory);
    if (const auto v=text(rules,map,"General","WallBuildSpeedCoefficient")) if (const auto d=IniFile::double_value(*v)) c.wall_coefficient=*d;
    if (const auto v=text(rules,map,"General","PadAircraft")) c.pad_aircraft=IniFile::split_list(*v);
    read_bool(rules,map,"General","SeparateAircraft",c.separate_aircraft);
    for (auto& t:c.infantry) read_bool(rules,map,t.id,"VehicleThief",t.can.infantry_flag_c14); // InfantryType +C14, 0050A83C.
    return c;
}
}

// ===========================================================================
// Shared world commands. Each command edits private copies and publishes
// Snapshot and FactorySet together only when it completes.
#include "../session/world/world.h"
#include "build_time.h"
#include <set>

namespace ra2::sim::production {
namespace {
namespace w=ra2::session::world;
constexpr std::int32_t kMissionConstruction=0x12, kMissionSelling=0x13;

Outcome fail(Status s,std::string d) { Outcome o; o.status=s; o.detail=std::move(d); return o; }
w::House* house_of(w::Snapshot& s,std::uint32_t id) {
    for (auto& h:s.houses) if (h.id==id) return &h;
    return nullptr;
}
const w::House* house_of(const w::Snapshot& s,std::uint32_t id) {
    for (const auto& h:s.houses) if (h.id==id) return &h;
    return nullptr;
}
w::Entity* entity_of(w::Snapshot& s,std::uint32_t id) {
    for (auto& e:s.entities) if (e.id==id) return &e;
    return nullptr;
}
const w::Entity* entity_of(const w::Snapshot& s,std::uint32_t id) {
    for (const auto& e:s.entities) if (e.id==id) return &e;
    return nullptr;
}
std::optional<std::uint32_t> cell_key(CellXY c) {
    if (c.x<0 || c.y<0 || c.x>=512 || c.y>=512) return std::nullopt;
    return std::uint32_t(c.y)*512u+std::uint32_t(c.x);
}
Rtti object_rtti(w::Kind k) {
    switch (k) { case w::Kind::Unit: return Rtti::Unit; case w::Kind::Infantry: return Rtti::Infantry;
    case w::Kind::Aircraft: return Rtti::Aircraft; case w::Kind::Structure: return Rtti::Building; default: return Rtti::Terrain; }
}
const TypeFacts* facts_of(const Catalog& c,const w::Entity& e) {
    const std::vector<TypeFacts>* list=e.kind==w::Kind::Structure?&c.buildings:e.kind==w::Kind::Unit?&c.units:
        e.kind==w::Kind::Infantry?&c.infantry:e.kind==w::Kind::Aircraft?&c.aircraft:nullptr;
    if (!list) return nullptr;
    for (const auto& t:*list) if (t.id==e.type) return &t;
    return nullptr;
}
bool runtime_ready(const w::Snapshot& s) {
    return s.setup_progress==w::SetupProgress::Complete && !s.pending_cell_setup && s.geometry;
}
std::optional<std::uint32_t> next_id(const w::Snapshot& s) {
    std::uint32_t largest=0;
    for (const auto& item:s.creation_order) largest=std::max(largest,item.id);
    if (largest==UINT32_MAX) return std::nullopt;
    return largest+1;
}
// Ground chain from Cell +E4 through Object +30.
std::optional<std::vector<const w::Entity*>> ground_chain(const w::Snapshot& s,const w::CellState& cell) {
    if (!cell.heads) return std::nullopt;
    std::vector<const w::Entity*> out; std::set<std::uint32_t> seen;
    for (auto id=cell.heads->ground_head;id;) {
        const auto* e=entity_of(s,id);
        if (!e || !seen.insert(id).second || !e->next) return std::nullopt;
        out.push_back(e); id=*e->next;
    }
    return out;
}
PlacementMap placement_view(const w::Snapshot& s,const Catalog& c,const std::vector<CellXY>& cells) {
    PlacementMap m;
    m.width=s.geometry->width; m.local_x=s.geometry->local.x; m.local_y=s.geometry->local.y;
    m.local_width=s.geometry->local.width; m.local_height=s.geometry->local.height;
    m.buildable=c.buildable;
    // A completed scenario runs with ScenarioInit 0, 00A40958 set by 005135F0,
    // and the editor flag cleared by 0051512E.
    m.scenario_init=0; m.object_scan=true; m.editor_mode=false;
    for (const auto xy:cells) {
        const auto key=cell_key(xy); if (!key) continue;
        const auto it=s.cells.find(*key);
        if (it==s.cells.end() || !it->second.allocated || !*it->second.allocated) continue;
        const auto& state=it->second;
        PlacementCell p;
        if (const auto chain=ground_chain(s,state)) {
            std::vector<CellOccupant> occ;
            for (const auto* e:*chain) occ.push_back({object_rtti(e->kind),e->kind!=w::Kind::Terrain});
            p.ground=occ;
        }
        if (state.ground_occupancy_124) p.occupancy_124=std::uint8_t(*state.ground_occupancy_124&0xFF);
        if (state.terrain) {
            if (state.terrain->height_level) p.height=std::int8_t(*state.terrain->height_level);
            if (state.terrain->ramp_index) p.ramp=std::uint8_t(*state.terrain->ramp_index);
            p.overlay=state.terrain->overlay_type_index;
            p.land_type=state.terrain->land_type;
        }
        p.flags_140=state.cell_flags_140;
        m.cells[packed_cell(xy)]=p;
    }
    return m;
}
std::vector<CellXY> footprint(CellXY origin,std::int32_t foundation) {
    std::vector<CellXY> out;
    for (const auto o:original_foundation_cells(foundation))
        out.push_back({std::int16_t(origin.x+o.x),std::int16_t(origin.y+o.y)});
    return out;
}
// House +6C order: buildings in original creation order.
std::vector<const w::Entity*> house_buildings(const w::Snapshot& s,std::uint32_t house) {
    std::vector<const w::Entity*> out;
    for (const auto& item:s.creation_order) if (item.kind==w::CreatedKind::Entity) {
        const auto* e=entity_of(s,item.id);
        if (e && e->house_id==house && e->kind==w::Kind::Structure && e->lifetime!=w::Lifetime::Destroyed) out.push_back(e);
    }
    return out;
}
CanBuildHouse can_build_house(const w::Snapshot& s,const FactorySet& f,const Catalog& c,const w::House& h) {
    CanBuildHouse in;
    in.tech_level=h.tech_level; in.stolen_allied=h.stolen_tech_205; in.stolen_soviet=h.stolen_tech_204;
    in.country=h.country_index; in.flag_134=h.human_controlled; in.flag_135=h.house_flag_135;
    if (h.stolen_mask_208 && h.stolen_mask_20c && h.stolen_mask_210 && h.stolen_mask_214)
        in.stolen_masks=std::array<std::uint32_t,4>{*h.stolen_mask_208,*h.stolen_mask_20c,*h.stolen_mask_210,*h.stolen_mask_214};
    in.active_buildings=h.buildings_active_5460;
    in.owned_buildings=h.buildings_owned_5420; in.owned_units=h.units_owned_5430;
    in.owned_infantry=h.infantry_owned_5440; in.owned_aircraft=h.aircraft_owned_5450;
    in.built_buildings=h.buildings_ever_54a0; in.built_units=h.units_ever_54b0;
    in.built_infantry=h.infantry_ever_54c0; in.built_aircraft=h.aircraft_ever_54d0;
    // Building +530 and upgrade slots are known only for runtime-created buildings.
    std::vector<HouseBuilding> list; bool known=true;
    for (const auto* e:house_buildings(s,h.id)) {
        HouseBuilding b;
        const auto extra=f.buildings.find(e->id);
        if (!e->object_field_76 || !e->mission || extra==f.buildings.end()) { known=false; break; }
        b.limbo=*e->object_field_76!=0; b.flag_530=extra->second.on_530; b.mission=*e->mission;
        b.mission_a0=extra->second.mission_a0;
        list.push_back(b);
    }
    if (known) in.buildings=list;
    std::vector<std::optional<std::pair<Rtti,std::int32_t>>> objects;
    for (const auto& factory:f.factories) if (factory.house_id==h.id) {
        if (!factory.object) { objects.push_back(std::nullopt); continue; }
        const auto* t=c.find(factory.object_type);
        if (!t) return in;
        objects.push_back(std::make_pair(t->rtti,t->heap_index));
    }
    in.factory_objects=objects;
    return in;
}
CanBuildRules can_build_rules(const FactorySet& f,const Catalog& c) {
    CanBuildRules r; r.super_weapons_allowed=f.flags.super_weapons; r.session_type=f.flags.session_type;
    r.generic_prerequisites=c.generic_prerequisites;
    for (const auto& b:c.buildings) r.powers_up_building[b.heap_index]=b.powers_up;
    return r;
}
std::uint32_t ownable(const TypeFacts& t,std::int32_t session) {          // 006DACF0.
    return t.double_owned && session!=0?0x7FFFFFFFu:t.owner;
}
// TechnoType::Who_Can_Build_Me 005D5FC0 with in-theory false and the house.
std::optional<const w::Entity*> who_can_build(const w::Snapshot& s,const FactorySet& f,const Catalog& c,
                                              const TypeFacts& type,const w::House& h,bool needs_530,bool legal_check) {
    if (!f.flags.session_type) return std::nullopt;
    if (type.rtti==Rtti::AircraftType) return std::nullopt;                // Dock checks 00636F10/00636EA0 not recovered.
    const auto mine=ownable(type,*f.flags.session_type);
    const w::Entity* candidate=nullptr;
    for (const auto* b:house_buildings(s,h.id)) {
        if (!b->object_field_76) return std::nullopt;
        if (*b->object_field_76) continue;
        const auto* bt=facts_of(c,*b); if (!bt) return std::nullopt;
        if (!bt->factory || *bt->factory!=type.rtti) continue;
        const auto extra=f.buildings.find(b->id);
        if (!b->mission || extra==f.buildings.end()) return std::nullopt;
        if (needs_530 && !extra->second.on_530) continue;
        if (*b->mission==kMissionSelling || extra->second.mission_a0==kMissionSelling) continue;
        if (legal_check) {
            const auto limit=original_can_build(type.can,can_build_house(s,f,c,h),can_build_rules(f,c),true,true);
            if (limit==CanBuild::NeedsInput || limit==CanBuild::Unsupported) return std::nullopt;
            if (limit!=CanBuild::Can) continue;
        }
        if (!(ownable(*bt,*f.flags.session_type)&mine)) continue;
        const bool naval_unit=type.rtti==Rtti::UnitType && type.placement.naval;
        if (naval_unit ? !bt->placement.naval : bt->placement.naval) continue;
        if (extra->second.primary) return b;
        candidate=b;
    }
    return candidate;
}
std::int32_t slot_of(const TypeFacts& t) {                                 // 004E5E7C.
    switch (t.rtti) {
    case Rtti::UnitType: return t.placement.naval?3:2;
    case Rtti::AircraftType: return 0;
    case Rtti::InfantryType: return 1;
    case Rtti::BuildingType: return t.build_cat==5?5:4;
    default: return -1;
    }
}
std::int32_t count_kind(const TypeFacts& t) {                              // 004EB980 / 004EAB10 counters.
    switch (t.rtti) {
    case Rtti::UnitType: return t.placement.naval?4:2;  // +52C8 naval, +52C0 unit.
    case Rtti::AircraftType: return 0;                  // +52B8.
    case Rtti::InfantryType: return 1;                  // +52BC.
    case Rtti::BuildingType: return 3;                  // +52C4.
    default: return -1;
    }
}
FactoryState* factory_in_slot(FactorySet& f,std::uint32_t house,std::int32_t slot) {
    for (auto& x:f.factories) if (x.house_id==house && x.slot==slot) return &x;
    return nullptr;
}
// TechnoType::Cost_Of(house) 006DAD30 with BuildingType 00458A70 adjustments.
std::optional<std::int32_t> cost_of(const Catalog& c,const TypeFacts& t,const w::House&) {
    // HouseType cost multipliers: the base rules comment them out, so the
    // HouseType constructor value 1.0f (004F91A8..) applies.
    auto raw=[&](const TypeFacts& x)->std::int64_t {
        std::int64_t v=x.cost;                                             // 006DACE0.
        if (x.rtti!=Rtti::BuildingType) return v;
        if (!x.free_unit.empty()) { const auto* u=c.find(x.free_unit); if (!u) return -1; v-=u->cost; }
        return v>0?v:0;                                                    // 004589F0 clamp.
    };
    auto scaled=[&](const TypeFacts& x)->std::int64_t { const auto r=raw(x); return r<0?-1:std::int64_t(float(r)*1.0f); };
    std::int64_t v=scaled(t);
    if (v<0) return std::nullopt;
    if (t.rtti==Rtti::BuildingType) {
        if (!c.separate_aircraft && !c.pad_aircraft.empty()) return std::nullopt; // Pad aircraft dock pricing not modeled.
        if (!t.free_unit.empty()) { const auto* u=c.find(t.free_unit); if (!u) return std::nullopt; v+=scaled(*u); }
        if (v<=0) v=0;
    }
    if (v>INT32_MAX) return std::nullopt;
    return std::int32_t(v);
}
std::optional<int> build_frames(const Catalog& c,const FactorySet& f,const TypeFacts& t,const w::House& h) {
    if (!h.power_output || !h.power_drain) return std::nullopt;
    const auto counts=f.house_factory_counts.find(h.id);
    if (counts==f.house_factory_counts.end()) return std::nullopt;
    const auto kind=count_kind(t); if (kind<0) return std::nullopt;
    BuildTimeInputs in; in.edition=BuildEdition::BaseRa2; in.cost=t.cost; in.build_speed=c.build_speed;
    in.country_multiplier=1.0f;                                            // HouseType ctor 1.0f; base rules leave it.
    in.power_output=*h.power_output; in.power_drain=*h.power_drain;
    in.minimum_low_power=c.minimum_low_power; in.maximum_low_power=c.maximum_low_power;
    in.low_power_penalty=c.low_power_penalty; in.multiple_factory=c.multiple_factory;
    in.factories=counts->second[std::size_t(kind)];
    in.wall=t.rtti==Rtti::BuildingType && t.wall; in.wall_coefficient=c.wall_coefficient;
    try { return original_build_time(in); } catch (...) { return std::nullopt; }
}
bool counter_add(std::optional<std::map<std::int32_t,std::int32_t>>& m,std::int32_t index,int delta) {
    if (!m) return false;
    auto& v=(*m)[index];
    const auto next=std::int64_t(v)+delta;
    if (next<INT32_MIN || next>INT32_MAX) return false;
    v=std::int32_t(next); return true;
}
// Techno constructor 006C12AA draws one shared RNG word and stores its low 16 bits.
std::optional<std::uint32_t> create_object(w::Snapshot& s,const TypeFacts& t,std::uint32_t house,w::Kind kind) {
    const auto id=next_id(s); if (!id) return std::nullopt;
    sim::Random random(s.random);
    w::Entity e; e.id=*id; e.house_id=house; e.kind=kind; e.type=t.id; e.lifetime=w::Lifetime::Created;
    e.constructor_random=std::uint16_t(random.next());
    e.next=0; e.on_map=false; e.object_field_74=0; e.object_field_76=1; e.techno_field_370=0;
    e.original_type_index=t.heap_index; e.resolved_hp=t.strength; e.maximum_hp=t.strength;
    e.mission=-1;
    s.random=random.state();
    s.entities.push_back(e);
    s.creation_order.push_back({w::CreatedKind::Entity,*id});
    return id;
}
// House::Tracking_Add 004EA970 for owned counters of a new object.
bool tracking_add(w::House& h,const TypeFacts& t) {
    switch (t.rtti) {
    case Rtti::BuildingType: return counter_add(h.buildings_owned_5420,t.heap_index,1);
    case Rtti::UnitType: return counter_add(h.units_owned_5430,t.heap_index,1);
    case Rtti::InfantryType: return counter_add(h.infantry_owned_5440,t.heap_index,1);
    default: return false;
    }
}
}

std::vector<std::pair<std::string,CanBuild>> options(const w::Snapshot& s,const FactorySet& f,const Catalog& c,std::uint32_t house_id) {
    std::vector<std::pair<std::string,CanBuild>> out;
    const auto* h=house_of(s,house_id); if (!h) return out;
    const auto in=can_build_house(s,f,c,*h); const auto rules=can_build_rules(f,c);
    for (const auto* list:{&c.buildings,&c.units,&c.infantry,&c.aircraft})
        for (const auto& t:*list) out.push_back({t.id,original_can_build(t.can,in,rules,false,true)});
    return out;
}
}

namespace ra2::sim::production {
namespace {
const std::array<std::pair<int,int>,22> kFoundationBounds{{{1,1},{2,1},{1,2},{2,2},{2,3},{3,2},{3,3},{3,5},{4,2},{3,3},
    {1,3},{3,1},{4,3},{1,4},{1,5},{2,6},{2,5},{5,3},{4,4},{3,4},{6,4},{0,0}}}; // 00458930 table 007CFC70.

// Cell::Occupy_Up / Occupy_Down link writes (00473D20 / 00473B30).
bool unlink(w::Snapshot& s,std::uint32_t key,std::uint32_t object) {
    w::Undo::save_cell(s,key);
    auto& cell=s.cells.at(key);
    if (!cell.heads) return false;
    auto* item=entity_of(s,object); if (!item || !item->next) return false;
    if (cell.heads->ground_head==object) { cell.heads->ground_head=*item->next; item->next=0; return true; }
    std::set<std::uint32_t> seen;
    for (auto id=cell.heads->ground_head;id;) {
        auto* e=entity_of(s,id); if (!e || !e->next || !seen.insert(id).second) return false;
        if (*e->next==object) { e->next=*item->next; item->next=0; return true; }
        id=*e->next;
    }
    return true;
}
bool link(w::Snapshot& s,std::uint32_t key,std::uint32_t object,bool building) {
    w::Undo::save_cell(s,key);
    auto& cell=s.cells.at(key);
    if (!cell.heads) return false;
    auto* item=entity_of(s,object); if (!item) return false;
    auto first=cell.heads->ground_head;
    if (building && first) {                                               // Buildings append at the tail.
        std::set<std::uint32_t> seen; auto tail=first;
        while (true) {
            auto* e=entity_of(s,tail); if (!e || !e->next || !seen.insert(tail).second) return false;
            if (!*e->next) break;
            tail=*e->next;
        }
        entity_of(s,tail)->next=object; item->next=0;
    } else {
        const auto* f=first?entity_of(s,first):nullptr;
        if (first && (!f || !f->next)) return false;
        if (!first || *f->next!=object) { item->next=first; cell.heads->ground_head=object; }
    }
    return true;
}
// Foot Mark up/down: chain link plus the Unit ground occupancy bit 20 (00707980/007079E0).
bool mark_unit(w::Snapshot& s,w::Entity& u,bool down) {
    if (!u.cell || u.layer!=w::Layer::Ground) return false;
    const auto key=cell_key({std::int16_t(u.cell->x),std::int16_t(u.cell->y)}); if (!key) return false;
    auto it=s.cells.find(*key); if (it==s.cells.end() || !it->second.ground_occupancy_124) return false;
    if (!(down?link(s,*key,u.id,false):unlink(s,*key,u.id))) return false;
    w::Undo::save_cell(s,*key);
    auto& bits=*s.cells.at(*key).ground_occupancy_124;
    bits=down?(bits|0x20u):(bits&~0x20u);
    entity_of(s,u.id)->object_field_74=down?1:0;
    return true;
}
std::optional<bool> legal(const w::Snapshot& s,const Catalog& c,const TypeFacts& t,CellXY origin) {
    std::vector<CellXY> region=footprint(origin,t.placement.foundation);
    const auto r=original_legal_placement(placement_view(s,c,region),origin,t.placement);
    if (r==Placement::Legal) return true;
    if (r==Placement::Illegal) return false;
    return std::nullopt;
}
// BuildingClass::Unlimbo 0043D3A0 effects that production depends on.
// The checks that unlimbo_building() makes before its first write. Each
// returns the failure that unlimbo_building() returns.
std::optional<Outcome> unlimbo_failure(const w::Snapshot& s,const Catalog& c,std::uint32_t id,CellXY origin) {
    const auto* b=entity_of(s,id); if (!b) return fail(Status::NeedsInput,"building missing");
    const auto* t=facts_of(c,*b); if (!t) return fail(Status::NeedsInput,"building type missing");
    // Permit base radar foundations and the factory bookkeeping below.
    // Base C export 00438CE0 sets radio link capacity in the constructor.
    // Other aircraft factories still require recovery of their dock behavior.
    const bool base_radar=t->id=="AMRADR" || t->id=="GAAIRC";
    if ((t->factory==Rtti::AircraftType && !base_radar) || t->placement.to_overlay || t->wall)
        return fail(Status::Unsupported,"dock, wall and overlay building unlimbo is not recovered");
    const auto ok=legal(s,c,*t,origin);                                    // Can_Enter_Cell 004453B0.
    if (!ok) return fail(Status::NeedsInput,"placement cells unresolved");
    if (!*ok) return fail(Status::Rejected,"cannot deploy here");
    if (!house_of(s,b->house_id)) return fail(Status::NeedsInput,"house missing");
    return std::nullopt;
}
Outcome unlimbo_building(w::Snapshot& s,FactorySet& f,const Catalog& c,std::uint32_t id,CellXY origin) {
    if (auto failure=unlimbo_failure(s,c,id,origin)) return *failure;
    auto* b=entity_of(s,id);
    const auto* t=facts_of(c,*b);
    auto* h=house_of(s,b->house_id);
    for (const auto cell:footprint(origin,t->placement.foundation)) {       // Mark(PUT) 005D4160, Place_Down 0054AB70.
        const auto key=cell_key(cell); if (!key || !s.cells.count(*key)) continue;
        if (!link(s,*key,id,true)) return fail(Status::NeedsInput,"cell chain unresolved");
    }
    b=entity_of(s,id);
    b->lifetime=w::Lifetime::Placed; b->cell=ra2map::Cell{std::uint16_t(origin.x),std::uint16_t(origin.y)}; b->layer=w::Layer::Ground;
    b->object_field_74=1; b->object_field_76=0; b->on_map=true;
    b->position=sim::Coord{origin.x*256+128,origin.y*256+128,0};
    b->mission=kMissionConstruction;                                       // Set_Mission 00594710 resets +A0.
    auto& extra=f.buildings[id]; extra.mission_a0=-1;
    if (!counter_add(h->buildings_active_5460,t->heap_index,1)) return fail(Status::NeedsInput,"active counter unresolved"); // 004ED610.
    if (!h->power_drain) return fail(Status::NeedsInput,"power drain unresolved");
    if (extra.on_530) *h->power_drain+=t->power_drain;                     // 00449DD0 via 004EC850.
    if (t->factory) {                                                      // 004444B0 then 004EAC70.
        extra.primary=false;
        for (const auto* other:house_buildings(s,h->id)) {
            if (other->id==id || !other->object_field_76 || *other->object_field_76) continue;
            const auto* ot=facts_of(c,*other); auto ox=f.buildings.find(other->id);
            if (!ot || ox==f.buildings.end()) return fail(Status::NeedsInput,"factory primary state unresolved");
            if (ot->factory==t->factory && ox->second.primary && ot->placement.naval==t->placement.naval) { ox->second.primary=false; break; }
        }
        extra.primary=true;
        auto& counts=f.house_factory_counts[h->id];
        const TypeFacts probe=[&]{ TypeFacts p; p.rtti=*t->factory; p.placement.naval=t->placement.naval; return p; }();
        const auto kind=count_kind(probe); if (kind>=0) ++counts[std::size_t(kind)];
    }
    f.power_dirty[h->id]=true;                                             // 0043D784.
    return Outcome{Status::Done,{},id,{}};
}
}

Outcome deploy(w::Snapshot& snapshot,FactorySet& factories,const Catalog& c,std::uint32_t unit_id,std::optional<bool> can_deploy) {
    if (!runtime_ready(snapshot)) return fail(Status::NeedsInput,"setup is not complete");
    w::Undo undo(snapshot); auto& s=snapshot; auto f=factories; // Restores unless committed.
    auto* u=entity_of(s,unit_id);
    if (!u || u->kind!=w::Kind::Unit || u->lifetime!=w::Lifetime::Placed) return fail(Status::Rejected,"no placed unit");
    if (!can_deploy) return fail(Status::NeedsInput,"006CD340 deploy check unresolved");
    if (!*can_deploy) return fail(Status::Rejected,"unit cannot deploy");
    if (!u->locomotor_moving) return fail(Status::NeedsInput,"locomotor state unresolved");
    if (*u->locomotor_moving) return fail(Status::Rejected,"unit is moving");      // 006FE007.
    const auto* ut=facts_of(c,*u); if (!ut) return fail(Status::NeedsInput,"unit type missing");
    if (ut->deploys_into.empty()) return fail(Status::Rejected,"type does not deploy"); // 006FE00F.
    const auto* bt=c.find(ut->deploys_into);
    if (!bt || bt->rtti!=Rtti::BuildingType) return fail(Status::NeedsInput,"DeploysInto type missing");
    auto* h=house_of(s,u->house_id); if (!h) return fail(Status::NeedsInput,"house missing");
    if (!u->cell || !u->hull_facing_raw || !u->resolved_hp || !u->maximum_hp || *u->maximum_hp<=0)
        return fail(Status::NeedsInput,"unit cell, facing or health unresolved");
    if (!f.flags.session_type || !h->human_controlled) return fail(Status::NeedsInput,"session or house control unresolved");
    if (bt->construction_yard && *f.flags.session_type!=0 && !*h->human_controlled)
        return fail(Status::Unsupported,"computer base planning 004EF5A0 uses RNG and is not recovered");
    // 006FE02F: Mark up and clear locomotor bits, then test the deploy cell.
    if (!mark_unit(s,*entity_of(s,unit_id),false)) return fail(Status::NeedsInput,"unit occupancy unresolved");
    u=entity_of(s,unit_id);
    CellXY cell{std::int16_t(u->cell->x),std::int16_t(u->cell->y)};
    if (!original_deploy_without_offset(bt->sensor_array,bt->tick_tank,bt->icbm,bt->artillery)) { --cell.x; --cell.y; } // 00851A8C.
    const auto ok=legal(s,c,*bt,cell);
    if (!ok) return fail(Status::NeedsInput,"deploy cells unresolved");
    if (!mark_unit(s,*entity_of(s,unit_id),true)) return fail(Status::NeedsInput,"unit occupancy unresolved");
    if (!*ok) {                                                            // 006FE0C9: Mark down again.
        undo.commit(); factories=std::move(f);
        return fail(Status::Rejected,"cannot deploy here");
    }
    u=entity_of(s,unit_id);
    const auto want=original_deploy_facing(bt->sensor_array,bt->tick_tank,bt->icbm,bt->artillery);
    const auto have=std::uint8_t(((*u->hull_facing_raw>>7)+1)>>1);
    if (have!=want) {                                                      // 006FE16B: turn first.
        undo.commit(); factories=std::move(f);
        Outcome o=fail(Status::NeedsTurn,"turn to the deploy facing, then deploy again"); o.value=want; return o;
    }
    if (!mark_unit(s,*entity_of(s,unit_id),false)) return fail(Status::NeedsInput,"unit occupancy unresolved");
    const auto owner=u->house_id;
    const auto id=create_object(s,*bt,owner,w::Kind::Structure);           // 00438CE0, RNG at 006C12AA.
    if (!id) return fail(Status::NeedsInput,"no entity id");
    h=house_of(s,owner);
    if (!tracking_add(*h,*bt)) return fail(Status::NeedsInput,"owned building counter unresolved"); // 0043F840 -> 004EA970.
    f.buildings[*id]=BuildingState{};
    auto placed=unlimbo_building(s,f,c,*id,cell);
    if (placed.status!=Status::Done) return placed;
    // 006FE33D: Health = ftol(unit health ratio * building Strength), minimum one.
    u=entity_of(s,unit_id); auto* b=entity_of(s,*id);
    const double ratio=double(*u->resolved_hp)/double(*u->maximum_hp);
    b->resolved_hp=std::max<std::int32_t>(1,std::int32_t(ratio*double(bt->strength)));
    // Unit removal: Limbo then delete. Owned unit counter drops (004EA800).
    u->lifetime=w::Lifetime::Destroyed; u->on_map=false; u->object_field_76=1;
    h=house_of(s,owner);
    if (!counter_add(h->units_owned_5430,ut->heap_index,-1)) return fail(Status::NeedsInput,"owned unit counter unresolved");
    if (h->unit_count) --*h->unit_count;
    if (h->unit_type_counts) (*h->unit_type_counts)[ut->heap_index]-=1;
    undo.commit(); factories=std::move(f);
    Outcome o; o.status=Status::Done; o.entity=id; return o;
}

Outcome begin_production(w::Snapshot& snapshot,FactorySet& factories,const Catalog& c,std::uint32_t house_id,
                         const std::string& type,std::uint64_t frame) {
    if (!runtime_ready(snapshot)) return fail(Status::NeedsInput,"setup is not complete");
    w::Undo undo(snapshot); auto& s=snapshot; auto f=factories; // Restores unless committed.
    const auto* t=c.find(type); if (!t) return fail(Status::Rejected,"unknown type");
    auto* h=house_of(s,house_id); if (!h) return fail(Status::Rejected,"unknown house");
    // Sidebar gate: the cameo exists only when Can_Build(type,false,true) is positive.
    const auto gate=original_can_build(t->can,can_build_house(s,f,c,*h),can_build_rules(f,c),false,true);
    if (gate==CanBuild::NeedsInput || gate==CanBuild::Unsupported) return fail(Status::NeedsInput,"eligibility inputs unresolved");
    if (gate!=CanBuild::Can) return fail(Status::Rejected,"not buildable now");
    const auto builder=who_can_build(s,f,c,*t,*h,true,true);               // 004E5F18.
    if (!builder) return fail(Status::NeedsInput,"factory buildings unresolved");
    if (!*builder) return fail(Status::Rejected,"No-one can build");
    const auto slot=slot_of(*t); if (slot<0) return fail(Status::Unsupported,"no factory slot");
    auto* factory=factory_in_slot(f,house_id,slot);
    if (!factory) { f.factories.push_back(FactoryState{}); factory=&f.factories.back(); factory->house_id=house_id; factory->slot=slot; } // 004B90A0.
    if (factory->interval!=0 && !factory->suspended && t->rtti==Rtti::BuildingType)
        return fail(Status::Rejected,"Cannot queue buildings");
    const bool resume=factory->suspended && factory->object && factory->object_type==t->id;
    if (!resume) {
        if (factory->object) return fail(Status::Unsupported,"factory queue and abandon 004B97C0 are not recovered");
        // Factory::Set 004B9440: Create_One_Of, then Cost_Of.
        const auto kind=t->rtti==Rtti::BuildingType?w::Kind::Structure:t->rtti==Rtti::UnitType?w::Kind::Unit:
            t->rtti==Rtti::InfantryType?w::Kind::Infantry:w::Kind::Aircraft;
        const auto cost=cost_of(c,*t,*h); if (!cost) return fail(Status::NeedsInput,"cost unresolved");
        const auto id=create_object(s,*t,house_id,kind); if (!id) return fail(Status::NeedsInput,"no entity id");
        h=house_of(s,house_id);
        if (!tracking_add(*h,*t)) return fail(Status::NeedsInput,"owned counter unresolved");
        if (kind==w::Kind::Unit && h->unit_count && h->unit_type_counts) { ++*h->unit_count; ++(*h->unit_type_counts)[t->heap_index]; }
        if (kind==w::Kind::Structure) f.buildings[*id]=BuildingState{};
        factory=factory_in_slot(f,house_id,slot);
        factory->object=*id; factory->object_type=t->id; factory->progress=0; factory->balance=*cost;
        factory->suspended=true; factory->interval=0; factory->timer_duration=0; factory->timer_start=frame;
    }
    // Factory::Start 004B9670.
    if (factory->progress==54) return fail(Status::Rejected,"production already complete");
    const auto frames=build_frames(c,f,*t,*house_of(s,house_id));
    if (!frames) return fail(Status::NeedsInput,"build time inputs unresolved");
    factory->suspended=false;
    factory->interval=factory_stage_frames(*frames);                       // 004B9670 writes +34 and +30.
    factory->timer_duration=factory->interval;
    factory->timer_start=frame;
    undo.commit(); factories=std::move(f);
    Outcome o; o.status=Status::Done; o.entity=factory_in_slot(factories,house_id,slot)->object; o.value=*frames; return o;
}

Outcome hold_production(w::Snapshot& s,FactorySet& f,const Catalog& c,std::uint32_t owner,
                        const std::string& type,std::uint64_t frame) {
    if (!runtime_ready(s)) return fail(Status::NeedsInput,"setup is not complete");
    const auto* t=c.find(type);
    if (!t) return fail(Status::Rejected,"unknown type");
    auto* factory=factory_in_slot(f,owner,slot_of(*t));
    if (!factory || !factory->object || factory->object_type!=type || factory->suspended)
        return fail(Status::Rejected,"no running product");
    // 004B9630: preserve progress and balance, discard the partial stage timer.
    factory->suspended=true; factory->interval=0;
    factory->timer_start=frame; factory->timer_duration=0;
    return Outcome{Status::Done,{},factory->object,{}};
}

Outcome cancel_production(w::Snapshot& snapshot,FactorySet& factories,const Catalog& c,std::uint32_t owner,
                          const std::string& type,std::uint64_t frame) {
    if (!runtime_ready(snapshot)) return fail(Status::NeedsInput,"setup is not complete");
    const auto* t=c.find(type);
    if (!t) return fail(Status::Rejected,"unknown type");
    auto* factory=factory_in_slot(factories,owner,slot_of(*t));
    if (!factory || !factory->object || factory->object_type!=type)
        return fail(Status::Rejected,"no product to cancel");
    auto* h=house_of(snapshot,owner);
    const auto* item=entity_of(snapshot,factory->object);
    if (!h || !h->cash || !item || item->lifetime!=w::Lifetime::Created)
        return fail(Status::NeedsInput,"factory product or cash unresolved");
    const auto cost=cost_of(c,*t,*h);
    if (!cost) return fail(Status::NeedsInput,"refund cost unresolved");
    const auto cash=std::int64_t(*h->cash)+*cost-factory->balance;
    if (cash<INT32_MIN || cash>INT32_MAX) return fail(Status::NeedsInput,"refund exceeds cash range");
    w::Undo undo(snapshot); auto f=factories;
    factory=factory_in_slot(f,owner,slot_of(*t));
    h=house_of(snapshot,owner);
    auto* object=entity_of(snapshot,factory->object);
    // 004B97C0 refunds current Cost_Of(owner) minus unpaid Balance, even at 54.
    *h->cash=std::int32_t(cash);
    auto* counts=t->rtti==Rtti::BuildingType?&h->buildings_owned_5420:
        t->rtti==Rtti::UnitType?&h->units_owned_5430:
        t->rtti==Rtti::InfantryType?&h->infantry_owned_5440:&h->aircraft_owned_5450;
    if (!counter_add(*counts,t->heap_index,-1)) return fail(Status::NeedsInput,"owned counter unresolved");
    if (t->rtti==Rtti::UnitType) {
        if (h->unit_count) --*h->unit_count;
        if (h->unit_type_counts) --(*h->unit_type_counts)[t->heap_index];
    }
    f.buildings.erase(object->id);
    object->lifetime=w::Lifetime::Destroyed; object->resolved_hp=0;
    object->active=false; object->in_limbo=true; object->on_map=false;
    object->object_field_74=0; object->object_field_76=1; object->target_id=0;
    factory->object=0; factory->object_type.clear(); factory->balance=0;
    factory->progress=0; factory->suspended=true; factory->interval=0;
    factory->timer_start=frame; factory->timer_duration=0;
    undo.commit(); factories=std::move(f);
    return Outcome{Status::Done,{},{},{}};
}

std::optional<bool> passes_proximity(const w::Snapshot& s,const FactorySet& f,const Catalog& c,
                                     std::uint32_t house_id,const std::string& type,CellXY cell) {
    const auto* t=c.find(type); const auto* h=house_of(s,house_id);
    if (!t || !h || !h->original_index || !f.flags.player_house) return std::nullopt;
    const auto* player=house_of(s,*f.flags.player_house); if (!player || !player->original_index) return std::nullopt;
    if (*h->original_index!=*player->original_index || t->rtti!=Rtti::BuildingType) return true; // 004991D0 early exits.
    if (t->placement.foundation<0 || t->placement.foundation>=22) return std::nullopt;
    const auto [width,height]=kFoundationBounds[std::size_t(t->placement.foundation)];
    const int adj=t->adjacent+1;
    bool ok=false;
    // Each Cell either makes the answer unknown, which ends the scan, or
    // can only set ok. So the visit order does not change the result. Read
    // each row of the area with one ordered map walk instead of one cold
    // map search per Cell.
    const int left=cell.x-adj,right=cell.x-adj+width+2*adj,top=cell.y-adj,bottom=cell.y-adj+height+2*adj;
    for (int y=std::max(top,0);y<std::min(bottom,512);++y) {
        const int first=std::max(left,0),last=std::min(right,512);
        if (first>=last) break;
        const auto row=std::uint32_t(y)*512u;
        for (auto it=s.cells.lower_bound(row+std::uint32_t(first));
             it!=s.cells.end() && it->first<row+std::uint32_t(last);++it) {
            const int x=int(it->first-row);
            if (x>=cell.x && x<cell.x+width && y>=cell.y && y<cell.y+height) continue;
            const auto chain=ground_chain(s,it->second); if (!chain) return std::nullopt;
            const w::Entity* building=nullptr;                             // 004717B0 first Building.
            for (const auto* e:*chain) if (e->kind==w::Kind::Structure) { building=e; break; }
            if (!building) continue;
            const auto* owner=house_of(s,building->house_id); const auto* bt=facts_of(c,*building);
            if (!owner || !owner->original_index || !bt) return std::nullopt;
            if (*owner->original_index==*h->original_index && bt->base_normal) ok=true;
            if (!f.flags.build_off_ally) return std::nullopt;
            if (*f.flags.build_off_ally && bt->eligible_ally && owner->id!=h->id) return std::nullopt; // Alliance mask +5674 not modeled.
        }
    }
    return ok;
}

std::optional<bool> legal_placement(const w::Snapshot& s,const Catalog& c,
                                  const std::string& type,CellXY origin) {
    const auto* t=c.find(type);
    if (!s.geometry || !t || t->rtti!=Rtti::BuildingType) return std::nullopt;
    return legal(s,c,*t,origin);
}

// The checks that place() makes before its first write, in its order. It
// returns the failure that place() would return, or nothing when place()
// reaches its first write. It does not change the world or the factories.
std::optional<Outcome> place_failure(const w::Snapshot& s,const FactorySet& f,const Catalog& c,std::uint32_t house_id,
                                     const std::string& type,CellXY cell) {
    if (!runtime_ready(s)) return fail(Status::NeedsInput,"setup is not complete");
    const auto* t=c.find(type); if (!t || t->rtti!=Rtti::BuildingType) return fail(Status::Rejected,"not a building type");
    const FactoryState* factory=nullptr;
    for (const auto& x:f.factories) if (x.house_id==house_id && x.slot==slot_of(*t)) { factory=&x; break; }
    if (!factory || !factory->object || factory->progress!=54) return fail(Status::Rejected,"no completed building");
    const auto* h=house_of(s,house_id); if (!h) return fail(Status::Rejected,"unknown house");
    const auto builder=who_can_build(s,f,c,*t,*h,false,false);
    if (!builder) return fail(Status::NeedsInput,"builder unresolved");
    if (!*builder) return fail(Status::Rejected,"no builder");
    return unlimbo_failure(s,c,factory->object,cell);
}

Outcome place(w::Snapshot& snapshot,FactorySet& factories,const Catalog& c,std::uint32_t house_id,
              const std::string& type,CellXY cell,std::uint64_t) {
    // A failed placement returns before any write; skip the Undo copy then.
    if (auto failure=place_failure(snapshot,factories,c,house_id,type,cell)) return *failure;
    w::Undo undo(snapshot); auto& s=snapshot; auto f=factories; // Restores unless committed.
    const auto* t=c.find(type); if (!t || t->rtti!=Rtti::BuildingType) return fail(Status::Rejected,"not a building type");
    auto* factory=factory_in_slot(f,house_id,slot_of(*t));
    if (!factory || !factory->object || factory->progress!=54) return fail(Status::Rejected,"no completed building"); // 004B9900.
    const auto object=factory->object;
    const auto* h=house_of(s,house_id); if (!h) return fail(Status::Rejected,"unknown house");
    const auto builder=who_can_build(s,f,c,*t,*h,false,false);             // 005D4500(0,0).
    if (!builder) return fail(Status::NeedsInput,"builder unresolved");
    if (!*builder) return fail(Status::Rejected,"no builder");
    auto placed=unlimbo_building(s,f,c,object,cell);
    if (placed.status==Status::Rejected) return placed;                    // EVA only; nothing changes.
    if (placed.status!=Status::Done) return placed;
    factory=factory_in_slot(f,house_id,slot_of(*t));                       // Factory::Completed 004B9970.
    factory->object=0; factory->object_type.clear(); factory->suspended=true; factory->progress=0; factory->interval=0;
    factory->timer_duration=0;
    // House::Just_Built 004E6F10 is reached for buildings from the construction mission, not here.
    undo.commit(); factories=std::move(f);
    Outcome o; o.status=Status::Done; o.entity=object; return o;
}

Outcome tick(w::Snapshot& snapshot,FactorySet& factories,const Catalog& c,std::uint64_t frame) {
    if (!runtime_ready(snapshot)) return fail(Status::NeedsInput,"setup is not complete");
    // This tick writes House records only. Edit the Snapshot in place and
    // restore its Houses on failure instead of copying every Cell.
    struct Restore {
        w::Snapshot& world; std::vector<w::House> houses; bool keep=false;
        ~Restore() { if (!keep) world.houses=std::move(houses); }
    } restore{snapshot,snapshot.houses};
    auto& s=snapshot; auto f=factories;
    // House::AI 004E41B4 -> Recalc_Power 004F2CC0 for skirmish sessions.
    for (auto& [id,dirty]:f.power_dirty) {
        if (!dirty) continue;
        auto* h=house_of(s,id); if (!h) continue;
        if (!f.flags.session_type || *f.flags.session_type==0) return fail(Status::Unsupported,"campaign power rule not modeled");
        const auto idle=f.power_blackout_idle.find(id);
        if (idle==f.power_blackout_idle.end()) return fail(Status::NeedsInput,"power blackout timer unresolved");
        std::int32_t output=0, drain=0;
        for (const auto* b:house_buildings(s,id)) {
            if (!b->object_field_76 || !b->object_field_74) return fail(Status::NeedsInput,"building map state unresolved");
            if (*b->object_field_76 || !*b->object_field_74) continue;
            const auto* bt=facts_of(c,*b); const auto x=f.buildings.find(b->id);
            if (!bt || x==f.buildings.end() || !b->resolved_hp || bt->strength<=0) return fail(Status::NeedsInput,"building power inputs unresolved");
            if (bt->power_output>0 && x->second.on_530)                      // 00449D40 health-scaled output.
                output+=std::int32_t(double(bt->power_output)*(double(*b->resolved_hp)/double(bt->strength)));
            if (x->second.on_530) drain+=bt->power_drain;                  // 00449DD0.
        }
        if (!idle->second) output=0;
        h->power_output=output; h->power_drain=drain; dirty=false;
        // 004F2CC0 then calls 004B9EB0: every factory of this House takes a
        // new stage rate from the new power. The running stage timer (+30)
        // keeps its duration; the next stage reloads it from the rate.
        for (auto& factory:f.factories) {
            if (factory.house_id!=id) continue;
            int rate=0;
            if (factory.object) {
                const auto* t=c.find(factory.object_type);
                if (!t) return fail(Status::NeedsInput,"factory object type unresolved");
                const auto frames=build_frames(c,f,*t,*h);
                if (!frames) return fail(Status::NeedsInput,"build time inputs unresolved");
                rate=*frames;
            }
            factory.interval=factory_stage_frames(rate);
        }
    }
    // FactoryClass::AI 004B9300 in global factory order, cash only.
    for (auto& factory:f.factories) {
        if (factory.suspended || !factory.object || factory.progress==54) continue;
        if (frame<factory.timer_start) return fail(Status::NeedsInput,"factory timer after frame");
        if (frame-factory.timer_start<std::uint64_t(std::max(factory.timer_duration,0))) continue;
        auto* h=house_of(s,factory.house_id); if (!h || !h->cash) return fail(Status::NeedsInput,"house cash unresolved");
        ++factory.progress; factory.timer_start=frame; factory.timer_duration=factory.interval; // StageClass reload.
        const int divisor=54-factory.progress;
        const int installment=std::min(factory.balance,divisor?factory.balance/divisor:factory.balance);
        if (installment>*h->cash) { --factory.progress; continue; }
        *h->cash-=installment; factory.balance-=installment;
        if (factory.progress==54) factory.suspended=true;
    }
    restore.keep=true; factories=std::move(f);
    return Outcome{Status::Done,{},{},{}};
}
}
