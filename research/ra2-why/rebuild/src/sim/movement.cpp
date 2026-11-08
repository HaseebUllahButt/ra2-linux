#include "sim.h"
#include "fp.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cctype>
#include <stdexcept>
namespace ra2::sim {
namespace {
std::uint32_t key(ra2map::Cell c) { return c.x|(static_cast<std::uint32_t>(c.y)<<16); }
std::string field(const Fields& f,const std::string& name) {
    auto it=f.find(name); return it==f.end()?"":it->second;
}
bool equal_ascii(std::string a,std::string b) {
    auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};
    std::transform(a.begin(),a.end(),a.begin(),lower); std::transform(b.begin(),b.end(),b.begin(),lower);
    return a==b;
}
Result unsupported(const std::string& text) { return {0,Error::Unsupported,0,text}; }
}
void Simulation::load_ground_assets(const std::string& executable,const Ini& art) {
    if (state_.frame || !queue_.empty()) throw std::logic_error("load original ground assets before advancing or issuing commands");
    auto tables=DriveTables::from_executable(executable);
    std::map<std::string,FoundationBounds> foundations;
    // 00474DA0's 22 original Foundation names; 0045EC90/0045ECA0 supply these bounds.
    const std::pair<const char*,FoundationBounds> names[]={
        {"1x1",{1,1}},{"2x1",{2,1}},{"1x2",{1,2}},{"2x2",{2,2}},{"2x3",{2,3}},
        {"3x2",{3,2}},{"3x3",{3,3}},{"3x5",{3,5}},{"4x2",{4,2}},{"3x3Refinery",{3,3}},
        {"1x3",{1,3}},{"3x1",{3,1}},{"4x3",{4,3}},{"1x4",{1,4}},{"1x5",{1,5}},
        {"2x6",{2,6}},{"2x5",{2,5}},{"5x3",{5,3}},{"4x4",{4,4}},{"3x4",{3,4}},{"6x4",{6,4}},{"0x0",{0,0}}};
    for (const auto& entry:types_) if (entry.second.category=="BuildingTypes") {
        auto image=field(entry.second.fields,"Image"); if (image.empty()) image=entry.first;
        const auto authored=art.read_string(image,"Foundation","1x1");
        auto bounds=FoundationBounds{1,1}; // Original constructor 0045DD90, Foundation=0.
        for (const auto& name:names) if (equal_ascii(authored,name.first)) { bounds=name.second; break; }
        foundations.emplace(entry.first,bounds);
    }
    foundations_=std::move(foundations); drive_tables_=std::move(tables);
}
Result Simulation::start_move(const Command& c) {
    if (!c.entity || c.entity>state_.entities.size()) return {0,Error::InvalidEntity,0,"Move entity does not exist"};
    auto& e=state_.entities[c.entity-1];
    if (!drive_tables_) return unsupported("Move needs original drive assets");
    if (e.dead || e.kind!=Kind::Unit || !e.position || !e.hp) return unsupported("Move needs a living vehicle with resolved world position and health");
    if (e.moving || (last_move_completion_.count(e.id) && last_move_completion_.at(e.id)==state_.frame))
        return unsupported("replanning and immediate segment chaining are not recovered");
    if (e.mission!="Guard" && e.mission!="Area Guard") return unsupported("Move mission transitions from this initial mission are not recovered");
    if (!movements_.empty()) return unsupported("simultaneous drive collision handling is not recovered");
    const auto& type=types_.at(e.type);
    if (Ini::bool_value(field(type.fields,"Naval")).value_or(false)) return unsupported("naval drive movement is not recovered");
    if (!type.speed || !type.strength || *e.hp!=*type.strength) return unsupported("Move needs original Speed and full vehicle health");
    if (field(type.fields,"Locomotor")!="{4A582741-9839-11d1-B709-00A024DDAFD1}")
        return unsupported("only the original drive locomotor is recovered");
    if (Ini::bool_value(field(type.fields,"Harvester")).value_or(false) || Ini::bool_value(field(type.fields,"Weeder")).value_or(false))
        return unsupported("harvester idle missions are not recovered");
    const auto accelerates=Ini::bool_value(field(type.fields,"Accelerates"));
    if (!accelerates || *accelerates) return unsupported("accelerating drive movement is not recovered");
    if (!e.map_veterancy || *e.map_veterancy || !e.map_group || *e.map_group!=-1 || !e.map_follow || *e.map_follow!=-1 || !e.map_above_bridge || *e.map_above_bridge || e.tag!="None")
        return unsupported("Move does not yet execute veterancy, groups, follow links, bridges, or map tags");
    const auto zone=field(type.fields,"MovementZone");
    const auto speed_type=field(type.fields,"SpeedType");
    if ((!zone.empty() && zone!="Normal" && zone!="Destroyer") || (!speed_type.empty() && speed_type!="Foot" && speed_type!="Track" && speed_type!="Wheel"))
        return unsupported("this movement zone or speed type is not recovered");
    const auto house=std::find_if(state_.houses.begin(),state_.houses.end(),[&](const House& h){return h.name==e.house;});
    if (house==state_.houses.end() || house->country.empty()) return unsupported("vehicle country speed modifier is unresolved");
    const auto country=country_fields_.find(house->country);
    if (country==country_fields_.end()) return unsupported("vehicle country rules are unresolved");
    double house_speed=1; // HouseType constructor 005113F0 initializes +12C to float one.
    const auto authored=field(country->second,"SpeedUnitsMult");
    if (!authored.empty()) {
        const auto parsed=Ini::double_value(authored); if (!parsed) return unsupported("invalid country SpeedUnitsMult");
        house_speed=static_cast<float>(*parsed);
    }
    const auto resolved_speed=fp::multiply(original_rule_speed(*type.speed),house_speed);
    if (!std::isfinite(resolved_speed) || resolved_speed<1 || resolved_speed>255)
        return unsupported("resolved original drive speed is outside the verified 1..255 range");
    const int speed=static_cast<int>(resolved_speed);
    if (!house->iq) return unsupported("vehicle idle mission needs the original house IQ");
    DriveIdleInputs idle; idle.edition=drive_tables_->edition(); idle.house_iq=*house->iq; idle.guard_area_iq=guard_area_iq_;
    idle.default_to_guard_area=Ini::bool_value(field(type.fields,"DefaultToGuardArea")).value_or(false);
    const auto idle_mission=type.primary.empty()?DriveIdleMission::Guard:original_armed_drive_idle(idle);
    const auto destination=terrain_index_.find(key(c.destination));
    if (destination==terrain_index_.end() || !terrain_[destination->second].floor_world_height) return unsupported("Move destination floor is missing");
    Coord goal{c.destination.x*256+128,c.destination.y*256+128,*terrain_[destination->second].floor_world_height};
    std::optional<DriveSegment> planned;
    try {
        if (c.path_directions.empty()) planned=DriveSegment::begin(*e.position,goal,static_cast<unsigned>(e.facing),speed,*drive_tables_);
        else planned=DriveSegment::begin_route(*e.position,goal,static_cast<unsigned>(e.facing),speed,c.path_directions,*drive_tables_);
    } catch (const std::exception& error) { return unsupported(error.what()); }
    for (const auto& pair:planned->swept_cells(*drive_tables_)) {
        if (pair[0]<0 || pair[0]>65535 || pair[1]<0 || pair[1]>65535) return unsupported("Move curve leaves the map coordinate range");
        const ra2map::Cell crossed{static_cast<std::uint16_t>(pair[0]),static_cast<std::uint16_t>(pair[1])};
        const auto found=terrain_index_.find(key(crossed));
        if (found==terrain_index_.end()) return unsupported("Move swept-cell terrain is missing");
        const auto& cell=terrain_[found->second];
        if (!cell.land_type || (*cell.land_type!=0 && *cell.land_type!=1) || !cell.ramp_index || *cell.ramp_index || !cell.bridge || *cell.bridge || !cell.overlay_type_index || *cell.overlay_type_index!=-1 || !cell.floor_world_height)
            return unsupported("Move currently supports resolved clear or road flat cells without overlays or bridges");
        if (*cell.land_type==0 && !cell.tile_replacement_resolved)
            return unsupported("clear-cell Move needs original ordered tile setup through TheaterInputs");
        if (*cell.floor_world_height!=e.position->z) return unsupported("Move swept-cell floor levels differ");
        const auto& land_rules=*cell.land_type==0?clear_fields_:road_fields_;
        for (const auto* category:{"Foot","Track","Wheel"}) {
            const auto multiplier=Ini::double_value(field(land_rules,category));
            if (!multiplier || *multiplier!=1) return unsupported("Move currently needs original terrain multipliers of one");
        }
        if (terrain_blockers_.count(key(crossed))) return unsupported("terrain-object occupancy is present");
        // Conservative bounds also cover the cells cut by a turn, not only the supplied path cells.
        for (const auto& blocker:state_.entities) {
            if (blocker.dead || blocker.id==e.id) continue;
            if (blocker.kind==Kind::Structure) {
                const auto foundation=foundations_.find(blocker.type);
                if (foundation==foundations_.end()) return unsupported("building foundation metadata is unresolved");
                const auto bounds=foundation->second;
                if (bounds.width && bounds.height && crossed.x>=blocker.cell.x && crossed.x<blocker.cell.x+bounds.width && crossed.y>=blocker.cell.y && crossed.y<blocker.cell.y+bounds.height+1)
                    return unsupported("Move swept cell intersects a conservative original building foundation bound");
            } else if (key(blocker.cell)==key(crossed) || (blocker.move_goal && key(*blocker.move_goal)==key(crossed)))
                return unsupported("vehicle or infantry collision handling is not recovered");
        }
    }
    movements_.emplace(e.id,std::move(*planned));
    completion_missions_[e.id]=idle_mission==DriveIdleMission::Guard?"Guard":"Area Guard";
    e.moving=true; e.move_goal=c.destination; e.mission="Move";
    return {};
}
void Simulation::update_movement() {
    if (!drive_tables_) return;
    for (auto it=movements_.begin();it!=movements_.end();) {
        auto& e=state_.entities.at(it->first-1);
        if (e.dead) { e.moving=false; e.move_goal.reset(); completion_missions_.erase(e.id); it=movements_.erase(it); continue; }
        it->second.tick(*drive_tables_); e.position=it->second.current; e.facing=static_cast<int>(it->second.facing);
        e.cell={static_cast<std::uint16_t>(e.position->x/256),static_cast<std::uint16_t>(e.position->y/256)};
        if (!it->second.active) {
            e.moving=false; e.move_goal.reset(); e.mission=completion_missions_.at(e.id); completion_missions_.erase(e.id);
            last_move_completion_[e.id]=state_.frame; it=movements_.erase(it);
        } else ++it;
    }
}
}
