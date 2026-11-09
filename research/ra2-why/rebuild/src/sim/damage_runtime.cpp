#include "damage_runtime.h"
#include "../loop/profile.h"
#include <map>
#include <set>
#include <cstdio>
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
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
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
    return e && (e->kind==Kind::Unit || e->kind==Kind::Infantry || e->kind==Kind::Structure) &&
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
    double gravity=0,condition_red=0;
    float spread=0,percent=0;
    bool arcing=false,lobber=false;
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
    out.lobber=prototype_bool(base,map,out.name,"Lobber");
    // TODO-ORIGINAL: Use constructor values when these rule keys are absent.
    out.spread=static_cast<float>(prototype_double(base,map,out.warhead,"CellSpread").value_or(0));
    out.percent=static_cast<float>(prototype_double(base,map,out.warhead,"PercentAtMax").value_or(1));
    out.max_damage=prototype_int(base,map,"CombatDamage","MaxDamage").value_or(10000);
    out.condition_red=prototype_double(base,map,"AudioVisual","ConditionRed").value_or(0.25);
    out.gravity=prototype_double(base,map,"AudioVisual","Gravity").value_or(6);
    if (out.spread<0 || out.spread>10 || out.percent<0 || out.max_damage<=0 ||
        out.condition_red<0 || out.condition_red>1 || !std::isfinite(out.gravity) || out.gravity<=0)
        return {};
    auto image=prototype_string(base,map,source.type,"Image");
    if (image.empty()) image=source.type;
    const auto flh=art.read_list(image,"PrimaryFireFLH");
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
    if (links.empty()) return false;
    for (auto* link:links) *link=*victim.next;
    victim.next=0;
    // TODO-ORIGINAL: Replace with Unit Mark(0), its Cell setup, and reserved
    // moving-cell cleanup. Do not clear occupancy for a Cell with survivors.
    for (auto* cell:emptied) if (cell->ground_occupancy_124) cell->ground_occupancy_124=0;
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
    // TODO-ORIGINAL: Resolve missing country, veteran, immunity and special
    // virtual state from the loaded actor instead of prototype defaults.
    techno.house_armor_multiplier=target->house_armor_multiplier.value_or(1);
    techno.object_armor_multiplier=target->armor_multiplier.value_or(1);
    techno.veteran_armor_multiplier=1;techno.attacker_present=source_id!=0;techno.warhead_present=true;
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
    if (status==4 && !prototype_unlink(world,*target))
        return {false,false,"dead actor has unresolved Cell links"};
    target->resolved_hp=health;
    if (status && target->techno_field_329) target->techno_field_329=1;
    if (status==4) {
        target->active=false;target->in_limbo=true;target->target_id=0;target->lifetime=Lifetime::Destroyed;
    }
    return {true,status==4,{}};
}
}

