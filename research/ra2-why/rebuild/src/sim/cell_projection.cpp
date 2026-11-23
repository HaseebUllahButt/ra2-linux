#include "cell_projection.h"
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Missing { const char* text; };
struct Unsupported { const char* text; };
template<class T> T need(const std::optional<T>& value,const char* text) {
    if (!value) throw Missing{text};
    return *value;
}
int signed_cell(std::uint16_t value) { return value<0x8000?value:int(value)-0x10000; }
ra2map::Cell from_world(int x,int y) {
    // C++ signed division preserves the original (value + (sign & FF)) >> 8.
    return {static_cast<std::uint16_t>(x/256),static_cast<std::uint16_t>(y/256)};
}
int level(const ProjectionCellState& state) {
    const auto height=need(state.level,"cell projection raw height is unresolved");
    if (height<-128 || height>127) throw Unsupported{"cell projection raw height is outside the original signed-byte domain"};
    return height;
}
}
CellProjectionResult original_cell_projection(Coord point,const ProjectionLookup& lookup) {
    CellProjectionResult out; out.source_cell=from_world(point.x,point.y);
    try {
        const auto cell=[&](ra2map::Cell value) {
            ++out.lookups;
            if (!lookup) throw Missing{"cell projection lookup is missing"};
            return need(lookup(value),"cell projection metadata is unresolved");
        };
        const auto source=out.source_cell;
        // The original performs separate source lookups for the flag word and the height byte.
        const auto adjust_bridge=need(cell(source).flag_1000,"cell projection source flag 1000 is unresolved");
        const auto source_level=level(cell(source));
        int x=signed_cell(source.x)*256+0x680,y=signed_cell(source.y)*256+0x680;
        // The original loop must encounter its source within 0x680 / 8 iterations.
        for (unsigned step=1;step<=0x680/8;++step) {
            x-=8;y-=8;out.steps=step;
            const auto trial=from_world(x,y);
            const auto state=cell(trial);
            int delta=level(state)-source_level;
            if (adjust_bridge && need(state.bridge,"cell projection trial bridge flag 100 is unresolved")) delta+=4;
            const auto adjusted=from_world(x-delta*0x80,y-delta*0x80);
            if ((signed_cell(adjusted.x)<=signed_cell(source.x) && signed_cell(adjusted.y)<=signed_cell(source.y)) ||
                (trial.x==source.x && trial.y==source.y)) {
                out.answer=trial;out.status=CellProjectionStatus::Complete;return out;
            }
        }
        throw Unsupported{"cell projection did not reach its source within the original coordinate steps"};
    } catch(const Missing& missing) { out.detail=missing.text; }
    catch(const Unsupported& unsupported) { out.status=CellProjectionStatus::Unsupported;out.detail=unsupported.text; }
    catch(const std::exception& error) { out.status=CellProjectionStatus::Unsupported;out.detail=error.what(); }
    return out;
}
}
