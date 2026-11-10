#include "prototype_aircraft.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
namespace {
namespace sw = session::world;
using sw::Kind;
using sw::Lifetime;
using Cell = ra2map::Cell;

constexpr std::int32_t cell_leptons = 256; // World units per cell side.
// TODO-ORIGINAL: climb and descent use one height pixel per frame in RA1
// AircraftClass::Landing_Takeoff_AI. The RA2 rate is unresolved.
constexpr std::int32_t climb_rate = 15;
// TODO-ORIGINAL: RA1 BUILDING.CPP Mission_Reload multiplies ReloadRate by
// TICKS_PER_MINUTE (900 at 15 fps). The RA2 frame rate is unresolved.
constexpr std::int32_t ticks_per_minute = 900;

bool equal_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

std::uint32_t key(Cell c) { return std::uint32_t(c.y) * 512 + std::uint32_t(c.x); }

std::uint64_t squared_xy(const sim::Coord& a, const sim::Coord& b) {
    const std::int64_t dx = std::int64_t(a.x) - b.x, dy = std::int64_t(a.y) - b.y;
    return std::uint64_t(dx * dx + dy * dy);
}

Coord cell_center(Cell c) {
    return {int(c.x) * cell_leptons + cell_leptons / 2, int(c.y) * cell_leptons + cell_leptons / 2, 0};
}

bool same(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }

const sw::Entity* find_entity(const sw::Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const auto& entity) { return entity.id == id; });
    return found == world.entities.end() ? nullptr : &*found;
}

sw::Entity* find_entity(sw::Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const auto& entity) { return entity.id == id; });
    return found == world.entities.end() ? nullptr : &*found;
}

// Placed and alive, with the same value defaults as the shared world users.
bool live(const sw::Entity& entity) {
    return entity.id != 0 && entity.lifetime == Lifetime::Placed && entity.cell &&
        entity.resolved_hp.value_or(0) > 0 && entity.active.value_or(false) &&
        !entity.in_limbo.value_or(true) && entity.on_map != false;
}

std::optional<std::int32_t> floor_under(const sw::Snapshot& world, Cell cell) {
    const auto found = world.cells.find(key(cell));
    if (found == world.cells.end() || !found->second.terrain) return {};
    return found->second.terrain->floor_world_height;
}

bool dock_matches(const PrototypeAircraftRules& rules, const sw::Entity& pad) {
    return std::any_of(rules.dock.begin(), rules.dock.end(),
        [&](const auto& name) { return equal_name(name, pad.type); });
}

bool armed(std::int32_t max_ammo, std::int32_t ammo) { return max_ammo < 0 || ammo > 0; }

// 8-way facing index for the travel direction. Same sign method as the
// movement prototype; TODO-ORIGINAL: original facing interpolation.
int facing_index(std::int64_t dx, std::int64_t dy) {
    const int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
    static constexpr int table[3][3] = {{7, 0, 1}, {6, -1, 2}, {5, 4, 3}}; // [sy+1][sx+1].
    return table[sy + 1][sx + 1];
}

void face(sw::Entity& actor, int index) {
    if (index < 0) return;
    actor.hull_facing = index * 32;
    actor.hull_facing_raw = static_cast<std::uint16_t>(index * 8192);
}

void apply_damage(sw::Entity& victim, std::int32_t damage) {
    // TODO-ORIGINAL: direct hit-point subtraction, not original bullets,
    // warheads or armour resolution.
    victim.resolved_hp = std::max(0, victim.resolved_hp.value_or(0) - damage);
}

