#include "detonation.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace ra2;
int main(int argc,char** argv) {
 if (argc!=2) return 1;
 if (std::string(argv[1])=="--rng") {
  unsigned seed;int warmup,active;
  while (std::cin >> seed >> warmup >> active) {
   sim::Random rng(seed);for (int i=0;i<warmup;++i) rng.next();
   if (!weapons::base_complete_single_cluster_scatter(bool(active),rng)) return 1;
   auto state=rng.state();std::cout << state.first << ' ' << state.second;
   for (auto word:state.table) std::cout << ' ' << word;
   std::cout << '\n';
  }
 } else if (std::string(argv[1])=="--position") {
  const char* dir=std::getenv("RA2_GAME_DIR");if (!dir) return 1;
  auto distance=sim::OriginalDistance::from_executable(std::string(dir)+"/game.exe");
  weapons::UnitCannonImpactInput in{};sim::Coord target;int present;
  while (std::cin >> in.impact.x >> in.impact.y >> in.impact.z >> present >> target.x >> target.y >> target.z) {
   in.target_present=bool(present);in.unit_position=target;in.ordinary_ground_target=true;
   auto p=weapons::base_unit_cannon_impact_position(in,distance);if (!p) return 1;
   std::cout << p->x << ' ' << p->y << ' ' << p->z << '\n';
  }
 } else return 1;
}
