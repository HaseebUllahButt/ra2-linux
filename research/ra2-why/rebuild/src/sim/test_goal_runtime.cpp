#include "goal_runtime.h"
#include "world_init.h"
#include "live_cell_setup.h"
#include "unit_visibility.h"
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool b,const char* text) { if (!b) throw std::runtime_error(text); }
std::uint32_t key(ra2map::Cell c) { return std::uint32_t(c.y)*512+c.x; }
struct Fixture {
    sw::Snapshot world;
    GoalRuntimeSetup setup;
    GoalRuntimeActor actor;
    GoalRuntimeCallbacks cb;
    TheaterInputs theater;
    LiveCellSetupInputs cell_setup;
    UnitVisibilityInputs visibility;
    Fixture() {
        world.geometry=session::PlacementGeometry{32,50,{0,0,32,50}};
        world.tube_vector_count=0; world.random=Random(42).state();world.setup_progress=sw::SetupProgress::Complete;
        world.cell_order=std::vector<std::uint32_t>{};for (const auto c:original_map_cell_order(32,50)) world.cell_order->push_back(key(c));
        sw::House house; house.id=1; house.name="Player";house.country="America";house.constructor_complete=true;world.houses.push_back(house);
        sw::Entity unit; unit.id=2;unit.house_id=1;unit.type="HTNK";unit.lifetime=sw::Lifetime::Placed;
        unit.next=0;unit.constructor_complete=true;unit.original_type_index=0;unit.cell=ra2map::Cell{54,69};unit.position=ground_cell_center(54,69);unit.layer=sw::Layer::Ground;
        unit.on_map=true;unit.unit_field_7d=1;unit.foot_field_5a1=0;unit.object_field_74=1;unit.object_field_75=1;unit.object_field_76=0;unit.techno_field_370=0;unit.hull_facing=128;unit.hull_facing_raw=32768;unit.resolved_hp=400;unit.maximum_hp=400;unit.occupancy_tracking=true;
        sw::BaseUnitProfile profile;
        profile.restricted_land=-1;profile.movement_mode=-1;profile.placement_mode=2;profile.speed_type=1;
        profile.level_height=104;profile.leptons_per_sight_increase=104;
        profile.type_flag_aa0=false;profile.unit_flag_32c=false;profile.linked_team=false;profile.type_flag_205=true;
        profile.unit_flag_32a=false;profile.type_flag_bb0=false;profile.type_flag_bb1=false;unit.base_profile=profile;
        CoarseOccupantState category;category.runtime_type=1;unit.category=category;
        world.entities.push_back(unit);world.creation_order={{sw::CreatedKind::House,1},{sw::CreatedKind::Entity,2}};
        setup.allocated.resize(512*512);setup.frame=11;setup.components.transitions=std::vector<ComponentTransition>{};
        constexpr unsigned stride=83;
        setup.hierarchy_cells.resize(stride*stride);
        for (auto& c:setup.hierarchy_cells) c={{7,0},false};
        for (const auto point:original_map_cell_order(32,50)) {
            const int x=point.x,y=point.y;const bool inside=x>=50 && x<=58 && y>=65 && y<=73;
            const auto index=key(point);setup.allocated[index]=true;
            auto& c=world.cells[index];c.allocated=true;c.heads=weapons::CellContentHeads{};
            auto terrain=original_default_terrain_cell(point);terrain.height_level=0;terrain.ramp_index=0;terrain.floor_world_height=0;
            terrain.land_type=0;terrain.bridge=false;terrain.resolved_tile_index=0;terrain.overlay_type_index=-1;c.terrain=terrain;
            c.usable=inside;c.coarse_category=inside?0:7;c.coarse_cache_height=0;c.hierarchy_cache_height=0;c.neighbor_count=0;
            c.ground_occupancy_124=0;c.bridge_occupancy_128=0;c.unit_land_speed=1;
            c.cell_flag_20000=false;c.cell_field_11d=0;c.cell_field_12c=0;c.cell_flags_140=0;c.wall_owner=-1;c.cell_flag_100=false;c.cell_flag_200=false;c.cell_flag_40000=false;
            c.cell_field_54=-1;c.cell_field_58=-1;c.tube_field_116=-1;
            setup.hierarchy_cells[y*stride+x]={{static_cast<std::uint8_t>(inside?0:7),0},inside};
        }
        world.cells.at(key({54,69})).heads->ground_head=2;world.cells.at(key({54,69})).ground_occupancy_124=32;
        FootPathState foot;foot.words.fill(-1);foot.field_640=0;foot.field_644=0;foot.field_648=0;foot.cell_558=ra2map::Cell{54,69};
        setup.types.emplace(2,GoalRuntimeType{0,false,0.0,15});
        setup.foot.emplace(2,foot);setup.motion.emplace(2,GroundMotion{});setup.search.emplace(2,OriginalSearchMemory{});
        actor.movement_zone=0;actor.special_guard=false;actor.threat_weight=0;actor.speed=15;actor.destination_guards=GroundGuards{};
        actor.foot.append_offset=0;actor.foot.mark_mode=0;actor.foot.is_train=false;actor.foot.field_5d4_is_null=true;
        actor.foot.field_6c8_is_null=true;actor.foot.field_6d0=false;actor.foot.frame=11;
        cb.can_queue=[](sw::Snapshot&,std::uint32_t,Coord)->std::optional<bool>{return true;};
        cb.enter=[](const sw::Snapshot& s,std::uint32_t,ra2map::Cell c)->std::optional<int>{
            auto i=s.cells.find(key(c));if(i==s.cells.end() || !i->second.ground_occupancy_124) return {};
            return *i->second.ground_occupancy_124 || !i->second.usable || !*i->second.usable?7:0;
        };
        // Test-only resolved callbacks. These model one Unit on empty cells; they
        // are not evidence for complete original marking or movement callbacks.
        cb.finish=[](sw::Snapshot&,std::uint32_t)->std::optional<bool>{return true;};
        cb.mark.bridge_height=416;
        visibility.projection_divisor=104;visibility.global_mode=false;visibility.allocated=&setup.allocated;
        cb.mark.insertion_visibility=[this](sw::Snapshot& world,std::uint32_t id,std::uint32_t cell)->std::optional<bool>{
            const auto result=original_unit_insertion_visibility(world,id,cell,visibility);
            if (result.status!=UnitVisibilityStatus::Complete) return {};
            return true;
        };
        theater.theater_name="TEMPERATE";theater.settings.parse("[General]\n");theater.tile_sets={{0,1}};
        theater.resolved_metadata=[](std::uint32_t index,std::uint8_t subtile)->std::optional<TileMetadata>{
            if (index!=0) return {};
            return TileMetadata{0,0,subtile==0};
        };
        cell_setup.theater=&theater;cell_setup.cliff=0;cell_setup.allocated=&setup.allocated;
        cell_setup.fallback=original_default_terrain_cell({0,0});cell_setup.land_wheels[0]=1;
        cell_setup.guard=[](std::uint32_t,std::uint32_t tile)->std::optional<CellSetupStatic>{
            if (tile!=0) return {};
            CellSetupStatic g;g.receiver_tile_index=0;g.animation_index=-1;g.shadow_enabled=false;g.bounds_height=30;return g;
        };
        cb.mark.cell_setup=[this](sw::Snapshot& world,std::uint32_t key)->std::optional<bool>{
            const auto result=original_live_cell_setup(world,key,cell_setup);
            if (result.status!=CellSetupStatus::Complete) {
                std::cerr<<"Cell setup "<<key<<": "<<result.detail<<'\n';return {};
            }
            return true;
        };

    }
};
}
int main(int argc,char** argv) try {
    if (argc!=2 && !(argc==3 && std::string(argv[2])=="--vectors"))
        throw std::runtime_error("usage: test_goal_runtime game.exe [--vectors]");
    if (argc==3) {
        constexpr int dx[]={0,1,1,1,0,-1,-1,-1},dy[]={-1,-1,0,1,1,1,0,-1};
        struct Golden { unsigned direction;int speed;unsigned frames;std::uint32_t hash; };
        static constexpr Golden goldens[]={
#include "drive_occupancy_golden.inc"
        };
        std::uint32_t hash=2166136261u;unsigned checks=0;std::size_t case_index=0;
        for (unsigned direction=0;direction<8;++direction) for (int speed:{1,7,15,30,255}) {
            std::uint32_t case_hash=2166136261u;unsigned case_frames=0;
            Fixture fixture;auto world=fixture.world;fixture.actor.speed=speed;fixture.setup.types.at(2).speed=speed;
            world.entities[0].hull_facing=static_cast<int>(direction*32);
            world.entities[0].hull_facing_raw=static_cast<std::uint16_t>(direction*8192);
            const auto imported=GoalRuntime::import(world,fixture.setup,argv[1]);
            require(imported.first!=nullptr,"direction fixture import failed");
            const ra2map::Cell goal{static_cast<std::uint16_t>(54+2*dx[direction]),static_cast<std::uint16_t>(69+2*dy[direction])};
            const auto plan=imported.first->begin(world,2,goal,fixture.actor,fixture.cb);
            if (plan.status!=GoalRuntimeStatus::Complete) std::cerr<<"direction "<<direction<<": "<<plan.detail<<'\n';
            require(plan.status==GoalRuntimeStatus::Complete && plan.directions==std::vector<unsigned>({direction,direction}),"direction search differs");
            for (unsigned frame=1;frame<1000;++frame) {
                const auto tick=imported.first->tick(world,2,fixture.cb);
                if (tick.status!=GoalRuntimeStatus::Complete) std::cerr<<"direction "<<direction<<" frame "<<frame<<": "<<tick.detail<<'\n';
                require(tick.status==GoalRuntimeStatus::Complete,"direction drive failed");
                const auto mix=[&](std::uint32_t n){hash=(hash^n)*16777619u;case_hash=(case_hash^n)*16777619u;};
                const auto& e=world.entities[0];
                for (auto n:{direction,static_cast<unsigned>(speed),frame,static_cast<unsigned>(e.position->x),static_cast<unsigned>(e.position->y),unsigned(*e.occupancy_tracking)}) mix(n);
                for (int i=0;i<3;++i) {
                    const auto& cell=world.cells.at(key({static_cast<std::uint16_t>(54+i*dx[direction]),static_cast<std::uint16_t>(69+i*dy[direction])}));
                    mix(cell.heads->ground_head==2);mix(*cell.ground_occupancy_124);
                }
                ++checks;++case_frames;if (!imported.first->moving(2)) break;
                require(frame!=999,"direction drive did not finish");
            }
            const auto expected=goldens[case_index++];
            if (case_frames!=expected.frames || case_hash!=expected.hash)
                std::cerr<<"direction "<<direction<<" speed "<<speed<<" frames "<<case_frames<<" hash "<<std::hex<<case_hash
                         <<" expected "<<expected.frames<<" "<<expected.hash<<std::dec<<'\n';
            require(expected.direction==direction && expected.speed==speed && case_frames==expected.frames && case_hash==expected.hash,
                    "original drive case differs");
        }
        if (checks!=3932 || hash!=0x413d4d59u) std::cerr<<"native vector frames="<<checks<<" hash="<<std::hex<<hash<<std::dec<<'\n';
        require(checks==3932 && hash==0x413d4d59u,"original occupancy vectors differ");
        std::cout<<checks<<" original frames across eight directions and five speeds match\n";return 0;
    }
    Fixture f;
    auto incomplete=f.world;incomplete.setup_progress=sw::SetupProgress::PackedMapRead;
    auto phase=GoalRuntime::import(incomplete,f.setup,argv[1]);
    require(!phase.first && phase.second.status==GoalRuntimeStatus::NeedsInput,"unfinished setup was accepted");
    auto bad=f.world;bad.cells.at(key({54,70})).cell_flag_100.reset();
    auto rejected=GoalRuntime::import(bad,f.setup,argv[1]);
    require(!rejected.first && rejected.second.status==GoalRuntimeStatus::NeedsInput,"unknown bridge was accepted");
    bad=f.world;bad.cells.at(key({54,70})).cell_field_12c.reset();
    rejected=GoalRuntime::import(bad,f.setup,argv[1]);
    require(!rejected.first && rejected.second.status==GoalRuntimeStatus::NeedsInput,"unknown Cell visibility was accepted");
    bad=f.world;bad.cells.at(key({54,70})).neighbor_count.reset();
    rejected=GoalRuntime::import(bad,f.setup,argv[1]);
    require(!rejected.first && rejected.second.status==GoalRuntimeStatus::NeedsInput,"unknown hierarchy byte was accepted");
    auto missing_type=f.setup;missing_type.types.at(2).movement_zone.reset();
    rejected=GoalRuntime::import(f.world,missing_type,argv[1]);
    require(!rejected.first && rejected.second.status==GoalRuntimeStatus::NeedsInput,"unknown movement type was accepted");
    auto missing_motion=f.setup;missing_motion.foot.erase(2);
    rejected=GoalRuntime::import(f.world,missing_motion,argv[1]);
    require(!rejected.first && rejected.second.status==GoalRuntimeStatus::NeedsInput,"unknown Foot state was accepted");
    // Original Unit mark: remove a middle list item, then insert it at the head.
    auto marked=f.world;sw::Entity other;other.id=3;other.type="TREE";other.kind=sw::Kind::Terrain;
    other.lifetime=sw::Lifetime::Placed;other.next=2;other.position=ground_cell_center(54,69);
    other.cell=ra2map::Cell{54,69};other.layer=sw::Layer::Ground;marked.entities.push_back(other);marked.creation_order.push_back({sw::CreatedKind::Entity,3});
    auto& markcell=marked.cells.at(key({54,69}));markcell.heads->ground_head=3;markcell.ground_occupancy_124=0xa5a50020u;
    const auto before_mark=marked.random;
    auto unavailable=f.cb.mark;unavailable.cell_setup=[](sw::Snapshot& s,std::uint32_t)->std::optional<bool>{s.random.table[0]^=8;return {};};
    require(original_unit_mark(marked,2,0,unavailable).status==UnitMarkStatus::NeedsInput,"unknown Cell setup was accepted");
    require(marked.cells.at(key({54,69})).heads->ground_head==3 && marked.entities[1].next==2 &&
            marked.entities[0].object_field_74==1 && marked.random.table==before_mark.table,"failed Unit mark changed the world");
    auto mark_fixture=f.cb.mark;
    mark_fixture.cell_setup=[](sw::Snapshot&,std::uint32_t)->std::optional<bool>{return true;};
    auto mark_out=original_unit_mark(marked,2,0,mark_fixture);
    require(mark_out.status==UnitMarkStatus::Complete && mark_out.value && marked.entities[0].object_field_74==0 &&
            marked.cells.at(key({54,69})).heads->ground_head==3 && marked.entities[1].next==0 && marked.entities[0].next==0 &&
            marked.cells.at(key({54,69})).ground_occupancy_124==0xa5a50000u,"original Unit mark-out differs");
    auto mark_in=original_unit_mark(marked,2,1,mark_fixture);
    require(mark_in.status==UnitMarkStatus::Complete && mark_in.value && marked.entities[0].object_field_74==1 &&
            marked.cells.at(key({54,69})).heads->ground_head==2 && marked.entities[0].next==3 && marked.entities[1].next==0 &&
            marked.cells.at(key({54,69})).ground_occupancy_124==0xa5a50020u,"original Unit mark-in differs");
    auto refresh_world=f.world;refresh_world.entities[0].object_field_75=0;
    auto refresh_callbacks=f.cb.mark;
    require(original_unit_mark(refresh_world,2,0,refresh_callbacks).status==UnitMarkStatus::Complete,"mark-out before refresh failed");
    const auto refresh_before=refresh_world;
    require(original_unit_mark(refresh_world,2,1,refresh_callbacks).status==UnitMarkStatus::NeedsInput &&
            refresh_world.entities[0].object_field_75==0 && refresh_world.entities[0].object_field_74==0 &&
            refresh_world.random.table==refresh_before.random.table,"unknown Object refresh published partial changes");
    bool refreshed=false;
    refresh_callbacks.refresh_after_mark=[&](sw::Snapshot& s,std::uint32_t)->std::optional<bool>{
        refreshed=s.entities[0].object_field_75==1;return true;
    };
    require(original_unit_mark(refresh_world,2,1,refresh_callbacks).status==UnitMarkStatus::Complete && refreshed,
            "Object refresh byte was not written before the callback");
    auto one=GoalRuntime::import(f.world,f.setup,argv[1]);auto two=GoalRuntime::import(f.world,f.setup,argv[1]);
    if (!one.first) std::cerr<<one.second.detail<<"\n";
    require(one.first && two.first,"complete fixture import failed");
    auto world_one=f.world,world_two=f.world;
    auto failed=f.cb;failed.mark.object_pre_mark=[](sw::Snapshot& s,std::uint32_t,int)->std::optional<bool> {
        s.random.table[0]^=123;s.cells.at(key({54,69})).heads->ground_head=0;return {};
    };
    require(one.first->begin(world_one,2,{54,71},f.actor,failed).status==GoalRuntimeStatus::NeedsInput,"unknown mark was accepted");
    require(world_one.random.table==f.world.random.table && world_one.cells.at(key({54,69})).heads->ground_head==2,
        "failed search published a partial world");
    const auto result=one.first->begin(world_one,2,{54,71},f.actor,f.cb);const auto replay=two.first->begin(world_two,2,{54,71},f.actor,f.cb);
    if (result.status!=GoalRuntimeStatus::Complete) std::cerr<<result.detail<<'\n';
    require(result.status==GoalRuntimeStatus::Complete && result.directions==std::vector<unsigned>({4,4}),"search did not connect to drive");
    require(replay.status==GoalRuntimeStatus::Complete && replay.directions==result.directions,"search replay changed");
    // Other systems use the same Snapshot. Their resolved changes must survive.
    world_one.houses[0].cash=77;world_two.houses[0].cash=77;
    world_one.random.table[0]^=0x1234;world_two.random.table[0]^=0x1234;
    auto stale=world_one;stale.entities[0].position->y++;
    require(one.first->tick(stale,2,f.cb).status==GoalRuntimeStatus::Unsupported && stale.entities[0].position->y==69*256+129,
            "external actor change was accepted or overwritten");
    auto collision=world_one;collision.cells.at(key({54,70})).ground_occupancy_124=32;
    require(one.first->tick(collision,2,f.cb).status==GoalRuntimeStatus::Unsupported &&
            collision.cells.at(key({54,70})).ground_occupancy_124==32,"external Cell change was accepted or overwritten");
    auto damaged=world_one;damaged.entities[0].resolved_hp=399;
    require(one.first->tick(damaged,2,f.cb).status==GoalRuntimeStatus::Unsupported && damaged.entities[0].resolved_hp==399,
            "external health change was accepted or overwritten");
    auto linked=world_one;sw::Entity foreign;foreign.id=3;foreign.kind=sw::Kind::Terrain;foreign.type="TREE";
    foreign.lifetime=sw::Lifetime::Placed;foreign.constructor_complete=true;foreign.on_map=true;foreign.cell=ra2map::Cell{50,65};
    foreign.position=ground_cell_center(50,65);foreign.layer=sw::Layer::Ground;foreign.next=2;
    linked.entities.push_back(foreign);linked.creation_order.push_back({sw::CreatedKind::Entity,3});
    require(one.first->tick(linked,2,f.cb).status==GoalRuntimeStatus::Unsupported && linked.entities.back().next==2,
            "external shared link was accepted or overwritten");
    auto changed_hierarchy=world_one;changed_hierarchy.cells.at(key({50,65})).coarse_category=6;
    require(one.first->tick(changed_hierarchy,2,f.cb).status==GoalRuntimeStatus::NeedsInput &&
            changed_hierarchy.cells.at(key({50,65})).coarse_category==6,"stale hierarchy was accepted or overwritten");
    auto failing=f.cb;failing.mark.cell_setup=[](sw::Snapshot& s,std::uint32_t)->std::optional<bool>{s.random.table[1]^=456;return {};};
    const auto prior=world_one;const auto prior_motion=one.first->state().motion.at(2);
    require(one.first->tick(world_one,2,failing).status==GoalRuntimeStatus::NeedsInput,"unknown Cell setup during movement was accepted");
    require(world_one.random.table==prior.random.table && world_one.entities[0].position->y==prior.entities[0].position->y &&
            one.first->state().motion.at(2).sample==prior_motion.sample,"failed tick published partial movement");
    const int original_offsets[]={23,47,70,94,117,141,164,188,212,235,253,282,306,329,353,376,400,424,447,471,494,509,512};
    for (int frame=1;frame<=23;++frame) {
        if (frame==23) {
            auto no_finish=f.cb;no_finish.finish={};const auto before_finish=world_one;
            const auto no_finish_result=one.first->tick(world_one,2,no_finish);
            if (no_finish_result.status!=GoalRuntimeStatus::NeedsInput) std::cerr<<"completion gate: "<<no_finish_result.detail<<'\n';
            require(no_finish_result.status==GoalRuntimeStatus::NeedsInput &&
                    world_one.entities[0].position->y==before_finish.entities[0].position->y,
                    "unresolved completion published a partial frame");
        }
        const auto step=one.first->tick(world_one,2,f.cb);if(step.status!=GoalRuntimeStatus::Complete) std::cerr<<step.detail<<'\n';
        require(step.status==GoalRuntimeStatus::Complete && two.first->tick(world_two,2,f.cb).status==GoalRuntimeStatus::Complete,"drive tick failed");
        const auto& w=world_one;const auto& replay_world=world_two;
        require(w.entities[0].position->y==69*256+128+original_offsets[frame-1] &&
                replay_world.entities[0].position->y==w.entities[0].position->y && w.random.table==replay_world.random.table &&
                w.random.first==replay_world.random.first && w.random.second==replay_world.random.second,"drive frame or RNG replay differs");
        const int current=frame<=5?0:frame<=16?1:2;const int reserved=frame<=11?1:2;
        for (int i=0;i<3;++i) {
            const auto& cell=w.cells.at(key({54,static_cast<std::uint16_t>(69+i)}));
            require(cell.heads->ground_head==static_cast<std::uint32_t>(i==current?2:0) &&
                    *cell.ground_occupancy_124==static_cast<std::uint32_t>(i==reserved?32:0),"original ordered head or occupancy trace differs");
        }
        require(w.entities[0].occupancy_tracking==bool(frame==23) && w.entities[0].foot_field_5a1==0,
                "original Foot occupancy tracking differs");
    }
    require(!one.first->moving(2) && !two.first->moving(2),"straight drive did not finish");
    require(world_one.houses[0].cash==77 && world_one.random.table==world_two.random.table &&
            world_one.random.table[0]!=(f.world.random.table[0]),"another system's shared state was overwritten");
    // Compare all original virtual-position calls, including intermediate samples.
    const auto tables=DriveTables::from_executable(argv[1]);
    auto trace=DriveSegment::begin_route(ground_cell_center(54,69),ground_cell_center(54,71),128,15,{4,4},tables);
    int trace_frame=0,event_count=0;std::uint32_t event_hash=2166136261u;
    while (trace.active && trace_frame<100) {
        ++trace_frame;trace.tick(tables,[&](Coord,Coord now){
            for (const auto value:{trace_frame,now.x-(54*256+128),now.y-(69*256+128),now.z})
                event_hash=(event_hash^static_cast<std::uint32_t>(value))*16777619u;
            ++event_count;
        });
    }
    require(event_count==68 && event_hash==0x0f94096cu,"original per-position event order differs");
    std::cout<<"checked import, Unit marks, original search, 23-frame occupancy replay, and atomic rejection passed; external callbacks are synthetic\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