// Base AircraftClass::UnmarkAllOccupationBits 005D49E0 separates flight
// from ground occupancy. TODO-ORIGINAL: air cell lists and dock footprints
// remain separate; this adapter only releases the old ground reservation.
std::optional<std::string> detach_ground(sw::Snapshot& world, sw::Entity& actor) {
    if (actor.occupancy_tracking == false) return {};
    if (!actor.next || !actor.cell) return "aircraft ground link is unresolved";
    const auto at = world.cells.find(key(*actor.cell));
    // Isolated flight fixtures need no ground list. A nonzero next still
    // requires a known list, so a damaged chain never silently disappears.
    if (at == world.cells.end() || !at->second.heads) {
        if (*actor.next) return "aircraft ground cell is unresolved";
        actor.occupancy_tracking = false;
        return {};
    }
    auto& cell = at->second;
    std::vector<sw::Entity*> chain;
    std::set<std::uint32_t> seen;
    for (auto id = cell.heads->ground_head; id;) {
        if (!seen.insert(id).second) return "aircraft ground chain is cyclic";
        auto* entry = find_entity(world, id);
        if (!entry || !entry->next) return "aircraft ground chain is unresolved";
        chain.push_back(entry);
        id = *entry->next;
    }
    const auto found = std::find(chain.begin(), chain.end(), &actor);
    if (found == chain.end()) {
        if (*actor.next) return "aircraft ground chain does not contain actor";
        actor.occupancy_tracking = false;
        return {};
    }
    if (!cell.ground_occupancy_124) return "aircraft ground occupancy is unresolved";
    if (found == chain.begin()) cell.heads->ground_head = *actor.next;
    else (*std::prev(found))->next = *actor.next;
    // Infantry marks only its own spot. Map actors and vehicles can reserve
    // the whole cell. Keep a shared bit when a remaining occupant uses it.
    const auto bit = [](const sw::Entity& e) -> std::uint32_t {
        if (e.kind == Kind::Infantry && e.position) {
            static constexpr int x[3] = {192,64,192}, y[3] = {64,192,192};
            for (int i=0;i<3;++i)
                if (e.position->x%256==x[i] && e.position->y%256==y[i]) return 1u<<(i+2);
        }
        return 0x20u;
    };
    std::uint32_t keep = 0;
    for (const auto* entry : chain) if (entry != &actor) keep |= bit(*entry);
    *cell.ground_occupancy_124 &= ~(bit(actor) & ~keep);
    actor.next = 0;
    actor.occupancy_tracking = false;
    return {};
}

PrototypeAircraftResult fail(PrototypeAircraftStatus status, std::string detail) {
    PrototypeAircraftResult result;
    result.status = status;
    result.detail = std::move(detail);
    return result;
}

PrototypeAircraftResult done() { return {}; }
} // namespace

// Pick the mission phase for a new or re-issued attack target.
void PrototypeAircraft::retarget(Flight& flight) {
    const bool full = flight.rules.ammo >= 0 && flight.ammo >= flight.rules.ammo;
    switch (flight.phase) {
        case AircraftPhase::Docked:
            if (full) flight.phase = AircraftPhase::TakeOff;
            break;
        case AircraftPhase::Hover:
            if (armed(flight.rules.ammo, flight.ammo)) flight.phase = AircraftPhase::FlyToTarget;
            break;
        case AircraftPhase::FlyToTarget:
        case AircraftPhase::Attack:
            if (!armed(flight.rules.ammo, flight.ammo)) {
                flight.target.reset();
                flight.phase = flight.rules.family == PrototypeAircraftRules::Family::Plane
                    ? AircraftPhase::ReturnToPad
                    : AircraftPhase::Hover;
            }
            break;
        case AircraftPhase::TakeOff:
        case AircraftPhase::ReturnToPad:
        case AircraftPhase::Land:
            break; // Finish the climb, descent or return first.
    }
}

