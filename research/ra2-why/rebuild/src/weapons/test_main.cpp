#include "weapons.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ra2::weapons;
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--angle") {
      auto tables = BaseBallisticAngles::from_executable("/path/to/user/ra2/game/game.exe");
      int speed, distance, height, alternate;
      double gravity;
      while (std::cin >> speed >> distance >> height >> gravity >> alternate) {
        auto angle = tables.angle(speed, distance, height, gravity, alternate != 0);
        std::cout << (angle ? int(*angle) : -1) << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--impact") {
      int hit, height, unit;
      while (std::cin >> hit >> height >> unit) {
        CannonImpactInputs input;
        if (hit >= 0) input.terrain_trace_hit = hit != 0;
        input.height_above_floor = height;
        input.original_height_unit = unit;
        std::cout << static_cast<int>(base_cannon_impact(input)) << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--fire") {
      auto table = ra2::sim::OriginalDistance::from_executable("/path/to/user/ra2/game/game.exe");
      ResolvedTankFire input{};
      input.source_id = 0x1001000;
      input.target_id = 0x1002000;
      input.projectile_type_id = 0x1003000;
      input.warhead_type_id = 0x1004000;
      int flag;
      while (std::cin >> input.muzzle[0] >> input.muzzle[1] >> input.target[0] >>
             input.target[1] >> input.damage >> input.gravity >> flag) {
        input.projectile_flag = static_cast<std::uint8_t>(flag);
        auto result = base_resolved_tank_fire(input, table);
        std::cout << result.source_id << ' ' << result.target_id << ' '
                  << result.projectile_type_id << ' ' << result.warhead_type_id << ' '
                  << result.damage << ' ' << result.horizontal_distance << ' '
                  << result.speed << ' ' << int(result.projectile_flag) << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--speed") {
      auto table = ra2::sim::OriginalDistance::from_executable("/path/to/user/ra2/game/game.exe");
      int distance; double gravity;
      while (std::cin >> distance >> gravity)
        std::cout << base_ballistic_launch_speed(distance, gravity, table) << '\n';
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--arc") {
      ArcInput input{};
      std::cout << std::setprecision(17);
      while (std::cin >> input.position[0] >> input.position[1] >> input.position[2]
                      >> input.velocity[0] >> input.velocity[1] >> input.velocity[2]
                      >> input.gravity) {
        auto result = base_arc_proposal(input);
        for (auto v : result.position) std::cout << v << ' ';
        for (auto v : result.velocity) std::cout << v << ' ';
        std::cout << '\n';
      }
      return 0;
    }
    ra2::sim::CandidatePrefix prefix;
    if (assess_target_for_scan(prefix).status != ra2::sim::CandidateStatus::NeedsInput)
      throw std::runtime_error("missing firing checks accepted");
    ra2::sim::DamageInputs damage;
    damage.damage = 90;
    damage.distance = 0;
    damage.maximum = 10000;
    damage.cell_spread = 0.3f;
    damage.percent_at_max = 0.5f;
    damage.armor_verse = .25;
    auto event = resolved_damage_event(123, damage);
    if (event.target_id != 123 || event.amount != 22)
      throw std::runtime_error("incorrect resolved damage event");
    try {
      resolved_damage_event(0, damage);
      throw std::runtime_error("accepted target ID zero");
    } catch (const std::invalid_argument &) {}
    try {
      base_arc_proposal({{0, 0, 0}, {1, 2, 3}, -1});
      throw std::runtime_error("accepted negative gravity");
    } catch (const std::invalid_argument &) {}
    try {
      BaseBallisticAngles::from_executable("/path/to/user/ra2/game/gamemd.exe");
      throw std::runtime_error("accepted the wrong angle table edition");
    } catch (const std::runtime_error &error) {
      if (std::string(error.what()) == "accepted the wrong angle table edition")
        throw;
    }
    {
      auto tables = BaseBallisticAngles::from_executable("/path/to/user/ra2/game/game.exe");
      try {
        (void)tables.angle(200, 100, 0, 3, true);
        throw std::runtime_error("accepted unverified subunit square root");
      } catch (const std::range_error &) {}
    }
    std::cout << "Firing prefix, damage event and invalid input checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
