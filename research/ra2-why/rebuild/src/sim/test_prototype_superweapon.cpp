#include "prototype_superweapon.h"
#include "../fs/mix_bootstrap.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace sw = ra2::session::world;
using ra2::rules::IniFile;
using ra2::sim::SuperKind;
using ra2::sim::SuperweaponType;
using ra2::sim::PrototypeSuperweapons;
using ra2map::Cell;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const ra2::sim::SuperweaponType* by_id(const std::vector<SuperweaponType>& types,
                                       const std::string& id) {
    for (const auto& type : types)
        if (type.id == id) return &type;
    return nullptr;
}

IniFile fixture_rules() {
    IniFile rules;
    rules.parse(
        "[SuperWeaponTypes]\n1=MultiSpecial\n2=IronCurtainSpecial\n3=ChronoWarpSpecial\n"
        "4=SpyPlaneSpecial\n"
        "[MultiSpecial]\nName=N U K E !!!\nIsPowered=true\nRechargeTime=10\nType=MultiMissile\n"
        "WeaponType=NukeCarrier\nShowTimer=yes\n"
        "[IronCurtainSpecial]\nIsPowered=true\nRechargeTime=5\nType=IronCurtain\nRange=1.4\n"
        "LineMultiplier=3\n"
        "[ChronoWarpSpecial]\nIsPowered=false\nRechargeTime=1\nType=ChronoWarp\nPostClick=yes\n"
        "PreDependent=ChronoSphere\n"
        "[SpyPlaneSpecial]\nIsPowered=false\nRechargeTime=4\nType=SpyPlane\nShowTimer=no\n"
        "[BuildingTypes]\n1=NAMISL\n2=NAIRON\n3=AMRADR\n[AMRADR]\nSuperWeapon=SpyPlaneSpecial\n"
        "[NAMISL]\nSuperWeapon=MultiSpecial\n"
        "[NAIRON]\nSuperWeapon=IronCurtainSpecial\nSuperWeapon2=MultiSpecial\n"
        "[NukePayload]\nDamage=600\nWarhead=NUKE\n"
        "[NUKE]\nCellSpread=10\nPercentAtMax=.02\nVerses=100%,100%,100%,200%,100%,100%,60%,100%,8%,100%,100%\n"
        "[CombatDamage]\nMaxDamage=10000\nIronCurtainDuration=750\n"
        "[General]\nCameraRange=9\n");
    return rules;
}

sw::House house(std::uint32_t id, const char* country, std::int32_t output, std::int32_t drain) {
    sw::House result;
    result.id = id;
    result.country = country;
    result.power_output = output;
    result.power_drain = drain;
    return result;
}

sw::Snapshot make_world() {
    sw::Snapshot world;
    world.edition = sw::Edition::Base;
    world.setup_progress = sw::SetupProgress::PrototypeComplete;
    for (int y = 5; y <= 80; ++y)
        for (int x = 5; x <= 80; ++x) {
            sw::CellState cell;
            cell.allocated = true;
            cell.heads.emplace();
            cell.cell_flag_100 = false;
            cell.ground_occupancy_124 = 0;
            cell.terrain.emplace();
            cell.terrain->cell = Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            cell.terrain->floor_world_height = 0;
            cell.terrain->ramp_index = 0;
            cell.terrain->overlay_type_index = -1;
            world.cells.emplace(static_cast<std::uint32_t>(y) * 512 + x, cell);
        }
    world.houses = {house(101, "Americans", 200, 100), house(102, "Russians", 200, 100)};
    return world;
}

sw::Entity& place(sw::Snapshot& world, std::uint32_t id, std::uint32_t owner,
                  const std::string& type, sw::Kind kind, Cell cell, int hp) {
    sw::Entity entity;
    entity.id = id;
    entity.house_id = owner;
    entity.type = type;
    entity.kind = kind;
    entity.lifetime = sw::Lifetime::Placed;
    entity.cell = cell;
    entity.position = ra2::sim::Coord{cell.x * 256 + 128, cell.y * 256 + 128, 0};
    entity.resolved_hp = hp;
    entity.maximum_hp = hp;
    entity.next = 0;
    entity.active = true;
    entity.in_limbo = false;
    entity.on_map = true;
    entity.layer = sw::Layer::Ground;
    auto& cell_state = world.cells.at(static_cast<std::uint32_t>(cell.y) * 512 + cell.x);
    entity.next = cell_state.heads->ground_head;
    cell_state.heads->ground_head = id;
    *cell_state.ground_occupancy_124 |= 0x20u;
    world.entities.push_back(std::move(entity));
    world.creation_order.push_back({sw::CreatedKind::Entity, id});
    return world.entities.back();
}

