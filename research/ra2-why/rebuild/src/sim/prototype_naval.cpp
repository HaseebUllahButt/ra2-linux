#include "prototype_naval.h"

#include "../weapons/occupant_order.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
namespace {
using WorldEntity = session::world::Entity;
using WorldHouse = session::world::House;
using WorldKind = session::world::Kind;
using session::world::Lifetime;
using rules::IniFile;

constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}; // 0049F2F0.
std::uint32_t key(ra2map::Cell c) { return std::uint32_t(c.y)*512+c.x; }
bool same(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }

bool equal_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

const WorldEntity* find_entity(const session::world::Snapshot& world, std::uint32_t id) {
    const auto it = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const WorldEntity& e) { return e.id == id; });
    return it == world.entities.end() ? nullptr : &*it;
}
WorldEntity* find_entity(session::world::Snapshot& world, std::uint32_t id) {
    const auto it = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const WorldEntity& e) { return e.id == id; });
    return it == world.entities.end() ? nullptr : &*it;
}

const WorldHouse* find_house(const session::world::Snapshot& world, std::uint32_t id) {
    const auto it = std::find_if(world.houses.begin(), world.houses.end(),
        [&](const WorldHouse& h) { return h.id == id; });
    return it == world.houses.end() ? nullptr : &*it;
}
WorldHouse* find_house(session::world::Snapshot& world, std::uint32_t id) {
    return const_cast<WorldHouse*>(find_house(std::as_const(world), id));
}

bool live(const WorldEntity& entity) {
    return entity.id != 0 && entity.lifetime == Lifetime::Placed && entity.cell &&
        entity.resolved_hp.value_or(0) > 0 && entity.active.value_or(false) &&
        !entity.in_limbo.value_or(true) && entity.on_map != false;
}

// Shared prototype shipyard cell: the placed ship keeps the ground-layer
// occupancy contract of PrototypeMovement (bit 0x20).
constexpr std::uint32_t occupancy_bit = 0x20;

int chebyshev(ra2map::Cell a, ra2map::Cell b) {
    return std::max(std::abs(int(a.x) - int(b.x)), std::abs(int(a.y) - int(b.y)));
}

std::optional<std::int32_t> land_cost(const IniFile& rules, std::int32_t land,
                                      NavalSpeedType speed) {
    static const char* lands[12] = {"Clear", "Road", "Water", "Rock", "Wall", "Tiberium",
        "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds"}; // 0064D610.
    if (land < 0 || unsigned(land) >= 12) return {};
    const auto index = static_cast<std::size_t>(speed);
    if (speed == NavalSpeedType::None || index >= 8) return {};
    static const char* speeds[8] = {"Foot", "Track", "Wheel", "Hover", "Winged",
        "Float", "Amphibious", "FloatBeach"};
    return rules.read_integer(lands[std::size_t(land)], speeds[index], -1);
}

// TODO-ORIGINAL: this prototype uses explicit staged Cell occupancy and the
// authored land table, not the original ship locomotor cell walk (006A05F0).
std::optional<bool> open_cell(const session::world::Snapshot& w, ra2map::Cell c,
    NavalSpeedType speed, const PrototypeWaterTable& table, std::uint32_t self) {
    if (c.x >= 512 || c.y >= 512) return false;
    const auto it = w.cells.find(key(c));
    if (it == w.cells.end()) return false; // The prototype setup owns the cell list.
    const auto& cell = it->second;
    if (!cell.allocated || !cell.heads || !cell.ground_occupancy_124 ||
        !cell.cell_flag_100 || !cell.terrain || !cell.terrain->land_type) return {};
    if (!*cell.allocated || *cell.cell_flag_100) return false;
    const auto cost = table.cost(*cell.terrain->land_type, speed);
    if (!cost) return {};
    if (*cost <= 0) return false; // VesselClass::Can_Enter_Cell: zero ground cost is MOVE_NO.
    if (cell.heads->ground_head == self) return true;
    return cell.heads->ground_head == 0 && *cell.ground_occupancy_124 == 0;
}

struct RouteResult { PrototypeMoveStatus status=PrototypeMoveStatus::Blocked;
    std::vector<ra2map::Cell> path; std::string detail; };

