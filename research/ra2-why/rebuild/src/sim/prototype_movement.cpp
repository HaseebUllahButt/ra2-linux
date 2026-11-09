#include "prototype_movement.h"
#include "../weapons/occupant_order.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <queue>
#include <set>
#include <utility>
namespace ra2::sim {
namespace {
using World=session::world::Snapshot;
using Entity=session::world::Entity;
constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
std::uint32_t key(ra2map::Cell c) { return std::uint32_t(c.y)*512+c.x; }
bool same(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
PrototypeMoveResult fail(PrototypeMoveStatus status,std::string detail) { return {status,std::move(detail),{},{},{}}; }
const Entity* find_unit(const World& w,std::uint32_t id) {
    const auto it=std::find_if(w.entities.begin(),w.entities.end(),[&](const auto& e){return e.id==id;});
    return it==w.entities.end()?nullptr:&*it;
}
Entity* find_unit(World& w,std::uint32_t id) {
    const auto it=std::find_if(w.entities.begin(),w.entities.end(),[&](const auto& e){return e.id==id;});
    return it==w.entities.end()?nullptr:&*it;
}
std::optional<std::string> unit_error(const Entity* e) {
    if (!e || (e->kind!=session::world::Kind::Unit && e->kind!=session::world::Kind::Infantry) ||
        e->lifetime!=session::world::Lifetime::Placed) return "prototype movement needs a placed Unit or Infantry";
    if (!e->resolved_hp || !e->position || !e->cell || !e->layer || !e->next) return "prototype Unit state is unresolved";
    if (*e->resolved_hp<=0 || *e->layer!=session::world::Layer::Ground || e->cell->x>=512 || e->cell->y>=512 ||
        e->position->x<0 || e->position->y<0 || e->position->x/256!=e->cell->x || e->position->y/256!=e->cell->y)
        return "prototype Unit is dead, off-map, or not on its ground Cell";
    return {};
}
// TODO-ORIGINAL: this prototype uses explicit staged Cell occupancy and a
// conservative ground-only check, not original Unit CanEnterCell decisions.
std::optional<bool> open_cell(const World& w,ra2map::Cell c,std::uint32_t self) {
    if (c.x>=512 || c.y>=512) return false;
    const auto it=w.cells.find(key(c));
    if (it==w.cells.end()) return false; // The prototype setup owns the complete allocation list.
    const auto& cell=it->second;
    if (!cell.allocated || !cell.heads || !cell.ground_occupancy_124 || !cell.cell_flag_100 ||
        !cell.terrain || !cell.terrain->floor_world_height) return {};
    if (!*cell.allocated || *cell.cell_flag_100) return false;
    if (cell.unit_land_speed && *cell.unit_land_speed<=0) return false;
    if (cell.heads->ground_head==self) return true;
    return cell.heads->ground_head==0 && *cell.ground_occupancy_124==0;
}
struct RouteResult { PrototypeMoveStatus status=PrototypeMoveStatus::Blocked;std::vector<ra2map::Cell> path;std::string detail; };
RouteResult route(const World& world,std::uint32_t id,ra2map::Cell start,ra2map::Cell goal) {
    if (!world.cell_order) return {PrototypeMoveStatus::NeedsInput,{},"prototype Cell allocation order is unresolved"};
    if (goal.x>=512 || goal.y>=512) return {PrototypeMoveStatus::Invalid,{},"goal is outside the Cell array"};
    const auto destination=open_cell(world,goal,id);
    if (!destination) return {PrototypeMoveStatus::NeedsInput,{},"goal Cell state is unresolved"};
    if (!*destination) return {PrototypeMoveStatus::Blocked,{},"goal Cell is blocked"};
    if (same(start,goal)) return {PrototypeMoveStatus::Complete,{},{}};
    struct Node { int f,g;std::uint32_t serial,index; };
    struct Later { bool operator()(const Node& a,const Node& b) const {return a.f!=b.f?a.f>b.f:a.serial>b.serial;} };
    constexpr int infinity=std::numeric_limits<int>::max();
    std::vector<int> cost(512*512,infinity);std::vector<std::uint32_t> previous(512*512,0xffffffffu);
    std::priority_queue<Node,std::vector<Node>,Later> open;std::uint32_t serial=0;
    const auto heuristic=[&](int x,int y) {const int a=std::abs(x-int(goal.x)),b=std::abs(y-int(goal.y));return 10*std::max(a,b)+4*std::min(a,b);};
    const auto source=key(start),target=key(goal);cost[source]=0;open.push({heuristic(start.x,start.y),0,serial++,source});
    while (!open.empty()) {
        const auto node=open.top();open.pop();if (node.g!=cost[node.index]) continue;
        if (node.index==target) {
            std::vector<ra2map::Cell> out;
            for (auto cursor=target;cursor!=source;cursor=previous[cursor]) {
                if (cursor==0xffffffffu) return {PrototypeMoveStatus::Invalid,{},"prototype path chain is invalid"};
                out.push_back({static_cast<std::uint16_t>(cursor%512),static_cast<std::uint16_t>(cursor/512)});
            }
            std::reverse(out.begin(),out.end());return {PrototypeMoveStatus::Complete,std::move(out),{}};
        }
        const int x=int(node.index%512),y=int(node.index/512);
        for (unsigned d=0;d<8;++d) {
            const int nx=x+directions[d][0],ny=y+directions[d][1];if (nx<0 || ny<0 || nx>=512 || ny>=512) continue;
            const ra2map::Cell next{static_cast<std::uint16_t>(nx),static_cast<std::uint16_t>(ny)};
            const auto available=open_cell(world,next,id);
            if (!available) return {PrototypeMoveStatus::NeedsInput,{},"prototype path reached unknown Cell state"};
            if (!*available) continue;
            if (d%2) {
                const auto horizontal=open_cell(world,{static_cast<std::uint16_t>(nx),static_cast<std::uint16_t>(y)},id);
                const auto vertical=open_cell(world,{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(ny)},id);
                if (!horizontal || !vertical) return {PrototypeMoveStatus::NeedsInput,{},"prototype corner Cell state is unresolved"};
                if (!*horizontal || !*vertical) continue;
            }
            const auto index=key(next);const int next_cost=node.g+(d%2?14:10);
            if (next_cost>=cost[index]) continue;
            cost[index]=next_cost;previous[index]=node.index;open.push({next_cost+heuristic(nx,ny),next_cost,serial++,index});
        }
    }
    return {PrototypeMoveStatus::Blocked,{},"no open prototype route"};
}
// TODO-ORIGINAL: the playable group order chooses a nearby open Cell when
// the requested Cell is blocked. Original Foot retargeting remains separate.
RouteResult route_or_nearby(const World& world,std::uint32_t id,ra2map::Cell start,ra2map::Cell goal) {
    auto result=route(world,id,start,goal);
    if (result.status!=PrototypeMoveStatus::Blocked) return result;
    for (int radius=1;radius<=8;++radius) for (int dy=-radius;dy<=radius;++dy) for (int dx=-radius;dx<=radius;++dx) {
        if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
        const int x=int(goal.x)+dx,y=int(goal.y)+dy;
        if (x<0 || y<0 || x>=512 || y>=512) continue;
        const ra2map::Cell candidate{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        if (same(candidate,start)) continue;
        const auto open=open_cell(world,candidate,id);
        if (!open) return {PrototypeMoveStatus::NeedsInput,{},"nearby prototype Cell state is unresolved"};
        if (!*open) continue;
        auto alternative=route(world,id,start,candidate);
        if (alternative.status==PrototypeMoveStatus::Complete && !alternative.path.empty()) return alternative;
        if (alternative.status==PrototypeMoveStatus::NeedsInput || alternative.status==PrototypeMoveStatus::Invalid) return alternative;
    }
    return result;
}
std::optional<std::string> transfer(World& world,std::uint32_t id,ra2map::Cell from,ra2map::Cell to) {
    auto source=world.cells.find(key(from)),target=world.cells.find(key(to));
    if (source==world.cells.end() || target==world.cells.end() || !source->second.heads || !target->second.heads ||
        !source->second.ground_occupancy_124 || !target->second.ground_occupancy_124)
        return "prototype transfer Cell is unresolved";
    const auto* actor=find_unit(world,id);
    if (!actor || !actor->next || *actor->next!=0 || source->second.heads->ground_head!=id ||
        target->second.heads->ground_head || *target->second.ground_occupancy_124)
        return "prototype transfer needs an isolated source and an empty target";
    weapons::ContentLinks links;links.cells.emplace(key(from),*source->second.heads);links.cells.emplace(key(to),*target->second.heads);
    for (const auto& e:world.entities) {
        if (e.lifetime!=session::world::Lifetime::Placed) continue;
        if (!e.next) return "prototype ordered Object link is unresolved";
        links.objects.emplace(e.id,weapons::ContentObject{*e.next,e.kind==session::world::Kind::Structure?6:1});
    }
    if (links.remove(key(from),weapons::ContentLayer::Ground,id)!=weapons::ContentResult::Complete ||
        links.insert(key(to),weapons::ContentLayer::Ground,id)!=weapons::ContentResult::Complete)
        return "prototype ordered Object links are cyclic";
    source->second.heads=links.cells.at(key(from));target->second.heads=links.cells.at(key(to));
    for (auto& e:world.entities) {
        const auto link=links.objects.find(e.id);if (link!=links.objects.end()) e.next=link->second.next;
    }
    // TODO-ORIGINAL: Infantry needs its original subcell mask and reservation.
    *source->second.ground_occupancy_124&=~0x20u;*target->second.ground_occupancy_124|=0x20u;
    return {};
}
}
PrototypeMoveResult PrototypeMovement::issue(const World& world,std::uint32_t id,ra2map::Cell goal,int speed) {
    if (world.setup_progress!=session::world::SetupProgress::PrototypeComplete)
        return fail(PrototypeMoveStatus::NeedsInput,"prototype Battle setup is unfinished");
    const auto* actor=find_unit(world,id);
    if (const auto error=unit_error(actor)) return fail(PrototypeMoveStatus::NeedsInput,*error);
    if (speed<=0 || speed>256) return fail(PrototypeMoveStatus::Invalid,"prototype speed must be 1..256 world units per frame");
    const auto source=world.cells.find(key(*actor->cell));
    if (source==world.cells.end() || !source->second.heads || !source->second.ground_occupancy_124 ||
        !source->second.cell_flag_100) return fail(PrototypeMoveStatus::NeedsInput,"prototype source Cell is unresolved");
    if (*source->second.cell_flag_100 || source->second.heads->ground_head!=id || *actor->next!=0 ||
        !(*source->second.ground_occupancy_124&0x20u))
        return fail(PrototypeMoveStatus::Blocked,"prototype source Unit is not isolated on ground");
    const auto path=route_or_nearby(world,id,*actor->cell,goal);
    if (path.status!=PrototypeMoveStatus::Complete) return fail(path.status,path.detail);
    moves_[id]={goal,speed,path.path};
    return {PrototypeMoveStatus::Complete,{},{},{},{}};
}
void PrototypeMovement::cancel(World& world,std::uint32_t id) {
    moves_.erase(id);
    if (auto* actor=find_unit(world,id)) actor->locomotor_moving=false;
}
PrototypeMoveResult PrototypeMovement::tick(World& world,std::uint32_t frame) {
    if (world.setup_progress!=session::world::SetupProgress::PrototypeComplete)
        return fail(PrototypeMoveStatus::NeedsInput,"prototype Battle setup is unfinished");
    if (last_frame_) {
        const auto elapsed=std::uint32_t(frame-*last_frame_);
        if (!elapsed || elapsed>0x7fffffffu) return fail(PrototypeMoveStatus::Invalid,"prototype frame is repeated or in the past");
    }
    auto candidate=world;auto moves=moves_;PrototypeMoveResult result;result.status=PrototypeMoveStatus::Complete;
    for (auto it=moves.begin();it!=moves.end();) {
        const auto id=it->first;auto& movement=it->second;auto* actor=find_unit(candidate,id);
        if (!actor || actor->lifetime==session::world::Lifetime::Destroyed || (actor->resolved_hp && *actor->resolved_hp<=0)) {
            if (actor) actor->locomotor_moving=false;
            it=moves.erase(it);continue;
        }
        if (const auto error=unit_error(actor)) return fail(PrototypeMoveStatus::NeedsInput,*error);
        const auto source=candidate.cells.find(key(*actor->cell));
        if (source==candidate.cells.end() || !source->second.heads || !source->second.ground_occupancy_124 ||
            !source->second.cell_flag_100) return fail(PrototypeMoveStatus::NeedsInput,"prototype source Cell is unresolved");
        if (*source->second.cell_flag_100 || source->second.heads->ground_head!=id || *actor->next!=0 ||
            !(*source->second.ground_occupancy_124&0x20u)) return fail(PrototypeMoveStatus::Blocked,"prototype source Unit is not isolated on ground");
        if (movement.path.empty()) { actor->locomotor_moving=false;result.arrived.push_back(id);it=moves.erase(it);continue; }
        auto target=movement.path.front();auto available=open_cell(candidate,target,id);
        if (!available) return fail(PrototypeMoveStatus::NeedsInput,"prototype next Cell is unresolved");
        if (!*available) {
            const auto revised=route_or_nearby(candidate,id,*actor->cell,movement.goal);
            if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) return fail(revised.status,revised.detail);
            if (revised.status==PrototypeMoveStatus::Blocked) { actor->locomotor_moving=false;result.blocked.push_back(id);++it;continue; }
            movement.path=revised.path;
            if (movement.path.empty()) { actor->locomotor_moving=false;result.arrived.push_back(id);it=moves.erase(it);continue; }
            target=movement.path.front();
        }
        auto found=candidate.cells.find(key(target));
        if (found==candidate.cells.end() || !found->second.terrain || !found->second.terrain->floor_world_height)
            return fail(PrototypeMoveStatus::NeedsInput,"prototype target floor is unresolved");
        const auto old=*actor->position;const Coord desired{int(target.x)*256+128,int(target.y)*256+128,*found->second.terrain->floor_world_height};
        const auto dx=std::int64_t(desired.x)-old.x,dy=std::int64_t(desired.y)-old.y,dz=std::int64_t(desired.z)-old.z;
        const auto distance=std::max(std::abs(dx),std::abs(dy));
        Coord next=desired;
        if (distance>movement.speed) {
            next.x=old.x+static_cast<int>(dx*movement.speed/distance);
            next.y=old.y+static_cast<int>(dy*movement.speed/distance);
            next.z=old.z+static_cast<int>(dz*movement.speed/distance);
        }
        if (next.x<0 || next.y<0 || next.x/256>=512 || next.y/256>=512)
            return fail(PrototypeMoveStatus::Invalid,"prototype position leaves the Cell array");
        const ra2map::Cell entered{static_cast<std::uint16_t>(next.x/256),static_cast<std::uint16_t>(next.y/256)};
        if (!same(*actor->cell,entered)) {
            const auto step=open_cell(candidate,entered,id);
            if (!step) return fail(PrototypeMoveStatus::NeedsInput,"prototype crossed Cell is unresolved");
            if (!*step) {actor->locomotor_moving=false;result.blocked.push_back(id);++it;continue;}
            if (const auto error=transfer(candidate,id,*actor->cell,entered)) return fail(PrototypeMoveStatus::NeedsInput,*error);
            actor=find_unit(candidate,id);actor->cell=entered;
        }
        // TODO-ORIGINAL: turn rates, acceleration, track samples and facing AI.
        const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
        for (unsigned d=0;d<8;++d) if (directions[d][0]==sx && directions[d][1]==sy) {
            actor->hull_facing=static_cast<int>(d*32);actor->hull_facing_raw=static_cast<std::uint16_t>(d*8192);break;
        }
        actor->position=next;actor->collision_virtual_position=next;actor->locomotor_moving=true;result.moved.push_back(id);
        if (next.x==desired.x && next.y==desired.y && next.z==desired.z) movement.path.erase(movement.path.begin());
        if (movement.path.empty()) {actor->locomotor_moving=false;result.arrived.push_back(id);it=moves.erase(it);} else ++it;
    }
    world=std::move(candidate);moves_=std::move(moves);last_frame_=frame;return result;
}
}
