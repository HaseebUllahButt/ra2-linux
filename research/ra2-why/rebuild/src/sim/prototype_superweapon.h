#pragma once
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {

// Original SuperWeaponType values. Order and names follow the basegame
// RA2 1.006 super weapon table (see SuperWeaponTypeClass::FindFirstOfAction,
// export 0069F7E0) and the charge rules of RA1 SUPER.CPP SuperClass.
enum class SuperKind {
    Invalid = -1,
    Nuke = 0,
    IronCurtain,
    LightningStorm,
    ChronoSphere,
    ChronoWarp,
    ParaDrop,
    AmerParaDrop,
    PsychicDominator,
    SpyPlane,
    GeneticMutator,
    ForceShield,
    PsychicReveal
};

// One [SuperWeapon] rules section with the fields this prototype uses.
struct SuperweaponType {
    std::string id;                     // Section name, e.g. "MultiSpecial".
    SuperKind kind = SuperKind::Invalid; // Type= value.
    int recharge_frames = 4500;         // RechargeTime minutes * 900 frames.
    bool is_powered = true;             // IsPowered: needs output >= drain.
    float range_cells = 0.0f;           // Range in cells; 0 is the target cell.
    int line_multiplier = 0;            // LineMultiplier.
    std::string weapon;                 // WeaponType= section name.
    std::string pre_dependent;          // PreDependent= Type= name.
    bool pre_click = false, post_click = false, show_timer = false;
};

// [SuperWeaponTypes] of the rules document, in authored order. Sections that
// are listed but missing keep kind Invalid so callers can report them.
std::vector<SuperweaponType> read_superweapon_types(const rules::IniFile& rules);

// Type= name to SuperKind, case-insensitive as the original parse table.
SuperKind superweapon_kind(std::string_view type_name);

// Building types whose SuperWeapon= or SuperWeapon2= grant each super weapon
// section. Keyed by section name in [SuperWeaponTypes] order-independent form.
std::map<std::string, std::vector<std::string>> superweapon_grants(const rules::IniFile& rules);

// Original SuperClass charge state. Grant and the recharge tick follow RA1
// SUPER.CPP Enable/Recharge/AI; Lose is export 0069CF40 and SetOnHold is
// export 0069CC60 (base RA2 game.exe 1.006).
struct SuperweaponState {
    bool present = false;   // IsPresent: granted by an owned building.
    bool ready = false;     // IsReady: charge timer finished.
    bool one_time = false;  // IsOneTime: crate weapon, removed when fired.
    bool suspended = false; // SetOnHold: paused by low power.
    int remaining = 0;      // RechargeTimer frames left.
};

// What one launch did. Counts are visible outcomes; notes list skipped
// original effects so integration can see what is still TODO-ORIGINAL.
struct SuperLaunchReport {
    bool fired = false;
    int damaged = 0, destroyed = 0, teleported = 0, dropped = 0, shielded = 0;
    std::vector<ra2map::Cell> revealed; // Spy plane camera cells.
    std::vector<std::string> notes;
};

// What one charging frame did: lightning storm strikes and their damage, plus
// a landed delayed nuke impact.
struct SuperTickReport {
    int damaged = 0, destroyed = 0;
    int nukes = 0; // Nuke impacts that landed this frame.
    std::vector<std::string> notes;
};

// Playable super weapons for the native skirmish. Owns charge clocks, low
// power suspension, Iron Curtain invulnerability and pending chrono warps.
// The caller owns the world snapshot and the game frame.
class PrototypeSuperweapons {
public:
    // game_exe enables the original distance table for blast falloff; an
    // empty path falls back to an integer distance (see the .cpp note).
    explicit PrototypeSuperweapons(const rules::IniFile& base_rules,
        const std::string& game_exe = {});
    ~PrototypeSuperweapons(); // Out of line: holds the original distance table.

    // Grant every super weapon whose granting building the house owns, and
    // lose the ones whose building is gone. Idempotent; call every frame or
    // after placement changes.
    void update_grants(const session::world::Snapshot& world);

    // Advance charge clocks one game frame. Suspends IsPowered weapons in a
    // low power house and resumes them when power returns. Applies due
    // lightning storm strikes to the world. A repeated or rewound frame does
    // nothing.
    SuperTickReport tick(session::world::Snapshot& world, std::uint64_t frame);

    const SuperweaponState* state(std::uint32_t house, std::string_view section) const;
    // Ready to fire: granted, charged and not suspended.
    bool ready(std::uint32_t house, std::string_view section) const;
    // 0 when ready, remaining frames while charging, -1 when not granted.
    int frames_left(std::uint32_t house, std::string_view section) const;
    // Iron Curtain / Force Shield protection for one object at a frame.
    bool invulnerable(std::uint32_t entity, std::uint64_t frame) const;

    // Fire a ready super weapon at a cell. Damages in an area (Nuke,
    // LightningStorm), shields objects (IronCurtain), arms and teleports
    // (ChronoSphere, ChronoWarp), drops infantry (ParaDrop, AmerParaDrop) or
    // reveals the map (SpyPlane). The charge is consumed except for the
    // ChronoSphere arm, which is consumed by the following ChronoWarp.
    SuperLaunchReport fire(session::world::Snapshot& world, std::uint32_t house,
        std::string_view section, ra2map::Cell target, std::uint64_t frame);

private:
    struct Storm {
        bool active = false;
        std::uint64_t start = 0;
        ra2map::Cell target{};
    };
    rules::IniFile rules_;
    std::vector<SuperweaponType> types_;
    std::vector<std::vector<std::string>> grants_;
    std::map<std::string, std::size_t> index_; // Lowercased id -> types_ slot.
    std::map<std::uint32_t, std::map<std::string, SuperweaponState>> houses_;
    std::map<std::uint32_t, std::uint64_t> invulnerable_;
    std::map<std::uint32_t, ra2map::Cell> armed_;
    struct PendingNuke {
        std::uint64_t impact = 0; // Frame the missile lands.
        ra2map::Cell target{};
    };
    std::vector<PendingNuke> nukes_; // Missiles in flight, one per launch.
    std::optional<Storm> storm_;
    std::vector<ra2map::Cell> bolts_; // Recent storm bolts for separation.
    std::unique_ptr<class OriginalDistance> distance_;
    std::optional<std::uint64_t> last_frame_;

    const SuperweaponType* find_type(std::string_view section) const;
    SuperweaponState* house_state(std::uint32_t house, const std::string& id);
    void grant(std::uint32_t house, std::size_t slot);
    void lose(std::uint32_t house, std::size_t slot);
    void discharge(std::uint32_t house, std::size_t slot);
    bool low_power(const session::world::House& house) const;
};

} // namespace ra2::sim
