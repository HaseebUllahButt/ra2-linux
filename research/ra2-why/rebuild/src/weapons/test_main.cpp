#include "weapons.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ra2::weapons;
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--trace-full") {
      int cliffs, walls, sample_floor, same, wall, rule, owner, allied;
      TerrainTraceInput input{};
      while (std::cin >> cliffs >> walls >> sample_floor >> input.reference_floor >>
             input.source_floor >> same >> wall >> input.source_level >>
             input.destination_level >> rule >> owner >> allied >> input.position[0] >>
             input.position[1] >> input.position[2]) {
        input.destination_cell_id = same ? 1 : 2;
        input.checks_cliffs = cliffs;
        input.checks_walls = walls;
        input.allied_wall_rule = rule;
        int cell_x = 0, cell_y = 0;
        const auto result = base_trace_terrain(input, [&](std::int16_t x, std::int16_t y) {
          cell_x = x; cell_y = y;
          return std::optional<TerrainTraceCell>(
              TerrainTraceCell{1, sample_floor, wall != 0, owner != 0, allied != 0});
        });
        std::cout << (result.status == TerrainTraceStatus::Hit) << ' '
                  << cell_x << ' ' << cell_y << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--trace-first") {
      int cliffs, walls, same, wall, rule, owner, allied;
      TerrainTraceFirstCell input{};
      while (std::cin >> cliffs >> walls >> input.sample_floor >>
             input.reference_floor >> input.source_floor >> same >> wall >>
             input.source_level >> input.destination_level >> rule >> owner >> allied) {
        input.checks_cliffs = cliffs;
        input.checks_walls = walls;
        input.sample_is_destination = same;
        input.sample_has_wall = wall;
        input.allied_wall_rule = rule;
        input.sample_has_owner = owner;
        input.owner_allied = allied;
        std::cout << base_trace_first_cell_candidate(input) << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--impact-gate") {
      int damage, blocked, warhead;
      while (std::cin >> damage >> blocked >> warhead)
        std::cout << base_area_impact_enabled(damage, blocked, warhead) << '\n';
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--recipient") {
      int active, receives, limbo, immune, group, flag, special_virtual, half;
      ResolvedImpactRecipient input{};
      input.target_id = 0x1001000;
      input.attacker_id = 0x1002000;
      input.warhead_id = 0x1003000;
      while (std::cin >> input.raw_damage >> input.distance >> input.radius >>
             input.original_type >> input.health >> active >> receives >> limbo >>
             immune >> group >> flag >> special_virtual >> half) {
        input.active = active;
        input.receives_damage = receives;
        input.in_limbo = limbo;
        input.type6_immune = immune;
        input.special_group = group;
        input.special_flag = flag;
        input.special_virtual = special_virtual;
        input.type2_virtual_54 = half;
        const auto request = base_resolved_impact_recipient(input);
        if (request)
          std::cout << request->raw_damage << ' ' << request->distance << '\n';
        else
          std::cout << "none\n";
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--lobber") {
      auto table = ra2::sim::OriginalDistance::from_executable("/path/to/user/ra2/game/game.exe");
      int present, lobber, target_present;
      LobberInputs input;
      ra2::sim::Coord target;
      while (std::cin >> present >> lobber >> target_present >> input.shooter.x >>
             input.shooter.y >> input.shooter.z >> target.x >> target.y >> target.z) {
        input.weapon_present = present != 0;
        input.lobber = lobber != 0;
        input.current_target = target_present ? std::optional<ra2::sim::Coord>(target) : std::nullopt;
        std::cout << base_lobber_branch(input, table) << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--tank-launch") {
      auto tables = BaseBallisticAngles::from_executable("/path/to/user/ra2/game/game.exe");
      int speed, alternate; double gravity;
      std::array<std::int32_t, 3> displacement{};
      std::cout << std::setprecision(17);
      while (std::cin >> speed >> displacement[0] >> displacement[1] >> displacement[2] >> gravity >> alternate) {
        auto result = tables.tank_launch(speed, displacement, gravity, alternate != 0);
        if (!result) { std::cout << "none\n"; continue; }
        std::cout << result->limited_speed << ' ' << result->heading << ' ' << result->pitch << ' ';
        for (auto value : result->velocity) std::cout << value << ' ';
        std::cout << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--heading-angle") {
      auto tables = BaseBallisticAngles::from_executable("/path/to/user/ra2/game/game.exe");
      std::int32_t x, y;
      while (std::cin >> x >> y) std::cout << tables.heading(x, y) << '\n';
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--limit-speed") {
      auto table = ra2::sim::OriginalDistance::from_executable("/path/to/user/ra2/game/game.exe");
      int speed; std::array<std::int32_t, 3> displacement{};
      while (std::cin >> speed >> displacement[0] >> displacement[1] >> displacement[2])
        std::cout << base_limit_initial_speed(speed, displacement, table) << '\n';
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--heading") {
      auto tables = BaseBallisticAngles::from_executable("/path/to/user/ra2/game/game.exe");
      int speed, heading;
      std::cout << std::setprecision(17);
      while (std::cin >> speed >> heading) {
        auto result = tables.horizontal_velocity(speed, static_cast<std::uint16_t>(heading));
        for (auto value : result) std::cout << value << ' ';
        std::cout << '\n';
      }
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--pitch") {
      auto tables = BaseBallisticAngles::from_executable("/path/to/user/ra2/game/game.exe");
      std::array<double, 3> velocity{};
      int pitch;
      std::cout << std::setprecision(17);
      while (std::cin >> velocity[0] >> velocity[1] >> velocity[2] >> pitch) {
        auto result = tables.pitch_velocity(velocity, static_cast<std::uint16_t>(pitch));
        for (auto value : result) std::cout << value << ' ';
        std::cout << '\n';
      }
      return 0;
    }
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
    {
      TerrainTraceInput input{};
      input.destination_cell_id = 1;
      auto missing = base_trace_terrain(input, [](std::int16_t, std::int16_t)
          -> std::optional<TerrainTraceCell> { return std::nullopt; });
      if (missing.status != TerrainTraceStatus::NeedsCell || missing.cell_id != 0)
        throw std::runtime_error("unresolved terrain cell accepted");
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
