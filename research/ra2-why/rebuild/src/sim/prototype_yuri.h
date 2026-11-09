#pragma once

#include "../map/map_file.h"
#include "../rules/ini_file.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::sim {

// ---------------------------------------------------------------------------
// Yuri's Revenge content for the native skirmish prototype. Every value comes
// from rulesmd.ini and artmd.ini read through the MIX loader. Behaviour that
// the original hardcodes or needs unrecovered code carries a TODO-ORIGINAL
// note with the gamemd.exe address.
// ---------------------------------------------------------------------------

// One techno type: a unit, an infantry or a building. The record keeps the raw
// lists so later prototypes can re-check membership without re-parsing.
struct YuriTechno {
    enum class Kind { Unit, Infantry, Building };

    Kind kind = Kind::Unit;
    std::string name;                    // Section name, e.g. "YURI".
    std::string ui_name;                 // UIName without the "Name:" prefix.
    std::string owner;                   // Raw Owner= country list.
    std::string primary, secondary;
    std::string elite_primary, elite_secondary;
    std::string armor;                   // Armor= name, e.g. "light".
    std::string prerequisite;            // Raw Prerequisite= list.
    std::string image;                   // Rules Image= override.
    std::string deploys_into;            // SCHP -> SCHD.
    std::string undeploys_into;          // YACNST -> PCV.
    std::string deployed_weapon;         // DeployedWeapon=.
    std::string enslaves;                // YAREFN Enslaves=SLAV.
    std::int32_t strength = 0, cost = 0, sight = 0, speed = 0, tech_level = 0;
    std::int32_t build_limit = 0;        // 0 means unlimited in this prototype.
    bool naval = false;
    bool gattling = false;               // IsGattling=yes.
    bool immune_to_psionics = false;     // ImmuneToPsionics=yes.
    bool deploy_to_fire = false;         // DeployToFire=yes.
    bool jumpjet = false;                // JumpJet=yes.
    bool cloakable = false;              // Cloakable=yes (Boomer submerge).
    bool grinding = false;               // Grinding=yes (Yuri Grinder).
    bool powered = false;                // Powered=yes (Psychic Tower).
    std::int32_t passengers = 0;         // Battle Fortress seats.
    std::int32_t power = 0;              // Signed building power output.
    bool trainable = false;              // Trainable=yes (veterancy).
    // artmd.ini additions.
    bool voxel = false;
    std::string foundation;              // "3x3".
    std::string buildup;                 // Buildup= animation.
    std::string cameo;                   // Cameo= sidebar icon.

    // True when the Owner= list names the given country. Case insensitive.
    bool owned_by(std::string_view country) const;
    bool yuri() const { return owned_by("YuriCountry"); }
};

// Mastermind overload categories from rulesmd.ini [General].
// You fall into the biggest category you are equal or less than.
struct YuriOverload {
    std::vector<std::int32_t> count;     // OverloadCount=3,6,10,50.
    std::vector<std::int32_t> damage;    // OverloadDamage=0,50,100,500.
    std::vector<std::int32_t> frames;    // OverloadFrames=30,60,60,60.
    // Biggest count entry >= controlled. Zero when the rules are missing.
    std::size_t category(std::int32_t controlled) const;
    std::int32_t damage_for(std::int32_t controlled) const;
    std::int32_t frames_for(std::int32_t controlled) const;
};

// One mind control weapon: a rulesmd.ini weapon whose warhead has
// MindControl=yes. Damage counts control links, not health.
struct YuriMindControl {
    std::string name;                    // "MindControl", "SuperMindControl", ...
    std::int32_t nodes = 0;              // Weapon Damage= control links.
    bool infinite = false;               // InfiniteMindControl=yes (Mastermind).
    std::int32_t rof = 0, range = 0;
    std::string warhead;                 // Controller / ControllerBuilding.
    std::vector<double> verses;          // Warhead Verses= by armor index.
    // Building armors are wood, steel and concrete (indexes 6..8). Only the
    // Yuri Prime warhead can hurt them.
    bool controls_buildings() const;
    // Verses value for one armor name ("none", "flak", ...). One when unknown.
    double verse_for(std::string_view armor) const;
};

// One gattling stage weapon from Weapon1..Weapon6 or EliteWeapon1..6.
struct YuriGattlingWeapon {
    std::string name;
    std::int32_t damage = 0, rof = 0, range = 0;
    std::string warhead, projectile;
    bool anti_air = false;               // Projectile AA=yes.
    bool anti_ground = false;            // Projectile AG=yes (default).
};

// Gattling stage rules from one gattling type. Stage1..3 and EliteStage1..3
// are rules keys; the defaults below only apply when a key is missing.
struct YuriGattling {
    struct Stage { YuriGattlingWeapon ground, air; };
    std::vector<Stage> stages;           // Weapon1..Weapon6 pairs.
    std::vector<Stage> elite_stages;     // EliteWeapon1..EliteWeapon6 pairs.
    std::int32_t rate_up = 1, rate_down = 50; // Rules RateUp/RateDown.
    std::array<std::int32_t, 3> stage_values{{30, 60, 90}};
    std::array<std::int32_t, 3> elite_stage_values{{30, 60, 90}};
};

