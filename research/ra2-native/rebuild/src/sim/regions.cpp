#include "regions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ra2::sim {
namespace {
struct Cell { std::uint8_t category,height; };
struct Span { int count,skip; };
class Builder {
    int stride_;
    std::vector<Cell> cells_;
    CoarseRegions out_;
    std::array<std::vector<std::pair<std::uint16_t,std::uint16_t>>,256> buckets_;
    std::uint16_t last_=0;
    unsigned depth_=0;
    const Cell& cell(int i) const { return cells_.at(static_cast<std::size_t>(i)); }
    std::uint16_t label(int i) const { return out_.labels.at(static_cast<std::size_t>(i)); }
    int difference(int a,int b) const { return std::abs(static_cast<int>(cell(a).height)-cell(b).height); }
    void edge(std::uint16_t neighbor,std::uint16_t current) {
        if (!neighbor || neighbor==last_ || neighbor==current) return;
        auto& bucket=buckets_[((neighbor&15u)<<4)|(current&15u)];
        const auto pair=std::make_pair(neighbor,current);
        if (std::find(bucket.begin(),bucket.end(),pair)==bucket.end()) bucket.push_back(pair);
        last_=neighbor;
    }
    Span fill(int seed,std::uint16_t id) {
        if (++depth_>4096) throw std::length_error("original region recursion exceeds the verified native stack domain");
        const auto category=cell(seed).category;
        const bool height_independent_edges=category==6;
        int previous_height=cell(seed).height,left=seed,right=seed;
        // Original left and right tests differ. Do not replace this with a generic flood fill.
        while (cell(left).category==category && std::abs(static_cast<int>(cell(left).height)-previous_height)<=1) {
            out_.labels.at(static_cast<std::size_t>(left))=id; previous_height=cell(left).height; --left;
        }
        if (label(left) && (std::abs(static_cast<int>(cell(left).height)-previous_height)<=1 || height_independent_edges)) edge(label(left),id);
        while (cell(right).category==category && std::abs(static_cast<int>(cell(right).height)-previous_height)<4) {
            out_.labels.at(static_cast<std::size_t>(right))=id; previous_height=cell(right).height; ++right;
        }
        if (label(right) && (std::abs(static_cast<int>(cell(right).height)-previous_height)<=1 || height_independent_edges)) edge(label(right),id);
        Span result{right-left-1,right-seed-1};
        int upper=left-stride_,upper_end=right-stride_;
        while (upper<=upper_end) {
            const int reference=upper+stride_+(upper<upper_end-1?1:(upper==upper_end-1?0:-1));
            const auto existing=label(upper);
            if (existing) {
                if (difference(upper,reference)<=1 || height_independent_edges) edge(existing,id);
                ++upper;
            } else if (cell(upper).category==category && difference(upper,reference)<=1) {
                const auto nested=fill(upper,id); result.count+=nested.count; upper+=nested.skip;
            } else ++upper;
        }
        int lower=left+stride_,lower_end=right+stride_;
        while (lower<=lower_end) {
            const int reference=lower-stride_+(lower<lower_end-1?1:(lower==lower_end-1?0:-1));
            const auto existing=label(lower);
            if (existing) {
                if (difference(lower,reference)<=1 || height_independent_edges) edge(existing,id);
                ++lower;
            } else if (cell(lower).category==category && difference(lower,reference)<=1) {
                const auto nested=fill(lower,id); result.count+=nested.count; lower+=nested.skip;
            } else ++lower;
        }
        --depth_; return result;
    }
public:
    Builder(unsigned stride,const std::vector<CoarseRegionCell>& input):stride_(static_cast<int>(stride)) {
        if (stride<3 || stride>512 || input.size()!=static_cast<std::size_t>(stride)*stride)
            throw std::invalid_argument("original region grid needs a 3..512 square stride");
        for (std::size_t i=0;i<input.size();++i) {
            const auto& value=input[i];
            if (!value.category || *value.category>7 || (!value.height && *value.category!=7))
                throw std::invalid_argument("original coarse cell category or height is unresolved");
            if ((i<stride || i>=input.size()-stride || i%stride==0 || i%stride==stride-1) && *value.category!=7)
                throw std::invalid_argument("original coarse grid requires a category-seven border");
            cells_.push_back({*value.category,value.height.value_or(0)});
        }
        out_.labels.resize(input.size()); out_.categories.push_back(7); out_.reported_sizes.push_back(0);
    }
    CoarseRegions run() {
        int largest_size=-1;
        for (int i=0;i<static_cast<int>(cells_.size());) {
            if (cell(i).category==7 || label(i)) { ++i; continue; }
            if (out_.categories.size()>=65535) throw std::length_error("original coarse region word overflow is unsupported");
            const auto id=static_cast<std::uint16_t>(out_.categories.size());
            out_.categories.push_back(cell(i).category); last_=0;
            const auto span=fill(i,id); out_.reported_sizes.push_back(span.count);
            if (largest_size<span.count) { largest_size=span.count; out_.largest=id; }
            i+=span.skip;
        }
        for (const auto& bucket:buckets_) out_.edges.insert(out_.edges.end(),bucket.begin(),bucket.end());
        return std::move(out_);
    }
};
}
CoarseRegions build_original_coarse_regions(unsigned stride,const std::vector<CoarseRegionCell>& cells) {
    return Builder(stride,cells).run();
}
}