PrototypeAircraftRules PrototypeAircraftRules::load(const rules::IniFile& rules,
                                                    std::string_view type) {
    if (type != "ORCA" && type != "BEAG" && type != "ZEP" && type != "JUMPJET")
        throw std::runtime_error("Aircraft prototype supports ORCA, BEAG, ZEP and JUMPJET");
    PrototypeAircraftRules result;
    result.type = std::string(type);
    result.speed = rules.read_integer(type, "Speed", 0);
    result.strength = rules.read_integer(type, "Strength", 0);
    result.ammo = rules.read_integer(type, "Ammo", -1);
    result.landable = rules.read_bool(type, "Landable", false);
    result.dock = rules.read_list(type, "Dock");
    result.hover_height = rules.read_integer(type, "JumpjetHeight", 0);
    if (type == "JUMPJET" || rules.read_bool(type, "JumpJet", false))
        result.family = PrototypeAircraftRules::Family::Hover;
    else if (type == "ZEP" || rules.read_bool(type, "BalloonHover", false))
        result.family = PrototypeAircraftRules::Family::Airship;
    else if (result.landable && !result.dock.empty())
        result.family = PrototypeAircraftRules::Family::Plane;
    else
        throw std::runtime_error("Unsupported aircraft family: " + std::string(type));
    const auto primary = rules.read_string(type, "Primary");
    if (primary.empty()) throw std::runtime_error("Aircraft needs a Primary weapon: " + std::string(type));
    result.range = rules.read_double(primary, "Range", 0);
    result.rof = rules.read_integer(primary, "ROF", 0);
    result.damage = rules.read_integer(primary, "Damage", 0) *
        std::max(1, rules.read_integer(primary, "Burst", 1));
    if (result.speed <= 0 || result.strength <= 0 || result.rof <= 0 || result.range <= 0 || result.damage <= 0)
        throw std::runtime_error("Missing or invalid aircraft rules: " + std::string(type));
    if (result.family == PrototypeAircraftRules::Family::Plane) {
        if (result.ammo <= 0) throw std::runtime_error("Plane needs Ammo=: " + std::string(type));
    } else if (result.hover_height <= 0)
        throw std::runtime_error("Airship or hover infantry needs JumpjetHeight=: " + std::string(type));
    return result;
}

bool is_reload_pad(const rules::IniFile& rules, std::string_view building_type) {
    return rules.read_bool(building_type, "UnitReload", false);
}

PrototypeAircraft::PrototypeAircraft(const rules::IniFile& base_rules, bool external_combat)
    : rules_(base_rules), external_combat_(external_combat) {
    flight_level_ = base_rules.read_integer("General", "FlightLevel", 1500);
    reload_rate_ = base_rules.read_double("General", "ReloadRate", 0.3);
    if (flight_level_ <= 0 || reload_rate_ <= 0)
        throw std::runtime_error("Missing or invalid aircraft general rules");
}

bool PrototypeAircraft::ready_to_fire(std::uint32_t aircraft) const {
    const auto found = flights_.find(aircraft);
    if (found == flights_.end()) return false;
    const auto& flight = found->second;
    return flight.phase == AircraftPhase::Attack && armed(flight.rules.ammo, flight.ammo);
}

void PrototypeAircraft::fired(std::uint32_t aircraft) {
    if (!ready_to_fire(aircraft)) return;
    auto& flight = flights_.at(aircraft);
    if (flight.rules.ammo >= 0) --flight.ammo;
    if (flight.rules.ammo >= 0 && flight.ammo <= 0) {
        flight.target.reset();
        flight.phase = flight.rules.family == PrototypeAircraftRules::Family::Plane
            ? AircraftPhase::ReturnToPad
            : AircraftPhase::Hover;
    }
}

std::optional<AircraftPhase> PrototypeAircraft::phase(std::uint32_t aircraft) const {
    const auto found = flights_.find(aircraft);
    return found == flights_.end() ? std::nullopt : std::optional{found->second.phase};
}

std::optional<std::int32_t> PrototypeAircraft::ammo(std::uint32_t aircraft) const {
    const auto found = flights_.find(aircraft);
    return found == flights_.end() ? std::nullopt : std::optional{found->second.ammo};
}

std::optional<std::int32_t> PrototypeAircraft::cruise_height(const sw::Snapshot& world,
    const sw::Entity& actor, const PrototypeAircraftRules& rules) const {
    const auto floor = floor_under(world, *actor.cell);
    if (!floor) return {};
    return *floor + (rules.family == PrototypeAircraftRules::Family::Plane ? flight_level_ : rules.hover_height);
}