RouteResult route(const session::world::Snapshot& world, std::uint32_t id,
    ra2map::Cell start, ra2map::Cell goal, NavalSpeedType speed,
    const PrototypeWaterTable& table) {
    if (!world.cell_order)
        return {PrototypeMoveStatus::NeedsInput,{}, "prototype Cell allocation order is unresolved"};
    if (goal.x >= 512 || goal.y >= 512)
        return {PrototypeMoveStatus::Invalid,{}, "goal is outside the Cell array"};
    const auto destination = open_cell(world, goal, speed, table, id);
    if (!destination) return {PrototypeMoveStatus::NeedsInput,{}, "goal Cell state is unresolved"};
    if (!*destination) return {PrototypeMoveStatus::Blocked,{}, "goal Cell is not water for this ship"};
    if (same(start, goal)) return {PrototypeMoveStatus::Complete,{}, {}};
    struct Node { int f,g; std::uint32_t serial,index; };
    struct Later { bool operator()(const Node& a,const Node& b) const {
        return a.f!=b.f?a.f>b.f:a.serial>b.serial; } };
    constexpr int infinity=std::numeric_limits<int>::max();
    std::vector<int> cost(512*512,infinity);
    std::vector<std::uint32_t> previous(512*512,0xffffffffu);
    std::priority_queue<Node,std::vector<Node>,Later> open;
    std::uint32_t serial=0;
    const auto source=key(start),target=key(goal);
    const auto heuristic=[&](int x,int y) {
        const int a=std::abs(x-int(goal.x)),b=std::abs(y-int(goal.y));
        return 10*std::max(a,b)+4*std::min(a,b);
    };
    cost[source]=0;open.push({heuristic(start.x,start.y),0,serial++,source});
    while (!open.empty()) {
        const auto node=open.top();open.pop();if (node.g!=cost[node.index]) continue;
        if (node.index==target) {
            std::vector<ra2map::Cell> out;
            for (auto cursor=target;cursor!=source;cursor=previous[cursor]) {
                if (cursor==0xffffffffu) return {PrototypeMoveStatus::Invalid,{}, "path chain is invalid"};
                out.push_back({static_cast<std::uint16_t>(cursor%512),static_cast<std::uint16_t>(cursor/512)});
            }
            std::reverse(out.begin(),out.end());
            return {PrototypeMoveStatus::Complete,std::move(out),{}};
        }
        const int x=int(node.index%512),y=int(node.index/512);
        for (unsigned d=0;d<8;++d) {
            const int nx=x+directions[d][0],ny=y+directions[d][1];
            if (nx<0 || ny<0 || nx>=512 || ny>=512) continue;
            const ra2map::Cell next{static_cast<std::uint16_t>(nx),static_cast<std::uint16_t>(ny)};
            const auto available=open_cell(world,next,speed,table,id);
            if (!available) return {PrototypeMoveStatus::NeedsInput,{}, "path reached unknown Cell state"};
            if (!*available) continue;
            if (d%2) { // Diagonal steps need both open neighbours, as on land.
                const auto horizontal=open_cell(world,{static_cast<std::uint16_t>(nx),static_cast<std::uint16_t>(y)},speed,table,id);
                const auto vertical=open_cell(world,{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(ny)},speed,table,id);
                if (!horizontal || !vertical)
                    return {PrototypeMoveStatus::NeedsInput,{}, "corner Cell state is unresolved"};
                if (!*horizontal || !*vertical) continue;
            }
            const auto index=key(next);
            const int next_cost=node.g+(d%2?14:10);
            if (next_cost>=cost[index]) continue;
            cost[index]=next_cost;previous[index]=node.index;
            open.push({next_cost+heuristic(nx,ny),next_cost,serial++,index});
        }
    }
    return {PrototypeMoveStatus::Blocked,{}, "no water route"};
}

