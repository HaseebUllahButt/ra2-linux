#include "object_damage.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ra2::weapons;
int main(int argc, char **argv) {
  try {
    if (argc==2 && std::string(argv[1])=="--death") {
      UnitDeathBranchInput input{}; int active;
      while (std::cin >> input.type_field_bb8 >> input.unit_field_5c0 >> input.health >> active) {
        input.active=active;
        const auto result=base_unit_death_branch(input);
        std::cout << static_cast<int>(result.branch) << ' ' << result.unit_field_5c0
                  << ' ' << result.health << ' ' << result.active << '\n';
      }
      return 0;
    }
    ObjectDamageInput in{}; int bypass,immune,resolved;
    while (std::cin >> in.health >> in.maximum_health >> in.raw_damage >> bypass >> immune >> in.condition_red >> resolved) {
      in.bypass_armor=bypass; in.immune=immune; in.resolved_damage=resolved;
      const auto plan=base_object_damage_prefix(in);
      std::cout << static_cast<int>(plan.stage) << ' ' << plan.health << ' '
                << plan.damage << ' ' << plan.severity << '\n';
    }
    in={100,100,10,false,false,0.25,std::nullopt};
    if (base_object_damage_prefix(in).stage!=ObjectDamageStage::NeedsArmor)
      throw std::runtime_error("unknown armor was accepted");
    return 0;
  } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