std::int32_t PrototypeAircraft::reload_frames(const sw::Snapshot& world, const sw::Entity& actor,
                                              const Flight&) const {
    double fraction = 1;
    const auto house = std::find_if(world.houses.begin(), world.houses.end(),
        [&](const auto& item) { return item.id == actor.house_id; });
    if (house != world.houses.end() && house->power_output && house->power_drain &&
        *house->power_drain > 0)
        fraction = std::clamp(double(*house->power_output) / double(*house->power_drain), 0.5, 1.0);
    // RA1 BUILDING.CPP Mission_Reload: Inverse(power) * ReloadRate * TICKS_PER_MINUTE.
    return static_cast<std::int32_t>(std::lround(reload_rate_ * ticks_per_minute / fraction));
}

bool PrototypeAircraft::dock_free(const std::map<std::uint32_t, Flight>& flights,
                                  const sw::Entity& pad) const {
    std::size_t used = 0;
    for (const auto& [id, flight] : flights)
        if (flight.pad == pad.id && (flight.phase == AircraftPhase::Docked || flight.phase == AircraftPhase::Land))
            ++used;
    const auto capacity = rules_.read_integer(pad.type, "NumberOfDocks", 1);
    return used < static_cast<std::size_t>(std::max(capacity, 0));
}

std::optional<std::uint32_t> PrototypeAircraft::pick_pad(const sw::Snapshot& world,
    const PrototypeAircraftRules& rules, std::uint32_t house_id,
    const std::map<std::uint32_t, Flight>& flights, const sim::Coord& from) const {
    const sw::Entity* best = nullptr;
    std::uint64_t best_range = ~std::uint64_t{0};
    for (const auto& entity : world.entities) {
        if (!live(entity) || entity.kind != sw::Kind::Structure || entity.house_id != house_id) continue;
        if (!is_reload_pad(rules_, entity.type) || !dock_matches(rules, entity)) continue;
        if (!dock_free(flights, entity)) continue;
        const auto range = squared_xy(from, cell_center(*entity.cell));
        if (range < best_range) {
            best = &entity;
            best_range = range;
        }
    }
    return best ? std::optional{best->id} : std::nullopt;
}

bool PrototypeAircraft::fly_toward(sw::Entity& actor, Flight& flight,
                                   const sim::Coord& destination) const {
    const auto dx = std::int64_t(destination.x) - actor.position->x;
    const auto dy = std::int64_t(destination.y) - actor.position->y;
    const auto distance = std::max(std::abs(dx), std::abs(dy));
    face(actor, facing_index(dx, dy));
    if (distance <= flight.rules.speed) {
        actor.position->x = destination.x;
        actor.position->y = destination.y;
        return true;
    }
    actor.position->x += static_cast<std::int32_t>(dx * flight.rules.speed / distance);
    actor.position->y += static_cast<std::int32_t>(dy * flight.rules.speed / distance);
    return false;
}

