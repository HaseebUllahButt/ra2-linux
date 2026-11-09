#include "fire_timer.h"
#include <cstdint>
#include <iostream>
using namespace ra2;
int main() {
 std::uint32_t seed; int warmup; weapons::SingleShotDelayInput input{};
 while (std::cin >> seed >> warmup >> input.rate_of_fire >> input.house_multiplier
        >> input.veteran >> input.elite >> input.veteran_rof >> input.elite_rof >> input.veteran_multiplier) {
  sim::Random random(seed); for (int j=0;j<warmup;++j) random.next();
  const int delay=weapons::base_single_shot_unit_delay(input,random);
  auto state=random.state();
  std::cout << delay << ' ' << state.first << ' ' << state.second;
  for (auto word:state.table) std::cout << ' ' << word;
  std::cout << '\n';
 }
}
