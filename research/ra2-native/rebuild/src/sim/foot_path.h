#pragma once
#include "distance.h"
#include "movement_zones.h"
#include "../map/map_file.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace ra2::sim {
struct FootPathState {
    std::array<std::optional<std::int32_t>,24> words; // Base +4D0 / expansion +5E0.
    std::optional<std::uint32_t> field_640; // Base +530.
    std::optional<std::int32_t> field_644,field_648; // Base +534/+538. Keep unknown semantics explicit.
    std::optional<ra2map::Cell> cell_558; // Base +450.
};
struct FootRetargetType {
    std::optional<DriveEdition> edition;
    std::optional<int> movement_zone,speed_type; // Base +500/+5A4; expansion +5B4/+67C.
    std::optional<bool> field_d2c; // Base +AF5. Only the known false branch is recovered here.
};
struct FootPathInputs {
    ra2map::Cell goal;
    std::optional<unsigned> append_offset,mark_mode;
    std::optional<bool> is_train;
    std::optional<bool> field_5d4_is_null; // Base +4C4; nonnull distance helper remains separate.
    std::optional<bool> field_6c8_is_null,field_6d0; // Base +5B0/+5B8; linked-Unit propagation.
    std::optional<std::uint32_t> frame;
    std::optional<std::int32_t> close_enough; // Rules base +1380 / expansion +1718; original internal distance units.
    std::optional<double> path_delay; // Rules PathDelay, base +13C8 / expansion +1760, in minutes.
    std::optional<ra2map::Cell> invalid_cell; // Resolved global Cell sentinel, base 00865F00 / expansion 008B3D88.
    std::optional<FootRetargetType> retarget;
};
struct FootNearestQuery {
    ra2map::Cell goal,source;
    int speed_type=0;
    unsigned movement_zone=0;
    std::uint32_t source_component=0;
    bool above_bridge=false;
    // Other original nearest-cell arguments: 1,1,0,1, false,1, source,0.
    DriveEdition edition=DriveEdition::BaseRa2; // Expansion also supplies one final zero argument.
};
struct FootEstimateQuery {
    ra2map::Cell candidate,goal;
    bool candidate_bridge=false,goal_bridge=false;
    int movement_zone_override=-1; // Known false Type +AF5/+D2C branch.
};
struct FootGoalContents {
    std::optional<bool> enabled; // Base 00A40958 / expansion 00A8E9A0 enables Cell Building traversal.
    std::optional<std::vector<std::optional<int>>> ordered_kinds; // Full original ground list, virtual +2C results.
};
struct FootFailureHouse {
    std::optional<std::uint8_t> field_1ec,field_1ed; // Base +134/+135. Keep raw byte meaning explicit.
};
struct FootPathQuery {
    ra2map::Cell goal;
    unsigned maximum=2000,append_offset=0,mark_mode=0;
};
struct ResolvedFootPath {
    bool present=false; // Known null is distinct from an unknown callback result.
    std::optional<std::int32_t> cost_key;
    std::optional<std::vector<std::int32_t>> words; // Original count, including any terminator.
};
struct FootPathCallbacks {
    // Supported mutations: private actor position, owner bytes, global mode, destination,
    // secondary slot, and mission records. Callbacks cannot read or change this kernel's
    // working queue, timer fields, or last-cell field. No working-state view is supplied.
    // A body that needs those fields must remain unresolved before live binding.
    std::function<std::optional<bool>(Coord)> can_queue;
    std::function<std::optional<Coord>()> position;
    std::function<std::optional<int>(ra2map::Cell)> enter_goal; // Direction -1, layer -1, source null, flag one.
    std::function<std::optional<bool>(bool)> mark; // Result is ignored; empty means unresolved body.
    std::function<std::optional<ResolvedFootPath>(const FootPathQuery&)> find;
    std::function<std::optional<ra2map::Cell>()> current_cell;
    std::function<std::optional<bool>()> stop_locomotor; // Null path: Foot +48C/+500 calls locomotor virtual +48.
    std::function<std::optional<bool>()> above_bridge; // Actor base +79 / expansion +8C at the requested phase.
    std::function<std::optional<bool>(ra2map::Cell)> goal_bridge; // Explicit Cell +140 bit 100 at the requested phase.
    std::function<std::optional<FootGoalContents>(ra2map::Cell)> goal_contents; // Read-only, original ground-link order.
    std::function<std::optional<bool>(std::optional<ra2map::Cell>,bool)> set_destination; // Base +410 / expansion +480.
    std::function<std::optional<bool>()> failure_slot_3c8; // Base +370 / expansion +3C8, argument zero.
    std::function<std::optional<FootFailureHouse>()> failure_house; // Read the current owner after both preceding virtual calls.
    std::function<std::optional<bool>()> failure_mode; // Base 00A3D298 / expansion 00A8B238 is nonzero; read again after mission.
    std::function<std::optional<bool>(unsigned,bool)> queue_mission; // Base +1D0 / expansion +1E8; raw original mission numbers.
    std::function<std::optional<ra2map::Cell>()> failure_goal; // Resolved base 004EB270 / expansion 00500200.
    std::function<std::optional<std::uint32_t>(ra2map::Cell,unsigned,bool)> retarget_component; // Base 0054F8F0 / expansion 0056D230.
    std::function<std::optional<ra2map::Cell>(const FootNearestQuery&)> nearest_cell; // Base 005502E0 / expansion 0056DC20.
    std::function<std::optional<int>(const FootEstimateQuery&)> estimate_path; // Base 0042AA30 / expansion 0042D170.
};
enum class FootPathStep {
    CanQueue,Position,EnterGoal,MarkOut,Find,MarkIn,CurrentCell,StopLocomotor,FailurePosition,FailureBridgeLayer,GoalBridge,
    GoalContents,FailureDestination,FailureSlot3c8,FailureHouse,FailureMode,FailureMission,FailureGoal,
    RetargetBridgeLayer,RetargetComponent,NearestCell,RetargetDistance,EstimatePath,RetargetDestination
};
enum class FootPathStatus { Complete, NeedsInput, Unsupported };
struct FootPathResult {
    FootPathStatus status=FootPathStatus::NeedsInput;
    bool accepted=false; // Original return, only meaningful for Complete.
    FootPathState state;
    std::optional<int> initial_distance;
    std::vector<FootPathStep> steps;
    std::string detail;
};
// Base 004C3000 / expansion 004D3920, ordinary Unit class, no linked-Unit
// propagation. Retarget helper and failure virtual bodies remain explicit callbacks. The Unit metadata
// virtual is a no-op: base 0041B540 / expansion 0041C140.
// Called bodies remain explicit. Marks must operate on private staged state.
// Only Complete supplies publishable queue state. No live Move command is enabled.
FootPathResult original_unit_basic_path(const FootPathState&,const FootPathInputs&,
    const OriginalDistance&,const FootPathCallbacks&);
}