bool chain_ok(const sw::Snapshot& world) {
    std::set<std::uint32_t> seen;
    for (const auto& pair : world.cells) {
        if (!pair.second.heads) continue;
        for (const auto head : {pair.second.heads->ground_head, pair.second.heads->bridge_head}) {
            auto link = head;
            while (link) {
                if (!seen.insert(link).second) return false;
                const auto found = std::find_if(world.entities.begin(), world.entities.end(),
                    [link](const sw::Entity& entity) { return entity.id == link; });
                if (found == world.entities.end() || !found->next) return false;
                link = *found->next;
            }
        }
    }
    return true;
}

std::size_t chain_length(const sw::Snapshot& world, Cell cell) {
    std::size_t count = 0;
    const auto& cell_state = world.cells.at(static_cast<std::uint32_t>(cell.y) * 512 + cell.x);
    if (!cell_state.heads) return 0;
    for (auto link = cell_state.heads->ground_head; link;) {
        ++count;
        const auto found = std::find_if(world.entities.begin(), world.entities.end(),
            [link](const sw::Entity& entity) { return entity.id == link; });
        if (found == world.entities.end() || !found->next) break;
        link = *found->next;
    }
    return count;
}

void check_parse() {
    const auto rules = fixture_rules();
    const auto types = ra2::sim::read_superweapon_types(rules);
    require(types.size() == 4, "Fixture super weapon count changed");
    const auto* nuke = by_id(types, "MultiSpecial");
    require(nuke && nuke->kind == SuperKind::Nuke && nuke->recharge_frames == 9000 &&
        nuke->is_powered && nuke->weapon == "NukeCarrier" && nuke->show_timer,
        "Nuke section did not parse Type, RechargeTime, IsPowered, WeaponType and ShowTimer");
    const auto* curtain = by_id(types, "IronCurtainSpecial");
    require(curtain && curtain->kind == SuperKind::IronCurtain &&
        curtain->recharge_frames == 4500 && curtain->is_powered &&
        curtain->range_cells == 1.4f && curtain->line_multiplier == 3,
        "Iron Curtain section did not parse Range and LineMultiplier");
    const auto* warp = by_id(types, "ChronoWarpSpecial");
    require(warp && warp->kind == SuperKind::ChronoWarp && warp->recharge_frames == 900 &&
        !warp->is_powered && warp->post_click && warp->pre_dependent == "ChronoSphere",
        "Chrono Warp section did not parse PreDependent and PostClick");
    require(ra2::sim::superweapon_kind("multimissile") == SuperKind::Nuke &&
        ra2::sim::superweapon_kind("LightningStorm") == SuperKind::LightningStorm &&
        ra2::sim::superweapon_kind("Bogus") == SuperKind::Invalid,
        "Type name parse changed");
    const auto grants = ra2::sim::superweapon_grants(rules);
    const auto nuke_grant = grants.find("MultiSpecial");
    require(nuke_grant != grants.end() && nuke_grant->second.size() == 2 &&
        nuke_grant->second[0] == "NAMISL" && nuke_grant->second[1] == "NAIRON",
        "SuperWeapon and SuperWeapon2 grants did not resolve");
    require(grants.find("IronCurtainSpecial") != grants.end() &&
        grants.at("IronCurtainSpecial").size() == 1 &&
        grants.at("IronCurtainSpecial")[0] == "NAIRON", "Iron Curtain grant changed");
    IniFile empty;
    require(ra2::sim::read_superweapon_types(empty).empty() &&
        ra2::sim::superweapon_grants(empty).empty(), "Empty rules produced types");
}

