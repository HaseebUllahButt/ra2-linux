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
    // Base 00701580 (unload): a computer house in a multiplayer game adds
    // Rules AIVirtualPurifiers[its difficulty] to its Ore Purifier count.
    // The battle sets this count for each computer house; default is zero.
    void set_virtual_purifiers(std::uint32_t house, int count);
    // [General] AIVirtualPurifiers for an original difficulty index
    // (House +C8: 0 hardest, 1 medium, 2 easiest). Zero when not listed.
    static int virtual_purifiers(const rules::IniFile& rules, int original_difficulty);
private:
    // Bails per resource index. Docked unloading gives one resource at a time.
    struct Cargo {
        std::vector<int> bails; int total=0; std::uint32_t next=0; bool returning=false, loading=false, docked=false;
        std::optional<ra2map::Cell> target;
        // Watchdog for a route that makes no progress: the last cell seen
        // while moving, the frame it was first seen, and an ore cell to skip
        // in the next search.
        std::optional<ra2map::Cell> progress, avoid; std::uint32_t progress_frame=0;
    };
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
    std::map<std::uint32_t,int> virtual_purifiers_;
    double purifier_bonus_=0.25; // General PurifierBonus (Rules +C40).
    // Extra credits for one unloaded resource worth `credits`.
    std::int64_t purifier_credits(const session::world::Snapshot&,std::uint32_t house,std::int64_t credits) const;
    // Frames per bail (9 animation steps of General HarvesterLoadRate) and
    // frames per docked resource unload (General HarvesterDumpRate minutes).
    int load_frames_=18, dump_frames_=15;
    // A moving miner that stays on one cell this long gives up its route
    // (five times General BlockagePathDelay).
    // TODO-ORIGINAL: the original harvest mission re-plans through the Foot
    // path retry states; this is a simple time limit.
    int stall_frames_=300;
    int resource(const session::world::CellState&) const;
};
}
