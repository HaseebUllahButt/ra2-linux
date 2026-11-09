#include "prototype_repair_sell.h"
#include "fs/mix_bootstrap.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace sw = ra2::session::world;
namespace weapons = ra2::weapons;
using ra2::rules::IniFile;
using ra2::sim::PrototypeRepairSell;
using ra2::sim::PrototypeRepairSellRules;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

IniFile synthetic_rules() {
    IniFile rules;
    rules.parse(
        "[General]\n"
        "RepairPercent=15%\n"
        "RepairRate=.016\n"
        "RepairStep=8\n"
        "RefundPercent=50%\n"
        "AlliedSurvivorDivisor=500\n"
        "SovietSurvivorDivisor=250\n"
        "ThirdSurvivorDivisor=750\n"
        "[Sides]\nGDI=AlliedCountry\nNod=SovietCountry\n"
        "[AlliedCountry]\nSide=GDI\n"
        "[SovietCountry]\nSide=Nod\n"
        "[WORKS]\nCost=2000\nStrength=1000\nCrewed=yes\n"
        "[CHEAP]\nCost=500\nStrength=400\nCrewed=yes\n"
        "[BARE]\nCost=2000\nStrength=1000\nCrewed=no\n"
        "[PRICY]\nCost=6000\nStrength=1000\nCrewed=yes\n"
        "[E1]\nStrength=125\n"
        "[E2]\nStrength=100\n");
    return rules;
}

IniFile synthetic_art() {
    IniFile art;
    art.parse("[WORKS]\nFoundation=2x2\n[CHEAP]\nFoundation=3x2\n");
    return art;
}

sw::House house(std::uint32_t id, const std::string& country, std::int32_t cash,
                std::optional<std::int32_t> side = {}) {
    sw::House result;
    result.id = id;
    result.name = country;
    result.country = country;
    result.cash = cash;
    result.side_index = side;
    return result;
}

sw::Entity building(std::uint32_t id, std::uint32_t owner, const std::string& type,
                    ra2map::Cell cell, std::int32_t hp, std::int32_t max_hp) {
    sw::Entity result;
    result.id = id;
    result.house_id = owner;
    result.type = type;
    result.kind = sw::Kind::Structure;
    result.lifetime = sw::Lifetime::Placed;
    result.cell = cell;
    result.position = ra2::sim::Coord{cell.x * 256 + 128, cell.y * 256 + 128, 0};
    result.resolved_hp = hp;
    result.maximum_hp = max_hp;
    result.layer = sw::Layer::Ground;
    result.next = 0;
    result.active = true;
    result.in_limbo = false;
    result.on_map = true;
    return result;
}

// A small prototype world: one Allied house, one Soviet house, one building.
sw::Snapshot world_with_building(const IniFile& rules, const char* type = "WORKS",
                                 ra2map::Cell cell = {10, 10}) {
    sw::Snapshot world;
    world.edition = sw::Edition::Base;
    world.setup_progress = sw::SetupProgress::PrototypeComplete;
    world.houses = {house(1, "AlliedCountry", 5000), house(2, "SovietCountry", 5000)};
    const auto max = rules.read_integer(type, "Strength", 100);
    world.entities = {building(10, 1, type, cell, max, max)};
    world.creation_order = {{sw::CreatedKind::House, 1}, {sw::CreatedKind::House, 2},
                            {sw::CreatedKind::Entity, 10}};
    return world;
}

// Stage an n-by-n cell block with empty ground chains.
void stage_cells(sw::Snapshot& world, int x0, int y0, int span) {
    for (int y = y0; y < y0 + span; ++y)
        for (int x = x0; x < x0 + span; ++x) {
            sw::CellState state;
            state.allocated = true;
            state.heads = weapons::CellContentHeads{};
            state.ground_occupancy_124 = 0;
            state.cell_flag_100 = false;
            state.unit_land_speed = 1.0f;
            world.cells.emplace(std::uint32_t(y) * 512 + x, std::move(state));
        }
}

