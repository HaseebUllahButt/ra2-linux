#include "overlay_cell_setup.h"
#include "sim/test_cell_setup_inputs.h"
#include <cassert>
#include <iomanip>
#include <iostream>
using namespace ra2;
int main() {
    for (unsigned seed=0;seed<512;++seed) {
        if (seed%8<3) continue; // The checked path requires a valid tile/subtile.
        sim::test::CellSetupFixture f(seed);
        f.state.terrain.overlay_type_index=102;
        f.callbacks.category=[&](const sim::TerrainCell&) -> std::optional<sim::CoarseCategoryInputs> {
            sim::CoarseCategoryInputs value;
            value.usable=f.usable; value.overlay_index=102; value.overlay.emplace();
            value.overlay->flag_22d=false; value.overlay->flag_2a8=false;
            value.overlay->flag_2b5=false; value.overlay->flag_2b4=false; value.overlay->land_wheel=0.5f;
            value.land_wheel=0.5f; value.occupants=std::vector<sim::CoarseOccupantState>{};
            if (f.unit) { sim::CoarseOccupantState object; object.runtime_type=1; value.occupants->push_back(object); }
            return value;
        };
        const auto unknown=session::world::setup_base_resource_overlay_cell(f.state,f.guard,f.theater,f.cliff,
                                                                            f.callbacks,5,false,{});
        assert(unknown.status==sim::CellSetupStatus::NeedsInput);
        const auto result=session::world::setup_base_resource_overlay_cell(f.state,f.guard,f.theater,f.cliff,
                                                                           f.callbacks,5,false,true);
        assert(result.status==sim::CellSetupStatus::Complete && result.state.terrain.overlay_type_index==102 &&
               result.state.terrain.land_type==5);
        std::cout<<seed<<' '<<std::hex<<std::setw(8)<<std::setfill('0')<<f.digest(result)<<std::dec<<'\n';
    }
}
