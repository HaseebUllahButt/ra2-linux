#pragma once
// Prototype turret turning and infantry fire steps for the first playable
// skirmish. Simplified from TechnoClass::Can_Fire, TechnoClass::Fire_At and
// the UnitClass turret AI in ref/CnC_Red_Alert/CODE/{TECHNO,TURRET,UNIT}.CPP.
// The combat prototype calls aim and ready_to_fire once per shooter per frame.
#include "../session/world/world.h"
#include <cstdint>
#include <cstdlib>

namespace ra2::sim {
// Facings use the original 0..255 clockwise scale, 0 = north.
constexpr std::int32_t facings = 256;

// Shortest signed facing distance between two facings, -128..128 steps.
std::int32_t facing_difference(std::int32_t from, std::int32_t to);

// One frame of turret rotation toward the desired facing at the ROT rate.
// The step never overshoots the desired facing. A zero or negative ROT rate
// holds the current facing.
std::int32_t aim(std::int32_t current, std::int32_t desired, std::int32_t rot);

// Facing in 0..255 steps from one world position to another. Equal positions
// keep the current facing. Prototype-only helper: the combat runtime resolves
// directions with the verified BaseBallisticAngles::heading path (base
// 006CB062..006CB0A2) instead of this atan2 version.
std::int32_t direction_to(const Coord& from, const Coord& to);

// Planar and height lepton distance between two world positions, matching the
// original ::Distance weight of the X, Y and Z components.
std::int64_t lepton_distance(const Coord& from, const Coord& to);

// One weapon fire gate check per frame. The caller resolves the authored
// weapon RateOfFire and Range into frames and leptons first.
struct FireControl {
    std::uint64_t frame=0;      // Current game frame.
    std::uint64_t last_shot=0;  // Frame of the last shot; 0 before the first.
    std::int32_t rof=0;         // Weapon RateOfFire in frames.
    std::int32_t range=0;       // Weapon Range in leptons (cells * 256).
    std::int32_t distance=0;    // Shooter-to-target distance in leptons.
};

// Rearm and range gate. True when the weapon has rearmed and the target is
// within range. This is not the original eligibility chain: it skips cloak,
// falling, ammo and anti-air checks that TechnoClass::Can_Fire performs.
bool ready_to_fire(const FireControl& control);

// Original base FacingClass: 16-bit desired and start facings, a frame timer
// and a per-frame ROT step. Raw facings use 65536 steps, 0 = north,
// clockwise. A timer start of -1 is the stopped state.
struct BaseFacing {
    std::uint16_t desired=0, start=0;
    std::int32_t timer_start=-1, timer_duration=0;
    std::int16_t rot=0;
};
// Base 004B8E90 Set_ROT: rates of 127 or more clamp to 127; the low byte of
// the rate becomes the high byte of the raw step.
std::int16_t base_facing_rot(std::int32_t rate);
// Base 004B8C90 Is_Rotating at the given game frame.
bool base_facing_rotating(const BaseFacing&, std::int32_t frame);
// Base 004B8BE0 Current: interpolates from start to desired by the frames
// left on the timer. The step divides before it multiplies.
std::uint16_t base_facing_current(const BaseFacing&, std::int32_t frame);
// Base 004B8A30 Set_Desired. Returns false when the desired facing is equal.
// Otherwise the start becomes the current facing and the timer restarts with
// |desired-start|/ROT frames.
bool base_facing_set_desired(BaseFacing&, std::uint16_t desired, std::int32_t frame);
// Base 004B8B10 Set_Current: snaps both facings and stops the rotation.
void base_facing_set_current(BaseFacing&, std::uint16_t current, std::int32_t frame);
// Base 00704DF0..00704E8E, UnitClass fire facing check. The turret (or hull)
// current facing must be within 8 facings (16 for a projectile with ROT) of
// the present direction to the target, else the original returns FIRE_FACING.
bool base_unit_facing_ready(std::uint16_t current, std::uint16_t target_direction,
                            bool projectile_has_rot);

// Base 006C9C00 Rearm_Delay burst branch for a weapon without special
// effects. burst_index is the Techno +310 count after the original increment
// at 006CB2D9, before its modulo. A count below Burst returns the Unit type
// BurstDelay(count-1) when that value is not -1; otherwise it asks for the
// original Random(3,5). A finished burst needs the ordinary ROF delay.
struct BurstDelayResult {
    bool in_burst=false;      // False: use the ordinary ROF delay.
    bool needs_random=false;  // True: the delay is Random(3,5).
    std::int32_t delay=0;     // Valid when in_burst && !needs_random.
};
BurstDelayResult base_burst_delay(std::int32_t burst_index, std::int32_t burst,
                                  bool shooter_is_unit, const std::int32_t (&burst_delays)[4]);

// Inline so every target that links the combat runtime gets these.
namespace detail {
// The original CDTimerClass remaining-time read used by FacingClass.
inline std::int32_t facing_timer_left(const BaseFacing& facing, std::int32_t frame) {
    if (facing.timer_start==-1) return facing.timer_duration;
    const std::int64_t elapsed=std::int64_t(frame)-facing.timer_start;
    if (facing.timer_duration<=elapsed) return 0;
    return static_cast<std::int32_t>(facing.timer_duration-elapsed);
}
}

inline std::int16_t base_facing_rot(std::int32_t rate) {
    if (rate>=127) rate=127;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>((rate&0xff)<<8));
}

inline bool base_facing_rotating(const BaseFacing& facing, std::int32_t frame) {
    return facing.rot>0 && detail::facing_timer_left(facing,frame)!=0;
}

inline std::uint16_t base_facing_current(const BaseFacing& facing, std::int32_t frame) {
    if (facing.rot<1) return facing.desired;
    const auto left=detail::facing_timer_left(facing,frame);
    if (left==0) return facing.desired;
    const auto difference=static_cast<std::int16_t>(facing.desired-facing.start);
    const auto steps=static_cast<std::int16_t>(std::abs(int(difference))/facing.rot);
    if (steps<1) return facing.desired;
    const int step=difference/steps;
    return static_cast<std::uint16_t>(facing.desired-step*static_cast<std::int16_t>(left));
}

inline bool base_facing_set_desired(BaseFacing& facing, std::uint16_t desired, std::int32_t frame) {
    if (facing.desired==desired) return false;
    facing.start=base_facing_current(facing,frame);
    facing.desired=desired;
    if (facing.rot>0) {
        facing.timer_start=frame;
        facing.timer_duration=std::abs(int(static_cast<std::int16_t>(desired-facing.start)))/facing.rot;
    }
    return true;
}

inline void base_facing_set_current(BaseFacing& facing, std::uint16_t current, std::int32_t frame) {
    facing.desired=facing.start=current;
    facing.timer_start=frame;facing.timer_duration=0;
}

inline bool base_unit_facing_ready(std::uint16_t current, std::uint16_t target_direction, bool projectile_has_rot) {
    const int tolerance=(projectile_has_rot?16:8)<<8;
    return std::abs(int(static_cast<std::int16_t>(current-target_direction)))<=tolerance;
}

inline BurstDelayResult base_burst_delay(std::int32_t burst_index, std::int32_t burst, bool unit,
                                  const std::int32_t (&delays)[4]) {
    if (burst_index>=burst) return {};
    if (burst_index>0 && burst_index<5 && unit && delays[burst_index-1]!=-1)
        return {true,false,delays[burst_index-1]};
    return {true,true,0};
}
}
