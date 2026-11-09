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
void pixel(render::Bitmap& b,int x,int y) {
    if(x<0||y<0||x>=b.width||y>=b.height)return;
    const auto at=(std::size_t(y)*b.width+x)*4;
    b.rgba[at]=255;b.rgba[at+1]=225;b.rgba[at+2]=130;b.rgba[at+3]=255;
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
    return k==Tracer?3:k==Flash?std::min<std::size_t>(6,flash_.size()*2):
        (k==Hit?hit_.size():death_.size())*2;
}
void BattleEffects::observe(const session::world::Snapshot& world,
    const std::vector<sim::PrototypeCombatProjectile>& shots,std::uint64_t frame,const Project& project) {
    if(ready_&&frame<frame_){effects_.clear();seen_.clear();ready_=false;}
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
        if(ready_&&old!=seen_.end()&&old->second.hp>0) {
            if(hp<=0)add(Death,old->second.position,old->second.position);
            else if(hp<old->second.hp)add(Hit,*e.position,*e.position);
        }
        // Compare timer changes, including the first tick at the setup frame.
        // Repeated observations of an unchanged paused world emit nothing.
        if(ready_&&old!=seen_.end()&&hp>0&&e.weapon_timer_start_frame&&
           old->second.weapon_timer!=e.weapon_timer_start_frame&&
           *e.weapon_timer_start_frame==static_cast<std::int64_t>(frame)) {
            add(Flash,*e.position,*e.position);
            const auto target=seen_.find(e.target_id.value_or(0));
            if(target!=seen_.end())add(Tracer,*e.position,target->second.position);
        }
        seen_[e.id]={hp,*e.position,e.weapon_timer_start_frame};
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
        if(e.kind==Tracer) {
            // Short tracer at the target end, not a persistent full-map beam.
            const int tx=e.to.x-cx,ty=e.to.y-cy;
            for(int i=0;i<8;++i)pixel(b,tx+(x-tx)*i/32,ty+(y-ty)*i/32);
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
