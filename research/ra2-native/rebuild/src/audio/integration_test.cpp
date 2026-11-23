#include "audio.h"
#include "mix_bootstrap.h"
#include "../rules/ini_file.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace ra2;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    fs::MixFileSystem f(argv[1]); f.bootstrap(); f.mount("language.mix"); f.mount("langmd.mix");
    auto soundbytes=f.read_file("soundmd.ini"); auto themebytes=f.read_file("thememd.ini");
    assert(soundbytes && themebytes);
    rules::IniFile soundini, themeini;
    soundini.parse(std::string(soundbytes->begin(),soundbytes->end()));
    themeini.parse(std::string(themebytes->begin(),themebytes->end()));
    auto event=audio::find_sound(soundini,"ChronoLegionAttack");
    assert(event && event->samples.size()==1 && event->samples[0]=="ichratta");
    auto intro=audio::find_theme(themeini,"INTRO");
    assert(intro && intro->sound=="Drok" && !intro->normal && intro->repeat);
    assert(!audio::find_sound(soundini,"missing"));
    for (auto archive : {"audio.mix", "audiomd.mix"}) {
        auto m=f.mount(archive); auto idx=m->read(*m->find("audio.idx"));
        auto bag=audio::Bag::open(idx,m->read(*m->find("audio.bag")));
        auto voice=bag.decode("ichratta");
        if (std::string(archive)=="audiomd.mix") {
            auto rex=bag.decode("grexselb");
            assert(rex.channels==2 && rex.samples.size()==40921*2);
        } assert(voice.sample_rate==22050 && voice.channels==1 && voice.samples.size()==141323);
        for (auto name : {"ceva062.wav", "csof062.wav"}) {
            auto e=m->find(name); assert(e); auto b=m->read(*e); auto p=audio::decode_wav(b);
            assert(p.sample_rate==22050 && p.channels==1 && !p.samples.empty());
            if (std::string(name)=="ceva062.wav") assert(p.samples.size()==17580);
            b.pop_back(); bool failed=false; try { audio::decode_wav(b); } catch(const std::runtime_error&) { failed=true; } assert(failed);
        }
    }
    auto theme=f.mount("thememd.mix");
    for (auto name : {"Drok.wav", "Tactics.wav"}) {
        auto e=theme->find(name); assert(e); auto p=audio::decode_wav(theme->read(*e));
        assert(p.sample_rate==22050 && p.channels==2 && !p.samples.empty());
    }
    std::cout << "real voice, EVA and music tests passed\n";
}
