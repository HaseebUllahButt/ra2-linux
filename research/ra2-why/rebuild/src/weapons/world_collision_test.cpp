#include <sim/damage_runtime.h>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace ra2;
int main(int argc, char **argv) {
  try {
    if (argc!=2 || std::string(argv[1])!="--object-collision") return 1;
    const char* dir=std::getenv("RA2_GAME_DIR");
    if (!dir) throw std::runtime_error("RA2_GAME_DIR is required");
    const auto distance=sim::OriginalDistance::from_executable(std::string(dir)+"/game.exe");
    sim::Coord pos; std::uint32_t source; int preserve, count;
    while (std::cin >> pos.x >> pos.y >> pos.z >> source >> preserve >> count) {
      session::world::Snapshot world;
      session::world::CellState cell; cell.allocated=true; cell.heads.emplace();
      std::map<std::uint32_t,sim::CollisionProperties> values;
      for (int i=0;i<count;++i) {
        session::world::Entity e; sim::Coord virt,stored; int eligible,allied;
        std::cin >> e.id >> virt.x >> virt.y >> virt.z >> stored.x >> stored.y >> stored.z >> eligible >> allied;
        if (world.entities.empty()) cell.heads->ground_head=e.id;
        else world.entities.back().next=e.id;
        e.lifetime=session::world::Lifetime::Placed; e.position=stored; e.next=0;
        // Keep different original categories in the chain.
        e.kind=i%2?session::world::Kind::Structure:session::world::Kind::Unit;
        values.emplace(e.id,sim::CollisionProperties{bool(eligible),virt,bool(allied)});
        world.entities.push_back(e);
      }
      if (source && !values.count(source)) {
        session::world::Entity e; e.id=source; e.next=0; world.entities.push_back(e);
      }
      world.cells.emplace(0,cell);
      const auto lookup=[&](const session::world::Entity& e,std::uint32_t) -> std::optional<sim::CollisionProperties> {
        const auto i=values.find(e.id); return i==values.end()?std::nullopt:std::optional(i->second);
      };
      const auto result=sim::base_world_projectile_collision(world,0,pos,source,bool(preserve),lookup,distance);
      if (!result.collision) throw std::runtime_error(result.missing);
      auto c=*result.collision;
      std::cout << c.selected_id << ' ' << c.collision << ' ' << c.position.x << ' '
                << c.position.y << ' ' << c.position.z << '\n';
      if (world.random.first!=0 || world.random.second!=103 || world.cells.at(0).heads->ground_head!=cell.heads->ground_head)
        throw std::runtime_error("collision mutated snapshot");
      if (count) {
        auto incomplete=world; incomplete.entities[0].next.reset();
        if (sim::base_world_projectile_collision(incomplete,0,pos,source,bool(preserve),lookup,distance).collision)
          throw std::runtime_error("missing next link accepted");
        incomplete=world; incomplete.pending_cell_setup=true;
        if (sim::base_world_projectile_collision(incomplete,0,pos,source,bool(preserve),lookup,distance).collision)
          throw std::runtime_error("pending setup accepted");
        incomplete=world; incomplete.entities[0].next=incomplete.entities[0].id;
        if (sim::base_world_projectile_collision(incomplete,0,pos,source,bool(preserve),lookup,distance).collision)
          throw std::runtime_error("cyclic chain accepted");
      }
    }
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
