#pragma once
#include "path.h"
#include <functional>
namespace ra2::sim {
struct GoalPathDecisions {
    std::optional<std::uint16_t> source_component,goal_component;
    std::optional<bool> is_train,on_map,special_guard,source_usable,goal_usable;
    std::optional<unsigned> search_mark_mode; // Supported zero mode; marked-search setup remains separate.
    int expansion_limit=-1;
};
struct GoalPathCallbacks {
    std::function<void()> reset_search;
    std::function<void(unsigned)> clear_blocked;
    std::function<std::optional<bool>()> find_hierarchy;
    std::function<PathResult(bool,int)> find_regular;
    std::function<std::optional<bool>(bool)> retry; // Receives Search +38 and returns its 0042CCD0 result, with its private blocked-list changes.
};
enum class GoalPathStep { Reset,ClearBlocked0,ClearBlocked1,ClearBlocked2,Hierarchy,Regular,Retry };
struct GoalPathEvent { GoalPathStep step; bool hierarchy=false; int expansion_limit=0; };
struct GoalPathResult {
    PathResult path;
    std::vector<GoalPathEvent> events;
    unsigned regular_attempts=0,hierarchy_attempts=0,retries=0;
    bool last_regular_hierarchy=false,allow_hierarchy_38=true;
};
// Base 0042A1D0 / expansion 0042C900, after resolved map/type queries for an ordinary ground Unit.
// Bridge endpoint conversion and special infantry locomotor setup remain caller work.
// Callbacks operate on private search state. Unknown required answers produce NeedsInput.
GoalPathResult run_original_ground_goal(const GoalPathDecisions&,const GoalPathCallbacks&);
}
