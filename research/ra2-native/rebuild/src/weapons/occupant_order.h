#pragma once
#include <cstdint>
#include <array>
#include <map>
#include <vector>
namespace ra2::weapons {
// Base 0065F768..0065F7AB invokes these map loaders in this order. Each
// loader walks the original INI entry list and may reject individual objects.
enum class ScenarioObjectKind { Unit, Aircraft, Infantry, Structure };
std::array<ScenarioObjectKind, 4> base_map_placement_order();

// Experimental Cell +E4/+E8 and Object +30 link model. Zero is null.
// It models link writes only. It does not model occupancy, visibility,
// placement callbacks, object lifetime, or Simulation integration.
struct CellContentHeads { std::uint32_t ground_head=0, bridge_head=0; };
struct ContentObject { std::uint32_t next=0; int original_type=0; };
enum class ContentLayer { Ground, Bridge };
enum class ContentResult { Complete, CyclicInput };
struct ContentLinks {
  std::map<std::uint32_t, CellContentHeads> cells;
  std::map<std::uint32_t, ContentObject> objects;
  // Base 00473B30 and 00473D20. A missing ID is invalid. Null is ignored.
  ContentResult insert(std::uint32_t cell, ContentLayer, std::uint32_t object);
  ContentResult remove(std::uint32_t cell, ContentLayer, std::uint32_t object);
  // Base 0054AB70 and 0054AF70 link calls in supplied footprint order.
  // Caller resolves footprint, layer, original type +205 and virtual +74.
  // The caller invokes this only when +205 is nonzero and virtual +74 is 2.
  // Missing cells are skipped. Placement callbacks remain outside this model.
  // The original 32-entry copy reserves its last slot for the terminator.
  ContentResult place(const std::vector<std::uint32_t> &, ContentLayer,
                      std::uint32_t object, bool adding);
};
}