// Put one object id alone on a staged cell chain.
void occupy(sw::Snapshot& world, ra2map::Cell cell, std::uint32_t id) {
    auto& state = world.cells.at(std::uint32_t(cell.y) * 512 + cell.x);
    state.heads->ground_head = id;
    state.ground_occupancy_124 = 0x20;
}

void check_rules_load() {
    const auto rules = synthetic_rules();
    const auto limits = PrototypeRepairSellRules::load(rules);
    // The INI reader promotes rates through the original float storage, so
    // compare the rate with a tolerance.
    require(limits.repair_percent == 0.15 &&
            std::abs(limits.repair_rate_minutes - 0.016) < 1e-6 &&
            limits.repair_step == 8 && limits.refund_percent == 0.5 &&
            limits.allied_survivor_divisor == 500 && limits.soviet_survivor_divisor == 250 &&
            limits.third_survivor_divisor == 750, "Rules load lost an authored value");
    bool threw = false;
    try {
        IniFile broken;
        broken.parse("[General]\nRepairStep=8\n");
        PrototypeRepairSellRules::load(broken);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "Rules load accepted missing repair values");
}

void check_money_math(const IniFile& rules, const IniFile& art) {
    PrototypeRepairSell sell(rules, &art);
    // RA1 shape: Cost / (Strength / RepairStep) * RepairPercent, truncated.
    require(sell.repair_cost_per_step("WORKS") == 2, "War factory repair step cost changed");
    require(sell.repair_cost_per_step("CHEAP") == 1, "Cheap building repair step cost changed");
    require(sell.refund_amount("WORKS") == 1000, "War factory refund changed");
    require(sell.refund_amount("CHEAP") == 250, "Cheap building refund changed");
    const sw::House allied = house(1, "AlliedCountry", 0);
    const sw::House soviet = house(2, "SovietCountry", 0);
    require(sell.survivor_count(allied, "WORKS") == 2, "Allied war factory crew count changed");
    require(sell.survivor_count(soviet, "WORKS") == 4, "Soviet war factory crew count changed");
    require(sell.survivor_count(allied, "CHEAP") == 1, "Crew count minimum clamp changed");
    require(sell.survivor_count(allied, "PRICY") == 5, "Crew count maximum clamp changed");
    require(sell.survivor_count(allied, "BARE") == 0, "Uncrewed building dropped crew");
    require(std::string(sell.crew_type(allied)) == "E1" &&
            std::string(sell.crew_type(soviet)) == "E2", "Crew infantry type changed");
    // Side fallback through the [Sides] list when the house has no index.
    const sw::House no_index = house(3, "SovietCountry", 0);
    require(sell.survivor_count(no_index, "WORKS") == 4, "Side lookup ignored the country side");
    require(std::string(sell.crew_type(no_index)) == "E2", "Side lookup changed the crew type");
}

