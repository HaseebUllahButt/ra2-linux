#pragma once
#include "../map/map_file.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace ra2::sim {
struct SmoothingQuery { ra2map::Cell destination; unsigned direction; int layer_height; };
struct SmoothingCell {
    std::optional<int> entry_status;
    std::optional<std::int8_t> level;
    std::optional<bool> bridge_100,marked_40000;
};
using SmoothingProbe=std::function<std::optional<SmoothingCell>(const SmoothingQuery&)>;
enum class SmoothingStatus { Complete,NeedsInput,Unsupported };
struct SmoothingResult {
    SmoothingStatus status=SmoothingStatus::Unsupported;
    std::vector<unsigned> directions;
    std::string detail;
};
// Base 004290C0, 00429570, 004296F0; expansion 0042B7F0, 0042BCA0, 0042BE20.
// Monotone paths only, no tubes, and explicitly resolved zero actor threat weight.
// Layer inputs hold the original layer at the start of each raw step, before smoothing.
// The probe supplies read-only virtual-entry results and Cell state in original query order.
SmoothingResult smooth_original_monotone_path(ra2map::Cell source,const std::vector<unsigned>& directions,
    const std::vector<std::optional<int>>& layers,std::optional<double> threat_weight,const SmoothingProbe&);
}
