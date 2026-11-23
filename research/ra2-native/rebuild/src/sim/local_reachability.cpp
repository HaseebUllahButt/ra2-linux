#include "local_reachability.h"
#include "movement_zones.h"
#include <algorithm>
namespace ra2::sim {
namespace {
struct Missing { const char* text; };
template<class T> T require(const std::optional<T>& value,const char* text) {
    if (!value) throw Missing{text};
    return *value;
}
}
LocalReachabilityResult original_local_reachability(const LocalReachabilityInputs& in,
    const LocalReachabilityCallbacks& cb) {
    LocalReachabilityResult out; out.regions=in.initial_regions;
    if (in.level>=3 || (in.edition!=DriveEdition::BaseRa2 && in.edition!=DriveEdition::YurisRevenge)) {
        out.status=RetryEdgesStatus::Unsupported; out.detail="local reachability level or edition is outside the original hierarchy"; return out;
    }
    const unsigned size=1u<<(in.level+1),mask=size-1;
    const auto coordinates=[&](ReachabilityCell cell) {
        if (!cb.coordinates) throw Missing{"local reachability coordinates callback is missing"};
        return require(cb.coordinates(cell),"local reachability coordinates are unresolved");
    };
    const auto region=[&](ReachabilityCell cell) {
        if (!cb.region) throw Missing{"local reachability region callback is missing"};
        return require(cb.region(cell,in.level),"local reachability region is unresolved");
    };
    const auto slot=[&](ra2map::Cell cell) { return (cell.x&mask)*8+(cell.y&mask); };
    try {
        const auto source_region=region(in.source);
        out.visited[slot(coordinates(in.source))]=true;
        const auto zone=require(in.movement_zone,"local reachability movement zone is unresolved");
        if (zone>=original_movement_zone_count(in.edition)) {
            out.status=RetryEdgesStatus::Unsupported; out.detail="local reachability movement zone is invalid"; return out;
        }
        std::vector<ReachabilityCell> pending{in.source};
        std::vector<std::uint16_t> exits;
        std::uint16_t last_exit=0;
        while (!pending.empty()) {
            const auto current=pending.back(); pending.pop_back();
            for (unsigned direction=0;direction<8;++direction) {
                if (!cb.neighbor) throw Missing{"local reachability neighbor callback is missing"};
                const auto next=require(cb.neighbor(current,direction),"local reachability neighbor is unresolved");
                const auto visited_slot=slot(coordinates(next));
                const auto next_region=region(next);
                if (!cb.height) throw Missing{"local reachability height callback is missing"};
                const auto height=require(cb.height(next),"local reachability height is unresolved");
                if (!cb.entry) throw Missing{"original local cell entry callback is missing"};
                const auto entry=require(cb.entry(next,direction,height),"original local cell entry result is unresolved");
                if (entry!=0) {
                    if (!cb.category) throw Missing{"local reachability category callback is missing"};
                    const auto category=require(cb.category(next),"local reachability Cell +4C is unresolved");
                    if (category>7) {
                        out.status=RetryEdgesStatus::Unsupported; out.detail="local reachability category is outside 0..7"; return out;
                    }
                    if (original_movement_compatibility(in.edition,zone,category)==1) continue;
                }
                if (next_region==source_region) {
                    if (!out.visited[visited_slot]) { pending.push_back(next); out.visited[visited_slot]=true; }
                } else if (next_region!=last_exit && next_region!=0) {
                    last_exit=next_region;
                    if (std::find(exits.begin(),exits.end(),next_region)==exits.end()) exits.push_back(next_region);
                }
            }
        }
        const auto source=coordinates(in.source);
        // The scan in base 00565EB0 / expansion 00584395..0058444D repeats this
        // coordinate. The loop indexes select visited bytes, not query coordinates.
        const ra2map::Cell query{static_cast<std::uint16_t>(source.x+(source.x&mask)),
                                static_cast<std::uint16_t>(source.y+(source.y&mask))};
        for (unsigned x=0;x<size;++x) for (unsigned y=0;y<size;++y) {
            if (!cb.usable) throw Missing{"local reachability usable-area callback is missing"};
            if (!require(cb.usable(query),"local reachability usable-area result is unresolved")) continue;
            if (!cb.scan_region) throw Missing{"local reachability scan region callback is missing"};
            if (require(cb.scan_region(query,in.level),"local reachability scan region is unresolved")==source_region && !out.visited[x*8+y]) {
                out.reachable=true; out.status=RetryEdgesStatus::Complete; return out;
            }
        }
        if (!cb.links) throw Missing{"local reachability hierarchy links callback is missing"};
        const auto links=require(cb.links(source_region,in.level),"local reachability hierarchy links are unresolved");
        for (auto link=links.rbegin();link!=links.rend();++link)
            if (std::find(exits.begin(),exits.end(),*link)==exits.end()) out.regions.push_back(*link);
        out.status=RetryEdgesStatus::Complete;
    } catch (const Missing& missing) { out.detail=missing.text; }
    return out;
}
}
