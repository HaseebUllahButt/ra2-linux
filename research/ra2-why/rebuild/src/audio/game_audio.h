#pragma once
// In-game audio for the native skirmish: music, EVA speech and sound events.
// The game loop owns one instance, pushes events while simulating, and calls
// render() once per frame to fill its audio stream with mixed mono samples.
//
// Data comes from the original game files:
// - THEME.INI [Themes] list plus WAV files from THEME.MIX for music.
// - EVA.INI speech lines; samples are WAV files such as CEVA062.WAV.
// - SOUND.INI event tables; samples live in the AUDIO bag (audio.idx/audio.bag).
// - RULES.INI VoiceSelect/VoiceMove/VoiceAttack and weapon Report= events.
//
// TODO-ORIGINAL: simplified playback. The original mixes positional stereo
// events with per-event control flags through the Westwood driver; see
// VoxClass::Play (gamemd 0x00752700) and ThemeClass::Play (gamemd 0x00720BB0).
// The music order follows ThemeClass::Next_Song in the GPL RA1 source
// (sequential wrap or shuffle that never repeats the current theme).

#include "audio.h"
#include "../rules/ini_file.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::audio {

// Speech priority order matching the original VoxPriority enum.
enum class SpeechPriority : std::uint8_t { Low = 0, Normal = 1, Important = 2, Critical = 3 };

// One match's audio. The caller keeps the configured INI objects alive for the
// lifetime of this object. All event calls ignore missing data quietly; decode
// errors from the SampleReader propagate.
class GameAudio {
public:
    // Returns one decoded sample for a name such as "igisea" (bag sample) or
    // "ceva062.wav" (WAV file). nullopt means the sample is unavailable.
    using SampleReader = std::function<std::optional<Pcm>(const std::string& name)>;

    struct Config {
        const rules::IniFile* rules = nullptr;  // Voice*= keys and weapon Report=
        const rules::IniFile* sound = nullptr;  // sound.ini event tables
        const rules::IniFile* speech = nullptr; // eva.ini (optional)
        const rules::IniFile* music = nullptr;  // THEME.INI (optional)
        SampleReader samples;                   // sample source for all playback
        std::uint32_t sample_rate = 22050;      // render rate of the mixed stream
        std::string speech_side = "Allied";     // eva.ini sample key (Allied/Russian)
    };

    GameAudio() = default;

    // Installs the data sources and rebuilds the music list. Call again to
    // replace everything; active playback stops.
    void configure(Config config);

    // Music: the [Themes] list from theme.ini. The playlist holds themes with
    // Normal=yes and a sound name, in declared order.
    void set_shuffle(bool shuffle);            // advance picks a different random theme
    bool shuffle() const noexcept { return shuffle_; }
    std::vector<std::string> playlist() const; // available theme section names
    // Starts a playlist entry (wraps past the end). Returns the playlist position
    // started, or playlist().size() when nothing could play.
    std::size_t play_music(std::size_t position);
    void next_music();                          // skip ahead (shuffle aware, wraps)
    void stop_music() noexcept;
    bool music_playing() const noexcept { return music_.has_value(); }
    const std::string& current_theme() const noexcept { return current_theme_; }

    // EVA speech. The name is an EVA.INI section such as "EVA_UnitReady";
    // the "EVA_" prefix may be omitted. Lines queue; a higher priority line
    // interrupts the playing one. Unknown names are dropped.
    void on_event(std::string_view name);

    // Unit voice lines from the unit's rules section. Unknown types or
    // missing voices are dropped.
    void on_select(std::string_view unit_type); // VoiceSelect=
    void on_move(std::string_view unit_type);   // VoiceMove=
    void on_attack(std::string_view unit_type); // VoiceAttack= (attack order)

    // Weapon fire sound from the weapon's Report= event.
    void on_fire(std::string_view weapon);

    // Mixes every active and queued sound into out (mono, one value per
    // frame, at the configured sample rate) and advances playback. Music
    // auto-advances to the next theme when it finishes.
    void render(std::vector<std::int16_t>& out);

    // Deterministic sample choice; seed 0 becomes 1.
    void seed(std::uint32_t value) noexcept;

    // Volume 0..1 per bus. Defaults: music 0.5, speech 1, sounds 1.
    void set_music_volume(float volume) noexcept { music_volume_ = clamp_volume(volume); }
    void set_speech_volume(float volume) noexcept { speech_volume_ = clamp_volume(volume); }
    void set_sound_volume(float volume) noexcept { sound_volume_ = clamp_volume(volume); }
    void set_speech_side(std::string side) { config_.speech_side = std::move(side); }

    // Playback state for the game loop and tests.
    std::size_t pending_speech() const noexcept { return speech_queue_.size(); }
    bool speech_playing() const noexcept { return speech_.has_value(); }
    std::size_t active_sounds() const noexcept { return sounds_.size(); }

    static constexpr std::size_t max_active_sounds = 8;   // original default Limit=5
    static constexpr std::size_t max_speech_queue = 4;
    static constexpr std::size_t max_cache_bytes = 32u << 20;

private:
    struct ThemeEntry {
        std::string name;
        std::string sound;
        bool repeat = false;
        bool normal = true;
    };
    struct Voice {
        std::shared_ptr<const Pcm> pcm;
        std::uint64_t position = 0; // source frames in 32.32 fixed point
        float volume = 1.0f;
    };
    struct PendingSpeech {
        std::vector<std::string> samples;
        float volume = 1.0f;
        SpeechPriority priority = SpeechPriority::Normal;
    };
    struct ResolvedEvent {
        std::vector<std::string> samples; // '$' prefixes stripped
        float volume = 1.0f;
        bool random = false;              // Control=random picks one sample
    };

    std::uint32_t next_random() noexcept;
    float clamp_volume(float volume) noexcept;
    std::size_t playlist_size() const noexcept { return playlist_positions_.size(); }
    void build_playlist();
    void start_theme(std::size_t playlist_position);
    void advance_music();
    void start_speech_if_idle();
    void push_sound(const ResolvedEvent& event);
    std::shared_ptr<const Pcm> load_sample(const std::string& name, bool allow_cache);
    const ResolvedEvent& resolve_event(const std::string& name);
    std::optional<std::string> unit_voice_event(std::string_view unit_type, const char* key);
    void mix_voice(std::vector<std::int32_t>& accumulator, Voice& voice) const;

    Config config_;
    bool shuffle_ = false;
    std::string current_theme_;
    std::size_t playlist_position_ = 0;
    std::vector<ThemeEntry> themes_;              // declared [Themes] order
    std::vector<std::size_t> playlist_positions_; // playable subset
    std::optional<Voice> music_;
    std::optional<Voice> speech_;
    SpeechPriority speech_priority_ = SpeechPriority::Low;
    std::vector<PendingSpeech> speech_queue_;
    std::vector<Voice> sounds_;
    std::map<std::string, ResolvedEvent> event_cache_;
    std::map<std::string, std::shared_ptr<const Pcm>> sample_cache_;
    std::size_t cache_bytes_ = 0;
    std::uint32_t rng_ = 1;
    float music_volume_ = 0.5f;
    float speech_volume_ = 1.0f;
    float sound_volume_ = 1.0f;
};

} // namespace ra2::audio