void check_base_rules(const IniFile& rules) {
    const auto types = ra2::sim::read_superweapon_types(rules);
    require(types.size() == 7, "Base [SuperWeaponTypes] count changed");
    struct Expect { const char* id; SuperKind kind; int frames; bool powered; };
    const std::vector<Expect> expected{
        {"MultiSpecial", SuperKind::Nuke, 9000, true},
        {"IronCurtainSpecial", SuperKind::IronCurtain, 4500, true},
        {"LightningStormSpecial", SuperKind::LightningStorm, 9000, true},
        {"ChronoSphereSpecial", SuperKind::ChronoSphere, 6300, true},
        {"ChronoWarpSpecial", SuperKind::ChronoWarp, 900, false},
        {"ParaDropSpecial", SuperKind::ParaDrop, 3600, false},
        {"AmericanParaDropSpecial", SuperKind::AmerParaDrop, 3600, false},
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require(types[i].id == expected[i].id && types[i].kind == expected[i].kind &&
            types[i].recharge_frames == expected[i].frames &&
            types[i].is_powered == expected[i].powered,
            std::string("Base super weapon changed: ") + expected[i].id);
    }
    require(by_id(types, "ChronoSphereSpecial")->pre_click &&
        by_id(types, "ChronoWarpSpecial")->pre_dependent == "ChronoSphere",
        "Base ChronoSphere and ChronoWarp lost their click pair");
    const auto grants = ra2::sim::superweapon_grants(rules);
    const std::vector<std::pair<const char*, const char*>> granting{
        {"MultiSpecial", "NAMISL"}, {"IronCurtainSpecial", "NAIRON"},
        {"LightningStormSpecial", "GAWEAT"}, {"ChronoSphereSpecial", "GACSPH"},
        {"ChronoWarpSpecial", "GADUMY"}, {"ParaDropSpecial", "CAAIRP"},
        {"AmericanParaDropSpecial", "AMRADR"},
    };
    for (const auto& [section, building] : granting) {
        const auto found = grants.find(section);
        require(found != grants.end() &&
            std::any_of(found->second.begin(), found->second.end(),
                [&](const std::string& name) { return name == building; }),
            std::string("Base grant missing: ") + section + " from " + building);
    }
    // The payload weapon the nuke detonates, from the installed base rules.
    require(rules.read_integer("NukePayload", "Damage") == 600 &&
        rules.read_string("NukePayload", "Warhead") == "NUKE" &&
        rules.read_double("NUKE", "CellSpread") == 10.0 &&
        rules.read_integer("CombatDamage", "IronCurtainDuration") == 750,
        "Base nuke payload and Iron Curtain duration changed");
}

