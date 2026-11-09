#include "unit_flags.h"
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

using namespace ra2::session::world;

int main(int argc,char** argv) {
    const bool vectors=argc==2 && std::string(argv[1])=="--vectors";
    const bool constructor_vectors=argc==2 && std::string(argv[1])=="--constructor-vectors";
    if (argc!=1 && !vectors && !constructor_vectors) return 2;
    const auto defaults=base_unit_type_constructor_fields();
    assert(defaults.raw_205 && !defaults.is_train && !defaults.crusher && !defaults.raw_bb0 &&
           !defaults.raw_bb1 && defaults.speed_type==-1 && defaults.movement_zone==0);
    if (constructor_vectors) {
        std::cout<<int(defaults.raw_205)<<' '<<int(defaults.is_train)<<' '<<int(defaults.crusher)<<' '
                 <<int(defaults.raw_bb0)<<' '<<int(defaults.raw_bb1)<<' '
                 <<defaults.speed_type<<' '<<defaults.movement_zone<<'\n';
        return 0;
    }
    const std::vector<std::pair<std::string,std::string>> cases={
        {"","yes"},{"yes","no"},{"true","false"},{"1","0"},
        {"no","yes"},{"false","true"},{"0","1"},{"YeS","nO"},{"TrUe","FaLsE"},{"",""}};
    const std::vector<std::pair<bool,bool>> expected={
        {false,true},{true,false},{true,false},{true,false},
        {false,true},{false,true},{false,true},{true,false},{true,false},{false,false}};
    for (std::size_t i=0;i<cases.size();++i) {
        BaseUnitFlagInput input; input.initial_is_train=false; input.initial_crusher=false;
        input.is_train.state=cases[i].first.empty()?UnitFlagKeyState::Absent:UnitFlagKeyState::Present;
        input.is_train.value=cases[i].first;
        input.crusher.state=cases[i].second.empty()?UnitFlagKeyState::Absent:UnitFlagKeyState::Present;
        input.crusher.value=cases[i].second;
        const auto result=resolve_base_unit_flags(input);
        assert(result.is_train.status==UnitFlagStatus::Complete && result.crusher.status==UnitFlagStatus::Complete);
        assert(result.is_train.value==expected[i].first && result.crusher.value==expected[i].second);
        if (vectors) std::cout<<int(*result.is_train.value)<<' '<<int(*result.crusher.value)<<'\n';
    }
    if (vectors) return 0;
    BaseUnitFlagInput input; input.initial_is_train=false; input.initial_crusher=true;
    input.is_train.state=UnitFlagKeyState::Absent;
    auto result=resolve_base_unit_flags(input);
    assert(result.is_train.value==false && result.crusher.status==UnitFlagStatus::NeedsInput);
    input.crusher.state=UnitFlagKeyState::Absent;
    assert(resolve_base_unit_flags(input).crusher.value==true);
    input.is_train.state=UnitFlagKeyState::Present; input.is_train.value="YeS";
    assert(resolve_base_unit_flags(input).is_train.value==true);
    input.is_train.value="unknown";
    assert(resolve_base_unit_flags(input).is_train.status==UnitFlagStatus::Unsupported);
    input.is_train.value="";
    assert(resolve_base_unit_flags(input).is_train.status==UnitFlagStatus::Unsupported);
    input.initial_is_train.reset(); input.is_train.value="yes";
    assert(resolve_base_unit_flags(input).is_train.status==UnitFlagStatus::NeedsInput);
    std::cout<<"checked base IsTrain and Crusher values pass\n";
}
