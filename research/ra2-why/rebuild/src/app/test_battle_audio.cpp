#include "battle_audio.h"
#include "battle_superweapons.h"
#include <SDL.h>
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace ra2;
rules::IniFile read_ini(const fs::MixFileSystem& files,const char* name) {
    const auto bytes=files.read_file(name);assert(bytes);
    rules::IniFile ini;ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));return ini;
}
bool loud(const std::vector<std::int16_t>& samples) {
    return std::any_of(samples.begin(),samples.end(),[](auto sample){return sample!=0;});
}
session::world::Snapshot state() {
    session::world::Snapshot world;
    session::world::Entity unit;
    unit.id=1;unit.house_id=1;unit.type="MTNK";unit.resolved_hp=300;
    unit.kind=session::world::Kind::Unit;unit.lifetime=session::world::Lifetime::Placed;
    world.entities={unit};return world;
}
int main(int argc,char** argv) {
    assert(argc>=2);
    fs::MixFileSystem files(argv[1]);files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);files.mount_overlays();files.mount_resources();
    const auto rules=read_ini(files,"rules.ini");
    assert(!SDL_WasInit(SDL_INIT_AUDIO));
    {
        app::BattleAudio headless(files,rules,1,"Allied",false);
        assert(!headless.enabled()&&headless.loaded_samples()==0&&!SDL_WasInit(SDL_INIT_AUDIO));
    }
    if(argc==3) {
        app::BattleAudio missing(files,rules,1,"Allied");
        assert(!missing.enabled()&&!missing.failure().empty());
        auto world=state();missing.observe(world,0);missing.service(false);
        assert(missing.mixed_samples()==0);
        std::cout<<"unavailable hardware keeps play running\n";return 0;
    }
    const auto sound=read_ini(files,"sound.ini"),eva=read_ini(files,"eva.ini");
    const auto archive=files.mount("audio.mix");
    const auto bag=audio::Bag::open(archive->read(*archive->find("audio.idx")),archive->read(*archive->find("audio.bag")));
    audio::GameAudio::Config config;
    config.rules=&rules;config.sound=&sound;config.speech=&eva;config.speech_side="Allied";
    config.samples=[&](const std::string& name)->std::optional<audio::Pcm> {
        if(name.size()>4&&name.substr(name.size()-4)==".wav") {
            const auto file=archive->find(name);assert(file);return audio::decode_wav(archive->read(*file));
        }
        return bag.decode(name);
    };
    {
        app::BattleAudio audio(files,rules,1,"Allied");
        if(!audio.enabled())std::cerr<<audio.failure()<<'\n';
        assert(audio.enabled()&&audio.loaded_samples()>0);
        const auto loaded=audio.loaded_samples();
        auto world=state();
        world.random.table[17]=0x1234;const auto rng=world.random;
        audio.observe(world,0);
        world.entities[0].weapon_timer_start_frame=0;audio.observe(world,0);
        assert(audio.fire_events()==1&&audio.mixed_samples()==0);
        audio.observe(world,0);assert(audio.fire_events()==1);
        audio.service(true);assert(audio.mixed_samples()==0);
        audio.service(false);assert(audio.mixed_samples()>0&&loud(audio.last_pcm()));
        audio::GameAudio reference;reference.configure(config);reference.seed(0x41554449);
        reference.on_fire(rules.read_string("MTNK","Primary"));
        std::vector<std::int16_t> expected(audio.last_pcm().size());reference.render(expected);
        assert(audio.last_pcm()==expected); // Original decoded bag PCM through GameAudio.
        const auto mixed=audio.mixed_samples();
        audio.service(true);const auto queued=audio.queued_samples();
        SDL_Delay(120);audio.service(true);
        assert(audio.mixed_samples()==mixed&&audio.queued_samples()==queued);
        // Many simulation ticks do not consume or queue PCM.
        for(unsigned frame=1;frame<500;++frame)audio.observe(world,frame);
        assert(audio.mixed_samples()==mixed&&audio.fire_events()==1&&audio.loaded_samples()==loaded);
        audio.service(false);SDL_Delay(120);audio.service(false);
        assert(audio.mixed_samples()>mixed&&audio.queued_samples()<=2048);
        assert(world.entities[0].resolved_hp==300&&world.entities[0].weapon_timer_start_frame==0);
        assert(world.random.table==rng.table&&world.random.first==rng.first&&world.random.second==rng.second);
    }
    for(const auto* side:{"Allied","Russian"}) {
        config.speech_side=side;
        app::BattleAudio audio(files,rules,1,side);assert(audio.enabled());
        auto world=state();audio.observe(world,0);
        world.entities[0].resolved_hp=0;world.entities[0].lifetime=session::world::Lifetime::Destroyed;
        audio.observe(world,0);assert(audio.death_events()==1);
        audio.observe(world,0);assert(audio.death_events()==1);
        audio.service(false);assert(loud(audio.last_pcm()));
        audio::GameAudio reference;reference.configure(config);reference.on_event("EVA_UnitLost");
        std::vector<std::int16_t> expected(audio.last_pcm().size());reference.render(expected);
        assert(audio.last_pcm()==expected); // Original EVA WAV, not a stand-in.
    }
    {
        app::BattleAudio audio(files,rules,1,"Russian");assert(audio.enabled());
        auto world=state();
        world.entities[0].type="AMCV";
        auto enemy=world.entities[0];enemy.id=2;enemy.house_id=2;
        auto building=world.entities[0];building.id=3;building.kind=session::world::Kind::Structure;
        world.entities.push_back(enemy);world.entities.push_back(building);
        audio.observe(world,0);
        world.entities[0].lifetime=session::world::Lifetime::Destroyed; // Deployment keeps HP.
        world.entities[1].resolved_hp=0;world.entities[2].resolved_hp=0;
        audio.observe(world,1);assert(audio.death_events()==0);
        audio.service(false);assert(!loud(audio.last_pcm()));
    }
    {
        // Speed 6 waits 96 ms. Include 16 ms of drawing work between pumps.
        app::BattleAudio audio(files,rules,1,"Allied",true,96);assert(audio.enabled());
        auto world=state();audio.observe(world,0);
        world.entities[0].resolved_hp=0;audio.observe(world,0);
        audio.service(false);
        assert(audio.last_pcm().size()>2048&&audio.queued_samples()<=3528);
        for(unsigned tick=0;tick<4;++tick) {
            SDL_Delay(112);
            assert(audio.queued_samples()>0); // Speech has no empty device queue.
            const auto before=audio.mixed_samples();
            audio.service(false);
            assert(audio.mixed_samples()>before&&audio.queued_samples()<=3528);
        }
        audio.service(true);const auto queued=audio.queued_samples();
        const auto mixed=audio.mixed_samples();SDL_Delay(112);audio.service(true);
        assert(audio.queued_samples()==queued&&audio.mixed_samples()==mixed);
    }
    std::cout<<"base fire and EVA PCM, first tick, pause, fast/slow pacing, and death filters passed\n";
    // Super weapon EVA: every mapped section exists in the stock base
    // EVA.INI with Allied and Russian samples, and the mapped names play
    // real PCM. A renamed or missing section fails here.
    {
        for(const auto* section:app::superweapon_speech_sections()) {
            assert(!eva.read_string(section,"Allied").empty());
            assert(!eva.read_string(section,"Russian").empty());
        }
        const char* sides[]={"Allied","Russian"};
        for(const auto* side:sides) {
            config.speech_side=side;
            const auto speak_pcm=[&](const char* section) {
                // A fresh player per line: the PCM block then starts at the
                // first sample of the speech, with nothing queued before it.
                app::BattleAudio audio(files,rules,1,side);
                assert(audio.enabled());
                audio.speak(section);
                audio.service(false);
                assert(loud(audio.last_pcm())); // Real decoded WAV, not silence.
                audio::GameAudio reference;reference.configure(config);
                reference.on_event(section);
                std::vector<std::int16_t> expected(audio.last_pcm().size());
                reference.render(expected);
                assert(audio.last_pcm()==expected); // Original sample, byte for byte.
            };
            speak_pcm("EVA_NuclearMissileLaunched");
            speak_pcm("EVA_LightningStormReady");
            speak_pcm("EVA_LightningStormCreated");
            for (const auto* section : app::superweapon_activation_sections())
                speak_pcm(section);
        }
        config.speech_side="Allied";
    }
    std::cout<<"super weapon EVA sections and PCM playback passed\n";
}
