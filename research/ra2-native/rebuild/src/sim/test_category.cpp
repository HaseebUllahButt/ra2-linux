#include "coarse_category.h"
#include "../rules/ini_file.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
float as_float(std::uint32_t bits) { float value; std::memcpy(&value,&bits,4); return value; }
struct Actor { int kind; unsigned flags,state,a,b; };
CoarseCategoryInputs inputs(unsigned seed) {
    std::uint32_t state=0x51ed270bu^seed;
    const auto next=[&]() { state=state*1664525u+1013904223u; return state>>16; };
    bool present=next()%2; unsigned flags=next()%16,land=next()%13;
    const std::uint32_t floats[]={0,0x80000000u,0x3f800000u,0x3f000000u,0xbf800000u,0x3c23d709u,0x3c23d70au,0x3c23d70bu,0x7fc00000u,0x7f800000u,0xff800000u};
    auto overlay_wheel=floats[next()%11],land_wheel=floats[next()%11];
    std::vector<Actor> actors;
    const unsigned count=next()%5;
    for (unsigned i=0;i<count;++i) {
        const int kinds[]={6,36,28,15}; const unsigned states[]={0,8,12,17};
        const int kind=kinds[next()%4];
        actors.push_back({kind,next()%8,states[next()%4],next()%9,next()%9});
    }
    const int mode=next()%3; bool usable=next()%8!=0;
    if (!seed) { present=false; flags=0; land=0; land_wheel=0x3f800000; actors.clear(); usable=true; }
    if (seed>=1 && seed<=6) {
        present=false; flags=0; land=0; land_wheel=0x3f800000; actors={{6,2,0,0,0}}; usable=true;
        if (seed==2) actors.push_back({6,2,0,0,0});
        if (seed==3) actors.push_back({36,0,0,0,0});
        if (seed==4) actors.push_back({36,0,0,7,7});
        if (seed==5) actors={{6,5,0,0,0}};
        if (seed==6) actors={{6,1,0,0,0}};
    }
    CoarseCategoryInputs result;
    result.usable=usable; result.overlay_index=present?0:-1;
    result.overlay=CoarseOverlayState{static_cast<bool>(flags&1),static_cast<bool>(flags&2),static_cast<bool>(flags&4),static_cast<bool>(flags&8),as_float(overlay_wheel)};
    result.land_type=land; result.land_wheel=as_float(land_wheel); result.session_field_1258=mode;
    result.occupants=std::vector<CoarseOccupantState>{};
    for (const auto& actor:actors) {
        CoarseOccupantState value;
        value.runtime_type=actor.kind;
        value.building_flag_16c0=actor.flags&1; value.building_flag_16bf=actor.flags&2; value.house_flag_1fa=actor.flags&4;
        value.building_state_618=actor.state; value.terrain_field_2a8=actor.a; value.terrain_field_2ac=actor.b;
        result.occupants->push_back(value);
    }
    return result;
}
std::uint32_t hash(const CoarseCategoryResult& value) {
    auto h=2166136261u;
    h=(h^value.category)*16777619u; h=(h^static_cast<std::uint32_t>(value.writes.size()))*16777619u;
    for (const auto write:value.writes) h=(h^write)*16777619u;
    return h;
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc,char** argv) try {
    unsigned checks=0; std::uint32_t h=2166136261u;
    const std::uint32_t checkpoints[]={0x93665191u,0xded206b6u,0x8f5ebfcdu,0x43842c9fu,0x34edc023u,0x8fdec326u,0xa02124c2u,0x94944648u,0x6f0b8bf0u,0x085c218cu,0xda176264u,0xb62e7a23u,0xacb7c4fbu,0x4ec9135fu,0x793344d7u,0xda970ea0u};
    for (unsigned seed=0;seed<4096;++seed) {
        h=(h^hash(original_coarse_category(inputs(seed))))*16777619u;
        if (seed%256==255 && h!=checkpoints[seed/256]) {
            std::cerr<<"ending seed="<<seed<<" hash="<<std::hex<<h<<"\n";
            throw std::runtime_error("original coarse category or ordered writes differ");
        }
        ++checks;
    }
    require(original_coarse_category(inputs(1)).writes==std::vector<std::uint8_t>({6,0}),"original overwritten building write was lost"); ++checks;
    require(original_coarse_category(inputs(2)).writes==std::vector<std::uint8_t>({6,6,0}),"original linked building write order differs"); ++checks;
    const auto rejected=[&](const CoarseCategoryInputs& value) {
        try { original_coarse_category(value); }
        catch (const std::invalid_argument&) { ++checks; return; }
        throw std::runtime_error("unknown required coarse category input accepted");
    };
    CoarseCategoryInputs value; rejected(value);
    value.usable=false; require(original_coarse_category(value).category==7,"unusable cell reads unresolved fields"); ++checks;
    value.usable=true; rejected(value);
    value.overlay_index=-2; rejected(value); value.overlay_index=0; rejected(value);
    value.overlay=CoarseOverlayState{}; rejected(value);
    value.overlay->flag_22d=true; require(original_coarse_category(value).category==1,"overlay early return reads unresolved fields"); ++checks;
    value=inputs(0); value.land_type.reset(); rejected(value);
    value=inputs(0); value.land_wheel.reset(); rejected(value);
    value=inputs(0); value.occupants.reset(); rejected(value);
    value=inputs(1); value.occupants->front().runtime_type.reset(); rejected(value);
    value=inputs(3); value.session_field_1258.reset(); rejected(value);
    if (argc!=2) throw std::runtime_error("usage: test_category ORIGINAL_RULESMD_INI");
    const auto rules=ra2::rules::IniFile::from_file(argv[1]);
    std::uint32_t columns=2166136261u;
    for (const auto& profile:std::array<std::array<float,2>,8>{{{0,1},{1,0},{.125f,.5f},{.5f,.125f},{1,1.f/128},{0,1.f/64},{.5f,-.25f},{0,1.5f}}}) {
        const float track=std::min(profile[0],1.0f),wheel=std::min(profile[1],1.0f);
        value=inputs(0); value.land_wheel=wheel;
        const auto category=original_coarse_category(value).category;
        require(category==(wheel<=.01f?6:0),"distinct Wheel category differs"); ++checks;
        std::uint32_t track_bits,wheel_bits; std::memcpy(&track_bits,&track,4); std::memcpy(&wheel_bits,&wheel,4);
        std::uint32_t row=2166136261u;
        for (auto word:{track_bits,wheel_bits,static_cast<std::uint32_t>(category)}) row=(row^word)*16777619u;
        columns=(columns^row)*16777619u;
    }
    require(columns==0x8d749c93u,"original loader-to-category columns differ");
    const auto tiberium_track=rules.read_double("Tiberium","Track");
    const auto tiberium_wheel=rules.read_double("Tiberium","Wheel");
    require(rules.read_string("Tiberium","Track")=="70%" && rules.read_string("Tiberium","Wheel")=="50%" &&
        static_cast<float>(tiberium_track)==.7f && tiberium_wheel==.5,"original distinct Tiberium columns differ");
    value=inputs(0); value.land_type=5; value.land_wheel=static_cast<float>(tiberium_wheel);
    require(original_coarse_category(value).category==0,"original Tiberium Wheel category differs"); ++checks;
    struct Fixture { const char* name; int land,category; };
    for (const auto& fixture:std::array<Fixture,5>{{{"Clear",0,0},{"Road",1,0},{"Water",2,4},{"Rock",3,6},{"Beach",6,3}}}) {
        const auto multiplier=ra2::rules::IniFile::double_value(rules.read_string(fixture.name,"Wheel"));
        require(multiplier.has_value(),"original rules Wheel input is missing");
        value=inputs(0); value.land_type=fixture.land; value.land_wheel=static_cast<float>(*multiplier);
        require(original_coarse_category(value).category==fixture.category,"original rules coarse category differs"); ++checks;
    }
    std::cout<<"original Cell coarse category and ordered writes: "<<checks<<" checks passed, hash="<<std::hex<<h<<"\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
