#include "bridge_footprint.h"
#include <cassert>
#include <iostream>
using namespace ra2::session::world;
int main() {
    for (int setter=0;setter<2;++setter) for (unsigned direction=0;direction<8;++direction)
        for (int initial=0;initial<2;++initial) for (int enabled=0;enabled<2;++enabled) {
            const auto cells=bridge_flag_cells({20,20},direction); assert(cells);
            for (int y=17;y<=23;++y) for (int x=17;x<=23;++x) {
                bool value=initial;
                for (const auto cell:*cells) if (cell.x==x && cell.y==y) value=enabled;
                std::cout<<int(value);
            }
            std::cout<<'\n';
        }
    assert(!bridge_flag_cells({1,20},0));
    assert(!bridge_flag_cells({20,20},8));
    assert(!overlay_bridge_direction(24,0,1,false));
    assert(!overlay_bridge_direction(24,5,1,true));
    assert(!overlay_bridge_direction(24,0,2,true));
    assert(!overlay_bridge_direction(26,0,1,true));
    for (int type: {24,25,237,238}) for (int mode: {1,3}) for (int ramp: {0,4}) {
        assert(!overlay_bridge_direction(type,0,mode,false));
        assert(!overlay_bridge_direction(type,5,mode,true));
        assert(!overlay_bridge_direction(type,0,2,true));
        const auto direction=overlay_bridge_direction(type,static_cast<std::uint8_t>(ramp),mode,true);
        assert(direction);
        const auto cells=bridge_flag_cells({20,20},*direction); assert(cells);
        for (int y=17;y<=23;++y) for (int x=17;x<=23;++x) {
            bool value=false;
            for (const auto cell:*cells) if (cell.x==x && cell.y==y) value=true;
            std::cout<<int(value);
        }
        std::cout<<'\n';
    }
}