void check_repair(const IniFile& rules, const IniFile& art) {
    PrototypeRepairSell sell(rules, &art);
    auto world = world_with_building(rules);
    world.entities[0].resolved_hp = 500;

    require(!sell.set_repair(world, 99, true), "Repair started on an unknown building");
    require(!sell.set_repair(world, 10, true) == false, "Repair refused a damaged building");
    require(sell.repairing(10), "Repair state was not stored");
    require(sell.set_repair(world, 10, false) && !sell.repairing(10),
        "Repair stop did not clear the state");

    require(sell.set_repair(world, 10, true), "Repair did not restart");
    const auto idle = sell.tick(world, 13);
    require(idle.repaired.empty() && idle.spent == 0, "Repair ticked before the interval");
    require(world.houses[0].cash == 5000 && world.entities[0].resolved_hp == 500,
        "Idle repair tick changed the world");
    const auto first = sell.tick(world, 14);
    require(first.repaired.size() == 1 && first.repaired[0] == 10 && first.spent == 2,
        "First repair step did not charge or heal");
    require(world.houses[0].cash == 4998 && world.entities[0].resolved_hp == 508,
        "Repair step changed the wrong amount");
    require(sell.tick(world, 15).repaired.empty() && sell.tick(world, 27).repaired.empty(),
        "Repair ticked inside the interval");
    const auto second = sell.tick(world, 28);
    require(second.repaired.size() == 1 && world.entities[0].resolved_hp == 516,
        "Second repair interval did not heal");
    require(sell.tick(world, 28).repaired.empty(), "Repeated frame healed twice");
    require(sell.tick(world, 27).repaired.empty(), "Rewound frame healed");
    require(sell.tick(world, 28).repaired.size() == 1, "Rewound frame lost the next interval");

    // Full health stops the repair.
    world.entities[0].resolved_hp = 996;
    const auto last = sell.tick(world, 42);
    require(last.repaired.size() == 1 && world.entities[0].resolved_hp == 1000 &&
            !sell.repairing(10), "Repair did not stop at full health");
    require(sell.tick(world, 56).repaired.empty(), "Repair continued at full health");

    // A broke house stops the repair without healing.
    world.entities[0].resolved_hp = 500;
    world.houses[0].cash = 1;
    require(sell.set_repair(world, 10, true), "Repair did not restart for the broke house");
    const auto broke = sell.tick(world, 70);
    require(broke.repaired.empty() && broke.spent == 0 && !sell.repairing(10) &&
            world.entities[0].resolved_hp == 500 && world.houses[0].cash == 1,
        "Broke house paid for or lost health in a repair step");
    require(sw::validate(world), "Repair corrupted the world");
}

void check_sell(const IniFile& rules, const IniFile& art) {
    PrototypeRepairSell sell(rules, &art);
    auto world = world_with_building(rules);
    stage_cells(world, 8, 8, 8); // Cells 8..15 by 8..15.
    occupy(world, {10, 10}, 10);
    occupy(world, {11, 10}, 10);
    occupy(world, {10, 11}, 10);
    occupy(world, {11, 11}, 10);

    const auto result = sell.sell(world, 10);
    require(result.status == PrototypeRepairSell::SellResult::Status::Complete &&
            result.refund == 1000, "Selling a war factory did not pay the authored refund");
    require(world.houses[0].cash == 6000, "Sell refund did not reach the house cash");
    require(world.entities[0].lifetime == sw::Lifetime::Destroyed &&
            world.entities[0].resolved_hp == 0, "Sold building still lives");
    require(result.crew.size() == 2, "War factory crew count changed on sell");
    require(world.entities.size() == 3 && world.creation_order.size() == 5,
        "Crew creation did not register ids and order");
    for (const auto id : result.crew) {
        const auto it = std::find_if(world.entities.begin(), world.entities.end(),
            [&](const sw::Entity& entity) { return entity.id == id; });
        require(it != world.entities.end() && it->kind == sw::Kind::Infantry &&
                it->type == "E1" && it->house_id == 1 && it->lifetime == sw::Lifetime::Placed &&
                it->cell && it->resolved_hp == 125, "Crew member is wrong or misplaced");
    }
    require(world.entities[1].cell->x == 10 && world.entities[1].cell->y == 10 &&
            world.entities[2].cell->x == 9 && world.entities[2].cell->y == 9,
        "Crew did not take the nearest free cells");
    for (const auto cell : {ra2map::Cell{10, 10}, ra2map::Cell{11, 10}, ra2map::Cell{10, 11},
                            ra2map::Cell{11, 11}}) {
        const auto& state = world.cells.at(std::uint32_t(cell.y) * 512 + cell.x);
        require(state.heads && (state.heads->ground_head == 0 ||
            state.heads->ground_head == result.crew[0] || state.heads->ground_head == result.crew[1]),
            "Sold building kept a staged cell chain");
    }
    require(sw::validate(world), "Sell corrupted the world");
    require(!sell.repairing(10), "Sold building stayed in the repair set");

    // Guards.
    auto guarded = world_with_building(rules);
    stage_cells(guarded, 8, 8, 8);
    auto bad = sell.sell(guarded, 99);
    require(bad.status == PrototypeRepairSell::SellResult::Status::Invalid,
        "Sell accepted an unknown building");
    guarded.entities[0].kind = sw::Kind::Unit;
    bad = sell.sell(guarded, 10);
    require(bad.status == PrototypeRepairSell::SellResult::Status::Invalid,
        "Sell accepted a unit");
    guarded = world_with_building(rules);
    guarded.setup_progress = sw::SetupProgress::Unknown;
    bad = sell.sell(guarded, 10);
    require(bad.status == PrototypeRepairSell::SellResult::Status::NeedsInput,
        "Sell accepted an unfinished world");
    guarded = world_with_building(rules);
    guarded.pending_cell_setup = true;
    bad = sell.sell(guarded, 10);
    require(bad.status == PrototypeRepairSell::SellResult::Status::NeedsInput,
        "Sell accepted a pending Cell setup");
    guarded = world_with_building(rules);
    guarded.houses[0].cash = {};
    bad = sell.sell(guarded, 10);
    require(bad.status == PrototypeRepairSell::SellResult::Status::NeedsInput,
        "Sell accepted unresolved house cash");
}

