#include "prototype_triggers.h"
#include "fs/mix_bootstrap.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
namespace sw = ra2::session::world;
namespace tr = ra2::sim::triggers;
using ra2::rules::IniFile;
using ra2::sim::skirmish::OrderKind;

void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }

sw::House house(std::uint32_t id, const char* name, std::int32_t cash = 1000) {
    sw::House h; h.id = id; h.name = name; h.country = name; h.cash = cash; return h;
}
sw::Entity unit(std::uint32_t id, std::uint32_t owner, const char* type, sw::Kind kind, int x, int y) {
    sw::Entity e; e.id = id; e.house_id = owner; e.type = type; e.kind = kind;
    e.cell = ra2map::Cell{(std::uint16_t)x, (std::uint16_t)y};
    e.lifetime = sw::Lifetime::Placed; e.resolved_hp = 100; e.maximum_hp = 100;
    return e;
}
bool has(const tr::TickResult& r, tr::EffectKind k) {
    return std::any_of(r.effects.begin(), r.effects.end(), [&](const tr::Effect& e) { return e.kind == k; });
}

const char* synthetic = R"(
[Houses]
0=Me House
1=Foe House
[Waypoints]
0=50010
1=50020
2=50030
[CellTags]
50005=TAGA
[Triggers]
T0000001=Me,<none>,Timer win,0,1,1,1,0
T0000002=Me,<none>,Global one,0,1,1,1,0
T0000003=Me,<none>,Turn on,0,1,1,1,0
T0000004=Me,<none>,Late text,1,1,1,1,0
T0000005=Me,<none>,Repeat,0,1,1,1,2
T0000006=Me,<none>,Reinforce,0,1,1,1,0
T0000007=Me,<none>,Cell,0,1,1,1,0
[Events]
T0000001=1,13,0,2
T0000002=1,27,0,3
T0000003=1,8,0,0
T0000004=1,8,0,0
T0000005=1,13,0,1
T0000006=1,36,0,7
T0000007=1,1,0,1
[Actions]
T0000001=1,1,0,13,0,0,0,0,A
T0000002=2,28,0,3,0,0,0,0,A,2,0,13,0,0,0,0,A
T0000003=2,53,2,T0000004,0,0,0,0,A,56,0,7,0,0,0,0,A
T0000004=1,11,4,Mission:Hello,0,0,0,0,A
T0000005=1,19,7,Bonus,0,0,0,0,A
T0000006=2,7,1,TEAM0001,0,0,0,0,A,17,0,0,0,0,0,0,B
T0000007=1,11,4,Cell hit,0,0,0,0,A
[Tags]
TAGA=0,Cell tag,T0000007
[TaskForces]
0=TF000001
[TF000001]
Name=Two
0=2,MTNK
Group=-1
[ScriptTypes]
0=SC000001
[SC000001]
Name=Go
0=3,1
1=3,2
[TeamTypes]
0=TEAM0001
[TEAM0001]
Name=Go team
House=Me House
Waypoint=A
Script=SC000001
TaskForce=TF000001
Max=1
)";
}

