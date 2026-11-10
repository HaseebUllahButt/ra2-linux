#pragma once

#include "skirmish_order.h"
#include "rules/ini_file.h"
#include "session/world/world.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {

// TODO-ORIGINAL: A small native opponent, not the recovered Team/Trigger AI.
// One instance serves one match. Only authored rules, AI.INI data, team state
// and decision clocks are stored here. The caller owns and executes every order
// in the shared world.
//
// Without AI.INI the opponent keeps the old policy: build a base, build the
// cheapest tank and send every fighter at the closest enemy.
// With base AI.INI the opponent picks teams from [AITriggerTypes] by weight,
// fills each team's [TaskForces] entry from its own factories or idle units,
// and runs a small part of the team's [ScriptTypes] entry.
class PrototypeAI {
public:
    // Human menu order. Rules lists such as TeamDelays are read hardest first.
    enum class Difficulty : std::int32_t { Easy = 0, Normal = 1, Hard = 2 };

    explicit PrototypeAI(const rules::IniFile& base_rules, const rules::IniFile* base_ai = nullptr,
                         Difficulty difficulty = Difficulty::Easy, const rules::IniFile* base_art = nullptr);

    static constexpr std::int32_t build_interval_frames = 90;
    static constexpr std::int32_t attack_interval_frames = 45;

    // Deploy, then Build, then Attack/Move in ascending actor ID order. Build
    // orders identify a live factory and use its cell as the production anchor.
    // The runtime must reject duplicate requests while a factory is busy.
    // Repeated frames do nothing. A frame rewind or house change resets clocks
    // and teams.
    std::vector<skirmish::Order> tick(const session::world::Snapshot&,
        std::uint32_t computer_house_id, std::uint32_t player_house_id,
        std::int32_t game_frame);
    void reset() noexcept;

    // Base 004E2E40 starts the House team timer when the difficulty is set:
    // TeamDelays[difficulty] + 175 * house array index (offset 0x30).
    // HouseAI 004E4110 calls the team picker 006BED50 only when it expires.
    static std::int32_t first_team_frame(const rules::IniFile& rules, Difficulty difficulty,
                                         std::int32_t house_index);

    // Read-only state for tests and debug views.
    struct TeamView {
        std::string team_type, trigger;
        std::vector<std::uint32_t> members;
        bool forming = true;   // not yet IsMoving (+7F)
        std::int32_t script_line = 0;
        std::uint32_t target = 0;            // QueuedFocus (+3C) object
        std::optional<ra2map::Cell> goal;    // QueuedFocus (+3C) Cell
    };
    bool uses_ai_ini() const noexcept { return !triggers_.empty(); }
    Difficulty difficulty() const noexcept { return difficulty_; }
    std::size_t trigger_count() const noexcept { return triggers_.size(); }
    std::optional<double> trigger_weight(const std::string& id) const;
    std::vector<TeamView> teams() const;
    // Base 004EF890 base plan, in build order. An empty name is a defense
    // node that 004F1190 has not resolved yet.
    std::vector<std::string> base_plan() const {
        std::vector<std::string> names;
        for (const auto& node : plan_) names.push_back(node.type);
        return names;
    }
    // A wall node that the plan asks to build: the type and the Cell to
    // place it at. The caller places the finished wall at that Cell.
    struct WallRequest { std::string type; ra2map::Cell cell; };
    std::optional<WallRequest> wall_request() const { return wall_request_; }
    // The Cell of each node (House +55F8 node +4); only wall nodes have one.
    std::vector<std::optional<ra2map::Cell>> base_plan_cells() const {
        std::vector<std::optional<ra2map::Cell>> cells;
        for (const auto& node : plan_) cells.push_back(node.cell);
        return cells;
    }

    // The Super Weapons game option (00A3D2C3), default on. With the
    // option off, 004EF890 and 004EF600 leave out each building whose
    // SuperWeapon is DisableableFromShell. Set it before the first tick;
    // the plan is made once.
    void set_super_weapons(bool allowed) noexcept { super_weapons_ = allowed; }
    bool super_weapons() const noexcept { return super_weapons_; }

    // Base 0044B480: the construction yard factory calls Exit_Object
    // (00440520) for a finished building. A result of 0 means that no
    // place was found. For a Naval building the owner house then clears
    // +138, and the factory abandons the product (004B97C0 refunds it).
    // The caller reports each full failed placement search here; other
    // houses are ignored.
    void placement_failed(std::uint32_t house, const std::string& type);
    // House +138, set by the House constructor 004E18C0. When it is clear,
    // 004E9660 removes Naval nodes from the base plan.
    bool naval_allowed() const noexcept { return naval_; }
    // House +53BC as a Cell, once the house has had a construction yard.
    std::optional<ra2map::Cell> base_centre(std::uint32_t house) const {
        const auto found = base_centres_.find(house);
        if (found == base_centres_.end()) return std::nullopt;
        return found->second;
    }

