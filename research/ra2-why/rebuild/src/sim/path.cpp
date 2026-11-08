#include "path.h"
#include "path_smoothing.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
namespace ra2::sim {
namespace {
constexpr int offsets[]={-512,-511,1,513,512,511,-1,-513}; // 0079C484 / 007E3774.
constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
// Base 007CF0DC / expansion 0081872C. These biases break directional ties.
constexpr float bias[]={0.001f,0.005f,0.002f,0.006f,0.003f,0.007f,0.004f,0.008f};
float single(double value) {
    float result=static_cast<float>(value);
    if (static_cast<double>(result)>value) result=std::nextafter(result,0.0f);
    return result;
}
int key(ra2map::Cell cell) { return cell.y*512+cell.x; }
bool same(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
struct Node { std::size_t cell,parent; float cost,priority; unsigned depth; };
constexpr std::size_t none=static_cast<std::size_t>(-1);
}
PathResult find_original_ground_path(ra2map::Cell source,ra2map::Cell goal,
    const std::vector<ResolvedGroundPathCell>& cells,const OriginalDistance& distance,int limit,const PathHierarchyFilter* filter,const PathPostprocessing* postprocessing) {
    PathResult result; result.end=source;
    std::map<int,std::size_t> lookup;
    for (std::size_t i=0;i<cells.size();++i) {
        if (cells[i].cell.x>511 || cells[i].cell.y>511 || !lookup.emplace(key(cells[i].cell),i).second)
            throw std::invalid_argument("original search needs unique cells within the 512-square map array");
    }
    if (source.x>511 || source.y>511 || goal.x>511 || goal.y>511)
        throw std::invalid_argument("original search endpoint exceeds the map array");
    if (!lookup.count(key(source)) || !lookup.count(key(goal)) || same(source,goal)) {
        result.evidence="00429A90: missing endpoint or identical flat endpoints"; return result;
    }
    const auto& start_height=cells[lookup.at(key(source))].height;
    const auto& goal_height=cells[lookup.at(key(goal))].height;
    if (!start_height || !goal_height) {
        result.status=PathStatus::NeedsInput; result.evidence="00429A90: endpoint height is unresolved"; return result;
    }
    if (start_height->flag_100 || goal_height->flag_100) {
        result.status=PathStatus::Unsupported; result.evidence="00429A90: bridge path states are not recovered"; return result;
    }
    if (limit<0) limit=0xfff7; // Original default, not a native gameplay value.
    std::vector<Node> nodes; nodes.reserve(std::min<std::size_t>(cells.size(),65536));
    std::vector<bool> seen(cells.size());
    const auto create=[&](std::size_t cell,std::size_t parent,float step) {
        const auto at=cells[cell].cell;
        const int dx=static_cast<int>(at.x)-goal.x,dy=static_cast<int>(at.y)-goal.y;
        const float cost=parent==none?0:single(static_cast<double>(nodes[parent].cost)+step);
        const float root=distance.square_root(static_cast<double>(dx*dx+dy*dy));
        nodes.push_back({cell,parent,cost,single(static_cast<double>(cost)+root),parent==none?1:nodes[parent].depth+1});
        return nodes.size()-1;
    };
    std::vector<std::size_t> heap(1,none); // Original open list is one-based.
    const auto lower=[&](std::size_t a,std::size_t b) { return nodes[a].priority<nodes[b].priority; };
    const auto push=[&](std::size_t node) {
        heap.push_back(node); std::size_t at=heap.size()-1;
        while (at>1 && lower(heap[at],heap[at/2])) { std::swap(heap[at],heap[at/2]); at/=2; }
    };
    const auto sift=[&]() {
        std::size_t at=1;
        for (;;) {
            auto best=at;
            if (at*2<heap.size() && lower(heap[at*2],heap[best])) best=at*2;
            if (at*2+1<heap.size() && lower(heap[at*2+1],heap[best])) best=at*2+1;
            if (best==at) break;
            std::swap(heap[at],heap[best]); at=best;
        }
    };
    auto current=create(lookup.at(key(source)),none,0);
    seen[nodes[current].cell]=true;
    bool stopped_at_blocked_goal=false;
    while (current!=none && result.expansions<limit) {
        const auto cell=cells[nodes[current].cell].cell;
        if (same(cell,goal)) break;
        std::size_t last=none;
        for (unsigned direction=0;direction<8;++direction) {
            const auto found=lookup.find(key(cell)+offsets[direction]);
            if (found==lookup.end()) continue;
            const auto next=found->second;
            if (filter) {
                const auto zone=cells[next].hierarchy_zone;
                if (!zone || *zone>=filter->allowed_zones.size()) {
                    result.status=PathStatus::NeedsInput; result.evidence="00429A90: hierarchy zone or allowed-zone mark is unresolved"; return result;
                }
                if (!filter->allowed_zones[*zone]) {
                    if (!cells[next].occupancy_neighbors) {
                        result.status=PathStatus::NeedsInput; result.evidence="00429A90: Cell +122 outside the hierarchy route is unresolved"; return result;
                    }
                    if (!*cells[next].occupancy_neighbors) continue;
                }
            }
            // The original marks nodes when generated and never decreases their stored cost.
            // For the side-effect-free resolved 0/7 adapter, an existing mark cannot add a node.
            if (seen[next]) continue;
            if (!cells[next].height) {
                result.status=PathStatus::NeedsInput; result.evidence="00429A90: neighbor height is unresolved"; return result;
            }
            if (cells[next].height->flag_100) {
                result.status=PathStatus::Unsupported; result.evidence="00429A90: bridge path states are not recovered"; return result;
            }
            HeightEntryInputs height; height.direction=static_cast<int>(direction);
            height.source=cells[nodes[current].cell].height; height.destination=*cells[next].height;
            height.layer_height=height.source->level;
            int entry=original_height_entry(height).status;
            if (entry!=7) {
                if (!cells[next].entry_status) {
                    result.status=PathStatus::NeedsInput; result.evidence="00429F04: unresolved CanEnterCell result at "+std::to_string(cells[next].cell.x)+","+std::to_string(cells[next].cell.y); return result;
                }
                entry=*cells[next].entry_status;
            }
            if (entry!=0 && entry!=7) {
                result.status=PathStatus::Unsupported; result.evidence="00429830: cell-entry cost requires unrecovered occupant inputs"; return result;
            }
            if (entry==0) {
                if (postprocessing && !cells[next].marked_40000) {
                    result.status=PathStatus::NeedsInput; result.evidence="00429830: regular-search marked-cell input is unresolved"; return result;
                }
                if (cells[next].marked_40000.value_or(false)) {
                    result.status=PathStatus::Unsupported; result.evidence="00429830: marked-cell regular-search cost is not recovered"; return result;
                }
            }
            if (entry==7) {
                if (same(cells[next].cell,goal) && std::abs(static_cast<int>(height.source->level)-goal_height->level)<2) { stopped_at_blocked_goal=true; break; }
                continue;
            }
            if (nodes.size()==65536) {
                result.status=PathStatus::Unsupported; result.evidence="0042A460: original node pool limit"; return result;
            }
            const auto candidate=create(next,current,single(1.0+static_cast<double>(bias[direction])));
            if (last!=none) push(last);
            last=candidate; seen[next]=true;
        }
        if (stopped_at_blocked_goal) break;
        if (last==none) {
            if (heap.size()==1) current=none;
            else {
                current=heap[1]; heap[1]=heap.back(); heap.pop_back();
                if (heap.size()>1) sift();
            }
        } else {
            current=last;
            // Original keeps the last generated child outside the heap, then compares its root.
            if (heap.size()>1 && nodes[heap[1]].priority<=nodes[last].priority) {
                current=heap[1]; heap[1]=last; sift();
            }
        }
        ++result.expansions;
    }
    if (current==none || result.expansions==limit || result.expansions==10000 || nodes[current].depth<=1) {
        result.evidence="0042A3DE: no reconstructable path or expansion limit"; return result;
    }
    result.end=cells[nodes[current].cell].cell; result.cost=nodes[current].cost;
    bool x_positive=false,x_negative=false,y_positive=false,y_negative=false;
    for (auto node=current;nodes[node].parent!=none;node=nodes[node].parent) {
        const auto end=cells[nodes[node].cell].cell,begin=cells[nodes[nodes[node].parent].cell].cell;
        const int dx=static_cast<int>(end.x)-begin.x,dy=static_cast<int>(end.y)-begin.y;
        unsigned direction=0;
        while (direction<8 && (directions[direction][0]!=dx || directions[direction][1]!=dy)) ++direction;
        if (direction==8) { result.status=PathStatus::Unsupported; result.evidence="0042AA90: nonadjacent path link"; return result; }
        result.directions.push_back(direction);
        x_positive|=dx>0; x_negative|=dx<0; y_positive|=dy>0; y_negative|=dy<0;
    }
    std::reverse(result.directions.begin(),result.directions.end());
    const auto direct_steps=static_cast<std::size_t>(std::max(std::abs(static_cast<int>(result.end.x)-source.x),std::abs(static_cast<int>(result.end.y)-source.y)));
    if ((x_positive && x_negative) || (y_positive && y_negative) || result.directions.size()>direct_steps) {
        if (!postprocessing) {
            result.status=PathStatus::Unsupported; result.evidence="0042B210/0042B7F0: path postprocessing needs explicit actor and cell inputs"; return result;
        }
        std::vector<std::optional<int>> layers;
        auto at=source;
        for (const auto direction:result.directions) {
            layers.push_back(cells[lookup.at(key(at))].height->level);
            at={static_cast<std::uint16_t>(at.x+directions[direction][0]),static_cast<std::uint16_t>(at.y+directions[direction][1])};
        }
        bool unsupported_bridge=false;
        const auto probe=[&](const SmoothingQuery& query)->std::optional<SmoothingCell> {
            const auto destination=lookup.find(key(query.destination));
            const auto previous=lookup.find(key(query.destination)-offsets[query.direction]);
            if (destination==lookup.end() || previous==lookup.end()) return {};
            const auto& target=cells[destination->second]; const auto& origin=cells[previous->second];
            if (!target.height || !origin.height) return {};
            if (target.height->flag_100 || origin.height->flag_100) { unsupported_bridge=true; return {}; }
            HeightEntryInputs height;
            height.destination=*target.height; height.source=origin.height;
            height.direction=static_cast<int>(query.direction); height.layer_height=query.layer_height;
            const auto status=original_height_entry(height).status;
            const auto entry=status==7?std::optional<int>(7):target.entry_status;
            return SmoothingCell{entry,target.height->level,target.height->flag_100,target.marked_40000};
        };
        const auto metadata=[&](ra2map::Cell cell)->std::optional<SmoothingCell> {
            const auto found=lookup.find(key(cell));
            if (found==lookup.end() || !cells[found->second].height) return {};
            const auto& value=cells[found->second];
            return SmoothingCell{std::nullopt,value.height->level,value.height->flag_100,value.marked_40000};
        };
        const auto smoothed=smooth_original_path(source,result.directions,layers,postprocessing->actor_threat_weight,probe,metadata);
        if (smoothed.status!=SmoothingStatus::Complete) {
            result.status=unsupported_bridge || smoothed.status==SmoothingStatus::Unsupported?PathStatus::Unsupported:PathStatus::NeedsInput;
            result.evidence=unsupported_bridge?"0042BE20: shortcut bridge entry state is not recovered":smoothed.detail; return result;
        }
        result.directions=smoothed.directions;
        result.evidence="00429A90/0042B7F0: original regular search and full path postprocessing with resolved zero threat";
    } else result.evidence="00429A90/0042A460: resolved regular search and unchanged shortest monotone postprocessing";
    result.status=stopped_at_blocked_goal?PathStatus::Partial:PathStatus::Found;
    return result;
}
PathResult find_original_flat_path(ra2map::Cell source,ra2map::Cell goal,
    const std::vector<ResolvedFlatPathCell>& cells,const OriginalDistance& distance,int limit) {
    std::vector<ResolvedGroundPathCell> ground; ground.reserve(cells.size());
    for (const auto& cell:cells) ground.push_back({cell.cell,cell.entry_status,EntryHeightCell{}});
    return find_original_ground_path(source,goal,ground,distance,limit);
}

}
