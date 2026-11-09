#include "prototype_movement.h"
#include "../loop/profile.h"
#include "../weapons/occupant_order.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
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
// Busy is open ground held only by a Unit that has a pending move. A route
// may plan through it, as original pathing treats a moving blocker as
// temporary. Held is ground held only by a stopped Unit. Busy and Held
// Cells do not stop a planned diagonal step past their corner; walls,
// buildings and blocked terrain do. A Unit may only enter an Open Cell.
// At move time, a diagonal step waits while a corner Cell is Busy, so two
// Units never cross one Cell corner together.
enum class Pass : std::uint8_t { Unknown=0,Open=1,Closed=2,Unresolved=3,Busy=4,Held=5 };
using Movers=std::set<std::uint32_t>;
Pass open_state(const session::world::CellState& cell,std::uint32_t self,const Movers* movers) {
    if (!cell.allocated || !cell.heads || !cell.ground_occupancy_124 || !cell.cell_flag_100 ||
        !cell.terrain || !cell.terrain->floor_world_height) return Pass::Unresolved;
    if (!*cell.allocated || *cell.cell_flag_100) return Pass::Closed;
    if (cell.unit_land_speed && *cell.unit_land_speed<=0) return Pass::Closed;
    const auto head=cell.heads->ground_head;
    if (head==self) return Pass::Open;
    if (head==0 && *cell.ground_occupancy_124==0) return Pass::Open;
    if (head && *cell.ground_occupancy_124==0x20u) return movers && movers->count(head)?Pass::Busy:Pass::Held;
    return Pass::Closed;
}
Pass pass_cell(const World& w,ra2map::Cell c,std::uint32_t self,const Movers* movers) {
    if (c.x>=512 || c.y>=512) return Pass::Closed;
    const auto it=w.cells.find(key(c));
    if (it==w.cells.end()) return Pass::Closed; // The prototype setup owns the complete allocation list.
    return open_state(it->second,self,movers);
}
struct RouteResult { PrototypeMoveStatus status=PrototypeMoveStatus::Blocked;std::vector<ra2map::Cell> path;std::string detail; };
// Work shared by the searches of one route_or_nearby call. The World, the
// Unit and the start do not change during that call, so each Cell answer
// of open_cell() stays the same. After one search finds no route, its
// touched Cells are every Cell reachable from the start; a later goal
// outside that set gets the same "no route" answer without a new search.
struct SearchMemo {
    // One Pass value for each Cell; Unknown until it is read.
    std::vector<std::uint8_t> open=std::vector<std::uint8_t>(512*512,0);
    std::vector<std::uint8_t> reachable=std::vector<std::uint8_t>(512*512,0);
    std::vector<std::uint32_t> touched,reached;
    bool have_reachable=false,all=false;
    const Movers* movers=nullptr;
    static std::uint8_t code(Pass answer) { return static_cast<std::uint8_t>(answer); }
    Pass cell(const World& w,ra2map::Cell c,std::uint32_t self) {
        if (c.x>=512 || c.y>=512) return Pass::Closed;
        auto& value=open[key(c)];
        if (!value) {
            // After fill(), a Cell without an answer is not in the map.
            if (all) return Pass::Closed;
            // A map search is slow when the map is not in the CPU cache.
            // Answer the 16 Cells of this row block with one search and a
            // short ordered walk; a route search reads nearby Cells next.
            // A Cell missing from the map is closed, as in open_cell().
            const auto first=key(c)&~15u,last=first+16;
            auto it=w.cells.lower_bound(first);
            for (auto k=first;k<last;++k) {
                auto answer=Pass::Closed;
                if (it!=w.cells.end() && it->first==k) { answer=open_state(it->second,self,movers); ++it; }
                if (!open[k]) { open[k]=code(answer); touched.push_back(k); }
            }
            // A large search reads every Cell once instead.
            if (touched.size()>=8192) fill(w,self);
        }
        return static_cast<Pass>(value);
    }
    // Answer every Cell of the map in one ordered walk. It walks eight key
    // ranges in step so the CPU can load several map nodes at once.
    void fill(const World& w,std::uint32_t self) {
        if (w.cells.size()<lanes) {
            for (const auto& item:w.cells) set(item,self);
        } else {
            using Iterator=std::map<std::uint32_t,session::world::CellState>::const_iterator;
            std::array<Iterator,lanes+1> bounds;
            bounds[0]=w.cells.begin(); bounds[lanes]=w.cells.end();
            const auto low=w.cells.begin()->first,high=w.cells.rbegin()->first;
            for (std::size_t i=1;i<lanes;++i)
                bounds[i]=w.cells.lower_bound(low+std::uint32_t((std::uint64_t(high-low)*i)/lanes));
            std::array<Iterator,lanes> at;
            for (std::size_t i=0;i<lanes;++i) at[i]=bounds[i];
            for (bool more=true;more;) {
                more=false;
                for (std::size_t i=0;i<lanes;++i) if (at[i]!=bounds[i+1]) { set(*at[i],self); ++at[i]; more=true; }
            }
        }
        all=true;
    }
    void set(const std::pair<const std::uint32_t,session::world::CellState>& item,std::uint32_t self) {
        if (item.first>=512u*512u) return; // open_cell() never looks up such a key.
        auto& value=open[item.first];
        if (value) return;
        value=code(open_state(item.second,self,movers));
        touched.push_back(item.first);
    }
    void clear() {
        for (const auto i:touched) open[i]=0;
        for (const auto i:reached) reachable[i]=0;
        touched.clear();reached.clear();have_reachable=false;all=false;movers=nullptr;
    }
    static constexpr std::size_t lanes=8;
};
RouteResult route(const World& world,std::uint32_t id,ra2map::Cell start,ra2map::Cell goal,SearchMemo* memo=nullptr) {
    const auto open_cell=[&](const World& w,ra2map::Cell c,std::uint32_t self) {
        return memo?memo->cell(w,c,self):pass_cell(w,c,self,nullptr);
    };
    if (!world.cell_order) return {PrototypeMoveStatus::NeedsInput,{},"prototype Cell allocation order is unresolved"};
    if (goal.x>=512 || goal.y>=512) return {PrototypeMoveStatus::Invalid,{},"goal is outside the Cell array"};
    const auto destination=open_cell(world,goal,id);
    if (destination==Pass::Unresolved) return {PrototypeMoveStatus::NeedsInput,{},"goal Cell state is unresolved"};
    if (destination==Pass::Closed || destination==Pass::Held) return {PrototypeMoveStatus::Blocked,{},"goal Cell is blocked"};
    if (same(start,goal)) return {PrototypeMoveStatus::Complete,{},{}};
    struct Node { int f,g;std::uint32_t serial,index; };
    struct Later { bool operator()(const Node& a,const Node& b) const {return a.f!=b.f?a.f>b.f:a.serial>b.serial;} };
    constexpr int infinity=std::numeric_limits<int>::max();
    // Reuse the two 512*512 search arrays. Reset only the touched entries,
    // on every return path, so each search starts from the same state.
    struct Search {
        std::vector<int> cost=std::vector<int>(512*512,std::numeric_limits<int>::max());
        std::vector<std::uint32_t> previous=std::vector<std::uint32_t>(512*512,0xffffffffu);
        std::vector<std::uint32_t> touched;
    };
    static thread_local Search search;
    struct Reset {
        Search& s;
        ~Reset() { for (const auto i:s.touched) {s.cost[i]=std::numeric_limits<int>::max();s.previous[i]=0xffffffffu;} s.touched.clear(); }
    } reset{search};
    auto& cost=search.cost;auto& previous=search.previous;auto& touched=search.touched;
    std::priority_queue<Node,std::vector<Node>,Later> open;std::uint32_t serial=0;
    const auto heuristic=[&](int x,int y) {const int a=std::abs(x-int(goal.x)),b=std::abs(y-int(goal.y));return 10*std::max(a,b)+4*std::min(a,b);};
    const auto source=key(start),target=key(goal);cost[source]=0;touched.push_back(source);open.push({heuristic(start.x,start.y),0,serial++,source});
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
            if (available==Pass::Unresolved) return {PrototypeMoveStatus::NeedsInput,{},"prototype path reached unknown Cell state"};
            if (available==Pass::Closed || available==Pass::Held) continue;
            if (d%2) {
                const auto horizontal=open_cell(world,{static_cast<std::uint16_t>(nx),static_cast<std::uint16_t>(y)},id);
                const auto vertical=open_cell(world,{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(ny)},id);
                if (horizontal==Pass::Unresolved || vertical==Pass::Unresolved) return {PrototypeMoveStatus::NeedsInput,{},"prototype corner Cell state is unresolved"};
                if (horizontal==Pass::Closed || vertical==Pass::Closed) continue;
            }
            // A Cell held by a moving Unit costs two extra steps. The route
            // goes around a long column but not far around one tank.
            const auto index=key(next);const int next_cost=node.g+(d%2?14:10)+(available==Pass::Busy?20:0);
            if (next_cost>=cost[index]) continue;
            if (cost[index]==infinity) touched.push_back(index);
            cost[index]=next_cost;previous[index]=node.index;open.push({next_cost+heuristic(nx,ny),next_cost,serial++,index});
        }
    }
    if (memo && !memo->have_reachable) {
        for (const auto i:touched) {memo->reachable[i]=1;memo->reached.push_back(i);}
        memo->have_reachable=true;
    }
    return {PrototypeMoveStatus::Blocked,{},"no open prototype route"};
}
// TODO-ORIGINAL: the playable group order chooses a nearby open Cell when
// the requested Cell is blocked. Original Foot retargeting remains separate.
// With movers, a Cell held by a moving Unit is not a wall; without them,
// every Unit is a wall. Each ring around the goal is read nearest to the
// start first, so the Unit stops on the near side and does not drive
// around the goal. If the start is the best open Cell, the Unit stays.
RouteResult route_or_nearby(const World& world,std::uint32_t id,ra2map::Cell start,ra2map::Cell goal,const Movers* movers) {
    static thread_local SearchMemo shared;
    struct Clear { SearchMemo& m; ~Clear() { m.clear(); } } clear{shared};
    auto* memo=&shared;memo->movers=movers;
    auto result=route(world,id,start,goal,memo);
    if (result.status!=PrototypeMoveStatus::Blocked) return result;
    std::vector<std::pair<std::int64_t,ra2map::Cell>> ring;
    for (int radius=1;radius<=8;++radius) {
        ring.clear();
        for (int dy=-radius;dy<=radius;++dy) for (int dx=-radius;dx<=radius;++dx) {
            if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
            const int x=int(goal.x)+dx,y=int(goal.y)+dy;
            if (x<0 || y<0 || x>=512 || y>=512) continue;
            const std::int64_t sx=x-int(start.x),sy=y-int(start.y);
            ring.push_back({sx*sx+sy*sy,{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)}});
        }
        // Stable: equal distances keep the row order, so replays match.
        std::stable_sort(ring.begin(),ring.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        for (const auto& [distance,candidate]:ring) {
            if (same(candidate,start)) return {PrototypeMoveStatus::Complete,{},{}};
            const auto open=memo->cell(world,candidate,id);
            if (open==Pass::Unresolved) return {PrototypeMoveStatus::NeedsInput,{},"nearby prototype Cell state is unresolved"};
            if (open!=Pass::Open) continue;
            // A Cell outside the known reachable set gives the same "no route"
            // result that a full search would return.
            if (memo->have_reachable && !memo->reachable[key(candidate)]) continue;
            auto alternative=route(world,id,start,candidate,memo);
            if (alternative.status==PrototypeMoveStatus::Complete && !alternative.path.empty()) return alternative;
            if (alternative.status==PrototypeMoveStatus::NeedsInput || alternative.status==PrototypeMoveStatus::Invalid) return alternative;
        }
    }
    return result;
}
// Frames a Unit waits behind a moving Unit before it plans around it.
constexpr int wait_limit=32;
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
    loop::profile::Scope probe(loop::profile::Phase::Movement);
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
    Movers movers;
    for (const auto& item:moves_) if (item.first!=id) movers.insert(item.first);
    const auto path=route_or_nearby(world,id,*actor->cell,goal,&movers);
    if (path.status!=PrototypeMoveStatus::Complete) return fail(path.status,path.detail);
    moves_[id]={goal,speed,path.path};
    return {PrototypeMoveStatus::Complete,{},{},{},{}};
}
void PrototypeMovement::cancel(World& world,std::uint32_t id) {
    moves_.erase(id);
    if (auto* actor=find_unit(world,id)) actor->locomotor_moving=false;
}
PrototypeMoveResult PrototypeMovement::tick(World& world,std::uint32_t frame) {
    loop::profile::Scope probe(loop::profile::Phase::Movement);
    if (world.setup_progress!=session::world::SetupProgress::PrototypeComplete)
        return fail(PrototypeMoveStatus::NeedsInput,"prototype Battle setup is unfinished");
    if (last_frame_) {
        const auto elapsed=std::uint32_t(frame-*last_frame_);
        if (!elapsed || elapsed>0x7fffffffu) return fail(PrototypeMoveStatus::Invalid,"prototype frame is repeated or in the past");
    }
    if (moves_.empty()) {
        last_frame_=frame;
        return {PrototypeMoveStatus::Complete,{},{},{},{}};
    }
    // Edit the world in place. Keep the prior Entities and each Cell before
    // its first transfer, and restore them if any step fails. This replaces
    // a full Snapshot copy with the same all-or-nothing result.
    struct Journal {
        World& world; std::vector<session::world::Entity> entities; std::vector<std::pair<std::uint32_t,session::world::CellState>> cells;
        bool keep=false;
        void cell(std::uint32_t key) {
            for (const auto& saved:cells) if (saved.first==key) return;
            const auto found=world.cells.find(key);
            if (found!=world.cells.end()) cells.emplace_back(key,found->second);
        }
        ~Journal() {
            if (keep) return;
            world.entities=std::move(entities);
            for (auto it=cells.rbegin();it!=cells.rend();++it) world.cells.find(it->first)->second=std::move(it->second);
        }
    } journal{world,world.entities,{}};
    auto& candidate=world;auto moves=moves_;PrototypeMoveResult result;result.status=PrototypeMoveStatus::Complete;
    Movers movers;
    for (const auto& item:moves) movers.insert(item.first);
    const auto finish=[&](std::uint32_t id,session::world::Entity* actor) { actor->locomotor_moving=false;movers.erase(id);result.arrived.push_back(id); };
    for (auto it=moves.begin();it!=moves.end();) {
        const auto id=it->first;auto& movement=it->second;auto* actor=find_unit(candidate,id);
        if (!actor || actor->lifetime==session::world::Lifetime::Destroyed || (actor->resolved_hp && *actor->resolved_hp<=0)) {
            if (actor) actor->locomotor_moving=false;
            movers.erase(id);it=moves.erase(it);continue;
        }
        if (const auto error=unit_error(actor)) return fail(PrototypeMoveStatus::NeedsInput,*error);
        const auto source=candidate.cells.find(key(*actor->cell));
        if (source==candidate.cells.end() || !source->second.heads || !source->second.ground_occupancy_124 ||
            !source->second.cell_flag_100) return fail(PrototypeMoveStatus::NeedsInput,"prototype source Cell is unresolved");
        if (*source->second.cell_flag_100 || source->second.heads->ground_head!=id || *actor->next!=0 ||
            !(*source->second.ground_occupancy_124&0x20u)) return fail(PrototypeMoveStatus::Blocked,"prototype source Unit is not isolated on ground");
        if (movement.path.empty() && !same(*actor->cell,movement.goal)) {
            // The route ended on a nearby Cell because the goal was held
            // when it was planned. The holder can be a group Unit that
            // has since left. Plan once more; a nearby route can only come
            // closer to the goal, so this ends.
            const auto revised=route_or_nearby(candidate,id,*actor->cell,movement.goal,&movers);
            if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) return fail(revised.status,revised.detail);
            if (revised.status==PrototypeMoveStatus::Complete) movement.path=revised.path;
        }
        if (movement.path.empty()) { finish(id,actor);it=moves.erase(it);continue; }
        // Wait behind a Unit that still moves. Do not plan a detour around
        // it, as that sends a group sideways and back. Plan again when the
        // blocker stops, or after a long wait.
        const auto wait_for=[&](ra2map::Cell cell) {
            const auto found=candidate.cells.find(key(cell));
            if (found==candidate.cells.end() || !found->second.heads) return false;
            const auto blocker=moves.find(found->second.heads->ground_head);
            return blocker!=moves.end() && blocker->first!=id && !blocker->second.stalled && movement.waited<wait_limit;
        };
        const auto hold=[&] {
            ++movement.waited;movement.stalled=true;actor->locomotor_moving=false;++it;
        };
        // A moving Unit in a corner Cell of a diagonal step, if any.
        const auto busy_corner=[&](ra2map::Cell from,ra2map::Cell to)->std::optional<ra2map::Cell> {
            if (from.x==to.x || from.y==to.y) return {};
            for (const ra2map::Cell corner:{ra2map::Cell{to.x,from.y},ra2map::Cell{from.x,to.y}})
                if (pass_cell(candidate,corner,id,&movers)==Pass::Busy) return corner;
            return {};
        };
        auto target=movement.path.front();auto available=pass_cell(candidate,target,id,&movers);
        if (available==Pass::Unresolved) return fail(PrototypeMoveStatus::NeedsInput,"prototype next Cell is unresolved");
        if (available==Pass::Busy && wait_for(target)) { hold();continue; }
        if (available!=Pass::Open) {
            // A wall or a stopped Unit: plan around it, through moving
            // Units. A Unit that blocks too long becomes a wall too.
            const bool strict=available==Pass::Busy;
            const auto revised=route_or_nearby(candidate,id,*actor->cell,movement.goal,strict?nullptr:&movers);
            if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) return fail(revised.status,revised.detail);
            if (revised.status==PrototypeMoveStatus::Blocked) { actor->locomotor_moving=false;movement.stalled=true;result.blocked.push_back(id);++it;continue; }
            movement.path=revised.path;
            if (movement.path.empty()) { finish(id,actor);it=moves.erase(it);continue; }
            target=movement.path.front();
            if (pass_cell(candidate,target,id,nullptr)!=Pass::Open) { hold();continue; }
        }
        if (const auto corner=busy_corner(*actor->cell,target)) {
            if (wait_for(*corner)) { hold();continue; }
        }
        // Move speed world units this frame. Units left at a Cell center
        // carry on toward the next Cell in the same frame if it is open.
        int budget=movement.speed;bool moved=false,blocked=false;
        while (budget>0 && !movement.path.empty()) {
            target=movement.path.front();
            if (moved && (pass_cell(candidate,target,id,nullptr)!=Pass::Open || busy_corner(*actor->cell,target))) break;
            auto found=candidate.cells.find(key(target));
            if (found==candidate.cells.end() || !found->second.terrain || !found->second.terrain->floor_world_height)
                return fail(PrototypeMoveStatus::NeedsInput,"prototype target floor is unresolved");
            const auto old=*actor->position;const Coord desired{int(target.x)*256+128,int(target.y)*256+128,*found->second.terrain->floor_world_height};
            const auto dx=std::int64_t(desired.x)-old.x,dy=std::int64_t(desired.y)-old.y,dz=std::int64_t(desired.z)-old.z;
            const auto distance=std::max(std::abs(dx),std::abs(dy));
            Coord next=desired;auto used=distance;
            if (distance>budget) {
                next.x=old.x+static_cast<int>(dx*budget/distance);
                next.y=old.y+static_cast<int>(dy*budget/distance);
                next.z=old.z+static_cast<int>(dz*budget/distance);
                used=budget;
            }
            if (next.x<0 || next.y<0 || next.x/256>=512 || next.y/256>=512)
                return fail(PrototypeMoveStatus::Invalid,"prototype position leaves the Cell array");
            const ra2map::Cell entered{static_cast<std::uint16_t>(next.x/256),static_cast<std::uint16_t>(next.y/256)};
            if (!same(*actor->cell,entered)) {
                const auto step=pass_cell(candidate,entered,id,&movers);
                if (step==Pass::Unresolved) return fail(PrototypeMoveStatus::NeedsInput,"prototype crossed Cell is unresolved");
                if (step!=Pass::Open && !same(entered,target)) {
                    // A Unit stopped off its Cell center cuts across a Cell
                    // that is not on its route. Go back to its own center
                    // first; that point stays inside its own Cell.
                    movement.path.insert(movement.path.begin(),*actor->cell);
                    continue;
                }
                if (step!=Pass::Open) {
                    if (!moved) blocked=!(step==Pass::Busy && wait_for(entered));
                    break;
                }
                journal.cell(key(*actor->cell));journal.cell(key(entered));
                if (const auto error=transfer(candidate,id,*actor->cell,entered)) return fail(PrototypeMoveStatus::NeedsInput,*error);
                actor=find_unit(candidate,id);actor->cell=entered;
            }
            // TODO-ORIGINAL: turn rates, acceleration, track samples and facing AI.
            const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
            for (unsigned d=0;d<8;++d) if (directions[d][0]==sx && directions[d][1]==sy) {
                actor->hull_facing=static_cast<int>(d*32);actor->hull_facing_raw=static_cast<std::uint16_t>(d*8192);break;
            }
            actor->position=next;actor->collision_virtual_position=next;moved=true;budget-=static_cast<int>(used);
            if (next.x==desired.x && next.y==desired.y && next.z==desired.z) movement.path.erase(movement.path.begin());
            else break;
        }
        if (blocked) { actor->locomotor_moving=false;movement.stalled=true;result.blocked.push_back(id);++it;continue; }
        if (!moved) { hold();continue; }
        movement.waited=0;movement.stalled=false;actor->locomotor_moving=true;result.moved.push_back(id);
        if (movement.path.empty() && same(*actor->cell,movement.goal)) {finish(id,actor);it=moves.erase(it);} else ++it;
    }
    journal.keep=true;moves_=std::move(moves);last_frame_=frame;return result;
}
}