// Gattling stage state: TechnoClass GattlingRateUp 0070DE70 and
// GattlingRateDown 0070E000. One instance per firing techno.
class GattlingState {
public:
    explicit GattlingState(const YuriGattling& rules) noexcept : rules_(&rules) {}

    // One frame. A firing weapon climbs by rate_up; an idle one cools by
    // rate_down. The stage follows the value against the Stage= thresholds.
    void tick(bool firing, bool elite);
    std::int32_t stage() const noexcept { return stage_; }
    // Current weapon number: stage*2, +1 for air targets. Elite weapons use
    // the same numbering inside their own table.
    std::int32_t weapon_number(bool air_target) const;
    // The stage weapon, or null when the stage table is incomplete.
    const YuriGattlingWeapon* weapon(bool air_target, bool elite) const;

private:
    const YuriGattling* rules_ = nullptr;
    std::int32_t stage_ = 0, value_ = 0; // CurrentGattlingStage, GattlingValue.
};

// Mind control links: CaptureManagerClass 00471C90 CanCapture, 00471D40
// CaptureUnit, 00471FF0 FreeUnit, 00472140 FreeAll and 00471A50 HandleOverload.
class MindController {
public:
    struct Node {
        std::uint32_t target = 0;          // Controlled techno id.
        std::uint32_t original_owner = 0;  // House id before the capture.
    };

    MindController() = default;
    MindController(std::int32_t max_nodes, bool infinite, YuriOverload overload)
        : max_nodes_(max_nodes), infinite_(infinite), overload_(overload) {}

    std::int32_t max_nodes() const noexcept { return max_nodes_; }
    bool infinite() const noexcept { return infinite_; }
    std::size_t node_count() const noexcept { return nodes_.size(); }
    bool full() const noexcept {
        return static_cast<std::int32_t>(nodes_.size()) >= max_nodes_;
    }
    // A Mastermind holding more links than its limit takes overload damage.
    bool overloaded() const noexcept {
        return infinite_ && static_cast<std::int32_t>(nodes_.size()) > max_nodes_;
    }
    bool controls(std::uint32_t target) const;

    // Original checks in order: a different abstract type than the controller,
    // a warhead verse above zero for the target armor, no immunity, and a
    // non-permanent target must not already be mind controlled.
    // TODO-ORIGINAL 00471C90 exact immune and existing-link checks.
    bool can_capture(bool same_abstract_type, double verse, bool immune,
                     bool already_controlled, bool permanent_target) const;

    // A full non-infinite controller drops every link first, so Yuri Prime
    // swaps targets and a Psychic Tower releases all three. The caller owns
    // the house transfer; the link only remembers the original owner.
    // Returns false when the target is already controlled by this controller.
    bool capture(std::uint32_t target, std::uint32_t original_owner);
    // One link. Returns the original owner, or zero when not controlled.
    std::uint32_t free(std::uint32_t target);
    void free_all();

    // One frame of Mastermind overload. The pip state climbs to the overload
    // category and damage is applied once per frames interval. Returns the
    // damage the controller takes this frame (zero when not overloaded).
    // TODO-ORIGINAL 00471A50 warhead delivery and pip animation.
    std::int32_t overload_tick();

private:
    std::vector<Node> nodes_;
    std::int32_t max_nodes_ = 0;
    bool infinite_ = false;
    YuriOverload overload_;
    std::int32_t pip_state_ = 0, damage_delay_ = 0;
};

// Magnetron magnetic beam. The target hovers helpless and is dragged one cell
// per beam tick toward the Magnetron, then falls.
// TODO-ORIGINAL: the original beam drives the target locomotor; this
// prototype moves the target directly and applies drop damage on release.
class MagnetronBeam {
public:
    void start(std::uint32_t source, std::uint32_t target);
    void stop() noexcept { active_ = false; }
    bool active() const noexcept { return active_; }
    std::uint32_t source() const noexcept { return source_; }
    std::uint32_t target() const noexcept { return target_; }
    // Drag the target one cell toward the source. Returns false when the
    // target reached the source cell or the beam is off.
    bool drag(ra2map::Cell& target_cell, ra2map::Cell source_cell);
    // Drop damage: FallingDamageMultiplier times the target strength when
    // CurrentStrengthDamage=yes, else times its maximum strength.
    std::int32_t drop_damage(std::int32_t target_strength,
                             std::int32_t target_max_strength,
                             double falling_multiplier,
                             bool current_strength_damage) const;

private:
    std::uint32_t source_ = 0, target_ = 0;
    bool active_ = false;
};

// Floating Disc credit drain while parked on an enemy building.
// rulesmd.ini [General] DrainMoneyFrameDelay=30, DrainMoneyAmount=30.
class DiscDrain {
public:
    void start(std::uint32_t building);
    void stop() noexcept { target_ = 0; elapsed_ = 0; }
    bool active() const noexcept { return target_ != 0; }
    std::uint32_t target() const noexcept { return target_; }
    // One frame over the building. Returns the credits drained this frame.
    // TODO-ORIGINAL: the original also cuts power over a power plant.
    std::int32_t tick(std::int32_t frame_delay, std::int32_t amount,
                      std::int32_t building_credits);

private:
    std::uint32_t target_ = 0;
    std::int32_t elapsed_ = 0;
};

