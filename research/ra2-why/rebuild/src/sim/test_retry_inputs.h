#pragma once
#include "retry_edges.h"
namespace ra2::sim::test {
struct RetryFixture {
    HierarchyLayer layer;
    std::vector<std::uint16_t> path;
    std::uint16_t current=0;
    HierarchyRetryEdges state;
    unsigned level=0;
    explicit RetryFixture(unsigned seed) {
        std::uint32_t value=seed^0x96c4a31f;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        const unsigned count=next()%11+2; level=next()%3;
        layer.nodes.resize(count);
        for (auto& node:layer.nodes) {
            const unsigned links=next()%12;
            for (unsigned i=0;i<links;++i) {
                const auto target=static_cast<std::uint16_t>(next()%count);
                node.links.push_back({target,next()%2!=0});
            }
        }
        const auto length=next()%13;
        for (unsigned i=0;i<length;++i) path.push_back(next()%count);
        current=next()%(count+1);
        state.allow_hierarchy_38=next()%2;
        const unsigned initial=next()%4;
        for (unsigned i=0;i<initial;++i) {
            const auto a=static_cast<std::uint16_t>(next()%count);
            const auto b=static_cast<std::uint16_t>(next()%count);
            state.blocked.emplace_back(a,b);
        }
    }
};
inline std::uint32_t retry_digest(const HierarchyRetryEdges& state) {
    std::uint32_t h=(2166136261u^static_cast<unsigned>(state.allow_hierarchy_38))*16777619u;
    h=(h^static_cast<unsigned>(state.blocked.size()))*16777619u;
    for (const auto& edge:state.blocked) h=(h^((static_cast<std::uint32_t>(edge.first)<<16)|edge.second))*16777619u;
    return h;
}
}
