#include "battle_effects.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::app {
namespace {
std::vector<gfx::RgbaImage> load(const fs::MixFileSystem& files,const char* name,const gfx::Palette& pal) {
    auto bytes=files.read_file(name);
    if(!bytes)throw std::runtime_error(std::string("Missing base combat art: ")+name);
    const auto shp=gfx::ShpImage::parse(std::move(*bytes));
    std::vector<gfx::RgbaImage> result;
    for(std::size_t i=0;i<shp.frames().size();++i)result.push_back(shp.decode_frame(i,pal));
    if(result.empty())throw std::runtime_error("Empty combat animation");
    return result;
}
void pixel(render::Bitmap& b,int x,int y,std::array<std::uint8_t,3> color={255,225,130}) {
    if(x<0||y<0||x>=b.width||y>=b.height)return;
    const auto at=(std::size_t(y)*b.width+x)*4;
    b.rgba[at]=color[0];b.rgba[at+1]=color[1];b.rgba[at+2]=color[2];b.rgba[at+3]=255;
}
void sprite(render::Bitmap& b,const gfx::RgbaImage& s,int x,int y) {
    x-=s.width/2;y-=s.height/2;
    for(int sy=std::max(0,-y);sy<std::min<int>(s.height,b.height-y);++sy)
        for(int sx=std::max(0,-x);sx<std::min<int>(s.width,b.width-x);++sx) {
            const auto from=(std::size_t(sy)*s.width+sx)*4;
            if(s.pixels[from+3])std::copy_n(s.pixels.begin()+from,4,b.rgba.begin()+(std::size_t(y+sy)*b.width+x+sx)*4);
        }
}
}
BattleEffects::BattleEffects(const fs::MixFileSystem& files) {
    const auto bytes=files.read_file("anim.pal");
    if(!bytes)throw std::runtime_error("Missing base anim.pal");
    const auto pal=gfx::Palette::from_bytes(*bytes);
    // Base animation assets. MapClass::SelectDamageAnimation uses AnimList;
    // ref/Phobos-YRpp/MapClass.h documents that original selection boundary.
    // TODO-ORIGINAL: weapon FLH, warhead-specific selection, translucent blits.
    flash_=load(files,"gunfire.shp",pal);
    hit_=load(files,"s_clsn16.shp",pal);
    death_=load(files,"twlt070.shp",pal);
}
std::uint64_t BattleEffects::lifetime(Kind k) const {
    return (k==Tracer||k==Beam||k==Bolt)?6:k==Flash?std::min<std::size_t>(6,flash_.size()*2):
        (k==Hit?hit_.size():death_.size())*2;
}
void BattleEffects::observe(const session::world::Snapshot& world,
    const std::vector<sim::PrototypeCombatProjectile>& shots,std::uint64_t frame,const Project& project,
    const std::vector<sim::PrototypeCombatEvent>* events,const rules::IniFile* rules) {
    if(ready_&&frame<frame_){effects_.clear();seen_.clear();ready_=false;event_frame_.reset();}
    frame_=frame;
    effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[&](const auto& e){
        return frame-e.born>=lifetime(e.kind);}),effects_.end());
    auto add=[&](Kind kind,sim::Coord pos,sim::Coord end) {
        const auto at=project(pos),to=project(end);
        if(at&&to&&effects_.size()<2048)effects_.push_back({kind,*at,*to,frame});
    };
    // Visit entities only; never traverse map cells or copy the world.
    for(const auto& e:world.entities) {
        if(!e.position)continue;
        const int hp=e.resolved_hp.value_or(0);
        const auto old=seen_.find(e.id);
        if(!events&&ready_&&old!=seen_.end()&&old->second.hp>0) {
            if(hp<=0)add(Death,old->second.position,old->second.position);
            else if(hp<old->second.hp)add(Hit,*e.position,*e.position);
        }
        // Compare timer changes, including the first tick at the setup frame.
        // Repeated observations of an unchanged paused world emit nothing.
        if(!events&&ready_&&old!=seen_.end()&&hp>0&&e.weapon_timer_start_frame&&
           old->second.weapon_timer!=e.weapon_timer_start_frame&&
           *e.weapon_timer_start_frame==static_cast<std::int64_t>(frame)) {
            add(Flash,*e.position,*e.position);
            const auto target=seen_.find(e.target_id.value_or(0));
            if(target!=seen_.end())add(Tracer,*e.position,target->second.position);
        }
        seen_[e.id]={hp,*e.position,e.weapon_timer_start_frame};
    }
    if(events && event_frame_!=frame) {
        event_frame_=frame;
        for(const auto& event:*events) {
            using Event=sim::PrototypeCombatEventKind;
            if(event.kind==Event::Shot) {
                add(Flash,event.source_position,event.source_position);
                const bool bolt=rules&&rules->read_bool(event.weapon,"IsElectricBolt",false);
                const bool beam=rules&&(rules->read_bool(event.weapon,"IsLaser",false)||
                    rules->read_bool(event.weapon,"IsRailgun",false));
                const auto projectile=rules?rules->read_string(event.weapon,"Projectile"):std::string{};
                const bool arcing=rules&&rules->read_bool(projectile,"Arcing",false);
                if(bolt||beam||!arcing)add(bolt?Bolt:beam?Beam:Tracer,event.source_position,event.target_position);
            } else if(event.kind==Event::Damage && event.hp_after>0)
                add(Hit,event.target_position,event.target_position);
            else if(event.kind==Event::Death)add(Death,event.target_position,event.target_position);
        }
    }
    shells_.clear();
    for(const auto& shot:shots) {
        const auto& p=shot.flight.arc.position;
        if(const auto at=project({p[0],p[1],p[2]}))shells_.push_back(*at);
    }
    ready_=true;
}
void BattleEffects::draw(render::Bitmap& b,int cx,int cy,std::uint64_t frame) const {
    if(b.width<=0||b.height<=0||std::uint64_t(b.width)*b.height*4>b.rgba.size())return;
    for(const auto& e:effects_) {
        if(frame<e.born||frame-e.born>=lifetime(e.kind))continue;
        const int x=e.at.x-cx,y=e.at.y-cy;
        if(e.kind==Tracer||e.kind==Beam||e.kind==Bolt) {
            const int tx=e.to.x-cx,ty=e.to.y-cy;
            const int steps=std::max(std::abs(tx-x),std::abs(ty-y));
            const auto color=e.kind==Bolt?std::array<std::uint8_t,3>{110,210,255}:
                e.kind==Beam?std::array<std::uint8_t,3>{255,65,55}:std::array<std::uint8_t,3>{255,225,130};
            for(int i=0;i<=std::min(steps,8192);++i) {
                const int px=x+int(std::int64_t(tx-x)*i/std::max(1,steps));
                const int py=y+int(std::int64_t(ty-y)*i/std::max(1,steps));
                const int zig=e.kind==Bolt&&i%12>5?2:0;
                pixel(b,px,py+zig,color);
                if(e.kind!=Tracer)pixel(b,px,py+zig+1,color);
            }
        } else {
            const auto& frames=e.kind==Flash?flash_:e.kind==Hit?hit_:death_;
            sprite(b,frames[std::min<std::size_t>((frame-e.born)/2,frames.size()-1)],x,y-8);
        }
    }
    // The loop presents tick N at frame N+1. Help can pause that frame.
    if(frame>=frame_&&frame-frame_<=1)for(const auto& p:shells_) {
        pixel(b,p.x-cx,p.y-cy);pixel(b,p.x-cx-1,p.y-cy);pixel(b,p.x-cx,p.y-cy-1);
    }
}
}
