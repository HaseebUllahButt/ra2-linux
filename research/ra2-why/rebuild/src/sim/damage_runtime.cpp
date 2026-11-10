#include "damage_runtime.h"
#include "../loop/profile.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <cstdio>
#include <iostream>
#include <cstdlib>

namespace ra2::sim {
WorldCollisionResult base_world_projectile_collision(
    const session::world::Snapshot& world, std::uint32_t cell_index,
    Coord position, std::uint32_t source_id, bool preserve,
    const CollisionLookup& resolve, const OriginalDistance& distance) {
    auto missing=[](const char* reason) { return WorldCollisionResult{{},reason}; };
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup)
        return missing("base world setup is incomplete");
    if (world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second)
        return missing("random state indexes are invalid");
    if (!resolve) return missing("collision resolver is missing");
    const auto cell=world.cells.find(cell_index);
    if (cell==world.cells.end() || !cell->second.allocated || !*cell->second.allocated ||
        !cell->second.heads)
        return missing("allocated collision cell or its ordered heads are unknown");
    std::map<std::uint32_t,const session::world::Entity*> entities;
    for (const auto& entity:world.entities)
        if (!entity.id || !entities.emplace(entity.id,&entity).second)
            return missing("object IDs are invalid");
    if (source_id && !entities.count(source_id)) return missing("projectile source is unknown");
    std::set<std::uint32_t> seen;
    std::vector<weapons::ProjectileCellObject> candidates;
    std::map<std::uint32_t,CollisionProperties> properties;
    for (auto id=cell->second.heads->ground_head; id;) {
        const auto it=entities.find(id);
        if (it==entities.end() || !seen.insert(id).second ||
            it->second->lifetime!=session::world::Lifetime::Placed)
            return missing("ordered ground object chain is invalid");
        const auto& entity=*it->second;
        if (!entity.next) return missing("object next link is unknown");
        auto p=resolve(entity,source_id);
        if (!p || !p->eligible) return missing("object collision flag is unknown");
        if (*p->eligible && !p->virtual_position)
            return missing("object virtual position is unknown");
        properties.emplace(id,*p);
        candidates.push_back({id,p->virtual_position.value_or(Coord{}),
                              entity.position.value_or(Coord{}),*p->eligible,false});
        id=*entity.next;
    }
    // Resolve the selected candidate before asking for its alliance and stored
    // position. The original selects first, then checks the source/alliance.
    auto selected=weapons::base_projectile_object_collision(position,source_id,
                                                               preserve,candidates,distance);
    if (!selected.selected_id || selected.selected_id==source_id)
        return {selected,{}};
    const auto* object=entities.at(selected.selected_id);
    auto& p=properties.at(selected.selected_id);
    if (source_id) {
        if (!p.allied_to_source) return missing("selected object alliance is unknown");
        if (*p.allied_to_source) {
            selected.collision=false; selected.position=position;
            return {selected,{}};
        }
    }
    if (!object->position) return missing("selected object stored position is unknown");
    for (auto& candidate:candidates)
        if (candidate.id==selected.selected_id) candidate.stored_position=*object->position;
    return {weapons::base_projectile_object_collision(position,source_id,preserve,
                                                       candidates,distance),{}};
}
}

#include "fp.h"
#include <array>
#include <cmath>

namespace ra2::sim {
AreaCollectionResult base_small_ground_area_candidates(
    const session::world::Snapshot& world, Coord impact, float spread,
    std::uint32_t source_id, bool type_filter,
    const AreaUnitLookup& resolve, const OriginalDistance& distance) {
    auto missing=[](const char* reason) { return AreaCollectionResult{{},reason}; };
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup ||
        world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second)
        return missing("base world setup or random state is incomplete");
    if (!resolve || !std::isfinite(spread) || spread<0 || spread>0.5f)
        return missing("area inputs are outside the recovered small-spread slice");
    const auto cx=impact.x/256, cy=impact.y/256;
    if (cx<0 || cx>=512 || cy<0 || cy>=512)
        return missing("impact needs the original fallback cell");
    const auto center=world.cells.find(static_cast<unsigned>(cy*512+cx));
    if (center==world.cells.end() || !center->second.terrain ||
        !center->second.terrain->floor_world_height || !center->second.terrain->ramp_index ||
        *center->second.terrain->ramp_index!=0 || !center->second.cell_flag_100 ||
        *center->second.cell_flag_100 || impact.z>*center->second.terrain->floor_world_height)
        return missing("aerial or bridge area collection is unresolved");
    // DAT_00A6F248 is initialized by 005441E0 in this order. Count is
    // DAT_007A5908[trunc(CellSpread + 0.99)].
    constexpr std::array<std::array<int,2>,9> offsets{{
        {{0,0}},{{1,-1}},{{0,-1}},{{-1,-1}},{{-1,0}},
        {{1,0}},{{-1,1}},{{0,1}},{{1,1}}}};
    const auto count=static_cast<int>(fp::add(static_cast<double>(spread),0.99)) ? 9u : 1u;
    std::map<std::uint32_t,const session::world::Entity*> entities;
    for (const auto& entity:world.entities)
        if (!entity.id || !entities.emplace(entity.id,&entity).second)
            return missing("object IDs are invalid");
    if (source_id && !entities.count(source_id)) return missing("area source is unknown");
    std::vector<AreaCandidate> result;
    for (unsigned i=0;i<count;++i) {
        const int x=cx+offsets[i][0], y=cy+offsets[i][1];
        if (x<0 || x>=512 || y<0 || y>=512)
            return missing("area needs the original fallback cell");
        const auto cell=world.cells.find(static_cast<unsigned>(y*512+x));
        if (cell==world.cells.end() || !cell->second.allocated || !*cell->second.allocated ||
            !cell->second.heads || !cell->second.terrain ||
            !cell->second.terrain->overlay_type_index || *cell->second.terrain->overlay_type_index!=-1)
            return missing("area cell or overlay state is unknown");
        std::set<std::uint32_t> seen;
        for (auto id=cell->second.heads->ground_head;id;) {
            const auto found=entities.find(id);
            if (found==entities.end() || !seen.insert(id).second ||
                found->second->lifetime!=session::world::Lifetime::Placed || !found->second->next)
                return missing("area object chain is incomplete or cyclic");
            const auto& object=*found->second;
            const auto next=*object.next;
            if (id==source_id) { id=next; continue; }
            auto p=resolve(object);
            if (!p || !p->active) return missing("area object active state is unknown");
            if (!*p->active) { id=next; continue; }
            if (object.kind!=session::world::Kind::Unit)
                return missing("non-Unit area distance or callbacks are unresolved");
            if (type_filter) {
                if (!p->excluded_by_scenario) return missing("area type exclusion is unknown");
                if (*p->excluded_by_scenario) { id=next; continue; }
            }
            if (!p->damage_position) return missing("area virtual damage position is unknown");
            const auto& q=*p->damage_position;
            const auto safe_delta=[](int a,int b) {
                const auto d=static_cast<long long>(a)-b;
                return d>=-1048576 && d<=1048576;
            };
            if (!safe_delta(impact.x,q.x) || !safe_delta(impact.y,q.y) || !safe_delta(impact.z,q.z))
                return missing("area distance exceeds the verified integer domain");
            const int d=distance.between(impact,q);
            if (i==0) {
                if (!p->special_flag) return missing("area special-group flag is unknown");
                if (*p->special_flag) {
                    if (!p->special_virtual) return missing("area special-group virtual result is unknown");
                    if (*p->special_virtual && d<0x55)
                        return missing("area special-group dispatch is unresolved");
                }
            }
            result.push_back({id,d});
            id=next;
        }
    }
    return {std::move(result),{}};
}
}

