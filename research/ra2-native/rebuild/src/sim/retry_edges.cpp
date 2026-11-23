#include "retry_edges.h"
#include <algorithm>
namespace ra2::sim {
RetryEdgesResult block_original_hierarchy_exit(const HierarchyLayer& layer,
    const std::vector<std::uint16_t>& path,std::uint16_t current,
    const HierarchyRetryEdges& initial) {
    RetryEdgesResult out; out.state=initial;
    if (path.size()>500) {
        out.status=RetryEdgesStatus::Unsupported;
        out.detail="hierarchy retry path exceeds the original 500-word buffer"; return out;
    }
    const auto at=std::find(path.begin(),path.end(),current);
    if (path.size()<2 || at==path.end()) {
        out.state.allow_hierarchy_38=false;
        out.status=RetryEdgesStatus::Complete; return out;
    }
    const auto last=at+1==path.end();
    const auto a=last?*(at-1):*at;
    const auto b=last?*at:*(at+1);
    if (a>=layer.nodes.size() || b>=layer.nodes.size()) {
        out.detail="hierarchy retry endpoint adjacency is unresolved"; return out;
    }
    const auto append=[&](std::uint16_t first,std::uint16_t second) {
        if (second<first) std::swap(first,second);
        out.state.blocked.emplace_back(first,second);
    };
    // Original 0042B0F0 / 0042D830 appends without a duplicate check.
    if (a!=b) append(a,b);
    const auto& from=layer.nodes[a].links;
    const auto& to=layer.nodes[b].links;
    for (auto candidate=to.rbegin();candidate!=to.rend();++candidate) {
        if (candidate->node==a) continue;
        for (auto common=from.rbegin();common!=from.rend();++common)
            if (common->node==candidate->node) append(a,candidate->node);
    }
    out.status=RetryEdgesStatus::Complete; return out;
}
}
