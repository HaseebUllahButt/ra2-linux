#pragma once

#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {

// Authored flying-type values from the base RULES.INI. The family decides the
// mission: planes dock and reload, airships bomb without landing, hover
// infantry flies at its JumpjetHeight and never lands.
struct PrototypeAircraftRules {
    enum class Family { Plane, Airship, Hover };
    std::string type;
    Family family = Family::Plane;
    std::int32_t speed = 0;        // Speed=, world units per frame.
    std::int32_t ammo = -1;        // Ammo= shots between reloads; -1 means unlimited.
    std::int32_t strength = 0;     // Strength=
    double range = 0;              // Primary Range=, in cells.
    std::int32_t rof = 0;          // Primary ROF=, in frames.
    std::int32_t damage = 0;       // Primary Damage= x Burst=, one trigger pull.
    bool landable = false;         // Landable=
    std::vector<std::string> dock; // Dock= pad types.
    std::int32_t hover_height = 0; // JumpjetHeight= for Airship and Hover.

    // Pass the loaded base RULES.INI. The prototype supports the Harrier ORCA,
    // Black Eagle BEAG, Kirov ZEP and Rocketeer JUMPJET. Throws for unsupported
    // types or missing/invalid required fields.
    static PrototypeAircraftRules load(const rules::IniFile& rules, std::string_view type);
};

// A building type reloads docked aircraft when its rules say UnitReload=yes.
bool is_reload_pad(const rules::IniFile& rules, std::string_view building_type);

// One flight phase per aircraft. Names follow the RA1 AircraftClass missions.
enum class AircraftPhase { Docked, TakeOff, FlyToTarget, Attack, ReturnToPad, Land, Hover };

// Result of one command or one shared frame.
enum class PrototypeAircraftStatus { Complete, Blocked, NeedsInput, Invalid };
struct AircraftShot { std::uint32_t source = 0, target = 0; std::int32_t damage = 0; };
struct PrototypeAircraftResult {
    PrototypeAircraftStatus status = PrototypeAircraftStatus::Complete;
    std::string detail;
    std::vector<std::uint32_t> moved;   // Aircraft that flew this frame.
    std::vector<std::uint32_t> landed;  // Aircraft that closed their dock this frame.
    std::vector<AircraftShot> shots;    // Shots fired this frame.
};

// Playable aircraft for the first native skirmish. This is not the original
// RA2 AircraftClass. The class owns one mission per aircraft and updates a
// private copy of the shared world snapshot, like PrototypeMovement.
// TODO-ORIGINAL: replace straight flight, the simple reload clock, direct shot
// damage and the missing crash handling with original AircraftClass AI.
class PrototypeAircraft {
public:
    // external_combat hands the trigger pull to the caller: the aircraft only
    // faces its target and reports ready_to_fire; the caller calls fired().
    explicit PrototypeAircraft(const rules::IniFile& base_rules,
        bool external_combat = false);

    // True only during the Attack phase with unlimited or positive ammo.
    bool ready_to_fire(std::uint32_t aircraft) const;
    // Spend one shot after a ready_to_fire call; an exhausted plane clears
    // its target and returns to its pad. Ignored when not ready.
    void fired(std::uint32_t aircraft);

    // Order a placed aircraft to attack a target entity. A plane on the ground
    // docks at the nearest owned pad listed in its Dock= rules with a free
    // dock slot. A plane without a pad still flies and hovers on return.
    PrototypeAircraftResult issue(const session::world::Snapshot& world,
        std::uint32_t aircraft, std::uint32_t target);
    // Order a flying entity to fly to a cell and hold there. Hover infantry
    // stays at the goal; planes and airships keep their ammo and pad on a
    // re-issued move, a new plane loads Ammo= and picks a pad, and a plane
    // flies back to its pad through the Hover logic once the goal is reached.
    PrototypeAircraftResult issue_move(const session::world::Snapshot& world,
        std::uint32_t aircraft, ra2map::Cell goal);
    void cancel(session::world::Snapshot& world, std::uint32_t aircraft);
    bool active(std::uint32_t aircraft) const { return flights_.count(aircraft) != 0; }
    std::size_t active_count() const noexcept { return flights_.size(); }
    // Current mission phase; empty when the aircraft has no mission.
    std::optional<AircraftPhase> phase(std::uint32_t aircraft) const;
    // Current ammo shots; empty when the aircraft has no mission.
    std::optional<std::int32_t> ammo(std::uint32_t aircraft) const;
    // [General] FlightLevel in world units; planes cruise at this height.
    std::int32_t flight_level() const noexcept { return flight_level_; }
    // [General] ReloadRate in minutes per ammo point.
    double reload_rate() const noexcept { return reload_rate_; }

    // Advance every mission one frame in ID order on a private copy and
    // publish the world. game_frame comes from the shared game loop.
    PrototypeAircraftResult tick(session::world::Snapshot& world, std::uint32_t game_frame);

private:
    struct Flight {
        PrototypeAircraftRules rules;
        AircraftPhase phase = AircraftPhase::Docked;
        std::optional<std::uint32_t> target;
        std::optional<ra2map::Cell> move_goal;
        std::uint32_t pad = 0;          // Dock pad entity id; zero is none.
        std::int32_t ammo = 0;          // Current shots.
        std::int32_t reload_timer = 0;  // Frames until the next ammo point.
        std::int32_t fire_timer = 0;    // Frames until the next shot.
        std::int32_t cruise_z = 0;      // Height while flying.
    };
    std::optional<std::string> update(session::world::Snapshot& world,
        std::map<std::uint32_t, Flight>& flights, Flight& flight,
        session::world::Entity& actor, PrototypeAircraftResult& result);
    // Pick the mission phase for a new or re-issued attack target.
    void retarget(Flight& flight);
    [[nodiscard]] bool fly_toward(session::world::Entity& actor, Flight& flight,
        const sim::Coord& destination) const;
    std::optional<std::uint32_t> pick_pad(const session::world::Snapshot& world,
        const PrototypeAircraftRules& rules, std::uint32_t house_id,
        const std::map<std::uint32_t, Flight>& flights, const sim::Coord& from) const;
    std::optional<std::int32_t> cruise_height(const session::world::Snapshot& world,
        const session::world::Entity& actor, const PrototypeAircraftRules& rules) const;
    std::int32_t reload_frames(const session::world::Snapshot& world,
        const session::world::Entity& actor, const Flight& flight) const;
    bool dock_free(const std::map<std::uint32_t, Flight>& flights,
        const session::world::Entity& pad) const;

    rules::IniFile rules_;
    bool external_combat_ = false;     // Caller pulls the trigger via fired().
    std::int32_t flight_level_ = 1500; // [General] FlightLevel, world units.
    double reload_rate_ = 0.3;         // [General] ReloadRate, minutes per ammo point.
    std::map<std::uint32_t, Flight> flights_;
    std::optional<std::uint32_t> last_frame_;
};

} // namespace ra2::sim