namespace ra2::sim {
weapons::FlatCannonTickStatus base_advance_flat_projectile(
    FlatProjectile& projectile, const weapons::FlatCannonTickInput& tick,
    const weapons::TerrainTraceLookup& terrain, const OriginalDistance& distance) {
    using S=weapons::FlatCannonTickStatus;
    if (projectile.phase!=FlatProjectilePhase::Flying ||
        projectile.arc.position!=tick.arc.position || projectile.arc.velocity!=tick.arc.velocity ||
        projectile.arc.gravity!=tick.arc.gravity || projectile.source_id!=tick.source_id ||
        projectile.target.x!=tick.target.x || projectile.target.y!=tick.target.y ||
        projectile.target.z!=tick.target.z || !terrain)
        return S::NeedsInput;
    const auto result=weapons::base_flat_cannon_tick(tick,terrain,distance);
    if (result.status==S::NeedsInput) return result.status;
    projectile.arc.position=result.state.position;
    projectile.arc.velocity=result.state.velocity;
    if (result.status==S::Impact) projectile.phase=FlatProjectilePhase::ImpactPending;
    else if (result.status==S::Remove) projectile.phase=FlatProjectilePhase::RemovalPending;
    return result.status;
}
}

namespace ra2::sim {
std::optional<Coord> base_stationary_unit_aim(const session::world::Snapshot& world,
    std::uint32_t source_id,std::uint32_t target_id,std::optional<bool> moving) {
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup ||
        world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second || !source_id || !target_id ||
        !moving || *moving) return {};
    const session::world::Entity* source=nullptr;const session::world::Entity* target=nullptr;
    std::set<std::uint32_t> seen;
    for (const auto& item:world.entities) {
        if (!item.id || !seen.insert(item.id).second) return {};
        if (item.id==source_id) source=&item;
        if (item.id==target_id) target=&item;
    }
    if (!source || !target || source->kind!=session::world::Kind::Unit ||
        target->kind!=session::world::Kind::Unit || source->lifetime!=session::world::Lifetime::Placed ||
        target->lifetime!=session::world::Lifetime::Placed || !target->position) return {};
    return target->position;
}
}

namespace ra2::sim {
AreaDamageNext base_next_area_damage(AreaDamageCursor& cursor,const AreaRecipientLookup& lookup) {
    using S=AreaDamageStep;
    if (!lookup || cursor.radius<0 || !cursor.event.area_flag || !cursor.event.warhead_id ||
        cursor.next>cursor.candidates.size()) return {S::NeedsInput,{}};
    if (cursor.pending) return {S::Request,cursor.pending};
    while (cursor.next<cursor.candidates.size()) {
        const auto candidate=cursor.candidates[cursor.next];
        if (!candidate.id || candidate.distance<0) return {S::NeedsInput,{}};
        auto state=lookup(candidate.id);
        if (!state || state->target_id!=candidate.id) return {S::NeedsInput,{}};
        state->attacker_id=cursor.event.attacker_id;
        state->warhead_id=cursor.event.warhead_id;
        state->raw_damage=cursor.event.damage;
        state->distance=candidate.distance;
        state->radius=cursor.radius;
        // 0047E2E8 checks the scenario gate once, before collection. A
        // trigger may change that global later; do not recheck it here.
        state->scenario_blocks_damage=false;
        const auto request=weapons::base_resolved_impact_recipient(*state);
        if (request) { cursor.pending=request; return {S::Request,request}; }
        ++cursor.next;
    }
    return {S::Complete,{}};
}
bool base_complete_area_damage_request(AreaDamageCursor& cursor,std::uint32_t id) {
    if (!cursor.pending || !id || cursor.pending->target_id!=id || cursor.next>=cursor.candidates.size() ||
        cursor.candidates[cursor.next].id!=id) return false;
    cursor.pending.reset();++cursor.next;return true;
}
}

// Playable prototype. The original entry points above remain independently
// guarded. Every approximation below is labeled TODO-ORIGINAL.
#include "combat.h"
#include "../weapons/cannon_range.h"
#include "../weapons/detonation.h"
#include "../weapons/fire_timer.h"
#include "../weapons/muzzle.h"
#include "../weapons/object_damage.h"
#include "../weapons/projectile_damage.h"
#include "../weapons/techno_damage.h"
#include "prototype_special.h"
#include "prototype_turret.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
double country_armor_multiplier(const session::world::House*,session::world::Kind,
    const std::string&,const rules::IniFile&,const rules::IniFile&);
