#include "battle_audio.h"
#include "battle_superweapons.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace ra2::app {
namespace {
std::string lower(std::string s) {
    for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
rules::IniFile ini(const fs::MixFileSystem& files,const char* name) {
    const auto bytes=files.read_file(name);
    if(!bytes)throw std::runtime_error(std::string("Missing base audio table: ")+name);
    rules::IniFile result;
    result.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
    return result;
}
}
BattleAudio::BattleAudio(const fs::MixFileSystem& files,const rules::IniFile& rules,
                        std::uint32_t player,std::string side,bool enabled,unsigned frame_delay_ms) noexcept :player_(player) {
    if(frame_delay_ms>32) {
        const auto delay=std::min(frame_delay_ms,250u);
        queue_target_=(22050u*(delay+64u)+999u)/1000u;
        queue_low_=(22050u*(delay+32u)+999u)/1000u;
    }
    if(!enabled)return; // Headless runs do not open hardware or decode samples.
    try {
        device_=std::make_unique<AudioQueue>(22050,1);
        rules_=rules;sound_=ini(files,"sound.ini");eva_=ini(files,"eva.ini");
        const auto archive=[&] {
            if(const auto at=files.locate("audio.mix"))
                return at->archive->open_nested(at->entry,"audio.mix");
            auto bytes=files.read_file("audio.mix");
            if(!bytes)throw std::runtime_error("Missing base audio.mix");
            return fs::MixArchive::from_bytes(std::move(*bytes),"audio.mix");
        }();
        const auto index=archive->find("audio.idx"),data=archive->find("audio.bag");
        if(!index||!data)throw std::runtime_error("Missing base audio bag");
        const auto bag=audio::Bag::open(archive->read(*index),archive->read(*data));
        std::set<std::string> names;
        // WeaponTypeClass::Report is the sound-event list in the recovered
        // ref/Ares-YRpp/WeaponTypeClass.h. Cache every report before play.
        for(const auto& section:rules_.sections()) {
            const auto primary=rules_.read_string(section.name,"Primary");
            if(!primary.empty())weapons_[section.name]=primary;
            for(const auto& event:rules_.read_list(section.name,"Report"))
                if(const auto sound=audio::find_sound(sound_,event))for(auto name:sound->samples) {
                    const auto start=name.find_first_not_of('$');
                    if(start!=std::string::npos)names.insert(lower(name.substr(start)));
                }
        }
        const auto speech=lower(eva_.read_string("EVA_UnitLost",side));
        if(!speech.empty())names.insert(speech+".wav");
        // Super weapon speech: every stock base EVA.INI section the battle
        // maps (see battle_superweapons.h). Cache each line the file has
        // for this side, so a mapped name never misses its sample.
        for(const auto* section:superweapon_speech_sections()) {
            const auto line=lower(eva_.read_string(section,side));
            if(!line.empty())names.insert(line+".wav");
        }
        std::size_t bytes=0;
        for(const auto& name:names) {
            try {
                std::optional<audio::Pcm> pcm;
                if(name.size()>4&&name.substr(name.size()-4)==".wav") {
                    if(const auto file=archive->find(name))pcm=audio::decode_wav(archive->read(*file));
                } else pcm=bag.decode(name);
                if(!pcm||pcm->samples.empty())continue;
                const auto size=pcm->samples.size()*sizeof(std::int16_t);
                if(bytes+size>audio::GameAudio::max_cache_bytes)continue;
                bytes+=size;samples_.emplace(name,std::move(*pcm));
            } catch(const std::exception&) { /* Missing optional samples stay silent. */ }
        }
        audio::GameAudio::Config config;
        config.rules=&rules_;config.sound=&sound_;config.speech=&eva_;
        config.speech_side=std::move(side);
        // This reader only accesses decoded memory, including for missing names.
        config.samples=[this](const std::string& name)->std::optional<audio::Pcm> {
            const auto found=samples_.find(lower(name));
            if(found==samples_.end())return {};
            return found->second;
        };
        mixer_.configure(std::move(config));mixer_.seed(0x41554449);
    } catch(const std::exception& e) {
        failure_=e.what();device_.reset();
    }
}
void BattleAudio::observe(const session::world::Snapshot& world,std::uint64_t frame) {
    if(!device_)return;
    if(ready_&&frame<frame_){seen_.clear();ready_=false;}
    frame_=frame;
    for(const auto& e:world.entities) {
        const int hp=e.resolved_hp.value_or(0);
        const auto old=seen_.find(e.id);
        if(ready_&&old!=seen_.end()) {
            // RA1 FOOT.CPP Death_Announcement speaks only for player-owned
            // mobile units. Deployment preserves HP and is not a death.
            if(old->second.placed&&old->second.hp>0&&hp<=0&&e.house_id==player_&&
               (e.kind==session::world::Kind::Unit||e.kind==session::world::Kind::Infantry)) {
                mixer_.on_event("EVA_UnitLost");++death_events_;
            }
            if(hp>0&&e.weapon_timer_start_frame&&old->second.timer!=e.weapon_timer_start_frame&&
               *e.weapon_timer_start_frame==static_cast<std::int64_t>(frame)) {
                const auto weapon=weapons_.find(e.type);
                if(weapon!=weapons_.end()) {mixer_.on_fire(weapon->second);++fire_events_;}
            }
        }
        seen_[e.id]={hp,e.lifetime==session::world::Lifetime::Placed,e.weapon_timer_start_frame};
    }
    ready_=true;
}
void BattleAudio::speak(const std::string& eva_section) {
    if(!device_||eva_section.empty())return;
    mixer_.on_event(eva_section);
}
void BattleAudio::service(bool paused) noexcept {
    if(!device_)return;
    try {
        device_->pause(paused);
        if(paused)return;
        // Fast settings keep the original 46..93 ms buffer. Slower settings
        // cover one configured wait plus 32..64 ms for drawing and device work.
        // The delay cap bounds both latency and work per service call.
        const auto queued=device_->queued_samples();
        if(queued>=queue_low_)return;
        buffer_.assign(queue_target_-queued,0);
        mixer_.render(buffer_);device_->append(buffer_);mixed_samples_+=buffer_.size();
    } catch(const std::exception& e) {
        failure_=e.what();device_.reset();
    }
}
}
