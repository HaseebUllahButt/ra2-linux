#pragma once
// Pure vertical missile movement, read from the base game.exe 1.006 bullet
// tick FUN_0045F710 (export lines 290-323). Before each position step the
// velocity magnitude gains the projectile's acceleration (read at
// projectile+0x2A0; export line 294: local_1a0 = *(int *)(projectile + 0x2a0)
// + speed) and is clamped against the bullet's max speed (bullet+0xF8,
// compared at line 296: if (speed < bullet+0xF8) rescale the velocity vector).
// The same math drives the nuke missiles: the silo mission FUN_00448400
// launches the upward missile with velocity 10 and speed cap 255, and the
// downward payload starts (FUN_00463C50, valid code 00463D55..00463E75 that
// the export wrongly omitted as unreachable) at target.z + the carrier
// projectile's DetonationAltitude with initial downward speed: the constant
// at 0079D578 is -1.0 (verified in .rdata: 00 00 00 00 00 00 F0 BF).
#include <algorithm>
#include <cmath>

namespace ra2::sim {

struct VerticalProjectileState {
    int z = 0;
    double velocity = 0.0;
};

struct VerticalProjectileFlags {
    bool above_ceiling = false;      // z > ceiling (upward detonation altitude).
    bool at_or_below_floor = false;  // z <= floor (ground reached).
};

// One game frame of vertical movement. Speed keeps its fractional part; only
// the slowdown term acceleration/2 uses integer division, matching the
// original.
inline VerticalProjectileFlags step_vertical_projectile(
    VerticalProjectileState& state, int max_speed, int acceleration,
    int floor, int ceiling) {
    double speed = std::abs(state.velocity);
    if (speed < max_speed) {
        speed = std::min<double>(max_speed, speed + acceleration);
    } else if (speed > max_speed) {
        const auto slowdown = acceleration / 2; // Integer division.
        speed = slowdown < speed ? speed - slowdown : 0.0;
    }
    state.velocity = state.velocity < 0 ? -speed : speed;
    state.z += static_cast<int>(state.velocity); // Truncate toward zero.
    return VerticalProjectileFlags{state.z > ceiling, state.z <= floor};
}

} // namespace ra2::sim
