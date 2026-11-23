#include "lockstep.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
namespace ra2::net {
Lockstep::Lockstep(std::uint8_t p,std::uint32_t d):players_(p),delay_(d) {
 if (!p || p>8 || !d || d>128) throw std::invalid_argument("lockstep configuration");
}
std::uint32_t Lockstep::delayed_frame() const {
 if(next_>UINT32_MAX-delay_) throw std::overflow_error("frame overflow");
 return next_+delay_;
}
bool Lockstep::submit(std::uint8_t p,std::uint32_t f,const std::vector<Event>& e) {
 if(p>=players_ || f<next_ || f-next_>256 || e.size()>128) return false;
 for(const auto& x:e) if(x.player!=p || x.frame!=f || unsigned(x.type)>46) return false;
 auto& batches=frames_[f]; auto it=batches.find(p);
 if(it!=batches.end()) return it->second==e;
 batches.emplace(p,e); return true;
}
bool Lockstep::ready() const {
 auto it=frames_.find(next_); return !desync_ && it!=frames_.end() && it->second.size()==players_;
}
std::vector<Event> Lockstep::advance() {
 if(!ready()) throw std::logic_error("frame waiting or desync");
 std::vector<Event> out;
 for(auto& [p,e]:frames_.at(next_)) out.insert(out.end(),e.begin(),e.end());
 frames_.erase(next_++); return out;
}
void Lockstep::checksum(std::uint8_t p,std::uint32_t f,std::uint32_t c) {
 if(p>=players_ || (f>next_ && f-next_>256) || (next_>f && next_-f>256)) return;
 auto& values=crcs_[f]; auto [it,added]=values.emplace(p,c);
 if(!added && it->second!=c && !desync_) desync_=Desync{f,p,it->second,c};
 auto base=values.find(0);
 if(base!=values.end()) for(auto [who,v]:values)
  if(v!=base->second && !desync_) desync_=Desync{f,who,base->second,v};
 while(!crcs_.empty() && next_>crcs_.begin()->first && next_-crcs_.begin()->first>256) crcs_.erase(crcs_.begin());
}
std::string Desync::report() const {
 return "Desync frame="+std::to_string(frame)+" player="+std::to_string(player)+
 " expected="+std::to_string(expected)+" received="+std::to_string(received);
 // TODO-ORIGINAL Network_WriteSyncDump 0064DEA0: include original per-object dump sections.
}
std::uint32_t frame_crc(std::span<const std::uint8_t> b) {
 // Ares-YRpp Checksummer.h: reflected CRC32, with Fill() on partial words.
 std::uint32_t c=0xffffffffu;
 auto add=[&](std::uint8_t x){c^=x;for(int k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320u:0);};
 for(auto x:b)add(x);
 auto tail=b.size()%4;
 if(tail){add(std::uint8_t(tail));for(auto i=tail+1;i<4;++i)add(b[b.size()-tail]);}
 c=~c;
 return c;
}
std::uint32_t world_crc(const session::world::Snapshot& w) {
 // TODO-ORIGINAL Network_WriteSyncDump: full original RNG, timers, map and object CRC coverage.
 // This digest covers shared prototype identity, ownership, position, health and cash only.
 std::vector<std::uint8_t> b;
 auto put=[&](std::uint32_t x){for(int i=0;i<4;++i)b.push_back(x>>(8*i));};
 auto optional=[&](auto x){put(x.has_value()); if(x) put(std::uint32_t(*x));};
 std::vector<const session::world::House*> houses;
 for(auto& h:w.houses) houses.push_back(&h);
 std::sort(houses.begin(),houses.end(),[](auto a,auto z){return a->id<z->id;});
 put(houses.size()); for(auto h:houses){put(h->id); optional(h->cash);}
 std::vector<const session::world::Entity*> entities;
 for(auto& e:w.entities) entities.push_back(&e);
 std::sort(entities.begin(),entities.end(),[](auto a,auto z){return a->id<z->id;});
 put(entities.size()); for(auto e:entities){put(e->id);put(e->house_id);put(unsigned(e->kind));put(unsigned(e->lifetime));
 put(e->type.size());for(auto c:e->type)b.push_back(c);
 optional(e->resolved_hp);put(e->position.has_value());if(e->position){put(e->position->x);put(e->position->y);put(e->position->z);}
 put(e->cell.has_value());if(e->cell){put(e->cell->x);put(e->cell->y);}}
 return frame_crc(b);
}
}