std::optional<std::string> transfer(session::world::Snapshot& world, std::uint32_t id,
    ra2map::Cell from, ra2map::Cell to) {
    auto source=world.cells.find(key(from)),target=world.cells.find(key(to));
    if (source==world.cells.end() || target==world.cells.end() || !source->second.heads ||
        !target->second.heads || !source->second.ground_occupancy_124 ||
        !target->second.ground_occupancy_124)
        return "naval transfer Cell is unresolved";
    const auto* actor=find_entity(world,id);
    if (!actor || !actor->next || *actor->next!=0 || source->second.heads->ground_head!=id ||
        target->second.heads->ground_head || *target->second.ground_occupancy_124)
        return "naval transfer needs an isolated source and an empty target";
    weapons::ContentLinks links;
    links.cells.emplace(key(from),*source->second.heads);
    links.cells.emplace(key(to),*target->second.heads);
    for (const auto& e:world.entities) {
        if (e.lifetime!=Lifetime::Placed) continue;
        if (!e.next) return "naval ordered Object link is unresolved";
        links.objects.emplace(e.id,weapons::ContentObject{*e.next,
            e.kind==WorldKind::Structure?6:1});
    }
    if (links.remove(key(from),weapons::ContentLayer::Ground,id)!=weapons::ContentResult::Complete ||
        links.insert(key(to),weapons::ContentLayer::Ground,id)!=weapons::ContentResult::Complete)
        return "naval ordered Object links are cyclic";
    source->second.heads=links.cells.at(key(from));
    target->second.heads=links.cells.at(key(to));
    for (auto& e:world.entities) {
        const auto link=links.objects.find(e.id);
        if (link!=links.objects.end()) e.next=link->second.next;
    }
    *source->second.ground_occupancy_124&=~occupancy_bit;
    *target->second.ground_occupancy_124|=occupancy_bit;
    return {};
}
} // namespace

std::optional<NavalSpeedType> naval_speed_type(std::string_view name) {
    static const std::pair<const char*,NavalSpeedType> table[8] = {
        {"Foot",NavalSpeedType::Foot},{"Track",NavalSpeedType::Track},
        {"Wheel",NavalSpeedType::Wheel},{"Hover",NavalSpeedType::Hover},
        {"Winged",NavalSpeedType::Winged},{"Float",NavalSpeedType::Float},
        {"Amphibious",NavalSpeedType::Amphibious},{"FloatBeach",NavalSpeedType::FloatBeach}};
    for (const auto& item : table)
        if (equal_name(name, item.first)) return item.second;
    return std::nullopt;
}

const std::vector<const char*>& prototype_ship_types() {
    static const std::vector<const char*> types{"CARRIER","DEST","SUB","AEGIS","DRED","SQD","DLPH"};
    return types;
}

PrototypeShipRules PrototypeShipRules::load(const rules::IniFile& rules,
                                            std::string_view type) {
    const std::string id(type);
    if (!rules.find_section(id)) throw std::runtime_error("missing ship section: " + id);
    PrototypeShipRules out;
    out.type = id;
    out.display_name = rules.read_string(id, "Name");
    out.strength = rules.read_integer(id, "Strength", 0);
    out.cost = rules.read_integer(id, "Cost", 0);
    out.rule_speed = rules.read_integer(id, "Speed", 0);
    out.tech_level = rules.read_integer(id, "TechLevel", 0);
    out.sight = rules.read_integer(id, "Sight", 0);
    out.speed_type = naval_speed_type(rules.read_string(id, "SpeedType"))
        .value_or(NavalSpeedType::None);
    out.naval = rules.read_bool(id, "Naval", false);
    out.underwater = rules.read_bool(id, "Underwater", false);
    out.sensors = rules.read_bool(id, "Sensors", false);
    out.sensors_sight = rules.read_integer(id, "SensorsSight", 0);
    out.category = rules.read_string(id, "Category");
    out.primary = rules.read_string(id, "Primary");
    out.armor = rules.read_string(id, "Armor");
    if (out.strength <= 0 || out.cost <= 0 || out.rule_speed <= 0 ||
        out.speed_type == NavalSpeedType::None || !out.naval)
        throw std::runtime_error("invalid ship rules: " + id);
    return out;
}

std::vector<PrototypeShipRules> load_prototype_ship_rules(const rules::IniFile& rules) {
    std::vector<PrototypeShipRules> result;
    for (const auto* type : prototype_ship_types())
        result.push_back(PrototypeShipRules::load(rules, type));
    return result;
}

