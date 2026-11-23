#include "weapons.h"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ra2::weapons;

int main() {
  try {
    const char *game_dir = std::getenv("RA2_GAME_DIR");
    if (!game_dir || !*game_dir)
      throw std::runtime_error("set RA2_GAME_DIR to the original game directory");
    const std::string exe = std::string(game_dir) + "/game.exe";
    const auto angles = BaseBallisticAngles::from_executable(exe);
    const auto distance = ra2::sim::OriginalDistance::from_executable(exe);
    std::array<std::int32_t, 3> displacement;
    while (std::cin >> displacement[0] >> displacement[1] >> displacement[2]) {
      const ra2::sim::Coord origin{0, 0, 0};
      const ra2::sim::Coord dest{displacement[0], displacement[1], 0};
      const int range = distance.between(origin, dest, true);
      const int speed = base_ballistic_launch_speed(range, 6, distance);
      const auto launch = angles.tank_launch(speed, displacement, 6, false);
      if (!launch) throw std::runtime_error("launch solution unavailable");
      std::cout << std::setprecision(17) << speed << ' ' << launch->limited_speed
                << ' ' << launch->heading << ' ' << launch->pitch;
      for (double v : launch->velocity) std::cout << ' ' << v;
      std::cout << '\n';
      FlatCannonTickInput tick{};
      tick.arc = {{0, 0, 2000}, launch->velocity, 6};
      tick.target = {100000, 100000, 0};
      tick.height_unit = 104;
      tick.height_before = 500;
      tick.height_after = 500;
      tick.source_id = 1;
      tick.proposed_building_id = 0;
      tick.target_building_id = 0;
      tick.building_intercepts = false;
      tick.position_in_map = true;
      tick.cell_objects = std::vector<ProjectileCellObject>{};
      tick.terrain.destination_cell_id = 2;
      const TerrainTraceLookup lookup = [](std::int16_t, std::int16_t) {
        return std::optional<TerrainTraceCell>(TerrainTraceCell{1, 0, false, false, false});
      };
      for (int i = 0; i < 8; ++i) {
        const auto result = base_flat_cannon_tick(tick, lookup, distance);
        std::cout << static_cast<int>(result.status);
        for (auto p : result.state.position) std::cout << ' ' << p;
        for (auto v : result.state.velocity) std::cout << ' ' << v;
        std::cout << '\n';
        if (result.status != FlatCannonTickStatus::Continue)
          throw std::runtime_error("unexpected unresolved tick");
        tick.arc.position = result.state.position;
        tick.arc.velocity = result.state.velocity;
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
