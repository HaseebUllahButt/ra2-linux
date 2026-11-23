#include "goal_path.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned seed; std::uint32_t hash; unsigned regular,hierarchy,retries; };
const Golden goldens[]={
#include "goal_control_golden.inc"
};
struct Inputs { GoalPathDecisions decisions; std::array<bool,6> hierarchy,regular,retry; };
Inputs inputs(unsigned seed) {
    std::uint32_t value=seed^0x9264ba17u;
    const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
    auto flags=next(); const int limits[]={-2,-1,0,1,500}; int limit=limits[next()%5];
    Inputs result;
    for (auto& answer:result.hierarchy) answer=next()%2;
    for (auto& answer:result.regular) answer=next()%2;
    for (auto& answer:result.retry) answer=next()%2;
    if (seed<10) {
        flags=0x1a; limit=seed%2==0?-1:10;
        result.hierarchy.fill(true); result.regular.fill(false); result.retry.fill(true);
        if (seed==2 || seed==3) result.hierarchy[0]=false;
        if (seed==4 || seed==5) result.retry[0]=false;
        if (seed==6 || seed==7) result.hierarchy[1]=false;
        if (seed==8 || seed==9) result.regular[1]=true;
    }
    auto& decision=result.decisions;
    decision.is_train=flags&1; decision.on_map=flags&2; decision.special_guard=flags&4;
    decision.source_usable=flags&8; decision.goal_usable=flags&16;
    decision.search_mark_mode=0; decision.source_component=17; decision.goal_component=17+static_cast<bool>(flags&32); decision.expansion_limit=limit;
    return result;
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() try {
    unsigned checks=0;
    for (const auto& golden:goldens) {
        const auto input=inputs(golden.seed); unsigned hs=0,regular=0,retries=0,resets=0,clears=0;
        GoalPathCallbacks callbacks;
        callbacks.reset_search=[&] { ++resets; };
        callbacks.clear_blocked=[&](unsigned level) { require(level==clears,"blocked lists cleared in wrong order"); ++clears; };
        callbacks.find_hierarchy=[&]()->std::optional<bool> { return input.hierarchy[std::min(hs++,5u)]; };
        callbacks.find_regular=[&](bool,int limit) { require(limit==input.decisions.expansion_limit,"expansion limit changed"); PathResult result; result.status=input.regular[std::min(regular++,5u)]?PathStatus::Found:PathStatus::NoPath; return result; };
        callbacks.retry=[&](bool allowed)->std::optional<bool> { require(allowed,"retry sees an incorrect hierarchy permission"); return input.retry[std::min(retries++,5u)]; };
        const auto result=run_original_ground_goal(input.decisions,callbacks);
        std::uint32_t h=2166136261u;
        const auto add=[&](std::uint32_t value) { h=(h^value)*16777619u; };
        add(result.path.status==PathStatus::Found || result.path.status==PathStatus::Partial); add(result.allow_hierarchy_38); add(result.events.size());
        for (const auto& event:result.events) { add(static_cast<unsigned>(event.step)); add(event.hierarchy); add(static_cast<std::uint32_t>(event.expansion_limit)); }
        if (h!=golden.hash || regular!=golden.regular || hs!=golden.hierarchy || retries!=golden.retries) {
            std::cerr<<"seed="<<golden.seed<<" hash="<<std::hex<<h<<" expected="<<golden.hash<<"\n";
            throw std::runtime_error("original goal control or callback order differs");
        }
        require(clears==3 && resets==retries+1,"original reset count differs");
        require(result.regular_attempts==regular && result.hierarchy_attempts==hs && result.retries==retries,"goal diagnostics differ"); ++checks;
    }
    auto decision=inputs(0).decisions;
    GoalPathCallbacks callbacks;
    callbacks.reset_search=[]{}; callbacks.clear_blocked=[](unsigned){};
    callbacks.find_hierarchy=[]()->std::optional<bool> { return true; };
    callbacks.find_regular=[](bool,int) { return PathResult{}; };
    const auto needs=[&](const GoalPathDecisions& value,const GoalPathCallbacks& cb) { require(run_original_ground_goal(value,cb).path.status==PathStatus::NeedsInput,"unknown goal dependency accepted"); ++checks; };
    needs(decision,callbacks);
    auto unknown=decision; unknown.search_mark_mode.reset(); needs(unknown,callbacks);
    unknown=decision; unknown.source_component.reset(); needs(unknown,callbacks);
    unknown=decision; unknown.is_train.reset(); needs(unknown,callbacks);
    unknown=decision; unknown.on_map.reset(); needs(unknown,callbacks);
    unknown=decision; unknown.special_guard.reset(); needs(unknown,callbacks);
    unknown=decision; unknown.source_usable.reset(); needs(unknown,callbacks);
    unknown=decision; unknown.goal_usable.reset(); needs(unknown,callbacks);
    auto missing=callbacks; missing.reset_search={}; needs(decision,missing);
    missing=callbacks; missing.clear_blocked={}; needs(decision,missing);
    missing=callbacks; missing.find_hierarchy={}; needs(decision,missing);
    missing=callbacks; missing.find_regular={}; needs(decision,missing);
    callbacks.find_regular=[](bool,int) { PathResult result; result.status=PathStatus::Partial; return result; };
    require(run_original_ground_goal(decision,callbacks).path.status==PathStatus::Partial,"partial original path retried"); ++checks;
    callbacks.find_regular=[](bool,int) { PathResult result; result.status=PathStatus::Unsupported; return result; };
    require(run_original_ground_goal(decision,callbacks).path.status==PathStatus::Unsupported,"unsupported native path silently retried"); ++checks;
    unknown=decision; unknown.search_mark_mode=1;
    require(run_original_ground_goal(unknown,callbacks).path.status==PathStatus::Unsupported,"unrecovered marked-search setup accepted"); ++checks;
    std::cout<<"original ground goal control and explicit dependencies: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
