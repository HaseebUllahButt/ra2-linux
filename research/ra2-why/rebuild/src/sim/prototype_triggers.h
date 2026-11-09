#pragma once
// Prototype map triggers and scripted teams for the playable native skirmish.
// It reads [Triggers] [Events] [Actions] [Tags] [TaskForces] [ScriptTypes]
// [TeamTypes] [Waypoints] [CellTags] from a map INI and runs them once per
// frame. It never changes the world. It returns Effects (things the app must
// show or do) and skirmish Orders (unit commands for the match to run).
#include "skirmish_order.h"
#include "rules/ini_file.h"
#include "session/world/world.h"
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace ra2::sim::triggers {

// ---- Parsed map data -------------------------------------------------------
struct EventDef { std::int32_t type=0, p1=0, p2=0; };
// Original TActionClass: type, six parameters, then a waypoint name.
struct ActionDef {
    std::int32_t type=0;
    std::array<std::string,6> param;   // p1..p6 as written in the map
    std::int32_t waypoint=-1;          // last field, letters A=0 Z=25 AA=26
    std::int32_t number(int i) const;  // param[i] as an integer, 0 if not one
};
enum class Persistence { Volatile=0, SemiPersistent=1, Persistent=2 };
struct TriggerDef {
    std::string id, house, linked, name;
    bool disabled=false, easy=true, medium=true, hard=true;
    Persistence persistence=Persistence::Volatile;
    std::vector<EventDef> events;
    std::vector<ActionDef> actions;
};
struct TagDef { std::string id, name, trigger; std::int32_t repeat=0; };
struct TaskForceDef {
    std::string id, name;
    std::vector<std::pair<std::int32_t,std::string>> members; // count, type
};
struct ScriptStep { std::int32_t type=0, param=0; };
struct ScriptDef { std::string id, name; std::vector<ScriptStep> steps; };
struct TeamTypeDef {
    std::string id, name, house, script, task_force;
    std::int32_t waypoint=-1, max=1, priority=0;
    bool autocreate=false, suicide=false, aggressive=false, full=false;
};

// Waypoint letters to an index: "A"=0, "Z"=25, "AA"=26, "BA"=52. -1 if bad.
std::int32_t waypoint_from_letters(const std::string&);
// Parses "3,GHOST"-style counted type lists and the sections above.
struct MapLogic {
    std::vector<TriggerDef> triggers;
    std::vector<TagDef> tags;
    std::vector<TaskForceDef> task_forces;
    std::vector<ScriptDef> scripts;
    std::vector<TeamTypeDef> team_types;
    std::map<std::int32_t,ra2map::Cell> waypoints;
    std::map<std::uint32_t,std::string> cell_tags; // key is y*1000+x as in the file
    std::vector<std::string> house_names;          // [Houses] in map order
    static MapLogic parse(const rules::IniFile& map);
    const TriggerDef* trigger(const std::string& id) const;
    const TeamTypeDef* team_type(const std::string& id) const;
    const TaskForceDef* task_force(const std::string& id) const;
    const ScriptDef* script(const std::string& id) const;
    const TagDef* tag(const std::string& id) const;
};

// ---- Counting helpers used by the report and the tests ---------------------
// How often each event or action type appears in a map's triggers.
std::map<std::int32_t,int> count_event_types(const MapLogic&);
std::map<std::int32_t,int> count_action_types(const MapLogic&);

// ---- Runtime outputs -------------------------------------------------------
enum class EffectKind {
    Win, Lose, ShowText, PlaySound, PlayMusic, PlaySpeech, PlayMovie,
    RevealAll, RevealAround, RevealZone, CenterView, FireSale, BeginProduction,
    BeginAutocreate, Spawn, PlayAnim, Explosion, DropFlare, Reshroud,
    LockInput, UnlockInput, IonStormStart, IonStormEnd, ChangeHouse, SetAlly,
    SetEnemy, DestroyAttached, ResizeView, Lighting, TimerStarted, TimerStopped,
    SoundAt, DeleteMember
};
struct Effect {
    EffectKind kind=EffectKind::ShowText;
    std::uint32_t house=0;       // acting house id, 0 if none
    std::uint32_t other=0;       // second house, or entity id
    ra2map::Cell cell{};
    std::int32_t number=0;       // radius, index, frames ...
    std::string text;            // text key, sound or type name
    std::uint32_t team=0;        // team instance for Spawn
};
struct TickResult {
    std::vector<Effect> effects;
    std::vector<skirmish::Order> orders;
};

// Event numbers raised by the match (the original raises them from objects).
enum class RaisedEvent : std::int32_t {
    EnteredBy=1, Spied=2, Thieved=3, DiscoveredByPlayer=4, HouseDiscovered=5,
    AttackedByAny=6, DestroyedByAny=7, BridgeDestroyed=31, SelectedByPlayer=33,
    FirstDamaged=38, HalfHealth=39, QuarterHealth=40, AttackedByHouse=44
};

class PrototypeTriggers {
public:
    // difficulty: 0 easy, 1 normal, 2 hard. Triggers not set for it are off.
    PrototypeTriggers(const rules::IniFile& map, const rules::IniFile& rules,
                      std::int32_t difficulty=1);

    // One call per game frame. Repeated frames do nothing.
    TickResult tick(const session::world::Snapshot&, std::int32_t frame);

    // Bind a map object to the tag it carries. tick() also does this for
    // structures, units and infantry that the map file lists with a tag.
    void attach_tag(std::uint32_t entity_id, const std::string& tag_id);
    // Raise an object event. Triggers reach it through the entity's tag
    // (or the cell tag), and global events reach every trigger of that type.
    void raise(RaisedEvent event, std::uint32_t house_id,
               std::uint32_t entity_id=0, ra2map::Cell cell={});
    // The match calls this for every unit made by a Spawn effect.
    void adopt(std::uint32_t team_instance, std::uint32_t entity_id);

    // State for the app and tests.
    const MapLogic& logic() const noexcept { return logic_; }
    bool trigger_enabled(const std::string& id) const;
    bool trigger_alive(const std::string& id) const;
    bool global_set(std::int32_t i) const;
    bool local_set(std::int32_t i) const;
    std::int32_t mission_timer_frames_left(std::int32_t frame) const;
    std::size_t team_count() const noexcept { return teams_.size(); }
    std::vector<std::uint32_t> team_members(std::uint32_t instance) const;
    std::vector<std::uint32_t> team_ids() const;

private:
    struct TriggerState {
        bool alive=true, enabled=true;
        std::int32_t timer_start=-1;
        std::uint64_t occurred=0;     // bit per raised event index
        bool fired_once=false;
    };
    struct Team {
        std::uint32_t instance=0, house=0;
        std::size_t type=0;           // index into logic_.team_types
        std::vector<std::uint32_t> members;
        std::int32_t pc=0, wait_until=0;
        std::uint32_t target=0;       // current Attack target
        ra2map::Cell goal{};
        bool moving=false, pending_spawn=false, done=false;
        std::int32_t spawn_expected=0;
    };
    struct Ctx;                       // one tick's working data
    bool event_true(const Ctx&, std::size_t trigger, std::size_t event_index);
    void fire(Ctx&, std::size_t trigger);
    bool run_action(Ctx&, std::size_t trigger, const ActionDef&);
    bool create_team(Ctx&, const std::string& type_id, std::uint32_t house, bool spawn);
    void run_teams(Ctx&);
    std::uint32_t resolve_house(const session::world::Snapshot&, const std::string&) const;
    std::uint32_t house_by_index(const session::world::Snapshot&, std::int32_t) const;
    std::int32_t index_of_house(const session::world::Snapshot&, std::uint32_t) const;
    int type_index(const std::string& list, const std::string& type) const;
    std::string type_name(const std::string& list, std::int32_t index) const;

    MapLogic logic_;
    rules::IniFile rules_, map_;
    std::int32_t difficulty_;
    std::vector<TriggerState> state_;
    std::map<std::string,std::size_t> trigger_index_;
    std::array<bool,50> globals_{};
    std::array<bool,100> locals_{};
    std::int32_t timer_start_=-1, timer_length_=0;   // mission timer
    bool timer_running_=false, timer_expired_=false;
    bool bound_=false;
    std::map<std::uint32_t,std::string> entity_tags_;   // entity -> tag id
    std::set<std::pair<std::uint32_t,std::uint32_t>> entered_; // entity, cell key
    std::set<std::uint32_t> seen_;                      // entities seen so far
    std::map<std::uint32_t,std::map<std::string,int>> built_; // house -> type -> n
    std::vector<Team> teams_;
    std::uint32_t next_team_=1;
    std::int32_t last_frame_=-1;
    std::vector<std::tuple<std::int32_t,std::uint32_t,std::uint32_t,ra2map::Cell>> raised_;
};

} // namespace ra2::sim::triggers
