#include "test_retry_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char**) try {
    std::uint32_t hash=2166136261u;
    for (unsigned seed=0;seed<4096;++seed) {
        test::RetryFixture fixture(seed);
        const auto out=block_original_hierarchy_exit(fixture.layer,fixture.path,fixture.current,fixture.state);
        require(out.status==RetryEdgesStatus::Complete,"resolved hierarchy retry failed");
        const auto digest=test::retry_digest(out.state);
        hash=(hash^digest)*16777619u;
        if (argc>1) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
    }
    if (argc>1) return 0;
    std::cout<<"hierarchy retry digest "<<std::hex<<hash<<std::dec<<"\n";
    require(hash==0x0bba8654u,"original hierarchy retry differs");
    HierarchyLayer graph; graph.nodes.resize(3);
    graph.nodes[0].links={{2,false},{2,true}}; graph.nodes[1].links={{2,true},{2,false}};
    const auto duplicates=block_original_hierarchy_exit(graph,{0,1},0,{});
    require(duplicates.state.blocked==std::vector<std::pair<std::uint16_t,std::uint16_t>>{{0,1},{0,2},{0,2},{0,2},{0,2}},"retry lost ordered duplicate edges");
    const auto missing=block_original_hierarchy_exit({}, {0,1},0,{});
    require(missing.status==RetryEdgesStatus::NeedsInput && missing.state.blocked.empty(),"unknown retry adjacency changed state");
    require(block_original_hierarchy_exit({},std::vector<std::uint16_t>(501),0,{}).status==RetryEdgesStatus::Unsupported,"oversized hierarchy retry path was accepted");
    require(!block_original_hierarchy_exit({}, {},0,{}).state.allow_hierarchy_38,"empty retry path kept hierarchy enabled");
    std::cout<<"4096 original hierarchy retry cases and four state checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
