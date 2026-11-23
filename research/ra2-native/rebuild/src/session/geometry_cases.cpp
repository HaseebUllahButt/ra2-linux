#include "session.h"
#include <iostream>
using namespace ra2;
int main() {
    for (const auto size : {session::MapRectangle{0,0,80,80},session::MapRectangle{0,0,100,80},
                            session::MapRectangle{0,0,45,55}})
        for (const auto local : {session::MapRectangle{0,0,80,80},session::MapRectangle{2,4,35,40},
                                 session::MapRectangle{-3,-1,30,33},session::MapRectangle{10,12,90,100}}) {
            const auto g=session::resolve_placement_geometry(size,local);
            if (!g) continue;
            for (int y : {-1,0,2,20,39,40,41,60,79,100,200})
                for (int x : {-1,0,2,20,39,40,41,60,79,100,200})
                    for (int height : {0,2,7}) for (int ramp : {0,1}) {
                        const session::StartPoint point{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
                        const auto cell=session::PlacementCell{static_cast<std::int8_t>(height),static_cast<std::uint8_t>(ramp)};
                        const auto b=session::placement_cell_in_map(point,session::PlacementEdition::Base,*g,cell);
                        const auto yuri=session::placement_cell_in_map(point,session::PlacementEdition::Yuri,*g,cell);
                        if (!b || !yuri) return 1;
                        std::cout<<size.width<<' '<<size.height<<' '<<local.x<<' '<<local.y<<' '
                                 <<local.width<<' '<<local.height<<' '<<g->local.x<<' '<<g->local.y<<' '
                                 <<g->local.width<<' '<<g->local.height<<' '<<x<<' '<<y<<' '<<height<<' '
                                 <<ramp<<' '<<*b<<' '<<*yuri<<'\n';
                    }
        }
}
