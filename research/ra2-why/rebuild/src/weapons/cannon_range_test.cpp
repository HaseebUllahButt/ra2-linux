#include "cannon_range.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ra2::weapons;
int main() {
 try {
  const char* dir=std::getenv("RA2_GAME_DIR");if (!dir) throw std::runtime_error("RA2_GAME_DIR is required");
  auto distance=ra2::sim::OriginalDistance::from_executable(std::string(dir)+"/game.exe");
  CannonRangeInput in{};int blocked,elevation,bonus;
  while (std::cin >> in.origin.x >> in.origin.y >> in.origin.z >> in.target.x >> in.target.y >> in.target.z
         >> in.weapon_range >> in.minimum_range >> in.gravity >> blocked >> elevation >> bonus) {
   in.plain_cannon_and_ground=true;in.line_blocked=bool(blocked);in.subject_to_elevation=bool(elevation);
   in.first_elevation_query_resolved=true;in.arcing_elevation_bonus=bonus;
   const auto result=base_cannon_range(in,distance);
   std::cout << (result==CannonRangeStatus::InRange?1:result==CannonRangeStatus::OutOfRange?0:-1) << '\n';
  }
 } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