PrototypeCombatRuntime::PrototypeCombatRuntime(const IniFile& base,const IniFile& map,
    const IniFile& art,const std::string& exe)
    :base_(base),map_(map),art_(art),distance_(OriginalDistance::from_executable(exe)),
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
    const auto weapon=prototype_weapon(base_,map_,art_,*source);
    if (!weapon) return false;
    const auto verse=prototype_verse(base_,map_,*weapon,*target);
    return verse && std::isfinite(*verse) && *verse>0;
}
bool PrototypeCombatRuntime::attack(session::world::Snapshot& world,std::uint32_t source_id,std::uint32_t target_id) {
    if (!can_attack(world,source_id,target_id)) return false;
    auto* source=prototype_find(world,source_id);
    guard_orders_.erase(source_id);
    orders_[source_id]=target_id;source->target_id=target_id;return true;
}
void PrototypeCombatRuntime::stop(session::world::Snapshot& world,std::uint32_t source_id) {
    guard_orders_.erase(source_id);
    orders_.erase(source_id);
    if (auto* source=prototype_find(world,source_id)) source->target_id=0;
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
    // TODO-ORIGINAL: The prototype advances saved bullets before attack orders.
    // Recover global object update order before treating this as replay exact.
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
        for (const auto& candidate:candidates) {
            auto* target=prototype_find(world,candidate.id);
            if (!prototype_alive(target) || candidate.distance>radius) continue;
            auto hit=prototype_damage(world,candidate.id,shot.source_id,shot.damage,
                                      candidate.distance,*weapon,base_,map_);
            if (!hit.missing.empty()) report.unresolved.push_back(hit.missing);
            hit_any|=hit.applied;
            if (hit.destroyed) ++report.destroyed;
        }
        if (!hit_any && prototype_alive(aimed) && prototype_cell(*aimed->position)==prototype_cell(position)) {
            // TODO-ORIGINAL: Permit a direct hit when the flat prototype's
            // impact height misses the original area distance check.
            auto hit=prototype_damage(world,aimed->id,shot.source_id,shot.damage,0,*weapon,base_,map_);
            if (!hit.missing.empty()) report.unresolved.push_back(hit.missing);
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
    // Guard targets stay local. Explicit attack orders retain their chase target.
    for (auto it=guard_orders_.begin();it!=guard_orders_.end();) {
        const auto order=orders_.find(it->first);
        if (order==orders_.end() || order->second!=it->second) {
            it=guard_orders_.erase(it); continue;
        }
        auto* source=prototype_find(world,it->first);
        const auto* target=prototype_find(world,it->second);
        const auto weapon=prototype_alive(source)?prototype_weapon(base_,map_,art_,*source):
            std::optional<PrototypeWeapon>{};
        if (!weapon || !prototype_alive(target) || source->locomotor_moving.value_or(false) ||
            distance_.between(*source->position,*target->position)>weapon->range ||
            !legal_guard_target(*source,*target)) {
            if (source) source->target_id=0;
            orders_.erase(order);
            it=guard_orders_.erase(it);
        } else ++it;
    }
    // Acquire only for idle actors. Explicit attack and move orders keep priority.
    // TODO-ORIGINAL: Recover the full guard scan and threat scoring.
    for (auto& source : world.entities) {
        if (!prototype_alive(&source) || orders_.count(source.id) ||
            source.target_id.value_or(0) || source.locomotor_moving.value_or(false) ||
            !source.mission || (*source.mission!=0 && *source.mission!=5 && *source.mission!=11)) continue;
        const auto weapon=prototype_weapon(base_,map_,art_,source);
        if (!weapon) continue;
        const auto* owner=prototype_house(world,source.house_id);
        if (!owner || owner->country=="Neutral" || owner->country=="Special") continue;
        if (prototype_bool(base_,map_,source.type,"Powered") &&
            owner->power_output.value_or(0)<owner->power_drain.value_or(0)) continue;
        const session::world::Entity* nearest=nullptr;
        int best=weapon->range+1;
        for (const auto& target : world.entities) {
            if(!prototype_alive(&target) || source.house_id==target.house_id)continue;
            const int range=distance_.between(*source.position,*target.position);
            if(range>weapon->range || range>best)continue;
            if (!legal_guard_target(source,target)) continue;
            if (range<=weapon->range && (range<best || (range==best && nearest && target.id<nearest->id))) {
                best=range; nearest=&target;
            }
        }
        if (nearest) {
            orders_[source.id]=nearest->id; guard_orders_[source.id]=nearest->id;
            source.target_id=nearest->id;
        }
    }
    for (auto it=orders_.begin();it!=orders_.end();) {
        auto* source=prototype_find(world,it->first);auto* target=prototype_find(world,it->second);
        if (!prototype_alive(source) || !prototype_alive(target)) {
            if (source) source->target_id=0;
            it=orders_.erase(it);continue;
        }
        try {
            const auto weapon=prototype_weapon(base_,map_,art_,*source);
            const auto verse=weapon?prototype_verse(base_,map_,*weapon,*target):std::optional<double>{};
            if (!weapon || !verse || !std::isfinite(*verse) || *verse<=0) {
                if (!weapon || !verse || !std::isfinite(*verse))
                    report.unresolved.push_back("weapon or target armor is unresolved");
                source->target_id=0;it=orders_.erase(it);continue;
            }
            const int d=distance_.between(*source->position,*target->position);
            if (d>weapon->range) {report.out_of_range.emplace_back(source->id,target->id);++it;continue;}
            // TODO-ORIGINAL: Replace these direct range and targeting checks
            // with the full mission, eligibility, terrain and elevation paths.
            // TODO-ORIGINAL: An unknown timer starts ready in this prototype.
            const weapons::FireTimer timer{source->weapon_timer_start_frame.value_or(-1),
                                           source->weapon_timer_duration.value_or(0)};
            if (!weapons::base_fire_timer_ready(timer,static_cast<int>(frame))) {++it;continue;}
            const auto* house=prototype_house(world,source->house_id);
            weapons::ProjectileDamageInput damage{weapon->damage,false,false,
                house?house->firepower_multiplier.value_or(1):1,source->firepower_multiplier.value_or(1),
                false,false,false,false,1};
            // TODO-ORIGINAL: Resolve missing veteran abilities and country multipliers.
            const auto amount=weapons::base_projectile_damage(damage);
            if (!amount || *amount<=0) {report.unresolved.push_back("projectile damage is unresolved");++it;continue;}
            PrototypeCombatProjectile shot;shot.source_id=source->id;shot.target_id=target->id;
            shot.weapon=weapon->name;shot.warhead=weapon->warhead;shot.damage=*amount;
            if (weapon->arcing && source->kind==session::world::Kind::Unit) {
                const auto dx=static_cast<long long>(target->position->x)-source->position->x;
                const auto dy=static_cast<long long>(target->position->y)-source->position->y;
                if (std::abs(dx)>100000 || std::abs(dy)>100000 || (!dx && !dy)) {++it;continue;}
                const auto turret=angles_.heading(static_cast<int>(dx),static_cast<int>(dy));
                // TODO-ORIGINAL: Resolve unknown hull facing, turret offset, and burst state.
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
                // TODO-ORIGINAL: Turret turning is instantaneous in this prototype.
                source->turret_facing_raw=turret;source->turret_facing=static_cast<int>(turret>>8);
            }
            Random rng(world.random);
            const weapons::SingleShotDelayInput reload{weapon->rof,
                house?house->reload_multiplier.value_or(1):1,false,false,false,false,1};
            const int delay=weapons::base_single_shot_unit_delay(reload,rng);
            source->weapon_timer_start_frame=static_cast<int>(frame);source->weapon_timer_duration=delay;
            world.random=rng.state();++report.fired;
            if (std::getenv("RA2_COMBAT_TRACE"))
                std::fprintf(stderr,"combat frame=%llu source=%u type=%s weapon=%s range=%d distance=%d target=%u target_type=%s hp=%d reload=%d\n",
                    static_cast<unsigned long long>(frame),source->id,source->type.c_str(),weapon->name.c_str(),
                    weapon->range,d,target->id,target->type.c_str(),target->resolved_hp.value_or(0),delay);

            if (weapon->arcing && source->kind==session::world::Kind::Unit) projectiles_.push_back(std::move(shot));
            else {
                // TODO-ORIGINAL: Small-arms and other non-Cannon projectiles
                // currently apply a direct hit without their original flight.
                impact(shot,*target->position,false);
            }
        } catch (const std::exception& e) {report.unresolved.push_back(e.what());}
        ++it;
    }
    return report;
}
}