PrototypeWaterTable PrototypeWaterTable::load(const rules::IniFile& rules) {
    PrototypeWaterTable out;
    for (std::size_t land = 0; land < land_count; ++land)
        for (std::size_t speed = 0; speed < speed_count; ++speed) {
            const auto value = land_cost(rules, static_cast<std::int32_t>(land),
                static_cast<NavalSpeedType>(static_cast<std::int32_t>(speed)));
            if (value && *value >= 0) out.costs_[land][speed] = *value;
        }
    return out;
}

std::optional<std::int32_t> PrototypeWaterTable::cost(std::int32_t land_type,
                                                      NavalSpeedType speed) const {
    if (land_type < 0 || unsigned(land_type) >= land_count) return {};
    const auto index = static_cast<std::size_t>(speed);
    if (speed == NavalSpeedType::None || index >= speed_count) return {};
    return costs_[std::size_t(land_type)][index];
}

std::optional<bool> naval_open_cell(const session::world::Snapshot& world,
    ra2map::Cell cell, NavalSpeedType speed_type, const PrototypeWaterTable& table,
    std::uint32_t self) {
    return open_cell(world, cell, speed_type, table, self);
}

bool prototype_sees_ship(const session::world::Snapshot& world, const rules::IniFile& rules,
    std::uint32_t viewer_house, std::uint32_t target) {
    const auto* found = find_entity(world, target);
    if (!found || found->lifetime != Lifetime::Placed || found->resolved_hp.value_or(0) <= 0)
        return false;
    if (found->house_id == viewer_house) return true;
    if (!found->cell || !rules.read_bool(found->type, "Underwater")) return true;
    for (const auto& entity : world.entities) {
        if (entity.house_id != viewer_house || entity.id == target || !live(entity) ||
            !entity.cell) continue;
        if (!rules.read_bool(entity.type, "Sensors")) continue;
        const auto sight = rules.read_integer(entity.type, "SensorsSight", 0);
        if (sight > 0 && chebyshev(*entity.cell, *found->cell) <= sight) return true;
    }
    return false;
}

bool is_naval_yard(const rules::IniFile& rules, const session::world::Entity& yard) {
    if (yard.kind != WorldKind::Structure) return false;
    // General BuildNavalYard names the naval factories (Rules 0064C2B0).
    for (const auto& name : rules.read_list("General", "BuildNavalYard"))
        if (equal_name(name, yard.type)) return true;
    const auto factory = rules.read_string(yard.type, "Factory");
    return equal_name(factory, "UnitType") && rules.read_bool(yard.type, "Naval") &&
        rules.read_bool(yard.type, "WaterBound");
}

const session::world::Entity* naval_yard(const session::world::Snapshot& world,
    const rules::IniFile& rules, std::uint32_t house) {
    const session::world::Entity* found = nullptr;
    for (const auto& entity : world.entities) {
        if (entity.house_id != house || !live(entity) || !is_naval_yard(rules, entity)) continue;
        if (!found || entity.id < found->id) found = &entity;
    }
    return found;
}

