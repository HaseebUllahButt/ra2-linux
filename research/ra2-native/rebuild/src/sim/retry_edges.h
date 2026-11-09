#pragma once
#include "hierarchy.h"
#include <string>
namespace ra2::sim {
struct HierarchyRetryEdges {
    bool allow_hierarchy_38=true;
    std::vector<std::pair<std::uint16_t,std::uint16_t>> blocked;
};
enum class RetryEdgesStatus { Complete, NeedsInput, Unsupported };
struct RetryEdgesResult {
    RetryEdgesStatus status=RetryEdgesStatus::NeedsInput;
    HierarchyRetryEdges state;
    std::string detail;
};
// Base 0042A840 / expansion 0042CF80. The layer and path are from one
// resolved hierarchy search. The caller selects the original level 0..2.
// This preserves insertion order and duplicates. It does not enable retries
// without the separate local reachability and search-progress state.
RetryEdgesResult block_original_hierarchy_exit(const HierarchyLayer&,
    const std::vector<std::uint16_t>& path,std::uint16_t current,
    const HierarchyRetryEdges&);
}
