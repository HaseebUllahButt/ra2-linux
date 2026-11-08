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
}
