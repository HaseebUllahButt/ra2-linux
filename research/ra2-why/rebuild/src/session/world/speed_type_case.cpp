#include "speed_type.h"
#include <cassert>
#include <iostream>

using namespace ra2::session::world;

int main(int argc,char** argv) {
    const bool vectors=argc==2 && std::string(argv[1])=="--vectors";
    if (argc!=1 && !vectors) return 2;
    BaseSpeedTypeInput input;
    assert(resolve_base_speed_type(input).status==SpeedTypeStatus::NeedsInput);
    input.initial_type=-1; input.crusher=true;
    assert(resolve_base_speed_type(input).status==SpeedTypeStatus::NeedsInput);
    input.key_state=SpeedKeyState::Absent;
    const auto real=resolve_base_speed_type(input);
    assert(real.status==SpeedTypeStatus::Complete && real.index==1);
    input.key_state=SpeedKeyState::Present; input.key_value="Wheel";
    const auto wheel=resolve_base_speed_type(input);
    assert(wheel.status==SpeedTypeStatus::Complete && wheel.index==2);
    input.key_state=SpeedKeyState::Absent; input.crusher=false;
    const auto noncrusher=resolve_base_speed_type(input);
    assert(noncrusher.status==SpeedTypeStatus::Complete && noncrusher.index==2);
    if (vectors) {
        std::cout<<*real.index<<'\n'<<*wheel.index<<'\n'<<*noncrusher.index<<'\n';
        return 0;
    }
    input.initial_type=1; input.crusher.reset();
    assert(resolve_base_speed_type(input).index==1);
    input.key_state=SpeedKeyState::Present; input.key_value="wHeEl";
    assert(resolve_base_speed_type(input).index==2);
    input.initial_type=-1; input.crusher.reset();
    assert(resolve_base_speed_type(input).status==SpeedTypeStatus::NeedsInput);
    input.initial_type=1; input.key_state=SpeedKeyState::Unknown;
    assert(resolve_base_speed_type(input).status==SpeedTypeStatus::NeedsInput);
    input.key_state=SpeedKeyState::Present; input.key_value="Hover";
    assert(resolve_base_speed_type(input).status==SpeedTypeStatus::Unsupported);
    input.initial_type=7; input.key_state=SpeedKeyState::Absent;
    assert(resolve_base_speed_type(input).status==SpeedTypeStatus::Unsupported);
    std::cout<<"checked base Track and Wheel SpeedType cases pass\n";
}
