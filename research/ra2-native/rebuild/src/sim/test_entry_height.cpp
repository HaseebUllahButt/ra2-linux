#include "cell_entry.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ra2::sim;
int main() try {
    std::uint32_t hash=2166136261u; unsigned cases=0;
    for (int source_level:{-128,-4,-1,0,1,4,127})
    for (int destination_level:{-128,-4,-1,0,1,4,127})
    for (int layer:{-132,-128,-4,-1,0,1,4,127,131})
    for (int source_ramp:{0,1}) for (int destination_ramp:{0,1})
    for (unsigned source_flags:{0u,0x100u,0x200u,0x300u})
    for (unsigned destination_flags:{0u,0x100u,0x200u,0x300u})
    for (int direction:{-1,0}) for (bool above:{false,true}) {
        HeightEntryInputs in;
        in.source=EntryHeightCell{static_cast<std::int8_t>(source_level),static_cast<std::uint8_t>(source_ramp),(source_flags&0x100u)!=0,(source_flags&0x200u)!=0};
        in.destination={static_cast<std::int8_t>(destination_level),static_cast<std::uint8_t>(destination_ramp),(destination_flags&0x100u)!=0,(destination_flags&0x200u)!=0};
        in.direction=direction; in.layer_height=layer; in.above_bridge=above;
        const auto out=original_height_entry(in);
        for (int value:{out.status,out.layer_height,static_cast<int>(out.above_bridge)}) hash=(hash^static_cast<std::uint32_t>(value))*16777619u;
        ++cases;
    }
    std::cout<<"native height entry cases="<<cases<<" hash="<<std::hex<<hash<<std::dec<<"\n";
    // The expected hash comes from both original executables.
    if (cases!=112896 || hash!=0x5d4cc105u) throw std::runtime_error("original height-gate grid differs");
    HeightEntryInputs unresolved; unresolved.direction=0;
    bool rejected=false;
    try { (void)original_height_entry(unresolved); } catch(const std::invalid_argument&) { rejected=true; }
    if (!rejected) throw std::runtime_error("unknown source lookup was accepted");
    HeightEntryInputs overflowing; overflowing.source=EntryHeightCell{}; overflowing.direction=0;
    overflowing.layer_height=std::numeric_limits<int>::max(); overflowing.destination.level=-128;
    rejected=false;
    try { (void)original_height_entry(overflowing); } catch(const std::range_error&) { rejected=true; }
    if (!rejected) throw std::runtime_error("unverified signed overflow was accepted");
    std::cout<<"original height entry grid and explicit limits passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
