#include "prototype_turret.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using ra2::sim::FireControl;
using ra2::sim::aim;
using ra2::sim::direction_to;
using ra2::sim::facing_difference;
using ra2::sim::lepton_distance;
using ra2::sim::ready_to_fire;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_facing_difference() {
    require(facing_difference(0, 0) == 0, "equal facings differ by zero");
    require(facing_difference(0, 8) == 8, "positive short difference");
    require(facing_difference(8, 0) == -8, "negative short difference");
    require(facing_difference(250, 10) == 16, "wrap forward through north");
    require(facing_difference(10, 250) == -16, "wrap backward through north");
    require(facing_difference(0, 128) == 128, "half circle is 128 steps");
    require(facing_difference(0, -128) == -128, "negative half circle is -128 steps");
    require(facing_difference(40, 300) == 4, "inputs above 255 wrap");
}

void check_aim() {
    // Rotates toward the target and never overshoots it.
    require(aim(0, 0, 5) == 0, "aim holds an aligned facing");
    require(aim(0, 3, 5) == 3, "aim rotates by the whole gap when ROT allows");
    require(aim(0, 250, 5) == 251, "aim rotates the short way across north");
    require(aim(250, 10, 5) == 250 + 5, "aim steps at most ROT steps per frame");
    require(aim(0, 128, 5) == 5, "aim steps at most ROT steps toward south");
    // Zero or negative ROT holds the facing.
    require(aim(10, 200, 0) == 10, "zero ROT holds the facing");
    require(aim(10, 200, -3) == 10, "negative ROT holds the facing");
    // Infantry face the target in one frame with a full-circle ROT.
    require(aim(128, 128, 40) == 128, "aligned facing stays put");
}

void check_ready_to_fire() {
    FireControl control;
    control.frame = 100;
    control.last_shot = 90;
    control.rof = 10;
    control.range = 5120;
    control.distance = 5120;
    require(ready_to_fire(control), "weapon is ready on the exact ROF frame");
    control.frame = 99;
    require(!ready_to_fire(control), "weapon is not ready one frame early");
    control.frame = 100;
    control.distance = 5121;
    require(!ready_to_fire(control), "out-of-range target blocks the shot");
    control.distance = 5120;
    control.last_shot = 0;
    require(ready_to_fire(control), "a fresh weapon is ready before the first shot");
}

void check_direction_and_distance() {
    const ra2::sim::Coord origin{100, 100, 0};
    require(direction_to(origin, {100, 90, 0}) == 0, "north is facing zero");
    require(direction_to(origin, {110, 100, 0}) == 64, "east is facing 64");
    require(direction_to(origin, {100, 110, 0}) == 128, "south is facing 128");
    require(direction_to(origin, {90, 100, 0}) == 192, "west is facing 192");
    require(lepton_distance(origin, {100, 100 - 2560, 0}) == 2560, "ten cells north");
    require(lepton_distance(origin, {100 + 768, 100 + 1024, 0}) == 1280, "3-4-5 diagonal in leptons");
    // The 3-4-5 triangle scales to leptons in the world plane.
    const ra2::sim::Coord corner{0, 0, 0};
    require(lepton_distance(corner, {768, 1024, 0}) == 1280, "3-4-5 triangle in leptons");
}
}

int main() try {
    check_facing_difference();
    check_aim();
    check_ready_to_fire();
    check_direction_and_distance();
    std::cout << "Prototype turret checks passed: facing difference, aim, rearm and range gates, direction and distance.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
