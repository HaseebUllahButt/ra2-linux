#include "movement_zone.h"
#include <cassert>
#include <iostream>

using namespace ra2::session::world;

int main(int argc,char** argv) {
    const bool vectors=argc==2 && std::string(argv[1])=="--vectors";
    if (argc!=1 && !vectors) return 2;
    BaseMovementZoneInput input;
    assert(resolve_base_movement_zone(input).status==ZoneStatus::NeedsInput);
    input.initial_zone=0;
    assert(resolve_base_movement_zone(input).status==ZoneStatus::NeedsInput);
    input.key_state=ZoneKeyState::Absent;
    const auto absent=resolve_base_movement_zone(input);
    assert(absent.status==ZoneStatus::Complete && absent.zone==0 && absent.raw_flag_af5==false);
    input.key_state=ZoneKeyState::Present; input.key_value="Normal";
    const auto installed=resolve_base_movement_zone(input);
    assert(installed.status==ZoneStatus::Complete && installed.zone==0 && installed.raw_flag_af5==false);
    input.key_value="Subterannean";
    const auto synthetic=resolve_base_movement_zone(input);
    assert(synthetic.status==ZoneStatus::Complete && synthetic.zone==6 && synthetic.raw_flag_af5==true);
    if (vectors) {
        for (const auto& result:{absent,installed,synthetic})
            std::cout<<*result.zone<<' '<<int(*result.raw_flag_af5)<<'\n';
        return 0;
    }
    input.key_value="nOrMaL";
    assert(resolve_base_movement_zone(input).zone==0);
    input.key_value="Destroyer";
    assert(resolve_base_movement_zone(input).status==ZoneStatus::Unsupported);
    input.initial_zone=12; input.key_value="Normal";
    assert(resolve_base_movement_zone(input).status==ZoneStatus::Unsupported);
    input.initial_zone=1; input.key_state=ZoneKeyState::Absent;
    assert(resolve_base_movement_zone(input).status==ZoneStatus::Unsupported);
    input.initial_zone.reset();
    assert(resolve_base_movement_zone(input).status==ZoneStatus::NeedsInput);
    std::cout<<"checked base MovementZone and raw +AF5 cases pass\n";
}
