#include "nearest_cell.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Missing { const char* text; };
struct Unsupported { const char* text; };
template<class T> T need(const std::optional<T>& value,const char* text) {
    if (!value) throw Missing{text};
    return *value;
}
bool same(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
int signed_cell(std::uint16_t value) { return value<0x8000?value:int(value)-0x10000; }
Coord center(ra2map::Cell cell) { return {signed_cell(cell.x)*256+128,signed_cell(cell.y)*256+128,0}; }
int level(const NearestCellState& cell) {
    const auto value=need(cell.level,"nearest-cell raw height is unresolved");
    if (value<-128 || value>127) throw Unsupported{"nearest-cell raw height is outside the original signed-byte domain"};
    return value;
}
}
NearestCellResult original_foot_nearest_cell(const NearestCellInputs& input,const OriginalDistance& distance,const NearestCellCallbacks& cb) {
    NearestCellResult out;
    try {
        const auto empty=need(input.empty_cell,"nearest-cell empty sentinel is unresolved").value;
        const auto width=need(input.map_width,"nearest-cell map width is unresolved");
        const auto height=need(input.map_height,"nearest-cell map height is unresolved");
        if (width<0 || width>512 || height<0 || height>512 || input.query.goal.x>511 || input.query.goal.y>511)
            throw Unsupported{"nearest-cell map or goal is outside the recovered grid domain"};
        (void)original_movement_zone_count(input.query.edition);
        const auto cell=[&](ra2map::Cell point) {
            if (!cb.cell) throw Missing{"nearest-cell metadata getter is missing"};
            return need(cb.cell(point),"nearest-cell metadata is unresolved");
        };
        const auto project=[&](ra2map::Cell point) {
            if (!cb.project) throw Missing{"nearest-cell projection body is missing"};
            return need(cb.project(center(point)),"nearest-cell projection is unresolved");
        };
        const auto goal=cell(input.query.goal);
        int reference=level(goal);
        if (input.query.above_bridge && need(goal.bridge,"nearest-cell goal bridge flag is unresolved")) reference+=4;
        const auto component=input.query.source_component==0xffffu?0xffffffffu:input.query.source_component;
        bool stop_after_ring=false;
        const auto probe=[&](int dx,int dy) {
            const ra2map::Cell point{static_cast<std::uint16_t>(int(input.query.goal.x)+dx),static_cast<std::uint16_t>(int(input.query.goal.y)+dy)};
            ++out.probes;
            const auto current=cell(point);
            if (!need(current.usable,"nearest-cell usable-area answer is unresolved")) return false;
            if (!cb.fits) throw Missing{"nearest-cell rectangle eligibility body is missing"};
            if (!need(cb.fits({point,input.query.speed_type,component,input.query.movement_zone,input.query.above_bridge}),"nearest-cell rectangle eligibility is unresolved")) return false;
            const int bridge=need(current.bridge,"nearest-cell candidate bridge flag is unresolved")?4:0;
            if (std::abs(reference-bridge-level(current))>=2) return false;
            out.admitted.push_back(point);
            // Base 005502E0: complete this ring after one exact projection.
            if (input.query.above_bridge || same(project(point),point)) stop_after_ring=true;
            return out.admitted.size()==24;
        };
        bool full=false;
        // Base 005502E0, before selection at 00550AE7. Radius zero visits the goal twice.
        for (int radius=0;radius<std::min(width+height,32);++radius) {
            for (int offset=-radius;offset<=radius;++offset) {
                if (probe(offset,-radius) || probe(offset,radius)) { full=true; break; }
            }
            if (!full) for (int offset=-radius+1;offset<=radius-1;++offset) {
                if (probe(-radius,offset) || probe(radius,offset)) { full=true; break; }
            }
            if (full || stop_after_ring) break;
        }
        if (out.admitted.empty()) { out.answer=empty; out.status=NearestCellStatus::Complete; return out; }
        for (const auto point:out.admitted) {
            if (same(project(point),point)) out.exact_projection.push_back(point);
            else out.other_projection.push_back(point);
        }
        const auto& choices=out.exact_projection.empty()?out.other_projection:out.exact_projection;
        if (same(input.query.source,empty)) {
            const auto frame=need(input.frame,"nearest-cell frame is unresolved for empty-source selection");
            if (frame>0x7fffffffu) throw Unsupported{"nearest-cell frame is outside the verified nonnegative domain"};
            out.answer=choices[frame%choices.size()];
        } else {
            double best=100000.0; out.answer=empty;
            for (const auto point:choices) {
                const auto dx=std::int64_t(signed_cell(point.x))-signed_cell(input.query.source.x);
                const auto dy=std::int64_t(signed_cell(point.y))-signed_cell(input.query.source.y);
                const auto squared=dx*dx+dy*dy;
                if (squared>std::numeric_limits<std::int32_t>::max())
                    throw Unsupported{"nearest-cell distance exceeds the recovered signed integer domain"};
                const auto length=distance.square_root(static_cast<double>(squared));
                if (length<best) { best=length; out.answer=point; }
            }
        }
        out.status=NearestCellStatus::Complete;
    } catch(const Missing& missing) { out.detail=missing.text; out.answer.reset(); }
    catch(const Unsupported& unsupported) { out.status=NearestCellStatus::Unsupported; out.detail=unsupported.text; out.answer.reset(); }
    catch(const std::exception& error) { out.status=NearestCellStatus::Unsupported; out.detail=error.what(); out.answer.reset(); }
    return out;
}
}
