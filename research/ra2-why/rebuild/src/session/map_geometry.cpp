#include "session.h"
#include <algorithm>
#include <cstdint>
namespace ra2::session {
std::optional<PlacementGeometry> resolve_placement_geometry(MapRectangle size, MapRectangle local) {
    // 00565C10 uses Size width and height. 00567230 intersects LocalSize
    // with the reset full-map rectangle and applies the original edge limits.
    if (size.width<2 || size.height<1 || size.width>511 || size.height>510 ||
        size.width+size.height>512 || local.width<=0 || local.height<=0) return std::nullopt;
    const auto left=std::max<std::int64_t>(0,local.x);
    const auto top=std::max<std::int64_t>(0,local.y);
    const auto right=std::min<std::int64_t>(size.width,std::int64_t(local.x)+local.width);
    const auto bottom=std::min<std::int64_t>(size.height,std::int64_t(local.y)+local.height);
    if (right<=left || bottom<=top) return std::nullopt;
    const int x=static_cast<int>(std::max<std::int64_t>(left,2));
    const int y=static_cast<int>(std::max<std::int64_t>(top,2));
    const int width=static_cast<int>(std::min<std::int64_t>(right-left,size.width-x-2));
    const int height=static_cast<int>(std::min<std::int64_t>(bottom-top,size.height-y-6));
    if (width<=0 || height<=0) return std::nullopt;
    return PlacementGeometry{size.width,size.height,{x,y,width,height}};
}
std::optional<bool> placement_cell_in_map(StartPoint point, PlacementEdition edition,
    const PlacementGeometry& geometry, std::optional<PlacementCell> cell) {
    // Base 0054AAB0 checks the allocated diamond. It does not inspect height.
    const std::int64_t x=point.x,y=point.y,width=geometry.width,height=geometry.height;
    if (width<2 || height<1 || width>511 || height>510 || width+height>512) return std::nullopt;
    if (edition==PlacementEdition::Base)
        return x+y>width && x-y<width && y-x<width && x+y<=width+2*height;
    // Yuri 00578460 uses the resolved live local rectangle and Cell +11B/11C.
    if (edition!=PlacementEdition::Yuri) return std::nullopt;
    const auto& local=geometry.local;
    if (!cell || local.x<0 || local.y<0 || local.width<=0 || local.height<=0 ||
        std::int64_t(local.x)+local.width>width || std::int64_t(local.y)+local.height>height)
        return std::nullopt;
    std::int64_t elevation=cell->height;
    if (cell->ramp && x+y<width+4+std::int64_t(local.y)*2+elevation) ++elevation;
    return x+y>width+std::int64_t(local.y)*2+elevation &&
           x+y<=width+2+(std::int64_t(local.height)+local.y)*2+elevation &&
           x-y<(std::int64_t(local.width)+local.x)*2-width &&
           y-x<width-std::int64_t(local.x)*2;
}
}
