#include "save_game.h"
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ra2::session::save {
namespace {
// Bound input size and allocation counts before allocating containers.
constexpr std::size_t limit=128*1024*1024;
struct Invalid {};
template<class A> void fields(A& a, world::CreatedObject& v) { a(v.kind,v.id); }
template<class A> void fields(A& a, world::House& v) { a(v.id,v.original_index,v.role,v.name,v.country,v.initial_credits,v.cash,v.selected_color,v.palette_color,v.constructor_random,v.unit_registry_size,v.category_flag,v.unit_count,v.unit_type_counts,v.unit_fixed_counts,v.placed_unit_counts,v.constructor_complete,v.reload_multiplier,v.country_index,v.side_index,v.tech_level,v.human_controlled,v.house_flag_135,v.stolen_tech_204,v.stolen_tech_205,v.stolen_mask_208,v.stolen_mask_20c,v.stolen_mask_210,v.stolen_mask_214,v.power_output,v.power_drain,v.buildings_active_5460,v.buildings_owned_5420,v.buildings_ever_54a0,v.units_owned_5430,v.units_ever_54b0,v.infantry_owned_5440,v.infantry_ever_54c0,v.aircraft_owned_5450,v.aircraft_ever_54d0,v.firepower_multiplier,v.attacker_damage_records,v.attacker_damage_count,v.selected_enemy_index,v.observer_flag_13d,v.alliance_mask_5674); }
template<class A> void fields(A& a, world::BaseUnitProfile& v) { a(v.restricted_land,v.movement_mode,v.placement_mode,v.speed_type,v.level_height,v.leptons_per_sight_increase,v.type_flag_aa0,v.unit_flag_32c,v.linked_team,v.type_flag_205,v.unit_flag_32a,v.type_flag_bb0,v.type_flag_bb1); }
template<class A> void fields(A& a, world::Entity& v) { a(v.id,v.house_id,v.next,v.kind,v.lifetime,v.type,v.position,v.cell,v.resolved_hp,v.hull_facing,v.turret_facing,v.threat_field_428,v.resolved_threat,v.original_type_index,v.excluded_from_house_tracking,v.constructor_random,v.on_map,v.occupancy_tracking,v.timer_length,v.timer_active,v.height_byte,v.layer,v.category,v.base_profile,v.constructor_complete,v.maximum_hp,v.active,v.in_limbo,v.collision_eligible,v.has_trigger,v.collision_virtual_position,v.target_id,v.weapon_timer_start_frame,v.weapon_timer_duration,v.object_field_74,v.object_field_76,v.techno_field_370,v.hull_facing_raw,v.locomotor_moving,v.mission,v.unit_field_7d,v.techno_field_1b8,v.foot_field_5a1,v.object_field_75,v.firepower_multiplier,v.veterancy,v.veteran_firepower,v.elite_firepower,v.veteran_rof,v.elite_rof,v.turret_facing_raw,v.burst_index,v.techno_field_13c,v.techno_field_140,v.techno_field_144,v.armor_multiplier,v.house_armor_multiplier,v.techno_field_329); }
template<class A> void fields(A& a, world::CellState& v) { a(v.allocated,v.heads,v.terrain,v.overlay,v.land_wheel,v.usable,v.coarse_category,v.neighbor_count,v.cell_field_11d,v.coarse_cache_height,v.hierarchy_cache_height,v.coarse_category_writes,v.cell_flag_20000,v.ground_occupancy_124,v.cell_flag_100,v.cell_flag_40000,v.cell_flag_200,v.unit_land_speed,v.cell_field_54,v.cell_field_58,v.tube_field_116,v.bridge_occupancy_128,v.wall_owner,v.overlay_data_11e,v.cell_flags_140,v.cell_flag_80,v.cell_field_12c); }
template<class A> void fields(A& a, world::Snapshot& v) { a(v.edition,v.pending_cell_setup,v.pending_setup_cell,v.creation_order,v.houses,v.entities,v.cells,v.geometry,v.session_field_838,v.tube_vector_count,v.random,v.setup_progress,v.cell_order,v.veteran_reload_multiplier,v.veteran_firepower_multiplier,v.fallback_cell_coordinate,v.fallback_overlay_index,v.fallback_overlay_data,v.rules_field_94); }
template<class A> void fields(A& a, sim::CoarseOverlayState& v) { a(v.flag_22d,v.flag_2a8,v.flag_2b5,v.flag_2b4,v.land_wheel); }
template<class A> void fields(A& a, sim::CoarseOccupantState& v) { a(v.runtime_type,v.building_flag_16c0,v.building_flag_16bf,v.house_flag_1fa,v.building_state_618,v.terrain_field_2a8,v.terrain_field_2ac); }
template<class A> void fields(A& a, sim::TerrainCell& v) { a(v.cell,v.tile_index,v.tile_replacement_resolved,v.resolved_tile_index,v.subtile,v.tmp_terrain,v.land_type,v.height_level,v.ramp_index,v.floor_world_height,v.bridge,v.overlay_type_index); }
template<class A> void fields(A& a, sim::RandomState& v) { a(v.table,v.first,v.second); }
template<class A> void fields(A& a, sim::production::FactoryState& v) { a(v.house_id,v.slot,v.object,v.object_type,v.progress,v.balance,v.interval,v.timer_start,v.timer_duration,v.suspended); }
template<class A> void fields(A& a, sim::production::SessionFlags& v) { a(v.session_type,v.super_weapons,v.build_off_ally,v.player_house); }
template<class A> void fields(A& a, sim::production::BuildingState& v) { a(v.on_530,v.primary,v.mission_a0); }
template<class A> void fields(A& a, sim::production::FactorySet& v) { a(v.flags,v.buildings,v.factories,v.house_factory_counts,v.power_dirty,v.power_blackout_idle); }
template<class A> void fields(A& a, StartPoint& v) { a(v.x,v.y); }
template<class A> void fields(A& a, MapRectangle& v) { a(v.x,v.y,v.width,v.height); }
template<class A> void fields(A& a, PlacementGeometry& v) { a(v.width,v.height,v.local); }
template<class A> void fields(A& a, sim::Coord& v) { a(v.x,v.y,v.z); }
template<class A> void fields(A& a, ra2map::Cell& v) { a(v.x,v.y); }
template<class A> void fields(A& a, weapons::CellContentHeads& v) { a(v.ground_head,v.bridge_head); }
template<class A> void fields(A& a, PendingMove& v) { a(v.goal,v.speed,v.path); }
template<class A> void fields(A& a, SavedGame& v) { a(v.frame,v.world,v.factories,v.moves,v.movement_last_frame); }

template<bool Reading> struct Archive {
    std::string bytes;
    std::size_t pos=0;
    void byte(std::uint8_t& v) {
        if constexpr (Reading) {
            if (pos==bytes.size()) throw Invalid{};
            v=static_cast<std::uint8_t>(bytes[pos++]);
        } else bytes.push_back(static_cast<char>(v));
    }
    template<class... T> void operator()(T&... v) { (value(v),...); }
    template<class T> void value(T& v) {
        if constexpr (std::is_same_v<T,bool>) {
            std::uint8_t b=v;byte(b);if(b>1) throw Invalid{};v=b;
        } else if constexpr (std::is_enum_v<T>) {
            std::int32_t n=static_cast<std::int32_t>(v);value(n);v=static_cast<T>(n);
        } else if constexpr (std::is_integral_v<T>) {
            using U=std::make_unsigned_t<T>;U bits=static_cast<U>(v);
            if constexpr (Reading) bits=0;
            for(unsigned i=0;i<sizeof(T);++i) {
                std::uint8_t b=static_cast<std::uint8_t>(bits>>(i*8));byte(b);
                if constexpr (Reading) bits|=U(b)<<(i*8);
            }
            if constexpr (Reading) std::memcpy(&v,&bits,sizeof(T));
        } else if constexpr (std::is_floating_point_v<T>) {
            using U=std::conditional_t<sizeof(T)==4,std::uint32_t,std::uint64_t>;
            U bits;std::memcpy(&bits,&v,sizeof(T));value(bits);std::memcpy(&v,&bits,sizeof(T));
        } else fields(*this,v);
    }
    void count(std::uint32_t& n) {
        value(n);
        if(n>262144 || (Reading && n>bytes.size()-pos)) throw Invalid{};
    }
    void value(std::string& s) {
        std::uint32_t n=static_cast<std::uint32_t>(s.size());count(n);
        if constexpr (Reading) {s.assign(bytes,pos,n);pos+=n;} else bytes+=s;
    }
    template<class T> void value(std::optional<T>& v) {
        bool present=v.has_value();value(present);
        if constexpr (Reading) {if(present) v.emplace();else v.reset();}
        if(present) value(*v);
    }
    template<class T,std::size_t N> void value(std::array<T,N>& v) {for(auto& x:v)value(x);}
    template<class T,class U> void value(std::pair<T,U>& v) {value(v.first);value(v.second);}
    template<class T> void value(std::vector<T>& v) {
        std::uint32_t n=static_cast<std::uint32_t>(v.size());count(n);
        if constexpr (Reading) v.resize(n);
        for(auto& x:v)value(x);
    }
    template<class K,class V> void value(std::map<K,V>& v) {
        std::uint32_t n=static_cast<std::uint32_t>(v.size());count(n);
        if constexpr (Reading) {
            v.clear();for(std::uint32_t i=0;i<n;++i) {
                K k{};V x{};value(k);value(x);
                if(!v.emplace(k,std::move(x)).second) throw Invalid{};
            }
        } else for(auto& entry:v) {K k=entry.first;value(k);value(entry.second);}
    }
};
std::uint64_t checksum(const std::string& s) {
    std::uint64_t h=14695981039346656037ull;
    for(unsigned char c:s) {h^=c;h*=1099511628211ull;}return h;
}
}
std::string encode(const SavedGame& state) {
    Archive<false> a;auto copy=state;
    std::string magic="RA2SAVE";std::uint32_t version=format_version;
    a(magic,version,copy);
    auto hash=checksum(a.bytes);a(hash);return a.bytes;
}
bool decode(const std::string& bytes,SavedGame& out) {
    if(bytes.size()>limit || bytes.size()<8) return false;
    try {
        Archive<true> a{bytes};SavedGame state;std::string magic;std::uint32_t version=0;
        a(magic,version);
        if(magic!="RA2SAVE" || version!=format_version) return false;
        a(state);
        const auto payload=a.pos;std::uint64_t hash=0;a(hash);
        if(a.pos!=bytes.size() || hash!=checksum(bytes.substr(0,payload))) return false;
        if(state.world.random.first>=250 || state.world.random.second>=250) return false;
        out=std::move(state);return true;
    } catch(const Invalid&) {return false;} catch(const std::exception&) {return false;}
}
bool save_file(const std::string& path,const SavedGame& state) {
    const auto bytes=encode(state);std::ofstream f(path,std::ios::binary|std::ios::trunc);
    f.write(bytes.data(),bytes.size());f.close();return bool(f);
}
std::optional<SavedGame> load_file(const std::string& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f) return {};
    const auto size=f.tellg();if(size<0 || size>static_cast<std::streamoff>(limit)) return {};
    std::string bytes(static_cast<std::size_t>(size),'\0');f.seekg(0);
    if(!f.read(bytes.data(),bytes.size())) return {};
    SavedGame state;if(!decode(bytes,state)) return {};return state;
}
}
