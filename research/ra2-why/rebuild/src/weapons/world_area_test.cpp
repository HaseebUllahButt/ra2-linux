#include <sim/damage_runtime.h>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace ra2;
int main() {
 try {
  const char* dir=std::getenv("RA2_GAME_DIR");
  if (!dir) throw std::runtime_error("RA2_GAME_DIR is required");
  const auto dist=sim::OriginalDistance::from_executable(std::string(dir)+"/game.exe");
  sim::Coord impact; float spread; std::uint32_t source; int count;
  while (std::cin >> impact.x >> impact.y >> impact.z >> spread >> source >> count) {
   session::world::Snapshot world;
   const int cx=impact.x/256,cy=impact.y/256;
   for (int dy=-2;dy<=2;++dy) for (int dx=-2;dx<=2;++dx) {
    session::world::CellState cell; cell.allocated=true; cell.heads.emplace(); cell.cell_flag_100=false;
    cell.terrain.emplace(); cell.terrain->floor_world_height=0; cell.terrain->ramp_index=0; cell.terrain->overlay_type_index=-1;
    world.cells.emplace((cy+dy)*512+cx+dx,std::move(cell));
   }
   std::map<std::uint32_t,sim::AreaUnitProperties> props;
   for (int j=0;j<count;++j) {
    session::world::Entity e; sim::Coord p; int dx,dy,active;
    std::cin >> e.id >> dx >> dy >> p.x >> p.y >> p.z >> active;
    auto &head=world.cells.at((cy+dy)*512+cx+dx).heads->ground_head;
    e.kind=session::world::Kind::Unit; e.lifetime=session::world::Lifetime::Placed;
    e.next=head; e.position=p; head=e.id;
    world.entities.push_back(e); props.emplace(e.id,sim::AreaUnitProperties{bool(active),p,false,false,false});
   }
   if (source && !props.count(source)) { session::world::Entity e; e.id=source; world.entities.push_back(e); }
   const auto lookup=[&](const session::world::Entity& e)->std::optional<sim::AreaUnitProperties> {
    const auto i=props.find(e.id);return i==props.end()?std::nullopt:std::optional(i->second);
   };
   const auto r=sim::base_small_ground_area_candidates(world,impact,spread,source,false,lookup,dist);
   if (!r.candidates) throw std::runtime_error(r.missing);
   std::cout << r.candidates->size(); for (const auto& c:*r.candidates) std::cout << ' ' << c.id << ' ' << c.distance;
   std::cout << '\n';
   if (count) {
    auto bad=world; bad.entities[0].next.reset();
    // This object may be outside the one-cell spread; run this check for the nine-cell slice.
    if (spread>0.01f && spread<=0.5f &&
        sim::base_small_ground_area_candidates(bad,impact,spread,source,false,lookup,dist).candidates)
      throw std::runtime_error("missing area link accepted");
   }
  }
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
