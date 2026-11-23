#include "test_overlay_inputs.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace ra2::sim;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char**) try {
    std::uint32_t hash=2166136261u;
    for (unsigned seed=0;seed<4096;++seed) {
        test::OverlayFixture fixture(seed);
        const auto result=dispatch_original_overlay_mark(fixture.mode,fixture.env,fixture.callbacks);
        require(result.status==OverlayMarkStatus::Applied || result.status==OverlayMarkStatus::Rejected,"resolved overlay fixture failed");
        const auto digest=fixture.digest(result);
        if (argc>1) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
        hash=(hash^digest)*16777619u;
    }
    if (argc>1) return 0;
    std::cout<<"overlay dispatch digest "<<std::hex<<hash<<std::dec<<"\n";
    // Filled from both original x86 editions after independent event comparison.
    require(hash==0x6d535f32u,"original overlay dispatch differs");
    unsigned errors=0;
    for (int index:{122,123,124,125,233,234,235,236}) {
        test::OverlayFixture fixture(0); fixture.type.index=index; fixture.type.land=0; fixture.type.wall=false;
        require(dispatch_original_overlay_mark(1,fixture.env,fixture.callbacks).status==OverlayMarkStatus::Unsupported,"bridge approach was accepted"); ++errors;
    }
    test::OverlayFixture missing(0); missing.parent=true; missing.type.index=0; missing.ramp=0;
    missing.type.land.reset();
    require(dispatch_original_overlay_mark(1,missing.env,missing.callbacks).status==OverlayMarkStatus::NeedsInput,"unknown overlay land was accepted"); ++errors;
    missing.callbacks.effect={};
    require(dispatch_original_overlay_mark(1,missing.env,missing.callbacks).status==OverlayMarkStatus::NeedsInput,"missing overlay body was accepted"); ++errors;
    std::cout<<"4096 original overlay dispatch cases and "<<errors<<" explicit gaps pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