PrototypeAircraftResult PrototypeAircraft::issue(const sw::Snapshot& world, std::uint32_t aircraft,
                                                 std::uint32_t target) {
    if (aircraft == 0 || target == 0 || aircraft == target)
        return fail(PrototypeAircraftStatus::Invalid, "prototype issue needs two different entity ids");
    const auto* entity = find_entity(world, aircraft);
    if (!entity) return fail(PrototypeAircraftStatus::Invalid, "prototype aircraft is missing from the world");
    const auto* victim = find_entity(world, target);
    if (!victim || !live(*victim) || victim->kind == sw::Kind::Terrain)
        return fail(PrototypeAircraftStatus::Invalid, "prototype attack target is missing, dead or terrain");
    PrototypeAircraftRules rules;
    try {
        rules = PrototypeAircraftRules::load(rules_, entity->type);
    } catch (const std::runtime_error& error) {
        return fail(PrototypeAircraftStatus::Invalid, error.what());
    }
    if ((rules.family == PrototypeAircraftRules::Family::Hover) != (entity->kind == sw::Kind::Infantry))
        return fail(PrototypeAircraftStatus::Invalid, "prototype type is not a flying entity");
    if (!live(*entity) || !entity->position)
        return fail(PrototypeAircraftStatus::NeedsInput, "prototype aircraft state is unresolved");

    const auto found = flights_.find(aircraft);
    if (found != flights_.end()) {
        found->second.target = target;
        found->second.move_goal.reset();
        retarget(found->second);
        return done();
    }
    Flight flight;
    flight.rules = rules;
    flight.target = target;
    flight.ammo = rules.ammo;
    const auto cruise = cruise_height(world, *entity, rules);
    if (!cruise) return fail(PrototypeAircraftStatus::NeedsInput, "prototype cruise height is unresolved");
    flight.cruise_z = *cruise;
    if (rules.family == PrototypeAircraftRules::Family::Plane && entity->position->z < *cruise) {
        // A grounded plane rests on a pad or needs one for its return.
        bool docked = false;
        for (const auto& candidate : world.entities) {
            if (!live(candidate) || candidate.kind != sw::Kind::Structure ||
                !candidate.cell || !same(*candidate.cell, *entity->cell) ||
                !is_reload_pad(rules_, candidate.type) || !dock_matches(rules, candidate)) continue;
            flight.pad = candidate.id;
            flight.phase = AircraftPhase::Docked;
            docked = true;
            break;
        }
        if (!docked) {
            flight.phase = AircraftPhase::TakeOff;
            if (const auto reserved = pick_pad(world, rules, entity->house_id, flights_, *entity->position))
                flight.pad = *reserved;
        }
    } else {
        flight.phase = AircraftPhase::FlyToTarget;
    }
    flights_.emplace(aircraft, std::move(flight));
    return done();
}

PrototypeAircraftResult PrototypeAircraft::issue_move(const sw::Snapshot& world,
                                                      std::uint32_t aircraft, Cell goal) {
    if (aircraft == 0) return fail(PrototypeAircraftStatus::Invalid, "prototype move needs an entity id");
    if (goal.x >= 512 || goal.y >= 512)
        return fail(PrototypeAircraftStatus::Invalid, "prototype move goal is outside the Cell array");
    const auto* entity = find_entity(world, aircraft);
    if (!entity) return fail(PrototypeAircraftStatus::Invalid, "prototype aircraft is missing from the world");
    PrototypeAircraftRules rules;
    try {
        rules = PrototypeAircraftRules::load(rules_, entity->type);
    } catch (const std::runtime_error& error) {
        return fail(PrototypeAircraftStatus::Invalid, error.what());
    }
    if (!live(*entity) || !entity->position)
        return fail(PrototypeAircraftStatus::NeedsInput, "prototype hover infantry state is unresolved");
    const auto cruise = cruise_height(world, *entity, rules);
    if (!cruise) return fail(PrototypeAircraftStatus::NeedsInput, "prototype cruise height is unresolved");
    const auto found = flights_.find(aircraft);
    if (found != flights_.end()) {
        found->second.target.reset();
        found->second.move_goal = goal;
        found->second.phase = AircraftPhase::Hover;
        found->second.cruise_z = *cruise;
        return done();
    }
    Flight flight;
    flight.rules = rules;
    flight.move_goal = goal;
    flight.phase = AircraftPhase::Hover;
    flight.cruise_z = *cruise;
    flight.ammo = rules.ammo;
    // A plane on a move order keeps a pad for its return; the Hover logic
    // flies it back once the goal is reached.
    if (rules.family == PrototypeAircraftRules::Family::Plane)
        if (const auto reserved = pick_pad(world, rules, entity->house_id, flights_, *entity->position))
            flight.pad = *reserved;
    flights_.emplace(aircraft, std::move(flight));
    return done();
}

void PrototypeAircraft::cancel(sw::Snapshot& world, std::uint32_t aircraft) {
    flights_.erase(aircraft);
    if (auto* actor = find_entity(world, aircraft)) actor->locomotor_moving = false;
}

