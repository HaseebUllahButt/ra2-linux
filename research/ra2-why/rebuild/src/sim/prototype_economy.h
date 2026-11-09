#pragma once
#include "prototype_movement.h"
#include "rules/ini_file.h"
namespace ra2::sim {
// One instance per match. Owns routes for miners only; tick once per frame.
// The battle loop must not give these miners to another movement instance.
// Construct from base rules.ini. Call with monotonically increasing frame IDs.
// Incomplete snapshots and duplicate/older frames do nothing.
// Cargo belongs to this instance; create a fresh instance for each match.
// Live placed CMIN/HARV units work automatically, without a harvest order.
class PrototypeEconomy {
public:
    explicit PrototypeEconomy(const rules::IniFile& rules);
    void tick(session::world::Snapshot& world, std::uint32_t frame);
    int load(std::uint32_t miner) const;
private:
    // Bails per resource index. Docked unloading gives one resource at a time.
    struct Cargo { std::vector<int> bails; int total=0; std::uint32_t next=0; bool returning=false, loading=false, docked=false; std::optional<ra2map::Cell> target; };
    struct Resource { std::string section; int first_overlay=-1; int growth=1, interval=1, value=0; };
    std::vector<Resource> resources_;
    // Live OverlayType index to resource index, or -1. The first resource
    // that names an overlay keeps it.
    std::vector<int> overlay_resources_;
    rules::IniFile rules_;
    PrototypeMovement movement_;
    std::map<std::uint32_t,Cargo> cargo_;
    std::optional<std::uint32_t> last_;
    bool grows_=true, spreads_=true; // General TiberiumGrows and TiberiumSpreads.
    // Frames per bail (9 animation steps of General HarvesterLoadRate) and
    // frames per docked resource unload (General HarvesterDumpRate minutes).
    int load_frames_=18, dump_frames_=15;
    int resource(const session::world::CellState&) const;
};
}
