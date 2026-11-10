// Focused speed, acceleration and turn checks for the prototype movement
// port (base 004A1000 FootClass::Movement_AI and the drive track helpers).
// Measures fixed-distance frame counts and steady speed against the values
// measured in the original game (reference/REFERENCE.md, Unit speed).
// Real unit sections MTNK (Grizzly) and HTNK (Rhino) and infantry E1/E2 are
// used; TNKD is a synthetic accelerating type kept apart from them.
#include "prototype_movement.h"
#include "ground.h"
#include "../rules/ini_file.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ra2;
using namespace ra2::sim;
namespace sw = ra2::session::world;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(int x, int y) { return std::uint32_t(y) * 512 + std::uint32_t(x); }
sw::Snapshot field(int width, int height) {
    sw::Snapshot world;world.setup_progress = sw::SetupProgress::PrototypeComplete;world.random = Random(24).state();world.cell_order = std::vector<std::uint32_t>{};
    sw::House house;house.id = 1;house.name = "P";house.country = "C";world.houses.push_back(house);
    world.creation_order.push_back({sw::
CreatedKind::House, 1});
    for (int y = 1; y <= height; ++y) for (int x = 1; x <= width; ++x) {
        auto& c = world.cells[key(x, y)];c.allocated = true;c.heads = weapons::CellContentHeads{};
        c.ground_occupancy_124 = 0;c.cell_flag_100 = false;c.terrain = original_default_terrain_cell({static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)});
        world.cell_order->push_back(key(x, y));
    }
    return world;
}
void put(sw::Snapshot& world, std::uint32_t id, const char* type, sw::Kind kind, int x, int y, int facing) {
    sw::Entity
 e;e.id = id;e.house_id = 1;e.type = type;e.kind = kind;e.lifetime = sw::Lifetime::Placed;
     e.cell = ra2map::Cell{
static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};e.position = ground_cell_center(x, y);
    e.layer = sw::Layer::Ground;e.next = 0;
    e.resolved_hp = type == std::string("HTNK") ? 400 : kind == sw::Kind::Infantry ? 125 : 300;e.maximum_hp = e.resolved_hp; // full Strength
    e.hull_facing = facing;e.hull_facing_raw = static_cast<std::uint16_t>(facing * 256);
    world.entities.push_back(e);world.creation_order.push_back({sw::CreatedKind::Entity, id});
    auto& c = world.cells.at(key(x, y));c.heads->ground_head = id;c.ground_occupancy_124 = 32;
}
const sw::Entity& unit(const sw::Snapshot& w, std::uint32_t id) {
    for (const auto& e : w.entities) if (e.id == id) return e;
    throw std::runtime_error("missing Unit");
}
rules::IniFile make_rules() {
    rules::IniFile ini;
    ini.parse(
        "[General]\nCloseEnough=2.25\nBlockagePathDelay=60\nTrackedUphill=1.0\nTrackedDownhill=1.2\n"
        "[AudioVisual]\nConditionYellow=50%\n"
        "[Clear]\nFoot=100%\nTrack=100%\nWheel=100%\n"
        // Real Grizzly (MTNK) and Rhino (HTNK) sections. Steady drive speed
        // on clear ground: mapped Speed leptons a frame; a straight cell
        // track is 24 samples of 7 budget each per 256 leptons with the +5
        // terminal correction, so mapped*256/163 leptons a frame.
        "[MTNK]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nROT=5\nAccelerates=false\nCrusher=yes\nStrength=300\n"
        "[HTNK]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=6\nROT=5\nAccelerates=false\nCrusher=yes\nStrength=400\n"
        // Synthetic accelerating type, kept apart from the real units.
        "[TNKD]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nROT=5\nCrusher=yes\nStrength=300\n"
        "[E1]\nSpeed=4\nCrushable=yes\n[E2]\nSpeed=4\nCrushable=yes\n");
    return ini;
}
// One straight run along a map axis, like the original measurement: the
// order speed comes from the parsed profile (profile.speed), the frames to
// arrive are counted and the mean leptons per frame over the steady frames
// 50..100 is taken.
struct Run { unsigned frames = 0; double steady = 0.0; };
Run straight_run(sw::Snapshot& world, PrototypeMovement& movement, std::uint32_t id, int cells) {
    Run run;
    const int start_x = int(unit(world, id).position->x) / 256;
    double sum = 0;unsigned counted = 0;
    const auto goal_y = unit(world, id).cell->y;
    const auto goal = ra2map::Cell{static_cast<std::uint16_t>(start_x + cells), static_cast<std::uint16_t>(goal_y)};
    const auto speed = movement.profile(unit(world, id)).speed;
    require(speed > 0, "profile speed is zero");
    require(movement.issue(world, id, goal, speed).status == PrototypeMoveStatus::Complete, "order rejected");
    for (std::uint32_t frame = 1; frame <= 400; ++frame) {
        const auto before = *unit(world, id).position;
        const auto r = movement.tick(world, frame);
        require(r.status == PrototypeMoveStatus::Complete, "frame failed");
        const auto& e = unit(world, id);
        if (frame >= 50 && frame <100) { sum += std::abs(double(e.position->x - before.x));++counted; }
        for (const auto a : r.arrived) if (a == id) run.frames = frame;
        if (run.frames) break;
    }
    require(run.frames != 0, "unit never arrived");
    run.steady = counted ? sum / counted : 0.0;
    return run;
}
// Pure turn frames: the unit stands at a Cell center with a new order and
// turns (position static) until it moves again.
unsigned turn_frames(sw::Snapshot& world, PrototypeMovement& movement, std::uint32_t id, ra2map::Cell goal) {
    const auto speed = movement.profile(unit(world, id)).speed;
    require(movement.issue(world, id, goal, speed).status == PrototypeMoveStatus::Complete, "turn order rejected");
    unsigned quiet = 0;
    for (std::uint32_t frame = 1; frame <= 400; ++frame) {
        const auto before = *unit(world, id).position;
        movement.tick(world, frame);
        const auto after = *unit(world, id).position;
        if (after.x == before.x && after.y == before.y && after.z == before.z) ++quiet;
        else { if (quiet) break; quiet = 0; }
    }
    return quiet; // the frames the unit stood still and turned
}
}
int main() {
    const auto table = make_rules();
    {
        // Real Grizzly (MTNK): Speed=7 maps to 17 (004C9B10 Speed*256/100),
        // Accelerates=false, so the speed fraction is the target at once.
        auto world = field(30, 12);PrototypeMovement movement;movement.use_rules(table);
        put(world, 1, "MTNK", sw::Kind::Unit, 3, 6, 64); // facing east
        const auto run = straight_run(world, movement, 1, 11);
        std::cout << "MTNK 11 cells: frames=" << run.frames << " steady=" << 
run.steady
            << " leptons/frame = " << run.steady / 256.0 << " cells/frame" << std::endl;
        // Original measured: 11 cells in about 105 frames, steady 27 leptons
        // a frame (0.105 cells). The model gives 2816 leptons at
        // 17*256/163 = 26.7 a frame plus the slow-down before the goal.
        require(run.frames >= 95 && run.frames <= 120, "MTNK 11-cell frame count left the original range");
        require(run.steady >= 25.0 && run.
steady <= 28.0, "MTNK steady speed left the original 26.7-27 range");
    }
    {
        // Real Rhino (HTNK): Speed=6 maps to 15, steady 15*256/163 = 23.6.
        auto world = field(30, 12);PrototypeMovement movement;movement.use_rules(table);
        put(world, 1, "HTNK", sw::Kind::Unit, 3, 6, 64);
        const auto run = straight_run(world, movement, 1, 11);
        std::cout << "HTNK 11 cells: frames=" << run.frames << " steady=" << run.steady
            << " leptons/frame = " << run.steady / 256.0 << " cells/frame" << std::endl;
        // Original measured: Rhino steady 0.093 cells (24 leptons) a frame.
        require(run.frames >= 110 && run.frames <= 135, "HTNK 11-cell frame count left the original range");
        require(run.steady >= 22.5 && run.steady <= 25.0, "HTNK steady speed left the original 23.6-24 range");
    }
    {
        // Synthetic accelerating type TNKD (Speed 7, Accelerates defaults
        // on): the fraction ramps at 0.03 a frame, so the same 11 cells
        // take longer than MTNK. A model check, not an original measurement.
        auto world = field(30, 12);PrototypeMovement movement;movement
.use_rules(table);
        put(world, 1, "TNKD", sw::Kind::Unit, 3, 6, 64);
        const auto run = straight_run(world, movement, 1, 11);
        std::cout << "TNKD 11 cells (accelerating): frames=" << run.frames << " steady=" << run.steady << std::endl;
        require(run.frames > 130, "accelerating type should pay the ramp cost");
        require(run.steady >= 25.0 && run.steady <= 28.0, "TNKD steady speed should reach the MTNK value");
    }
    {
        // A standing MTNK ordered behind itself turns 180 degrees in
        // ceil(32768 / (ROT 5 * 256)) = 26 frames (004B8E90). The original
        // measured about 1.2-1.7 s (25-35 frames) for the turn alone.
        auto world = field(30, 12);PrototypeMovement movement;movement.use_rules(table);
        put(world, 1, "MTNK", sw::Kind::Unit, 3, 6, 64);
        const auto frames = turn_frames(world, movement, 1, ra2map::Cell{1, 6});
        std::cout << "MTNK 180-degree turn: " << frames << " frames" << std::endl;
        require(frames >= 24 && frames <= 30, "180-degree turn left the 26-frame model and the 25-35 measured range");
    }
    {
        // GI (E1) and Conscript (E2): Speed=4 maps to 10 leptons a frame,
        // no acceleration (the original foot model has none). Original
        // measured: GI 7 cells in about 184 frames, 10 leptons a frame
        // (0.039 cells). The Conscript run was a bent path, so its measured
        // 0.036 cells a frame is noisy; the E2 bound below stays loose.
        auto world = field(30, 12);PrototypeMovement movement;movement.use_rules(table);
        put(world, 1, "E1", sw::Kind::Infantry, 3, 6, 64);
        const auto run1 = straight_run(world, movement, 1, 7);
        std::cout << "E1 7 cells: frames=" << run1.frames << " steady=" << run1.steady
            << " leptons/frame = " << run1.steady / 256.0 << " cells/frame" << std::endl;
        require(run1.frames >= 170 && run1.frames <= 200, "E1 7-cell frame count left the original range");
        require(run1.steady >= 9.0 && run1.steady <= 11.0, "E1 steady speed left the original 10-lepton range");
        auto world2 = field(30, 12);PrototypeMovement movement2;movement2.use_rules(table);
        put(world2, 2, "E2", sw::Kind::Infantry, 3, 9, 64);
        const auto run2 = straight_run(world2, movement2, 2, 7);
        std::cout << "E2 7 cells: frames=" << run2.frames << " steady=" << run2.steady << "leptons/frame" << std::endl;
        require(run2.frames >= 170 && run2.frames <= 200, "E2 7-cell frame count left the original range");
        require(run2.steady >= 9.0 && run2.
steady <= 11.0, "E2 steady speed left the original 10-lepton range");
    }
    std::cout << "prototype speed checks passed\n" << std::flush;
    return 0;
}
