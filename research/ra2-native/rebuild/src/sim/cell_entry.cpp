#include "cell_entry.h"
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace ra2::sim {
HeightEntryResult original_height_entry(const HeightEntryInputs& in) {
    auto out=HeightEntryResult{0,in.layer_height,in.above_bridge};
    const auto& to=in.destination;
    if (in.direction==-1) {
        if (out.layer_height==-1 && to.flag_100) out.layer_height=to.level+4;
        return out;
    }
    if (!in.source) throw std::invalid_argument("004D9C60: source cell lookup is unresolved");
    const auto& from=*in.source;
    if (out.layer_height==-1 && from.flag_100) {
        out.layer_height=from.level+4;
        if (!to.flag_200) { out.status=7; return out; }
    }
    const auto difference=static_cast<std::int64_t>(from.flag_100?from.level:out.layer_height)-to.level;
    if (difference<std::numeric_limits<int>::min() || difference>std::numeric_limits<int>::max())
        throw std::range_error("004D9C60: overflowing original height difference is unsupported");
    const auto magnitude=std::abs(difference);
    if (!magnitude) {
        if (!(to.flag_100 && to.flag_200 && from.flag_100) && out.layer_height!=-1 && out.layer_height!=to.level) out.status=7;
    } else if (magnitude==1) {
        if (difference<0 ? !from.ramp : !to.ramp) out.status=7;
    } else {
        if (magnitude!=4) { out.status=7; return out; }
        if (from.level==to.level-4 && (out.layer_height!=to.level || !from.flag_100)) { out.status=7; return out; }
        if (to.level==from.level-4) {
            if (!to.flag_100 || !to.flag_200) { out.status=7; return out; }
            out.above_bridge=true;
        }
    }
    return out;
}
}
