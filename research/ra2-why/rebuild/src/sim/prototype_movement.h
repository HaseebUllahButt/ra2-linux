#pragma once
#include "../session/world/world.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace ra2::sim {
// Playable prototype only. It accepts PrototypeComplete snapshots only.
// This is not the original RA2 movement implementation.
// TODO-ORIGINAL: replace grid routing, simple motion, Infantry occupancy,
// collision responses, missions, visibility, bridge traversal and locomotor AI.
enum class PrototypeMoveStatus { Complete,Blocked,NeedsInput,Invalid };
struct PrototypeMoveResult {
    PrototypeMoveStatus status=PrototypeMoveStatus::NeedsInput;
    std::string detail;
    std::vector<std::uint32_t> moved,arrived,blocked;
};
class PrototypeMovement {
public:
    // Speed is supplied in world units per frame. Commands do not change world.
    // A later command replaces the pending route only after the new path exists.
    PrototypeMoveResult issue(const session::world::Snapshot&,std::uint32_t unit,ra2map::Cell goal,int speed);
    void cancel(session::world::Snapshot&,std::uint32_t unit);
    bool moving(std::uint32_t unit) const { return moves_.count(unit)!=0; }
    std::size_t active_count() const { return moves_.size(); }
    // Apply all pending moves in ID order to one private world. Publish the
    // world and all routes together. game_frame is supplied by the game loop.
    PrototypeMoveResult tick(session::world::Snapshot&,std::uint32_t game_frame);
private:
    struct Move { ra2map::Cell goal;int speed=0;std::vector<ra2map::Cell> path; };
    std::map<std::uint32_t,Move> moves_;
    std::optional<std::uint32_t> last_frame_;
};
}
