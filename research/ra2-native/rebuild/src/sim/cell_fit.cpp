#include "cell_fit.h"
#include <cmath>
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Missing { const char* text; };
struct Unsupported { const char* text; };
template<class T> T need(const std::optional<T>& value,const char* text) {
    if (!value) throw Missing{text};
    return *value;
}
int level(const CellFitState& cell) {
    const auto height=need(cell.level,"Cell fit raw height is unresolved");
    if (height<-128 || height>127) throw Unsupported{"Cell fit raw height is outside the signed-byte domain"};
    return height;
}
}
CellFitResult original_cell_fit(const ResolvedCellFitQuery& query,const CellFitState& cell,const CellFitCallbacks& cb) {
    CellFitResult out;
    const auto finish=[&](bool allowed) { out.status=CellFitStatus::Complete;out.allowed=allowed;return out; };
    try {
        // 00478760 / 004834A0: speed type four bypasses all Cell data.
        if (query.speed_type==4) return finish(true);
        if (query.speed_type<0 || query.speed_type>7) throw Unsupported{"Cell fit speed type is outside the original eight-entry table"};
        if (query.edition!=DriveEdition::BaseRa2 && query.edition!=DriveEdition::YurisRevenge)
            throw Unsupported{"Cell fit edition is invalid"};
        if (query.component!=0xffffffffu) {
            if (!cb.component) throw Missing{"Cell fit component lookup is missing"};
            const auto point=need(cell.object_cell,"Cell fit object coordinate is unresolved");
            const auto component=need(cb.component(point,query.movement_zone,query.above_bridge),"Cell fit component is unresolved");
            if (component!=query.component) return finish(false);
        }
        const auto bridge=need(cell.bridge,"Cell fit bridge bit 100 is unresolved");
        int height=0;
        if (query.level!=-1) {
            height=level(cell);
            if (query.level==height) {
                if (bridge && !query.above_bridge) return finish(false);
            } else if (!bridge || query.level!=height+4) return finish(false);
        }
        const bool bridge_layer=bridge && (query.level==-1 || query.level==height+4);
        auto occupancy=need(bridge_layer?cell.bridge_occupancy:cell.ground_occupancy,"Cell fit selected occupancy byte is unresolved");
        if (query.ignore_low_five) occupancy&=0xe0;
        if (query.ignore_a0) occupancy&=0x5f;
        if (occupancy) return finish(false);
        const auto overlay=need(cell.overlay_index,"Cell fit runtime overlay index is unresolved");
        if (overlay<-1) throw Unsupported{"Cell fit overlay index is invalid"};
        bool clear_land=false;
        if (overlay!=-1) {
            const auto object=need(cell.overlay,"Cell fit overlay registry state is unresolved");
            if (need(object.present,"Cell fit overlay pointer presence is unresolved") && need(object.flag_2a8,"Cell fit overlay flag +2A8/+278 is unresolved")) {
                bool allowed=query.movement_zone==2 || query.movement_zone==3 || query.movement_zone==8;
                if (query.movement_zone==1 || query.movement_zone==4) allowed=need(object.flag_22d,"Cell fit overlay flag +22D/+1FD is unresolved");
                if (query.edition==DriveEdition::YurisRevenge && query.movement_zone==12) allowed=true;
                if (!allowed) return finish(false);
                clear_land=true;
            }
        }
        const auto land=clear_land?0:need(cell.land_type,"Cell fit final LandType is unresolved");
        if (land<0) throw Unsupported{"Cell fit final LandType is invalid"};
        if (!cb.land_speed) throw Missing{"Cell fit selected land-speed lookup is missing"};
        const auto speed=need(cb.land_speed(land,query.speed_type),"Cell fit selected land-speed value is unresolved");
        // FCOMP's equality bit is also set for unordered values. Preserve the bridge-layer override.
        return finish(bridge_layer || (speed!=0 && !std::isnan(speed)));
    } catch(const Missing& missing) { out.detail=missing.text; }
    catch(const Unsupported& unsupported) { out.status=CellFitStatus::Unsupported;out.detail=unsupported.text; }
    catch(const std::exception& error) { out.status=CellFitStatus::Unsupported;out.detail=error.what(); }
    return out;
}
RectangleFitResult original_rectangle_fit(const RectangleFitQuery& query,const CellFitLookup& lookup,const CellFitCallbacks& cb) {
    RectangleFitResult out;
    try {
        if (query.width<=0 || query.height<=0) { out.status=CellFitStatus::Complete;out.allowed=true;return out; }
        if (query.width>512 || query.height>512) throw Unsupported{"rectangle fit exceeds the recovered grid-size domain"};
        for (int x=0;x<query.width;++x) for (int y=0;y<query.height;++y) {
            const ra2map::Cell point{static_cast<std::uint16_t>(int(query.origin.x)+x),static_cast<std::uint16_t>(int(query.origin.y)+y)};
            ++out.tested;
            if (!lookup) throw Missing{"rectangle fit Cell lookup is missing"};
            const auto cell=need(lookup(point),"rectangle fit Cell metadata is unresolved");
            if (query.reject_overlay && need(cell.overlay_index,"rectangle fit overlay index is unresolved")!=-1) {
                out.status=CellFitStatus::Complete;out.allowed=false;out.blocked_cell=point;return out;
            }
            const auto result=original_cell_fit({query.edition,query.speed_type,false,false,query.component,query.movement_zone,query.level,query.above_bridge},cell,cb);
            if (result.status!=CellFitStatus::Complete) { out.status=result.status;out.detail=result.detail;return out; }
            if (!*result.allowed) { out.status=CellFitStatus::Complete;out.allowed=false;out.blocked_cell=point;return out; }
        }
        out.status=CellFitStatus::Complete;out.allowed=true;
    } catch(const Missing& missing) { out.detail=missing.text; }
    catch(const Unsupported& unsupported) { out.status=CellFitStatus::Unsupported;out.detail=unsupported.text; }
    catch(const std::exception& error) { out.status=CellFitStatus::Unsupported;out.detail=error.what(); }
    return out;
}
}
