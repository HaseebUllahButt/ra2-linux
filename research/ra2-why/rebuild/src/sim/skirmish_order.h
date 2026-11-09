#pragma once
#include "../map/map_file.h"
#include <cstdint>
#include <string>

namespace ra2::sim::skirmish {
// Orders from the native match interface and computer player.
// The match controller owns execution and the single shared frame.
enum class OrderKind { Move, Attack, Deploy, Build, Place, Stop };
struct Order {
    OrderKind kind=OrderKind::Stop;
    std::uint32_t house=0, entity=0, target=0;
    std::string type;
    ra2map::Cell cell{};
};
}
