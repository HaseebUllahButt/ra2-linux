#include "occupant_order.h"
#include <set>
#include <stdexcept>
namespace ra2::weapons {
std::array<ScenarioObjectKind, 4> base_map_placement_order() {
  return {ScenarioObjectKind::Unit, ScenarioObjectKind::Aircraft,
          ScenarioObjectKind::Infantry, ScenarioObjectKind::Structure};
}
namespace {
std::uint32_t &head(CellContentHeads &cell, ContentLayer layer) {
  return layer == ContentLayer::Ground ? cell.ground_head : cell.bridge_head;
}
ContentObject &node(ContentLinks &links, std::uint32_t id) {
  const auto it = links.objects.find(id);
  if (!id || it == links.objects.end())
    throw std::invalid_argument("unknown occupant object ID");
  return it->second;
}
}
ContentResult ContentLinks::insert(std::uint32_t id, ContentLayer layer,
                                   std::uint32_t object) {
  auto &cell = cells.at(id);
  if (!object) return ContentResult::Complete;
  auto &item = node(*this, object);
  auto &first = head(cell, layer);
  if (item.original_type == 6 && first) {
    std::set<std::uint32_t> seen;
    auto tail = first;
    while (true) {
      if (!seen.insert(tail).second) return ContentResult::CyclicInput;
      const auto next = node(*this, tail).next;
      if (!next) break;
      tail = next;
    }
    node(*this, tail).next = object;
    item.next = 0;
  } else if (!first || node(*this, first).next != object) {
    item.next = first;
    first = object;
  }
  return ContentResult::Complete;
}
ContentResult ContentLinks::remove(std::uint32_t id, ContentLayer layer,
                                   std::uint32_t object) {
  auto &cell = cells.at(id);
  if (!object) return ContentResult::Complete;
  auto &item = node(*this, object);
  auto &first = head(cell, layer);
  if (first == object) {
    first = item.next;
    item.next = 0;
    return ContentResult::Complete;
  }
  std::set<std::uint32_t> seen;
  auto current = first;
  while (current) {
    if (!seen.insert(current).second) return ContentResult::CyclicInput;
    auto &link = node(*this, current).next;
    if (link == object) {
      link = item.next;
      item.next = 0;
      break;
    }
    current = link;
  }
  return ContentResult::Complete;
}
ContentResult ContentLinks::place(const std::vector<std::uint32_t> &footprint,
                                  ContentLayer layer, std::uint32_t object,
                                  bool adding) {
  std::size_t index = 0;
  for (const auto cell : footprint) {
    if (index++ == 31) break;
    if (!cells.count(cell)) continue;
    const auto result = adding ? insert(cell, layer, object)
                               : remove(cell, layer, object);
    if (result != ContentResult::Complete) return result;
  }
  return ContentResult::Complete;
}
}