std::optional<std::string> PrototypeAircraft::update(sw::Snapshot& world,
    std::map<std::uint32_t, Flight>& flights, Flight& flight, sw::Entity& actor,
    PrototypeAircraftResult& result) {
    if (!actor.position) return "prototype aircraft position is unresolved";
    const auto* pad = flight.pad ? find_entity(world, flight.pad) : nullptr;
    const bool pad_live = pad && live(*pad) && pad->kind == sw::Kind::Structure &&
        is_reload_pad(rules_, pad->type) && dock_matches(flight.rules, *pad);
    const auto* victim = flight.target ? find_entity(world, *flight.target) : nullptr;
    if (victim && !victim->position)
        return "prototype attack target position is unresolved";
    const bool victim_live = victim && live(*victim);
    const auto pad_floor = [&]() -> std::optional<std::int32_t> {
        if (!pad || !pad->cell) return {};
        return floor_under(world, *pad->cell);
    };
    bool flying = false;

    switch (flight.phase) {
        case AircraftPhase::Docked: {
            if (!pad_live) {
                // TODO-ORIGINAL: a plane without a pad crashes on landing in
                // RA1 Landing_Takeoff_AI; the prototype circles instead.
                const auto cruise = cruise_height(world, actor, flight.rules);
                if (!cruise) return "prototype cruise height is unresolved";
                flight.cruise_z = *cruise;
                flight.phase = AircraftPhase::Hover;
                break;
            }
            const auto floor = pad_floor();
            if (!floor) return "prototype pad floor is unresolved";
            actor.position->z = *floor;
            if (armed(flight.rules.ammo, flight.ammo)) {
                if (victim_live) flight.phase = AircraftPhase::TakeOff;
                break;
            }
            if (--flight.reload_timer <= 0) {
                ++flight.ammo;
                flight.reload_timer = reload_frames(world, actor, flight);
            }
            break;
        }
        case AircraftPhase::TakeOff: {
            flying = true;
            actor.position->z += climb_rate;
            if (actor.position->z >= flight.cruise_z) {
                actor.position->z = flight.cruise_z;
                flight.phase = victim_live ? AircraftPhase::FlyToTarget : AircraftPhase::Hover;
            }
            break;
        }
        case AircraftPhase::FlyToTarget: {
            flying = true;
            if (!victim_live) {
                flight.target.reset();
                flight.phase = flight.rules.family == PrototypeAircraftRules::Family::Plane
                    ? AircraftPhase::ReturnToPad
                    : AircraftPhase::Hover;
                break;
            }
            if (fly_toward(actor, flight, *victim->position)) {
                flight.phase = AircraftPhase::Attack;
                flight.fire_timer = 0;
                break;
            }
            const auto reach = std::uint64_t(std::llround(flight.rules.range * cell_leptons));
            if (squared_xy(*actor.position, *victim->position) <= reach * reach) {
                flight.phase = AircraftPhase::Attack;
                flight.fire_timer = 0;
            }
            break;
        }
        case AircraftPhase::Attack: {
            if (!victim_live) {
                flight.target.reset();
                flight.phase = flight.rules.family == PrototypeAircraftRules::Family::Plane
                    ? AircraftPhase::ReturnToPad
                    : AircraftPhase::Hover;
                break;
            }
            face(actor, facing_index(std::int64_t(victim->position->x) - actor.position->x,
                                     std::int64_t(victim->position->y) - actor.position->y));
            // External combat: the caller pulls the trigger via fired(); the
            // aircraft only tracks and faces its target here.
            if (external_combat_) {
                const auto reach = std::uint64_t(std::llround(flight.rules.range * cell_leptons));
                if (squared_xy(*actor.position, *victim->position) > reach * reach)
                    flight.phase = AircraftPhase::FlyToTarget;
                break;
            }
            if (--flight.fire_timer <= 0) {
                if (flight.rules.ammo >= 0) --flight.ammo;
                result.shots.push_back({actor.id, *flight.target, flight.rules.damage});
                apply_damage(*find_entity(world, *flight.target), flight.rules.damage);
                flight.fire_timer = flight.rules.rof;
            }
            if (flight.rules.family == PrototypeAircraftRules::Family::Plane && flight.rules.ammo >= 0 && flight.ammo == 0) {
                flight.target.reset();
                flight.phase = AircraftPhase::ReturnToPad;
            }
            break;
        }
        case AircraftPhase::ReturnToPad: {
            flying = true;
            if (!pad_live) {
                // TODO-ORIGINAL: a plane without a pad crashes on landing in
                // RA1 Landing_Takeoff_AI; the prototype circles instead.
                flight.phase = AircraftPhase::Hover;
                break;
            }
            if (fly_toward(actor, flight, cell_center(*pad->cell)))
                flight.phase = dock_free(flights, *pad) ? AircraftPhase::Land : AircraftPhase::Hover;
            break;
        }
        case AircraftPhase::Land: {
            if (!pad_live) {
                flight.phase = AircraftPhase::Hover;
                break;
            }
            const auto floor = pad_floor();
            if (!floor) return "prototype pad floor is unresolved";
            flying = true;
            actor.position->z -= climb_rate;
            if (actor.position->z <= *floor) {
                actor.position->z = *floor;
                flight.phase = AircraftPhase::Docked;
                flight.reload_timer = reload_frames(world, actor, flight);
                result.landed.push_back(actor.id);
            }
            break;
        }
        case AircraftPhase::Hover: {
            if (flight.move_goal) {
                flying = true;
                if (actor.position->z < flight.cruise_z) {
                    actor.position->z += climb_rate;
                    break;
                }
                actor.position->z = flight.cruise_z;
                if (fly_toward(actor, flight, cell_center(*flight.move_goal))) flight.move_goal.reset();
                break;
            }
            if (flight.rules.family == PrototypeAircraftRules::Family::Plane) {
                if (victim_live && armed(flight.rules.ammo, flight.ammo)) {
                    flight.phase = AircraftPhase::FlyToTarget;
                    break;
                }
                if (!pad_live) {
                    if (const auto replacement =
                            pick_pad(world, flight.rules, actor.house_id, flights, *actor.position))
                        flight.pad = *replacement;
                    break;
                }
                // Fly to the pad before landing; a plane away from its pad
                // must not descend over open ground.
                if (!same(*actor.cell, *pad->cell)) {
                    flight.move_goal = *pad->cell;
                    break;
                }
                if (dock_free(flights, *pad)) flight.phase = AircraftPhase::Land;
            } else if (victim_live) {
                flight.phase = AircraftPhase::FlyToTarget;
            }
            break;
        }
    }
    actor.cell = Cell{static_cast<std::uint16_t>(actor.position->x / cell_leptons),
        static_cast<std::uint16_t>(actor.position->y / cell_leptons)};
    actor.locomotor_moving = flying;
    return {};
}