// Boomer missile submarine: submerge hides the hull, torpedoes hit ships and
// cruise missiles hit land targets.
class BoomerState {
public:
    bool submerged() const noexcept { return submerged_; }
    void submerge() noexcept { submerged_ = true; }
    void surface() noexcept { submerged_ = false; }
    // Weapon name for one target: the primary torpedo against naval targets,
    // the secondary cruise missile against land targets.
    // TODO-ORIGINAL: original target pick uses TechnoClass GetWeapon 007177C0.
    static const char* weapon(bool target_is_naval);

private:
    bool submerged_ = false;
};

// Siege Chopper: flies, deploys into an artillery platform.
class SiegeChopperState {
public:
    bool deployed() const noexcept { return deployed_; }
    // Deploy needs the SCHD type to exist; the caller checks it.
    void deploy() noexcept { deployed_ = true; }
    void undeploy() noexcept { deployed_ = false; }
    // Weapon name: the cannon while flying, the deployed artillery on ground.
    // The chopper type stores the artillery in Secondary= (160mm).
    // Null when the deployed weapon is missing.
    const char* weapon(const std::string& deployed_weapon) const;

private:
    bool deployed_ = false;
};

// Grinder: credits granted for one ground techno. The original pays
// RefundPercent of the type cost. TODO-ORIGINAL 00711F60 GetRefund.
std::int32_t grinder_refund(const YuriTechno& unit, std::int32_t refund_percent);

// Cloning Vats: the free duplicate for one trained infantry type. Every
// infantry gets a copy. TODO-ORIGINAL: original spawn order and veteran
// copy rules are unknown; the prototype spawns a plain copy.
bool cloning_vats_duplicates(const YuriTechno& trained);

// Guardian GI: deployed weapon switch. Standing the rifle, deployed the
// anti-armour missile launcher that also reaches air targets.
const char* guardian_gi_weapon(bool deployed, const std::string& primary,
                               const std::string& secondary);

// Robot Tank: immune to mind control and needs a Robot Control Center.
bool robot_tank_needs_center(const YuriTechno& tank, const YuriTechno& center);
bool robot_tank_immune_to_psionics(const YuriTechno& tank);

// Battle Fortress passenger fire: open-topped range bonus, damage multiplier
// and chrono warp distance from rulesmd.ini [General].
struct BattleFortressFire {
    std::int32_t range_bonus = 0;           // OpenToppedRangeBonus=2.
    double damage_multiplier = 1.0;         // OpenToppedDamageMultiplier=1.2.
    std::int32_t warp_distance = 0;         // OpenToppedWarpDistance=7.
};

// The loaded Yuri's Revenge content pack.
struct PrototypeYuriRules {
    // Reads rulesmd.ini and artmd.ini. Throws std::runtime_error when the
    // Yuri side or one of the special rules is missing.
    static PrototypeYuriRules load(const rules::IniFile& rules_md,
                                   const rules::IniFile& art_md);

    // [Sides] and the Yuri country.
    std::string side_name;               // "ThirdSide".
    std::string country;                 // "YuriCountry".
    std::int32_t side_index = 0;         // ThirdSide index, 2.
    std::int32_t country_index = 0;      // YuriCountry index, 9.
    std::string country_suffix;          // YuriCountry Suffix=Soviet.
    // Yuri side technos in rules order.
    std::vector<YuriTechno> infantry, vehicles, buildings;
    // New Allied and Soviet expansion units.
    std::vector<YuriTechno> allied_units;  // GGI, ROBO, BFRT.
    std::vector<YuriTechno> soviet_units;  // SCHP.
    // Special rule values.
    YuriOverload overload;               // Mastermind overload categories.
    YuriMindControl mind_control;        // [MindControl] (Yuri).
    YuriMindControl super_mind_control;  // [SuperMindControl] (Yuri Prime).
    YuriMindControl multi_tank_control;  // [MultipleMindControlTank] (Mastermind).
    YuriMindControl multi_tower_control; // [MultipleMindControlTower] (tower).
    YuriGattling gattling_tank;          // [YTNK] Gattling Tank stages.
    YuriGattling gattling_cannon;        // [YAGGUN] Gattling Cannon stages.
    std::int32_t drain_amount = 0, drain_delay = 0;   // Floating Disc.
    double falling_damage_multiplier = 1.0;           // Magnetron drop.
    bool current_strength_damage = true;              // Magnetron drop base.
    std::int32_t refund_percent = 50;                 // Grinder refund.
    BattleFortressFire open_topped;                   // Battle Fortress.
    // Lookups. Null when the name is not loaded. find searches every list;
    // the other three search the Yuri side lists only.
    const YuriTechno* find(std::string_view name) const;
    const YuriTechno* find_infantry(std::string_view name) const;
    const YuriTechno* find_vehicle(std::string_view name) const;
    const YuriTechno* find_building(std::string_view name) const;
};

} // namespace ra2::sim
