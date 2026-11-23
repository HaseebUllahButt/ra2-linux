#pragma once
#include "hierarchy_retry.h"
#include "test_retry_inputs.h"
namespace ra2::sim::test {
struct HierarchyRetryFixture {
    Hierarchy graph;
    HierarchyRetryInputs inputs;
    HierarchyRetryState state;
    HierarchyRetryCallbacks callbacks;
    std::array<std::array<std::uint16_t,3>,2> regions;
    std::array<RetryReachability,3> reachable;
    std::array<bool,3> mutate;
    explicit HierarchyRetryFixture(unsigned seed) {
        std::uint32_t value=seed^0xa39265c1;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        inputs.progress_cell=ra2map::Cell{54,69};
        for (unsigned level=0;level<3;++level) {
            RetryFixture fixture(seed*3+level);
            graph.layers[level]=fixture.layer; inputs.paths[level]=fixture.path;
            state.blocked[level]=fixture.state.blocked;
            if (!level) state.allow_hierarchy_38=fixture.state.allow_hierarchy_38;
            regions[0][level]=fixture.current;
            regions[1][level]=(fixture.current+1)%(fixture.layer.nodes.size()+1);
            reachable[level].reachable=next()%2;
            const unsigned count=next()%8;
            for (unsigned i=0;i<count;++i) reachable[level].regions.push_back(next()%(fixture.layer.nodes.size()+1));
            mutate[level]=next()%2;
        }
        callbacks.region_entry=[](ra2map::Cell cell)->std::optional<RetryRegionEntry> { return cell.x&1; };
        callbacks.region=[&](RetryRegionEntry entry,unsigned level)->std::optional<std::uint16_t> { return regions.at(entry).at(level); };
        callbacks.reachability=[&](ra2map::Cell,unsigned level)->std::optional<RetryReachability> {
            if (mutate[level]) {
                inputs.progress_cell->x^=1;
                const auto other=(level+1)%3;
                regions[0][other]=(regions[0][other]+1)%(graph.layers[other].nodes.size()+1);
            }
            return reachable[level];
        };
    }
    std::uint32_t digest(const HierarchyRetryResult& out) const {
        std::uint32_t h=(2166136261u^static_cast<unsigned>(out.status))*16777619u;
        const auto mix=[&](std::uint32_t value) { h=(h^value)*16777619u; };
        mix(out.state.allow_hierarchy_38);
        for (const auto& blocked:out.state.blocked) {
            mix(blocked.size());
            for (const auto& edge:blocked) mix((static_cast<std::uint32_t>(edge.first)<<16)|edge.second);
        }
        for (const auto& event:out.events) {
            mix(static_cast<unsigned>(event.step)); mix(event.cell.x); mix(event.cell.y); mix(event.level);
        }
        mix(inputs.progress_cell->x); mix(inputs.progress_cell->y);
        for (const auto& row:regions) for (auto value:row) mix(value);
        return h;
    }
};
}
