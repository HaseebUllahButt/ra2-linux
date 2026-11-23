#include "prototype_production.h"
#include "prototype_tech.h"
#include "build_time.h"
#include "damage_runtime.h"
#include "sim.h"
#include "session/world/setup.h"
#include "session/world/world_hash.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ra2;
namespace w=ra2::session::world;
namespace p=ra2::sim::production;
namespace proto=ra2::sim::production::prototype;

namespace {
void require(bool condition,const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void done(const p::Outcome& result,const std::string& operation) {
    if (result.status!=p::Status::Done)
        throw std::runtime_error(operation+": "+result.detail+" (status "+std::to_string(int(result.status))+")");
}
w::Entity& actor(w::Snapshot& world,std::uint32_t id) {
    for (auto& item:world.entities) if (item.id==id) return item;
    throw std::runtime_error("missing actor "+std::to_string(id));
}
p::FactoryState* factory(p::FactorySet& factories,std::uint32_t id) {
    for (auto& item:factories.factories) if (item.object==id) return &item;
    return nullptr;
}
void require_prototype(const w::Snapshot& world) {
    require(world.setup_progress==w::SetupProgress::PrototypeComplete && w::validate(world),
            "production changed the prototype phase or links");
}
void destroy_actor(w::Snapshot& world,std::uint32_t id) {
    auto& item=actor(world,id);
    require(item.next==0,"test actor has a nonempty shared link");
    for (auto& [key,cell]:world.cells) {
        (void)key;
        if (!cell.heads) continue;
        if (cell.heads->ground_head==id) {
            cell.heads->ground_head=0;
            if ((item.kind==w::Kind::Unit || item.kind==w::Kind::Infantry) && cell.ground_occupancy_124)
                *cell.ground_occupancy_124&=~0x20u;
        }
        if (cell.heads->bridge_head==id) cell.heads->bridge_head=0;
    }
    item.lifetime=w::Lifetime::Destroyed; item.resolved_hp=0; item.active=false; item.in_limbo=true;
    item.on_map=false; item.next=0; item.object_field_74=0; item.object_field_76=1;
}

// A product must leave on the ground in front of its building: lower on
// the screen than every footprint Cell, so the roof art never covers it.
void require_in_front(const w::Snapshot& world,const p::Catalog& catalog,std::uint32_t house,
                      const std::string& building_type,const w::Entity& product) {
    require(product.cell.has_value(),building_type+" product has no Cell");
    const auto* facts=catalog.find(building_type);
    require(facts,"missing rules for "+building_type);
    int front=-1;
    for (const auto& item:world.entities) {
        if (item.house_id!=house || item.type!=building_type || item.lifetime!=w::Lifetime::Placed || !item.cell) continue;
        for (const auto& offset:p::original_foundation_cells(facts->placement.foundation)) {
            const int x=item.cell->x+offset.x,y=item.cell->y+offset.y;
            require(x!=product.cell->x || y!=product.cell->y,building_type+" product stands on its roof");
            front=std::max(front,x+y);
        }
    }
    require(front>=0,"no placed "+building_type);
    require(int(product.cell->x)+product.cell->y>front,
            building_type+" product left behind the building art");
}

int count_at(const std::optional<std::map<std::int32_t,std::int32_t>>& counts,std::int32_t index) {
    require(counts.has_value(),"counter is unknown");
    const auto item=counts->find(index);
    return item==counts->end() ? 0 : item->second;
}

std::uint32_t build(w::Snapshot& world,p::FactorySet& factories,const p::Catalog& catalog,
                    std::uint32_t house,const std::string& type,std::uint64_t& frame,bool full_run,
                    bool block_exit=false) {
    const auto outcome=proto::begin_production(world,factories,catalog,house,type,frame);
    done(outcome,"begin "+type);
    require(outcome.entity.has_value(),"factory object is missing");
    const auto id=*outcome.entity;
    auto* state=factory(factories,id);
    require(state && state->interval>0,"factory is not running");
    if (!full_run) state->progress=53; // Test the completion boundary without waiting for 53 stages.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> saved;
    while (state && state->progress<54) {
        if (block_exit && state->progress==53 && saved.empty()) {
            for (auto& item:world.cells) if (item.second.ground_occupancy_124) {
                saved.push_back({item.first,*item.second.ground_occupancy_124});
                *item.second.ground_occupancy_124|=0x20;
            }
        }
        frame=std::max(frame+1,state->timer_start+std::uint64_t(state->timer_duration));
        done(proto::tick(world,factories,catalog,frame),"tick "+type);
        require_prototype(world);
        state=factory(factories,id);
        if (block_exit && state && state->progress==54) {
            require(actor(world,id).lifetime==w::Lifetime::Created,"blocked exit released an actor");
            const auto* first_cell=&world.cells.begin()->second;
            done(proto::tick(world,factories,catalog,++frame),"wait for exit "+type);
            require(first_cell==&world.cells.begin()->second && factory(factories,id) &&
                    factory(factories,id)->progress==54,"blocked exit replaced the world or lost its actor");
            for (const auto& item:saved) world.cells.at(item.first).ground_occupancy_124=item.second;
            done(proto::tick(world,factories,catalog,++frame),"retry exit "+type);
            require_prototype(world);
            state=factory(factories,id);
            break;
        }
    }
    return id;
}

p::CellXY place_building(w::Snapshot& world,p::FactorySet& factories,const p::Catalog& catalog,
                         std::uint32_t house,const std::string& type,std::uint64_t frame,ra2map::Cell base) {
    const auto* facts=catalog.find(type);
    require(facts,"missing building rules for "+type);
    const auto& offsets=p::original_foundation_cells(facts->placement.foundation);
    require(!offsets.empty(),"missing building footprint for "+type);
    for (int radius=1;radius<=20;++radius) for (int dy=-radius;dy<=radius;++dy)
        for (int dx=-radius;dx<=radius;++dx) {
            if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
            const int x=int(base.x)+dx,y=int(base.y)+dy;
            if (x<0 || y<0 || x>=512 || y>=512) continue;
            const p::CellXY origin{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
            if (proto::passes_proximity(world,factories,catalog,house,type,origin)!=true) continue;
            bool clear=true;
            for (const auto& offset:offsets) {
                const int xx=x+offset.x,yy=y+offset.y;
                if (xx<0 || yy<0 || xx>=512 || yy>=512) { clear=false; break; }
                const auto found=world.cells.find(std::uint32_t(yy)*512+xx);
                if (found==world.cells.end() || !found->second.heads || found->second.heads->ground_head ||
                    !found->second.ground_occupancy_124 || *found->second.ground_occupancy_124 ||
                    !found->second.terrain || found->second.terrain->overlay_type_index!= -1 ||
                    found->second.terrain->ramp_index!=0) { clear=false; break; }
            }
            if (!clear) continue;
            const auto outcome=proto::place(world,factories,catalog,house,type,origin,frame);
            if (outcome.status==p::Status::Done) { require_prototype(world); return origin; }
        }
    throw std::runtime_error("no place for "+type);
}

// A mouse Cell whose whole cursor grid is legal, searched in rings around
// the yard. The original grid is centred on the mouse (00498F10).
std::optional<p::CellXY> legal_mouse(const w::Snapshot& world,const p::FactorySet& factories,const p::Catalog& catalog,
                                     std::uint32_t house,const std::string& type,ra2map::Cell base,int rings) {
    for (int radius=0;radius<=rings;++radius) for (int dy=-radius;dy<=radius;++dy)
        for (int dx=-radius;dx<=radius;++dx) {
            if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
            const p::CellXY mouse{std::int16_t(base.x+1+dx),std::int16_t(base.y+1+dy)};
            const auto cursor=proto::placement_cursor(world,factories,catalog,house,type,mouse);
            require(cursor.has_value(),"no cursor for "+type);
            bool every=!cursor->cells.empty();
            for (const auto& cell:cursor->cells) every=every && cell.legal_cell==true;
            require(every==(cursor->proximity==true && cursor->legal==true),
                    type+" grid Cells disagree with the whole placement");
            if (every) return mouse;
        }
    return std::nullopt;
}

// Place type with the mouse on a legal grid near the yard, as a player clicks.
p::CellXY place_at_mouse(w::Snapshot& world,p::FactorySet& factories,const p::Catalog& catalog,std::uint32_t house,
                         const std::string& type,std::uint64_t frame,ra2map::Cell base,p::CellXY offset) {
    const auto mouse=legal_mouse(world,factories,catalog,house,type,base,6);
    require(mouse.has_value(),"no legal "+type+" grid within six Cells of the yard");
    const auto origin=proto::placement_origin(catalog,type,*mouse);
    require(origin && origin->x==mouse->x+offset.x && origin->y==mouse->y+offset.y,
            type+" grid is not centred on the mouse");
    std::size_t count=world.entities.size();
    done(proto::place(world,factories,catalog,house,type,*origin,frame),"place "+type+" at the mouse");
    require_prototype(world);
    bool found=false;
    for (const auto& item:world.entities) if (item.type==type && item.cell && item.cell->x==origin->x &&
        item.cell->y==origin->y && item.lifetime==w::Lifetime::Placed) found=true;
    require(found && world.entities.size()>=count,type+" is not at the cursor origin");
    return *origin;
}

// Units in the footprint block the Cell (004718B0), like the original. Only
// that Cell turns red; the click is rejected and the product stays ready.
// Only the House's own units count: own infantry and own vehicles block.
int check_units_block(const w::Snapshot& world,const p::FactorySet& factories,const p::Catalog& catalog,
                      std::uint32_t house,const std::string& type,std::uint32_t product,std::uint64_t frame,
                      bool& other_cell_legal,int& infantry_blocks,int& vehicle_blocks) {
    int checked=0;
    for (const auto& unit:world.entities) {
        if (unit.house_id!=house || unit.lifetime!=w::Lifetime::Placed || !unit.cell ||
            unit.in_limbo==true || (unit.kind!=w::Kind::Unit && unit.kind!=w::Kind::Infantry)) continue;
        const p::CellXY mouse{std::int16_t(unit.cell->x+1),std::int16_t(unit.cell->y+1)};
        const auto cursor=proto::placement_cursor(world,factories,catalog,house,type,mouse);
        require(cursor.has_value(),"no cursor over a unit");
        if (cursor->proximity!=true) continue;
        require(cursor->legal==false,type+" grid accepted a unit in the footprint");
        bool unit_cell=false;
        for (const auto& cell:cursor->cells) {
            if (cell.cell.x==unit.cell->x && cell.cell.y==unit.cell->y) {
                require(cell.legal_cell==false,type+" grid showed a unit Cell as legal");
                unit_cell=true;
            } else if (cell.legal_cell==true) other_cell_legal=true;
        }
        require(unit_cell,type+" grid does not cover the unit under the mouse");
        auto blocked=world; auto blocked_factories=factories;
        const auto outcome=proto::place(blocked,blocked_factories,catalog,house,type,cursor->origin,frame);
        require(outcome.status==p::Status::Rejected,type+" over a unit was not rejected: "+outcome.detail);
        // The product stays ready for the next click.
        require(factory(blocked_factories,product) && factory(blocked_factories,product)->progress==54 &&
                blocked.entities.size()==world.entities.size() &&
                actor(blocked,product).lifetime==w::Lifetime::Created,
                type+" rejected over a unit lost its product");
        // Nothing moves: every entity Cell and position and every Cell head
        // and occupancy count stays exactly the same after the rejection.
        const auto& after=actor(blocked,unit.id);
        require(after.cell && after.cell->x==unit.cell->x && after.cell->y==unit.cell->y &&
                after.position.has_value()==unit.position.has_value() &&
                (!after.position || (after.position->x==unit.position->x &&
                 after.position->y==unit.position->y && after.position->z==unit.position->z)),
                type+" rejection moved the blocking unit");
        require(world_hash(blocked)==world_hash(world),
                type+" rejected over a unit changed the world");
        // The hash reads the main fields only. Compare every Cell's occupancy
        // words and heads directly, and sweep every entity Cell and position.
        for (const auto& [key,cell]:world.cells) {
            const auto& after_cell=blocked.cells.at(key);
            require(cell.ground_occupancy_124==after_cell.ground_occupancy_124 &&
                    cell.bridge_occupancy_128==after_cell.bridge_occupancy_128,
                    type+" rejection changed a Cell occupancy word");
            require(cell.heads.has_value()==after_cell.heads.has_value() &&
                    (!cell.heads || (cell.heads->ground_head==after_cell.heads->ground_head &&
                     cell.heads->bridge_head==after_cell.heads->bridge_head)),
                    type+" rejection changed a Cell head");
        }
        for (const auto& item:world.entities) {
            const auto& after_entity=actor(blocked,item.id);
            require(item.cell.has_value()==after_entity.cell.has_value() &&
                    (!item.cell || (item.cell->x==after_entity.cell->x && item.cell->y==after_entity.cell->y)) &&
                    item.position.has_value()==after_entity.position.has_value() &&
                    (!item.position || (item.position->x==after_entity.position->x &&
                     item.position->y==after_entity.position->y && item.position->z==after_entity.position->z)),
                    type+" rejection moved an entity");
        }
        (unit.kind==w::Kind::Infantry?infantry_blocks:vehicle_blocks)+=1;
        ++checked;
    }
    return checked;
}

// Rejections that never touch the world: no ready product in the factory,
// a far Cell outside base proximity, and the occupied yard footprint.
void check_rejection_cases(const w::Snapshot& world,const p::FactorySet& factories,const p::Catalog& catalog,
                           std::uint32_t house,ra2map::Cell base,const std::string& type,std::uint64_t frame) {
    const auto before=world_hash(world);
    // No completed product: the click is rejected before any write. The
    // factory copy keeps its flags and inputs; only the type's own factory
    // loses its object and progress. An empty FactorySet would lose the
    // setup flags and prove a different failure.
    {
        auto no_ready=world; auto no_ready_factories=factories;
        bool cleared=false;
        for (auto& item:no_ready_factories.factories)
            if (item.house_id==house && item.object_type==type) {
                item.object=0; item.progress=0; cleared=true;
            }
        require(cleared,type+" has no factory state to clear");
        const auto outcome=proto::place(no_ready,no_ready_factories,catalog,house,type,
            {static_cast<std::int16_t>(base.x+2),static_cast<std::int16_t>(base.y+2)},frame);
        require(outcome.status==p::Status::Rejected && outcome.detail=="no completed building",
                "place without a ready factory was not 'no completed building': "+outcome.detail);
        require(world_hash(no_ready)==before,"place without a ready factory changed the world");
    }
    // A far Cell: the grid is never legal, and the click is rejected.
    {
        bool found=false;
        for (int radius=24;radius<=40 && !found;++radius)
            for (int dy=-radius;dy<=radius && !found;++dy)
                for (int dx=-radius;dx<=radius && !found;++dx) {
                    if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
                    const p::CellXY mouse{static_cast<std::int16_t>(base.x+dx),static_cast<std::int16_t>(base.y+dy)};
                    if (mouse.x<0 || mouse.y<0 || mouse.x>=512 || mouse.y>=512) continue;
                    const auto it=world.cells.find(std::uint32_t(mouse.y)*512+mouse.x);
                    if (it==world.cells.end() || !it->second.allocated || !*it->second.allocated) continue;
                    const auto cursor=proto::placement_cursor(world,factories,catalog,house,type,mouse);
                    if (!cursor || cursor->proximity==true) continue;
                    // Proximity is false, so the whole grid must read illegal.
                    require(cursor->legal==false,
                            type+" far grid is legal outside base proximity");
                    auto far=world; auto far_factories=factories;
                    const auto outcome=proto::place(far,far_factories,catalog,house,type,cursor->origin,frame);
                    require(outcome.status==p::Status::Rejected,"far click was accepted: "+outcome.detail);
                    require(world_hash(far)==before,"far click changed the world");
                    found=true;
                }
        require(found,"no far Cell outside base proximity was checked");
    }
    // The yard's own footprint: occupied Cells, so the click is rejected.
    {
        const auto cursor=proto::placement_cursor(world,factories,catalog,house,type,
            {static_cast<std::int16_t>(base.x),static_cast<std::int16_t>(base.y)});
        require(cursor && cursor->legal==false,"grid over the yard is legal");
        auto blocked=world; auto blocked_factories=factories;
        const auto outcome=proto::place(blocked,blocked_factories,catalog,house,type,cursor->origin,frame);
        require(outcome.status==p::Status::Rejected,"click on the yard was accepted: "+outcome.detail);
        require(world_hash(blocked)==before,"click on the yard changed the world");
    }
}

// Direct ra2::sim::production::place checks. The adapter's preflight hides
// pre-Undo mutations, so these call the runtime place itself on a Complete
// world copy. A footprint over an own unit must be rejected with no write,
// even before the Undo copy.
void check_direct_place(const w::Snapshot& world,const p::FactorySet& factories,const p::Catalog& catalog,
                        std::uint32_t house,const std::string& type,std::uint64_t frame) {
    const w::Entity* blocker=nullptr;
    for (const auto& unit:world.entities) {
        if (unit.house_id!=house || unit.lifetime!=w::Lifetime::Placed || !unit.cell ||
            unit.in_limbo==true || (unit.kind!=w::Kind::Unit && unit.kind!=w::Kind::Infantry)) continue;
        const auto cursor=proto::placement_cursor(world,factories,catalog,house,type,
            {static_cast<std::int16_t>(unit.cell->x+1),static_cast<std::int16_t>(unit.cell->y+1)});
        if (cursor && cursor->proximity==true) { blocker=&unit; break; }
    }
    require(blocker,"no own unit stands in a legal grid near the base");
    auto reference=world; reference.setup_progress=w::SetupProgress::Complete;
    const auto before=world_hash(reference);
    const p::CellXY origin{static_cast<std::int16_t>(blocker->cell->x),static_cast<std::int16_t>(blocker->cell->y)};
    // A ready product under the unit: rejected, and nothing moves first.
    {
        auto direct=world; auto direct_factories=factories;
        direct.setup_progress=w::SetupProgress::Complete;
        const auto outcome=p::place(direct,direct_factories,catalog,house,type,origin,frame);
        require(outcome.status==p::Status::Rejected,"direct place over a unit was not rejected: "+outcome.detail);
        require(world_hash(direct)==before,"direct place over a unit changed the world before its rejection");
    }
    // No ready product on the same occupied footprint: still no mutation.
    {
        auto direct=world; auto direct_factories=factories;
        direct.setup_progress=w::SetupProgress::Complete;
        bool cleared=false;
        for (auto& item:direct_factories.factories)
            if (item.house_id==house && item.object_type==type) { item.object=0; item.progress=0; cleared=true; }
        require(cleared,type+" has no factory state to clear");
        const auto outcome=p::place(direct,direct_factories,catalog,house,type,origin,frame);
        require(outcome.status==p::Status::Rejected,"direct no-ready place was not rejected: "+outcome.detail);
        require(world_hash(direct)==before,"direct no-ready place changed the world before its rejection");
    }
}

// Clicks at the map edge: the grid is never legal and the product stays.
void check_edge_clicks(const w::Snapshot& world,const p::FactorySet& factories,const p::Catalog& catalog,
                       std::uint32_t house,const std::string& type,std::uint32_t product,std::uint64_t frame) {
    require(world.geometry.has_value(),"map geometry is missing");
    int edges=0;
    for (int y=0;y<512 && edges<4;y+=37) for (int x=0;x<512;++x) {
        const auto found=world.cells.find(std::uint32_t(y)*512+x);
        if (found==world.cells.end() || !found->second.allocated || !*found->second.allocated) continue;
        // First allocated Cell of the row: the map edge.
        const p::CellXY mouse{std::int16_t(x),std::int16_t(y)};
        const auto cursor=proto::placement_cursor(world,factories,catalog,house,type,mouse);
        require(cursor && cursor->legal==false,type+" edge grid is not illegal at "+std::to_string(x)+","+std::to_string(y));
        auto edge=world; auto edge_factories=factories;
        const auto outcome=proto::place(edge,edge_factories,catalog,house,type,cursor->origin,frame);
        require(outcome.status==p::Status::Rejected,type+" edge click was not rejected: "+outcome.detail);
        require(factory(edge_factories,product) && factory(edge_factories,product)->progress==54,
                type+" edge click lost its product");
        ++edges; break;
    }
    require(edges>0,"no map edge was checked");
    // Off the map: no Cell exists, so the grid cannot be legal or placed.
    const auto off=proto::placement_cursor(world,factories,catalog,house,type,{-3,-3});
    require(off && off->legal!=true,"off-map grid is legal");
    auto edge=world; auto edge_factories=factories;
    require(proto::place(edge,edge_factories,catalog,house,type,off->origin,frame).status!=p::Status::Done &&
            factory(edge_factories,product),"off-map click placed a building");
}
}

int main(int argc,char** argv) try {
    require(argc==2,"usage: test_prototype_production GAME_DIRECTORY");
    auto assets=w::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    w::PrototypeBattleSetup setup;
    setup.players={{"Player","Americans",20000,0,0,2,true},{"Enemy","Russians",20000,1,1,2,false}};
    auto loaded=w::prototype_battle(assets,setup,sim::Random(42).state());
    require(loaded.has_value(),"cannot load MP02T2 prototype");
    auto& world=loaded->snapshot;
    const auto player=world.houses[0].id, enemy=world.houses[1].id;
    const auto find_unit=[&](std::uint32_t house,const std::string& type) {
        for (const auto& item:world.entities) if (item.house_id==house && item.type==type) return item.id;
        throw std::runtime_error("missing starting "+type);
    };
    const auto amcv=find_unit(player,"AMCV"),smcv=find_unit(enemy,"SMCV");
    const auto catalog=p::Catalog::from(assets.rules(),assets.art_ini(),assets.map_ini());
    require(catalog.find("GACNST") && catalog.find("NACNST") &&
            catalog.find("GACNST")->placement.foundation==18 &&
            catalog.find("NACNST")->placement.foundation==18,"installed yard foundations differ");
    // 00498F10 centres the grid: origin = mouse - ((w-1)/2,(h-1)/2).
    for (const auto& [type,offset]:std::vector<std::pair<std::string,std::pair<int,int>>>{
             {"GAPOWR",{0,0}},{"GAREFN",{-1,-1}},{"GAWEAP",{-2,-1}},{"GACNST",{-1,-1}},{"GAPILE",{-1,0}}}) {
        const auto origin=proto::placement_origin(catalog,type,{100,100});
        require(origin && origin->x==100+offset.first && origin->y==100+offset.second,
                type+" cursor offset differs");
    }
    require(!proto::placement_origin(catalog,"MTNK",{100,100}),"a Unit has a building cursor");
    p::FactorySet factories;
    {
        auto wrong=world; wrong.setup_progress=w::SetupProgress::Complete;
        require(proto::initialize(wrong,factories,catalog,player).status==p::Status::NeedsInput,
                "prototype adapter accepted an exact world");
    }
    done(proto::initialize(world,factories,catalog,player),"initialize");
    require_prototype(world);
    const auto old_random=world.random;
    const auto* first_cell=&world.cells.begin()->second;
    done(proto::tick(world,factories,catalog,0),"idle tick");
    require(first_cell==&world.cells.begin()->second && world.random.table==old_random.table &&
            world.random.first==old_random.first && world.random.second==old_random.second,
            "idle tick replaced the world or changed the random state");
    require(proto::deploy(world,factories,catalog,amcv,false).status==p::Status::Rejected &&
            actor(world,amcv).lifetime==w::Lifetime::Placed && world.random.table==old_random.table,
            "rejected deployment changed the world");
    // A prototype turn completes immediately, so a moved MCV can still deploy.
    actor(world,amcv).hull_facing=0; actor(world,amcv).hull_facing_raw=0;
    const auto allied=proto::deploy(world,factories,catalog,amcv,true);
    done(allied,"deploy AMCV");
    const auto soviet=proto::deploy(world,factories,catalog,smcv,true);
    done(soviet,"deploy AI SMCV");
    require(allied.entity && soviet.entity && actor(world,*allied.entity).type=="GACNST" &&
            actor(world,*soviet.entity).type=="NACNST","deployed yard types differ");
    require(world.houses[0].human_controlled==true && world.houses[1].human_controlled==false,
            "AI deployment changed House control");
    require(actor(world,amcv).lifetime==w::Lifetime::Destroyed && actor(world,amcv).active==false &&
            actor(world,*allied.entity).active==true && actor(world,*soviet.entity).active==true,
            "deploy did not update live actors");
    require(world.random.first!=old_random.first || world.random.second!=old_random.second,
            "deploy did not preserve construction draws");
    sim::Random expected(old_random);
    expected.next(); expected.next();
    require(world.random.table==expected.state().table && world.random.first==expected.first() &&
            world.random.second==expected.second(),"deployed Houses did not share the same random stream");
    require_prototype(world);
    const auto base=*actor(world,*allied.entity).cell;
    const auto enemy_base=*actor(world,*soviet.entity).cell;
    std::uint64_t frame=0;
    done(proto::tick(world,factories,catalog,frame),"initial power tick");

    // A computer house starts a building through 0044B480, which has no
    // Can_Build test: TechLevel -1 stops the sidebar, not the computer.
    {
        auto barred=catalog;
        for (auto& t:barred.buildings) if (t.id=="GAPOWR" || t.id=="NAPOWR") t.can.tech_level=-1;
        auto copy=world; auto copy_factories=factories;
        require(proto::begin_production(copy,copy_factories,barred,player,"GAPOWR",frame).status==p::Status::Rejected,
                "sidebar started a TechLevel -1 building");
        done(proto::begin_production(copy,copy_factories,barred,enemy,"NAPOWR",frame),"computer TechLevel -1 building");
        require_prototype(copy);
    }
    // WaterBound shipyards: 004718B0 reads the Float land speed, so open
    // water is legal and land is not, for the player's cursor and Unlimbo.
    // The computer finds water near its base (004F03C0 -> 005502E0).
    {
        std::optional<p::CellXY> water;
        for (const auto& [key,cell]:world.cells) {
            const p::CellXY at{std::int16_t(key%512),std::int16_t(key/512)};
            bool open=true;
            for (const auto o:p::original_foundation_cells(catalog.find("GAYARD")->placement.foundation)) {
                const auto it=world.cells.find(std::uint32_t((at.y+o.y)*512+at.x+o.x));
                open=open && it!=world.cells.end() && it->second.terrain && it->second.terrain->land_type==2 &&
                    it->second.heads && it->second.heads->ground_head==0;
            }
            if (open && p::legal_placement(world,catalog,"GAYARD",at)) { water=at; break; }
            (void)cell;
        }
        require(water.has_value(),"MP02T2 has no open water for a shipyard");
        require(p::legal_placement(world,catalog,"GAYARD",{std::int16_t(base.x),std::int16_t(base.y)})==false,"shipyard accepted on the yard");
        // The frame picks one ring candidate; a far one is rejected
        // (AINavalYardAdjacency), so try a few frames.
        std::optional<p::NavalSite> site;
        for (std::uint32_t at=0;at<64 && !site;++at) {
            const auto found=p::naval_yard_site(world,factories,catalog,enemy,
                {std::int16_t(enemy_base.x),std::int16_t(enemy_base.y)},at);
            require(found.status!=p::SiteStatus::NeedsInput,"water search inputs unresolved: "+found.detail);
            if (found.status==p::SiteStatus::Found) site=found;
        }
        require(site && p::legal_placement(world,catalog,"NAYARD",site->cell)==true,
                "computer found no legal water Cell near its MP02T2 base");
    }

    // A queued building must not keep the House alive after its last yard dies.
    {
        auto lost=world;
        auto lost_factories=factories;
        const auto before_cash=*lost.houses[0].cash;
        const auto started=proto::begin_production(lost,lost_factories,catalog,player,"GAPOWR",frame);
        done(started,"queue plant before yard loss");
        require(started.entity.has_value(),"queued plant is missing");
        auto* pending=factory(lost_factories,*started.entity);
        require(pending,"queued plant factory is missing");
        const auto paid_frame=frame+pending->timer_duration;
        done(proto::tick(lost,lost_factories,catalog,paid_frame),"pay one plant stage");
        require(*lost.houses[0].cash<before_cash,"plant stage did not debit cash");
        std::vector<std::uint32_t> victims;
        for (const auto& item:lost.entities)
            if (item.house_id==player && item.lifetime==w::Lifetime::Placed) victims.push_back(item.id);
        for (const auto id:victims) destroy_actor(lost,id);
        const auto random_before_abandon=lost.random;
        done(proto::tick(lost,lost_factories,catalog,paid_frame+1),"abandon after yard loss");
        require(actor(lost,*started.entity).lifetime==w::Lifetime::Destroyed &&
                !factory(lost_factories,*started.entity) && !lost_factories.buildings.count(*allied.entity) &&
                !lost_factories.buildings.count(*started.entity),"yard loss kept its queued building");
        require(*lost.houses[0].cash==before_cash && lost.houses[0].unit_count==0 &&
                count_at(lost.houses[0].buildings_owned_5420,catalog.find("GAPOWR")->heap_index)==0 &&
                count_at(lost.houses[0].buildings_active_5460,catalog.find("GACNST")->heap_index)==0 &&
                lost_factories.house_factory_counts.at(player)[3]==0,
                "yard loss kept stale money or counters");
        require(lost.random.table==random_before_abandon.table && lost.random.first==random_before_abandon.first &&
                lost.random.second==random_before_abandon.second,"abandon changed the shared random state");
        require(std::none_of(lost.entities.begin(),lost.entities.end(),[&](const auto& item){
            return item.house_id==player && item.lifetime!=w::Lifetime::Destroyed &&
                (item.kind==w::Kind::Unit || item.kind==w::Kind::Infantry || item.kind==w::Kind::Structure);
        }),"queued building still prevents defeat");
        require_prototype(lost);
    }

    const auto cash=*world.houses[0].cash;
    build(world,factories,catalog,player,"GAPOWR",frame,true);
    require(*world.houses[0].cash==cash-catalog.find("GAPOWR")->cost,"plant cost differs");
    // Every live starting unit blocks the footprint, even without proximity.
    for(const auto& unit:world.entities) if(unit.lifetime==w::Lifetime::Placed &&
        unit.kind==w::Kind::Unit && unit.cell) {
        const p::CellXY occupied{std::int16_t(unit.cell->x),std::int16_t(unit.cell->y)};
        require(p::legal_placement(world,catalog,"GAPOWR",occupied)==false,
                "preview accepted a unit under the power footprint");
        auto blocked=world;auto blocked_factories=factories;
        require(proto::place(blocked,blocked_factories,catalog,player,"GAPOWR",occupied,frame).status!=p::Status::Done,
                "placement accepted a unit under the power footprint");
        require(blocked.entities.size()==world.entities.size() &&
                blocked_factories.factories.size()==factories.factories.size(),
                "rejected placement consumed the completed product");
    }
    place_building(world,factories,catalog,player,"GAPOWR",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"power update");
    require(*world.houses[0].power_output>0,"plant produced no power");
    build(world,factories,catalog,player,"GAPILE",frame,false);
    place_building(world,factories,catalog,player,"GAPILE",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"barracks power update");
    // 00449290 -> 004498FD: construction finishes in Guard, enabling acquisition.
    {
        auto defense_world=world;
        auto defense_factories=factories;
        auto defense_frame=frame;
        for (const auto* type:{"NAPOWR","NAHAND"}) {
            build(defense_world,defense_factories,catalog,enemy,type,defense_frame,false);
            place_building(defense_world,defense_factories,catalog,enemy,type,defense_frame,enemy_base);
            done(proto::tick(defense_world,defense_factories,catalog,++defense_frame),"defense prerequisite power");
        }
        const auto gun=build(defense_world,defense_factories,catalog,enemy,"NALASR",defense_frame,false);
        place_building(defense_world,defense_factories,catalog,enemy,"NALASR",defense_frame,enemy_base);
        done(proto::tick(defense_world,defense_factories,catalog,++defense_frame),"defense power update");
        require(actor(defense_world,gun).mission==5,"constructed defense stayed in Construction");
        auto target=std::find_if(defense_world.entities.begin(),defense_world.entities.end(),[&](const auto& item) {
            return item.house_id==player && item.kind==w::Kind::Unit && item.lifetime==w::Lifetime::Placed;
        });
        require(target!=defense_world.entities.end(),"defense target missing");
        // Keep the target's live identity; move its aim point into weapon range.
        target->position=actor(defense_world,gun).position;
        target->position->x+=256;
        target->collision_virtual_position=target->position;
        target->locomotor_moving=false;
        sim::PrototypeCombatRuntime combat(assets.rules(),assets.map_ini(),assets.art_ini(),std::string(argv[1])+"/game.exe");
        bool fired=false;
        for (int tick=0;tick<64 && !fired;++tick) {
            const auto report=combat.tick(defense_world,++defense_frame);
            for (const auto& event:report.events)
                if (event.kind==sim::PrototypeCombatEventKind::Shot && event.source_id==gun) fired=true;
        }
        require(fired,"constructed defense did not acquire and fire");
    }
    // A completed soldier must be abandoned when the last barracks is lost.
    {
        auto lost=world;
        auto lost_factories=factories;
        const auto initial_infantry=count_at(lost.houses[0].infantry_owned_5440,catalog.find("E1")->heap_index);
        const auto started=proto::begin_production(lost,lost_factories,catalog,player,"E1",frame);
        done(started,"queue infantry before barracks loss");
        require(started.entity.has_value(),"queued infantry is missing");
        auto* pending=factory(lost_factories,*started.entity);
        require(pending,"queued infantry factory is missing");
        const auto cash=*lost.houses[0].cash;
        *lost.houses[0].cash-=catalog.find("E1")->cost;
        pending->progress=54; pending->balance=0; pending->suspended=true;
        const auto barracks=std::find_if(lost.entities.begin(),lost.entities.end(),[&](const auto& item){
            return item.house_id==player && item.type=="GAPILE" && item.lifetime==w::Lifetime::Placed;
        });
        require(barracks!=lost.entities.end(),"barracks is missing");
        const auto barracks_id=barracks->id;
        destroy_actor(lost,barracks_id);
        done(proto::tick(lost,lost_factories,catalog,frame+1),"abandon after barracks loss");
        require(actor(lost,*started.entity).lifetime==w::Lifetime::Destroyed &&
                !factory(lost_factories,*started.entity) && !lost_factories.buildings.count(barracks_id) &&
                *lost.houses[0].cash==cash &&
                count_at(lost.houses[0].infantry_owned_5440,catalog.find("E1")->heap_index)==initial_infantry &&
                count_at(lost.houses[0].buildings_active_5460,catalog.find("GAPILE")->heap_index)==0 &&
                lost_factories.house_factory_counts.at(player)[1]==0,
                "barracks loss kept a product, money, or stale counters");
        require_prototype(lost);
    }
    // A remaining barracks keeps the same queued product after one is lost.
    {
        auto surviving=world;
        auto surviving_factories=factories;
        auto local_frame=frame;
        const auto extra=build(surviving,surviving_factories,catalog,player,"GAPILE",local_frame,false);
        place_building(surviving,surviving_factories,catalog,player,"GAPILE",local_frame,base);
        done(proto::tick(surviving,surviving_factories,catalog,++local_frame),"second barracks power update");
        const auto started=proto::begin_production(surviving,surviving_factories,catalog,player,"E1",local_frame);
        done(started,"queue infantry with two barracks");
        require(started.entity.has_value(),"second barracks product is missing");
        const auto cash=*surviving.houses[0].cash;
        destroy_actor(surviving,extra);
        done(proto::tick(surviving,surviving_factories,catalog,local_frame),"continue after one barracks loss");
        auto* pending=factory(surviving_factories,*started.entity);
        require(pending && actor(surviving,*started.entity).lifetime==w::Lifetime::Created &&
                *surviving.houses[0].cash==cash && surviving_factories.house_factory_counts.at(player)[1]==1,
                "surviving barracks did not retain the queued infantry");
        pending->progress=53;
        local_frame=std::max(local_frame+1,pending->timer_start+std::uint64_t(pending->timer_duration));
        done(proto::tick(surviving,surviving_factories,catalog,local_frame),"finish at surviving barracks");
        require(!factory(surviving_factories,*started.entity) &&
                actor(surviving,*started.entity).lifetime==w::Lifetime::Placed,
                "queued infantry did not leave the surviving barracks");
        require_prototype(surviving);
    }
    const auto infantry=build(world,factories,catalog,player,"E1",frame,false,true);
    require(!factory(factories,infantry),"completed infantry stayed in its factory");
    require(actor(world,infantry).kind==w::Kind::Infantry && actor(world,infantry).active==true &&
            actor(world,infantry).in_limbo==false && actor(world,infantry).cell,
            "infantry did not leave the barracks");
    require(actor(world,infantry).mission==0,"factory infantry left without an idle mission");
    require_in_front(world,catalog,player,"GAPILE",actor(world,infantry));
    const auto foot=*actor(world,infantry).cell;
    require((*world.cells.at(std::uint32_t(foot.y)*512+foot.x).ground_occupancy_124&0x20)!=0,
            "infantry exit did not reserve its Cell");

    const auto refinery_start=frame;
    const auto pending_refinery=build(world,factories,catalog,player,"GAREFN",frame,true);
    const auto first_refinery_frames=frame-refinery_start;
    {
        auto probe=world;
        auto probe_factories=factories;
        const auto origin=place_building(probe,probe_factories,catalog,player,"GAREFN",frame,base);
        auto sealed=world;
        auto sealed_factories=factories;
        for (auto& item:sealed.cells) if (item.second.ground_occupancy_124)
            *item.second.ground_occupancy_124|=0x20;
        for (const auto& offset:p::original_foundation_cells(catalog.find("GAREFN")->placement.foundation)) {
            const auto key=std::uint32_t(origin.y+offset.y)*512+origin.x+offset.x;
            sealed.cells.at(key).ground_occupancy_124=0;
        }
        const auto before=sealed.random;
        const auto blocked=proto::place(sealed,sealed_factories,catalog,player,"GAREFN",origin,frame);
        require(blocked.status==p::Status::Rejected && actor(sealed,pending_refinery).lifetime==w::Lifetime::Created &&
                factory(sealed_factories,pending_refinery) && sealed.random.table==before.table &&
                sealed.random.first==before.first && sealed.random.second==before.second,
                "blocked refinery exit changed the world or random state");
    }
    int infantry_blocks=0,vehicle_blocks=0;
    {
        bool other_cell_legal=false;
        int inf=0,veh=0;
        require(check_units_block(world,factories,catalog,player,"GAREFN",pending_refinery,frame,other_cell_legal,inf,veh)>0,
                "no unit stood in a refinery grid near the base");
        infantry_blocks+=inf; vehicle_blocks+=veh;
        require(other_cell_legal,"a unit turned the whole refinery grid red");
        check_edge_clicks(world,factories,catalog,player,"GAREFN",pending_refinery,frame);
        auto near=world; auto near_factories=factories;
        place_at_mouse(near,near_factories,catalog,player,"GAREFN",frame,base,{-1,-1});
    }
    const auto before_free_unit=world.random;
    const auto cash_before_free_unit=*world.houses[0].cash;
    place_building(world,factories,catalog,player,"GAREFN",frame,base);
    const auto* allied_refinery=catalog.find("GAREFN");
    require(allied_refinery && !allied_refinery->free_unit.empty(),"refinery has no installed free Unit");
    const auto allied_miner=find_unit(player,allied_refinery->free_unit);
    require(actor(world,allied_miner).lifetime==w::Lifetime::Placed && actor(world,allied_miner).active==true &&
            actor(world,allied_miner).maximum_hp>0 && actor(world,allied_miner).next,
            "Allied refinery did not place its free miner");
    require_in_front(world,catalog,player,"GAREFN",actor(world,allied_miner));
    sim::Random free_unit_random(before_free_unit);
    free_unit_random.next();
    require(world.random.table==free_unit_random.state().table && world.random.first==free_unit_random.first() &&
            world.random.second==free_unit_random.second() && *world.houses[0].cash==cash_before_free_unit,
            "free miner changed cash or used a different random stream");
    done(proto::tick(world,factories,catalog,++frame),"refinery power update");
    // QA3: construct and place each country variant through the real adapter.
    for (const auto& variant:std::vector<std::pair<std::string,std::string>>{
            {"Americans","AMRADR"},{"British","GAAIRC"}}) {
        auto radar_world=world; auto radar_factories=factories; auto local=frame;
        auto& owner=radar_world.houses[0];
        owner.country=variant.first;
        const auto country=std::find(catalog.countries.begin(),catalog.countries.end(),variant.first);
        require(country!=catalog.countries.end(),"missing radar country");
        owner.country_index=std::int32_t(country-catalog.countries.begin());
        require(!sim::tech::radar_online(assets.rules(),radar_world,player),"radar online before construction");
        const auto radar=build(radar_world,radar_factories,catalog,player,variant.second,local,true);
        require(actor(radar_world,radar).lifetime!=w::Lifetime::Placed,"ready radar already placed");
        const auto origin=place_building(radar_world,radar_factories,catalog,player,variant.second,local,base);
        done(proto::tick(radar_world,radar_factories,catalog,++local),"radar power update");
        const auto& building=actor(radar_world,radar);
        require(building.lifetime==w::Lifetime::Placed && building.active==true && building.in_limbo==false,
                "radar placement did not activate building");
        require(!factory(radar_factories,radar),"radar placement kept completed product");
        for (const auto& offset:p::original_foundation_cells(catalog.find(variant.second)->placement.foundation))
            require(radar_world.cells.at(std::uint32_t(origin.y+offset.y)*512+origin.x+offset.x).heads->ground_head==radar,
                    "radar foundation not linked");
        require(*radar_world.houses[0].power_output>=*radar_world.houses[0].power_drain,"radar lacks power");
        require(sim::tech::radar_online(assets.rules(),radar_world,player),variant.second+" powered radar offline");
        require(radar_factories.buildings.at(radar).primary,"radar is not the primary aircraft factory");
        require(count_at(radar_world.houses[0].buildings_active_5460,catalog.find(variant.second)->heap_index)==1,
                "placed radar has no active building count");
        require(*radar_world.houses[0].power_drain==*world.houses[0].power_drain+catalog.find(variant.second)->power_drain,
                "radar power drain differs");
        // Part 5c: produced planes dock on the airfield pad and the dock
        // limit holds. An Alliance house builds its Black Eagle first.
        {
            auto allied=radar_world; auto allied_factories=radar_factories; auto allied_frame=local;
            allied.houses[0].country="Alliance";
            const auto alliance=std::find(catalog.countries.begin(),catalog.countries.end(),"Alliance");
            require(alliance!=catalog.countries.end(),"Alliance country is missing");
            allied.houses[0].country_index=std::int32_t(alliance-catalog.countries.begin());
            *allied.houses[0].cash=100000;
            const auto eagle=build(allied,allied_factories,catalog,player,"BEAG",allied_frame,false);
            const auto& plane=actor(allied,eagle);
            require(plane.kind==w::Kind::Aircraft && plane.lifetime==w::Lifetime::Placed &&
                    plane.active==true && plane.in_limbo==false && plane.next==0 &&
                    plane.occupancy_tracking==false,"BEAG did not activate on its pad");
            require(!factory(allied_factories,eagle),"BEAG left a factory object");
            require(plane.cell && plane.cell->x==origin.x && plane.cell->y==origin.y,
                    "BEAG did not dock on the airfield pad Cell");
            require(allied.cells.at(std::uint32_t(origin.y)*512+origin.x).heads->ground_head==radar,
                    "BEAG took the airfield ground head");
            const auto* eagle_facts=catalog.find("BEAG");
            require(eagle_facts,"BEAG rules are missing");
            require(count_at(allied.houses[0].aircraft_owned_5450,eagle_facts->heap_index)==1,
                    "BEAG owned counter is wrong");
            require(*allied.houses[0].cash<100000,"BEAG production did not spend cash");
            require_prototype(allied);
        }
        *radar_world.houses[0].cash=100000;
        const std::int32_t cash_before_planes=*radar_world.houses[0].cash;
        const auto* orca_facts=catalog.find("ORCA");
        require(orca_facts,"ORCA rules are missing");
        std::vector<std::uint32_t> planes;
        for (int i=0;i<4;++i) {
            const auto before=*radar_world.houses[0].cash;
            const auto plane_id=build(radar_world,radar_factories,catalog,player,"ORCA",local,false);
            const auto& plane=actor(radar_world,plane_id);
            require(plane.kind==w::Kind::Aircraft && plane.lifetime==w::Lifetime::Placed &&
                    plane.active==true && plane.in_limbo==false && plane.next==0 &&
                    plane.occupancy_tracking==false,
                    "ORCA "+std::to_string(i)+" did not activate on its pad");
            require(!factory(radar_factories,plane_id),"ORCA left a factory object");
            require(plane.cell && plane.cell->x==origin.x && plane.cell->y==origin.y,
                    "ORCA did not dock on the airfield pad Cell");
            require(radar_world.cells.at(std::uint32_t(origin.y)*512+origin.x).heads->ground_head==radar,
                    "ORCA took the airfield ground head");
            require(*radar_world.houses[0].cash<before,"ORCA production did not spend cash");
            planes.push_back(plane_id);
        }
        require(count_at(radar_world.houses[0].aircraft_owned_5450,orca_facts->heap_index)==4,
                "ORCA owned counter is wrong");
        const auto fifth=proto::begin_production(radar_world,radar_factories,catalog,player,"ORCA",local);
        require(fifth.status==p::Status::Rejected,"fifth ORCA passed four dock slots");
        destroy_actor(radar_world,planes.front());
        const auto replacement=build(radar_world,radar_factories,catalog,player,"ORCA",local,false);
        require(!factory(radar_factories,replacement) && actor(radar_world,replacement).kind==w::Kind::Aircraft &&
                actor(radar_world,replacement).lifetime==w::Lifetime::Placed &&
                actor(radar_world,replacement).active==true,
                "replacement ORCA did not complete after a dock cleared");
        int live_planes=0;
        for (const auto& item:radar_world.entities)
            if (item.house_id==player && item.kind==w::Kind::Aircraft &&
                item.lifetime==w::Lifetime::Placed && item.active==true && item.in_limbo==false &&
                item.resolved_hp.value_or(0)>0) ++live_planes;
        require(live_planes==4,"live plane count differs after the replacement");
        require(*radar_world.houses[0].cash<cash_before_planes,"aircraft production did not spend cash");
        require_prototype(radar_world);
        radar_world.houses[0].power_output=0;
        require(!sim::tech::radar_online(assets.rules(),radar_world,player),"radar stayed online without power");
        require_prototype(radar_world);
    }
    // A second refinery takes the same frames as the first when power is
    // enough: build time depends on cost, power, and factory count only.
    {
        auto second=world; auto second_factories=factories; auto local=frame;
        build(second,second_factories,catalog,player,"GAREFN",local,true);
        const auto second_refinery_frames=local-frame;
        // 006C22A0: 2000 * BuildSpeed 0.7 * 0.9 = 1260 frames; 1260/54 = 23 frames a stage.
        require(first_refinery_frames==54*23 && second_refinery_frames==first_refinery_frames,
                "refinery build frames differ: first "+std::to_string(first_refinery_frames)+
                ", second "+std::to_string(second_refinery_frames));
    }
    const auto pending_factory=build(world,factories,catalog,player,"GAWEAP",frame,false);
    {
        bool other_cell_legal=false;
        int inf=0,veh=0;
        require(check_units_block(world,factories,catalog,player,"GAWEAP",pending_factory,frame,other_cell_legal,inf,veh)>0,
                "no unit stood in a factory grid near the base");
        infantry_blocks+=inf; vehicle_blocks+=veh;
        require(other_cell_legal,"a unit turned the whole factory grid red");
        require(infantry_blocks>0 && vehicle_blocks>0,
                "own infantry and own vehicles did not both block a placement");
        check_rejection_cases(world,factories,catalog,player,base,"GAWEAP",frame);
        check_direct_place(world,factories,catalog,player,"GAWEAP",frame);
        check_edge_clicks(world,factories,catalog,player,"GAWEAP",pending_factory,frame);
        auto near=world; auto near_factories=factories;
        place_at_mouse(near,near_factories,catalog,player,"GAWEAP",frame,base,{-2,-1});
    }
    place_building(world,factories,catalog,player,"GAWEAP",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"factory power update");
    const auto tank=build(world,factories,catalog,player,"MTNK",frame,false);
    require(!factory(factories,tank) && actor(world,tank).kind==w::Kind::Unit &&
            actor(world,tank).lifetime==w::Lifetime::Placed && actor(world,tank).resolved_hp>0 &&
            actor(world,tank).active==true && actor(world,tank).next,
            "tank did not leave the war factory");
    require_in_front(world,catalog,player,"GAWEAP",actor(world,tank));
    {
        // A second tank takes the next clear Cell; it never stacks on the first.
        auto busy=world; auto busy_factories=factories; auto local=frame;
        const auto second=build(busy,busy_factories,catalog,player,"MTNK",local,false);
        require_in_front(busy,catalog,player,"GAWEAP",actor(busy,second));
        require(actor(busy,second).cell->x!=actor(busy,tank).cell->x || actor(busy,second).cell->y!=actor(busy,tank).cell->y,"second tank stacked on the first");
    }
    {
        // With every front Cell blocked, the tank waits in the factory. It
        // never falls back to a Cell behind the building art.
        auto blocked=world; auto blocked_factories=factories; auto local=frame;
        const auto* weap=catalog.find("GAWEAP");
        int front=-1;
        for (const auto& item:blocked.entities)
            if (item.house_id==player && item.type=="GAWEAP" && item.lifetime==w::Lifetime::Placed && item.cell)
                for (const auto& offset:p::original_foundation_cells(weap->placement.foundation))
                    front=std::max(front,int(item.cell->x)+offset.x+int(item.cell->y)+offset.y);
        require(front>=0,"war factory is missing");
        std::vector<std::pair<std::uint32_t,std::uint32_t>> saved;
        for (auto& item:blocked.cells) {
            const int x=int(item.first%512),y=int(item.first/512);
            if (x+y>front && item.second.ground_occupancy_124) {
                saved.push_back({item.first,*item.second.ground_occupancy_124});
                *item.second.ground_occupancy_124|=0x20;
            }
        }
        const auto started=proto::begin_production(blocked,blocked_factories,catalog,player,"MTNK",local);
        done(started,"begin tank with a blocked front");
        auto* pending=factory(blocked_factories,*started.entity);
        require(pending,"blocked tank factory is missing");
        pending->progress=53;
        local=std::max(local+1,pending->timer_start+std::uint64_t(pending->timer_duration));
        const auto waiting=proto::tick(blocked,blocked_factories,catalog,local);
        done(waiting,"tick with a blocked front");
        require(factory(blocked_factories,*started.entity) && actor(blocked,*started.entity).lifetime==w::Lifetime::Created &&
                waiting.detail=="waiting for a clear factory exit",
                "tank left the factory with every front Cell blocked");
        for (const auto& item:saved) blocked.cells.at(item.first).ground_occupancy_124=item.second;
        done(proto::tick(blocked,blocked_factories,catalog,++local),"tick with a clear front");
        require(!factory(blocked_factories,*started.entity),"tank did not leave after the front cleared");
        require_in_front(blocked,catalog,player,"GAWEAP",actor(blocked,*started.entity));
        require_prototype(blocked);
    }
    // Low power slows production that is already running. Recalc_Power
    // 004F2CC0 calls 004B9EB0, which gives each factory a new stage rate.
    // The running stage keeps its timer; the next stage uses the new rate.
    {
        auto dark=world; auto dark_factories=factories; auto local=frame;
        const auto started=proto::begin_production(dark,dark_factories,catalog,player,"MTNK",local);
        done(started,"begin tank before power loss");
        require(started.entity && started.value,"tank production result is missing");
        auto* pending=factory(dark_factories,*started.entity);
        require(pending,"tank factory is missing");
        const auto full_rate=pending->interval;
        require(full_rate==sim::factory_stage_frames(*started.value) && pending->timer_duration==full_rate,
                "tank stage rate differs from its build time");
        std::vector<std::uint32_t> plants;
        for (const auto& item:dark.entities)
            if (item.house_id==player && item.type=="GAPOWR" && item.lifetime==w::Lifetime::Placed) plants.push_back(item.id);
        require(!plants.empty(),"power plant is missing");
        for (const auto id:plants) destroy_actor(dark,id);
        done(proto::tick(dark,dark_factories,catalog,++local),"power loss update");
        const auto& house=dark.houses[0];
        require(house.power_output==0 && house.power_drain && *house.power_drain>0,"power loss did not reach the House");
        sim::BuildTimeInputs low;
        low.cost=catalog.find("MTNK")->cost; low.build_speed=catalog.build_speed;
        low.power_output=0; low.power_drain=*house.power_drain;
        low.minimum_low_power=catalog.minimum_low_power; low.maximum_low_power=catalog.maximum_low_power;
        low.low_power_penalty=catalog.low_power_penalty; low.multiple_factory=catalog.multiple_factory;
        low.factories=dark_factories.house_factory_counts.at(player)[2];
        const auto low_rate=sim::factory_stage_frames(sim::original_build_time(low));
        pending=factory(dark_factories,*started.entity);
        require(pending && pending->interval==low_rate && low_rate>full_rate && pending->timer_duration==full_rate &&
                pending->progress==0,"power loss did not slow the next tank stage");
        local=pending->timer_start+std::uint64_t(pending->timer_duration);
        done(proto::tick(dark,dark_factories,catalog,local),"first slow tank stage");
        pending=factory(dark_factories,*started.entity);
        require(pending && pending->progress==1 && pending->timer_duration==low_rate,
                "next tank stage did not use the low-power rate");
        require_prototype(dark);
    }
    build(world,factories,catalog,enemy,"NAPOWR",frame,false);
    place_building(world,factories,catalog,enemy,"NAPOWR",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI power update");
    build(world,factories,catalog,enemy,"NAHAND",frame,false);
    place_building(world,factories,catalog,enemy,"NAHAND",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI barracks power update");
    const auto enemy_infantry=build(world,factories,catalog,enemy,"E2",frame,false);
    require(!factory(factories,enemy_infantry) && actor(world,enemy_infantry).house_id==enemy &&
            actor(world,enemy_infantry).lifetime==w::Lifetime::Placed && actor(world,enemy_infantry).active==true,
            "AI infantry did not leave the barracks");
    build(world,factories,catalog,enemy,"NAREFN",frame,false);
    place_building(world,factories,catalog,enemy,"NAREFN",frame,enemy_base);
    const auto* soviet_refinery=catalog.find("NAREFN");
    require(soviet_refinery && !soviet_refinery->free_unit.empty(),"AI refinery has no installed free Unit");
    const auto soviet_miner=find_unit(enemy,soviet_refinery->free_unit);
    require(actor(world,soviet_miner).lifetime==w::Lifetime::Placed && actor(world,soviet_miner).active==true &&
            actor(world,soviet_miner).maximum_hp>0 && actor(world,soviet_miner).next,
            "AI refinery did not place its free miner");
    done(proto::tick(world,factories,catalog,++frame),"AI refinery power update");
    build(world,factories,catalog,enemy,"NAWEAP",frame,false);
    place_building(world,factories,catalog,enemy,"NAWEAP",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI factory power update");
    const auto enemy_tank=build(world,factories,catalog,enemy,"HTNK",frame,false);
    require(!factory(factories,enemy_tank) && actor(world,enemy_tank).house_id==enemy &&
            actor(world,enemy_tank).lifetime==w::Lifetime::Placed && actor(world,enemy_tank).active==true,
            "AI tank did not leave the war factory");
    require_in_front(world,catalog,enemy,"NAWEAP",actor(world,enemy_tank));
    require_in_front(world,catalog,enemy,"NAHAND",actor(world,enemy_infantry));
    require_prototype(world);
    // A wall: the finished product places through prototype::place, writes
    // its wall overlay on the Cell and retires the temporary building; the
    // factory product clears. No live Structure stays on the map.
    {
        const auto* facts=catalog.find("GAWALL");
        require(facts && facts->wall,"GAWALL is a wall type");
        int overlay=-1;
        for (std::size_t i=0;i<catalog.overlay_names.size();++i) if (catalog.overlay_names[i]=="GAWALL") overlay=int(i);
        require(overlay>=0,"GAWALL has no overlay entry");
        build(world,factories,catalog,player,"GAWALL",frame,false);
        const auto origin=place_building(world,factories,catalog,player,"GAWALL",frame,base);
        const auto& cell=world.cells.at(std::uint32_t(origin.y)*512+std::uint32_t(origin.x));
        require(cell.terrain && cell.terrain->overlay_type_index && *cell.terrain->overlay_type_index==overlay,
            "wall overlay did not survive placement");
        require(cell.wall_owner && *cell.wall_owner==int(player),"wall owner missing after placement");
        for (const auto& e:world.entities)
            require(!(e.type=="GAWALL" && e.kind==w::Kind::Structure && e.lifetime==w::Lifetime::Placed),
                "a wall left a live Structure on the map");
        const auto* wall_factory=[&]{ for (const auto& x:factories.factories) if (x.house_id==player && x.slot==5) return &x; return (const p::FactoryState*)nullptr; }();
        require(wall_factory && !wall_factory->object && wall_factory->object_type.empty() &&
            wall_factory->progress==0 && wall_factory->suspended,"wall product did not clear from the factory");
        require_prototype(world);
        std::cout<<"GAWALL placed as an overlay at ("<<origin.x<<","<<origin.y<<") with the factory cleared\n";
    }
    std::cout<<"MP02T2 prototype deploys both Houses and produces infantry and a tank\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