namespace {
using session::world::Entity;
using session::world::Snapshot;
using session::world::Kind;
using session::world::Lifetime;
using rules::IniFile;

IniFile prototype_merge_rules(const IniFile& base,const IniFile& mode) {
    std::map<std::string,std::map<std::string,std::string>> sections;
    for (const auto* ini:{&base,&mode}) {
        std::set<std::string> section_names;
        std::set<std::pair<std::string,std::string>> seen;
        for (const auto& section:ini->sections()) {
            if (!section_names.insert(section.name).second) continue;
            for (const auto& entry:section.entries)
                if (seen.emplace(section.name,entry.key).second)
                    sections[section.name][entry.key]=entry.value;
        }
    }
    std::ostringstream text;
    for (const auto& section:sections) {
        text<<'['<<section.first<<"]\n";
        for (const auto& entry:section.second) text<<entry.first<<'='<<entry.second<<'\n';
    }
    IniFile merged;merged.parse(text.str());return merged;
}

Entity* prototype_find(Snapshot& world,std::uint32_t id) {
    auto it=std::find_if(world.entities.begin(),world.entities.end(),
        [id](const Entity& e){return e.id==id;});
    return it==world.entities.end()?nullptr:&*it;
}
const Entity* prototype_find(const Snapshot& world,std::uint32_t id) {
    const auto it=std::find_if(world.entities.begin(),world.entities.end(),
        [id](const Entity& e){return e.id==id;});
    return it==world.entities.end()?nullptr:&*it;
}
bool prototype_alive(const Entity* e) {
    // TODO-ORIGINAL: Resolve missing active and limbo values from the actor.
    return e && (e->kind==Kind::Unit || e->kind==Kind::Infantry || e->kind==Kind::Aircraft ||
        e->kind==Kind::Structure) &&
        e->lifetime==Lifetime::Placed && e->position && e->resolved_hp && *e->resolved_hp>0 &&
        (!e->active || *e->active) && (!e->in_limbo || !*e->in_limbo);
}
const IniFile::Entry* prototype_entry(const IniFile& base,const IniFile& map,
                                     const std::string& section,const std::string& key) {
    if (auto* e=map.find(section,key)) return e;
    return base.find(section,key);
}
std::string prototype_string(const IniFile& base,const IniFile& map,
                             const std::string& section,const std::string& key) {
    const auto* e=prototype_entry(base,map,section,key);
    return e?e->value:std::string{};
}
std::optional<int> prototype_int(const IniFile& b,const IniFile& m,
                                 const std::string& section,const std::string& key) {
    const auto* e=prototype_entry(b,m,section,key);
    return e?IniFile::integer_value(e->value):std::optional<int>{};
}
std::optional<double> prototype_double(const IniFile& b,const IniFile& m,
                                       const std::string& section,const std::string& key) {
    const auto* e=prototype_entry(b,m,section,key);
    return e?IniFile::double_value(e->value):std::optional<double>{};
}
bool prototype_bool(const IniFile& b,const IniFile& m,
                    const std::string& section,const std::string& key,bool fallback=false) {
    const auto* e=prototype_entry(b,m,section,key);
    return e?IniFile::bool_value(e->value).value_or(fallback):fallback;
}
struct PrototypeWeapon {
    std::string name,warhead,projectile;
    int damage=0,rof=0,range=0,max_damage=0;
    int burst=1; // Original WeaponType +9C.
    double gravity=0,condition_red=0;
    double prone_damage=1; // Original WarheadType +F8.
    float spread=0,percent=0;
    bool arcing=false,lobber=false;
    bool anti_air=false,anti_ground=true; // BulletType +274/+275 (004644E0).
    bool omni_fire=false;      // Original WeaponType +107.
    bool projectile_rot=false; // Original BulletType +2AC is not zero.
    std::array<int,3> fire_flh{{0,0,0}};
};
std::optional<PrototypeWeapon> prototype_weapon(const IniFile& base,const IniFile& map,
    const IniFile& art,const Entity& source,const std::string& weapon_name={}) {
    PrototypeWeapon out;
    out.name=weapon_name.empty()?prototype_string(base,map,source.type,"Primary"):weapon_name;
    if (out.name.empty()) return {};
    const auto damage=prototype_int(base,map,out.name,"Damage");
    const auto rof=prototype_int(base,map,out.name,"ROF");
    const auto range=prototype_double(base,map,out.name,"Range");
    if (!damage || *damage<=0 || !rof || *rof<0 || !range || *range<0) return {};
    const auto resolved_range=weapons::base_weapon_range_from_cells(*range,0);
    if (!resolved_range || *resolved_range<=0) return {};
    out.damage=*damage;out.rof=*rof;out.range=*resolved_range;
    out.warhead=prototype_string(base,map,out.name,"Warhead");
    out.projectile=prototype_string(base,map,out.name,"Projectile");
    if (out.warhead.empty() || out.projectile.empty()) return {};
    out.arcing=prototype_bool(base,map,out.projectile,"Arcing");
    out.anti_air=prototype_bool(base,map,out.projectile,"AA");
    out.anti_ground=prototype_bool(base,map,out.projectile,"AG",true);
    out.burst=prototype_int(base,map,out.name,"Burst").value_or(1);
    if (out.burst<1) return {}; // 006CB2F3 divides by Burst.
    out.omni_fire=prototype_bool(base,map,out.name,"OmniFire");
    out.projectile_rot=prototype_int(base,map,out.projectile,"ROT").value_or(0)!=0;
    out.lobber=prototype_bool(base,map,out.name,"Lobber");
    // TODO-ORIGINAL: Use constructor values when these rule keys are absent.
    out.spread=static_cast<float>(prototype_double(base,map,out.warhead,"CellSpread").value_or(0));
    out.percent=static_cast<float>(prototype_double(base,map,out.warhead,"PercentAtMax").value_or(1));
    out.prone_damage=prototype_double(base,map,out.warhead,"ProneDamage").value_or(1);
    if (!std::isfinite(out.prone_damage) || out.prone_damage<0) return {};
    out.max_damage=prototype_int(base,map,"CombatDamage","MaxDamage").value_or(10000);
    out.condition_red=prototype_double(base,map,"AudioVisual","ConditionRed").value_or(0.25);
    out.gravity=prototype_double(base,map,"AudioVisual","Gravity").value_or(6);
    if (out.spread<0 || out.spread>10 || out.percent<0 || out.max_damage<=0 ||
        out.condition_red<0 || out.condition_red>1 || !std::isfinite(out.gravity) || out.gravity<=0)
        return {};
    auto image=prototype_string(base,map,source.type,"Image");
    if (image.empty()) image=source.type;
    const bool secondary=!weapon_name.empty() && weapon_name==prototype_string(base,map,source.type,"Secondary") &&
        weapon_name!=prototype_string(base,map,source.type,"Primary");
    const auto flh=art.read_list(image,secondary?"SecondaryFireFLH":"PrimaryFireFLH");
    if (flh.size()==3) {
        for (std::size_t i=0;i<3;++i) {
            const auto value=IniFile::integer_value(flh[i]);
            if (!value) return {};
            out.fire_flh[i]=*value;
        }
    }
    // TODO-ORIGINAL: Missing art offsets use the actor position in this path.
    return out;
}
std::optional<double> prototype_verse(const IniFile& base,const IniFile& map,
                                      const PrototypeWeapon& weapon,const Entity& target) {
    auto armor=prototype_string(base,map,target.type,"Armor");
    std::transform(armor.begin(),armor.end(),armor.begin(),
                   [](unsigned char c){return static_cast<char>(std::tolower(c));});
    static const std::array<const char*,11> names{{"none","flak","plate","light","medium",
        "heavy","wood","steel","concrete","special_1","special_2"}};
    const auto name=std::find(names.begin(),names.end(),armor);
    if (name==names.end()) return {};
    const auto verses=IniFile::split_list(prototype_string(base,map,weapon.warhead,"Verses"));
    const auto index=static_cast<std::size_t>(name-names.begin());
    if (index>=verses.size()) return {};
    return IniFile::double_value(verses[index]);
}
const session::world::House* prototype_house(const Snapshot& w,std::uint32_t id) {
    auto it=std::find_if(w.houses.begin(),w.houses.end(),[id](const auto& h){return h.id==id;});
    return it==w.houses.end()?nullptr:&*it;
}
std::uint32_t prototype_cell(Coord p) {
    if (p.x<0 || p.y<0 || p.x/256>=512 || p.y/256>=512) return UINT32_MAX;
    return static_cast<std::uint32_t>((p.y/256)*512+p.x/256);
}
int prototype_floor(const Snapshot& w,Coord p) {
    const auto it=w.cells.find(prototype_cell(p));
    // TODO-ORIGINAL: Resolve ramp and bridge floor instead of using zero.
    if (it==w.cells.end() || !it->second.terrain || !it->second.terrain->floor_world_height) return 0;
    return *it->second.terrain->floor_world_height;
}
// Base 004CCE70 / 005D5410 IsInAir: on-map height is at least two levels.
// TODO-ORIGINAL: jumpjet locomotion is not connected to this adapter, so
// non-landing airships and hover infantry still carry a ground position.
// Their declared flight family supplies the air class until that state exists.
bool prototype_air_target(const IniFile& base,const IniFile& map,const Snapshot& world,const Entity& target) {
    const bool permanent_air=!prototype_bool(base,map,target.type,"Landable") &&
        (prototype_bool(base,map,target.type,"BalloonHover") || prototype_bool(base,map,target.type,"JumpJet"));
    return permanent_air || (target.position &&
        std::int64_t(target.position->z)-prototype_floor(world,*target.position)>=208);
}
std::optional<PrototypeWeapon> prototype_target_weapon(const IniFile& base,const IniFile& map,
    const IniFile& art,const Snapshot& world,const Entity& source,const Entity& target) {
    const bool airborne=prototype_air_target(base,map,world,target);
    // Base 006C9260: IsInAir requires AA; a ground object requires AG.
    // TODO-ORIGINAL: recover the remaining primary/secondary preference gates.
    for(const auto* slot:{"Primary","Secondary"}) {
        const auto name=prototype_string(base,map,source.type,slot);
        if(name.empty())continue;
        const auto weapon=prototype_weapon(base,map,art,source,name);
        if(weapon && (airborne?weapon->anti_air:weapon->anti_ground))return weapon;
    }
    return {};
}
// Remove a dead actor from all known ordered chains without changing the
// order of survivors. The original Unit marking and occupancy callbacks are
// still pending; this is only the visible prototype completion.
bool prototype_unlink(Snapshot& w,Entity& victim) {
    if (!victim.next || *victim.next==victim.id) return false;
    std::map<std::uint32_t,Entity*> objects;
    for (auto& e:w.entities) if (!e.id || !objects.emplace(e.id,&e).second) return false;
    if (*victim.next && !objects.count(*victim.next)) return false;
    std::vector<std::uint32_t*> links;
    std::vector<session::world::CellState*> emptied;
    for (auto& pair:w.cells) {
        auto& cell=pair.second;
        if (!cell.heads) continue; // TODO-ORIGINAL: unknown unallocated/fallback Cells.
        for (auto* head:{&cell.heads->ground_head,&cell.heads->bridge_head}) {
            std::set<std::uint32_t> seen;
            auto* link=head;
            while (*link) {
                if (!seen.insert(*link).second) return false;
                const auto it=objects.find(*link);
                if (it==objects.end() || !it->second->next) return false;
                if (*link==victim.id) {
                    links.push_back(link);
                    if (head==&cell.heads->ground_head && head==link && *victim.next==0)
                        emptied.push_back(&cell);
                }
                link=&*it->second->next;
            }
        }
    }
    // Base flight mission: aircraft are unlinked from their Cell while
    // flying. Flight unlinks them and clears Foot +5A0 occupancy tracking,
    // so an unlinked victim with no tracking is already off the map. Only
    // the known flying types take this path; every other rejection stands.
    if (links.empty()) {
        static const std::array<const char*,4> flying{{"ORCA","BEAG","ZEP","JUMPJET"}};
        if (*victim.next!=0 || victim.occupancy_tracking.value_or(true) ||
            std::find_if(flying.begin(),flying.end(),[&](const char* type){
                return victim.type==type;})==flying.end()) return false;
        return true;
    }
    for (auto* link:links) *link=*victim.next;
    victim.next=0;
    // TODO-ORIGINAL: Replace with Unit Mark(0), its Cell setup, and reserved
    // moving-cell cleanup. Do not clear occupancy for a Cell with survivors.
    for (auto* cell:emptied) if (cell->ground_occupancy_124) cell->ground_occupancy_124=0;
    return true;
}
bool prototype_die(Snapshot& world,Entity& target) {
    if (target.lifetime==Lifetime::Placed && !prototype_unlink(world,target)) return false;
    target.resolved_hp=0;
    if (target.techno_field_329) target.techno_field_329=1;
    target.active=false;target.in_limbo=true;target.target_id=0;target.lifetime=Lifetime::Destroyed;
    return true;
}
struct PrototypeHit { bool applied=false,destroyed=false; std::string missing; };
PrototypeHit prototype_damage(Snapshot& world,std::uint32_t target_id,std::uint32_t source_id,
    int raw,int distance,const PrototypeWeapon& weapon,const IniFile& base,const IniFile& map) {
    auto* target=prototype_find(world,target_id);
    if (!prototype_alive(target)) return {};
    const auto maximum=target->maximum_hp ? target->maximum_hp : prototype_int(base,map,target->type,"Strength");
    const auto verse=prototype_verse(base,map,weapon,*target);
    if (!maximum || !verse || *maximum<=0 || !std::isfinite(*verse) || *verse<0)
        return {false,false,"target Strength or warhead Verses is unresolved"};
    weapons::TechnoDamageInput techno{};
    techno.raw_damage=raw;
    // Country, veteran and special state resolve from the loaded actor.
    // The house multiplier rides on the entity; the rank armor divisor is
    // the [General] VeteranArmor value. TODO-ORIGINAL: the invulnerability
    // and immune virtuals still need the loaded actor state.
    // Base 004F58A0: the owning House.country picks the armor factor by
    // target RTTI. Resolved here from the merged rules so every creation
    // path (setup actors and new factory products) uses the live country,
    // not a stale per-actor default.
    techno.house_armor_multiplier=country_armor_multiplier(
        prototype_house(world,target->house_id),target->kind,target->type,base,map);
    target->house_armor_multiplier=techno.house_armor_multiplier;
    // The country Firepower key is not applied here. Country +C8
    // FirepowerMult and +E0 ArmorMult are unused TS leftovers. UNRESOLVED:
    // the original House +D0 initializer is not traced; setup.cpp writes
    // 1.0 as a multiplayer stand-in, so that value is not proven live.
    techno.object_armor_multiplier=target->armor_multiplier.value_or(1);
    const auto target_abilities=load_type_abilities(base,target->type);
    const auto target_rank=rank_of(target->veterancy.value_or(0.0));
    techno.veteran=target_rank!=Rank::Rookie;
    techno.elite=target_rank==Rank::Elite;
    techno.veteran_armor=target_abilities.veteran.flag[AbilityStronger];
    techno.elite_armor=target_abilities.elite.flag[AbilityStronger];
    techno.veteran_armor_multiplier=VeterancyRules::load(base).veteran_armor;
    techno.attacker_present=source_id!=0;techno.warhead_present=true;
    const auto prefix=weapons::base_techno_damage_prefix(techno);
    if (prefix.stage!=weapons::TechnoDamageStage::ObjectDamage)
        return {false,false,"prototype Techno damage needs an original effect"};
    const auto amount=total_damage({prefix.damage,distance,weapon.max_damage,
        weapon.spread,weapon.percent,*verse,false,true});
    weapons::ObjectDamageInput input{*target->resolved_hp,*maximum,prefix.damage,false,false,
                                    weapon.condition_red,amount};
    const int original_kind=target->kind==Kind::Unit?1:target->kind==Kind::Infantry?15:6;
    weapons::ObjectDamageState state{*target->resolved_hp,*maximum,true,false,false,true,original_kind};
    int health=0,status=0;
    if (target->kind==Kind::Infantry && amount>=*target->resolved_hp) {
        // TODO-ORIGINAL: Infantry death has a separate damage animation and
        // original virtual path. Use only the recovered Object HP prefix here.
        const auto plan=weapons::base_object_damage_prefix(input);
        health=plan.health;status=4;
    } else {
        // TODO-ORIGINAL: Resolve Unit/Foot/Techno callbacks, triggers, scores,
        // death visuals, passengers, and deferred deletion. Do not claim that
        // a no-op callback reproduces those effects.
        const auto path=weapons::base_object_damage_path(input,state,source_id!=0,
            [](const weapons::ObjectDamageAction&,weapons::ObjectDamageState&){});
        health=path.state.health;status=path.status;
    }
    if (status==4 && !prototype_die(world,*target))
        return {false,false,"dead actor has unresolved Cell links"};
    target->resolved_hp=health;
    if (status && target->techno_field_329) target->techno_field_329=1;
    return {true,status==4,{}};
}
void prototype_record(const Snapshot& world,PrototypeCombatTick& report,std::uint64_t frame,
    PrototypeCombatEventKind kind,const PrototypeCombatProjectile& shot,const Entity& target,int before,int after) {
        const auto* source=prototype_find(world,shot.source_id);
        PrototypeCombatEvent event;
        event.kind=kind;event.frame=frame;event.fired_frame=shot.fired_frame;
        event.source_id=shot.source_id;event.target_id=target.id;
        if (source) {
            event.source_house=source->house_id;event.source_type=source->type;
            event.source_position=source->position.value_or(Coord{});
        }
        event.target_house=target.house_id;event.target_type=target.type;
        event.target_position=target.position.value_or(Coord{});
        event.weapon=shot.weapon;event.warhead=shot.warhead;event.raw_damage=shot.damage;
        event.hp_before=before;event.hp_after=after;
        report.events.push_back(event);
        // Always keep attribution in the captured match log, including off-screen deaths.
        std::fprintf(stderr,"combat event=%s frame=%llu fired_frame=%llu source=%u type=%s house=%u source_pos=%d,%d,%d weapon=%s warhead=%s target=%u target_type=%s target_house=%u target_pos=%d,%d,%d raw=%d hp_before=%d hp_after=%d\n",
            kind==PrototypeCombatEventKind::Shot?"shot":kind==PrototypeCombatEventKind::Death?"death":"damage",
            static_cast<unsigned long long>(frame),static_cast<unsigned long long>(shot.fired_frame),
            event.source_id,event.source_type.c_str(),event.source_house,
            event.source_position.x,event.source_position.y,event.source_position.z,
            event.weapon.c_str(),event.warhead.c_str(),event.target_id,event.target_type.c_str(),event.target_house,
            event.target_position.x,event.target_position.y,event.target_position.z,shot.damage,before,after);
}
}

