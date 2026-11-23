// FUN_004e3660 (House::Can_Build) production gate, focused behavior tests.
// The fix: after the shared gates, a computer house returns buildable at
// once (004E3878); the prerequisite loop and the BuildLimit switch are
// only for a human. See prototype_ai_internal.h.
#include "prototype_ai_internal.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace sw = ra2::session::world;
using ra2::rules::IniFile;
using ra2::sim::PrototypeAI;
using ra2::sim::skirmish::Order;
constexpr std::uint32_t computer = 100, player = 200;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

sw::House house(std::uint32_t id, const std::string& country, bool human) {
    sw::House result;
    result.id = id;
    result.country = country;
    result.human_controlled = human;
    result.cash = 10000;
    result.tech_level = 10;
    return result;
}

sw::Entity actor(const IniFile& rules, std::uint32_t id, std::uint32_t owner,
                 std::string type, sw::Kind kind, ra2map::Cell cell) {
    sw::Entity result;
    result.id = id;
    result.house_id = owner;
    result.type = std::move(type);
    result.kind = kind;
    result.cell = cell;
    result.position = ra2::sim::Coord{cell.x * 256 + 128, cell.y * 256 + 128, 0};
    result.resolved_hp = rules.read_integer(result.type, "Strength", 100);
    result.maximum_hp = result.resolved_hp;
    result.lifetime = sw::Lifetime::Placed;
    result.active = true;
    result.in_limbo = false;
    result.on_map = true;
    return result;
}

IniFile parse(const std::string& text) {
    IniFile result;
    result.parse(text);
    return result;
}

// A computer house starts its AI only with a construction yard.
std::string yard_rules(const std::string& text) { return text + "[YARD]\nConstructionYard=yes\n"; }
void add_yard(const IniFile& rules, sw::Snapshot& world) {
    world.entities.push_back(actor(rules, 9, computer, "YARD", sw::Kind::Structure, {2, 2}));
}

IniFile gate_rules() {
    return parse(yard_rules(
        "[General]\nTeamDelays=100,100,100\n"
        "[InfantryTypes]\n0=FLAKT\n"
        "[FLAKT]\nName=Flak Trooper\nCost=100\nTechLevel=1\nStrength=100\nOwner=Local\n"
        "Prerequisite=NAHAND,NARADR\n"
        "[LIMIT]\nCost=100\nTechLevel=1\nOwner=Local\nBuildLimit=1\n"
        "[BANNED]\nCost=100\nTechLevel=1\nOwner=Local\nForbiddenHouses=Local\n"
        "[NEEDED]\nCost=100\nTechLevel=1\nOwner=Local\nRequiredHouses=Other\n"
        "[FOREIGN]\nCost=100\nTechLevel=1\nOwner=Other\n"
        "[PROMO]\nCost=100\nTechLevel=11\nOwner=Local\n"
        "[NO_TECH]\nCost=100\nOwner=Local\nTechLevel=-1\n"
        "[NAHAND]\nFactory=InfantryType\n"
        "[NARADR]\n"));
}

// Base 004E9C90 infantry choice: the team TaskForce wants one Flak Trooper.
// The house has the barracks (NAHAND) but no radar (NARADR). FUN_004e3660
// 004E3878 makes the computer house buildable at once, so the team member
// is requested from the barracks.
void check_computer_requests_team_flak() {
    const auto rules = gate_rules();
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,FLAKT\n"
        "[ScriptTypes]\n0=S\n[S]\n0=0,2\n[TeamTypes]\n0=T\n"
        "[T]\nTaskForce=TF\nScript=S\nMax=1\n"
        "[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {
        actor(rules, 5, computer, "NAHAND", sw::Kind::Structure, {4, 4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40, 40})};
    add_yard(rules, world);
    PrototypeAI policy(rules, &ai);
    const auto start = PrototypeAI::first_team_frame(rules, policy.difficulty(), 0);
    const auto orders = policy.tick(world, computer, player, start);
    require(policy.teams().size() == 1 && policy.teams()[0].team_type == "T",
        "Computer did not pick the Flak Trooper team");
    const auto build = std::find_if(orders.begin(), orders.end(), [](const Order& o) {
        return o.kind == ra2::sim::skirmish::OrderKind::Build && o.type == "FLAKT" && o.house == computer;
    });
    require(build != orders.end() && build->entity == 5,
        "Computer with NAHAND but no NARADR did not request the team Flak Trooper");
}

