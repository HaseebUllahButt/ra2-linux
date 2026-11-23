#pragma once
#include "ground.h"
#include <array>
#include <string>
namespace ra2::sim {
// Base 004BA410/table 00817DFC; expansion 004CAC40/table 008650BC. Read-only input.
class OriginalDistance {
public:
    static OriginalDistance from_executable(const std::string& path);
    float square_root(double squared) const;
    int between(Coord from,Coord to,bool planar=false) const;
private:
    std::array<std::uint32_t,16384> table_{};
};
}
