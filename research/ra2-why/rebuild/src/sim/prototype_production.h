#pragma once
#include "production_runtime.h"
#include <utility>

// TODO-ORIGINAL: A playable adapter around the checked production slices.
// The caller owns one PrototypeComplete Snapshot and one FactorySet.
// These functions never publish the private phase used by checked slices.
namespace ra2::sim::production::prototype {
Outcome initialize(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t player_house);
Outcome deploy(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t unit_id,
               std::optional<bool> can_deploy=true);
std::vector<std::pair<std::string,CanBuild>> options(const session::world::Snapshot&,const FactorySet&,
                                                    const Catalog&,std::uint32_t house_id);
Outcome begin_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
                         const std::string& type,std::uint64_t frame);
Outcome hold_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
    const std::string& type,std::uint64_t frame);
Outcome cancel_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
    const std::string& type,std::uint64_t frame);
std::optional<bool> passes_proximity(const session::world::Snapshot&,const FactorySet&,const Catalog&,
                                     std::uint32_t house_id,const std::string& type,CellXY);
Outcome place(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
              const std::string& type,CellXY,std::uint64_t frame);
// Also places finished Units and Infantry in a free Cell near their factory.
Outcome tick(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint64_t frame);
}
