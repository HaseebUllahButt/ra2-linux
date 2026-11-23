#include "cell_occupancy.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace ra2::session::world;
int main() {
    for (int bridge=0;bridge<2;++bridge) for (int z: {-1,0,1}) for (unsigned slot=0;slot<5;++slot)
        for (std::uint32_t bits: {0u,1u,4u,7u,28u,255u,0xa1b2c3ffu}) for (int remove=0;remove<2;++remove) {
            SubcellOccupancy cell{bits,bits^0x55u,3,9};
            const auto layer=subcell_layer(!remove,0,0,z,bridge!=0); assert(layer);
            assert(remove?remove_subcell(cell,*layer,slot):add_subcell(cell,*layer,slot,7));
            std::cout<<cell.ground<<' '<<cell.bridge<<' '<<cell.ground_owner<<' '<<cell.bridge_owner<<'\n';
        }
    SubcellOccupancy invalid;
    assert(!add_subcell(invalid,OccupancyLayer::Ground,5,7));
    assert(!remove_subcell(invalid,static_cast<OccupancyLayer>(2),0));
    assert(!add_unit_occupancy(invalid,static_cast<OccupancyLayer>(2)));
    assert(!remove_unit_occupancy(invalid,static_cast<OccupancyLayer>(2)));
    assert(!subcell_layer(true,0,0,0,{}));
    assert(!subcell_layer(true,std::numeric_limits<std::int32_t>::max(),1,0,false));
    assert(!terrain_occupancy_mask({},7,7));
    assert(!terrain_occupancy_mask(1,7,{}));
    for (int session=0;session<2;++session) for (unsigned mask=0;mask<8;++mask)
        for (std::uint32_t bits: {0u,1u,4u,7u,28u,255u,0xa1b2c3ffu}) for (int remove=0;remove<2;++remove) {
            auto occupancy=bits;
            const auto selected=terrain_occupancy_mask(session,session==0?mask:mask^7,
                                                       session==1?mask:mask^7);
            assert(selected && *selected==mask);
            if (remove) remove_terrain_occupancy(occupancy,*selected);
            else add_terrain_occupancy(occupancy,*selected);
            std::cout<<occupancy<<'\n';
        }
    for (int bridge=0;bridge<2;++bridge) for (int z: {-1,0,1})
        for (std::uint32_t bits: {0u,1u,4u,7u,28u,255u,0xa1b2c3ffu}) for (int remove=0;remove<2;++remove) {
            SubcellOccupancy cell{bits,bits^0x55u,3,9};
            const auto layer=subcell_layer(!remove,0,0,z,bridge!=0); assert(layer);
            assert(remove?remove_unit_occupancy(cell,*layer):add_unit_occupancy(cell,*layer));
            assert(cell.ground_owner==3 && cell.bridge_owner==9);
            std::cout<<cell.ground<<' '<<cell.bridge<<'\n';
        }
}
