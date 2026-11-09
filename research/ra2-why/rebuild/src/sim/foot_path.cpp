#include "foot_path.h"
#include "fp.h"
#include <algorithm>
#include <limits>
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
int signed_cell(std::int32_t value) {
    const auto low=static_cast<std::uint32_t>(value/256)&0xffffu;
    return low<0x8000u?static_cast<int>(low):static_cast<int>(low)-0x10000;
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
        // Resolve the unlinked CloseEnough threshold only when entry status 6 needs it.
        if (require(input.is_train,"Foot UnitType IsTrain is unresolved"))
            throw Unsupported{"Foot train path handling is not recovered"};
        out.steps.push_back(FootPathStep::EnterGoal);
        if (!cb.enter_goal) throw Missing{"Foot goal-entry body is missing"};
        const auto entry=require(cb.enter_goal(input.goal),"Foot goal-entry result is unresolved");
        if (entry==6 && *out.initial_distance>require(input.close_enough,"Foot CloseEnough is unresolved"))
            throw Unsupported{"Foot goal retarget decisions are not recovered"};
        if (entry==7) {
            out.steps.push_back(FootPathStep::GoalContents);
            if (!cb.goal_contents) throw Missing{"Foot goal Building lookup inputs are missing"};
            const auto contents=require(cb.goal_contents(input.goal),"Foot goal Building lookup inputs are unresolved");
            // Base 004717B0 / expansion 0047C520 scans the original ground list only when enabled.
            if (require(contents.enabled,"Foot goal Building lookup flag is unresolved")) {
                const auto kinds=require(contents.ordered_kinds,"Foot goal ground occupant order is unresolved");
                for (const auto& kind:kinds)
                    if (require(kind,"Foot goal occupant runtime type is unresolved")==6)
                        throw Unsupported{"Foot Building-goal retarget decisions are not recovered"};
            }
        }
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
        if (path.present && require(path.cost_key,"Foot path result cost key is unresolved")!=0) {
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
        if (path.present) {
            out.steps.push_back(FootPathStep::CurrentCell);
            if (!cb.current_cell) throw Missing{"Foot current-cell getter is missing"};
            state.cell_558=require(cb.current_cell(),"Foot current cell is unresolved");
            out.accepted=true;
        } else {
            // Base 004C36EC / expansion 004D4016 reads Rules PathDelay and multiplies by 900.
            const auto ticks=fp::multiply(require(input.path_delay,"Foot PathDelay is unresolved"),900.0);
            if (ticks<std::numeric_limits<std::int32_t>::min() || ticks>std::numeric_limits<std::int32_t>::max())
                throw Unsupported{"Foot PathDelay conversion exceeds the recovered integer domain"};
            // The original retains the initial Y delta. Popping EDI shifts the stack offsets by four.
            state.field_648=static_cast<std::int32_t>(ticks);
            out.steps.push_back(FootPathStep::StopLocomotor);
            if (!cb.stop_locomotor) throw Missing{"Foot locomotor stop body is missing"};
            (void)require(cb.stop_locomotor(),"Foot locomotor stop body is unresolved");
            // The original reads actor coordinates after this virtual, not the earlier getter result.
            out.steps.push_back(FootPathStep::FailurePosition);
            const auto after_stop=require(cb.position(),"Foot position after locomotor stop is unresolved");
            bool dispatch=std::max(std::abs(signed_cell(after_stop.x)-input.goal.x),std::abs(signed_cell(after_stop.y)-input.goal.y))>1;
            if (!dispatch) {
                out.steps.push_back(FootPathStep::FailureBridgeLayer);
                if (!cb.above_bridge) throw Missing{"Foot actor bridge layer getter is missing"};
                if (!require(cb.above_bridge(),"Foot actor bridge layer is unresolved")) {
                    out.steps.push_back(FootPathStep::GoalBridge);
                    if (!cb.goal_bridge) throw Missing{"Foot goal bridge flag getter is missing"};
                    dispatch=require(cb.goal_bridge(input.goal),"Foot goal bridge flag is unresolved");
                }
            }
            if (dispatch) {
                // Base 004C380D / expansion 004D413A, after the linked-object branch is excluded.
                const auto destination=[&](std::optional<ra2map::Cell> cell) {
                    out.steps.push_back(FootPathStep::FailureDestination);
                    if (!cb.set_destination) throw Missing{"Foot failure destination body is missing"};
                    (void)require(cb.set_destination(cell,true),"Foot failure destination body is unresolved");
                };
                destination({});
                out.steps.push_back(FootPathStep::FailureSlot3c8);
                if (!cb.failure_slot_3c8) throw Missing{"Foot failure virtual +3C8/+370 body is missing"};
                (void)require(cb.failure_slot_3c8(),"Foot failure virtual +3C8/+370 is unresolved");
                out.steps.push_back(FootPathStep::FailureHouse);
                if (!cb.failure_house) throw Missing{"Foot failure House getter is missing"};
                const auto house=require(cb.failure_house(),"Foot failure House state is unresolved");
                const auto mode=[&]() {
                    out.steps.push_back(FootPathStep::FailureMode);
                    if (!cb.failure_mode) throw Missing{"Foot failure global mode getter is missing"};
                    return require(cb.failure_mode(),"Foot failure global mode is unresolved");
                };
                // Real owner predicate: base 004F52F0 / expansion 0050B730.
                bool owner_flag=require(house.field_1ec,"Foot owner byte +1EC/+134 is unresolved")!=0;
                if (!owner_flag && !mode()) owner_flag=require(house.field_1ed,"Foot owner byte +1ED/+135 is unresolved")!=0;
                out.steps.push_back(FootPathStep::FailureMission);
                if (!cb.queue_mission) throw Missing{"Foot failure mission body is missing"};
                (void)require(cb.queue_mission(owner_flag?5:11,false),"Foot failure mission body is unresolved");
                if (!owner_flag && mode()) {
                    out.steps.push_back(FootPathStep::FailureGoal);
                    if (!cb.failure_goal) throw Missing{"Foot failure goal helper is missing"};
                    const auto cell=require(cb.failure_goal(),"Foot failure goal is unresolved");
                    const auto invalid=require(input.invalid_cell,"Foot invalid Cell sentinel is unresolved");
                    if (cell.x!=invalid.x || cell.y!=invalid.y) destination(cell);
                }
            }
        }
        out.state=std::move(state); out.status=FootPathStatus::Complete;
    } catch(const Missing& missing) { out.detail=missing.text; }
    catch(const Unsupported& unsupported) { out.status=FootPathStatus::Unsupported; out.detail=unsupported.text; }
    catch(const std::exception& error) { out.status=FootPathStatus::Unsupported; out.detail=error.what(); }
    return out;
}
}
