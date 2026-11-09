#pragma once

#include "skirmish_order.h"
#include "rules/ini_file.h"
#include "session/world/world.h"
#include <cstdint>
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
                         Difficulty difficulty = Difficulty::Easy);

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

    // Read-only state for tests and debug views.
    struct TeamView {
        std::string team_type, trigger;
        std::vector<std::uint32_t> members;
        bool forming = true;
        std::int32_t script_line = 0;
    };
    bool uses_ai_ini() const noexcept { return !triggers_.empty(); }
    Difficulty difficulty() const noexcept { return difficulty_; }
    std::size_t trigger_count() const noexcept { return triggers_.size(); }
    std::optional<double> trigger_weight(const std::string& id) const;
    std::vector<TeamView> teams() const;

    // Parsed AI.INI records. Public so tests can check the base data.
    struct TaskForce { std::string id; std::vector<std::pair<std::int32_t, std::string>> members; };
    struct ScriptStep { std::int32_t action = 0, argument = 0; };
    struct TeamType {
        std::string id, task_force, script;
        std::int32_t max = 1, tech_level = 0, priority = 5;
        bool base_defense = false;
    };
    struct Trigger {
        std::string id, name, team1, team2, house, condition_object;
        std::int32_t tech_level = 0, condition = -1, side = 0;
        std::int32_t number = 0, comparator = 0;
        double weight = 0, minimum = 0, maximum = 0;
        bool skirmish = false, base_defense = false, easy = false, normal = false, hard = false;
    };

private:
    struct Team {
        std::size_t type = 0, trigger = 0;
        std::vector<std::uint32_t> members;
        bool forming = true, guarding = false, moving = false;
        std::int32_t line = 0;
        std::int64_t wait_until = 0, deadline = 0;
        std::uint32_t target = 0;
        ra2map::Cell goal{};
    };
    struct Context;
    void select_team(const Context&);
    void fill_teams(Context&, std::vector<skirmish::Order>* build_orders);
    void run_teams(Context&, std::vector<skirmish::Order>&);
    void finish(Team&, bool success);
    bool condition(const Context&, const Trigger&) const;
    std::uint32_t random(std::uint32_t low, std::uint32_t high);

    rules::IniFile rules_;
    Difficulty difficulty_ = Difficulty::Easy;
    std::vector<TaskForce> task_forces_;
    std::vector<std::vector<ScriptStep>> scripts_; // index matches script_ids_
    std::vector<std::string> script_ids_;
    std::vector<TeamType> team_types_;
    std::vector<Trigger> triggers_;
    std::vector<double> weights_; // current trigger weights
    std::vector<Team> teams_;
    std::optional<std::int32_t> last_frame_;
    std::uint32_t computer_house_ = 0, player_house_ = 0, seed_ = 1;
    std::int64_t next_build_ = 0, next_attack_ = 0, next_team_ = 0;
};

} // namespace ra2::sim
