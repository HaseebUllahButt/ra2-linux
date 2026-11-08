#include "sim.h"
#include "world_init.h"
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ra2::sim {
namespace {
Fields fields(const Ini& base,const Ini& map,const std::string& name) {
    Fields out;
    if (auto s=base.find_section(name)) for (const auto& e:s->entries) out.emplace(e.key,e.value);
    if (auto s=map.find_section(name)) {
        Fields overlay;
        for (const auto& e:s->entries) overlay.emplace(e.key,e.value);
        for (const auto& e:overlay) out[e.first]=e.second;
    }
    return out;
}
std::string get(const Fields& f,const std::string& key) {
    const auto it=f.find(key); return it==f.end()?"":it->second;
}
std::optional<std::int32_t> number(const Fields& f,const std::string& key) {
    return Ini::integer_value(get(f,key));
}
Result failure(Error error,const std::string& text) { return {0,error,0,text}; }
void object_fields(Entity&,const ra2map::Structure&) {}
void object_fields(Entity& e,const ra2map::Unit& obj) { e.mission=obj.mission; }
void object_fields(Entity& e,const ra2map::Aircraft& obj) { e.mission=obj.mission; }
void object_fields(Entity& e,const ra2map::Infantry& obj) { e.mission=obj.mission; e.subcell=obj.subcell; }
bool equal_ascii_case(const std::string& a,const std::string& b) {
    if (a.size()!=b.size()) return false;
    auto upper=[](unsigned char c){return c>='a' && c<='z'?c-'a'+'A':c;};
    for (std::size_t i=0;i<a.size();++i) if (upper(a[i])!=upper(b[i])) return false;
    return true;
}
}
Simulation::Simulation(const Ini& base,const Ini& map,std::uint32_t seed,const TerrainCells& terrain,const TheaterInputs* theater):random_(seed),terrain_(terrain) {
    for (std::size_t i=0;i<terrain_.size();++i) {
        const auto cell=terrain_[i].cell;
        const auto key=static_cast<std::uint32_t>(cell.x)|(static_cast<std::uint32_t>(cell.y)<<16);
        if (!terrain_index_.emplace(key,i).second) throw std::invalid_argument("duplicate terrain input cell");
    }
    // Rules constructor 00665650 gives CliffBackImpassability an initial byte value of zero.
    const auto general_fields=fields(base,map,"General");
    const auto cliff=static_cast<std::uint8_t>(number(general_fields,"CliffBackImpassability").value_or(0));
    int map_width=0,map_height=0;
    if (theater) {
        const auto size=map.read_list("Map","Size");
        if (size.size()!=4) throw std::invalid_argument("original ordered tile setup needs Map.Size");
        const auto width=Ini::integer_value(size[2]),height=Ini::integer_value(size[3]);
        if (!width || !height) throw std::invalid_argument("invalid original Map.Size dimensions");
        map_width=*width; map_height=*height;
        // 00686B20 runs Cell setup before loading objects. Recompute floor heights now.
        initialize_original_tile_pass(terrain_,*theater,map_width,map_height,cliff,true);
    } else resolve_overlay_free_terrain(terrain_,cliff);
    const auto iq_fields=fields(base,map,"IQ");
    guard_area_iq_=number(iq_fields,"GuardArea").value_or(4); // 00665650 constructor, 00674240 reader.
    const auto max_iq=number(iq_fields,"MaxIQLevels").value_or(5);
    clear_fields_=fields(base,map,"Clear"); road_fields_=fields(base,map,"Road");
    if (const auto* trees=map.find_section("Terrain")) for (const auto& entry:trees->entries) {
        const auto packed=Ini::integer_value(entry.key);
        if (packed && *packed>=0) {
            // Modern packed map cells: original 0068BDC0, X=remainder, Y=quotient.
            const auto x=static_cast<std::uint32_t>(*packed%1000), y=static_cast<std::uint32_t>(*packed/1000);
            terrain_blockers_.insert(x|(y<<16));
        }
    }
    // Registry names remain authored inputs. No guessed constructor defaults.
    for (const auto* category:{"BuildingTypes","VehicleTypes","InfantryTypes","AircraftTypes"}) {
        const auto registry=fields(base,map,category);
        for (const auto& entry:registry) {
            const auto& name=entry.second;
            Type type; type.name=name; type.category=category;
            type.fields=fields(base,map,name);
            // ObjectType 005F92D0: Strength, Armor; TechnoType 00712170: weapon references.
            type.strength=number(type.fields,"Strength"); type.armor=get(type.fields,"Armor");
            type.cost=number(type.fields,"Cost"); type.speed=number(type.fields,"Speed");
            type.primary=get(type.fields,"Primary"); type.secondary=get(type.fields,"Secondary");
            for (const auto& weapon_name:{type.primary,type.secondary}) {
                if (weapon_name.empty() || weapons_.count(weapon_name)) continue;
                Weapon weapon; weapon.name=weapon_name; weapon.fields=fields(base,map,weapon_name);
                weapon.damage=number(weapon.fields,"Damage"); weapon.rof=number(weapon.fields,"ROF");
                weapon.range=get(weapon.fields,"Range"); weapon.projectile=get(weapon.fields,"Projectile");
                weapon.warhead=get(weapon.fields,"Warhead"); weapons_.emplace(weapon_name,std::move(weapon));
            }
            types_.emplace(name,std::move(type));
        }
    }
    if (auto houses=map.find_section("Houses")) for (const auto& entry:houses->entries) {
        if (std::any_of(state_.houses.begin(),state_.houses.end(),[&](const House& h){return h.name==entry.value;}))
            throw std::runtime_error("duplicate house: "+entry.value);
        House h; h.name=entry.value;
        h.iq=map.read_integer(h.name,"IQ",0); if (*h.iq>max_iq) h.iq=1; // 00500B40 +24C.
        h.country=map.read_string(h.name,"Country");
        h.allies=map.read_list(h.name,"Allies");
        if (!h.country.empty()) country_fields_.emplace(h.country,fields(base,map,h.country));
        // 00500B40: Credits*100; local player adjustments depend on session globals.
        const auto credits=map.read_integer(h.name,"Credits",0);
        const auto cash=static_cast<std::int64_t>(credits)*100;
        if (cash<0 || cash>std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("house Credits outside supported range: "+h.name);
        if (!map.read_bool(h.name,"PlayerControl",false)) h.cash=static_cast<std::int32_t>(cash);
        else state_.gaps.push_back("Unknown local player cash: "+h.name+"; 00500B40 session adjustment");
        state_.houses.push_back(std::move(h));
    }
    // Use the existing map and pack loaders, without copying or editing assets.
    std::ostringstream text;
    for (const auto& s:map.sections()) {
        text<<'['<<s.name<<"]\n";
        for (const auto& e:s.entries) text<<e.key<<'='<<e.value<<'\n';
    }
    ra2map::MapFile parsed; parsed.parse(text.str());
    auto append=[&](const auto& objects,Kind kind) {
        std::size_t source_index=0;
        const char* section_name=kind==Kind::Structure?"Structures":kind==Kind::Unit?"Units":kind==Kind::Infantry?"Infantry":"Aircraft";
        const auto* original_section=map.find_section(section_name);
        for (const auto& obj:objects) {
            const auto record=original_section && source_index<original_section->entries.size()?Ini::split_list(original_section->entries[source_index].value):std::vector<std::string>{};
            ++source_index;
            Entity e; e.id=static_cast<std::uint32_t>(state_.entities.size()+1); e.kind=kind;
            e.type=obj.type; e.house=obj.house; e.cell=obj.cell; e.facing=obj.facing; e.map_health=obj.health;
            auto t=types_.find(e.type);
            if (t==types_.end()) t=std::find_if(types_.begin(),types_.end(),[&](const auto& type){return equal_ascii_case(type.first,e.type);});
            const char* expected=kind==Kind::Structure?"BuildingTypes":kind==Kind::Unit?"VehicleTypes":kind==Kind::Infantry?"InfantryTypes":"AircraftTypes";
            if (t==types_.end() || t->second.category!=expected) {
                // 00743270 / base 00706AE0 gate creation on a valid type-registry index.
                state_.skipped_objects.push_back({kind,e.type,e.house,"type is absent from its original category registry"});
                continue;
            }
            e.type=t->first;
            // Base 006FC95E / Yuri 00737BCC set the separate turret facing from the map byte.
            if (kind==Kind::Unit) e.turret_facing=e.facing&255;
            object_fields(e,obj);
            if (kind==Kind::Unit && record.size()>11) {
                e.tag=record[7]; e.map_veterancy=Ini::integer_value(record[8]);
                e.map_group=Ini::integer_value(record[9]); e.map_follow=Ini::integer_value(record[11]);
                if (const auto n=Ini::integer_value(record[10])) e.map_above_bridge=*n!=0;
            }
            const auto key=static_cast<std::uint32_t>(e.cell.x)|(static_cast<std::uint32_t>(e.cell.y)<<16);
            const auto floor=terrain_index_.find(key);
            // 0051FB00 adds infantry subcell offsets. Other categories have separate placement handlers.
            // Expose only the ground vehicle center recovered by 00743270 in this map state.
            if (kind==Kind::Unit && e.map_above_bridge && !*e.map_above_bridge && floor!=terrain_index_.end() && terrain_[floor->second].floor_world_height) {
                e.position=Coord{e.cell.x*256+128,e.cell.y*256+128,*terrain_[floor->second].floor_world_height};
            }
            if (std::none_of(state_.houses.begin(),state_.houses.end(),[&](const House& h){return h.name==e.house;}))
                {
                House house; house.name=e.house;
                state_.houses.push_back(std::move(house));
                state_.gaps.push_back("Referenced house has no map definition: "+e.house);
            }
            if (e.map_health==256 && t->second.strength && *t->second.strength>0) e.hp=t->second.strength;
            else if (kind==Kind::Unit && t->second.strength && *t->second.strength>0 && e.map_health>=0 && e.map_health<=256) {
                // 007435DA..00743616: health*Strength/256, truncation, near-full snap, zero -> one.
                auto hp=static_cast<std::int32_t>(static_cast<std::int64_t>(e.map_health)* *t->second.strength/256);
                if (hp>*t->second.strength-3) hp=*t->second.strength;
                e.hp=hp==0?1:hp;
            }
            else state_.gaps.push_back("Unknown initial HP: entity "+std::to_string(e.id)+"; map health scaling or Strength missing");
            state_.entities.push_back(std::move(e));
        }
    };
    append(parsed.structures(),Kind::Structure);
    append(parsed.units(),Kind::Unit);
    append(parsed.infantry(),Kind::Infantry);
    append(parsed.aircraft(),Kind::Aircraft);
    if (theater) {
        // 00686B20 later calls 00568BB0, which runs a second setup pass after object loading.
        initialize_original_tile_pass(terrain_,*theater,map_width,map_height,cliff,false);
        state_.gaps.push_back("Ordered tile setup excludes overlay land decisions, bridge/tube objects, animations, shadows, and passability cache effects");
    }
    state_.gaps.push_back("Map missions are loaded, not executed; autonomous object updates are unsupported");
}
Result Simulation::issue(const Command& c) {
    if (c.frame<state_.frame) return failure(Error::PastFrame,"command frame has passed");
    if (c.op==Op::Move && !drive_tables_) return failure(Error::Unsupported,"Move needs load_ground_assets and resolved original terrain");
    if (c.op==Op::Attack || c.op==Op::Produce || c.op==Op::Stop)
        return failure(Error::Unsupported,"unrecovered command semantics; inspect capabilities()");
    if (c.op!=Op::TakeCash && c.op!=Op::ResolvedDamage && c.op!=Op::RandomRange && c.op!=Op::Move)
        return failure(Error::Unsupported,"unknown command opcode");
    if (c.op==Op::RandomRange && std::abs(static_cast<std::int64_t>(c.amount)-c.high)>std::numeric_limits<std::int32_t>::max())
        return failure(Error::InvalidAmount,"random range exceeds signed original span");
    Result result; result.ticket=next_ticket_++;
    queue_.push_back({c,result.ticket}); return result;
}
Result Simulation::execute(const Command& c) {
    if (c.op==Op::Move) return start_move(c);
    if (c.op==Op::RandomRange) return {0,Error::None,random_.ranged(c.amount,c.high),{}};
    if (c.op==Op::TakeCash) {
        if (c.amount<0) return failure(Error::InvalidAmount,"negative debit unsupported");
        auto it=std::find_if(state_.houses.begin(),state_.houses.end(),[&](const House& h){return h.name==c.house;});
        if (it==state_.houses.end()) return failure(Error::InvalidHouse,"house does not exist");
        if (!it->cash) return failure(Error::UnknownMoney,"local player cash adjustment unresolved");
        // 004F9790: cash-only branch; this world has no stored ore.
        auto paid=std::min(*it->cash,c.amount); *it->cash-=paid; it->spent+=paid;
        return {0,Error::None,paid,{}};
    }
    if (!c.entity || c.entity>state_.entities.size()) return failure(Error::InvalidEntity,"entity does not exist");
    auto& e=state_.entities[c.entity-1];
    if (!e.hp) return failure(Error::UnknownHealth,"initial HP is unresolved");
    // 005F5390 forced damage path, arithmetic only; bypass armor and immunity.
    // Building minimum damage and infantry death mutations need derived handlers.
    if (e.kind==Kind::Structure || e.kind==Kind::Infantry)
        return failure(Error::Unsupported,"derived damage handler is not recovered for this category");
    if (e.dead || c.amount==0) return {};
    if (e.moving) return failure(Error::Unsupported,"forced health changes during drive movement need the original speed and death handlers");
    const auto maximum=types_.at(e.type).strength;
    if (!maximum) return failure(Error::UnknownHealth,"Strength missing");
    const auto old=*e.hp;
    const auto changed=static_cast<std::int64_t>(old)-c.amount;
    e.hp=static_cast<std::int32_t>(std::clamp<std::int64_t>(changed,0,*maximum));
    e.dead=*e.hp==0;
    if (e.dead) { e.moving=false; e.move_goal.reset(); movements_.erase(e.id); }
    return {0,Error::None,old-*e.hp,{}};
}
void Simulation::tick() {
    if (state_.frame==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("native frame overflow");
    state_.results.clear();
    // Original frame updates precede event dispatch (0055AFB0 -> 00647260).
    update_movement();
    std::vector<Pending> future;
    for (const auto& pending:queue_) {
        if (pending.command.frame>state_.frame) { future.push_back(pending); continue; }
        auto result=execute(pending.command); result.ticket=pending.ticket;
        state_.results.push_back(std::move(result));
    }
    queue_=std::move(future); ++state_.frame;
}
Snapshot Simulation::snapshot() const {
    auto out=state_; out.queued=queue_.size(); out.random_table=random_.table();
    out.random_first=random_.first(); out.random_second=random_.second(); return out;
}
std::vector<Capability> Simulation::capabilities() {
    return {
        {"map_objects",true,"Authored objects, houses and missions; partial vehicle HP recovered"},
        {"rules",true,"Authored type/weapon values with map overlay; no guessed defaults"},
        {"random",true,"0065C6D0/780/7E0; gamemd constants; signed nonoverflow spans"},
        {"native_commands",true,"Scheduled FIFO; not original event wire format or autonomous AI"},
        {"cash_debit",true,"004F9790 cash-only; no ore; local player initialization unresolved"},
        {"resolved_health_arithmetic",true,"005F5390 forced arithmetic for vehicles/aircraft; dead entities remain as records"},
        {"death_effects",false,"Derived death handlers, triggers, passengers and scoring are missing"},
        {"ground_primitives",true,"ground.h: guarded destination setter and original curve transforms; no map movement tick"},
        {"damage_primitives",true,"combat.h: 00489180 armor/range arithmetic in explicit precision domain"},
        {"target_primitives",true,"combat.h: resolved candidate maximum and original threat category flags"},
        {"movement",true,"Aligned adjacent Move, or supplied original path with straight steps and one final curve; flat clear/road cells; no path search"},
        {"target_selection",false,"Threat arithmetic recovered; full 006F7CA0 eligibility and bounded scans remain incomplete"},
        {"combat",false,"Armor/range arithmetic recovered; projectile and derived damage handlers remain incomplete"},
        {"production",false,"Cash-only Factory primitive available; 006F47A0 complete formula and placement unresolved"}
    };
}
}
