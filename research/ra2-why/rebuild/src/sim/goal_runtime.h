#pragma once
#include "component_lookup.h"
#include "foot_path.h"
#include "goal_path.h"
#include "hierarchy_route.h"
#include "hierarchy_retry.h"
#include "search_memory.h"
#include "unit_mark.h"
#include "../session/world/world.h"
#include <functional>
#include <map>
#include <memory>

namespace ra2::sim {
struct GoalRuntimeType {
    std::optional<unsigned> movement_zone;
    std::optional<bool> is_train;
    std::optional<double> threat_weight;
    std::optional<int> speed;
};
// All values are original runtime values, supplied by the completed setup.
// A false allocation bit is a known null map-array entry, not an omitted Cell.
struct GoalRuntimeSetup {
    std::vector<bool> allocated; // 512*512 direct Cell pointer array.
    std::vector<HierarchyCell> hierarchy_cells; // Original width+height+1 square, including fallback cells.
    ComponentLookupInputs components;
    std::map<std::uint32_t,GoalRuntimeType> types;
    std::map<std::uint32_t,FootPathState> foot;
    std::map<std::uint32_t,GroundMotion> motion;
    std::map<std::uint32_t,OriginalSearchMemory> search;
    std::optional<std::uint32_t> frame;
};
struct GoalRuntimeActor {
    FootPathInputs foot;
    std::optional<unsigned> movement_zone;
    std::optional<bool> special_guard;
    std::optional<double> threat_weight;
    std::optional<int> speed;
    std::optional<GroundGuards> destination_guards;
};
enum class GoalRuntimeStatus { Complete, NeedsInput, Unsupported };
struct GoalRuntimeResult {
    GoalRuntimeStatus status=GoalRuntimeStatus::NeedsInput;
    std::string detail;
    std::vector<unsigned> directions;
};
// Every callback runs on the private candidate snapshot. It must execute the
// original body or return no value. Mutating callbacks include all original
// occupancy, Cell setup, visibility, and RNG effects, not just a head update.
// None may retain references or modify another copy of the world.
struct GoalRuntimeCallbacks {
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t,Coord)> can_queue;
    // The 0/7 result must hold for every direction and layer in this flat,
    // empty-cell slice. Occupied or direction-dependent entries stay unresolved.
    std::function<std::optional<int>(const session::world::Snapshot&,std::uint32_t,ra2map::Cell)> enter;
    UnitMarkCallbacks mark;
    // Original local flood and its Cell callbacks. Empty means unresolved.
    std::function<std::optional<RetryReachability>(const session::world::Snapshot&,std::uint32_t,ra2map::Cell,unsigned)> reachability;
    // Original arrival and idle-mission side effects. Required at completion.
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t)> finish;

};
// No command is connected to Simulation or the UI. The runtime retains only
// movement state. Each call stages the caller-owned Snapshot and movement state,
// then publishes both together on Complete. Do not run concurrent transactions.
class GoalRuntime {
public:
    static std::pair<std::unique_ptr<GoalRuntime>,GoalRuntimeResult> import(
        const session::world::Snapshot&,const GoalRuntimeSetup&,const std::string& original_executable);
    GoalRuntimeResult begin(session::world::Snapshot&,std::uint32_t actor,ra2map::Cell goal,const GoalRuntimeActor&,const GoalRuntimeCallbacks&);
    GoalRuntimeResult tick(session::world::Snapshot&,std::uint32_t actor,const GoalRuntimeCallbacks&);
    const GoalRuntimeSetup& state() const noexcept { return state_; }
    bool moving(std::uint32_t actor) const { return active_.count(actor)!=0; }
private:
    GoalRuntime(GoalRuntimeSetup,OriginalDistance,DriveTables,Hierarchy,MovementRegions,unsigned);
    GoalRuntimeSetup state_;
    OriginalDistance distance_;
    DriveTables tables_;
    Hierarchy hierarchy_;
    MovementRegions movement_;
    unsigned stride_;
    struct ActiveDrive {
        DriveSegment segment;
        std::int32_t health=0,bridge_height=0;
        bool tracking=false;
        std::uint8_t mark=0,refresh=0,foot_5a1=0;
        std::vector<std::pair<std::uint32_t,session::world::CellState>> watched_cells;
    };
    std::map<std::uint32_t,ActiveDrive> active_;
};
}
