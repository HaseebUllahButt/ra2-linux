#include "movement_regions.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace ra2::sim {
MovementRegions build_original_movement_regions(const CoarseRegions& graph,DriveEdition edition) {
    const auto zone_count=original_movement_zone_count(edition);
    const auto count=graph.categories.size();
    if (!count || count>65535 || graph.categories[0]!=7) throw std::invalid_argument("original region table needs its blocked dummy and word-sized identities");
    for (const auto category:graph.categories) if (category>7) throw std::invalid_argument("original region category is invalid");
    MovementRegions result; result.edition=edition; result.neighbors.resize(count);
    std::set<std::pair<std::uint16_t,std::uint16_t>> pairs;
    for (const auto& edge:graph.edges) {
        if (edge.first>=edge.second || edge.second>=count || !pairs.insert(edge).second) throw std::invalid_argument("original ordered region pair is invalid or duplicated");
        result.neighbors[edge.second].push_back(edge.first);
        result.neighbors[edge.first].push_back(edge.second);
    }
    result.components.resize(zone_count);
    for (unsigned zone=0;zone<zone_count;++zone) {
        auto& components=result.components[zone]; components.resize(count);
        for (std::size_t i=0;i<count;++i) components[i]=original_movement_compatibility(edition,zone,graph.categories[i])!=1;
        std::uint32_t next=2;
        for (std::size_t i=0;i<count;++i) if (!components[i]) {
            if (next>65535) throw std::length_error("original movement-component word overflow is unsupported");
            std::vector<std::uint16_t> stack{static_cast<std::uint16_t>(i)};
            components[i]=static_cast<std::uint16_t>(next);
            const auto mode=original_movement_compatibility(edition,zone,graph.categories[i]);
            while (!stack.empty()) {
                const auto current=stack.back(); stack.pop_back();
                const auto& neighbors=result.neighbors[current];
                for (auto at=neighbors.rbegin();at!=neighbors.rend();++at)
                    if (!components[*at] && original_movement_compatibility(edition,zone,graph.categories[*at])==mode) {
                        components[*at]=static_cast<std::uint16_t>(next); stack.push_back(*at);
                    }
            }
            ++next;
        }
        components[0]=0xffff;
    }
    return result;
}
std::uint16_t original_ground_component(const CoarseRegions& graph,const MovementRegions& movement,unsigned stride,
    unsigned zone,std::int16_t x,std::int16_t y) {
    const auto count=original_movement_zone_count(movement.edition);
    if (zone>=count || movement.components.size()!=count || stride<3 || stride>512 || graph.labels.size()!=static_cast<std::size_t>(stride)*stride)
        throw std::invalid_argument("original ground-component query needs a valid zone and square grid");
    const auto& components=movement.components[zone];
    if (components.size()!=graph.categories.size()) throw std::invalid_argument("original ground-component table has the wrong size");
    const auto linear=static_cast<std::int64_t>(stride)*y+x;
    const auto index=static_cast<std::size_t>(std::clamp<std::int64_t>(linear,0,graph.labels.size()-1));
    const auto region=graph.labels[index];
    if (region>=components.size()) throw std::invalid_argument("original ground-component raw label is invalid");
    return components[region];
}
}
