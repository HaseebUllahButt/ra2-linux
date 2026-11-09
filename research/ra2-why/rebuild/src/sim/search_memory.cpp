#include "search_memory.h"
#include "hierarchy_route.h"
#include <algorithm>
#include <stdexcept>
namespace ra2::sim {
void retain_original_hierarchy_paths(OriginalSearchMemory& memory,const HierarchyRoute& route) {
    for (const auto& layer:route.layers)
        if (layer.path.size()>500) throw std::invalid_argument("original hierarchy path exceeds its 500-word buffer");
    for (int level=2;level>=0;--level) {
        const auto& path=route.layers[level].path;
        if (path.empty()) continue;
        std::copy(path.begin(),path.end(),memory.paths[level].begin());
        memory.counts[level]=static_cast<std::uint32_t>(path.size());
    }
}
}
