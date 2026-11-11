#pragma once
#include "fp.h"
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <limits>
#include <string>

namespace ra2::sim {
// Bounded base game.exe 006CED90 / 00713360. No scores, callbacks or
// spawned-missile parent credit. These functions never invent missing actors.
struct KillPromotion {
    std::uint32_t killer_id=0;
    std::optional<double> value;
    std::string missing;
};
// 006DAD30 stores the country factor as float before the 53-bit multiply.
inline float base_promotion_float(double value) {
    fp::domain(value);
    float result=static_cast<float>(value);
    if (!std::isfinite(result)) throw std::range_error("promotion country factor is out of range");
    if ((value>0 && result>value) || (value<0 && result<value))
        result=std::nextafter(result,0.0f);
    return result;
}
inline int base_promotion_price(int cost,float factor) {
    const double price=fp::multiply(cost,factor);
    if (price<1 || price>std::numeric_limits<int>::max())
        throw std::range_error("promotion price is outside the positive slice");
    return static_cast<int>(price); // 0077F110 with original control word E7F.
}
inline double base_kill_gain(double value,int killer_price,int victim_price,
    double victim_rank,double ratio,double cap) {
    if (killer_price<=0 || victim_price<=0 || ratio<=0 || cap<0)
        throw std::range_error("promotion arithmetic is outside the positive slice");
    fp::domain(value);fp::domain(victim_rank);fp::domain(ratio);fp::domain(cap);
    const int factor=victim_rank>=2 ? 3 : victim_rank>=1 ? 2 : 1;
    if (victim_price>std::numeric_limits<int>::max()/factor)
        throw std::range_error("promotion credit would overflow");
    const double next=fp::add(value,fp::divide(victim_price*factor,
        fp::multiply(killer_price,ratio)));
    return next<cap ? next : cap;
}
// Resolve before removal, then commit only after removal succeeds. The
// prototype has no intervening death callbacks. Repeated impacts cannot
// award again because destroyed actors cannot enter its damage path.
inline KillPromotion base_prepare_kill_promotion(const session::world::Snapshot& world,
    const rules::IniFile& rules,std::uint32_t source_id,std::uint32_t target_id) {
    using session::world::Kind;
    using session::world::Edition;
    using WorldEntity=session::world::Entity;
    using WorldHouse=session::world::House;
    auto missing=[](const char* s) {return KillPromotion{0,{},s};};
    if (!source_id) return {};
    if (world.edition!=Edition::Base) return missing("kill promotion needs base game state");
    const WorldEntity *source=nullptr,*target=nullptr;
    for (const auto& e:world.entities) {
        if(e.id==source_id) {if(source)return missing("kill promotion source ID repeats");source=&e;}
        if(e.id==target_id) {if(target)return missing("kill promotion victim ID repeats");target=&e;}
    }
    if(!source || !target) return missing("kill promotion actor is unknown");
    const auto ordinary=[](Kind k){return k==Kind::Infantry || k==Kind::Unit;};
    if(!ordinary(source->kind) || !ordinary(target->kind))
        return missing("kill promotion actor kind is outside the ordinary ground slice");
    if(!rules.find_section(source->type) || !rules.find_section(target->type))
        return missing("kill promotion type is unknown");
    // 006D9B40: Trainable=true, Passengers=0 (+52C), MissileSpawn=false
    // (+B18). Infantry +C00 defaults false in 00509B20; special death
    // types are excluded rather than treating their prototype HP as proof.
    if(rules.read_bool(source->type,"MissileSpawn",false) ||
       rules.read_bool(target->type,"MissileSpawn",false))
        return missing("kill promotion missile parent state is unrepresented");
    for(const auto* actor:{source,target}) {
        if(rules.read_integer(actor->type,"Passengers",0)!=0)
            return missing("kill promotion passenger state is unrepresented");
        if(rules.read_bool(actor->type,"Cyborg",false) ||
           rules.read_bool(actor->type,"NotHuman",false))
            return missing("kill promotion special death state is unrepresented");
    }
    if(!target->has_trigger || *target->has_trigger || !target->active || !*target->active)
        return missing("kill promotion victim death callback state is unknown or unsupported");
    if(!rules.read_bool(source->type,"Trainable",true)) return {};
    const WorldHouse *attacker=nullptr,*victim=nullptr;
    for(const auto& h:world.houses) {
        if(h.id==source->house_id) {if(attacker)return missing("kill promotion house ID repeats");attacker=&h;}
        if(h.id==target->house_id) {if(victim)return missing("kill promotion house ID repeats");victim=&h;}
    }
    if(!attacker || !victim) return missing("kill promotion house is unknown");
    // 004E5580 is directed from killer house toward victim house.
    if(attacker==victim) return {};
    if(!attacker->original_index || !victim->original_index)
        return missing("kill promotion house index is unknown");
    if(*attacker->original_index==*victim->original_index) return {};
    if(*victim->original_index!=-1) {
        if(!attacker->alliance_mask_5674) return missing("kill promotion alliance is unknown");
        if(*attacker->alliance_mask_5674 & (1u<<(static_cast<unsigned>(*victim->original_index)&31u))) return {};
    }
    if(victim->country.empty() || !rules.find_section(victim->country))
        return missing("kill promotion victim country is unknown");
    if(!source->veterancy || !target->veterancy)
        return missing("kill promotion rank is unknown");
    if(!rules.contains(source->type,"Cost") || !rules.contains(target->type,"Cost") ||
       !rules.contains("General","VeteranRatio") || !rules.contains("General","VeteranCap"))
        return missing("kill promotion price or general rules are unknown");
    try {
        // 004F90F0 initializes cost factors to float 1; 004F9550 loads
        // country overrides. Both virtual price calls use the VICTIM house.
        const auto price=[&](const WorldEntity& e) {
            const char* key=e.kind==Kind::Infantry ? "CostInfantryMult" : "CostUnitsMult";
            return base_promotion_price(rules.read_integer(e.type,"Cost",0),
                base_promotion_float(rules.read_double(victim->country,key,1.0)));
        };
        return {source_id,base_kill_gain(*source->veterancy,price(*source),price(*target),
            *target->veterancy,rules.read_double("General","VeteranRatio",0),
            rules.read_double("General","VeteranCap",0)),{}};
    } catch(const std::exception&) {
        return missing("kill promotion arithmetic is outside the verified slice");
    }
}
inline void base_commit_kill_promotion(session::world::Snapshot& world,const KillPromotion& plan) {
    if(plan.value) for(auto& e:world.entities) if(e.id==plan.killer_id) {e.veterancy=*plan.value;return;}
}
}
