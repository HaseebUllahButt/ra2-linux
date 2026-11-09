#pragma once
// Prototype win and lose detection for the first playable native Linux
// skirmish. It reads one session world snapshot and never changes it.
// The defeat rule is a simplification; see the TODO-ORIGINAL note in
// prototype_victory.cpp.
#include "../session/world/world.h"
#include <cstdint>

namespace ra2::sim {
// One verdict per snapshot. The app shows the first settled verdict.
enum class Victory { InProgress, PlayerWon, PlayerLost, Draw };

// Latches the first non-InProgress verdict. The world keeps running after
// game over, so later snapshots must not change a settled verdict.
struct VictoryState {
    Victory result=Victory::InProgress;
    bool settled() const noexcept { return result!=Victory::InProgress; }
    // Feed every new snapshot. Returns the verdict to display.
    Victory update(const session::world::Snapshot& world,
        std::uint32_t player_house, std::uint32_t enemy_house);
};

// Pure verdict for one snapshot, from the simplified prototype defeat rule.
// Counts surviving Units, Infantry and Structures with positive health that
// belong to the two given houses. Created and Placed objects count; Destroyed
// objects and map-neutral houses never count. Unknown health stays alive.
// Throws std::invalid_argument when a house id is zero, when both ids match,
// when a house is missing from the snapshot, or when a given house has the
// map-neutral or special role.
Victory evaluate_prototype_victory(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house);
}
