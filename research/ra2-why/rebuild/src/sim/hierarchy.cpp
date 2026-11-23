#include "hierarchy.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Edge { std::uint16_t neighbor,current; bool diagonal; };
class Builder {
    int stride_;
    const std::vector<HierarchyCell>& cells_;
    Hierarchy out_;
    HierarchyLayer* layer_=nullptr;
    std::array<std::vector<Edge>,256> buckets_;
    int minimum_x_=0,minimum_y_=0,maximum_x_=0,maximum_y_=0;
    unsigned depth_=0;
    std::size_t at(int x,int y) const { return static_cast<std::size_t>(y*stride_+x); }
    std::uint16_t label(int x,int y) const { return layer_->labels.at(at(x,y)); }
    std::uint16_t component(int x,int y) const { return out_.coarse.labels.at(at(x,y)); }
    int height(int x,int y) const { return cells_.at(at(x,y)).coarse.height.value_or(0); }
    bool close(int x,int y,int reference_x,int reference_y) const { return std::abs(height(x,y)-height(reference_x,reference_y))<=1; }
    bool usable(int x,int y) const {
        if (x<0 || y<0 || x>=stride_ || y>=stride_) throw std::out_of_range("original hierarchy usable query leaves its verified grid");
        const auto value=cells_.at(at(x,y)).usable;
        if (!value) throw std::invalid_argument("original hierarchy usable-area input is unresolved");
        return *value;
    }
    void edge(std::uint16_t neighbor,std::uint16_t current,bool diagonal,int& last) {
        auto& bucket=buckets_[((neighbor&15u)<<4)|(current&15u)];
        const auto same=[&](const Edge& value) { return value.neighbor==neighbor && value.current==current; };
        if (std::find_if(bucket.begin(),bucket.end(),same)==bucket.end()) bucket.push_back({neighbor,current,diagonal});
        last=neighbor;
    }
    int fill(int x,int y,std::uint16_t id) {
        if (++depth_>4096) throw std::length_error("original hierarchy recursion exceeds the verified native stack domain");
        const auto region=component(x,y);
        int left=x,right=x,last=-1,previous_height=height(x,y);
        while (component(left,y)==region && left>=minimum_x_ && std::abs(height(left,y)-previous_height)<=1) {
            layer_->labels.at(at(left,y))=id; previous_height=height(left,y); --left;
        }
        auto neighbor=label(left,y);
        if (neighbor>0 && neighbor<32768 && std::abs(height(left,y)-previous_height)<=1 && usable(left,y) && usable(left+1,y)) edge(neighbor,id,false,last);
        previous_height=height(x,y);
        while (component(right,y)==region && right<=maximum_x_ && std::abs(height(right,y)-previous_height)<=1) {
            layer_->labels.at(at(right,y))=id; previous_height=height(right,y); ++right;
        }
        neighbor=label(right,y);
        if (neighbor>0 && neighbor<32768 && neighbor!=last && std::abs(height(right,y)-previous_height)<=1 && usable(right,y) && usable(right-1,y)) edge(neighbor,id,false,last);
        const int skip=right-x-1;
        for (int side=-1;side<=1;side+=2) {
            const int adjacent_y=y+side;
            for (int current=left;current<=right;) {
                const int reference_x=current==left?current+1:(current<right?current:current-1);
                neighbor=label(current,adjacent_y);
                const bool inside=current>=minimum_x_ && current<=maximum_x_;
                const bool inside_y=side<0?y>minimum_y_:y<maximum_y_;
                if (!neighbor && inside && inside_y) {
                    if (component(current,adjacent_y)==region && close(current,adjacent_y,reference_x,y)) {
                        fill(current,adjacent_y,id); // Retry this cell. Its newly written label advances the next iteration.
                        continue;
                    }
                } else if (neighbor && neighbor!=id && neighbor!=last && close(current,adjacent_y,reference_x,y) && usable(reference_x,y) && usable(current,adjacent_y)) {
                    edge(neighbor,id,!inside,last);
                }
                ++current;
            }
        }
        --depth_; return skip;
    }
public:
    Builder(unsigned stride,const std::vector<HierarchyCell>& input):stride_(static_cast<int>(stride)),cells_(input) {
        std::vector<CoarseRegionCell> coarse;
        for (const auto& cell:input) coarse.push_back(cell.coarse);
        out_.coarse=build_original_coarse_regions(stride,coarse);
    }
    Hierarchy run() {
        for (int level=2;level>=0;--level) {
            layer_=&out_.layers[level]; layer_->labels.resize(cells_.size()); layer_->nodes.push_back({});
            for (auto& bucket:buckets_) bucket.clear();
            const int block=1<<(level+1);
            for (int i=0;i<static_cast<int>(cells_.size());) {
                if (*cells_[i].coarse.category==7 || layer_->labels[i]) { ++i; continue; }
                if (layer_->nodes.size()>=32768) throw std::length_error("original hierarchy signed node range is unsupported");
                const int x=i%stride_,y=i/stride_;
                minimum_x_=x-(x&(block-1)); maximum_x_=minimum_x_+block-1;
                minimum_y_=y-(y&(block-1)); maximum_y_=minimum_y_+block-1;
                const auto id=static_cast<std::uint16_t>(layer_->nodes.size());
                const auto skip=fill(x,y,id);
                HierarchyNode node;
                node.category=*cells_[i].coarse.category;
                node.parent=level<2?out_.layers[level+1].labels[i]:0;
                node.center_index=x/4+0x83+(y/4)*0x82;
                layer_->nodes.push_back(std::move(node));
                i+=skip;
            }
            for (const auto& bucket:buckets_) for (const auto& link:bucket) {
                layer_->nodes.at(link.current).links.push_back({link.neighbor,link.diagonal});
                layer_->nodes.at(link.neighbor).links.push_back({link.current,link.diagonal});
            }
        }
        return std::move(out_);
    }
};
}
Hierarchy build_original_hierarchy(unsigned stride,const std::vector<HierarchyCell>& cells) { return Builder(stride,cells).run(); }
}
