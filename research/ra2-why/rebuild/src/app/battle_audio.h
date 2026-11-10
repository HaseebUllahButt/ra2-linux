#pragma once
#include "platform.h"
#include "../audio/game_audio.h"
#include "../fs/mix_bootstrap.h"
#include "../session/world/world.h"
#include <map>
#include <memory>

namespace ra2::app {
// Match sound observer. This class never changes the world or its RNG.
class BattleAudio {
public:
    BattleAudio(const fs::MixFileSystem&, const rules::IniFile&, std::uint32_t player,
                std::string speech_side, bool enabled=true, unsigned frame_delay_ms=32) noexcept;
    BattleAudio(const BattleAudio&)=delete;
    BattleAudio& operator=(const BattleAudio&)=delete;
    void observe(const session::world::Snapshot&, std::uint64_t frame);
    // One EVA.INI speech line, for example "EVA_NuclearMissileReady".
    // Unknown names and headless runs are silent.
    void speak(const std::string& eva_section);
    // Call from the event pump, including while help is open. The device queue
    // controls PCM production; simulation ticks never advance sound playback.
    void service(bool paused) noexcept;
    bool enabled() const { return bool(device_); }
    const std::string& failure() const { return failure_; }
    std::size_t loaded_samples() const { return samples_.size(); }
    std::size_t fire_events() const { return fire_events_; }
    std::size_t death_events() const { return death_events_; }
    std::size_t mixed_samples() const { return mixed_samples_; }
    std::size_t queued_samples() const { return device_?device_->queued_samples():0; }
    // Last submitted PCM block, also used by the focused original-sample check.
    const std::vector<std::int16_t>& last_pcm() const { return buffer_; }
private:
    struct Seen { int hp; bool placed; std::optional<std::int32_t> timer; };
    rules::IniFile rules_,sound_,eva_;
    std::map<std::string,audio::Pcm> samples_;
    std::map<std::string,std::string> weapons_;
    audio::GameAudio mixer_;
    std::unique_ptr<AudioQueue> device_;
    std::map<std::uint32_t,Seen> seen_;
    std::vector<std::int16_t> buffer_;
    std::string failure_;
    std::uint32_t player_=0;
    std::uint64_t frame_=0;
    std::size_t fire_events_=0,death_events_=0,mixed_samples_=0;
    std::size_t queue_target_=2048,queue_low_=1024;
    bool ready_=false;
};
}
