#include "cell_occupancy.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace ra2::session::world;
int main() {
    for (int bridge=0;bridge<2;++bridge) for (int z: {-1,0,1}) for (unsigned slot=0;slot<5;++slot)
        for (int bits: {0,1,4,7,28,255}) for (int remove=0;remove<2;++remove) {
            SubcellOccupancy cell{static_cast<std::uint8_t>(bits),static_cast<std::uint8_t>(bits^0x55),3,9};
            const auto layer=subcell_layer(!remove,0,0,z,bridge!=0); assert(layer);
            assert(remove?remove_subcell(cell,*layer,slot):add_subcell(cell,*layer,slot,7));
            std::cout<<int(cell.ground)<<' '<<int(cell.bridge)<<' '<<cell.ground_owner<<' '<<cell.bridge_owner<<'\n';
        }
    SubcellOccupancy invalid;
    assert(!add_subcell(invalid,OccupancyLayer::Ground,5,7));
    assert(!remove_subcell(invalid,static_cast<OccupancyLayer>(2),0));
    assert(!subcell_layer(true,0,0,0,{}));
    assert(!subcell_layer(true,std::numeric_limits<std::int32_t>::max(),1,0,false));
    assert(!terrain_occupancy_mask({},7,7));
    assert(!terrain_occupancy_mask(1,7,{}));
    for (int session=0;session<2;++session) for (unsigned mask=0;mask<8;++mask)
        for (int bits: {0,1,4,7,28,255}) for (int remove=0;remove<2;++remove) {
            auto occupancy=static_cast<std::uint8_t>(bits);
            const auto selected=terrain_occupancy_mask(session,session==0?mask:mask^7,
                                                       session==1?mask:mask^7);
            assert(selected && *selected==mask);
            if (remove) remove_terrain_occupancy(occupancy,*selected);
            else add_terrain_occupancy(occupancy,*selected);
            std::cout<<int(occupancy)<<'\n';
        }
}
