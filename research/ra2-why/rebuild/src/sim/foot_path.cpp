#include "foot_path.h"
#include <algorithm>
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Missing { const char* text; };
struct Unsupported { const char* text; };
template<class T> T require(const std::optional<T>& value,const char* text) {
    if (!value) throw Missing{text};
    return *value;
}
std::int32_t signed_word(std::uint32_t value) {
    return value<=0x7fffffffu?static_cast<std::int32_t>(value):-1-static_cast<std::int32_t>(0xffffffffu-value);
}
}
FootPathResult original_unit_basic_path(const FootPathState& in,const FootPathInputs& input,
    const OriginalDistance& distance,const FootPathCallbacks& cb) {
    FootPathResult out; out.state=in;
    try {
        auto state=in;
        const auto append=require(input.append_offset,"Foot path append offset is unresolved");
        const auto mode=require(input.mark_mode,"Foot path mark mode is unresolved");
        if (append>24 || input.goal.x>511 || input.goal.y>511)
            throw Unsupported{"Foot path offset or goal is outside the recovered map domain"};
        if (!append) state.words[0]=-1;
        const Coord goal{input.goal.x*256+128,input.goal.y*256+128,0};
        out.steps.push_back(FootPathStep::CanQueue);
        if (!cb.can_queue) throw Missing{"Foot queue guard body is missing"};
        if (!require(cb.can_queue(goal),"Foot queue guard is unresolved")) {
            state.words[0]=-1;
            out.state=std::move(state); out.status=FootPathStatus::Complete; return out;
        }
        out.steps.push_back(FootPathStep::Position);
        if (!cb.position) throw Missing{"Foot position getter is missing"};
        const auto position=require(cb.position(),"Foot position is unresolved");
        out.initial_distance=distance.between(position,goal);
        if (!require(input.field_5d4_is_null,"Foot field +5D4/+4C4 is unresolved"))
            throw Unsupported{"Foot linked-object distance helper is not recovered"};
        // The unlinked CloseEnough read cannot affect this ordinary entry branch.
        if (require(input.is_train,"Foot UnitType IsTrain is unresolved"))
            throw Unsupported{"Foot train path handling is not recovered"};
        out.steps.push_back(FootPathStep::EnterGoal);
        if (!cb.enter_goal) throw Missing{"Foot goal-entry body is missing"};
        const auto entry=require(cb.enter_goal(input.goal),"Foot goal-entry result is unresolved");
        if (entry==6 || entry==7) throw Unsupported{"Foot goal retarget decisions are not recovered"};
        const auto mark=[&](bool inserted) {
            out.steps.push_back(inserted?FootPathStep::MarkIn:FootPathStep::MarkOut);
            if (!cb.mark) throw Missing{"Foot mark body is missing"};
            (void)require(cb.mark(inserted),"Foot mark body is unresolved");
        };
        mark(false);
        if (!append) state.words[0]=-1;
        out.steps.push_back(FootPathStep::Find);
        if (!cb.find) throw Missing{"Foot goal search body is missing"};
        const auto path=require(cb.find({input.goal,2000,append,mode}),"Foot goal search is unresolved");
        if (!path.present) throw Unsupported{"Foot null-path fallback is not recovered"};
        if (require(path.cost_key,"Foot path result cost key is unresolved")!=0) {
            const auto words=require(path.words,"Foot path result words are unresolved");
            if (words.size()>2000) throw Unsupported{"Foot path result exceeds its supplied direction storage"};
            const auto count=std::min<std::size_t>(24-append,words.size());
            for (std::size_t i=0;i<count;++i) state.words[append+i]=words[i];
        }
        mark(true);
        state.field_640=require(input.frame,"Foot path frame is unresolved");
        // Base +534 / expansion +644 receives this signed delta, not the sqrt.
        state.field_644=signed_word(static_cast<std::uint32_t>(position.y)-static_cast<std::uint32_t>(goal.y));
        state.field_648=0;
        if (state.words[0]!=-1 && input.field_6c8_is_null!=true && input.field_6d0!=true) {
            if (!state.words[0] || !input.field_6c8_is_null || !input.field_6d0)
                throw Missing{"Foot linked-Unit propagation state is unresolved"};
            throw Unsupported{"Foot linked-Unit path propagation is not recovered"};
        }
        out.steps.push_back(FootPathStep::CurrentCell);
        if (!cb.current_cell) throw Missing{"Foot current-cell getter is missing"};
        state.cell_558=require(cb.current_cell(),"Foot current cell is unresolved");
        out.state=std::move(state); out.status=FootPathStatus::Complete; out.accepted=true;
    } catch(const Missing& missing) { out.detail=missing.text; }
    catch(const Unsupported& unsupported) { out.status=FootPathStatus::Unsupported; out.detail=unsupported.text; }
    catch(const std::exception& error) { out.status=FootPathStatus::Unsupported; out.detail=error.what(); }
    return out;
}
}