int main(int argc, char** argv) try {
    // --- Synthetic map ---
    IniFile map; map.parse(std::string_view(synthetic));
    IniFile rules; rules.parse(std::string_view("[General]\nRevealTriggerRadius=4\n[BuildingTypes]\n0=GAPOWR\n"));
    {
        const auto logic = tr::MapLogic::parse(map);
        require(logic.triggers.size() == 7 && logic.triggers[1].events[0].type == 27 &&
                logic.triggers[1].actions.size() == 2 && logic.triggers[1].actions[1].type == 2,
                "Synthetic map parse failed");
        require(logic.triggers[3].disabled && logic.triggers[4].persistence == tr::Persistence::Persistent,
                "Trigger flags parse failed");
        require(tr::waypoint_from_letters("A") == 0 && tr::waypoint_from_letters("Z") == 25 &&
                tr::waypoint_from_letters("AA") == 26 && tr::waypoint_from_letters("BA") == 52 &&
                tr::waypoint_from_letters("AQ") == 42, "Waypoint letters wrong");
        require(logic.waypoints.at(0).x == 10 && logic.waypoints.at(0).y == 50, "Waypoint cell wrong");
        require(logic.team_types.size() == 1 && logic.task_forces[0].members[0].first == 2 &&
                logic.scripts[0].steps.size() == 2, "Team data parse failed");
        require(tr::count_action_types(logic).at(53) == 1 && tr::count_event_types(logic).at(8) == 2, "Counts wrong");
    }
    tr::PrototypeTriggers t(map, rules, 1);
    sw::Snapshot world;
    world.houses = {house(1, "Me House"), house(2, "Foe House")};
    world.entities = {unit(10, 1, "MTNK", sw::Kind::Unit, 5, 5), unit(11, 2, "MTNK", sw::Kind::Unit, 40, 40)};

    auto r = t.tick(world, 0);
    require(!has(r, tr::EffectKind::Win), "Early win at frame 0");
    // T3 ("any event") fires at once: enables T4 and sets local 7. Later
    // triggers in file order (T4 text, T6 reinforcement) see that in the same frame.
    require(t.local_set(7) && !t.trigger_alive("T0000003"), "Any-event trigger did not run its actions");
    require(has(r, tr::EffectKind::ShowText) && has(r, tr::EffectKind::Spawn), "Enable/local chain failed");
    std::uint32_t team = 0; int spawns = 0;
    for (auto& e : r.effects) if (e.kind == tr::EffectKind::Spawn) { team = e.team; ++spawns; require(e.text == "MTNK" && e.cell.x == 10 && e.cell.y == 50, "Spawn data wrong"); }
    require(spawns == 2 && has(r, tr::EffectKind::RevealAround), "Reinforcement effects wrong");
    for (auto& e : r.effects) if (e.kind == tr::EffectKind::RevealAround) require(e.number == 4, "Reveal radius not from rules");
    // Repeat trigger fires after 1 second = 15 frames, again after 15 more.
    int bonus = 0;
    for (int f = 2; f <= 31; ++f) { r = t.tick(world, f); bonus += has(r, tr::EffectKind::PlaySound); }
    require(bonus == 2, "Persistent timer trigger did not repeat");
    require(!has(r, tr::EffectKind::Win), "Win came before 2 seconds");
    r = t.tick(world, 31);
    require(r.effects.empty() && r.orders.empty(), "Repeated frame did something");
    // Timer win at 30 frames: should have fired already in loop (frame 30).
    require(!t.trigger_alive("T0000001"), "Elapsed-time trigger never fired");
    // Team: add the two spawned tanks and see the script move them.
    world.entities.push_back(unit(20, 1, "MTNK", sw::Kind::Unit, 10, 50));
    world.entities.push_back(unit(21, 1, "MTNK", sw::Kind::Unit, 10, 50));
    t.adopt(team, 20); t.adopt(team, 21);
    r = t.tick(world, 40);
    int moves = 0;
    for (auto& o : r.orders) if (o.kind == OrderKind::Move) { ++moves; require(o.cell.x == 20 && o.cell.y == 50, "Script move goal wrong"); }
    require(moves == 2, "Team did not get move orders for script step 1");
    for (auto& e : world.entities) if (e.id == 20 || e.id == 21) e.cell = ra2map::Cell{20, 50};
    r = t.tick(world, 56);
    moves = 0;
    for (auto& o : r.orders) if (o.kind == OrderKind::Move) { ++moves; require(o.cell.x == 30, "Second waypoint wrong"); }
    require(moves == 2, "Team did not advance to script step 2");
    for (auto& e : world.entities) if (e.id == 20 || e.id == 21) e.cell = ra2map::Cell{30, 50};
    for (int f = 88; f < 140; f += 16) t.tick(world, f);
    require(t.team_count() == 0, "Finished team stayed alive");
    // Entering a tagged cell raises "entered by" through the cell tag.
    world.entities.push_back(unit(30, 1, "E1", sw::Kind::Infantry, 5, 50));
    r = t.tick(world, 200);
    require(has(r, tr::EffectKind::ShowText) && !t.trigger_alive("T0000007"), "Cell tag did not fire its trigger");
    // Global variable and Win.
    IniFile map2; map2.parse(std::string_view(synthetic));
    tr::PrototypeTriggers t2(map2, rules, 1);
    require(!t2.global_set(3), "Global starts set");

    // --- Real maps ---
    if (argc >= 2) {
        ra2::fs::MixFileSystem files(argv[1]);
        files.mount_language(ra2::fs::GameVariant::RedAlert2);
        files.bootstrap(ra2::fs::GameVariant::RedAlert2);
        files.mount_overlays();
        // Movie archives may be missing; maps do not need them.
        try { files.mount_resources(); } catch (const std::exception&) {}
        std::vector<std::shared_ptr<ra2::fs::MixArchive>> all = files.mixes();
        for (auto& a : files.maps()) all.push_back(a);
        std::map<std::int32_t,int> events, actions;
        int maps = 0, with_triggers = 0, triggers = 0, bad_refs = 0;
        for (auto& a : all) for (auto& entry : a->entries()) {
            if (entry.size < 200 || entry.size > 3000000) continue;
            std::vector<std::uint8_t> bytes;
            try { bytes = a->read(entry); } catch (...) { continue; }
            std::string text(bytes.begin(), bytes.end());
            if (text.find("[Basic]") == std::string::npos || text.find("[Map]") == std::string::npos) continue;
            IniFile ini; ini.parse(std::string_view(text));
            // Comment text in AI files can mention both map section names.
            if (!ini.find_section("Basic") || !ini.find_section("Map")) continue;
            ++maps;
            auto logic = tr::MapLogic::parse(ini);
            if (logic.triggers.empty()) continue;
            ++with_triggers; triggers += (int)logic.triggers.size();
            for (auto& [k, v] : tr::count_event_types(logic)) events[k] += v;
            for (auto& [k, v] : tr::count_action_types(logic)) actions[k] += v;
            for (auto& tag : logic.tags) if (!logic.trigger(tag.trigger)) {
                ++bad_refs;
                std::cerr << a->name() << " entry=" << entry.id << " " << ini.read_string("Basic", "Name") << ": tag " << tag.id
                          << " has missing trigger " << tag.trigger << '\n';
            }
            for (auto& tt : logic.team_types) if (!logic.script(tt.script) || !logic.task_force(tt.task_force)) {
                ++bad_refs;
                std::cerr << a->name() << " entry=" << entry.id << " " << ini.read_string("Basic", "Name") << ": team " << tt.id
                          << " script=" << tt.script << " taskforce=" << tt.task_force << '\n';
            }
            // Every parsed trigger must run a few frames without trouble.
            tr::PrototypeTriggers run(ini, rules, 1);
            sw::Snapshot w;
            w.houses = {house(1, "Player House"), house(2, "BadGuy1 House")};
            for (int f = 0; f < 120; ++f) run.tick(w, f);
        }
        require(maps >= 100 && with_triggers >= 60 && triggers > 1000, "Too few real maps with triggers were read");
        require(bad_refs == 0, "A tag or team type points at a missing trigger, script or task force");
        auto top = [](const std::map<std::int32_t,int>& m) {
            std::vector<std::pair<int,std::int32_t>> v;
            for (auto& [k, c] : m) v.emplace_back(c, k);
            std::sort(v.rbegin(), v.rend());
            return v;
        };
        std::cout << "Real maps: " << maps << " read, " << with_triggers << " with triggers, " << triggers << " triggers\n";
        std::cout << "Top events (type:count):";
        for (std::size_t i = 0; i < std::min<std::size_t>(30, top(events).size()); ++i) std::cout << ' ' << top(events)[i].second << ':' << top(events)[i].first;
        std::cout << "\nTop actions (type:count):";
        for (std::size_t i = 0; i < std::min<std::size_t>(30, top(actions).size()); ++i) std::cout << ' ' << top(actions)[i].second << ':' << top(actions)[i].first;
        std::cout << '\n';
    }
    std::cout << "Prototype trigger checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