// computer_products scopes the 004E3878 computer bypass to the AI.INI
// product picker and harvester path. There, the shared gates (owner,
// forbidden and required houses, tech level, policy cash) still bind a
// computer house; the prerequisite and BuildLimit tests do not. The
// legacy policy (no computer_products) keeps every test, even for a
// computer house. A human always keeps all of them, including plan_node,
// which only ever skipped the tech test.
void check_human_and_common_gates() {
    const auto rules = gate_rules();
    using ra2::sim::ai_detail::Actors;
    using ra2::sim::ai_detail::buildable;
    const Actors none;
    const auto machine = house(computer, "Local", false);
    const auto human = house(player, "Local", true);

    // Product picker path: the computer needs NAHAND only; the human also
    // needs NARADR.
    require(buildable(rules, machine, none, none, "FLAKT", std::nullopt, false, true),
        "Computer house did not skip the human-only prerequisite test");
    require(!buildable(rules, human, none, none, "FLAKT", std::nullopt, false, true),
        "Human house bypassed the NARADR prerequisite");
    const auto radar = actor(rules, 6, player, "NARADR", sw::Kind::Structure, {8, 8});
    const auto barracks = actor(rules, 8, player, "NAHAND", sw::Kind::Structure, {10, 8});
    const Actors with_radar{&radar, &barracks};
    require(buildable(rules, human, none, with_radar, "FLAKT", std::nullopt, false, true),
        "Human house failed the prerequisite it does own");

    // The same split for a BuildLimit with the limit already reached.
    const auto limited = actor(rules, 7, computer, "LIMIT", sw::Kind::Unit, {6, 6});
    const Actors one_limit{&limited};
    require(buildable(rules, machine, one_limit, none, "LIMIT", std::nullopt, false, true),
        "Computer house did not skip the BuildLimit test");
    require(!buildable(rules, human, one_limit, none, "LIMIT", std::nullopt, false, true),
        "Human house bypassed the BuildLimit");

    // The earlier shared gates still bind the computer product path.
    require(!buildable(rules, machine, none, none, "BANNED", std::nullopt, false, true),
        "Computer ignored ForbiddenHouses");
    require(!buildable(rules, machine, none, none, "NEEDED", std::nullopt, false, true),
        "Computer ignored RequiredHouses");
    require(!buildable(rules, machine, none, none, "FOREIGN", std::nullopt, false, true),
        "Computer ignored Owner");
    require(!buildable(rules, machine, none, none, "PROMO", std::nullopt, false, true),
        "Computer ignored the tech level");
    require(!buildable(rules, machine, none, none, "FLAKT", 50, false, true),
        "Computer ignored the policy cash gate");

    // The legacy policy path keeps the conservative tests for a computer.
    require(!buildable(rules, machine, none, none, "FLAKT", std::nullopt),
        "Legacy policy lost its prerequisite test for the computer");
    require(!buildable(rules, machine, one_limit, none, "LIMIT", std::nullopt),
        "Legacy policy lost its BuildLimit test for the computer");

    // plan_node keeps its tech exemption (TechLevel=-1 passes) and gains
    // no other exemption: its prerequisite tests stay, computer or human.
    require(buildable(rules, machine, none, none, "NO_TECH", std::nullopt, true),
        "plan_node no longer passes TechLevel=-1 for the computer");
    require(buildable(rules, human, none, none, "NO_TECH", std::nullopt, true),
        "plan_node lost TechLevel=-1 for the human");
    require(!buildable(rules, machine, none, none, "NO_TECH", std::nullopt),
        "The legacy path stopped blocking TechLevel=-1");
}
} // namespace

int main() {
    int failures = 0;
    const auto run = [&](const char* name, void (*check)()) {
        try {
            check();
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAILED " << name << ": " << error.what() << '\n';
        }
    };
    run("check_computer_requests_team_flak", check_computer_requests_team_flak);
    run("check_human_and_common_gates", check_human_and_common_gates);
    if (failures) return 1;
    std::cout << "Prototype AI production gate checks passed: computer Flak Trooper request, human-only gates.\n";
    return 0;
}
