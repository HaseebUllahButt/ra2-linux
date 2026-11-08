#include "hierarchy_route.h"
#include "fp.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace ra2::sim {
namespace {
// Base 007E00C4 / expansion 0082A594. One row per original movement-zone value.
constexpr unsigned compatibility[][8]={
    {1,2,2,2,2,2,2,3},{1,1,2,2,2,2,2,3},{1,1,1,2,2,2,2,3},
    {1,1,1,1,1,1,2,3},{1,1,2,1,1,2,2,3},{1,2,2,1,1,2,2,3},
    {1,1,1,2,2,2,1,3},{1,2,2,2,2,1,2,3},{1,1,1,2,2,1,2,3},
    {1,1,1,1,1,1,1,3},{2,2,2,2,1,2,2,3},{2,2,2,1,1,2,2,3},
    {1,1,1,2,2,2,2,3}
};
// Base 0079C4A4 / expansion 007E3794. Only category values zero through seven are read here.
constexpr float category_cost[]={1,0,0,1,1,0,1,1};
float single_zero(double value) {
    float result=static_cast<float>(value);
    if (static_cast<double>(result)>value) result=std::nextafter(result,0.0f);
    return result;
}
std::uint32_t pair_key(std::uint16_t a,std::uint16_t b) {
    return (static_cast<std::uint32_t>(std::min(a,b))<<16)|std::max(a,b);
}
struct Node { std::size_t parent; std::uint16_t zone; float cost; unsigned depth; };
constexpr auto none=static_cast<std::size_t>(-1);
}
HierarchyRoute find_original_hierarchy_route(const Hierarchy& graph,const HierarchyRouteInputs& input) {
    if ((input.edition!=DriveEdition::BaseRa2 && input.edition!=DriveEdition::YurisRevenge) || input.movement_zone>=(input.edition==DriveEdition::BaseRa2?12u:13u)) throw std::invalid_argument("original hierarchy movement-zone value is unsupported");
    HierarchyRoute result;
    for (unsigned level=0;level<3;++level) {
        const auto& layer=graph.layers[level];
        if (input.source_index>=layer.labels.size() || input.destination_index>=layer.labels.size() || layer.nodes.empty() || layer.nodes.size()>32768)
            throw std::invalid_argument("original hierarchy endpoint or signed node storage is invalid");
        for (const auto label:layer.labels) if (label>=layer.nodes.size()) throw std::invalid_argument("original hierarchy cell label is invalid");
        for (const auto& node:layer.nodes) {
            if (node.category>7 || (level<2 && node.parent>=graph.layers[level+1].nodes.size())) throw std::invalid_argument("original hierarchy node metadata is invalid");
            for (const auto& link:node.links) if (link.node>=layer.nodes.size()) throw std::invalid_argument("original hierarchy neighbor is invalid");
        }
        result.layers[level].allowed.resize(layer.nodes.size());
        result.layers[level].best_costs.resize(layer.nodes.size());
    }
    for (int level=2;level>=0;--level) {
        const auto& layer=graph.layers[level]; auto& output=result.layers[level];
        const auto source=layer.labels[input.source_index],goal=layer.labels[input.destination_index];
        output.allowed[source]=true; output.allowed[goal]=true;
        if (source==goal) { output.path.push_back(source); continue; }
        std::set<std::uint32_t> blocked;
        for (const auto& edge:input.blocked_edges[level]) {
            if (edge.first>=layer.nodes.size() || edge.second>=layer.nodes.size()) throw std::invalid_argument("original hierarchy blocked edge is invalid");
            blocked.insert(pair_key(edge.first,edge.second));
        }
        std::vector<Node> nodes{{none,source,0,0}};
        std::vector<std::size_t> heap{none};
        const auto push=[&](std::size_t index) {
            if (heap.size()>=10000) throw std::length_error("original hierarchy open list exceeds its verified storage");
            heap.push_back(index); auto current=heap.size()-1;
            while (current>1 && nodes[heap[current]].cost<nodes[heap[current/2]].cost) { std::swap(heap[current],heap[current/2]); current/=2; }
        };
        const auto pop=[&]() {
            const auto index=heap.at(1); heap[1]=heap.back(); heap.pop_back();
            std::size_t current=1;
            while (current<heap.size()) {
                auto best=current;
                if (current*2<heap.size() && nodes[heap[current*2]].cost<nodes[heap[best]].cost) best=current*2;
                if (current*2+1<heap.size() && nodes[heap[current*2+1]].cost<nodes[heap[best]].cost) best=current*2+1;
                if (best==current) break;
                std::swap(heap[current],heap[best]); current=best;
            }
            return index;
        };
        push(0); output.best_costs[source]=0;
        std::size_t goal_node=none;
        while (heap.size()>1) {
            const auto current=pop(); const auto node=nodes[current];
            if (node.zone==goal) { goal_node=current; break; }
            for (const auto& link:layer.nodes[node.zone].links) {
                const auto& destination=layer.nodes[link.node];
                const float cost=single_zero(fp::add(fp::add(category_cost[destination.category],node.cost),link.diagonal?0.001:0.0));
                if (output.best_costs[link.node] && !(cost<*output.best_costs[link.node])) continue;
                if (level<2 && !result.layers[level+1].allowed[destination.parent] && destination.category!=1) continue;
                const auto compatible=compatibility[input.movement_zone][destination.category];
                if (compatible!=1) continue;
                if (blocked.count(pair_key(node.zone,link.node))) continue;
                if (nodes.size()>=10000) throw std::length_error("original hierarchy scratch nodes exceed their verified storage");
                nodes.push_back({current,link.node,cost,node.depth+1}); push(nodes.size()-1);
                output.best_costs[link.node]=cost;
            }
        }
        if (goal_node==none) return result;
        if (nodes[goal_node].depth>=500) throw std::length_error("original hierarchy path exceeds its verified output storage");
        for (auto current=goal_node;current!=none;current=nodes[current].parent) {
            output.path.push_back(nodes[current].zone); output.allowed[nodes[current].zone]=true;
        }
        std::reverse(output.path.begin(),output.path.end());
    }
    result.found=true; return result;
}
}