PrototypeAircraftResult PrototypeAircraft::tick(sw::Snapshot& world, std::uint32_t game_frame) {
    PrototypeAircraftResult result;
    if (last_frame_) {
        const auto elapsed = std::uint32_t(game_frame - *last_frame_);
        if (!elapsed || elapsed > 0x7fffffffu)
            return fail(PrototypeAircraftStatus::Invalid, "prototype frame is repeated or in the past");
    }
    auto candidate = world;
    auto flights = flights_;
    for (auto it = flights.begin(); it != flights.end();) {
        auto* actor = find_entity(candidate, it->first);
        if (!actor || !live(*actor)) {
            it = flights.erase(it);
            continue;
        }
        if (const auto error = detach_ground(candidate, *actor))
            return fail(PrototypeAircraftStatus::NeedsInput, *error);
        const auto error = update(candidate, flights, it->second, *actor, result);
        if (error) return fail(PrototypeAircraftStatus::NeedsInput, *error);
        // Idle airships and hover infantry keep flying without a mission.
        if (it->second.rules.family != PrototypeAircraftRules::Family::Plane && it->second.phase == AircraftPhase::Hover &&
            !it->second.target && !it->second.move_goal)
            it = flights.erase(it);
        else
            ++it;
    }
    world = std::move(candidate);
    flights_.swap(flights);
    last_frame_ = game_frame;
    return result;
}

} // namespace ra2::sim