// Base 004F58A0: the victim's House asks its CountryType for an armor
// factor by target RTTI. Original branch values: RTTI 3 AircraftType reads
// +108 ArmorAircraftMult, 0x10 InfantryType +100 ArmorInfantryMult, 0x28
// UnitType +104 ArmorUnitsMult, and 7 BuildingType +110 ArmorDefensesMult
// only when type BuildCat==5 (Combat), otherwise +10C ArmorBuildingsMult.
// The country loader 004F9550 reads these five keys as floats; a missing
// key or country leaves the factor at 1.0. The global Country +C8
// FirepowerMult and +E0 ArmorMult are unused TS leftovers and are not read.
double country_armor_multiplier(const session::world::House* house,
    session::world::Kind kind,const std::string& type,
    const rules::IniFile& base,const rules::IniFile& map) {
    if (!house || house->country.empty()) return 1.0;
    const char* key=kind==session::world::Kind::Aircraft ? "ArmorAircraftMult"
        : kind==session::world::Kind::Infantry ? "ArmorInfantryMult"
        : kind==session::world::Kind::Unit ? "ArmorUnitsMult"
        : kind==session::world::Kind::Structure
          ? ([](const std::string& cat) {
                // BuildCat enum names are case-insensitive in the original
                // readers; accept any spelling of Combat (BuildCat 5).
                if (cat.size()!=6) return false;
                const char combat[]="combat";
                for (std::size_t i=0;i<cat.size();++i)
                    if (std::tolower(static_cast<unsigned char>(cat[i]))!=combat[i]) return false;
                return true;
            })(prototype_string(base,map,type,"BuildCat"))
            ? "ArmorDefensesMult" : "ArmorBuildingsMult"
        : "";
    if (!*key) return 1.0;
    const auto value=prototype_double(base,map,house->country,key).value_or(1.0);
    // 004F9550 stores each factor as a 32-bit float. The original FPU
    // control word 00000E7F rounds toward zero on the store, so a double
    // INI value like 0.8 lands on the binary32 value at or below it.
    float stored=static_cast<float>(value);
    if (static_cast<double>(stored)>value) stored=std::nextafter(stored,0.0f);
    return static_cast<double>(stored);
}