void check_sell_blocked_exits(const IniFile& rules, const IniFile& art) {
    PrototypeRepairSell sell(rules, &art);
    // Only the footprint is staged. The freed footprint takes four of the
    // five PRICY crew members; the fifth finds no staged cell and is dropped,
    // like an original failed Unlimbo. The refund still happens.
    auto world = world_with_building(rules, "PRICY");
    stage_cells(world, 10, 10, 2);
    occupy(world, {10, 10}, 10);
    occupy(world, {11, 10}, 10);
    occupy(world, {10, 11}, 10);
    occupy(world, {11, 11}, 10);
    const auto partial = sell.sell(world, 10);
    require(partial.status == PrototypeRepairSell::SellResult::Status::Complete &&
            partial.crew.size() == 4 && partial.refund == 3000,
        "Blocked exits changed the sell result");
    require(world.entities.size() == 5, "Dropped crew member was created");
    require(sw::validate(world), "Blocked sell corrupted the world");

    // A neighbouring building keeps its cells blocked for the crew.
    auto crowded = world_with_building(rules);
    stage_cells(crowded, 8, 8, 8);
    occupy(crowded, {10, 10}, 10);
    occupy(crowded, {11, 10}, 10);
    occupy(crowded, {10, 11}, 10);
    occupy(crowded, {11, 11}, 10);
    crowded.entities.push_back(building(11, 1, "CHEAP", {8, 8}, 400, 400)); // 3x2 at 8..10, 8..9
    crowded.creation_order.push_back({sw::CreatedKind::Entity, 11});
    occupy(crowded, {9, 9}, 11);
    const auto tight = sell.sell(crowded, 10);
    require(tight.crew.size() == 2, "Neighbouring building changed the crew count");
    for (const auto id : tight.crew) {
        const auto it = std::find_if(crowded.entities.begin(), crowded.entities.end(),
            [&](const sw::Entity& entity) { return entity.id == id; });
        require(it->cell->x >= 8 && it->cell->x < 16 && it->cell->y >= 8 && it->cell->y < 16 &&
                !(it->cell->x >= 8 && it->cell->x <= 10 && it->cell->y >= 8 && it->cell->y <= 9),
            "Crew entered a neighbouring building footprint");
    }
    require(sw::validate(crowded), "Crowded sell corrupted the world");
}

