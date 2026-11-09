#pragma once
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace ra2::sim {
// Playable prototype only. It accepts PrototypeComplete snapshots only.
// Vehicles with the original drive locomotor use the original speed, terrain,
// acceleration, turn and crush rules (see prototype_movement.cpp).
// TODO-ORIGINAL: replace grid routing, Infantry occupancy and walking,
// curve tracks, scatter, missions, visibility and bridge traversal.
enum class PrototypeMoveStatus { Complete,Blocked,NeedsInput,Invalid };
struct PrototypeMoveResult {
    PrototypeMoveStatus status=PrototypeMoveStatus::NeedsInput;
    std::string detail;
    std::vector<std::uint32_t> moved,arrived,blocked;
    // Infantry killed this frame by a crushing vehicle (base 00705160).
    std::vector<std::uint32_t> crushed;
};
// Rules values for one object type. Defaults are the original constructor
// values; from_rules() reads the type's RULES.INI section.
struct PrototypeMoveProfile {
    bool drive=false;          // Locomotor {4A582741-...}: track-sample motion.
    int speed_type=1;          // Foot 0, Track 1, Wheel 2, Hover 3, ...
    int rot=0;                 // ROT; 0 turns at once.
    bool accelerates=false;    // Base TechnoType +B59.
    int slowdown_distance=500; // +2C8.
    double deceleration=0.002,acceleration=0.03; // +2D0, +2D8.
    bool crusher=false,crushable=false; // +AF3, Object +1FD.
    int strength=0;
    int speed=10;              // original_rule_speed(Speed); 10 for E1 Speed=4.
    // Ground[land].Cost[speed_type], land types 0..11 (Clear .. Weeds).
    std::array<float,12> ground{{1,1,1,1,1,1,1,1,1,1,1,1}};
};
// Global RULES.INI values. Defaults are the base RULES.INI values.
struct PrototypeMoveGlobals {
    double tracked_uphill=1.0,tracked_downhill=1.2,wheeled_uphill=1.0,wheeled_downhill=1.2;
    double condition_yellow=0.5;
    int close_enough=576;      // [General] CloseEnough=2.25 cells, in leptons.
    int blockage_path_delay=60; // [General] BlockagePathDelay frames.
};
class PrototypeMovement {
public:
    // Read per-type and global movement values. The rules must outlive this
    // object. Without rules, vehicles use the drive model with Track, ROT 0,
    // no acceleration, no crushing and a ground multiplier of one.
    void use_rules(const rules::IniFile&);
    PrototypeMoveProfile profile(const session::world::Entity&) const;
    const PrototypeMoveGlobals& globals() const { return globals_; }
    // Speed is the original rule speed (Type +5A0, original_rule_speed()).
    // Drive vehicles spend it on track samples; other Units move that many
    // world units per frame. Commands do not change world.
    // A later command replaces the pending route only after the new path exists.
    PrototypeMoveResult issue(const session::world::Snapshot&,std::uint32_t unit,ra2map::Cell goal,int speed);
    // A vehicle part way into a step completes that step, as the original
    // drive finishes its current track.
    void cancel(session::world::Snapshot&,std::uint32_t unit);
    bool moving(std::uint32_t unit) const { return moves_.count(unit)!=0; }
    std::size_t active_count() const { return moves_.size(); }
    // Apply all pending moves in ID order to one private world. Publish the
    // world and all routes together. game_frame is supplied by the game loop.
    PrototypeMoveResult tick(session::world::Snapshot&,std::uint32_t game_frame);
    // Drive state of one vehicle (empty if it never moved).
    struct Motion {
        bool stepping=false;
        ra2map::Cell from{},to{};
        Coord origin{},end{};
        int sample=0,samples=0,correction=0,remainder=0;
        double speed=0.0,target=1.0; // Foot +468 SpeedPercentage, Drive +50.
        int speed_value=0;           // Rule speed of the last order.
    };
    const Motion* motion(std::uint32_t unit) const { auto it=motion_.find(unit); return it==motion_.end()?nullptr:&it->second; }
private:
    // waited counts frames spent behind a moving Unit. stalled is true when
    // the Unit did not move in its last tick. aside is set for a soldier's
    // step out of a vehicle's way: the place where this owner last put it.
    struct Move { ra2map::Cell goal;int speed=0;std::vector<ra2map::Cell> path;int waited=0;bool stalled=false;std::optional<Coord> aside={}; };
    std::map<std::uint32_t,Move> moves_;
    std::map<std::uint32_t,Motion> motion_;
    // Infantry spot (2..4) in its Cell; 0 is a whole-Cell 0x20 occupant.
    std::map<std::uint32_t,int> spots_;
    std::optional<std::uint32_t> last_frame_;
    const rules::IniFile* rules_=nullptr;
    PrototypeMoveGlobals globals_;
    mutable std::map<std::string,PrototypeMoveProfile> profiles_;
};
}
