#pragma once
#include "../session/world/world.h"
#include "../sim/sim.h"

namespace ra2::app {
// A temporary drawing view. The session Snapshot stays the only live world.
sim::Snapshot battle_scene_state(const session::world::Snapshot&, std::uint64_t frame);
}
