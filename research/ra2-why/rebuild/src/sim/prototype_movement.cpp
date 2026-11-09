#include "prototype_movement.h"
#include "../loop/profile.h"
#include "../weapons/occupant_order.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
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
PrototypeMoveResult fail(PrototypeMoveStatus status,std::string detail) { return {status,std::move(detail),{},{},{},{}}; }
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
// Busy is open ground held only by a Unit that has a pending move, or the
// Cell a vehicle is stepping into. A route may plan through it, as original
// pathing treats a moving blocker as temporary. Held is ground held only by
// a stopped Unit. Busy and Held Cells do not stop a planned diagonal step
// past their corner; walls, buildings and blocked terrain do. A Unit may
// only enter an Open Cell. At move time, a diagonal step waits while a
// corner Cell is Busy, so two Units never cross one Cell corner together.
enum class Pass : std::uint8_t { Unknown=0,Open=1,Closed=2,Unresolved=3,Busy=4,Held=5 };
using Movers=std::set<std::uint32_t>;
// Crushable Infantry: id -> (House, next object).
using Victims=std::map<std::uint32_t,std::pair<std::uint32_t,std::uint32_t>>;
// What one mover may enter. All pointers are optional.
struct Context {
    const Movers* movers=nullptr;
    // Cells that a stepping vehicle has claimed: Cell key -> vehicle.
    const std::map<std::uint32_t,std::uint32_t>* claims=nullptr;
    const Victims* victims=nullptr; // Set only for a crushing mover.
    std::uint32_t house=0;
    const std::array<float,12>* ground=nullptr;
};
std::uint32_t key(ra2map::Cell c);
Pass open_state(const session::world::CellState& cell,std::uint32_t at,std::uint32_t self,const Context* ctx) {
    if (!cell.allocated || !cell.heads || !cell.ground_occupancy_124 || !cell.cell_flag_100 ||
        !cell.terrain || !cell.terrain->floor_world_height) return Pass::Unresolved;
    if (!*cell.allocated || *cell.cell_flag_100) return Pass::Closed;
    if (cell.unit_land_speed && *cell.unit_land_speed<=0) return Pass::Closed;
    const auto head=cell.heads->ground_head;
    if (head==self) return Pass::Open;
    // 004A2670 enters a Cell only when its Ground cost for the SpeedType is
    // above zero; the zero case is refused by Can_Enter_Cell first.
    if (ctx && ctx->ground && cell.terrain->land_type && *cell.terrain->land_type>=0 &&
        *cell.terrain->land_type<12 && (*ctx->ground)[std::size_t(*cell.terrain->land_type)]<=0.0f) return Pass::Closed;
    if (ctx && ctx->claims) {
        const auto claim=ctx->claims->find(at);
        if (claim!=ctx->claims->end() && claim->second!=self) return Pass::Busy;
    }
    if (head==0 && *cell.ground_occupancy_124==0) return Pass::Open;
    // Base 004A2670 treats a crushable result as open for a Crusher
    // (Type +AF3); 00705160 then kills enemy crushable Infantry there.
    if (head && ctx && ctx->victims) {
        const auto victim=ctx->victims->find(head);
        if (victim!=ctx->victims->end() && victim->second.first!=ctx->house && victim->second.second==0)
            return Pass::Open;
    }
    if (head && *cell.ground_occupancy_124==0x20u)
        return ctx && ctx->movers && ctx->movers->count(head)?Pass::Busy:Pass::Held;
    return Pass::Closed;
}
Pass pass_cell(const World& w,ra2map::Cell c,std::uint32_t self,const Context* ctx) {
    if (c.x>=512 || c.y>=512) return Pass::Closed;
    const auto it=w.cells.find(key(c));
    if (it==w.cells.end()) return Pass::Closed; // The prototype setup owns the complete allocation list.
    return open_state(it->second,key(c),self,ctx);
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
    const Context* ctx=nullptr;
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
                if (it!=w.cells.end() && it->first==k) { answer=open_state(it->second,k,self,ctx); ++it; }
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
        value=code(open_state(item.second,item.first,self,ctx));
        touched.push_back(item.first);
    }
    void clear() {
        for (const auto i:touched) open[i]=0;
        for (const auto i:reached) reachable[i]=0;
        touched.clear();reached.clear();have_reachable=false;all=false;ctx=nullptr;
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
RouteResult route_or_nearby(const World& world,std::uint32_t id,ra2map::Cell start,ra2map::Cell goal,const Context* ctx) {
    static thread_local SearchMemo shared;
    struct Clear { SearchMemo& m; ~Clear() { m.clear(); } } clear{shared};
    auto* memo=&shared;memo->ctx=ctx;
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
constexpr const char* drive_locomotor="{4A582741-9839-11d1-B709-00A024DDAFD1}";
// Original LandType order of the Ground table at 00850EA0 (9 floats a row).
constexpr const char* land_names[12]={"Clear","Road","Water","Rock","Wall","Tiberium","Beach","Rough","Ice","Railroad","Tunnel","Weeds"};
constexpr const char* speed_names[8]={"Foot","Track","Wheel","Hover","Winged","Float","Amphibious","FloatBeach"};
int direction_of(ra2map::Cell from,ra2map::Cell to) {
    const int dx=(int(to.x)>int(from.x))-(int(to.x)<int(from.x)),dy=(int(to.y)>int(from.y))-(int(to.y)<int(from.y));
    for (int d=0;d<8;++d) if (directions[d][0]==dx && directions[d][1]==dy) return d;
    return -1;
}
Coord center(const World& w,ra2map::Cell c) {
    const auto& cell=w.cells.at(key(c));
    return {int(c.x)*256+128,int(c.y)*256+128,*cell.terrain->floor_world_height};
}
std::uint16_t facing_raw(const Entity& e) {
    if (e.hull_facing_raw) return *e.hull_facing_raw;
    return static_cast<std::uint16_t>(e.hull_facing.value_or(0)*256);
}
void set_facing(Entity& e,std::uint16_t raw) {
    e.hull_facing_raw=raw;e.hull_facing=static_cast<int>(raw>>8);
}
// 004C2DF0 Set_Speed: clamp the speed fraction to 0..1.
double set_speed(double value) { return value>=1.0?1.0:value<=0.0?0.0:value; }
}
void PrototypeMovement::use_rules(const rules::IniFile& rules) {
    rules_=&rules;profiles_.clear();
    // Base RulesClass +690..+6A8 (00648A9C..00648B0E), AudioVisual +1368
    // (006464FD), General +1380 (0064A89A, cells to leptons) and +13D0
    // (0064CFF2).
    globals_.tracked_uphill=rules.read_double("General","TrackedUphill",1.0);
    globals_.tracked_downhill=rules.read_double("General","TrackedDownhill",1.2);
    globals_.wheeled_uphill=rules.read_double("General","WheeledUphill",1.0);
    globals_.wheeled_downhill=rules.read_double("General","WheeledDownhill",1.2);
    globals_.condition_yellow=rules.read_double("AudioVisual","ConditionYellow",0.5);
    globals_.close_enough=static_cast<int>(rules.read_double("General","CloseEnough",2.25)*256.0);
    globals_.blockage_path_delay=rules.read_integer("General","BlockagePathDelay",60);
}
PrototypeMoveProfile PrototypeMovement::profile(const session::world::Entity& e) const {
    using session::world::Kind;
    const auto cache_key=e.type+(e.kind==Kind::Infantry?"#I":e.kind==Kind::Unit?"#U":"#O");
    const auto cached=profiles_.find(cache_key);
    if (cached!=profiles_.end()) return cached->second;
    PrototypeMoveProfile out;
    // InfantryType constructor 00509CE4 sets Crushable; other types clear it.
    out.crushable=e.kind==Kind::Infantry;
    out.drive=e.kind==Kind::Unit;
    out.speed_type=e.kind==Kind::Infantry?0:1;
    if (rules_) {
        const auto& r=*rules_;const auto& t=e.type;
        out.drive=e.kind==Kind::Unit && r.read_string(t,"Locomotor")==drive_locomotor;
        out.crusher=r.read_bool(t,"Crusher",false);
        out.crushable=r.read_bool(t,"Crushable",out.crushable);
        // TechnoType 006D9C07..006D9C2F and 006DA50F defaults.
        out.accelerates=r.read_bool(t,"Accelerates",true);
        out.slowdown_distance=r.read_integer(t,"SlowdownDistance",500);
        out.deceleration=r.read_double(t,"DeaccelerationFactor",0.002);
        out.acceleration=r.read_double(t,"AccelerationFactor",0.03);
        out.rot=r.read_integer(t,"ROT",0);
        out.strength=r.read_integer(t,"Strength",0);
        // UnitType 0070ABEA: no SpeedType gives Track for a Crusher, else Wheel.
        if (e.kind==Kind::Unit) out.speed_type=out.crusher?1:2;
        const auto authored=r.read_string(t,"SpeedType");
        for (int i=0;i<8;++i) if (authored==speed_names[i]) out.speed_type=i;
        for (int land=0;land<12;++land)
            out.ground[std::size_t(land)]=static_cast<float>(r.read_double(land_names[land],speed_names[out.speed_type],1.0));
    }
    profiles_.emplace(cache_key,out);
    return out;
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
    const auto self=profile(*actor);
    Movers movers;
    for (const auto& item:moves_) if (item.first!=id) movers.insert(item.first);
    std::map<std::uint32_t,std::uint32_t> claims;
    for (const auto& [owner,m]:motion_) if (m.stepping && owner!=id) { claims[key(m.from)]=owner;claims[key(m.to)]=owner; }
    Victims victims;
    if (self.crusher) for (const auto& e:world.entities)
        if (e.kind==session::world::Kind::Infantry && e.lifetime==session::world::Lifetime::Placed && e.next &&
            e.resolved_hp.value_or(0)>0 && profile(e).crushable) victims[e.id]={e.house_id,*e.next};
    const Context ctx{&movers,&claims,self.crusher?&victims:nullptr,actor->house_id,rules_?&self.ground:nullptr};
    const auto path=route_or_nearby(world,id,*actor->cell,goal,&ctx);
    if (path.status!=PrototypeMoveStatus::Complete) return fail(path.status,path.detail);
    moves_[id]={goal,speed,path.path};
    return {PrototypeMoveStatus::Complete,{},{},{},{},{}};
}
void PrototypeMovement::cancel(World& world,std::uint32_t id) {
    moves_.erase(id);
    const auto m=motion_.find(id);
    // A canceled vehicle finishes its current track (see tick()).
    if (auto* actor=find_unit(world,id)) actor->locomotor_moving=m!=motion_.end() && m->second.stepping;
}
PrototypeMoveResult PrototypeMovement::tick(World& world,std::uint32_t frame) {
    loop::profile::Scope probe(loop::profile::Phase::Movement);
    if (world.setup_progress!=session::world::SetupProgress::PrototypeComplete)
        return fail(PrototypeMoveStatus::NeedsInput,"prototype Battle setup is unfinished");
    if (last_frame_) {
        const auto elapsed=std::uint32_t(frame-*last_frame_);
        if (!elapsed || elapsed>0x7fffffffu) return fail(PrototypeMoveStatus::Invalid,"prototype frame is repeated or in the past");
    }
    const bool stepping=std::any_of(motion_.begin(),motion_.end(),[](const auto& m){return m.second.stepping;});
    if (moves_.empty() && !stepping) {
        last_frame_=frame;
        return {PrototypeMoveStatus::Complete,{},{},{},{},{}};
    }
    // Edit the world in place. Keep the prior Entities and each Cell before
    // its first change, and restore them if any step fails. This replaces
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
    auto& candidate=world;auto moves=moves_;auto motions=motion_;PrototypeMoveResult result;result.status=PrototypeMoveStatus::Complete;
    Movers movers;
    for (const auto& item:moves) movers.insert(item.first);
    // Cells claimed by stepping vehicles, both ends, until the step ends.
    std::map<std::uint32_t,std::uint32_t> claims;
    for (const auto& [owner,m]:motions) if (m.stepping) { claims[key(m.from)]=owner;claims[key(m.to)]=owner; }
    const auto release=[&](std::uint32_t id) {
        for (auto it=claims.begin();it!=claims.end();) it=it->second==id?claims.erase(it):std::next(it);
    };
    Victims victims;
    for (const auto& e:candidate.entities)
        if (e.kind==session::world::Kind::Infantry && e.lifetime==session::world::Lifetime::Placed && e.next &&
            e.resolved_hp.value_or(0)>0 && profile(e).crushable) victims[e.id]={e.house_id,*e.next};
    std::vector<std::uint32_t> order;
    for (const auto& item:moves) order.push_back(item.first);
    for (const auto& item:motions) if (item.second.stepping && !moves.count(item.first)) order.push_back(item.first);
    std::sort(order.begin(),order.end());
    const auto finish=[&](std::uint32_t id,session::world::Entity* actor) {
        actor->locomotor_moving=false;movers.erase(id);result.arrived.push_back(id);moves.erase(id);
    };
    // 00705160 with a false second argument: kill each enemy crushable
    // object in the entered Cell, play its CrushSound, and remove it.
    // TODO-ORIGINAL: sound, score and the 004A2670 scatter warning.
    const auto crush=[&](const session::world::Entity& crusher,ra2map::Cell at)->std::optional<std::string> {
        auto found=candidate.cells.find(key(at));
        if (found==candidate.cells.end() || !found->second.heads) return {};
        const auto head=found->second.heads->ground_head;
        const auto victim=victims.find(head);
        if (!head || victim==victims.end() || victim->second.first==crusher.house_id || victim->second.second!=0) return {};
        journal.cell(key(at));
        auto* dead=find_unit(candidate,head);
        if (!dead || !dead->next || *dead->next!=0) return "prototype crushed Object link is unresolved";
        found->second.heads->ground_head=0;*found->second.ground_occupancy_124&=~0x20u;
        dead->next=0;dead->resolved_hp=0;dead->active=false;dead->in_limbo=true;dead->target_id=0;
        dead->locomotor_moving=false;dead->lifetime=session::world::Lifetime::Destroyed;
        victims.erase(victim);moves.erase(head);motions.erase(head);movers.erase(head);release(head);
        result.crushed.push_back(head);
        return {};
    };
    for (const auto id:order) {
        // A crushed Unit has left the order.
        auto* movement=moves.count(id)?&moves.at(id):nullptr;
        if (!movement && !motions.count(id)) continue;
        auto* actor=find_unit(candidate,id);
        if (!actor || actor->lifetime==session::world::Lifetime::Destroyed || (actor->resolved_hp && *actor->resolved_hp<=0)) {
            if (actor) actor->locomotor_moving=false;
            movers.erase(id);moves.erase(id);motions.erase(id);release(id);continue;
        }
        if (const auto error=unit_error(actor)) return fail(PrototypeMoveStatus::NeedsInput,*error);
        const auto source=candidate.cells.find(key(*actor->cell));
        if (source==candidate.cells.end() || !source->second.heads || !source->second.ground_occupancy_124 ||
            !source->second.cell_flag_100) return fail(PrototypeMoveStatus::NeedsInput,"prototype source Cell is unresolved");
        if (*source->second.cell_flag_100 || source->second.heads->ground_head!=id || *actor->next!=0 ||
            !(*source->second.ground_occupancy_124&0x20u)) return fail(PrototypeMoveStatus::Blocked,"prototype source Unit is not isolated on ground");
        const auto self=profile(*actor);
        const Context ctx{&movers,&claims,self.crusher?&victims:nullptr,actor->house_id,rules_?&self.ground:nullptr};
        Context strict=ctx;strict.movers=nullptr;
        const int wait_limit=globals_.blockage_path_delay;
        const auto at_center=[&] {
            return actor->position->x==int(actor->cell->x)*256+128 && actor->position->y==int(actor->cell->y)*256+128;
        };
        // Wait behind a Unit that still moves. Do not plan a detour around
        // it, as that sends a group sideways and back. Plan again when the
        // blocker stops, or after BlockagePathDelay frames (004A3600 sets
        // Foot +5A1 and starts its timer from Rules +13D0).
        const auto wait_for=[&](ra2map::Cell cell) {
            if (!movement || movement->waited>=wait_limit) return false;
            const auto claim=claims.find(key(cell));
            if (claim!=claims.end() && claim->second!=id) return true;
            const auto found=candidate.cells.find(key(cell));
            if (found==candidate.cells.end() || !found->second.heads) return false;
            const auto blocker=moves.find(found->second.heads->ground_head);
            return blocker!=moves.end() && blocker->first!=id && !blocker->second.stalled;
        };
        // A moving Unit in a corner Cell of a diagonal step, if any.
        const auto busy_corner=[&](ra2map::Cell from,ra2map::Cell to)->std::optional<ra2map::Cell> {
            if (from.x==to.x || from.y==to.y) return {};
            for (const ra2map::Cell corner:{ra2map::Cell{to.x,from.y},ra2map::Cell{from.x,to.y}})
                if (pass_cell(candidate,corner,id,&ctx)==Pass::Busy) return corner;
            return {};
        };
        // Distance to the move goal, as 004A2670 measures it for CloseEnough.
        const auto goal_distance=[&] {
            const auto goal=center(candidate,movement->goal);
            const double dx=double(goal.x)-actor->position->x,dy=double(goal.y)-actor->position->y,dz=double(goal.z)-actor->position->z;
            return static_cast<int>(std::sqrt(dx*dx+dy*dy+dz*dz));
        };
        // Choose the next Cell. Return the Cell, or nothing with one of:
        // hold (waited), stop (finished or blocked; handled here).
        enum class Next { Step,Hold,Done,Blocked,Fail,Turn };
        PrototypeMoveResult failure;
        // face(target) returns true while the vehicle still turns to target.
        // 004A2670 checks the facing before it asks Can_Enter_Cell.
        const auto choose=[&](ra2map::Cell& target,const std::function<bool(ra2map::Cell)>& face)->Next {
            if (!movement) return Next::Done;
            if (!movement->path.empty() && same(movement->path.front(),*actor->cell)) movement->path.erase(movement->path.begin());
            const bool adjacent=movement->path.empty() ||
                std::max(std::abs(int(movement->path.front().x)-int(actor->cell->x)),std::abs(int(movement->path.front().y)-int(actor->cell->y)))==1;
            if ((movement->path.empty() && !same(*actor->cell,movement->goal)) || !adjacent) {
                // The route ended on a nearby Cell because the goal was held
                // when it was planned, or a new order began part way into a
                // step. Plan once more from the current Cell.
                const auto revised=route_or_nearby(candidate,id,*actor->cell,movement->goal,&ctx);
                if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) { failure=fail(revised.status,revised.detail);return Next::Fail; }
                if (revised.status==PrototypeMoveStatus::Complete) movement->path=revised.path;
                else if (!adjacent) movement->path.clear();
            }
            if (movement->path.empty()) return Next::Done;
            target=movement->path.front();
            if (face(target)) return Next::Turn;
            auto available=pass_cell(candidate,target,id,&ctx);
            if (available==Pass::Unresolved) { failure=fail(PrototypeMoveStatus::NeedsInput,"prototype next Cell is unresolved");return Next::Fail; }
            if (available==Pass::Busy && wait_for(target)) return Next::Hold;
            if (available!=Pass::Open) {
                // 004A2670: a blocked vehicle less than CloseEnough from its
                // destination ends the move where it is.
                if (goal_distance()<globals_.close_enough) { movement->path.clear();return Next::Done; }
                // A wall or a stopped Unit: plan around it, through moving
                // Units. A Unit that blocks too long becomes a wall too.
                const bool hard=available==Pass::Busy;
                const auto revised=route_or_nearby(candidate,id,*actor->cell,movement->goal,hard?&strict:&ctx);
                if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) { failure=fail(revised.status,revised.detail);return Next::Fail; }
                if (revised.status==PrototypeMoveStatus::Blocked) return Next::Blocked;
                movement->path=revised.path;
                if (movement->path.empty()) return Next::Done;
                target=movement->path.front();
                if (pass_cell(candidate,target,id,&strict)!=Pass::Open) return Next::Hold;
                if (face(target)) return Next::Turn;
            }
            if (const auto corner=busy_corner(*actor->cell,target)) if (wait_for(*corner)) return Next::Hold;
            return Next::Step;
        };
        const auto hold=[&] {
            if (movement) { ++movement->waited;movement->stalled=true; }
            actor->locomotor_moving=false;
        };
        const auto block=[&] {
            actor->locomotor_moving=false;movement->stalled=true;result.blocked.push_back(id);
        };
        if (self.drive) {
            auto& m=motions[id];
            if (movement) m.speed_value=movement->speed;
            const bool rolling=m.stepping; // A track was active at the frame start.
            bool moved=false,stopped=false;
            // 004A2670 for a clear Cell: the speed fraction for the new
            // Cell. Ground[land].Cost[SpeedType] (at most one), the slope
            // factors for a Unit, 0.5 for zero, then 0.75 at or below
            // ConditionYellow.
            const auto target_speed=[&](ra2map::Cell target) {
                const auto& cell=candidate.cells.at(key(target));
                const int land=cell.terrain->land_type && *cell.terrain->land_type>=0 && *cell.terrain->land_type<12?*cell.terrain->land_type:0;
                float ground=self.ground[std::size_t(land)];
                double value=ground>1.0f?1.0:double(ground);
                const int here=*candidate.cells.at(key(*actor->cell)).terrain->floor_world_height,there=*cell.terrain->floor_world_height;
                const bool tracked=self.speed_type==1;
                if (here<there) value*=tracked?globals_.tracked_uphill:globals_.wheeled_uphill;
                else if (there<here) value*=tracked?globals_.tracked_downhill:globals_.wheeled_downhill;
                if (value==0.0) value=0.5;
                const int maximum=actor->maximum_hp.value_or(self.strength);
                if (maximum>0 && actor->resolved_hp && double(*actor->resolved_hp)/double(maximum)<=globals_.condition_yellow) value*=0.75;
                return value;
            };
            // Start one step: the straight track of 24 samples, or the
            // diagonal track of 32. The terminal adds the 004B1FD0
            // correction: +5 for straight (last sample 3 from the anchor),
            // -3 for diagonal (last sample 16 from the anchor).
            const auto begin=[&](ra2map::Cell target) {
                m.stepping=true;m.from=*actor->cell;m.to=target;m.origin=*actor->position;m.end=center(candidate,target);m.sample=0;
                if (same(m.from,m.to)) {
                    const int far=std::max(std::abs(m.end.x-m.origin.x),std::abs(m.end.y-m.origin.y));
                    m.samples=std::max(1,(24*far+255)/256);m.correction=0;
                } else {
                    const bool diagonal=m.from.x!=m.to.x && m.from.y!=m.to.y;
                    m.samples=diagonal?32:24;m.correction=diagonal?-3:5;
                    m.target=target_speed(target);
                }
                claims[key(m.from)]=id;claims[key(m.to)]=id;
            };
            // Prepare the next track. Return false when the vehicle must
            // wait this frame (turning, waiting or done).
            const auto prepare=[&](bool chained)->std::optional<bool> {
                if (!at_center()) { begin(*actor->cell);return true; } // Return to the Cell center first.
                // 004A2670 (004A3530..): a vehicle not facing the first
                // direction exactly sets its desired facing and waits.
                // Facing turns at ROT*256 of 65536 a frame (004B8E90).
                // TODO-ORIGINAL: turns of 45 or 90 degrees while rolling
                // use the curve tracks; here they keep rolling.
                const auto face=[&](ra2map::Cell target) {
                    const auto desired=static_cast<std::uint16_t>(direction_of(*actor->cell,target)<<13);
                    const auto current=facing_raw(*actor);
                    const int difference=static_cast<std::int16_t>(static_cast<std::uint16_t>(desired-current));
                    if (self.rot<=0 || !difference || (chained && std::abs(difference)<=0x4000)) return false;
                    if (chained) return true;
                    const int rate=std::min(self.rot,127)<<8;
                    set_facing(*actor,std::abs(difference)<=rate?desired:static_cast<std::uint16_t>(current+(difference<0?-rate:rate)));
                    actor->locomotor_moving=true;moved=true;
                    if (movement) movement->waited=0,movement->stalled=false;
                    return true;
                };
                ra2map::Cell target{};
                const auto next=choose(target,face);
                if (next==Next::Fail) return std::nullopt;
                if (next==Next::Turn) return false;
                if (next==Next::Hold) { if (!chained) hold();stopped=true;return false; }
                if (next==Next::Blocked) { block();stopped=true;return false; }
                if (next==Next::Done) {
                    if (movement) finish(id,actor),movement=nullptr;
                    stopped=true;return false;
                }
                set_facing(*actor,static_cast<std::uint16_t>(direction_of(*actor->cell,target)<<13));
                begin(target);
                return true;
            };
            if (!m.stepping) {
                const auto ready=prepare(false);
                if (!ready) return failure;
            }
            if (m.stepping) {
                // 004A1000: the speed fraction for this frame.
                if (!self.accelerates) m.speed=set_speed(m.target);
                else {
                    const auto goal=movement?center(candidate,movement->goal):m.end;
                    const double dx=double(actor->position->x)-goal.x,dy=double(actor->position->y)-goal.y,dz=double(actor->position->z)-goal.z;
                    const int distance=static_cast<int>(std::sqrt(dx*dx+dy*dy+dz*dz));
                    double value=m.speed;
                    if (distance<self.slowdown_distance) {
                        value=m.speed-double(m.speed_value)*self.deceleration;
                        if (value<0.30000001192092896) value=0.30000001192092896;
                    } else if (m.speed<m.target) value=std::min(self.acceleration+m.speed,m.target);
                    else if (m.target<m.speed) value=std::max(m.speed-double(m.speed_value)*self.deceleration,m.target);
                    m.speed=set_speed(value);
                }
                // 004C9B10: Speed * country (one) * Foot +470 (one), then
                // times the speed fraction, each truncated.
                int budget=m.remainder+static_cast<int>(double(m.speed_value)*m.speed);
                bool chained=false,crossing_blocked=false;
                const auto place=[&](int sample)->std::optional<bool> {
                    Coord next=m.end;
                    if (sample<m.samples) {
                        const auto part=[&](int a,int b){return a+static_cast<int>(std::int64_t(b-a)*sample/m.samples);};
                        next={part(m.origin.x,m.end.x),part(m.origin.y,m.end.y),part(m.origin.z,m.end.z)};
                    }
                    ra2map::Cell entered{static_cast<std::uint16_t>(next.x/256),static_cast<std::uint16_t>(next.y/256)};
                    if (!same(entered,m.from) && !same(entered,m.to)) {
                        // The diagonal midpoint lies on the Cell corner. Keep
                        // it out of a third Cell (004B22CC..004B23F5).
                        next.x=std::clamp(next.x,int(m.to.x)*256,int(m.to.x)*256+255);
                        next.y=std::clamp(next.y,int(m.to.y)*256,int(m.to.y)*256+255);
                        entered=m.to;
                    }
                    if (!same(entered,*actor->cell)) {
                        if (self.crusher) if (const auto error=crush(*actor,entered)) { failure=fail(PrototypeMoveStatus::NeedsInput,*error);return std::nullopt; }
                        if (pass_cell(candidate,entered,id,&ctx)!=Pass::Open || !same(entered,m.to)) return false;
                        journal.cell(key(*actor->cell));journal.cell(key(entered));
                        if (const auto error=transfer(candidate,id,*actor->cell,entered)) { failure=fail(PrototypeMoveStatus::NeedsInput,*error);return std::nullopt; }
                        actor=find_unit(candidate,id);actor->cell=entered;
                    }
                    actor->position=next;actor->collision_virtual_position=next;moved=true;
                    return true;
                };
                while (m.stepping && budget>7) {
                    budget-=7;
                    if (m.sample==m.samples-1) {
                        // 004B0F20 terminal: place at the anchor, end the track.
                        const auto placed=place(m.samples);
                        if (!placed) return failure;
                        if (!*placed) { crossing_blocked=true;break; }
                        m.stepping=false;release(id);budget+=m.correction;
                        // 004B0500: one more preparation and Follow in this
                        // frame, without new speed, if a track was active.
                        if (!rolling || chained) break;
                        chained=true;
                        const auto ready=prepare(true);
                        if (!ready) return failure;
                        if (!*ready) break;
                        continue;
                    }
                    const auto placed=place(m.sample+1);
                    if (!placed) return failure;
                    if (!*placed) { crossing_blocked=true;break; }
                    ++m.sample;
                }
                if (crossing_blocked) budget=0;
                m.remainder=m.stepping?budget:budget;
                // Follow: move part of the way to the next sample with the
                // remaining budget, inside the current Cell (004B22CC).
                if (m.stepping && budget>0 && m.sample<m.samples-1) {
                    const auto part=[&](int a,int b,int k){return a+static_cast<int>(std::int64_t(b-a)*k/m.samples);};
                    const Coord here=*actor->position;
                    const Coord next{part(m.origin.x,m.end.x,m.sample+1),part(m.origin.y,m.end.y,m.sample+1),part(m.origin.z,m.end.z,m.sample+1)};
                    const float fraction=static_cast<float>(budget/7.0);
                    Coord proposed=here;
                    proposed.x+=static_cast<int>((next.x-here.x)*double(fraction));
                    proposed.y+=static_cast<int>((next.y-here.y)*double(fraction));
                    if (proposed.x/256==here.x/256 && proposed.y/256==here.y/256) { actor->position=proposed;actor->collision_virtual_position=proposed; }
                }
                if (!m.stepping && movement && movement->path.empty() && same(*actor->cell,movement->goal) && at_center()) finish(id,actor),movement=nullptr,stopped=true;
            }
            if (!m.stepping) m.remainder=0;
            if (moved) result.moved.push_back(id);
            if (!movement && !m.stepping) actor->locomotor_moving=false;
            else if (moved && !stopped) {
                actor->locomotor_moving=true;
                if (movement) { movement->waited=0;movement->stalled=false; }
            }
            continue;
        }
        if (!movement) continue;
        if (movement->path.empty() && !same(*actor->cell,movement->goal)) {
            const auto revised=route_or_nearby(candidate,id,*actor->cell,movement->goal,&ctx);
            if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) return fail(revised.status,revised.detail);
            if (revised.status==PrototypeMoveStatus::Complete) movement->path=revised.path;
        }
        // A blocked goal can resolve to the current Cell after entry but
        // before its center. Finish the step before releasing the route.
        if (movement->path.empty() && !at_center()) movement->path.push_back(*actor->cell);
        if (movement->path.empty()) { finish(id,actor);continue; }
        auto target=movement->path.front();auto available=pass_cell(candidate,target,id,&ctx);
        if (available==Pass::Unresolved) return fail(PrototypeMoveStatus::NeedsInput,"prototype next Cell is unresolved");
        if (available==Pass::Busy && wait_for(target)) { hold();continue; }
        if (available!=Pass::Open) {
            if (goal_distance()<globals_.close_enough && at_center()) { finish(id,actor);continue; }
            const bool hard=available==Pass::Busy;
            const auto revised=route_or_nearby(candidate,id,*actor->cell,movement->goal,hard?&strict:&ctx);
            if (revised.status==PrototypeMoveStatus::NeedsInput || revised.status==PrototypeMoveStatus::Invalid) return fail(revised.status,revised.detail);
            if (revised.status==PrototypeMoveStatus::Blocked) { block();continue; }
            movement->path=revised.path;
            if (movement->path.empty()) { finish(id,actor);continue; }
            target=movement->path.front();
            if (pass_cell(candidate,target,id,&strict)!=Pass::Open) { hold();continue; }
        }
        if (const auto corner=busy_corner(*actor->cell,target)) {
            if (wait_for(*corner)) { hold();continue; }
        }
        // Other Units move speed world units this frame. Units left at a
        // Cell center carry on toward the next Cell in the same frame.
        // TODO-ORIGINAL: the walk locomotor and its subcells.
        int budget=movement->speed;bool moved=false,blocked=false;
        while (budget>0 && !movement->path.empty()) {
            target=movement->path.front();
            if (moved && (pass_cell(candidate,target,id,&strict)!=Pass::Open || busy_corner(*actor->cell,target))) break;
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
                const auto step=pass_cell(candidate,entered,id,&ctx);
                if (step==Pass::Unresolved) return fail(PrototypeMoveStatus::NeedsInput,"prototype crossed Cell is unresolved");
                if (step!=Pass::Open && !same(entered,target)) {
                    // A Unit stopped off its Cell center cuts across a Cell
                    // that is not on its route. Go back to its own center
                    // first; that point stays inside its own Cell.
                    movement->path.insert(movement->path.begin(),*actor->cell);
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
            const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
            for (unsigned d=0;d<8;++d) if (directions[d][0]==sx && directions[d][1]==sy) {
                actor->hull_facing=static_cast<int>(d*32);actor->hull_facing_raw=static_cast<std::uint16_t>(d*8192);break;
            }
            actor->position=next;actor->collision_virtual_position=next;moved=true;budget-=static_cast<int>(used);
            if (next.x==desired.x && next.y==desired.y && next.z==desired.z) movement->path.erase(movement->path.begin());
            else break;
        }
        if (blocked) { block();continue; }
        if (!moved) { hold();continue; }
        movement->waited=0;movement->stalled=false;actor->locomotor_moving=true;result.moved.push_back(id);
        if (movement->path.empty() && same(*actor->cell,movement->goal)) finish(id,actor);
    }
    journal.keep=true;moves_=std::move(moves);motion_=std::move(motions);last_frame_=frame;return result;
}
}