bool naval_buildable(const session::world::Snapshot& world, const rules::IniFile& rules,
    std::uint32_t house, std::string_view type) {
    const auto* house_record = find_house(world, house);
    if (!house_record) return false;
    const std::string id(type);
    if (!rules.find_section(id) || !rules.read_bool(id, "Naval")) return false;
    if (!naval_speed_type(rules.read_string(id, "SpeedType"))) return false;
    const auto cost = rules.read_integer(id, "Cost", -1);
    if (cost <= 0 || rules.read_integer(id, "Strength", -1) <= 0) return false;
    const auto owner = rules.read_list(id, "Owner");
    // TODO-ORIGINAL: House::Can_Build matches country bit masks; the prototype
    // compares the authored country name.
    if (std::none_of(owner.begin(), owner.end(),
        [&](const std::string& name) { return equal_name(name, house_record->country); }))
        return false;
    const auto tech = rules.read_integer(id, "TechLevel", -1);
    if (tech < 0 || tech > house_record->tech_level.value_or(10)) return false;
    // Prerequisites: naval yards and other live buildings of the named type.
    // Generic groups resolve through the General Prerequisite lists.
    constexpr std::array<const char*, 6> groups{{"Power", "Factory", "Barracks",
        "Radar", "Tech", "Proc"}};
    const auto owns = [&](std::string_view name) {
        return std::any_of(world.entities.begin(), world.entities.end(),
            [&](const WorldEntity& e) {
                return e.house_id == house && e.kind == WorldKind::Structure &&
                    live(e) && equal_name(e.type, name);
            });
    };
    const auto satisfied = [&](std::string_view name) {
        for (const auto* group : groups) {
            if (!equal_name(name, group)) continue;
            for (const auto& type : rules.read_list("General", std::string("Prerequisite") + group))
                if (owns(type)) return true;
            return false;
        }
        return owns(name);
    };
    const auto overrides = rules.read_list(id, "PrerequisiteOverride");
    if (!overrides.empty()) {
        for (const auto& name : overrides) if (satisfied(name)) return true;
        return false;
    }
    for (const auto& name : rules.read_list(id, "Prerequisite"))
        if (!satisfied(name)) return false;
    return static_cast<std::int64_t>(house_record->cash.value_or(0)) >= cost;
}

std::optional<std::uint32_t> naval_produce(session::world::Snapshot& world,
    const rules::IniFile& rules, const PrototypeWaterTable& table,
    std::uint32_t house, std::string_view type) {
    if (!naval_buildable(world, rules, house, type)) return std::nullopt;
    const auto* yard = naval_yard(world, rules, house);
    if (!yard || !yard->cell) return std::nullopt;
    const auto ship = PrototypeShipRules::load(rules, type);
    // TODO-ORIGINAL: the original exits the finished Unit through the
    // building's Dock cells (BuildingClass::Exit_Object 0045C930). The
    // prototype searches a ring of free water cells around the yard.
    std::optional<ra2map::Cell> dock;
    for (int radius = 1; radius <= 12 && !dock; ++radius)
        for (int dy = -radius; dy <= radius && !dock; ++dy)
            for (int dx = -radius; dx <= radius && !dock; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                const int x = int(yard->cell->x) + dx, y = int(yard->cell->y) + dy;
                if (x < 0 || y < 0 || x >= 512 || y >= 512) continue;
                const ra2map::Cell candidate{static_cast<std::uint16_t>(x),
                    static_cast<std::uint16_t>(y)};
                const auto open = open_cell(world, candidate, ship.speed_type, table, 0);
                if (open && *open) dock = candidate;
            }
    if (!dock) return std::nullopt;
    std::uint32_t id = 0;
    for (const auto& entity : world.entities) id = std::max(id, entity.id);
    ++id;
    WorldEntity ship_entity;
    ship_entity.id = id;
    ship_entity.house_id = house;
    ship_entity.type = ship.type;
    ship_entity.kind = WorldKind::Unit;
    ship_entity.lifetime = Lifetime::Placed;
    ship_entity.resolved_hp = ship.strength;
    ship_entity.maximum_hp = ship.strength;
    ship_entity.cell = *dock;
    const auto cell = world.cells.find(key(*dock));
    const auto floor = cell->second.terrain && cell->second.terrain->floor_world_height
        ? *cell->second.terrain->floor_world_height : 0;
    ship_entity.position = Coord{int(dock->x) * 256 + 128, int(dock->y) * 256 + 128, floor};
    ship_entity.layer = session::world::Layer::Ground;
    ship_entity.next = 0;
    ship_entity.active = true;
    ship_entity.in_limbo = false;
    ship_entity.on_map = true;
    ship_entity.hull_facing = 0;
    ship_entity.hull_facing_raw = 0;
    world.entities.push_back(ship_entity);
    world.creation_order.push_back({session::world::CreatedKind::Entity, id});
    cell->second.heads->ground_head = id;
    *cell->second.ground_occupancy_124 |= occupancy_bit;
    // TODO-ORIGINAL: House::Begin_Production 004E5E30 and FactoryClass take
    // the money; the prototype pays the full cost when the ship appears.
    auto* house_record = find_house(world, house);
    if (house_record && house_record->cash) house_record->cash = *house_record->cash - ship.cost;
    return id;
}