    // Parsed AI.INI records. Public so tests can check the base data.
    // TaskForceClass holds up to six entries (Team +84 CountObjects[6]).
    struct TaskForce { std::string id; std::vector<std::pair<std::int32_t, std::string>> members; };
    struct ScriptStep { std::int32_t action = 0, argument = 0; };
    // TeamTypeClass fields as 006BF770 writes them.
    struct TeamType {
        std::string id, task_force, script;
        std::int32_t max = 1, tech_level = 0, priority = 5;  // +B8, +C4, +B4
        bool base_defense = false;                          // +EE
        bool guard_slower = false, recruiter = false;       // +A7, +A8
        bool autocreate = false, prebuild = false;          // +A9, +AA
        bool reinforce = false, whiner = false;             // +AB, +AC
        bool aggressive = false, loose_recruit = false;     // +AD, +AE
        bool suicide = false, annoyance = false;            // +AF, +A6
        bool recruitable = true;                            // +ED AreTeamMembersRecruitable
        bool only_target_house_enemy = false;               // +EF
    };
    struct Trigger {
        std::string id, name, team1, team2, house, condition_object;
        std::int32_t tech_level = 0, condition = -1, side = 0;
        std::int32_t number = 0, comparator = 0;
        double weight = 0, minimum = 0, maximum = 0;
        bool skirmish = false, base_defense = false, easy = false, normal = false, hard = false;
    };

private:
    // A Team target: an object (Abstract pointer) or a Cell.
    struct Focus {
        std::uint32_t object = 0;
        std::optional<ra2map::Cell> cell;
        bool empty() const noexcept { return !object && !cell; }
    };
    // What this policy last told a member. The original reads the unit's
    // own Mission (+A8), NavCom (+480) and TarCom; the caller owns those,
    // so the orders given are remembered here instead.
    struct Member {
        std::string type;
        std::int32_t mission = 5;  // Guard after production
        std::optional<ra2map::Cell> destination; // NavCom
        std::uint32_t target = 0;                // TarCom
        std::int64_t order_frame = -100;
        bool initiated = false;    // Foot +579, set when the member joins the group
        bool leader = false;       // Foot +165, the first member added
        bool recruitable = true;   // Techno flag from the last team's AreTeamMembersRecruitable
    };
    // TeamClass (base 006B7430 constructor, 0x9C bytes).
    struct Team {
        std::size_t type = 0, trigger = 0;         // +24; the AI trigger, or npos
        std::vector<std::uint32_t> members;        // +54 FirstUnit list, newest first
        std::array<std::int32_t, 6> quantity{};    // +84 CountObjects
        std::int64_t created = 0;                  // +50 CreationFrame
        std::uint32_t closest = 0;                 // +38 ClosestMember
        std::optional<ra2map::Cell> zone;          // +34 Zone (a Cell)
        Focus queued, focus;                       // +3C QueuedFocus, +40 Focus
        std::int64_t guard_until = 0;              // +58 GuardAreaTimer
        bool forced_active = false, has_been = false, full = false;  // +77 +78 +79
        bool under = true, reforming = false, lagging = false;       // +7A +7B +7C
        bool altered = true, moving = false, next_mission = true;    // +7D +7F +80
        bool succeeded = false;                    // +83, set by script action 49
        std::int32_t line = -1;                    // Script +2C CurrentMission
        bool dead = false;                         // deleted; removed at the next safe point
    };
    struct Context;
    std::size_t team_type_index(const std::string& id) const;
    bool factories_for(const Context&, std::size_t type) const;
    std::int32_t team_count(std::size_t type) const;
    std::int32_t defense_teams() const;
    bool trigger_ready(const Context&, std::size_t trigger, bool enough_defense) const;
    std::vector<std::pair<std::size_t, std::size_t>> pick_teams(const Context&);
    void make_team(const Context&, std::size_t type, std::size_t trigger);
    const Team* team_of(std::uint32_t id) const;
    void team_ai(Context&, Team&, std::vector<skirmish::Order>&);
    void recruit(Context&, Team&, std::size_t entry);
    bool can_add(const Context&, const Team&, const session::world::Entity&, std::size_t& entry) const;
    void add_member(Context&, Team&, std::uint32_t id, std::size_t entry);
    void remove_member(Context&, Team&, std::uint32_t id);
    void calc_center(const Context&, Team&) const;
    void retreat(Context&, Team&, std::vector<skirmish::Order>&);
    bool coordinate_regroup(Context&, Team&, std::vector<skirmish::Order>&);
    void coordinate_move(Context&, Team&, std::vector<skirmish::Order>&);
    void coordinate_attack(Context&, Team&, std::vector<skirmish::Order>&);
    void coordinate_do(Context&, Team&, std::int32_t mission, std::vector<skirmish::Order>&);
    void run_step(Context&, Team&, bool first, std::vector<skirmish::Order>&);
    void assign(Context&, std::uint32_t id, std::int32_t mission, std::optional<ra2map::Cell> destination,
                std::uint32_t target, std::vector<skirmish::Order>&);
    void assign_mission_target(Context&, Team&, Focus, std::vector<skirmish::Order>&);
    bool has_navcom(const Context&, std::uint32_t id);
    bool has_tarcom(const Context&, std::uint32_t id) const;
    std::optional<ra2map::Cell> focus_cell(const Context&, const Focus&) const;
    std::int32_t current_action(const Team&) const;
    std::uint32_t leader(const Context&, const Team&) const;
    const session::world::Entity* pick_building(const Context&, std::uint32_t unit, std::int32_t argument,
                                                bool enemy) const;
    std::optional<ra2map::Cell> gather_point(Context&, const Team&, bool at_enemy);
    void finish(Team&);
    void house_ai(Context&, std::vector<skirmish::Order>&);
    void old_policy(Context&, bool build_due, bool attack_due, std::vector<skirmish::Order>&);
    void choose_products(Context&, std::vector<skirmish::Order>&);
    std::string choose_harvester(const Context&) const;
    std::string choose_product(const Context&, const char* registry, session::world::Kind);
    bool recruitable(const Context&, const session::world::Entity&) const;
    std::vector<std::string> missing_types(const Context&, const Team&) const;
    const TaskForce* task_force(const TeamType&) const;
    void make_plan(const Context&);
    std::string plan_build(const Context&);
    std::array<std::int32_t, 3> defense_values(const std::string& type) const;
    std::string defense_choice(const Context&);
    bool plan_walls(const Context&, std::size_t node);
    bool condition(const Context&, const Trigger&) const;
    std::uint32_t random(std::uint32_t low, std::uint32_t high);

