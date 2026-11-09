#include "component_lookup.h"
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <unordered_set>
namespace ra2::sim {
namespace {
struct Missing { const char* text; };
struct Unsupported { const char* text; };
template<class T> T need(const std::optional<T>& value,const char* text) {
    if (!value) throw Missing{text};
    return *value;
}
int signed_word(std::uint16_t word) { return word<0x8000?word:int(word)-0x10000; }
}
TransitionLookupResult original_find_component_transition(ra2map::Cell point,int tolerance,int start,
    const std::vector<ComponentTransition>& records) {
    TransitionLookupResult out;
    try {
        if (start<0 || records.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw Unsupported{"component transition start or count is outside the recovered array domain"};
        for (std::size_t i=static_cast<unsigned>(start);i<records.size();++i) {
            const auto& record=records[i];
            if (need(record.field_c,"component transition field +C is unresolved")) continue;
            const auto first=need(record.first,"component transition first endpoint is unresolved");
            const auto second=need(record.second,"component transition second endpoint is unresolved");
            const auto x=signed_word(point.x),y=signed_word(point.y);
            const auto x0=signed_word(first.x),y0=signed_word(first.y),x1=signed_word(second.x),y1=signed_word(second.y);
            const bool matched=x0==x1?(y0<=y && y<=y1 && std::abs(x-x0)<=tolerance):
                (x<=x1 && x0<=x && std::abs(y-y0)<=tolerance);
            if (matched) { out.status=ComponentLookupStatus::Complete;out.index=static_cast<int>(i);return out; }
        }
        out.status=ComponentLookupStatus::Complete;out.index=-1;
    } catch(const Missing& error) { out.detail=error.text; }
    catch(const Unsupported& error) { out.status=ComponentLookupStatus::Unsupported;out.detail=error.text; }
    return out;
}
bool original_component_tile_range(DriveEdition edition,std::int32_t start,std::int32_t tile) {
    if (edition!=DriveEdition::BaseRa2 && edition!=DriveEdition::YurisRevenge)
        throw std::invalid_argument("component tile-range edition is invalid");
    if (edition==DriveEdition::YurisRevenge && start==-1) return false;
    const auto upper=std::int64_t(start)+16;
    const auto signed_upper=upper>std::numeric_limits<std::int32_t>::max()?upper-0x100000000ll:upper;
    return start<=tile && tile<signed_upper;
}
MovementComponentResult original_movement_component(const CoarseRegions& graph,const MovementRegions& movement,unsigned stride,
    const MovementComponentQuery& query,const ComponentLookupInputs& input,const ComponentCellLookup& lookup) {
    MovementComponentResult out;
    try {
        auto selected=query.cell;
        const auto cell=[&](ra2map::Cell point,ComponentLookupStep step) {
            out.steps.push_back(step);
            if (!lookup) throw Missing{"movement component Cell lookup is missing"};
            return need(lookup(point),"movement component Cell metadata is unresolved");
        };
        if (query.above_bridge) {
            const auto source=cell(query.cell,ComponentLookupStep::SourceCell);
            if (need(source.bridge,"movement component source bridge bit is unresolved")) {
                out.steps.push_back(ComponentLookupStep::Transition);
                const auto& records=need(input.transitions,"movement component transition records are unresolved");
                const auto found=original_find_component_transition(query.cell,1,0,records);
                if (found.status!=ComponentLookupStatus::Complete) { out.status=found.status;out.detail=found.detail;return out; }
                out.transition_index=*found.index;
                if (*found.index==-1) { out.status=ComponentLookupStatus::Complete;out.component=0xffffffffu;return out; }
                const auto& record=records.at(static_cast<unsigned>(*found.index));
                selected=need(record.first,"movement component first endpoint is unresolved");
                if (need(record.field_8,"movement component transition field +8 is unresolved")==0) {
                    auto current=cell(query.cell,ComponentLookupStep::WalkCell);
                    const auto second=need(record.second,"movement component second endpoint is unresolved");
                    const unsigned direction=selected.x==second.x?4:2;
                    std::unordered_set<std::uint32_t> visited;
                    while (need(current.bridge,"movement component walk bridge bit is unresolved")) {
                        const auto actual=need(current.object_cell,"movement component walk Cell coordinate is unresolved");
                        const auto key=actual.x|(std::uint32_t(actual.y)<<16);
                        if (!visited.insert(key).second) throw Unsupported{"movement component bridge walk repeats a Cell"};
                        if (out.walk_steps==query.maximum_walk_steps) throw Unsupported{"movement component bridge walk exceeds the caller's recovery limit"};
                        out.steps.push_back(ComponentLookupStep::Neighbor);
                        const ra2map::Cell next{static_cast<std::uint16_t>(actual.x+(direction==2)),static_cast<std::uint16_t>(actual.y+(direction==4))};
                        ++out.walk_steps;current=cell(next,ComponentLookupStep::WalkCell);
                    }
                    const auto range=[&](std::optional<std::int32_t> start,ComponentLookupStep step) {
                        out.steps.push_back(step);
                        const auto first=need(start,"movement component tile-range start is unresolved");
                        if (movement.edition==DriveEdition::YurisRevenge && first==-1) return false;
                        return original_component_tile_range(movement.edition,first,need(current.tile_index,"movement component final tile index is unresolved"));
                    };
                    if ((range(input.first_tile_range,ComponentLookupStep::FirstTileRange) || range(input.second_tile_range,ComponentLookupStep::SecondTileRange)) &&
                        need(current.land_type,"movement component final LandType is unresolved")!=3) selected=second;
                }
            }
        }
        out.selected_cell=selected;out.steps.push_back(ComponentLookupStep::Ground);
        out.component=original_ground_component(graph,movement,stride,query.movement_zone,
            static_cast<std::int16_t>(signed_word(selected.x)),static_cast<std::int16_t>(signed_word(selected.y)));
        out.status=ComponentLookupStatus::Complete;
    } catch(const Missing& error) { out.detail=error.text; }
    catch(const Unsupported& error) { out.status=ComponentLookupStatus::Unsupported;out.detail=error.text; }
    catch(const std::exception& error) { out.status=ComponentLookupStatus::Unsupported;out.detail=error.what(); }
    return out;
}
}