PrototypeMoveResult PrototypeNavalMovement::issue(const session::world::Snapshot& world,
    std::uint32_t unit, ra2map::Cell goal, int speed, NavalSpeedType speed_type,
    const PrototypeWaterTable& table) {
    if (world.setup_progress != session::world::SetupProgress::PrototypeComplete)
        return {PrototypeMoveStatus::NeedsInput, "prototype Battle setup is unfinished", {}, {}, {}};
    if (speed_type == NavalSpeedType::None)
        return {PrototypeMoveStatus::NeedsInput, "ship SpeedType is unresolved", {}, {}, {}};
    const auto* actor = find_entity(world, unit);
    if (!actor || actor->kind != WorldKind::Unit || actor->lifetime != Lifetime::Placed)
        return {PrototypeMoveStatus::NeedsInput, "naval movement needs a placed Unit", {}, {}, {}};
    if (!actor->resolved_hp || !actor->position || !actor->cell || !actor->layer ||
        !actor->next || *actor->resolved_hp <= 0 ||
        *actor->layer != session::world::Layer::Ground)
        return {PrototypeMoveStatus::NeedsInput, "naval Unit state is unresolved", {}, {}, {}};
    if (speed <= 0 || speed > 256)
        return {PrototypeMoveStatus::Invalid, "naval speed must be 1..256 world units per frame", {}, {}, {}};
    const auto path = route(world, unit, *actor->cell, goal, speed_type, table);
    if (path.status != PrototypeMoveStatus::Complete) return {path.status, path.detail, {}, {}, {}};
    moves_[unit] = {goal, speed, speed_type, path.path};
    return {PrototypeMoveStatus::Complete, {}, {}, {}, {}};
}

void PrototypeNavalMovement::cancel(session::world::Snapshot& world, std::uint32_t unit) {
    moves_.erase(unit);
    if (auto* actor = find_entity(world, unit)) actor->locomotor_moving = false;
}