PrototypeCombatRuntime::PrototypeCombatRuntime(const IniFile& base,const IniFile& map,
    const IniFile& art,const std::string& exe)
    :base_(base),map_(map),art_(art),merged_(prototype_merge_rules(base,map)),
     distance_(OriginalDistance::from_executable(exe)),
     angles_(weapons::BaseBallisticAngles::from_executable(exe)) {}
PrototypeCombatRuntime::PrototypeCombatRuntime(const IniFile& base,const IniFile& mode,
    const IniFile& map,const IniFile& art,const std::string& exe)
    :PrototypeCombatRuntime(prototype_merge_rules(base,mode),map,art,exe) {}

bool PrototypeCombatRuntime::can_attack(const session::world::Snapshot& world,
    std::uint32_t source_id,std::uint32_t target_id) const {
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup ||
        world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second ||
        !source_id || !target_id || source_id==target_id) return false;
    const auto* source=prototype_find(world,source_id);
    const auto* target=prototype_find(world,target_id);
    // TODO-ORIGINAL: Resolve alliances beyond matching House IDs.
    if (!prototype_alive(source) || !prototype_alive(target) || source->house_id==target->house_id)
        return false;
    const auto weapon=prototype_target_weapon(base_,map_,art_,world,*source,*target);
    if (!weapon) return false;
    const auto verse=prototype_verse(base_,map_,*weapon,*target);
    return verse && std::isfinite(*verse) && *verse>0;
}
std::optional<PrototypeCombatRuntime::TurretAim> PrototypeCombatRuntime::turret_aim(
    session::world::Entity& source,const session::world::Entity& target,bool omni_fire,std::int32_t frame) {
    // Base 006FB890 turret branch for UnitType +AAB Turret. OmniFire (+107)
    // weapons leave the turret alone. TODO-ORIGINAL: TurretSpins, locked
    // turrets, hull aim for Units without a turret, and the idle return to
    // the hull or move destination (006FB98E) are not reproduced here.
    if (source.kind!=session::world::Kind::Unit || omni_fire || !source.position || !target.position ||
        !prototype_bool(base_,map_,source.type,"Turret") ||
        prototype_bool(base_,map_,source.type,"TurretSpins")) return {};
    const auto dx=static_cast<long long>(target.position->x)-source.position->x;
    const auto dy=static_cast<long long>(target.position->y)-source.position->y;
    if (std::abs(dx)>100000 || std::abs(dy)>100000 || (!dx && !dy)) return {};
    // Base 005D2730 GetTargetDirection uses the same atan2 arithmetic.
    const auto direction=angles_.heading(static_cast<int>(dx),static_cast<int>(dy));
    // Base 006FA5AD: the turret ROT is the Techno type ROT (+638).
    const auto rot=base_facing_rot(prototype_int(base_,map_,source.type,"ROT").value_or(0));
    const auto shown=source.turret_facing_raw.value_or(source.hull_facing_raw.value_or(0));
    auto state=turrets_.find(source.id);
    if (state==turrets_.end() || state->second.facing.rot!=rot || state->second.written!=shown) {
        TurretState fresh;fresh.facing.rot=rot;base_facing_set_current(fresh.facing,shown,frame);
        state=turrets_.insert_or_assign(source.id,fresh).first;
    }
    auto& facing=state->second.facing;
    base_facing_set_desired(facing,direction,frame);
    const auto current=base_facing_current(facing,frame);
    state->second.written=current;
    source.turret_facing_raw=current;source.turret_facing=static_cast<int>(current>>8);
    return TurretAim{current,direction};
}
bool PrototypeCombatRuntime::attack(session::world::Snapshot& world,std::uint32_t source_id,std::uint32_t target_id) {
    if (!can_attack(world,source_id,target_id)) return false;
    auto* source=prototype_find(world,source_id);
    guard_orders_.erase(source_id);guard_place_.erase(source_id);
    orders_[source_id]=target_id;source->target_id=target_id;return true;
}
void PrototypeCombatRuntime::stop(session::world::Snapshot& world,std::uint32_t source_id) {
    guard_orders_.erase(source_id);guard_place_.erase(source_id);
    orders_.erase(source_id);
    if (auto* source=prototype_find(world,source_id)) source->target_id=0;
}

PrototypeCombatTick PrototypeCombatRuntime::destroy_house(session::world::Snapshot& world,
    std::uint32_t house,std::uint64_t frame) {
    PrototypeCombatTick report;
    if(!house || world.edition!=session::world::Edition::Base || world.pending_cell_setup) {
        report.unresolved.push_back("house destruction needs a ready base world");return report;
    }
    // Base 004E7DF0: current HP, C4Warhead, null attacker, ignore defenses.
    // Transport-derived ownership and the pre-death stun callback are not modeled.
    for(auto& target:world.entities) {
        if(target.house_id!=house || target.kind==session::world::Kind::Terrain || target.lifetime==Lifetime::Destroyed)continue;
        if(!target.resolved_hp) {report.unresolved.push_back("fallen actor health is unresolved");continue;}
        const int before=*target.resolved_hp;
        if(!prototype_die(world,target)) {report.unresolved.push_back("fallen actor has unresolved Cell links");continue;}
        stop(world,target.id);turrets_.erase(target.id);prone_.erase(target.id);
        PrototypeCombatProjectile death;
        death.target_id=target.id;death.fired_frame=frame;death.damage=before;
        death.warhead=prototype_string(base_,map_,"CombatDamage","C4Warhead");
        prototype_record(world,report,frame,PrototypeCombatEventKind::Death,death,target,before,0);
        if(!target.position)report.events.back().target_position={-1,-1,-1};
        ++report.destroyed;
    }
    return report;
}

