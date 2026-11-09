#include "goal_runtime.h"
#include "world_init.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ra2::sim {
namespace {
using World=session::world::Snapshot;
using Entity=session::world::Entity;
GoalRuntimeResult missing(std::string text) { return {GoalRuntimeStatus::NeedsInput,std::move(text),{}}; }
GoalRuntimeResult unsupported(std::string text) { return {GoalRuntimeStatus::Unsupported,std::move(text),{}}; }
std::uint32_t key(ra2map::Cell cell) { return std::uint32_t(cell.y)*512+cell.x; }
bool same(Coord a,Coord b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
bool same(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
bool same_watched(const session::world::CellState& a,const session::world::CellState& b) {
    if (!a.heads || !b.heads || !a.terrain || !b.terrain) return false;
    return a.heads->ground_head==b.heads->ground_head && a.heads->bridge_head==b.heads->bridge_head &&
        a.ground_occupancy_124==b.ground_occupancy_124 && a.bridge_occupancy_128==b.bridge_occupancy_128 &&
        a.cell_flags_140==b.cell_flags_140 && a.neighbor_count==b.neighbor_count && a.usable==b.usable &&
        a.wall_owner==b.wall_owner && a.land_wheel==b.land_wheel && a.cell_field_11d==b.cell_field_11d && a.cell_field_12c==b.cell_field_12c &&
        a.overlay_data_11e==b.overlay_data_11e &&
        a.coarse_category==b.coarse_category && a.coarse_category_writes==b.coarse_category_writes && a.coarse_cache_height==b.coarse_cache_height &&
        a.hierarchy_cache_height==b.hierarchy_cache_height && a.unit_land_speed==b.unit_land_speed &&
        a.cell_field_54==b.cell_field_54 && a.cell_field_58==b.cell_field_58 && a.tube_field_116==b.tube_field_116 &&
        a.terrain->tile_index==b.terrain->tile_index && a.terrain->subtile==b.terrain->subtile &&
        a.terrain->tile_replacement_resolved==b.terrain->tile_replacement_resolved && a.terrain->tmp_terrain==b.terrain->tmp_terrain &&
        a.terrain->resolved_tile_index==b.terrain->resolved_tile_index && a.terrain->height_level==b.terrain->height_level &&
        a.terrain->ramp_index==b.terrain->ramp_index && a.terrain->floor_world_height==b.terrain->floor_world_height &&
        a.terrain->land_type==b.terrain->land_type && a.terrain->overlay_type_index==b.terrain->overlay_type_index;
}
Entity* entity(World& world,std::uint32_t id) {
    const auto it=std::find_if(world.entities.begin(),world.entities.end(),[&](const Entity& e){return e.id==id;});
    return it==world.entities.end()?nullptr:&*it;
}
std::optional<std::string> check_world(const World& world,const GoalRuntimeSetup& setup) {
    if (world.edition!=session::world::Edition::Base) return "only base RA2 setup is supported";
    if (world.pending_cell_setup || world.pending_setup_cell) return "Cell setup is unfinished";
    if (!session::world::validate(world)) return "snapshot links or random state are invalid";
    if (!world.geometry || !setup.frame || world.setup_progress!=session::world::SetupProgress::Complete || !world.cell_order)
        return "map geometry, frame, original Cell order, or completed setup is unresolved";
    if (!world.tube_vector_count || *world.tube_vector_count!=0) return "tube setup is unresolved or unsupported";
    if (!setup.components.transitions || !setup.components.transitions->empty()) return "bridge transition records are unresolved or unsupported";
    if (setup.allocated.size()!=512u*512u) return "complete Cell allocation array is unresolved";
    const auto stride64=std::int64_t(world.geometry->width)+world.geometry->height+1;
    if (stride64<3 || stride64>512 || setup.hierarchy_cells.size()!=std::size_t(stride64)*std::size_t(stride64))
        return "complete hierarchy Cell table is unresolved";
    const auto stride=static_cast<std::size_t>(stride64);
    for (const auto& item:world.cells) {
        if (item.first>=setup.allocated.size() || !item.second.allocated ||
            *item.second.allocated!=setup.allocated[item.first]) return "Cell allocation disagrees with the direct array";
    }
    for (std::size_t i=0;i<setup.allocated.size();++i) if (setup.allocated[i]) {
        const auto it=world.cells.find(static_cast<std::uint32_t>(i));
        if (it==world.cells.end()) return "allocated Cell record is missing";
        const auto& c=it->second;
        if (!c.heads || !c.terrain || !c.usable || !c.coarse_category || !c.coarse_cache_height ||
            !c.hierarchy_cache_height || !c.neighbor_count || !c.ground_occupancy_124 || !c.bridge_occupancy_128 ||
            !c.unit_land_speed || !c.cell_field_11d || !c.cell_field_12c || !c.cell_flags_140 || !c.cell_flag_20000 || !c.wall_owner ||
            !c.cell_flag_100 || !c.cell_flag_200 || !c.cell_flag_40000 || !c.cell_field_54 ||
            !c.cell_field_58 || !c.tube_field_116) return "allocated Cell state is unresolved";
        const auto x=i%512,y=i/512;
        if (x>=stride || y>=stride) return "allocated Cell lies outside the hierarchy grid";
        const auto& hierarchy=setup.hierarchy_cells[y*stride+x];
        if (!hierarchy.coarse.category || !hierarchy.coarse.height || !hierarchy.usable ||
            *hierarchy.coarse.category!=*c.coarse_category || *hierarchy.coarse.height!=*c.coarse_cache_height ||
            *hierarchy.coarse.height!=*c.hierarchy_cache_height || *hierarchy.usable!=*c.usable)
            return "Cell hierarchy changed or is unresolved";
        const auto& t=*c.terrain;
        if (!t.height_level || !t.ramp_index || !t.floor_world_height || !t.land_type || !t.bridge ||
            !t.resolved_tile_index || !t.overlay_type_index) return "allocated Cell terrain is unresolved";
        if (*c.cell_flag_100!=*t.bridge) return "Cell bridge fields disagree";
        if (*c.cell_flag_100) return "bridge movement and hierarchy are unsupported";
        if (*t.height_level < -128 || *t.height_level > 127 || *t.ramp_index<0 || *t.ramp_index>19)
            return "Cell height or ramp exceeds the recovered domain";
    }
    for (const auto& e:world.entities) if (e.lifetime==session::world::Lifetime::Placed) {
        if (!e.next || !e.position || !e.cell || !e.layer || !e.on_map) return "placed object state or ordered link is unresolved";
        if (e.kind==session::world::Kind::Unit) {
            if (!e.base_profile || !e.hull_facing || !e.occupancy_tracking || !e.resolved_hp ||
                !e.object_field_74 || !e.object_field_75 || !e.object_field_76 || !e.techno_field_370 || !e.unit_field_7d || !e.foot_field_5a1)
                return "placed Unit runtime fields are unresolved";
            const auto& p=*e.base_profile;
            if (!p.restricted_land || !p.movement_mode || !p.placement_mode || !p.speed_type || !p.level_height ||
                !p.leptons_per_sight_increase || !p.type_flag_aa0 || !p.unit_flag_32c || !p.linked_team ||
                !p.type_flag_205 || !p.unit_flag_32a || !p.type_flag_bb0 || !p.type_flag_bb1)
                return "placed Unit type fields are unresolved";
            const auto f=setup.foot.find(e.id); const auto s=setup.search.find(e.id); const auto m=setup.motion.find(e.id);
            const auto t=setup.types.find(e.id);
            if (t==setup.types.end() || !t->second.movement_zone || !t->second.is_train || !t->second.threat_weight || !t->second.speed)
                return "Unit movement type or speed is unresolved";
            if (f==setup.foot.end() || s==setup.search.end() || m==setup.motion.end()) return "Foot path, drive motion, or retained search state is unresolved";
            const auto& foot=f->second;
            if (!foot.field_640 || !foot.field_644 || !foot.field_648 || !foot.cell_558 ||
                std::any_of(foot.words.begin(),foot.words.end(),[](const auto& word){return !word;}))
                return "Foot path queue or retry fields are unresolved";
            for (std::size_t i=0;i<foot.words.size();++i) if (*foot.words[i]!=m->second.path[i])
                return "Foot and locomotor path words disagree";
            if (std::any_of(s->second.counts.begin(),s->second.counts.end(),[](std::uint32_t n){return n>500;}))
                return "retained path count exceeds original storage";
        }
    }
    return {};
}
std::size_t graph_index(ra2map::Cell cell,unsigned stride) {
    const auto x=static_cast<std::int16_t>(cell.x),y=static_cast<std::int16_t>(cell.y);
    const auto linear=std::int64_t(y)*stride+x;
    return static_cast<std::size_t>(std::clamp<std::int64_t>(linear,0,std::int64_t(stride)*stride-1));
}
}
GoalRuntime::GoalRuntime(GoalRuntimeSetup setup,OriginalDistance distance,DriveTables tables,
                         Hierarchy hierarchy,MovementRegions movement,unsigned stride):
    state_(std::move(setup)),distance_(std::move(distance)),tables_(std::move(tables)),
    hierarchy_(std::move(hierarchy)),movement_(std::move(movement)),stride_(stride) {}
std::pair<std::unique_ptr<GoalRuntime>,GoalRuntimeResult> GoalRuntime::import(
    const World& world,const GoalRuntimeSetup& setup,const std::string& executable) {
    if (const auto error=check_world(world,setup)) return {nullptr,missing(*error)};
    const auto sum=std::int64_t(world.geometry->width)+world.geometry->height+1;
    if (sum<3 || sum>512) return {nullptr,unsupported("map hierarchy stride is outside the recovered domain")};
    const auto stride=static_cast<unsigned>(sum);
    try {
        const auto order=original_map_cell_order(world.geometry->width,world.geometry->height);
        if (world.cell_order->size()!=order.size()) return {nullptr,missing("original Cell order has the wrong length")};
        for (std::size_t i=0;i<order.size();++i)
            if (world.cell_order->at(i)!=key(order[i])) return {nullptr,missing("original Cell order differs from map initialization")};
    } catch(const std::exception& e) { return {nullptr,unsupported(e.what())}; }
    if (setup.hierarchy_cells.size()!=std::size_t(stride)*stride) return {nullptr,missing("complete hierarchy Cell table is unresolved")};
    for (std::size_t i=0;i<setup.hierarchy_cells.size();++i) {
        const auto& h=setup.hierarchy_cells[i];
        if (!h.coarse.category || !h.coarse.height || !h.usable) return {nullptr,missing("hierarchy Cell category, height, or usability is unresolved")};
        const auto cellkey=std::uint32_t(i/stride)*512+std::uint32_t(i%stride);
        if (setup.allocated[cellkey]) {
            const auto& c=world.cells.at(cellkey);
            if (*h.coarse.category!=*c.coarse_category || *h.coarse.height!=*c.coarse_cache_height || *h.coarse.height!=*c.hierarchy_cache_height || *h.usable!=*c.usable)
                return {nullptr,missing("hierarchy inputs disagree with the loaded Cells")};
        }
    }
    try {
        auto distance=OriginalDistance::from_executable(executable);
        auto tables=DriveTables::from_executable(executable);
        if (tables.edition()!=DriveEdition::BaseRa2) return {nullptr,unsupported("runtime needs the original base game executable")};
        auto hierarchy=build_original_hierarchy(stride,setup.hierarchy_cells);
        auto movement=build_original_movement_regions(hierarchy.coarse,DriveEdition::BaseRa2);
        auto ptr=std::unique_ptr<GoalRuntime>(new GoalRuntime(setup,std::move(distance),std::move(tables),std::move(hierarchy),std::move(movement),stride));
        return {std::move(ptr),GoalRuntimeResult{GoalRuntimeStatus::Complete,{},{}}};
    } catch(const std::exception& e) { return {nullptr,unsupported(e.what())}; }
}
GoalRuntimeResult GoalRuntime::begin(World& world,std::uint32_t id,ra2map::Cell goal,const GoalRuntimeActor& actor,const GoalRuntimeCallbacks& cb) {
    if (const auto error=check_world(world,state_)) return missing(*error);
    if (!active_.empty()) return unsupported("concurrent movement and replanning are not recovered");
    const auto source_entity=entity(world,id);
    if (!source_entity || source_entity->kind!=session::world::Kind::Unit || source_entity->lifetime!=session::world::Lifetime::Placed)
        return unsupported("movement needs a placed Unit");
    if (!actor.movement_zone || !actor.special_guard || !actor.threat_weight || !actor.speed ||
        !actor.destination_guards || !actor.foot.frame || *actor.foot.frame!=*state_.frame) return missing("actor movement type, guards, speed, threat, or frame is unresolved");
    const auto& type=state_.types.at(id);
    if (!actor.foot.is_train || *actor.foot.is_train!=*type.is_train || *actor.movement_zone!=*type.movement_zone ||
        *actor.threat_weight!=*type.threat_weight || *actor.speed!=*type.speed)
        return missing("Unit movement type changed or is unresolved");
    if (*actor.threat_weight!=0.0) return unsupported("nonzero actor threat is not recovered");
    if (!source_entity->hull_facing || !source_entity->hull_facing_raw) return missing("Unit full hull facing is unresolved");
    if (*source_entity->hull_facing<0 || *source_entity->hull_facing>255 ||
        *source_entity->hull_facing_raw!=static_cast<unsigned>(*source_entity->hull_facing)*256u ||
        (*source_entity->hull_facing_raw%8192)!=0) return unsupported("Unit hull facing is not aligned to a recovered drive direction");
    if (!source_entity->resolved_hp || !source_entity->maximum_hp) return missing("Unit health or maximum health is unresolved");
    if (*source_entity->resolved_hp<=0 || *source_entity->resolved_hp!=*source_entity->maximum_hp)
        return unsupported("damaged or dead Unit drive is not recovered");
    if (!source_entity->on_map || !source_entity->layer || *source_entity->layer!=session::world::Layer::Ground ||
        !source_entity->cell || !source_entity->position || !source_entity->hull_facing) return missing("actor ground placement is unresolved");
    if (!cb.can_queue || !cb.enter) return missing("original queue or entry callback is unresolved");
    if (!source_entity->unit_field_7d || !*source_entity->unit_field_7d || !source_entity->base_profile ||
        !source_entity->base_profile->type_flag_aa0 || *source_entity->base_profile->type_flag_aa0)
        return unsupported("Unit continuation or crusher behavior is unresolved");
    if (goal.x>=512 || goal.y>=512 || !state_.allocated[key(goal)]) return missing("goal Cell allocation is unresolved or null");
    if (actor.foot.append_offset!=0 || actor.foot.mark_mode!=0) return unsupported("only a fresh unmarked Foot path is supported");
    const auto& idle=state_.motion.at(id);
    if (idle.destination || idle.anchor || idle.track!=-1 || idle.sample!=0 || idle.remainder!=0 || idle.second_track || idle.segment_active)
        return unsupported("only a fully resolved idle drive state is supported");
    auto candidate=world; auto setup=state_; const auto source=*source_entity->cell;
    const auto* initial=entity(candidate,id);
    if (initial->position->x!=int(source.x)*256+128 || initial->position->y!=int(source.y)*256+128)
        return unsupported("drive segment needs the original aligned Cell center");
    FootPathInputs inputs=actor.foot; inputs.goal=goal;
    FootPathCallbacks foot;
    foot.can_queue=[&](Coord point){return cb.can_queue(candidate,id,point);};
    foot.position=[&]()->std::optional<Coord>{const auto* e=entity(candidate,id);return e?e->position:std::nullopt;};
    foot.current_cell=[&]()->std::optional<ra2map::Cell>{const auto* e=entity(candidate,id);return e?e->cell:std::nullopt;};
    foot.enter_goal=[&](ra2map::Cell c){return cb.enter(candidate,id,c);};
    std::optional<UnitMarkResult> mark_error;
    foot.mark=[&](bool value)->std::optional<bool>{
        const auto result=original_unit_mark(candidate,id,value?1:0,cb.mark);
        if (result.status!=UnitMarkStatus::Complete) { mark_error=result;return {}; }
        return result.value;
    };
    GoalRuntimeResult search_error=missing("original goal search did not run");
    PathResult result_path;
    foot.find=[&](const FootPathQuery& query)->std::optional<ResolvedFootPath> {
        if (query.goal.x>=512 || query.goal.y>=512 || !setup.allocated[key(query.goal)]) { search_error=missing("search goal Cell is unresolved");return {}; }
        const auto* unit=entity(candidate,id);
        if (!unit || !unit->cell || !unit->on_map) { search_error=missing("actor state changed during marking");return {}; }
        const auto target=candidate.cells.find(key(query.goal)), start=candidate.cells.find(key(*unit->cell));
        if (target==candidate.cells.end() || start==candidate.cells.end() || !target->second.usable || !start->second.usable) { search_error=missing("endpoint usability is unresolved");return {}; }
        const auto component=[&](ra2map::Cell cell)->MovementComponentResult {
            return original_movement_component(hierarchy_.coarse,movement_,stride_,{cell,*actor.movement_zone,false},setup.components,{});
        };
        const auto a=component(*unit->cell),b=component(query.goal);
        if (a.status!=ComponentLookupStatus::Complete || b.status!=ComponentLookupStatus::Complete || !a.component || !b.component || *a.component>65535 || *b.component>65535) {
            search_error=missing("endpoint component lookup is unresolved");return {};
        }
        std::vector<ResolvedGroundPathCell> cells; cells.reserve(candidate.cells.size());
        for (const auto& pair:candidate.cells) if (pair.second.allocated && *pair.second.allocated) {
            const auto& c=pair.second;
            if (!c.terrain || !c.terrain->height_level || !c.terrain->ramp_index || !c.cell_flag_100 || !c.cell_flag_200) { search_error=missing("search Cell height or flags are unresolved");return {}; }
            const ra2map::Cell point{static_cast<std::uint16_t>(pair.first%512),static_cast<std::uint16_t>(pair.first/512)};
            const auto index=graph_index(point,stride_);
            EntryHeightCell height{static_cast<std::int8_t>(*c.terrain->height_level),static_cast<std::uint8_t>(*c.terrain->ramp_index),*c.cell_flag_100,*c.cell_flag_200};
            cells.push_back({point,cb.enter(candidate,id,point),height,hierarchy_.layers[0].labels.at(index),c.neighbor_count,c.cell_flag_40000});
        }
        GoalPathDecisions decisions;
        decisions.source_component=static_cast<std::uint16_t>(*a.component);decisions.goal_component=static_cast<std::uint16_t>(*b.component);
        decisions.is_train=inputs.is_train; decisions.on_map=unit->on_map; decisions.special_guard=actor.special_guard;
        decisions.source_usable=start->second.usable; decisions.goal_usable=target->second.usable;
        decisions.search_mark_mode=query.mark_mode; decisions.expansion_limit=-1;
        HierarchyRoute route; std::array<std::vector<std::pair<std::uint16_t,std::uint16_t>>,3> blocked;
        GoalPathCallbacks calls;
        calls.reset_search=[]{}; calls.clear_blocked=[&](unsigned level){blocked.at(level).clear();};
        calls.find_hierarchy=[&]()->std::optional<bool> {
            HierarchyRouteInputs hi; hi.source_index=graph_index(*unit->cell,stride_); hi.destination_index=graph_index(query.goal,stride_);
            hi.edition=DriveEdition::BaseRa2; hi.movement_zone=*actor.movement_zone;hi.blocked_edges=blocked;
            route=find_original_hierarchy_route(hierarchy_,hi); retain_original_hierarchy_paths(setup.search.at(id),route);return route.found;
        };
        calls.find_regular=[&](bool hierarchical,int limit) {
            PathHierarchyFilter filter; if (hierarchical) filter.allowed_zones=route.layers[0].allowed;
            PathPostprocessing post{actor.threat_weight};
            return find_original_ground_path(*unit->cell,query.goal,cells,distance_,limit,hierarchical?&filter:nullptr,&post,&setup.search.at(id));
        };
        calls.retry=[&](bool allowed)->std::optional<bool> {
            if (!cb.reachability) return {};
            HierarchyRetryInputs ri;const auto& memory=setup.search.at(id);ri.progress_cell=memory.progress_cell_70;
            for (unsigned level=0;level<3;++level)
                ri.paths[level].assign(memory.paths[level].begin(),memory.paths[level].begin()+memory.counts[level]);
            HierarchyRetryState previous;previous.allow_hierarchy_38=allowed;previous.blocked=blocked;
            HierarchyRetryCallbacks rcb;
            rcb.region_entry=[&](ra2map::Cell c)->std::optional<RetryRegionEntry>{return graph_index(c,stride_);};
            rcb.region=[&](RetryRegionEntry i,unsigned level)->std::optional<std::uint16_t>{
                if (level>=3 || i>=hierarchy_.layers[level].labels.size()) return {};
                return hierarchy_.layers[level].labels[i];
            };
            rcb.reachability=[&](ra2map::Cell c,unsigned level){return cb.reachability(candidate,id,c,level);};
            const auto retry=run_original_hierarchy_retry(hierarchy_,ri,previous,rcb);
            if (retry.status!=RetryEdgesStatus::Complete) return {};
            blocked=retry.state.blocked;return retry.state.allow_hierarchy_38;
        };
        const auto found=run_original_ground_goal(decisions,calls); result_path=found.path;
        if (found.path.status!=PathStatus::Found || !same(found.path.end,query.goal)) {
            search_error=found.path.status==PathStatus::NeedsInput?missing(found.path.evidence):unsupported(found.path.evidence.empty()?"partial or failed path needs original retry and failure bodies":found.path.evidence);
            return {};
        }
        std::uint32_t bits; std::memcpy(&bits,&found.path.cost,sizeof(bits));
        ResolvedFootPath path;path.present=true;path.cost_key=static_cast<std::int32_t>(bits);path.words=std::vector<std::int32_t>{};
        for (auto direction:found.path.directions) path.words->push_back(static_cast<std::int32_t>(direction));
        path.words->push_back(-1);search_error={GoalRuntimeStatus::Complete,{},{}};return path;
    };
    FootPathResult planned;
    try { planned=original_unit_basic_path(setup.foot.at(id),inputs,distance_,foot); }
    catch (const std::exception& e) { return unsupported(e.what()); }
    if (mark_error) return mark_error->status==UnitMarkStatus::NeedsInput?missing(mark_error->detail):unsupported(mark_error->detail);
    if (search_error.status!=GoalRuntimeStatus::Complete && std::find(planned.steps.begin(),planned.steps.end(),FootPathStep::Find)!=planned.steps.end()) return search_error;
    if (planned.status!=FootPathStatus::Complete) return planned.status==FootPathStatus::NeedsInput?missing(planned.detail):unsupported(planned.detail);
    if (!planned.accepted) return unsupported("Foot rejected the goal; its failure effects are not connected");
    if (result_path.directions.empty() || result_path.directions.size()>23) return unsupported("drive route exceeds the recovered queue slice");
    if (std::any_of(result_path.directions.begin(),result_path.directions.end(),[&](unsigned d){return d!=result_path.directions.front();}))
        return unsupported("per-sample facing effects for turning tracks are unresolved");
    const auto end=candidate.cells.find(key(goal));
    if (end==candidate.cells.end() || !end->second.terrain || !end->second.terrain->floor_world_height) return missing("goal floor is unresolved");
    const Coord destination{int(goal.x)*256+128,int(goal.y)*256+128,*end->second.terrain->floor_world_height};
    std::optional<DriveSegment> segment;
    const auto* after_mark=entity(candidate,id);
    if (!after_mark || !after_mark->position || !after_mark->cell || !after_mark->hull_facing ||
        !same(*after_mark->position,*source_entity->position) || !same(*after_mark->cell,source) ||
        *after_mark->hull_facing!=*source_entity->hull_facing) return missing("marking changed the Unit drive state");
    try { segment=DriveSegment::begin_route(*after_mark->position,destination,static_cast<unsigned>(*after_mark->hull_facing),*actor.speed,result_path.directions,tables_); }
    catch(const std::exception& e) { return unsupported(e.what()); }
    // Preserve the original queue tail while applying the preparation shift.
    std::array<std::int32_t,24> words{};
    for (std::size_t i=0;i<words.size();++i) {
        if (!planned.state.words[i]) return missing("Foot path tail is unresolved");
        words[i]=*planned.state.words[i];
    }
    const auto first=static_cast<unsigned>(words[0]); const auto second=words[1]<0?first:static_cast<unsigned>(words[1]);
    if (first>7 || second>7) return unsupported("Foot queue contains an unsupported direction");
    auto track=first*8+second; if (!tables_.control(track).primary) track=first*9;
    if (tables_.control(track).flags&8) { for (unsigned i=0;i<22;++i) words[i]=words[i+2];words[22]=-1; }
    else for (unsigned i=0;i<23;++i) words[i]=words[i+1];
    segment->motion.path=words;
    // The supported track slice requires every swept cell to be flat and
    // empty. A visual overlay absence does not supply the bridge flag.
    for (const auto& pair:segment->swept_cells(tables_)) {
        if (pair[0]<0 || pair[1]<0 || pair[0]>=512 || pair[1]>=512) return unsupported("drive track leaves the Cell array");
        const ra2map::Cell point{static_cast<std::uint16_t>(pair[0]),static_cast<std::uint16_t>(pair[1])};
        const auto it=candidate.cells.find(key(point));
        if (it==candidate.cells.end() || !it->second.allocated || !*it->second.allocated || !it->second.heads ||
            !it->second.terrain || !it->second.ground_occupancy_124 || !it->second.bridge_occupancy_128 ||
            !it->second.unit_land_speed || !it->second.cell_flag_100 || !it->second.cell_flag_40000)
            return missing("drive swept Cell state is unresolved");
        const auto& c=it->second;const auto& terrain=*c.terrain;
        if (!terrain.ramp_index || !terrain.floor_world_height || !terrain.overlay_type_index) return missing("drive swept terrain is unresolved");
        if (*c.cell_flag_100 || *c.cell_flag_40000 || *terrain.ramp_index!=0 || *terrain.floor_world_height!=after_mark->position->z ||
            *terrain.overlay_type_index!=-1 || *c.unit_land_speed!=1.0f || c.heads->bridge_head || *c.bridge_occupancy_128)
            return unsupported("drive swept Cell is outside the recovered flat empty slice");
        if (same(point,source)) {
            if (c.heads->ground_head!=id || *c.ground_occupancy_124!=32 || !after_mark->next || *after_mark->next)
                return unsupported("drive source has another occupant or unsupported occupancy");
        } else if (c.heads->ground_head || *c.ground_occupancy_124) return unsupported("drive swept Cell is occupied");
    }
    auto motion=idle;
    if (!motion.move_to(destination,*actor.destination_guards,0)) return unsupported("original drive destination guard rejected the goal");
    for (std::size_t i=0;i<planned.state.words.size();++i) motion.path[i]=*planned.state.words[i];
    setup.motion[id]=motion;
    setup.foot[id]=planned.state;
    if (const auto error=check_world(candidate,setup)) return missing(*error);
    if (!cb.mark.bridge_height) return missing("original bridge height is unresolved");
    ActiveDrive running;running.health=*source_entity->resolved_hp;running.bridge_height=*cb.mark.bridge_height;
    const auto* prepared=entity(candidate,id);
    if (!prepared || !prepared->occupancy_tracking || !prepared->object_field_74 || !prepared->object_field_75 || !prepared->foot_field_5a1)
        return missing("prepared Unit tracking or mark state is unresolved");
    running.tracking=*prepared->occupancy_tracking;running.mark=*prepared->object_field_74;
    running.refresh=*prepared->object_field_75;running.foot_5a1=*prepared->foot_field_5a1;
    for (const auto& pair:segment->swept_cells(tables_)) {
        const auto cell=std::uint32_t(pair[1])*512+std::uint32_t(pair[0]);
        running.watched_cells.emplace_back(cell,candidate.cells.at(cell));
    }
    running.segment=std::move(*segment);
    world=std::move(candidate);state_=std::move(setup);active_.emplace(id,std::move(running));
    return {GoalRuntimeStatus::Complete,{},result_path.directions};
}
GoalRuntimeResult GoalRuntime::tick(World& world,std::uint32_t id,const GoalRuntimeCallbacks& cb) {
    if (const auto error=check_world(world,state_)) return missing(*error);
    const auto active=active_.find(id);
    if (active==active_.end()) return unsupported("Unit has no active drive segment");
    if (!cb.mark.bridge_height || *cb.mark.bridge_height!=active->second.bridge_height)
        return missing("original bridge height changed or is unresolved");
    const auto* current=entity(world,id);
    if (!current || !current->position || !same(*current->position,active->second.segment.current) ||
        !current->hull_facing || *current->hull_facing!=static_cast<int>(active->second.segment.facing) ||
        !current->hull_facing_raw || *current->hull_facing_raw!=active->second.segment.facing*256u ||
        !current->unit_field_7d || !*current->unit_field_7d || !current->resolved_hp || *current->resolved_hp!=active->second.health || !current->maximum_hp ||
        *current->maximum_hp!=active->second.health || !current->occupancy_tracking ||
        *current->occupancy_tracking!=active->second.tracking || !current->object_field_74 ||
        *current->object_field_74!=active->second.mark || !current->object_field_75 ||
        *current->object_field_75!=active->second.refresh || !current->foot_field_5a1 ||
        *current->foot_field_5a1!=active->second.foot_5a1)
        return unsupported("another action changed the active Unit drive state");
    for (const auto& watched:active->second.watched_cells) {
        const auto found=world.cells.find(watched.first);
        if (found==world.cells.end() || !same_watched(found->second,watched.second))
            return unsupported("another action changed a drive Cell");
    }
    auto candidate=world;auto setup=state_;auto segment=active->second.segment;
    struct StepError { GoalRuntimeResult result; };
    const auto mark=[&](bool adding) {
        const auto out=original_unit_mark(candidate,id,adding?1:0,cb.mark);
        if (out.status!=UnitMarkStatus::Complete || !out.value)
            throw StepError{out.status==UnitMarkStatus::Unsupported?unsupported(out.detail):missing(out.detail.empty()?"original Unit mark did not complete":out.detail)};
    };
    const auto occupant_cell=[&](Coord point)->session::world::CellState& {
        if (point.x<0 || point.y<0 || point.x/256>=512 || point.y/256>=512)
            throw StepError{unsupported("drive occupancy leaves the original Cell array")};
        const ra2map::Cell at{static_cast<std::uint16_t>(point.x/256),static_cast<std::uint16_t>(point.y/256)};
        const auto found=candidate.cells.find(key(at));
        if (found==candidate.cells.end() || !found->second.allocated || !*found->second.allocated ||
            !found->second.ground_occupancy_124 || !found->second.bridge_occupancy_128 ||
            !found->second.cell_flag_100 || !found->second.terrain || !found->second.terrain->floor_world_height)
            throw StepError{missing("drive occupancy Cell is unresolved")};
        if (*found->second.cell_flag_100 || !cb.mark.bridge_height || *cb.mark.bridge_height<=0 ||
            std::int64_t(point.z)>=std::int64_t(*found->second.terrain->floor_world_height)+*cb.mark.bridge_height)
            throw StepError{unsupported("drive occupancy bridge layer is not recovered")};
        return found->second;
    };
    try {
        segment.tick(tables_,[&](Coord old,Coord now) {
            auto* actor=entity(candidate,id);
            if (!actor || !actor->position || !same(*actor->position,old) || !actor->object_field_74 ||
                !actor->unit_field_7d || !*actor->unit_field_7d)
                throw StepError{missing("position virtual source state is unresolved")};
            const ra2map::Cell from{static_cast<std::uint16_t>(old.x/256),static_cast<std::uint16_t>(old.y/256)};
            const ra2map::Cell to{static_cast<std::uint16_t>(now.x/256),static_cast<std::uint16_t>(now.y/256)};
            const auto marked=*actor->object_field_74;
            if (!same(from,to)) mark(false);
            else *actor->object_field_74=0;
            actor=entity(candidate,id);
            if (!actor || !actor->object_field_74 || *actor->object_field_74)
                throw StepError{missing("position virtual needs Object +74 clear")};
            // Base 004CA160 calls 005D51C0 when Object +74 is clear.
            actor->position=now;actor->cell=to;
            if (!same(from,to)) mark(true);
            else *actor->object_field_74=marked;
        },[&](const DriveEvent& event) {
            auto* actor=entity(candidate,id);
            if (!actor || !actor->position || !same(*actor->position,event.current) || !actor->occupancy_tracking || !actor->foot_field_5a1 ||
                !actor->unit_field_7d || !*actor->unit_field_7d)
                throw StepError{missing("drive Foot tracking state is unresolved")};
            if (event.kind==DriveEventKind::Prepare) {
                // 004A2670 marks the Unit, then 004A0BC0 reserves the anchor.
                if (!event.motion.anchor) throw StepError{missing("drive anchor is unresolved")};
                mark(false);mark(true);
                auto& reserved=occupant_cell(*event.motion.anchor);
                *reserved.ground_occupancy_124|=0x20u;
            } else if (event.kind==DriveEventKind::Sample) {
                // 004A1000 clears current occupancy before the first track sample.
                if (*actor->occupancy_tracking) {
                    auto& current=occupant_cell(event.current);
                    *current.ground_occupancy_124&=~0x20u;
                    actor->occupancy_tracking=false;actor->foot_field_5a1=0;
                }
            } else {
                actor->occupancy_tracking=true;actor->foot_field_5a1=0;
            }
        });
    } catch(const StepError& e) { return e.result; }
      catch(const std::exception& e) { return unsupported(e.what()); }
    if (!segment.active) {
        if (!cb.finish) return missing("original drive completion and mission effects are unresolved");
        try {
            const auto finished=cb.finish(candidate,id);
            if (!finished || !*finished) return missing("original drive completion and mission effects are unresolved");
        } catch(const std::exception& e) { return unsupported(e.what()); }
    }
    const auto* actor=entity(candidate,id);
    if (!actor || !actor->unit_field_7d || !*actor->unit_field_7d || !actor->position || !same(*actor->position,segment.current) || !actor->hull_facing || *actor->hull_facing!=static_cast<int>(segment.facing))
        return missing("drive actor position or facing is unresolved");
    for (std::size_t i=0;i<segment.motion.path.size();++i) setup.foot.at(id).words[i]=segment.motion.path[i];
    setup.motion.at(id)=segment.motion;++*setup.frame;
    if (const auto error=check_world(candidate,setup)) return missing(*error);
    auto watched=active->second.watched_cells;
    for (auto& cell:watched) cell.second=candidate.cells.at(cell.first);
    if (!actor->occupancy_tracking || !actor->object_field_74 || !actor->object_field_75 || !actor->foot_field_5a1)
        return missing("drive Unit tracking or mark state became unresolved");
    const auto tracking=*actor->occupancy_tracking;const auto mark_byte=*actor->object_field_74;
    const auto refresh=*actor->object_field_75;const auto foot_5a1=*actor->foot_field_5a1;
    world=std::move(candidate);state_=std::move(setup);
    if (segment.active) {
        active->second.segment=std::move(segment);active->second.watched_cells=std::move(watched);
        active->second.tracking=tracking;active->second.mark=mark_byte;active->second.refresh=refresh;active->second.foot_5a1=foot_5a1;
    }
    else active_.erase(active);
    return {GoalRuntimeStatus::Complete,{},{}};
}
}
