#include "game_audio.h"
#include "fs/mix_bootstrap.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace ra2;
using audio::GameAudio;

static rules::IniFile load_ini(fs::MixFileSystem& f, const char* name) {
    auto b = f.read_file(name); assert(b);
    rules::IniFile ini; ini.parse(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()));
    return ini;
}
static bool loud(const std::vector<std::int16_t>& v) { for (auto s : v) if (s) return true; return false; }
static void run(GameAudio& a, int frames) { std::vector<std::int16_t> out(frames); a.render(out); }

int main(int argc, char** argv) {
    fs::MixFileSystem files(argc > 1 ? argv[1] : "/path/to/user/ra2-test-host/game");
    files.mount_language(fs::GameVariant::RedAlert2); files.bootstrap(fs::GameVariant::RedAlert2);
    auto theme_mix = files.mount("THEME.MIX");
    auto rules = load_ini(files, "rules.ini"), sound = load_ini(files, "sound.ini"),
         eva = load_ini(files, "eva.ini"), theme = load_ini(files, "theme.ini");

    std::vector<std::string> requested;
    GameAudio::Config cfg;
    cfg.rules = &rules; cfg.sound = &sound; cfg.speech = &eva; cfg.music = &theme;
    cfg.sample_rate = 22050;
    cfg.samples = [&](const std::string& name) -> std::optional<audio::Pcm> {
        requested.push_back(name);
        if (name.size() > 4 && name.substr(name.size() - 4) == ".wav") {
            if (auto e = theme_mix->find(name)) return audio::decode_wav(theme_mix->read(*e)); // real music
            if (name[0] == 'c') return audio::Pcm{22050, 1, std::vector<std::int16_t>(2000, 1000)}; // EVA stand-in
            return std::nullopt;
        }
        return audio::Pcm{22050, 1, std::vector<std::int16_t>(1000, 2000)}; // bag sample stand-in
    };
    GameAudio a; a.configure(cfg); a.seed(7);

    // Music: real playlist and a real WAV from THEME.MIX.
    auto list = a.playlist();
    assert(list.size() > 5);
    assert(std::find(list.begin(), list.end(), "INTRO") == list.end()); // Normal=no
    assert(std::find(list.begin(), list.end(), "Grinder") != list.end());
    assert(a.play_music(0) == 0 && a.music_playing() && a.current_theme() == list[0]);
    std::vector<std::int16_t> out(4096); a.render(out); assert(loud(out));
    a.set_shuffle(false); auto first = a.current_theme(); a.next_music(); assert(a.current_theme() == list[1] && a.current_theme() != first);
    a.set_shuffle(true);
    for (int i = 0; i < 20; ++i) { auto before = a.current_theme(); a.next_music(); assert(a.current_theme() != before); }
    a.stop_music(); assert(!a.music_playing());

    // EVA lines.
    for (auto n : {"EVA_UnitReady", "ConstructionComplete", "OurBaseIsUnderAttack", "LowPower", "InsufficientFunds"}) {
        GameAudio b; b.configure(cfg); requested.clear(); b.on_event(n);
        assert(b.speech_playing());
        run(b, 100);
    }
    requested.clear(); { GameAudio b; b.configure(cfg); b.on_event("UnitReady"); assert(requested.at(0) == "ceva062.wav"); }
    requested.clear(); { auto c = cfg; c.speech_side = "Russian"; GameAudio b; b.configure(c); b.on_event("UnitReady"); assert(requested.at(0) == "csof062.wav"); }
    { GameAudio b; b.configure(cfg); b.on_event("NoSuchLine"); assert(!b.speech_playing() && b.pending_speech() == 0); }
    { // priority: low line is interrupted by an important one; queue plays after
        GameAudio b; b.configure(cfg);
        b.on_event("UnitReady"); b.on_event("ConstructionComplete"); assert(b.pending_speech() == 1);
        b.on_event("LowPower"); // Important interrupts
        assert(b.speech_playing());
        b.on_event("LowPower"); assert(b.pending_speech() <= 2);
        for (int i = 0; i < 20 && (b.speech_playing() || b.pending_speech()); ++i) run(b, 2000);
        assert(!b.speech_playing() && b.pending_speech() == 0);
    }

    // Unit voices and weapon reports.
    { GameAudio b; b.configure(cfg); requested.clear(); b.on_select("E1"); assert(b.active_sounds() == 1);
      assert(requested.at(0).find('$') == std::string::npos);
      b.on_move("E1"); b.on_attack("E1"); assert(b.active_sounds() == 3);
      b.on_select("NOPE"); assert(b.active_sounds() == 3);
      b.on_fire("105mm"); b.on_fire("NoWeapon"); assert(b.active_sounds() == 4);
      std::vector<std::int16_t> o(500); b.render(o); assert(loud(o));
      run(b, 4000); assert(b.active_sounds() == 0);
      for (int i = 0; i < 30; ++i) { b.on_select("E1"); }
      assert(b.active_sounds() <= GameAudio::max_active_sounds); }
    // Determinism.
    { GameAudio x, y; x.configure(cfg); y.configure(cfg); x.seed(5); y.seed(5);
      requested.clear(); x.on_select("E1"); auto rx = requested; requested.clear(); y.on_select("E1"); assert(rx == requested); }
    // Missing data is quiet.
    { GameAudio empty; empty.on_event("UnitReady"); empty.on_select("E1"); empty.on_fire("x"); std::vector<std::int16_t> o(10, 5); empty.render(o); assert(!loud(o)); }
    std::cout << "game audio tests passed\n";
}