void check_real_data(const char* game_dir) {
    ra2::fs::MixFileSystem files(game_dir);
    files.mount_language(ra2::fs::GameVariant::RedAlert2);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    const auto rules_bytes = files.read_file("RULES.INI");
    const auto art_bytes = files.read_file("ART.INI");
    require(rules_bytes.has_value() && art_bytes.has_value(), "Installed RULES or ART INI missing");
    IniFile rules, art;
    rules.parse(std::string_view(reinterpret_cast<const char*>(rules_bytes->data()), rules_bytes->size()));
    art.parse(std::string_view(reinterpret_cast<const char*>(art_bytes->data()), art_bytes->size()));

    PrototypeRepairSell sell(rules, &art);
    require(sell.repair_cost_per_step("GAWEAP") == 2, "Base war factory repair step cost changed");
    require(sell.repair_cost_per_step("GAPILE") == 1, "Base barracks repair step cost changed");
    require(sell.refund_amount("GAWEAP") == 1000, "Base war factory refund changed");
    require(sell.refund_amount("GAPILE") == 250, "Base barracks refund changed");
    const sw::House allied = house(1, "Americans", 0);
    const sw::House soviet = house(2, "Russians", 0);
    require(sell.survivor_count(allied, "GAWEAP") == 2, "Base Allied crew count changed");
    require(sell.survivor_count(soviet, "GAWEAP") == 4, "Base Soviet crew count changed");
    require(std::string(sell.crew_type(allied)) == "E1" &&
            std::string(sell.crew_type(soviet)) == "E2", "Base crew types changed");

    // Sell a staged Allied war factory on its 5x3 foundation.
    auto world = world_with_building(rules, "GAWEAP", {20, 20});
    world.houses[0].country = "Americans";
    stage_cells(world, 16, 16, 12); // Cells 16..27 by 16..27.
    for (int dy = 0; dy < 3; ++dy)
        for (int dx = 0; dx < 5; ++dx)
            occupy(world, {static_cast<std::uint16_t>(20 + dx), static_cast<std::uint16_t>(20 + dy)}, 10);
    const auto result = sell.sell(world, 10);
    require(result.status == PrototypeRepairSell::SellResult::Status::Complete &&
            result.refund == 1000 && result.crew.size() == 2,
        "Base war factory sell did not refund and crew as authored");
    for (const auto id : result.crew) {
        const auto it = std::find_if(world.entities.begin(), world.entities.end(),
            [&](const sw::Entity& entity) { return entity.id == id; });
        require(it->type == "E1" && it->cell->x >= 16 && it->cell->x < 28 &&
                it->cell->y >= 16 && it->cell->y < 28, "Base crew left the staged ground");
    }
    // The whole 5x3 foundation lost the sold building.
    for (int dy = 0; dy < 3; ++dy)
        for (int dx = 0; dx < 5; ++dx) {
            const auto key = std::uint32_t(20 + dy) * 512 + 20 + dx;
            const auto& state = world.cells.at(key);
            require(state.heads->ground_head != 10, "Sold factory kept a foundation cell");
        }
    require(sw::validate(world), "Base data sell corrupted the world");

    // Repair a damaged base war factory over time.
    auto repair_world = world_with_building(rules, "GAWEAP", {20, 20});
    repair_world.entities[0].resolved_hp = 500;
    require(sell.set_repair(repair_world, 10, true), "Base repair did not start");
    const auto step = sell.tick(repair_world, 14);
    require(step.repaired.size() == 1 && step.spent == 2 &&
            repair_world.entities[0].resolved_hp == 508 &&
            repair_world.houses[0].cash == 4998, "Base repair step changed the wrong amounts");
    require(sw::validate(repair_world), "Base repair corrupted the world");
}
} // namespace

int main(int argc, char** argv) try {
    check_rules_load();
    const auto rules = synthetic_rules();
    const auto art = synthetic_art();
    check_money_math(rules, art);
    check_repair(rules, art);
    check_sell(rules, art);
    check_sell_blocked_exits(rules, art);
    if (argc == 2) check_real_data(argv[1]);
    std::cout << "Prototype repair and sell checks passed"
              << (argc == 2 ? " against installed base rules and art.\n" : ".\n");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
