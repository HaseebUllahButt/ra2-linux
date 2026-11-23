#include "hierarchy_retry.h"
namespace ra2::sim {
namespace {
struct Missing { const char* detail; };
template<class T> T require(const std::optional<T>& value,const char* detail) {
    if (!value) throw Missing{detail};
    return *value;
}
}
HierarchyRetryResult run_original_hierarchy_retry(const Hierarchy& graph,
    const HierarchyRetryInputs& in,const HierarchyRetryState& initial,const HierarchyRetryCallbacks& cb) {
    HierarchyRetryResult out; out.state=initial;
    const auto entry=[&]() {
        const auto cell=require(in.progress_cell,"hierarchy retry progress cell is unresolved");
        out.events.push_back({HierarchyRetryStep::RegionEntry,cell,0});
        if (!cb.region_entry) throw Missing{"hierarchy retry entry lookup is missing"};
        return require(cb.region_entry(cell),"hierarchy retry entry lookup is unresolved");
    };
    const auto region=[&](RetryRegionEntry index,unsigned level) {
        if (!cb.region) throw Missing{"hierarchy retry region table is missing"};
        return require(cb.region(index,level),"hierarchy retry region is unresolved");
    };
    try {
        // Keep the entry identity. Read each level before its reachability call.
        // A prior callback can change later words in this same cached entry.
        const auto cached_entry=entry();
        for (unsigned level=0;level<3;++level) {
            const auto source=region(cached_entry,level);
            const auto cell=require(in.progress_cell,"hierarchy retry progress cell is unresolved");
            out.events.push_back({HierarchyRetryStep::Reachability,cell,level});
            if (!cb.reachability) throw Missing{"original local reachability body is missing"};
            const auto reachable=require(cb.reachability(cell,level),"original local reachability is unresolved");
            if (!reachable.reachable) {
                for (auto candidate=reachable.regions.rbegin();candidate!=reachable.regions.rend();++candidate) {
                    if (*candidate==source) continue;
                    const auto a=source<*candidate?source:*candidate;
                    const auto b=source<*candidate?*candidate:source;
                    out.state.blocked[level].emplace_back(a,b);
                }
            } else {
                // The original re-resolves Search +70 only on this branch.
                const auto current=region(entry(),level);
                const auto selected=block_original_hierarchy_exit(graph.layers[level],in.paths[level],current,
                    {out.state.allow_hierarchy_38,out.state.blocked[level]});
                if (selected.status!=RetryEdgesStatus::Complete) {
                    out.status=selected.status; out.detail=selected.detail; return out;
                }
                out.state.allow_hierarchy_38=selected.state.allow_hierarchy_38;
                out.state.blocked[level]=selected.state.blocked;
            }
        }
        out.status=RetryEdgesStatus::Complete;
    } catch (const Missing& missing) { out.detail=missing.detail; }
    return out;
}
}
