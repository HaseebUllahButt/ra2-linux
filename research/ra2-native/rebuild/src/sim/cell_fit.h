#pragma once
#include "drive.h"
#include "../map/map_file.h"
#include <functional>
#include <optional>
#include <string>
namespace ra2::sim {
struct CellFitOverlay {
    std::optional<bool> present; // Known null registry pointer is false; unknown is not null.
    std::optional<bool> flag_2a8,flag_22d; // Base +278/+1FD.
};
struct CellFitState {
    std::optional<ra2map::Cell> object_cell; // Actual Cell +24, after linear aliases or fallback lookup.
    std::optional<int> level; // Signed raw Cell +11B.
    std::optional<bool> bridge; // Cell +140 bit 100.
    std::optional<std::uint8_t> ground_occupancy,bridge_occupancy; // Bytes +124/+128, not inferred from head IDs.
    std::optional<int> overlay_index; // Actual runtime Cell +44, -1 means no overlay.
    std::optional<CellFitOverlay> overlay;
    std::optional<int> land_type; // Final Cell +EC.
};
struct ResolvedCellFitQuery {
    DriveEdition edition=DriveEdition::BaseRa2;
    int speed_type=0;
    bool ignore_low_five=false,ignore_a0=false; // Original parameter masks E0 and 5F; no unproved names.
    std::uint32_t component=0xffffffffu; // Only FFFFFFFF disables the component query here.
    int movement_zone=0,level=-1;
    bool above_bridge=false;
};
struct CellFitCallbacks {
    std::function<std::optional<std::uint32_t>(ra2map::Cell,int,bool)> component; // 0054F8F0 / 0056D230.
    // Selected speed column, not coarse-category Wheel: 00850EA0 / 0089EA40, stride 36 bytes.
    std::function<std::optional<float>(int land,int speed)> land_speed;
};
enum class CellFitStatus { Complete,NeedsInput,Unsupported };
struct CellFitResult {
    CellFitStatus status=CellFitStatus::NeedsInput;
    std::optional<bool> allowed; // Boolean AL result only. Unknown is neither blocked nor allowed.
    std::string detail;
};
// Base 00478760 / expansion 004834A0. Read-only Cell fields and callbacks.
// Occupancy producers remain separate. This body reads bytes, not occupant links.
// Preserve original placement/link order when producing those bytes. This is not full UnitClass entry dispatch.
// Callbacks must read resolved state. Publish only Complete results. This body consumes no shared RNG.
// Requested level -1 can select bridge occupancy even when above_bridge is false.
CellFitResult original_cell_fit(const ResolvedCellFitQuery&,const CellFitState&,const CellFitCallbacks&);
struct RectangleFitQuery {
    DriveEdition edition=DriveEdition::BaseRa2;
    ra2map::Cell origin;
    int width=1,height=1,speed_type=0;
    std::uint32_t component=0xffffffffu;
    int movement_zone=0,level=-1;
    bool above_bridge=false,reject_overlay=false;
};
using CellFitLookup=std::function<std::optional<CellFitState>(ra2map::Cell)>;
struct RectangleFitResult : CellFitResult {
    std::optional<ra2map::Cell> blocked_cell; // Raw requested coordinate, only on a Complete false result.
    unsigned tested=0;
};
// Base 00550CF0 / expansion 0056E7C0. Preserves X-outer/Y-inner order and raw 16-bit wrapping.
// Lookup must resolve original aliases and fallback data. Both occupancy-ignore arguments are zero.
// Positive dimensions above 512 are outside this recovered API domain, not a claimed engine limit.
RectangleFitResult original_rectangle_fit(const RectangleFitQuery&,const CellFitLookup&,const CellFitCallbacks&);
}
