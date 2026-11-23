#pragma once
// Stable world hash for regression and benchmark checks. It reads explicit
// values only, never padding or pointers. It extends tools/headless_match.cpp
// with resource cells, facings and maximum hit points.
#include "world.h"
#include <cstdint>
#include <sstream>

namespace ra2::session::world {
inline std::uint64_t world_hash(const Snapshot& world) {
    std::ostringstream out;
    out.precision(17);
    auto opt=[&](const auto& v) { out<<bool(v)<<':'; if(v) out<<+*v; out<<','; };
    out<<int(world.setup_progress)<<world.pending_cell_setup;
    out<<world.random.first<<','<<world.random.second<<',';
    for(auto v:world.random.table) out<<v<<',';
    for(const auto& h:world.houses) {
        out<<h.id<<':'<<h.name<<':'<<h.country;opt(h.cash);opt(h.power_output);opt(h.power_drain);
        opt(h.human_controlled);
    }
    for(const auto& e:world.entities) {
        out<<e.id<<','<<e.house_id<<','<<int(e.kind)<<','<<int(e.lifetime)<<':'<<e.type<<';';
        opt(e.next);opt(e.resolved_hp);opt(e.maximum_hp);opt(e.target_id);opt(e.weapon_timer_start_frame);
        opt(e.weapon_timer_duration);opt(e.mission);opt(e.on_map);opt(e.active);opt(e.in_limbo);
        opt(e.locomotor_moving);opt(e.hull_facing);opt(e.turret_facing);
        if(e.cell) out<<e.cell->x<<','<<e.cell->y;
        if(e.position) out<<e.position->x<<','<<e.position->y<<','<<e.position->z;
    }
    for(const auto& [id,c]:world.cells) {
        out<<id<<';';opt(c.ground_occupancy_124);opt(c.bridge_occupancy_128);opt(c.overlay_data_11e);
        if(c.heads) out<<c.heads->ground_head<<','<<c.heads->bridge_head;
        if(c.terrain) {opt(c.terrain->overlay_type_index);opt(c.terrain->land_type);}
    }
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:out.str()) {hash^=c;hash*=1099511628211ull;}
    return hash;
}
}
