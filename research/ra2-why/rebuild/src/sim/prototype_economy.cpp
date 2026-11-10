#include <cstdlib>
#include <iostream>
#include "prototype_economy.h"
#include "drive.h"
#include "../loop/profile.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
namespace ra2::sim {
namespace {
namespace sw=session::world;
std::uint32_t key(ra2map::Cell c) { return c.y*512u+c.x; }
ra2map::Cell coord(std::uint32_t k) { return {std::uint16_t(k%512),std::uint16_t(k/512)}; }
int distance(ra2map::Cell a,ra2map::Cell b) { int x=int(a.x)-b.x,y=int(a.y)-b.y;return x*x+y*y; }
bool alive(const sw::Entity& e) { return e.lifetime==sw::Lifetime::Placed && e.resolved_hp.value_or(0)>0 && e.cell; }
bool open(const sw::CellState& c) {
    return c.allocated.value_or(false) && c.heads && !c.heads->ground_head &&
        c.ground_occupancy_124==0 && c.cell_flag_100==false && c.terrain &&
        c.terrain->floor_world_height && c.unit_land_speed.value_or(1)>0;
}
// Ground that only live Infantry of one House stands on. A miner of that
// House may take it as its goal: its drive asks the soldiers to step aside
// (004A2670 calls Cell Scatter_Content 00476920 on the blocked Cell).
bool held_by_friends(const sw::Snapshot& w,const sw::CellState& c,std::uint32_t house) {
    if(!c.allocated.value_or(false) || !c.heads || !c.heads->ground_head || c.cell_flag_100!=false ||
       !c.terrain || !c.terrain->floor_world_height || c.unit_land_speed.value_or(1)<=0) return false;
    int guard=0;
    for(auto id=c.heads->ground_head;id;) {
        const auto e=std::find_if(w.entities.begin(),w.entities.end(),[&](const auto& item){return item.id==id;});
        if(e==w.entities.end() || !alive(*e) || e->kind!=sw::Kind::Infantry || e->house_id!=house || !e->next || ++guard>8) return false;
        id=*e->next;
    }
    return true;
}
}
PrototypeEconomy::PrototypeEconomy(const rules::IniFile& r):rules_(r) {
    movement_.use_rules(rules_);
    purifier_bonus_=r.read_double("General","PurifierBonus",0.25);
    stall_frames_=5*std::max(1,r.read_integer("General","BlockagePathDelay",60));
    const auto* overlays=r.find_section("OverlayTypes");
    const auto* types=r.find_section("Tiberiums");
    if(!overlays || !types) return;
    // Registry positions, not numeric INI keys, are the live OverlayType indexes.
    // TODO-ORIGINAL TiberiumTypeClass::Read_INI: extend Image families beyond the four base resource images.
    for(const auto& type:types->entries) {
        int image=r.read_integer(type.value,"Image",1);
        const char* prefix=image==1?"TIB":image==2?"GEM":image==3?"TIB2_":image==4?"TIB3_":nullptr;
        if(!prefix) continue;
        Resource resource{type.value,-1};
        int resource_index=int(resources_.size());
        for(std::size_t i=0;i<overlays->entries.size();++i) {
            const auto& name=overlays->entries[i].value;
            std::string stem=prefix;
            if(name.size()!=stem.size()+2 || name.compare(0,stem.size(),stem)!=0 ||
               name[stem.size()]<'0' || name[stem.size()]>'9' || name.back()<'0' || name.back()>'9' ||
               !r.read_bool(name,"Tiberium",false)) continue;
            if(resource.first_overlay<0) resource.first_overlay=int(i);
            if(overlay_resources_.size()<=i) overlay_resources_.resize(i+1,-1);
            if(overlay_resources_[i]<0) overlay_resources_[i]=resource_index;
        }
        resource.growth=std::max(1,r.read_integer(resource.section,"Growth",2200));
        resource.interval=std::max(1,r.read_integer(resource.section,"Spread",2200));
        resource.value=std::max(0,r.read_integer(resource.section,"Value",0));
        resources_.push_back(std::move(resource));
    }
    grows_=r.read_bool("General","TiberiumGrows",true);
    spreads_=r.read_bool("General","TiberiumSpreads",true);
    // 00640B80 defaults: HarvesterLoadRate=2 frames per animation step and
    // HarvesterDumpRate=.016 minutes. 00647F40 reads both from [General].
    // 007025A0 harvests when Animation.Value reaches 9. 00701580 unloads when
    // Animation.Value (one step per frame) is no longer below rate * 900.
    load_frames_=9*std::max(1,r.read_integer("General","HarvesterLoadRate",2));
    const long double dump=static_cast<long double>(r.read_double("General","HarvesterDumpRate",0.016))*900.0L;
    dump_frames_=dump<1?1:dump>1e6L?1000000:int(std::floor(dump))+(dump==std::floor(dump)?0:1);
}
void PrototypeEconomy::set_virtual_purifiers(std::uint32_t house,int count) {
    virtual_purifiers_[house]=std::max(0,count);
}
int PrototypeEconomy::virtual_purifiers(const rules::IniFile& rules,int original_difficulty) {
    const auto list=rules.read_list("General","AIVirtualPurifiers");
    if(original_difficulty<0 || std::size_t(original_difficulty)>=list.size()) return 0;
    return std::max(0,rules::IniFile::integer_value(list[std::size_t(original_difficulty)]).value_or(0));
}
std::int32_t PrototypeEconomy::start_money_bonus(const rules::IniFile& rules,int original_difficulty,
                                                std::int32_t available_money) {
    const auto list=rules.read_list("General","MultiplayerAICM");
    if(original_difficulty<0 || std::size_t(original_difficulty)>=list.size()) return 0;
    const auto percent=rules::IniFile::integer_value(list[std::size_t(original_difficulty)]).value_or(0);
    // fild percent; fmul 0.01 (0079C518); fimul money; __ftol (0077F110) cuts toward zero.
    return static_cast<std::int32_t>(static_cast<double>(percent)*0.01*static_cast<double>(available_money));
}
// Base 00701580: purifiers = House +52CC (Ore Purifier buildings); zero when
// the power percentage is below one; plus the virtual purifiers of a
// computer house. Extra = purifiers * PurifierBonus * load.
// TODO-ORIGINAL: the original multiplies the bail amount as a float before
// GiveTiberium; here the credit value is cut to a whole number.
std::int64_t PrototypeEconomy::purifier_credits(const session::world::Snapshot& w,std::uint32_t house,
                                                std::int64_t credits) const {
    int purifiers=0;
    for(const auto& e:w.entities)
        if(e.house_id==house && e.kind==session::world::Kind::Structure &&
           e.lifetime==session::world::Lifetime::Placed && e.resolved_hp.value_or(1)>0 &&
           rules_.read_bool(e.type,"OrePurifier",false)) ++purifiers;
    for(const auto& h:w.houses) if(h.id==house && h.power_output && h.power_drain && *h.power_output<*h.power_drain) purifiers=0;
    if(const auto found=virtual_purifiers_.find(house);found!=virtual_purifiers_.end()) purifiers+=found->second;
    if(purifiers<=0 || credits<=0) return 0;
    return std::int64_t(double(purifiers)*purifier_bonus_*double(credits));
}
int PrototypeEconomy::load(std::uint32_t id) const { auto i=cargo_.find(id);return i==cargo_.end()?0:i->second.total; }
int PrototypeEconomy::resource(const sw::CellState& c) const {
    if (!c.terrain || !c.overlay_data_11e) return -1;
    int index=c.terrain->overlay_type_index.value_or(-1);
    return index<0 || std::size_t(index)>=overlay_resources_.size()?-1:overlay_resources_[std::size_t(index)];
}
void PrototypeEconomy::tick(sw::Snapshot& w,std::uint32_t frame) {
    loop::profile::Scope probe(loop::profile::Phase::Economy);
    if(w.setup_progress!=sw::SetupProgress::PrototypeComplete || w.pending_cell_setup || (last_ && frame<=*last_)) return;
    last_=frame;
    movement_.tick(w,frame);
    // TODO-ORIGINAL TiberiumClass::RegisterForSpread: deterministic clocks replace the growth queues and percentages.
    std::vector<std::pair<std::uint32_t,int>> spread;
    // The Cell pass changes nothing unless some resource grows or spreads on
    // this frame. Rules are fixed for the match, so skip the pass otherwise.
    bool active=false;
    if(frame) for(const auto& item:resources_)
        if((grows_ && frame%item.growth==0) || (spreads_ && frame%item.interval==0)) active=true;
    // Full Cell passes walk the map with prefetch; see session::world::walk_cells.
    thread_local std::vector<std::uintptr_t> grow_ahead,field_ahead;
    if(active) sw::walk_cells(w.cells,grow_ahead,[&](auto& item) {
        const auto k=item.first;auto& c=item.second;
        int r=resource(c);if(r<0) return true;
        const int growth=resources_[r].growth,interval=resources_[r].interval;
        if(frame && frame%growth==0 && grows_ && *c.overlay_data_11e<11) ++*c.overlay_data_11e;
        if(!frame || frame%interval || !spreads_ || *c.overlay_data_11e<11) return true;
        auto p=coord(k);
        for(auto [dx,dy]:{std::pair<int,int>{0,-1},{1,0},{0,1},{-1,0}}) {
            int x=p.x+dx,y=p.y+dy;if(x<0 || y<0 || x>=512 || y>=512) continue;
            auto i=w.cells.find(y*512u+x);
            if(i!=w.cells.end() && open(i->second) && i->second.terrain->overlay_type_index==-1 && i->second.terrain->ramp_index==0) {
                spread.push_back({i->first,r});break;
            }
        }
        return true;
    });
    for(auto [k,r]:spread) {auto& c=w.cells.at(k);if(c.terrain->overlay_type_index!=-1) continue;c.terrain->overlay_type_index=resources_[r].first_overlay;c.overlay_data_11e=0;c.terrain->land_type=5;}
    std::set<std::uint32_t> live;
    std::vector<sw::Entity*> miners;
    for(auto& e:w.entities) if(alive(e) && e.kind==sw::Kind::Unit && (e.type=="CMIN" || e.type=="HARV")) miners.push_back(&e);
    std::sort(miners.begin(),miners.end(),[](auto a,auto b){return a->id<b->id;});
    // QA trace, only with RA2_MINER_DEBUG set: each miner every 600 frames.
    static const bool trace=std::getenv("RA2_MINER_DEBUG")!=nullptr;
    if(trace && frame%600==0) for(auto* miner:miners) {
        const auto& bag=cargo_[miner->id];
        std::cerr<<"MINER frame="<<frame<<" id="<<miner->id<<" house="<<miner->house_id<<" cell="<<miner->cell->x<<','<<miner->cell->y
                 <<" load="<<bag.total<<" returning="<<bag.returning<<" docked="<<bag.docked<<" loading="<<bag.loading
                 <<" moving="<<movement_.moving(miner->id)<<" next="<<bag.next;
        if(bag.target) std::cerr<<" target="<<bag.target->x<<','<<bag.target->y;
        std::cerr<<'\n';
    }
    if(trace && frame%6000==0 && !miners.empty()) {
        std::cerr<<"STRUCTS frame="<<frame;
        for(const auto& b:w.entities) if(alive(b) && b.kind==sw::Kind::Structure)
            std::cerr<<' '<<b.house_id<<':'<<b.type<<'@'<<b.cell->x<<','<<b.cell->y;
        std::cerr<<'\n';
    }
    for(auto* miner:miners) {
        auto& e=*miner;live.insert(e.id);auto& bag=cargo_[e.id];
        // Ore can grow, or another miner can leave it, after this route
        // starts. Harvest usable ore on the way instead of passing it.
        const auto under=w.cells.find(key(*e.cell));
        if(movement_.moving(e.id) && !bag.returning && under!=w.cells.end() && resource(under->second)>=0)
            movement_.cancel(w,e.id);
        if(movement_.moving(e.id) && !bag.returning) {
            // Reconsider an adjacent field even when it is beside the route.
            std::optional<ra2map::Cell> nearby;int best_distance=3;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const int x=int(e.cell->x)+dx,y=int(e.cell->y)+dy;
                if(x<0 || y<0 || x>=512 || y>=512) continue;
                const ra2map::Cell cell{std::uint16_t(x),std::uint16_t(y)};
                const auto at=w.cells.find(key(cell));const int d=dx*dx+dy*dy;
                if(d<best_distance && at!=w.cells.end() && open(at->second) && resource(at->second)>=0) {
                    nearby=cell;best_distance=d;
                }
            }
            if(nearby && movement_.issue(w,e.id,*nearby,original_rule_speed(rules_.read_integer(e.type,"Speed",4))).status==PrototypeMoveStatus::Complete) bag.target=nearby;
        }
        if(movement_.moving(e.id) && !bag.returning && bag.target && frame%15==0) {
            // Ore can become available beyond the adjacent ring. Keep the route
            // only while its destination remains the nearest reachable resource.
            std::vector<std::uint32_t> closer;
            const int limit=distance(*e.cell,*bag.target);
            sw::walk_cells(std::as_const(w.cells),field_ahead,[&](const auto& item) {
                if(resource(item.second)>=0 && open(item.second) &&
                   distance(*e.cell,coord(item.first))<limit) closer.push_back(item.first);
                return true;
            });
            std::sort(closer.begin(),closer.end(),[&](auto a,auto b) {
                const int da=distance(*e.cell,coord(a)),db=distance(*e.cell,coord(b));
                return da!=db?da<db:a<b;
            });
            for(auto k:closer) {
                const auto target=coord(k);
                if(movement_.issue(w,e.id,target,original_rule_speed(rules_.read_integer(e.type,"Speed",4))).status==PrototypeMoveStatus::Complete) {
                    bag.target=target;break;
                }
            }
        }
        if(movement_.moving(e.id)) {
            bag.loading=false;
            if(!bag.progress || bag.progress->x!=e.cell->x || bag.progress->y!=e.cell->y) { bag.progress=*e.cell;bag.progress_frame=frame; }
            else if(frame-bag.progress_frame>=std::uint32_t(stall_frames_)) {
                // MP02T2, Brutal computer: a miner with 36 bails waited one
                // cell from its next ore cell from frame 22657 to the end.
                movement_.cancel(w,e.id);
                bag.progress.reset();
                if(bag.total>0) bag.returning=true; else bag.avoid=bag.target;
                bag.next=frame+1;
            }
            continue;
        }
        bag.progress.reset();
        if(frame<bag.next) continue;
        sw::Entity* refinery=nullptr;int best=std::numeric_limits<int>::max();
        for(auto& b:w.entities) if(alive(b) && b.kind==sw::Kind::Structure && b.house_id==e.house_id && (b.type=="GAREFN" || b.type=="NAREFN")) {
            // TODO-ORIGINAL UnitClass::Mission_Harvest docks at the refinery dock; here any cell beside the footprint unloads.
            int fw=1,fh=1;
            if(auto f=rules_.read_string(b.type,"Foundation");f.find('x')!=std::string::npos) {
                fw=std::max(1,atoi(f.substr(0,f.find('x')).c_str()));
                fh=std::max(1,atoi(f.substr(f.find('x')+1).c_str()));
            }
            int cx=std::clamp(int(e.cell->x),int(b.cell->x),int(b.cell->x)+fw-1);
            int cy=std::clamp(int(e.cell->y),int(b.cell->y),int(b.cell->y)+fh-1);
            int dx=int(e.cell->x)-cx,dy=int(e.cell->y)-cy;
            int d=dx*dx+dy*dy;
            if(d<best || (d==best && refinery && b.id<refinery->id)) {best=d;refinery=&b;}
        }
        if(!refinery) continue;
        const int capacity=std::max(1,rules_.read_integer(e.type,"Storage",e.type=="CMIN"?20:40));
        auto current=w.cells.find(key(*e.cell));
        const bool on_resource=current!=w.cells.end() && resource(current->second)>=0;
        // 007025A0 case 1 starts the load animation; the first bail waits for it.
        if(!bag.returning && !bag.loading && bag.total<capacity && on_resource) {
            bag.loading=true;bag.next=frame+std::uint32_t(load_frames_);continue;
        }
        if(bag.total>=capacity) bag.returning=true;
        if(bag.returning) {
            bag.loading=false;
            // TODO-ORIGINAL UnitClass::Mission_Harvest: adjacent docking replaces building radio and dock animations.
            int rw=1,rh=1;
            if(auto f=rules_.read_string(refinery->type,"Foundation");f.find('x')!=std::string::npos) {
                rw=std::max(1,atoi(f.substr(0,f.find('x')).c_str()));
                rh=std::max(1,atoi(f.substr(f.find('x')+1).c_str()));
            }
            const int ex=int(e.cell->x),ey=int(e.cell->y);
            const int fx=int(refinery->cell->x),fy=int(refinery->cell->y);
            if(ex>=fx-1&&ex<=fx+rw&&ey>=fy-1&&ey<=fy+rh) {
                // 00701580 state 3: dock, wait, then give all of the first
                // non-empty resource. Repeat until the cargo is empty.
                if(!bag.docked) { bag.docked=true;bag.next=frame+std::uint32_t(dump_frames_);continue; }
                sw::House* house=nullptr;
                for(auto& h:w.houses) if(h.id==e.house_id && h.cash) { house=&h;break; }
                if(!house) continue;
                auto full=std::find_if(bag.bails.begin(),bag.bails.end(),[](int b){return b>0;});
                if(full==bag.bails.end()) { bag={};bag.next=frame+1;continue; }
                const auto base_credits=std::int64_t(*full)*resources_[std::size_t(full-bag.bails.begin())].value;
                // 00701580 gives the load, then the purifier share of the same load.
                const auto credits=base_credits+purifier_credits(w,e.house_id,base_credits);
                *house->cash=int(std::min<std::int64_t>(std::numeric_limits<int>::max(),std::int64_t(*house->cash)+credits));
                bag.total-=*full;*full=0;bag.next=frame+std::uint32_t(dump_frames_);
                continue;
            }
            bag.docked=false;
            // An empty Cell beside the refinery first. If guards stand on
            // every one, a driving miner heads for a Cell that only friendly
            // soldiers hold; they step aside for it. The old code waited for
            // an empty Cell and the full miner never moved (MP02T2, HARV at
            // 87,123 from frame 10039).
            // Ring of cells around the refinery footprint, nearest first.
            std::vector<ra2map::Cell> ring;
            for(int y=fy-1;y<=fy+rh;++y) for(int x=fx-1;x<=fx+rw;++x) {
                bool inside=x>=fx&&x<fx+rw&&y>=fy&&y<fy+rh;
                if(inside||x<0||y<0||x>=512||y>=512) continue;
                ring.push_back(ra2map::Cell{std::uint16_t(x),std::uint16_t(y)});
            }
            std::sort(ring.begin(),ring.end(),[&](auto a,auto b){return distance(*e.cell,a)!=distance(*e.cell,b)?distance(*e.cell,a)<distance(*e.cell,b):a.y!=b.y?a.y<b.y:a.x<b.x;});
            bool done=false;
            for(int pass=0;pass<2&&!done;++pass) for(std::size_t ri=0;ri<ring.size()&&!done;++ri) {
                if(movement_.moving(e.id)) break; // the first issued route stands
                int x=int(ring[ri].x),y=int(ring[ri].y);
                ra2map::Cell target{std::uint16_t(x),std::uint16_t(y)};auto i=w.cells.find(key(target));
                if(i==w.cells.end()) continue;
                if(pass?e.type=="CMIN" || !held_by_friends(w,i->second,e.house_id):!open(i->second)) continue;
                if(e.type=="CMIN") {
                    auto old=w.cells.find(key(*e.cell));
                    if(old==w.cells.end() || !old->second.heads || old->second.heads->ground_head!=e.id || e.next.value_or(1)!=0) continue;
                    old->second.heads->ground_head=0;old->second.ground_occupancy_124=0;
                    i->second.heads->ground_head=e.id;i->second.ground_occupancy_124=32;
                    e.cell=target;e.position=Coord{x*256+128,y*256+128,*i->second.terrain->floor_world_height};
                    bag.next=frame+1;done=true;
                } else movement_.issue(w,e.id,target,original_rule_speed(rules_.read_integer(e.type,"Speed",4)));
            }
            continue;
        }
        if(bag.loading && on_resource) {
            auto& c=current->second;int r=resource(c);
            if(bag.bails.size()<resources_.size()) bag.bails.resize(resources_.size(),0);
            ++bag.bails[std::size_t(r)];++bag.total;
            // 00475DB0 removes the overlay after its last (data + 1) bail.
            // 00472540 restores tile land; scene redraw remains an integration task.
            if(*c.overlay_data_11e==0) {
                c.terrain->overlay_type_index=-1;c.overlay_data_11e=0;
                c.terrain->land_type=c.terrain->tmp_terrain
                    ? std::optional<int>(original_tmp_land_type(*c.terrain->tmp_terrain)) : std::nullopt;
            } else --*c.overlay_data_11e;
            // 007013A0 takes one bail, then restarts the HarvesterLoadRate animation.
            bag.next=frame+std::uint32_t(load_frames_);
            continue;
        }
        bag.loading=false;
        std::vector<std::uint32_t> fields;
        sw::walk_cells(std::as_const(w.cells),field_ahead,[&](const auto& item) {
            if(resource(item.second)>=0 && open(item.second)) fields.push_back(item.first);
            return true;
        });
        std::sort(fields.begin(),fields.end(),[&](auto a,auto b){int da=distance(*e.cell,coord(a)),db=distance(*e.cell,coord(b));return da!=db?da<db:a<b;});
        bool found=false;
        for(auto k:fields) {
            if(bag.avoid && bag.avoid->x==coord(k).x && bag.avoid->y==coord(k).y) continue;
            if(movement_.issue(w,e.id,coord(k),original_rule_speed(rules_.read_integer(e.type,"Speed",4))).status==PrototypeMoveStatus::Complete) {bag.target=coord(k);found=true;break;}
        }
        bag.avoid.reset();
        if(!found && bag.total) bag.returning=true;
    }
    for(auto i=cargo_.begin();i!=cargo_.end();) if(!live.count(i->first)) {movement_.cancel(w,i->first);i=cargo_.erase(i);} else ++i;
}
}
