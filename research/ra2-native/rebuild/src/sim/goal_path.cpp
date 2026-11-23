#include "goal_path.h"
namespace ra2::sim {
GoalPathResult run_original_ground_goal(const GoalPathDecisions& input,const GoalPathCallbacks& callbacks) {
    GoalPathResult result;
    const auto missing=[&](const char* detail) { result.path.status=PathStatus::NeedsInput; result.path.evidence=detail; return result; };
    const auto reset=[&]() {
        result.events.push_back({GoalPathStep::Reset});
        if (!callbacks.reset_search) return false;
        callbacks.reset_search(); return true;
    };
    if (!reset()) return missing("0042A5B0: search reset callback is unresolved");
    for (unsigned level=0;level<3;++level) {
        result.events.push_back({static_cast<GoalPathStep>(static_cast<unsigned>(GoalPathStep::ClearBlocked0)+level)});
        if (!callbacks.clear_blocked) return missing("0042C900: blocked-list clear callback is unresolved");
        callbacks.clear_blocked(level);
    }
    if (!input.search_mark_mode) return missing("0042C900: search mark mode is unresolved");
    if (*input.search_mark_mode!=0) { result.path.status=PathStatus::Unsupported; result.path.evidence="0042ACF0: marked-search setup is not recovered"; return result; }
    if (!input.source_component || !input.goal_component) return missing("0056D230: endpoint component query is unresolved");
    if (!input.is_train) return missing("0042C900: UnitType IsTrain is unresolved");
    bool hierarchy=false;
    if (!*input.is_train) {
        if (!input.on_map) return missing("0042C900: Foot on-map flag is unresolved");
        if (*input.on_map) {
            if (!input.special_guard) return missing("0042C900: Foot special-state guard is unresolved");
            if (!*input.special_guard) {
                if (!input.source_usable) return missing("00578460: hierarchy source usability is unresolved");
                if (*input.source_usable) {
                    if (!input.goal_usable) return missing("00578460: hierarchy goal usability is unresolved");
                    hierarchy=*input.goal_usable;
                }
            }
        }
    }
    const auto find_hierarchy=[&]()->std::optional<bool> {
        result.events.push_back({GoalPathStep::Hierarchy}); ++result.hierarchy_attempts;
        if (!callbacks.find_hierarchy) return {};
        return callbacks.find_hierarchy();
    };
    if (*input.source_component==*input.goal_component) {
        if (hierarchy) {
            const auto found=find_hierarchy();
            if (!found) return missing("0042C290: hierarchy search is unresolved");
            if (!*found) hierarchy=false; // Only the initial hierarchy failure falls back immediately.
        }
    } else if (hierarchy) {
        result.path.evidence="0042C900: different endpoint movement components"; return result;
    }
    const unsigned maximum=input.expansion_limit!=-1?1:5;
    for (;;) {
        result.events.push_back({GoalPathStep::Regular,hierarchy,input.expansion_limit});
        ++result.regular_attempts; result.last_regular_hierarchy=hierarchy;
        if (!callbacks.find_regular) return missing("00429A90: regular search callback is unresolved");
        result.path=callbacks.find_regular(hierarchy,input.expansion_limit);
        if (result.path.status!=PathStatus::NoPath || !hierarchy) return result;
        result.events.push_back({GoalPathStep::Retry}); ++result.retries;
        if (!callbacks.retry) return missing("0042CCD0: hierarchy retry and blocked-list decisions are unresolved");
        const auto allowed=callbacks.retry(result.allow_hierarchy_38);
        if (!allowed) return missing("0042CCD0: hierarchy retry state is unresolved");
        result.allow_hierarchy_38=*allowed;
        if (!reset()) return missing("0042A5B0: retry search reset callback is unresolved");
        hierarchy=*allowed;
        // Original retry and reset run before this attempt-limit test, including the final attempt.
        if (result.retries>=maximum) return result;
        if (hierarchy) {
            const auto found=find_hierarchy();
            if (!found) return missing("0042C290: retry hierarchy search is unresolved");
            if (!*found) return result;
        }
    }
}
}
