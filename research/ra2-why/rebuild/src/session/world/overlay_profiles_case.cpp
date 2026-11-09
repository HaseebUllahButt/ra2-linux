#include "overlay_profiles.h"
#include <cassert>
#include <iostream>
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto assets=BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    const auto types=prepare_base_overlay_inputs(assets);
    assert(types && types->size()==17 && assets.overlays_in_scan_order().size()==407);
    const auto& bridge=types->at(24);
    assert(bridge.name=="BRIDGE1" && bridge.shape.filename=="BRIDGE.TEM" && !bridge.rules.tiberium);
    const auto& ore=types->at(102);
    assert(ore.name=="TIB01" && ore.rules.tiberium && ore.rules.land==5 && !ore.rules.crate &&
           ore.shape.filename=="TIB01.TEM");
    const auto& rubble=types->at(239);
    assert(rubble.name=="RUBBLE_OVERLAY" && rubble.shape.filename=="SROCK01.TEM");
    for (const auto& overlay:assets.overlays_in_scan_order()) {
        assert(types->count(overlay.type_index));
        const auto& fields=types->at(overlay.type_index).rules;
        if (fields.tiberium) assert(fields.land==5 && !fields.no_use_tile_land && !fields.wall);
    }
    std::cout<<"Prepared 17 MP02T2 Overlay type inputs from installed assets; runtime state remains unresolved\n";
}