PrototypeMoveResult PrototypeNavalMovement::tick(session::world::Snapshot& world,
    std::uint32_t game_frame, const PrototypeWaterTable& table) {
    using session::world::SetupProgress;
    if (world.setup_progress != SetupProgress::PrototypeComplete)
        return {PrototypeMoveStatus::NeedsInput, "prototype Battle setup is unfinished", {}, {}, {}};
    if (last_frame_) {
        const auto elapsed = std::uint32_t(game_frame - *last_frame_);
        if (!elapsed || elapsed > 0x7fffffffu)
            return {PrototypeMoveStatus::Invalid, "prototype frame is repeated or in the past", {}, {}, {}};
    }
    auto candidate = world; auto moves = moves_;
    PrototypeMoveResult result; result.status = PrototypeMoveStatus::Complete;
    for (auto it = moves.begin(); it != moves.end();) {
        const auto id = it->first; auto& movement = it->second;
        auto* actor = find_entity(candidate, id);
        if (!actor || actor->lifetime == Lifetime::Destroyed ||
            (actor->resolved_hp && *actor->resolved_hp <= 0)) {
            if (actor) actor->locomotor_moving = false;
            it = moves.erase(it); continue;
        }
        if (!actor->resolved_hp || !actor->position || !actor->cell || !actor->layer ||
            !actor->next || *actor->layer != session::world::Layer::Ground)
            return {PrototypeMoveStatus::NeedsInput, "naval Unit state is unresolved", {}, {}, {}};
        const auto source = candidate.cells.find(key(*actor->cell));
        if (source == candidate.cells.end() || !source->second.heads ||
            !source->second.ground_occupancy_124)
            return {PrototypeMoveStatus::NeedsInput, "naval source Cell is unresolved", {}, {}, {}};
        if (source->second.heads->ground_head != id || *actor->next != 0 ||
            !(*source->second.ground_occupancy_124 & occupancy_bit))
            return {PrototypeMoveStatus::Blocked, "naval source Unit is not isolated on water", {}, {}, {}};
        if (movement.path.empty()) {
            actor->locomotor_moving = false; result.arrived.push_back(id);
            it = moves.erase(it); continue;
        }
        auto target = movement.path.front();
        auto available = open_cell(candidate, target, movement.speed_type, table, id);
        if (!available)
            return {PrototypeMoveStatus::NeedsInput, "naval next Cell is unresolved", {}, {}, {}};
        if (!*available) {
            // Another ship can occupy the next water cell; try a new route.
            const auto revised = route(candidate, id, *actor->cell, movement.goal,
                movement.speed_type, table);
            if (revised.status == PrototypeMoveStatus::NeedsInput ||
                revised.status == PrototypeMoveStatus::Invalid)
                return {revised.status, revised.detail, {}, {}, {}};
            if (revised.status == PrototypeMoveStatus::Complete && !revised.path.empty()) {
                movement.path = revised.path;
                target = movement.path.front();
            } else {
                actor->locomotor_moving = false; result.blocked.push_back(id); ++it; continue;
            }
        }
        const auto found = candidate.cells.find(key(target));
        if (found == candidate.cells.end() || !found->second.terrain ||
            !found->second.terrain->floor_world_height)
            return {PrototypeMoveStatus::NeedsInput, "naval target floor is unresolved", {}, {}, {}};
        // TODO-ORIGINAL: the original scales drive speed by the land table
        // cost inside the locomotor; the prototype scales the whole step.
        const auto cell_cost = table.cost(*found->second.terrain->land_type, movement.speed_type)
            .value_or(100);
        const int step = std::max(1, movement.speed * std::clamp(cell_cost, 0, 100) / 100);
        const auto old = *actor->position;
        const Coord desired{int(target.x) * 256 + 128, int(target.y) * 256 + 128,
            *found->second.terrain->floor_world_height};
        const auto dx = std::int64_t(desired.x) - old.x, dy = std::int64_t(desired.y) - old.y,
            dz = std::int64_t(desired.z) - old.z;
        const auto distance = std::max(std::abs(dx), std::abs(dy));
        Coord next = desired;
        if (distance > step) {
            next.x = old.x + static_cast<int>(dx * step / distance);
            next.y = old.y + static_cast<int>(dy * step / distance);
            next.z = old.z + static_cast<int>(dz * step / distance);
        }
        if (next.x < 0 || next.y < 0 || next.x / 256 >= 512 || next.y / 256 >= 512)
            return {PrototypeMoveStatus::Invalid, "naval position leaves the Cell array", {}, {}, {}};
        const ra2map::Cell entered{static_cast<std::uint16_t>(next.x / 256),
            static_cast<std::uint16_t>(next.y / 256)};
        if (!same(*actor->cell, entered)) {
            const auto step_cell = open_cell(candidate, entered, movement.speed_type, table, id);
            if (!step_cell)
                return {PrototypeMoveStatus::NeedsInput, "naval crossed Cell is unresolved", {}, {}, {}};
            if (!*step_cell) {
                actor->locomotor_moving = false; result.blocked.push_back(id); ++it; continue;
            }
            if (const auto error = transfer(candidate, id, *actor->cell, entered))
                return {PrototypeMoveStatus::NeedsInput, *error, {}, {}, {}};
            actor = find_entity(candidate, id); actor->cell = entered;
        }
        // TODO-ORIGINAL: turn rates, wake tracks and facing AI are not recovered.
        const int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
        for (unsigned d = 0; d < 8; ++d) if (directions[d][0] == sx && directions[d][1] == sy) {
            actor->hull_facing = static_cast<int>(d * 32);
            actor->hull_facing_raw = static_cast<std::uint16_t>(d * 8192);
            break;
        }
        actor->position = next; actor->collision_virtual_position = next;
        actor->locomotor_moving = true; result.moved.push_back(id);
        if (next.x == desired.x && next.y == desired.y && next.z == desired.z)
            movement.path.erase(movement.path.begin());
        if (movement.path.empty()) {
            actor->locomotor_moving = false; result.arrived.push_back(id);
            it = moves.erase(it);
        } else ++it;
    }
    world = std::move(candidate); moves_ = std::move(moves);
    last_frame_ = game_frame;
    return result;
}

} // namespace ra2::sim