void check_charge(const IniFile& rules) {
    auto world = make_world();
    place(world, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    PrototypeSuperweapons supers(rules);
    supers.update_grants(world);
    const auto* state = supers.state(102, "MultiSpecial");
    require(state && state->present && !state->ready && state->remaining == 9000,
        "Nuke did not start charging at its rules recharge time");
    require(!supers.ready(102, "MultiSpecial") && supers.frames_left(102, "MultiSpecial") == 9000,
        "Charging weapon reported ready");
    require(supers.state(101, "MultiSpecial") == nullptr ||
        !supers.state(101, "MultiSpecial")->present,
        "Weapon granted without its building");
    for (int frame = 0; frame < 8999; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(supers.frames_left(102, "MultiSpecial") == 1, "Charge clock drifted");
    supers.tick(world, 8999);
    require(supers.ready(102, "MultiSpecial") && supers.frames_left(102, "MultiSpecial") == 0,
        "Nuke did not become ready after its recharge time");
    supers.tick(world, 9000);
    require(supers.ready(102, "MultiSpecial"), "Ready weapon lost its charge");
    supers.update_grants(world);
    require(supers.ready(102, "MultiSpecial"), "Regrant reset a charged weapon");
    // Destroying the silo loses the weapon; rebuilding grants it again.
    world.entities[0].lifetime = sw::Lifetime::Destroyed;
    supers.update_grants(world);
    require(!supers.ready(102, "MultiSpecial") && supers.frames_left(102, "MultiSpecial") == -1,
        "Weapon stayed granted without its building");
    world.entities[0].lifetime = sw::Lifetime::Placed;
    supers.update_grants(world);
    require(supers.frames_left(102, "MultiSpecial") == 9000,
        "Regrant did not restart the charge");
    // Frame rewind and repeats do not advance the clock.
    const auto before = supers.frames_left(102, "MultiSpecial");
    supers.tick(world, 10);
    require(supers.frames_left(102, "MultiSpecial") == before, "Rewound frame charged the weapon");
}

void check_low_power(const IniFile& rules) {
    auto world = make_world();
    place(world, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    PrototypeSuperweapons supers(rules);
    supers.update_grants(world);
    world.houses[1].power_output = 50;
    world.houses[1].power_drain = 100;
    supers.tick(world, 0);
    const auto* state = supers.state(102, "MultiSpecial");
    require(state->suspended, "Low power did not suspend the powered weapon");
    for (int frame = 1; frame <= 100; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(supers.frames_left(102, "MultiSpecial") == 9000,
        "Suspended weapon kept charging");
    world.houses[1].power_output = 200;
    supers.tick(world, 101);
    require(!supers.state(102, "MultiSpecial")->suspended,
        "Weapon stayed suspended after power returned");
    for (int frame = 102; frame < 9102; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(supers.ready(102, "MultiSpecial"), "Weapon did not finish charging after power returned");
    // A ready powered weapon cannot fire at low power.
    world.houses[1].power_output = 0;
    supers.tick(world, 9200);
    const auto report = supers.fire(world, 102, "MultiSpecial", {50, 50}, 9200);
    require(!report.fired && !report.notes.empty(), "Low power allowed a powered launch");
    world.houses[1].power_output = 200;
    // Unpowered weapons charge and fire in the same brownout.
    place(world, 2, 102, "AMRADR", sw::Kind::Structure, {12, 10}, 600);
    supers.update_grants(world);
    supers.tick(world, 9003);
    require(supers.frames_left(102, "AmericanParaDropSpecial") == 3600,
        "Unpowered weapon did not charge during the brownout");
}

void check_nuke(const IniFile& rules, const std::string& exe) {
    auto world = make_world();
    place(world, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    place(world, 2, 101, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    place(world, 3, 101, "HTNK", sw::Kind::Unit, {55, 50}, 400);
    place(world, 4, 101, "HTNK", sw::Kind::Unit, {70, 50}, 400);
    PrototypeSuperweapons supers(rules, exe);
    supers.update_grants(world);
    for (int frame = 0; frame < 9000; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    const auto report = supers.fire(world, 102, "MultiSpecial", {50, 50}, 9000);
    require(report.fired, "Nuke launch rejected");
    // The missile is in flight: nothing is hit at the click (base 1.006
    // FUN_0069D850 case 0 spawns a falling missile instead of detonating).
    require(report.destroyed == 0 && report.damaged == 0 && world.entities[1].resolved_hp == 400,
        "Nuke damaged the target at click time instead of on missile impact");
    require(supers.frames_left(102, "MultiSpecial") == 9000,
        "Nuke launch did not restart its charge while the missile flies");
    // The missile flight comes from the base rules weapon chain:
    // [NukeCarrier] Speed=100 rising to [GiantNukeUp] DetonationAltitude=20000,
    // then [NukePayload] Speed=10 falling from [GiantNukeDown]
    // DetonationAltitude=30000: 200 + 3000 = 3200 frames.
    int tick_damaged = 0, tick_destroyed = 0, nukes = 0;
    for (int frame = 9001; frame <= 12200; ++frame) {
        const auto impact = supers.tick(world, static_cast<std::uint64_t>(frame));
        tick_damaged += impact.damaged;
        tick_destroyed += impact.destroyed;
        nukes += impact.nukes;
    }
    require(nukes == 1, "Nuke missile did not land exactly once");
    require(tick_destroyed == 1 && tick_damaged == 2,
        "Nuke blast did not hit the center and the 5 cell ring");
    require(world.entities[1].lifetime == sw::Lifetime::Destroyed &&
        world.entities[1].resolved_hp == 0, "Center tank survived the nuke");
    require(world.entities[2].resolved_hp == 95,
        "Nuke falloff at 5 cells did not match the original damage math");
    require(world.entities[3].resolved_hp == 400, "Nuke reached beyond its 10 cell spread");
    require(chain_ok(world), "Nuke deaths broke the Cell chains");
    // A launch already in flight lands even if the silo is lost afterwards.
    auto in_flight = make_world();
    place(in_flight, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    place(in_flight, 2, 101, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    PrototypeSuperweapons flight(rules, exe);
    flight.update_grants(in_flight);
    for (int frame = 0; frame < 9000; ++frame) flight.tick(in_flight, static_cast<std::uint64_t>(frame));
    require(flight.fire(in_flight, 102, "MultiSpecial", {50, 50}, 9000).fired,
        "In-flight nuke launch rejected");
    in_flight.entities[0].lifetime = sw::Lifetime::Destroyed;
    flight.update_grants(in_flight);
    int flight_destroyed = 0, flight_nukes = 0;
    for (int frame = 9001; frame <= 12200; ++frame) {
        const auto impact = flight.tick(in_flight, static_cast<std::uint64_t>(frame));
        flight_destroyed += impact.destroyed;
        flight_nukes += impact.nukes;
    }
    require(flight_nukes == 1 && flight_destroyed == 1,
        "Losing the silo cancelled a nuke already in flight");
    // The recharge clock runs while the missile flies: 3200 charge frames passed.
    require(!supers.ready(102, "MultiSpecial") &&
        supers.frames_left(102, "MultiSpecial") == 9000 - 3200,
        "Nuke did not restart its charge");
    // The same launch replays identically.
    auto replay = make_world();
    place(replay, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    place(replay, 2, 101, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    place(replay, 3, 101, "HTNK", sw::Kind::Unit, {55, 50}, 400);
    place(replay, 4, 101, "HTNK", sw::Kind::Unit, {70, 50}, 400);
    PrototypeSuperweapons again(rules, exe);
    again.update_grants(replay);
    for (int frame = 0; frame < 9000; ++frame) again.tick(replay, static_cast<std::uint64_t>(frame));
    const auto second = again.fire(replay, 102, "MultiSpecial", {50, 50}, 9000);
    int replay_damaged = 0, replay_destroyed = 0;
    for (int frame = 9001; frame <= 12200; ++frame) {
        const auto impact = again.tick(replay, static_cast<std::uint64_t>(frame));
        replay_damaged += impact.damaged;
        replay_destroyed += impact.destroyed;
    }
    require(second.fired && replay_damaged == tick_damaged && replay_destroyed == tick_destroyed &&
        replay.entities[2].resolved_hp == world.entities[2].resolved_hp,
        "Nuke replay differed");
    // Two houses launch concurrently: both missiles stay in flight and both
    // impacts land (the pending queue must not drop a launch).
    auto both = make_world();
    place(both, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    place(both, 2, 101, "NAMISL", sw::Kind::Structure, {12, 10}, 1000);
    place(both, 3, 102, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    place(both, 4, 101, "HTNK", sw::Kind::Unit, {70, 50}, 400);
    PrototypeSuperweapons pair(rules, exe);
    pair.update_grants(both);
    for (int frame = 0; frame < 9000; ++frame) pair.tick(both, static_cast<std::uint64_t>(frame));
    require(pair.fire(both, 102, "MultiSpecial", {50, 50}, 9000).fired, "First concurrent launch rejected");
    require(pair.fire(both, 101, "MultiSpecial", {70, 50}, 9000).fired, "Second concurrent launch rejected");
    int pair_nukes = 0, pair_destroyed = 0;
    for (int frame = 9001; frame <= 12200; ++frame) {
        const auto impact = pair.tick(both, static_cast<std::uint64_t>(frame));
        pair_nukes += impact.nukes;
        pair_destroyed += impact.destroyed;
    }
    require(pair_nukes == 2 && pair_destroyed == 2,
        "Concurrent nuke launches did not both land");
    require(both.entities[2].lifetime == sw::Lifetime::Destroyed &&
        both.entities[3].lifetime == sw::Lifetime::Destroyed,
        "A concurrent nuke impact was lost");
}

void check_iron_curtain(const IniFile& rules, const std::string& exe) {
    auto world = make_world();
    place(world, 1, 102, "NAIRON", sw::Kind::Structure, {10, 10}, 1000);
    place(world, 2, 102, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    place(world, 3, 102, "E1", sw::Kind::Infantry, {50, 50}, 125);
    place(world, 4, 101, "HTNK", sw::Kind::Unit, {52, 50}, 400);
    place(world, 5, 102, "NAMISL", sw::Kind::Structure, {11, 10}, 1000);
    place(world, 6, 102, "HTNK", sw::Kind::Unit, {51, 50}, 400);
    PrototypeSuperweapons supers(rules, exe);
    supers.update_grants(world);
    for (int frame = 0; frame < 4500; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    const auto report = supers.fire(world, 102, "IronCurtainSpecial", {50, 50}, 4500);
    require(report.fired && report.shielded == 2 && report.destroyed == 1,
        "Iron Curtain did not shield the 3x3 block and kill the infantry");
    // The curtain covers the 3x3 block around the click (base 1.006
    // FUN_006B26D0 loops nine DAT_00A6F248 offsets); the enemy two cells away
    // stays unshielded.
    require(supers.invulnerable(6, 4500),
        "Iron Curtain missed a tank on the neighbouring Cell");
    require(supers.invulnerable(2, 4500) && supers.invulnerable(2, 5249) &&
        !supers.invulnerable(2, 5250), "Iron Curtain duration did not match 750 frames");
    require(!supers.invulnerable(4, 4500), "Iron Curtain shielded an enemy tank");
    // A nuke cannot hurt the curtained tank but kills the rest. The nuke is
    // ready at frame 9000 and its impact lands 3200 frames after the click, so
    // the curtain must be refreshed inside its 750 frame window of the impact.
    for (int frame = 4501; frame <= 9000; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(supers.fire(world, 102, "IronCurtainSpecial", {50, 50}, 9000).fired,
        "Second Iron Curtain was not charged");
    for (int frame = 9001; frame <= 13500; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    const auto nuke = supers.fire(world, 102, "MultiSpecial", {50, 50}, 13500);
    require(nuke.fired, "Second nuke was not charged");
    for (int frame = 13501; frame <= 16000; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(supers.fire(world, 102, "IronCurtainSpecial", {50, 50}, 16000).fired,
        "Third Iron Curtain was not charged");
    for (int frame = 16001; frame <= 16700; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(world.entities[1].lifetime == sw::Lifetime::Placed &&
        world.entities[1].resolved_hp == 400, "Nuke hurt a curtained tank");
    require(chain_ok(world), "Iron Curtain deaths broke the Cell chains");
}

void check_chrono(const IniFile& rules, const std::string& exe) {
    auto world = make_world();
    place(world, 1, 102, "GACSPH", sw::Kind::Structure, {10, 10}, 1000);
    place(world, 2, 102, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    place(world, 3, 102, "HTNK", sw::Kind::Unit, {51, 50}, 400);
    place(world, 4, 101, "HTNK", sw::Kind::Unit, {52, 50}, 400);
    place(world, 5, 102, "NAMISL", sw::Kind::Structure, {11, 10}, 1000);
    PrototypeSuperweapons supers(rules, exe);
    supers.update_grants(world);
    // The warp needs an armed sphere first.
    const auto early = supers.fire(world, 102, "ChronoWarpSpecial", {50, 50}, 0);
    require(!early.fired && !early.notes.empty(), "Chrono Warp fired without an armed sphere");
    for (int frame = 0; frame < 6300; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    // Source click first: the sphere click marks the block of objects to warp.
    const auto arm = supers.fire(world, 102, "ChronoSphereSpecial", {50, 50}, 6300);
    require(arm.fired, "ChronoSphere launch rejected");
    require(supers.ready(102, "ChronoSphereSpecial"),
        "Arming the ChronoSphere consumed its charge");
    // Destination click second: the warp click is where the block lands.
    const auto warp = supers.fire(world, 102, "ChronoWarpSpecial", {55, 50}, 6301);
    require(warp.fired && warp.teleported == 2, "Chrono Warp did not move the 3x3 block");
    const auto& moved = world.entities[1];
    require(moved.cell && moved.cell->x == 55 && moved.cell->y == 50 &&
        moved.position && moved.position->x == 55 * 256 + 128,
        "Warped tank did not land on the destination click");
    require(world.entities[2].cell && world.entities[2].cell->x == 56 &&
        world.entities[2].cell->y == 50, "Second tank did not warp with the block");
    // The enemy tank outside the source block stays put.
    require(world.entities[3].cell && world.entities[3].cell->x == 52 &&
        world.entities[3].cell->y == 50, "Chrono Warp moved an object outside the source block");
    require(chain_ok(world), "Chrono Warp broke the Cell chains");
    require(chain_length(world, {50, 50}) == 0 && chain_length(world, {55, 50}) == 1,
        "Warped tanks did not move between Cell chains");
    require(!supers.ready(102, "ChronoSphereSpecial") &&
        supers.frames_left(102, "ChronoSphereSpecial") == 6300,
        "Chrono Sphere did not restart its charge after the warp");
    const auto repeat = supers.fire(world, 102, "ChronoWarpSpecial", {50, 50}, 6302);
    require(!repeat.fired, "Chrono Warp fired twice on one charge");
    // A blocked landing Cell leaves the object in place. Source click on the
    // tank, destination click on the occupied Cell.
    auto blocked = make_world();
    place(blocked, 1, 102, "GACSPH", sw::Kind::Structure, {10, 10}, 1000);
    place(blocked, 2, 102, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    place(blocked, 3, 102, "GACNST", sw::Kind::Structure, {55, 50}, 1000);
    PrototypeSuperweapons blocked_supers(rules, exe);
    blocked_supers.update_grants(blocked);
    for (int frame = 0; frame < 6300; ++frame)
        blocked_supers.tick(blocked, static_cast<std::uint64_t>(frame));
    (void)blocked_supers.fire(blocked, 102, "ChronoSphereSpecial", {50, 50}, 6300);
    const auto blocked_warp = blocked_supers.fire(blocked, 102, "ChronoWarpSpecial", {55, 50}, 6301);
    require(blocked_warp.fired && blocked_warp.teleported == 0 &&
        blocked.entities[1].cell && blocked.entities[1].cell->x == 50,
        "Warp moved a tank onto an occupied Cell");
    // Losing the sphere after arming cancels the arm: the warp must not fire
    // through a stale arm.
    auto stale = make_world();
    place(stale, 1, 102, "GACSPH", sw::Kind::Structure, {10, 10}, 1000);
    place(stale, 2, 102, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    PrototypeSuperweapons stale_supers(rules, exe);
    stale_supers.update_grants(stale);
    for (int frame = 0; frame < 6300; ++frame) stale_supers.tick(stale, static_cast<std::uint64_t>(frame));
    (void)stale_supers.fire(stale, 102, "ChronoSphereSpecial", {50, 50}, 6300);
    stale.entities[0].lifetime = sw::Lifetime::Destroyed;
    stale_supers.update_grants(stale);
    const auto stale_warp = stale_supers.fire(stale, 102, "ChronoWarpSpecial", {55, 50}, 6301);
    require(!stale_warp.fired && stale_warp.teleported == 0 &&
        !stale_warp.notes.empty(), "Warp fired through a stale arm after the sphere was lost");
    require(stale.entities[1].cell && stale.entities[1].cell->x == 50,
        "Stale arm moved a tank");
    // Low power suspends the powered sphere: the warp click is rejected while
    // the brownout lasts.
    auto brown = make_world();
    place(brown, 1, 102, "GACSPH", sw::Kind::Structure, {10, 10}, 1000);
    place(brown, 2, 102, "HTNK", sw::Kind::Unit, {50, 50}, 400);
    PrototypeSuperweapons brown_supers(rules, exe);
    brown_supers.update_grants(brown);
    for (int frame = 0; frame < 6300; ++frame) brown_supers.tick(brown, static_cast<std::uint64_t>(frame));
    (void)brown_supers.fire(brown, 102, "ChronoSphereSpecial", {50, 50}, 6300);
    brown.houses[1].power_output = 0;
    brown.houses[1].power_drain = 100;
    brown_supers.tick(brown, 6301);
    const auto brown_warp = brown_supers.fire(brown, 102, "ChronoWarpSpecial", {55, 50}, 6302);
    require(!brown_warp.fired && brown_warp.teleported == 0 && !brown_warp.notes.empty(),
        "Warp fired while low power suspended the sphere");
    require(brown.entities[1].cell && brown.entities[1].cell->x == 50,
        "Low power warp moved a tank");
}

void check_paradrop(const IniFile& rules, const std::string& exe) {
    auto world = make_world();
    place(world, 1, 101, "AMRADR", sw::Kind::Structure, {10, 10}, 600);
    PrototypeSuperweapons supers(rules, exe);
    supers.update_grants(world);
    for (int frame = 0; frame < 3600; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    const auto report = supers.fire(world, 101, "AmericanParaDropSpecial", {50, 50}, 3600);
    require(report.fired && report.dropped == 8, "American drop did not land 8 paratroopers");
    std::size_t gi = 0;
    for (const auto& entity : world.entities)
        if (entity.type == "E1" && entity.house_id == 101 && entity.id != 1) ++gi;
    require(gi == 8, "Drop spawned the wrong infantry type or count");
    for (const auto& entity : world.entities) {
        if (entity.id <= 1) continue;
        require(entity.kind == sw::Kind::Infantry && entity.resolved_hp == 125 &&
            entity.lifetime == sw::Lifetime::Placed,
            "Paratrooper did not use the base E1 Strength");
        require(entity.cell && entity.position &&
            entity.position->x == entity.cell->x * 256 + 128,
            "Paratrooper is not centered on its Cell");
    }
    require(chain_ok(world), "Paratroopers broke the Cell chains");
    require(!supers.ready(101, "AmericanParaDropSpecial") &&
        supers.frames_left(101, "AmericanParaDropSpecial") == 3600,
        "Drop did not restart its charge");
    // The generic ParaDrop type uses PParatrooper.
    auto generic = make_world();
    place(generic, 1, 101, "CAAIRP", sw::Kind::Structure, {10, 10}, 600);
    PrototypeSuperweapons generic_supers(rules, exe);
    generic_supers.update_grants(generic);
    for (int frame = 0; frame < 3600; ++frame)
        generic_supers.tick(generic, static_cast<std::uint64_t>(frame));
    const auto drop = generic_supers.fire(generic, 101, "ParaDropSpecial", {50, 50}, 3600);
    require(drop.fired && drop.dropped == 6, "Generic ParaDrop did not land 6 paratroopers");
    require(chain_ok(generic), "Generic paratroopers broke the Cell chains");
}

void check_storm(const IniFile& rules, const std::string& exe) {
    auto world = make_world();
    place(world, 1, 102, "GAWEAT", sw::Kind::Structure, {10, 10}, 1000);
    for (std::uint32_t i = 0; i < 6; ++i)
        place(world, 2 + i, 101, "HTNK", sw::Kind::Unit,
              {static_cast<std::uint16_t>(48 + i), 50}, 400);
    PrototypeSuperweapons supers(rules, exe);
    supers.update_grants(world);
    for (int frame = 0; frame < 9000; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    require(supers.ready(102, "LightningStormSpecial"), "Weather Storm did not charge");
    require(supers.fire(world, 102, "LightningStormSpecial", {50, 50}, 9000).fired,
        "Weather Storm launch rejected");
    int damaged = 0;
    for (int frame = 9001; frame < 9800; ++frame)
        damaged += supers.tick(world, static_cast<std::uint64_t>(frame)).damaged;
    require(damaged > 0, "Weather Storm struck nothing");
    require(chain_ok(world), "Weather Storm broke the Cell chains");
    require(supers.frames_left(102, "LightningStormSpecial") > 0, "Storm did not restart charge");
}

void check_spy_plane() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 1, 101, "AMRADR", sw::Kind::Structure, {10, 10}, 600);
    PrototypeSuperweapons supers(rules);
    supers.update_grants(world);
    for (int frame = 0; frame < 3600; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    const auto report = supers.fire(world, 101, "SpyPlaneSpecial", {50, 50}, 3600);
    require(report.fired && report.revealed.size() >= 19 * 40,
        "Spy plane did not reveal its CameraRange band");
    bool band = true;
    for (const auto& cell : report.revealed)
        if (cell.y < 41 || cell.y > 59) band = false;
    require(band, "Spy plane reveal left the CameraRange band");
    require(chain_ok(world) && world.entities.size() == 1,
        "Spy plane changed the world");
}

void check_guards(const IniFile& rules) {
    auto world = make_world();
    place(world, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    PrototypeSuperweapons supers(rules);
    auto report = supers.fire(world, 102, "BogusSpecial", {50, 50}, 0);
    require(!report.fired && !report.notes.empty(), "Unknown section fired");
    report = supers.fire(world, 102, "MultiSpecial", {50, 50}, 0);
    require(!report.fired && !report.notes.empty(), "Uncharged weapon fired");
    world.entities[0].lifetime = sw::Lifetime::Destroyed;
    supers.update_grants(world);
    for (int frame = 0; frame < 9000; ++frame) supers.tick(world, static_cast<std::uint64_t>(frame));
    report = supers.fire(world, 102, "MultiSpecial", {50, 50}, 9000);
    require(!report.fired, "Weapon fired after its building was lost");
    report = supers.fire(world, 102, "MultiSpecial", {600, 50}, 9000);
    require(!report.fired, "Off map cell accepted");
    report = supers.fire(world, 999, "MultiSpecial", {50, 50}, 9000);
    require(!report.fired, "Unknown house fired");
    // A fixture without [SuperWeaponTypes] grants and fires nothing.
    IniFile plain;
    plain.parse("[General]\nCameraRange=9\n");
    PrototypeSuperweapons plain_supers(plain);
    plain_supers.update_grants(world);
    report = plain_supers.fire(world, 102, "MultiSpecial", {50, 50}, 0);
    require(!report.fired, "Rules without super weapons fired");
    // A missing nuke payload is validated before the charge is consumed.
    IniFile no_payload;
    no_payload.parse(
        "[SuperWeaponTypes]\n1=MultiSpecial\n"
        "[MultiSpecial]\nType=MultiMissile\nRechargeTime=1\n"
        "[BuildingTypes]\n1=NAMISL\n[NAMISL]\nSuperWeapon=MultiSpecial\n");
    auto no_payload_world = make_world();
    place(no_payload_world, 1, 102, "NAMISL", sw::Kind::Structure, {10, 10}, 1000);
    PrototypeSuperweapons payload_supers(no_payload);
    payload_supers.update_grants(no_payload_world);
    for (int frame = 0; frame < 900; ++frame) payload_supers.tick(no_payload_world, static_cast<std::uint64_t>(frame));
    const auto payload_report = payload_supers.fire(no_payload_world, 102, "MultiSpecial", {50, 50}, 900);
    require(!payload_report.fired && !payload_report.notes.empty(),
        "Nuke fired without a payload weapon");
    require(payload_supers.ready(102, "MultiSpecial"),
        "A failed payload validation consumed the nuke charge");
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: prototype_superweapon_test <base-game-directory>");
    const std::string game_dir = argv[1];
    check_parse();
    ra2::fs::MixFileSystem files(game_dir);
    files.mount_language(ra2::fs::GameVariant::RedAlert2);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    const auto bytes = files.read_file("RULES.INI");
    require(bytes.has_value(), "Installed base RULES.INI is missing");
    IniFile rules;
    rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    check_base_rules(rules);
    const std::string exe = game_dir + "/game.exe";
    check_charge(rules);
    check_low_power(rules);
    check_nuke(rules, exe);
    check_iron_curtain(rules, exe);
    check_chrono(rules, exe);
    check_paradrop(rules, exe);
    check_storm(rules, exe);
    check_spy_plane();
    check_guards(rules);
    std::cout << "Super weapon spot checks passed: rules parse, charging, low power, "
                 "nuke falloff, Iron Curtain, Chrono Warp, paratroopers, spy plane and guards.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
