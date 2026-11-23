#include "test_local_reach_inputs.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace ra2::sim;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char** argv) try {
    const bool extra=argc>1 && std::string(argv[1])=="--zone12";
    std::uint32_t hash=2166136261u,common_hash=0;
    for (unsigned seed=extra?4096:0;seed<(argc>1?(extra?4224:4096):4224);++seed) {
        if (seed==4096) hash=2166136261u;
        test::LocalReachFixture fixture(seed);
        const auto out=original_local_reachability(fixture.inputs,fixture.callbacks);
        require(out.status==RetryEdgesStatus::Complete,"resolved local reachability failed");
        const auto digest=fixture.digest(out); hash=(hash^digest)*16777619u;
        if (seed==4095) common_hash=hash;
        if (argc>1) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
    }
    if (argc>1) return 0;
    std::cout<<"local reachability digests "<<std::hex<<common_hash<<" "<<hash<<std::dec<<"\n";
    require(common_hash==0x2f2a3c1eu && hash==0x67deb132u,"original local reachability differs");
    test::LocalReachFixture fixture(0); fixture.inputs.movement_zone.reset();
    require(original_local_reachability(fixture.inputs,fixture.callbacks).status==RetryEdgesStatus::NeedsInput,"unknown movement zone was accepted");
    fixture.inputs.movement_zone=0; fixture.callbacks.entry={};
    require(original_local_reachability(fixture.inputs,fixture.callbacks).status==RetryEdgesStatus::NeedsInput,"missing entry body was accepted");
    fixture.inputs.level=3;
    require(original_local_reachability(fixture.inputs,fixture.callbacks).status==RetryEdgesStatus::Unsupported,"invalid local level was accepted");
    
    test::LocalReachFixture scan(0); scan.inputs.source=5*16+5; scan.inputs.level=1;
    scan.callbacks.scan_region=[](ra2map::Cell,unsigned)->std::optional<std::uint16_t> { return 6; };
    scan.callbacks.links=[](std::uint16_t,unsigned)->std::optional<std::vector<std::uint16_t>> { return std::vector<std::uint16_t>{}; };
    const auto result=original_local_reachability(scan.inputs,scan.callbacks);
    unsigned queries=0;
    for (const auto& event:scan.events) if (event[0]==1) { require(event[1]==6 && event[2]==6,"final scan changed its repeated coordinate"); ++queries; }
    require(result.status==RetryEdgesStatus::Complete && !result.reachable && queries==16,"original repeated scan count differs");
    std::cout<<"4096 common and 128 Yuri local reachability cases and four state checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
