#include "test_hierarchy_retry_inputs.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace ra2::sim;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char**) try {
    std::uint32_t hash=2166136261u;
    for (unsigned seed=0;seed<4096;++seed) {
        test::HierarchyRetryFixture fixture(seed);
        const auto out=run_original_hierarchy_retry(fixture.graph,fixture.inputs,fixture.state,fixture.callbacks);
        require(out.status==RetryEdgesStatus::Complete,"resolved hierarchy retry dispatch failed");
        const auto digest=fixture.digest(out);
        hash=(hash^digest)*16777619u;
        if (argc>1) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
    }
    if (argc>1) return 0;
    std::cout<<"hierarchy retry dispatch digest "<<std::hex<<hash<<std::dec<<"\n";
    require(hash==0x082c66e3u,"original hierarchy retry dispatch differs");
    test::HierarchyRetryFixture missing(0); missing.inputs.progress_cell.reset();
    auto out=run_original_hierarchy_retry(missing.graph,missing.inputs,missing.state,missing.callbacks);
    require(out.status==RetryEdgesStatus::NeedsInput && out.events.empty() && out.state.blocked==missing.state.blocked,"unknown progress changed retry state");
    missing.inputs.progress_cell=ra2map::Cell{54,69}; missing.callbacks.reachability={};
    out=run_original_hierarchy_retry(missing.graph,missing.inputs,missing.state,missing.callbacks);
    require(out.status==RetryEdgesStatus::NeedsInput && out.events.size()==2 && out.state.blocked==missing.state.blocked,"missing local reachability changed retry state");
    test::HierarchyRetryFixture partial(0);
    partial.callbacks.reachability=[&](ra2map::Cell,unsigned level)->std::optional<RetryReachability> {
        if (level) return {};
        const auto target=static_cast<std::uint16_t>(partial.regions[0][0]+1);
        return RetryReachability{false,{target,target}};
    };
    out=run_original_hierarchy_retry(partial.graph,partial.inputs,partial.state,partial.callbacks);
    require(out.status==RetryEdgesStatus::NeedsInput && out.state.blocked[0].size()==partial.state.blocked[0].size()+2 && out.state.blocked[1]==partial.state.blocked[1],"unknown later level lost completed private retry state");
    std::cout<<"4096 original hierarchy retry dispatch cases and three unknown-input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