PrototypeCombatTick PrototypeCombatRuntime::tick(session::world::Snapshot& world,std::uint64_t frame) {
    loop::profile::Scope probe(loop::profile::Phase::Combat);
    PrototypeCombatTick report;
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup ||
        world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second ||
        frame>static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        report.unresolved.push_back("world setup, RNG, or frame is invalid");return report;
    }
    const auto record=[&](PrototypeCombatEventKind kind,const PrototypeCombatProjectile& shot,
                          const session::world::Entity& target,int before,int after) {
        prototype_record(world,report,frame,kind,shot,target,before,after);
    };
    // TODO-ORIGINAL: The prototype advances saved bullets before attack orders.
    // Recover global object update order before treating this as replay exact.
    // Survivors hit this frame, with the attacker, for the 006CDC40 retaliation.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> struck;
    auto impact=[&](const PrototypeCombatProjectile& shot,Coord position,bool scatter) {
        ++report.impacts;
        const auto* source=prototype_find(world,shot.source_id);
        const auto weapon=source?prototype_weapon(base_,map_,art_,*source,shot.weapon):std::optional<PrototypeWeapon>{};
        if (!weapon) {report.unresolved.push_back("projectile weapon is unavailable");return;}
        const auto* aimed=prototype_find(world,shot.target_id);
        if (prototype_alive(aimed) && aimed->kind==session::world::Kind::Unit) {
            const auto adjusted=weapons::base_unit_cannon_impact_position(
                {position,true,aimed->position,true},distance_);
            if (adjusted) position=*adjusted;
        }
        std::vector<AreaCandidate> candidates;
        const auto collect=base_small_ground_area_candidates(world,position,weapon->spread,shot.source_id,
            false,[](const session::world::Entity& e)->std::optional<AreaUnitProperties> {
                // TODO-ORIGINAL: Resolve type exclusion and special-group virtuals.
                return AreaUnitProperties{e.active.value_or(true),e.position,false,false,false};
            },distance_);
        if (collect.candidates) candidates=*collect.candidates;
        else if (prototype_alive(aimed) && prototype_cell(*aimed->position)==prototype_cell(position)) {
            // TODO-ORIGINAL: Direct-hit fallback for infantry, missing Cells,
            // bridges, overlays, and unresolved area collection.
            candidates.push_back({aimed->id,0});
        }
        bool hit_any=false;
        const int radius=static_cast<int>(weapon->spread*256.0f);
        // Base 004FFB70..004FFBB9: a prone Infantry (+5C3) takes positive
        // damage times the warhead ProneDamage (+F8), at least 1.
        const auto raw_for=[&](const session::world::Entity& t) {
            if (t.kind!=session::world::Kind::Infantry || !prone_.count(t.id) || shot.damage<=0) return shot.damage;
            return std::max(1,static_cast<int>(shot.damage*weapon->prone_damage));
        };
        // Base 00500236..0050028C: a damaged survivor that is not Fearless
        // (+C0A) starts to panic, and its AI then lies down (00504AB3).
        // TODO-ORIGINAL: model the panic timer (+5BC), the prone animation
        // and standing up; here an Infantry stays prone until it moves.
        const auto panic=[&](const session::world::Entity& t,const PrototypeHit& hit) {
            if (hit.applied && !hit.destroyed && t.kind==session::world::Kind::Infantry &&
                !prototype_bool(base_,map_,t.type,"Fearless")) prone_.insert(t.id);
        };
        for (const auto& candidate:candidates) {
            auto* target=prototype_find(world,candidate.id);
            if (!prototype_alive(target) || candidate.distance>radius) continue;
            const int before=*target->resolved_hp;
            auto hit=prototype_damage(world,candidate.id,shot.source_id,raw_for(*target),
                                      candidate.distance,*weapon,merged_,map_);
            panic(*target,hit);
            if (hit.applied && !hit.destroyed && shot.source_id) struck.emplace_back(target->id,shot.source_id);
            if (!hit.missing.empty()) report.unresolved.push_back(hit.missing);
            if (hit.applied) record(PrototypeCombatEventKind::Damage,shot,*target,before,*target->resolved_hp);
            if (hit.destroyed) record(PrototypeCombatEventKind::Death,shot,*target,before,*target->resolved_hp);
            hit_any|=hit.applied;
            if (hit.destroyed) ++report.destroyed;
        }
        if (!hit_any && prototype_alive(aimed) && prototype_cell(*aimed->position)==prototype_cell(position)) {
            // TODO-ORIGINAL: Permit a direct hit when the flat prototype's
            // impact height misses the original area distance check.
            const int before=*aimed->resolved_hp;
            auto hit=prototype_damage(world,aimed->id,shot.source_id,raw_for(*aimed),0,*weapon,merged_,map_);
            panic(*aimed,hit);
            if (hit.applied && !hit.destroyed && shot.source_id) struck.emplace_back(aimed->id,shot.source_id);
            if (!hit.missing.empty()) report.unresolved.push_back(hit.missing);
            if (hit.applied) record(PrototypeCombatEventKind::Damage,shot,*aimed,before,*aimed->resolved_hp);
            if (hit.destroyed) record(PrototypeCombatEventKind::Death,shot,*aimed,before,*aimed->resolved_hp);
            if (hit.destroyed) ++report.destroyed;
        }
        if (scatter) {
            Random rng(world.random);
            weapons::base_complete_single_cluster_scatter(true,rng);
            world.random=rng.state();
        }
        // TODO-ORIGINAL: Run Bullet +11C/+F0, reference cleanup and deferred
        // deletion. Erasing this prototype's value only ends its visual life.
    };
    for (auto it=prone_.begin();it!=prone_.end();) {
        const auto* infantry=prototype_find(world,*it);
        if (!prototype_alive(infantry) || infantry->locomotor_moving.value_or(false)) it=prone_.erase(it);
        else ++it;
    }
    std::vector<PrototypeCombatProjectile> flying;
    for (auto shot:projectiles_) {
        try {
            const auto proposal=weapons::base_arc_proposal(shot.flight.arc);
            const Coord old{shot.flight.arc.position[0],shot.flight.arc.position[1],shot.flight.arc.position[2]};
            const Coord next{proposal.position[0],proposal.position[1],proposal.position[2]};
            const auto cell_index=prototype_cell(next);
            if (cell_index==UINT32_MAX || !world.cells.count(cell_index)) continue;
            weapons::FlatCannonTickInput tick{};tick.arc=shot.flight.arc;tick.target=shot.flight.target;
            tick.height_before=old.z-prototype_floor(world,old);tick.height_after=next.z-prototype_floor(world,next);
            // TODO-ORIGINAL: Read the engine's actual height unit and bridge state.
            tick.height_unit=104;tick.proposed_floor=prototype_floor(world,next);tick.source_id=shot.source_id;
            tick.proposed_building_id=0;tick.target_building_id=0;tick.building_intercepts=false;
            tick.position_in_map=true;tick.cell_objects.emplace();
            const auto cell=world.cells.find(cell_index);
            if (cell!=world.cells.end() && cell->second.heads) {
                std::set<std::uint32_t> seen;
                for (auto id=cell->second.heads->ground_head;id;) {
                    const auto* object=prototype_find(world,id);
                    if (!object || !object->next || !seen.insert(id).second) break;
                    if (object->position) {
                        const auto* source=prototype_find(world,shot.source_id);
                        // TODO-ORIGINAL: Resolve all collision flags and House alliances.
                        tick.cell_objects->push_back({id,object->collision_virtual_position.value_or(*object->position),
                            *object->position,object->collision_eligible.value_or(prototype_alive(object)),
                            source && source->house_id==object->house_id});
                    }
                    id=*object->next;
                }
            }
            tick.terrain.destination_cell_id=prototype_cell(shot.flight.target)+1;
            tick.terrain.checks_cliffs=false;tick.terrain.checks_walls=false;
            const weapons::TerrainTraceLookup terrain=[](std::int16_t x,std::int16_t y)
                ->std::optional<weapons::TerrainTraceCell> {
                if (x<0 || y<0 || x>=512 || y>=512) return {};
                // TODO-ORIGINAL: Read cliff and wall trace state from each Cell.
                return weapons::TerrainTraceCell{static_cast<std::uint32_t>(y*512+x+1),0,false,false,false};
            };
            auto status=base_advance_flat_projectile(shot.flight,tick,terrain,distance_);
            if (status==weapons::FlatCannonTickStatus::NeedsInput) {
                // TODO-ORIGINAL: Slow projectiles, bridges, interception and
                // unknown terrain need their complete original branches.
                shot.flight.arc.position=proposal.position;shot.flight.arc.velocity=proposal.velocity;
                status=next.z<=prototype_floor(world,next)?weapons::FlatCannonTickStatus::Impact:
                    weapons::FlatCannonTickStatus::Continue;
            }
            if (status==weapons::FlatCannonTickStatus::Impact) {
                const auto& p=shot.flight.arc.position;impact(shot,{p[0],p[1],p[2]},true);
            } else if (status==weapons::FlatCannonTickStatus::Continue) flying.push_back(std::move(shot));
        } catch (const std::exception& e) {report.unresolved.push_back(e.what());}
    }
    projectiles_=std::move(flying);
    const auto legal_guard_target=[&](const session::world::Entity& source,
                                      const session::world::Entity& target) {
        const auto* owner=prototype_house(world,source.house_id);
        const auto* house=prototype_house(world,target.house_id);
        if (!owner || !house || owner->country=="Neutral" || owner->country=="Special" ||
            house->country=="Neutral" || house->country=="Special" ||
            !prototype_bool(base_,map_,target.type,"LegalTarget",true) ||
            prototype_bool(base_,map_,target.type,"Immune")) return false;
        const auto allied=[](const session::world::House& a,const session::world::House& b) {
            return a.alliance_mask_5674 && b.original_index && *b.original_index>=0 &&
                *b.original_index<32 && (*a.alliance_mask_5674 & (1u<<*b.original_index));
        };
        if (allied(*owner,*house) || allied(*house,*owner)) return false;
        if (prototype_bool(base_,map_,source.type,"Powered") &&
            owner->power_output.value_or(0)<owner->power_drain.value_or(0)) return false;
        return can_attack(world,source.id,target.id);
    };
    // Original mission numbers: 0 Sleep, 5 Guard, 11 Area Guard, 25 Patrol.
    // TODO-ORIGINAL: Setup and production still write 0 for new idle
    // objects, so this prototype scans and retaliates with 0 like Guard.
    const auto guard_mission=[](const session::world::Entity& e) {
        return e.mission && (*e.mission==0 || *e.mission==5 || *e.mission==11);
    };
    // Base 006D31E0 Threat_Range and 006C6530 Greatest_Threat. Guard (control
    // 0) uses TechnoType +504 GuardRange; zero makes 006C5570 use the weapon
    // range through virtual +350. Area Guard (control 1) doubles GuardRange,
    // or the longer of both weapon ranges, and clamps it to 0..1000h.
    // TODO-ORIGINAL: virtual +304 can disable control 0 GuardRange.
    const auto scan_range=[&](const session::world::Entity& source,const PrototypeWeapon& weapon) {
        const auto cells=prototype_double(base_,map_,source.type,"GuardRange");
        const auto guard=cells?weapons::base_weapon_range_from_cells(*cells,0).value_or(0):0;
        if (!source.mission || *source.mission!=11) return guard>0?guard:weapon.range;
        int range=guard;
        if (!range) {
            range=weapon.range;
            const auto secondary=prototype_string(base_,map_,source.type,"Secondary");
            if (!secondary.empty())
                if (const auto other=prototype_weapon(base_,map_,art_,source,secondary)) range=std::max(range,other->range);
        }
        return std::clamp(range*2,0,0x1000);
    };
    std::set<std::uint32_t> returning;
    // Guard targets stay local. Explicit attack orders retain their chase target.
    for (auto it=guard_orders_.begin();it!=guard_orders_.end();) {
        const auto order=orders_.find(it->first);
        if (order==orders_.end() || order->second!=it->second.target) {
            it=guard_orders_.erase(it); continue;
        }
        auto* source=prototype_find(world,it->first);
        const auto* target=prototype_find(world,it->second.target);
        const auto weapon=prototype_alive(source) && prototype_alive(target)?
            prototype_target_weapon(base_,map_,art_,world,*source,*target):std::optional<PrototypeWeapon>{};
        // A Guard target ends when the actor moves or the target leaves the
        // scan range. An Area Guard actor may drive toward its target.
        // Base 004C5C1D..004C5CA3 (Area Guard): a stopped actor farther than
        // Threat_Range(1) * 0.75 from its guard place drops the target and
        // heads back there. TODO-ORIGINAL: the +57D flag, the +494 check
        // and GuardModeStray for a guarded object are not resolved.
        bool stray=false;
        if (it->second.area && source && !source->locomotor_moving.value_or(false))
            stray=distance_.between(*source->position,it->second.anchor)>
                static_cast<int>(it->second.range*0.75);
        const bool lost=stray || !weapon || !prototype_alive(target) || !legal_guard_target(*source,*target) ||
            (!it->second.area && (source->locomotor_moving.value_or(false) ||
                 distance_.between(*source->position,*target->position)>it->second.range));
        if (stray) {report.guard_return.emplace_back(source->id,it->second.anchor);returning.insert(source->id);}
        if (lost) {
            if (source) source->target_id=0;
            orders_.erase(order);
            it=guard_orders_.erase(it);
        } else ++it;
    }
    // Acquire only for idle actors. Explicit attack and move orders keep priority.
    // TODO-ORIGINAL: Recover the 006C5570 threat score, the cell scan order
    // and the NormalTargetingDelay/GuardAreaTargetingDelay scan timer (006D46D0).
    for (auto& source : world.entities) {
        if (!prototype_alive(&source) || orders_.count(source.id) ||
            source.target_id.value_or(0) || !guard_mission(source)) continue;
        // The flight mission may deny permission to fire. A rejected source
        // keeps its order and is skipped here without an approach request.
        if (fire_gate_ && !fire_gate_(source)) continue;
        // The prototype keeps the idle mission number during Move orders, so
        // a driving actor without a guard target is not idle and its old
        // guard place no longer applies.
        const bool area=*source.mission==11;
        if (!area || source.locomotor_moving.value_or(false)) guard_place_.erase(source.id);
        if (returning.count(source.id) || source.locomotor_moving.value_or(false)) continue;
        const auto weapon=prototype_weapon(base_,map_,art_,source);
        if (!weapon) continue;
        const auto* owner=prototype_house(world,source.house_id);
        if (!owner || owner->country=="Neutral" || owner->country=="Special") continue;
        if (prototype_bool(base_,map_,source.type,"Powered") &&
            owner->power_output.value_or(0)<owner->power_drain.value_or(0)) continue;
        const int range=scan_range(source,*weapon);
        const session::world::Entity* nearest=nullptr;
        int best=range+1;
        for (const auto& target : world.entities) {
            if(!prototype_alive(&target) || source.house_id==target.house_id)continue;
            const int d=distance_.between(*source.position,*target.position);
            if(d>range || d>best)continue;
            if (!legal_guard_target(source,target)) continue;
            if (d<best || (nearest && target.id<nearest->id)) {best=d; nearest=&target;}
        }
        if (nearest) {
            orders_[source.id]=nearest->id;
            const auto anchor=area?guard_place_.emplace(source.id,*source.position).first->second:*source.position;
            guard_orders_[source.id]=GuardOrder{nearest->id,range,anchor,area};
            source.target_id=nearest->id;
        }
    }
    for (auto it=orders_.begin();it!=orders_.end();) {
        auto* source=prototype_find(world,it->first);auto* target=prototype_find(world,it->second);
        if (!prototype_alive(source) || !prototype_alive(target)) {
            if (source) source->target_id=0;
            it=orders_.erase(it);continue;
        }
        // Base flight mission: a source without permission to fire keeps its
        // order, takes no shot and asks no approach. Weapon timing, damage
        // and in-flight projectiles stay shared with the ground prototype.
        if (fire_gate_ && !fire_gate_(*source)) {++it;continue;}
        try {
            const auto weapon=prototype_target_weapon(base_,map_,art_,world,*source,*target);
            std::optional<double> verse;
            if (weapon) verse=prototype_verse(base_,map_,*weapon,*target);
            // One checked read: GCC 13 reports the optional as maybe unset
            // when it is read again in the nested test.
            const bool resolved=verse.has_value() && std::isfinite(verse.value_or(0.0));
            if (!weapon || !resolved || verse.value_or(0.0)<=0) {
                if ((!weapon && !prototype_weapon(base_,map_,art_,*source)) ||
                    (weapon && !resolved))
                    report.unresolved.push_back("weapon or target armor is unresolved");
                source->target_id=0;it=orders_.erase(it);continue;
            }
            // Base 006FB890: a turreted Unit with a target turns its turret
            // toward that target every frame, also while out of range.
            const auto aim=turret_aim(*source,*target,weapon->omni_fire,static_cast<std::int32_t>(frame));
            // AircraftClass::IsCloseEnoughToAttack (006C5070) delegates to
            // weapon eligibility. Flight missions use ground separation here,
            // so a bomber can reach a ground target below its cruise height.
            // TODO-ORIGINAL: recover the complete aircraft range override.
            const bool flying=prototype_air_target(base_,map_,world,*source);
            const int d=distance_.between(*source->position,*target->position,flying);
            if (d>weapon->range) {report.out_of_range.emplace_back(source->id,target->id);++it;continue;}
            // TODO-ORIGINAL: Replace these direct range and targeting checks
            // with the full mission, eligibility, terrain and elevation paths.
            // TODO-ORIGINAL: An unknown timer starts ready in this prototype.
            const weapons::FireTimer timer{source->weapon_timer_start_frame.value_or(-1),
                                           source->weapon_timer_duration.value_or(0)};
            if (!weapons::base_fire_timer_ready(timer,static_cast<int>(frame))) {++it;continue;}
            // Base 00704DF0..00704E8E: FIRE_FACING until the turret is close
            // enough. OmniFire weapons skip the check.
            if (aim && !weapon->omni_fire &&
                !base_unit_facing_ready(aim->current,aim->direction,weapon->projectile_rot)) {++it;continue;}
            const auto* house=prototype_house(world,source->house_id);
            // Veteran and elite rank now feeds the shot: the FIREPOWER
            // ability flag gates the [General] VeteranCombat multiplier in
            // base_projectile_damage.
            const auto source_abilities=load_type_abilities(merged_,source->type);
            const auto source_rank=rank_of(source->veterancy.value_or(0.0));
            weapons::ProjectileDamageInput damage{weapon->damage,false,false,
                house?house->firepower_multiplier.value_or(1):1,source->firepower_multiplier.value_or(1),
                source_rank!=Rank::Rookie,source_rank==Rank::Elite,
                source_abilities.veteran.flag[AbilityFirepower],
                source_abilities.elite.flag[AbilityFirepower],
                VeterancyRules::load(merged_).veteran_combat};
            const auto amount=weapons::base_projectile_damage(damage);
            if (!amount || *amount<=0) {report.unresolved.push_back("projectile damage is unresolved");++it;continue;}
            PrototypeCombatProjectile shot;shot.source_id=source->id;shot.target_id=target->id;
            shot.fired_frame=frame;shot.weapon=weapon->name;shot.warhead=weapon->warhead;shot.damage=*amount;
            if (weapon->arcing && source->kind==session::world::Kind::Unit) {
                const auto dx=static_cast<long long>(target->position->x)-source->position->x;
                const auto dy=static_cast<long long>(target->position->y)-source->position->y;
                if (std::abs(dx)>100000 || std::abs(dy)>100000 || (!dx && !dy)) {++it;continue;}
                // The muzzle uses the present turret facing, which can still
                // be up to the fire tolerance away from the target.
                const auto turret=aim?aim->current:angles_.heading(static_cast<int>(dx),static_cast<int>(dy));
                // TODO-ORIGINAL: Resolve unknown hull facing and turret offset.
                const auto fallback_hull=(static_cast<std::uint32_t>(source->hull_facing.value_or(0))&255u)<<8;
                const auto hull=source->hull_facing_raw.value_or(static_cast<std::uint16_t>(fallback_hull));
                const auto muzzle=weapons::base_flat_unit_muzzle({*source->position,hull,turret,
                    weapon->fire_flh,0,source->burst_index.value_or(0),true});
                if (!muzzle) {report.unresolved.push_back("Unit muzzle is unresolved");++it;continue;}
                const auto delta_x=static_cast<long long>(target->position->x)-muzzle->x;
                const auto delta_y=static_cast<long long>(target->position->y)-muzzle->y;
                const auto delta_z=static_cast<long long>(target->position->z)-muzzle->z;
                if (std::abs(delta_x)>100000 || std::abs(delta_y)>100000 || std::abs(delta_z)>100000) {
                    report.unresolved.push_back("Cannon displacement is outside the supported domain");++it;continue;
                }
                const std::array<std::int32_t,3> delta{{static_cast<int>(delta_x),
                    static_cast<int>(delta_y),static_cast<int>(delta_z)}};
                const int horizontal=distance_.between(*muzzle,*target->position,true);
                const int speed=weapons::base_ballistic_launch_speed(horizontal,weapon->gravity,distance_);
                const bool lobber=weapons::base_lobber_branch({true,weapon->lobber,*source->position,target->position},distance_);
                const auto launch=angles_.tank_launch(speed,delta,weapon->gravity,lobber);
                if (!launch) {report.unresolved.push_back("Cannon arc is unresolved");++it;continue;}
                shot.flight.arc={{muzzle->x,muzzle->y,muzzle->z},launch->velocity,weapon->gravity};
                shot.flight.target=*target->position;shot.flight.source_id=source->id;
                if (!aim) {source->turret_facing_raw=turret;source->turret_facing=static_cast<int>(turret>>8);}
            }
            Random rng(world.random);
            // Base 006CB2D9..006CB2F3: count the shot, read Rearm_Delay
            // (006C9C00), then wrap the count by Burst.
            const int burst_index=source->burst_index.value_or(0)+1;
            std::int32_t burst_delays[4]={-1,-1,-1,-1};
            for (int i=0;i<4;++i)
                burst_delays[i]=prototype_int(base_,map_,source->type,"BurstDelay"+std::to_string(i)).value_or(-1);
            const auto in_burst=base_burst_delay(burst_index,weapon->burst,
                source->kind==session::world::Kind::Unit,burst_delays);
            int delay=0;
            if (in_burst.in_burst) delay=in_burst.needs_random?rng.ranged(3,5):in_burst.delay;
            else {
                const weapons::SingleShotDelayInput reload{weapon->rof,
                    house?house->reload_multiplier.value_or(1):1,false,false,false,false,1};
                delay=weapons::base_single_shot_unit_delay(reload,rng);
            }
            source->burst_index=burst_index%weapon->burst;
            source->weapon_timer_start_frame=static_cast<int>(frame);source->weapon_timer_duration=delay;
            world.random=rng.state();++report.fired;
            record(PrototypeCombatEventKind::Shot,shot,*target,*target->resolved_hp,*target->resolved_hp);

            if (weapon->arcing && source->kind==session::world::Kind::Unit) projectiles_.push_back(std::move(shot));
            else {
                // TODO-ORIGINAL: Small-arms and other non-Cannon projectiles
                // currently apply a direct hit without their original flight.
                impact(shot,*target->position,false);
            }
        } catch (const std::exception& e) {report.unresolved.push_back(e.what());}
        ++it;
    }
    // Base 006CDC40 Take_Damage calls 006D3B60 Can_Retaliate with the
    // attacker. When it passes, the victim assigns the attacker as its target
    // (virtual +2B8) and overrides its mission to attack it (virtual +1DC).
    // A human-controlled victim out of range skips that override when the
    // attacker is farther than (Sight + 0.5) * 256 leptons.
    // TODO-ORIGINAL: a computer victim with a current Techno target compares
    // 006D7550 threat values; this prototype keeps its current target. The
    // 005950D0 +8 game flag, transport (+230, +238) and PlayerScatter paths
    // are not resolved.
    for (const auto& [victim_id,attacker_id] : struck) {
        auto* victim=prototype_find(world,victim_id);
        const auto* attacker=prototype_find(world,attacker_id);
        if (!prototype_alive(victim) || !prototype_alive(attacker) || orders_.count(victim_id) ||
            victim->target_id.value_or(0) || victim->house_id==attacker->house_id ||
            !prototype_bool(base_,map_,victim->type,"CanRetaliate",true)) continue;
        const auto weapon=prototype_target_weapon(base_,map_,art_,world,*victim,*attacker);
        if (!weapon || !legal_guard_target(*victim,*attacker)) continue;
        const auto* owner=prototype_house(world,victim->house_id);
        const bool human=owner && owner->human_controlled.value_or(false);
        const bool building=victim->kind==session::world::Kind::Structure;
        // 006D3B60: a human object that is not a Building returns fire only in
        // Guard, Area Guard or Patrol unless [CombatDamage] PlayerReturnFire.
        // The prototype keeps the idle mission number during a Move order,
        // so a driving human object counts as being on Move here.
        if (human && !building && !prototype_bool(base_,map_,"CombatDamage","PlayerReturnFire") &&
            (victim->locomotor_moving.value_or(false) ||
             !(guard_mission(*victim) || victim->mission.value_or(-1)==25))) continue;
        const int d=distance_.between(*victim->position,*attacker->position);
        if (building || (human && d>weapon->range &&
            d>(prototype_int(base_,map_,victim->type,"Sight").value_or(0)*2+1)*128)) {
            // Without the attack override the target lasts while it is in range.
            if (d>weapon->range) continue;
            guard_orders_[victim_id]=GuardOrder{attacker_id,weapon->range,*victim->position,false};
        }
        orders_[victim_id]=attacker_id;victim->target_id=attacker_id;
    }
    return report;
}
}
