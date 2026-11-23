#include "prototype_tech.h"
#include "../loop/profile.h"
#include "production_runtime.h"
#include "session/world/world_hash.h"
#include <cstdio>
#include <algorithm>
#include <limits>

namespace ra2::sim::tech {
namespace {
namespace w = session::world;
namespace p = production;
const w::House* house(const w::Snapshot& s, unsigned id) {
    for (const auto& h:s.houses) if(h.id==id) return &h;
    return nullptr;
}
bool live(const w::Entity& e) {
    return e.lifetime==w::Lifetime::Placed && !e.in_limbo.value_or(false) &&
        e.on_map.value_or(true) && e.resolved_hp.value_or(1)>0;
}
// game.exe 0x004F2CC0 (HouseClass::UpdatePower) recounts per house: it walks
// the house buildings, skips limbo/off-map ones, and adds each building's
// GetPowerOutput 0x00449D40 and GetPowerDrain 0x00449DD0.
HousePower recount(const rules::IniFile& r,const w::Snapshot& s,unsigned id) {
    std::int64_t output=0,drain=0;
    for(const auto& e:s.entities) if(e.house_id==id && e.kind==w::Kind::Structure && live(e)) {
        const int power=r.read_integer(e.type,"Power");
        if(power>0) {
            // 0x00449D40: power plants add Type Power= plus turbine upgrades
            // and powerups (not in the snapshot yet), then scale the sum by
            // Health/Strength 0x005D4540 and truncate with x87 F2I 0x0077F110.
            const int strength=std::max(1,r.read_integer(e.type,"Strength",1));
            const int hp=std::max(0,e.resolved_hp.value_or(strength));
            output+=(int)((double)hp/(double)strength*(double)power);
        } else if(power<0) {
            // 0x00449DD0: drain is Type Power= (negative in INI), never
            // health scaled, zero when the building is off or disabled.
            drain-=(std::int64_t)power;
        }
    }
    return {int(std::min(output,std::int64_t(INT32_MAX))),int(std::min(drain,std::int64_t(INT32_MAX)))};
}
}
double HousePower::fraction() const noexcept { return !low() || drain<=0 ? 1.0 : std::clamp(double(output)/drain,0.0,1.0); }
HousePower house_power(const rules::IniFile& r,const w::Snapshot& s,unsigned id) {
    const auto* h=house(s,id);
    if(h && h->power_output && h->power_drain) return {*h->power_output,*h->power_drain};
    return recount(r,s,id);
}
// game.exe 0x006C22A0 (TechnoClass::TimeToBuild, called from the per-house
// factory rate update 0x004B9EB0) computes the build clock speed with x87
// float arithmetic: speed = 1 - (1 - PowerFraction 0x004E8330) *
// LowPowerPenaltyModifier; speed = max(speed, MinLowPowerProductionSpeed);
// when the fraction is below 1 also speed = min(speed,
// MaxLowPowerProductionSpeed); a zero speed becomes 0.01. Build frames are
// F2I(cost / speed) 0x0077F110, and the factory clock Rate is frames/54
// clamped to 1..255 (0x004B9EB0). This returns the reciprocal so callers can
// divide build time by it.
double production_multiplier(const rules::IniFile& r,const HousePower& p) {
    const float frac=(float)p.fraction();
    const float mod=(float)r.read_double("General","LowPowerPenaltyModifier",1);
    float speed=1.f-(1.f-frac)*mod;
    const float lo=(float)r.read_double("General","MinLowPowerProductionSpeed",.5);
    if(speed<lo) speed=lo;
    if(frac<1.f) {
        const float hi=(float)r.read_double("General","MaxLowPowerProductionSpeed",.8);
        if(speed>hi) speed=hi;
    }
    if(speed==0.f) speed=0.01f;
    return 1.0/(double)speed;
}
bool defenses_online(const HousePower& p) { return !p.low(); }
bool building_online(const rules::IniFile& r,const HousePower& p,const std::string& type) {
    return !r.read_bool(type,"Powered",false) || !p.low();
}
std::string side_of(const rules::IniFile& r,const w::Snapshot& s,unsigned id) {
    const auto* h=house(s,id); const auto* sides=r.find_section("Sides");
    if(!h || !sides) return {};
    for(const auto& side:sides->entries) for(const auto& c:rules::IniFile::split_list(side.value))
        if(c==h->country) return side.key;
    return {};
}
BuildList buildable_types(const rules::IniFile& r,const w::Snapshot& s,unsigned id) {
    BuildList out; const auto* h=house(s,id); if(!h) return out;
    // The catalog depends only on this rules document. Build it once per
    // document revision instead of once per call.
    struct CatalogCache { std::uint64_t revision=0; std::optional<p::Catalog> catalog; };
    static thread_local CatalogCache cache;
    if(!cache.catalog || cache.revision!=r.revision()) { cache.catalog=p::Catalog::from(r,{},{}); cache.revision=r.revision(); }
    const auto& c=*cache.catalog;
    p::CanBuildHouse in;
    in.tech_level=h->tech_level.value_or(10);
    in.country=h->country_index;
    if(!in.country) for(unsigned i=0;i<c.countries.size();++i) if(c.countries[i]==h->country) in.country=int(i);
    if(!in.country || *in.country<0 || *in.country>=32) return out;
    in.stolen_allied=h->stolen_tech_205.value_or(false); in.stolen_soviet=h->stolen_tech_204.value_or(false);
    in.stolen_masks=std::array<std::uint32_t,4>{h->stolen_mask_208.value_or(0),h->stolen_mask_20c.value_or(0),h->stolen_mask_210.value_or(0),h->stolen_mask_214.value_or(0)};
    // TODO-ORIGINAL 004E3878: prototype AI uses human eligibility to avoid impossible orders.
    in.flag_134=true; in.flag_135=false;
    in.active_buildings=std::map<int,int>{};
    in.owned_buildings=in.active_buildings; in.owned_units=in.active_buildings;
    in.owned_infantry=in.active_buildings; in.owned_aircraft=in.active_buildings;
    in.built_buildings=h->buildings_ever_54a0.value_or(std::map<int,int>{});
    in.built_units=h->units_ever_54b0.value_or(std::map<int,int>{});
    in.built_infantry=h->infantry_ever_54c0.value_or(std::map<int,int>{});
    in.built_aircraft=h->aircraft_ever_54d0.value_or(std::map<int,int>{});
    in.factory_objects=std::vector<std::optional<std::pair<p::Rtti,int>>>{};
    in.buildings=std::vector<p::HouseBuilding>{}; in.infantry_c14_matches=0;
    std::vector<const p::TypeFacts*> factories;
    for(const auto& e:s.entities) if(e.house_id==id && live(e)) {
        const auto* t=c.find(e.type); if(!t) continue;
        auto* counts=&in.owned_units;
        if(e.kind==w::Kind::Structure) { counts=&in.owned_buildings; ++(*in.active_buildings)[t->heap_index]; if(t->factory) factories.push_back(t); }
        else if(e.kind==w::Kind::Infantry) counts=&in.owned_infantry;
        else if(e.kind==w::Kind::Aircraft) counts=&in.owned_aircraft;
        ++(**counts)[t->heap_index];
    }
    p::CanBuildRules cr; cr.super_weapons_allowed=true; cr.session_type=3; cr.generic_prerequisites=c.generic_prerequisites;
    for(const auto& b:c.buildings) cr.powers_up_building[b.heap_index]=b.powers_up;
    auto append=[&](const auto& types,auto& dest) {
        for(const auto& t:types) {
            if(!(t.owner & (1u<<*in.country))) continue;
            bool factory=false;
            for(const auto* f:factories) if(f->factory==t.rtti && (f->owner&t.owner) &&
                (t.rtti!=p::Rtti::UnitType || f->placement.naval==t.placement.naval)) factory=true;
            if(factory && p::original_can_build(t.can,in,cr,false,false)==p::CanBuild::Can) dest.push_back(t.id);
        }
    };
    append(c.buildings,out.buildings); append(c.units,out.units); append(c.infantry,out.infantry); append(c.aircraft,out.aircraft);
    return out;
}
bool can_build(const rules::IniFile& r,const w::Snapshot& s,unsigned id,const std::string& type) {
    const auto b=buildable_types(r,s,id);
    for(const auto* list:{&b.buildings,&b.units,&b.infantry,&b.aircraft}) if(std::find(list->begin(),list->end(),type)!=list->end()) return true;
    return false;
}
void update(const rules::IniFile& r,w::Snapshot& s) {
    {
        loop::profile::Scope probe(loop::profile::Phase::Tech);
        for(auto& h:s.houses) { const auto p=recount(r,s,h.id); h.power_output=p.output; h.power_drain=p.drain; }
    }
    // Benchmark proof: the match calls this once per game frame. Print a
    // world hash every 100 calls so two builds can be compared.
    if(loop::profile::enabled()) {
        const loop::profile::Exclude probe_only;
        static std::uint64_t calls=0;
        if(++calls%100==0) std::fprintf(stderr,"RA2_BENCH world call=%llu hash=%016llx\n",
            static_cast<unsigned long long>(calls),static_cast<unsigned long long>(w::world_hash(s)));
    }
}
bool radar_online(const rules::IniFile& r,const w::Snapshot& s,unsigned id) {
    if(house_power(r,s,id).low()) return false;
    for(const auto& e:s.entities) if(e.house_id==id && e.kind==w::Kind::Structure && live(e) && r.read_bool(e.type,"Radar")) return true;
    return false;
}
}
