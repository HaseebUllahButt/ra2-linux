#include "weapons.h"
#include <sim/test_inputs.h>
#include <sim/world_init.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace ra2;
using namespace ra2::weapons;
using namespace ra2::sim;

int main(int argc, char **argv) {
  try {
    const char *game = std::getenv("RA2_GAME_DIR");
    if (!game || !*game) throw std::runtime_error("set RA2_GAME_DIR");
    if (argc != 2) throw std::runtime_error("expected --assets or --tick");
    if (std::string(argv[1]) == "--assets") {
      const auto cells = original_test_cells(game, "ALL02S.MAP", true);
      const auto catalog = original_test_catalog(game, "ALL02S.MAP", true);
      rules::IniFile map, rules;
      map.parse(original_test_ini(game, "ALL02S.MAP", true));
      rules.parse(original_test_ini(game, "RULES.INI", true));
      TerrainCells terrain;
      for (const auto &c : cells) {
        TerrainCell cell;
        cell.cell = {c.x, c.y}; cell.tile_index = c.tile;
        cell.resolved_tile_index = c.resolved; cell.subtile = c.subtile;
        cell.height_level = c.height < 128 ? int(c.height) : int(c.height) - 256;
        cell.overlay_type_index = c.overlay;
        terrain.push_back(cell);
      }
      TheaterInputs theater;
      theater.theater_name = catalog.name; theater.settings.parse(catalog.settings);
      for (const auto &range : catalog.ranges)
        theater.tile_sets.push_back({range.first, range.count});
      theater.resolved_metadata = [&catalog](std::uint32_t id, std::uint8_t sub)
          -> std::optional<TileMetadata> {
        const auto m = catalog.metadata(id, sub);
        if (!m) return {};
        return TileMetadata{m->terrain, m->ramp, m->valid};
      };
      const auto size = map.read_list("Map", "Size");
      const int width = *rules::IniFile::integer_value(size.at(2));
      const int height = *rules::IniFile::integer_value(size.at(3));
      const auto cliff = static_cast<std::uint8_t>(rules.read_integer("General", "CliffBackImpassability", 0));
      initialize_original_tile_pass(terrain, theater, width, height, cliff, true);
      initialize_original_tile_pass(terrain, theater, width, height, cliff, false);
      unsigned selected = 0;
      unsigned examined = 0;
      for (const auto &cell : terrain) {
        FlatCannonTickInput input{};
        input.arc.position = {int(cell.cell.x)*256+128, int(cell.cell.y)*256+128, 500};
        input.arc.velocity = {20, 0, -12}; input.arc.gravity = 6;
        if (!bind_flat_tick_cells(input, cell, cell)) continue;
        if (examined++ % 50 != 0) continue;
        std::cout << cell.cell.x << ' ' << cell.cell.y << ' ' << *cell.height_level
                  << ' ' << input.proposed_floor << '\n';
        if (++selected == 32) break;
      }
      if (selected != 32) throw std::runtime_error("too few resolved original flat cells");
      return 0;
    }
    if (std::string(argv[1]) != "--tick") throw std::runtime_error("unknown mode");
    const auto distance = OriginalDistance::from_executable(std::string(game)+"/game.exe");
    int x,y,level,floor,offset,source;
    while (std::cin >> x >> y >> level >> floor >> offset >> source) {
      TerrainCell cell; cell.cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
      cell.tile_replacement_resolved=true; cell.ramp_index=0; cell.height_level=level;
      cell.floor_world_height=floor; cell.bridge=false; cell.overlay_type_index=-1;
      FlatCannonTickInput input{};
      input.arc.position={x*256+128,y*256+128,floor+offset};
      input.arc.velocity={20,0,-12}; input.arc.gravity=6;
      input.target={100000,100000,0}; input.height_before=offset; input.height_after=offset-18;
      input.height_unit=104; input.source_id=source; input.proposed_building_id=0;
      input.target_building_id=0; input.building_intercepts=false; input.position_in_map=true;
      input.cell_objects=std::vector<ProjectileCellObject>{}; input.terrain.destination_cell_id=2;
      if (!bind_flat_tick_cells(input,cell,cell)) throw std::runtime_error("real cell did not bind");
      const auto missing = [&] { auto copy=cell; copy.bridge.reset(); auto old=input;
        if (bind_flat_tick_cells(input,copy,cell) || input.proposed_floor != old.proposed_floor)
          throw std::runtime_error("unknown bridge state accepted"); };
      missing();
      for (int guard = 0; guard < 3; ++guard) {
        auto unresolved = cell;
        if (guard == 0) unresolved.overlay_type_index = 4;
        if (guard == 1) unresolved.ramp_index = 1;
        if (guard == 2) unresolved.tile_replacement_resolved = false;
        if (bind_flat_tick_cells(input, cell, unresolved))
          throw std::runtime_error("unresolved terrain cell accepted");
      }
      const auto result=base_flat_cannon_tick(input,[](std::int16_t,std::int16_t) {
        return std::optional<TerrainTraceCell>(TerrainTraceCell{1,0,false,false,false});
      },distance);
      std::cout << static_cast<int>(result.status);
      for (auto v:result.state.position) std::cout << ' ' << v;
      for (auto v:result.state.velocity) std::cout << ' ' << v;
      std::cout << '\n';
    }
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
