#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
namespace ra2::sim {
// Raw Map +68 records produced by Cell setup: category byte, height byte, then region word.
// These categories are not LandType. The caller must resolve the original Cell +4C cache first.
struct CoarseRegionCell { std::optional<std::uint8_t> category,height; };
struct CoarseRegions {
    std::vector<std::uint16_t> labels;
    std::vector<std::uint8_t> categories; // Index zero is the original blocked dummy category seven.
    std::vector<int> reported_sizes;
    // Original bucket order, preserving the first discovered undirected edge.
    std::vector<std::pair<std::uint16_t,std::uint16_t>> edges;
    std::uint32_t largest=0xffffffffu;
};
// Initial component pass at base 0054F250 / Yuri 0056CB90, driven by 0056C510's row scan.
// Requires the original category-seven border. Tube edge additions and zone tables are separate.
CoarseRegions build_original_coarse_regions(unsigned stride,const std::vector<CoarseRegionCell>&);
}