    rules::IniFile rules_;
    Difficulty difficulty_ = Difficulty::Easy;
    std::vector<TaskForce> task_forces_;
    std::vector<std::vector<ScriptStep>> scripts_; // index matches script_ids_
    std::vector<std::string> script_ids_;
    std::vector<TeamType> team_types_;
    std::vector<bool> authored_autocreate_; // the picker sets Autocreate (+A9) on its picks
    std::vector<Trigger> triggers_;
    std::vector<double> weights_; // current trigger weights
    std::vector<Team> teams_;
    std::map<std::uint32_t, Member> members_; // by object ID, for team members
    std::int32_t ratio_ai_trigger_team_ = 100; // House +554C (004E18C0)
    bool ai_started_ = false;                   // House +13A (004F4070)
    // Base House +53BC: the last seen construction yard Cell. It stays when
    // the yard dies, as the original does not clear it.
    std::map<std::uint32_t, ra2map::Cell> base_centres_;
    // House +55F8 base nodes (16 bytes each): +0 the BuildingType (by
    // name; empty for a defense node, -1) and +4 the Cell.
    struct PlanNode { std::string type; std::optional<ra2map::Cell> cell; };
    std::vector<PlanNode> plan_;
    std::optional<WallRequest> wall_request_;
    const rules::IniFile* art_ = nullptr; // ART.INI: real building foundations.
    bool super_weapons_ = true;     // 00A3D2C3
    bool naval_ = true;             // House +138
    // House +5540, +5544, +5548: the vehicle, infantry and aircraft to make
    // next, and the product of each choice once a factory has it.
    std::array<std::string, 3> choice_;
    std::array<std::uint32_t, 3> choice_product_{};
    std::optional<std::int32_t> last_frame_;
    std::uint32_t computer_house_ = 0, player_house_ = 0;
    Random rng_{1}; // 006388A0 draws, on a private stream
    std::int64_t next_build_ = 0, next_attack_ = 0, next_team_ = 0; // next_team_: House +5684 TeamTime
};

} // namespace ra2::sim
