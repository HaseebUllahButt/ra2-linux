#include "game_audio.h"

#include <algorithm>
#include <cctype>

namespace ra2::audio {
namespace {
std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmed(std::string_view text) { return std::string(rules::IniFile::trim(text)); }
SpeechPriority parse_priority(std::string_view text) {
    const auto v = lower(trimmed(text));
    if (v == "critical") return SpeechPriority::Critical;
    if (v == "important" || v == "high") return SpeechPriority::Important;
    if (v == "low") return SpeechPriority::Low;
    return SpeechPriority::Normal;
}
bool has_word(std::string_view text, std::string_view word) {
    const auto v = lower(text);
    std::size_t pos = 0;
    while ((pos = v.find(word, pos)) != std::string::npos) {
        const bool left = pos == 0 || std::isspace(static_cast<unsigned char>(v[pos - 1]));
        const auto end = pos + word.size();
        const bool right = end == v.size() || std::isspace(static_cast<unsigned char>(v[end]));
        if (left && right) return true;
        pos = end;
    }
    return false;
}
} // namespace

std::uint32_t GameAudio::next_random() noexcept {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; // xorshift32
    return rng_;
}
float GameAudio::clamp_volume(float volume) noexcept { return std::clamp(volume, 0.0f, 1.0f); }
void GameAudio::seed(std::uint32_t value) noexcept { rng_ = value ? value : 1; }

void GameAudio::configure(Config config) {
    config_ = std::move(config);
    music_.reset(); speech_.reset(); speech_queue_.clear(); sounds_.clear();
    event_cache_.clear(); sample_cache_.clear(); cache_bytes_ = 0;
    current_theme_.clear(); playlist_position_ = 0;
    build_playlist();
}

void GameAudio::build_playlist() {
    themes_.clear(); playlist_positions_.clear();
    const auto* ini = config_.music;
    if (!ini) return;
    const auto* list = ini->find_section("Themes");
    if (!list) return;
    for (const auto& entry : list->entries) {
        const auto name = trimmed(entry.value); // "19= ;Name" leaves an empty value
        if (name.empty()) continue;
        const auto theme = find_theme(*ini, name);
        if (!theme || theme->sound.empty()) continue;
        themes_.push_back({name, theme->sound, theme->repeat, theme->normal});
        if (theme->normal) playlist_positions_.push_back(themes_.size() - 1);
    }
}

std::vector<std::string> GameAudio::playlist() const {
    std::vector<std::string> out;
    for (auto i : playlist_positions_) out.push_back(themes_[i].name);
    return out;
}
void GameAudio::set_shuffle(bool shuffle) { shuffle_ = shuffle; }

std::shared_ptr<const Pcm> GameAudio::load_sample(const std::string& name, bool allow_cache) {
    if (!config_.samples) return nullptr;
    if (allow_cache) {
        const auto it = sample_cache_.find(name);
        if (it != sample_cache_.end()) return it->second;
    }
    auto pcm = config_.samples(name);
    if (!pcm || pcm->samples.empty() || pcm->channels == 0 || pcm->sample_rate == 0) return nullptr;
    auto shared = std::make_shared<const Pcm>(std::move(*pcm));
    if (allow_cache) {
        const auto bytes = shared->samples.size() * sizeof(std::int16_t);
        if (cache_bytes_ + bytes > max_cache_bytes) { sample_cache_.clear(); cache_bytes_ = 0; }
        if (bytes <= max_cache_bytes) { sample_cache_[name] = shared; cache_bytes_ += bytes; }
    }
    return shared;
}

void GameAudio::start_theme(std::size_t position) {
    playlist_position_ = position;
    const auto& theme = themes_[playlist_positions_[position]];
    current_theme_ = theme.name;
    // Music files are WAV files in THEME.MIX named after the Sound= value.
    auto pcm = load_sample(lower(theme.sound) + ".wav", false);
    if (!pcm) { music_.reset(); return; }
    music_ = Voice{pcm, 0, 1.0f};
}

std::size_t GameAudio::play_music(std::size_t position) {
    if (playlist_positions_.empty()) return 0;
    position %= playlist_size();
    start_theme(position);
    return music_ ? position : playlist_size();
}

void GameAudio::next_music() {
    const auto n = playlist_size();
    if (n == 0) return;
    std::size_t next;
    if (shuffle_ && n > 1) { // ThemeClass::Next_Song: never the same theme twice
        next = next_random() % (n - 1);
        if (next >= playlist_position_) ++next;
    } else {
        next = (playlist_position_ + 1) % n;
    }
    // A missing file skips to the next entry so music keeps going.
    for (std::size_t tries = 0; tries < n; ++tries) {
        start_theme(next);
        if (music_) return;
        next = (next + 1) % n;
    }
}
void GameAudio::advance_music() { next_music(); }
void GameAudio::stop_music() noexcept { music_.reset(); current_theme_.clear(); }

void GameAudio::on_event(std::string_view name) {
    const auto* eva = config_.speech;
    if (!eva) return;
    std::string section(name);
    if (!eva->find_section(section)) section = "EVA_" + section;
    if (!eva->find_section(section)) return;
    auto sample = trimmed(eva->read_string(section, config_.speech_side));
    if (sample.empty()) return;
    PendingSpeech line;
    line.samples = {lower(sample) + ".wav"};
    line.priority = parse_priority(eva->read_string(section, "Priority", "Normal"));
    for (const auto& queued : speech_queue_) // no duplicate lines in the queue
        if (queued.samples == line.samples) return;
    if (speech_ && line.priority > speech_priority_) { speech_.reset(); } // interrupt
    // Keep the queue ordered by priority (stable within equal priority).
    auto at = std::find_if(speech_queue_.begin(), speech_queue_.end(),
                           [&](const PendingSpeech& p) { return p.priority < line.priority; });
    speech_queue_.insert(at, std::move(line));
    if (speech_queue_.size() > max_speech_queue) speech_queue_.pop_back();
    start_speech_if_idle();
}

void GameAudio::start_speech_if_idle() {
    while (!speech_ && !speech_queue_.empty()) {
        auto line = std::move(speech_queue_.front());
        speech_queue_.erase(speech_queue_.begin());
        if (auto pcm = load_sample(line.samples.front(), true)) {
            speech_ = Voice{pcm, 0, line.volume};
            speech_priority_ = line.priority;
        }
    }
}

const GameAudio::ResolvedEvent& GameAudio::resolve_event(const std::string& name) {
    const auto found = event_cache_.find(name);
    if (found != event_cache_.end()) return found->second;
    ResolvedEvent event;
    if (config_.sound) {
        if (const auto sound = find_sound(*config_.sound, name)) {
            for (auto s : sound->samples) {
                const auto start = s.find_first_not_of('$'); // '$' marks a voice flag
                if (start != std::string::npos) event.samples.push_back(s.substr(start));
            }
            // Volume is 0..100; the [Defaults] section supplies the fallback (80).
            const int fallback = config_.sound->read_integer("Defaults", "Volume", 80);
            event.volume = clamp_volume(config_.sound->read_integer(name, "Volume", fallback) / 100.0f);
            event.random = has_word(config_.sound->read_string(name, "Control"), "random");
        }
    }
    return event_cache_.emplace(name, std::move(event)).first->second;
}

std::optional<std::string> GameAudio::unit_voice_event(std::string_view unit_type, const char* key) {
    if (!config_.rules) return std::nullopt;
    const auto value = trimmed(config_.rules->read_string(unit_type, key));
    if (value.empty()) return std::nullopt;
    return value;
}

void GameAudio::push_sound(const ResolvedEvent& event) {
    if (event.samples.empty()) return;
    const auto& name = event.samples[event.random ? next_random() % event.samples.size() : 0];
    auto pcm = load_sample(name, true);
    if (!pcm) return;
    if (sounds_.size() >= max_active_sounds) sounds_.erase(sounds_.begin()); // drop the oldest
    sounds_.push_back(Voice{pcm, 0, event.volume});
}

void GameAudio::on_select(std::string_view unit_type) {
    if (auto e = unit_voice_event(unit_type, "VoiceSelect")) push_sound(resolve_event(*e));
}
void GameAudio::on_move(std::string_view unit_type) {
    if (auto e = unit_voice_event(unit_type, "VoiceMove")) push_sound(resolve_event(*e));
}
void GameAudio::on_attack(std::string_view unit_type) {
    if (auto e = unit_voice_event(unit_type, "VoiceAttack")) push_sound(resolve_event(*e));
}
void GameAudio::on_fire(std::string_view weapon) {
    if (auto e = unit_voice_event(weapon, "Report")) push_sound(resolve_event(*e));
}

void GameAudio::mix_voice(std::vector<std::int32_t>& acc, Voice& voice) const {
    const auto& pcm = *voice.pcm;
    const std::uint64_t frames = pcm.samples.size() / pcm.channels;
    const std::uint64_t step = (static_cast<std::uint64_t>(pcm.sample_rate) << 32) / config_.sample_rate;
    for (auto& slot : acc) {
        const std::uint64_t index = voice.position >> 32;
        if (index >= frames) break;
        std::int32_t sum = 0; // stereo is folded down to mono
        for (std::uint16_t c = 0; c < pcm.channels; ++c) sum += pcm.samples[index * pcm.channels + c];
        slot += static_cast<std::int32_t>(sum / pcm.channels * voice.volume);
        voice.position += step;
    }
}

void GameAudio::render(std::vector<std::int16_t>& out) {
    std::vector<std::int32_t> acc(out.size(), 0);
    if (music_) {
        mix_voice(acc, *music_); // scaled below
        for (auto& v : acc) v = static_cast<std::int32_t>(v * music_volume_);
        const auto& pcm = *music_->pcm;
        if ((music_->position >> 32) >= pcm.samples.size() / pcm.channels) {
            const auto repeat = !current_theme_.empty() &&
                                themes_[playlist_positions_.empty() ? 0 : playlist_positions_[playlist_position_]].repeat;
            if (repeat) music_->position = 0; else advance_music();
        }
    }
    std::vector<std::int32_t> bus(out.size(), 0);
    if (speech_) {
        mix_voice(bus, *speech_);
        for (std::size_t i = 0; i < acc.size(); ++i) acc[i] += static_cast<std::int32_t>(bus[i] * speech_volume_);
        if ((speech_->position >> 32) >= speech_->pcm->samples.size() / speech_->pcm->channels) {
            speech_.reset(); start_speech_if_idle();
        }
    }
    std::fill(bus.begin(), bus.end(), 0);
    for (auto& voice : sounds_) mix_voice(bus, voice);
    for (std::size_t i = 0; i < acc.size(); ++i) acc[i] += static_cast<std::int32_t>(bus[i] * sound_volume_);
    sounds_.erase(std::remove_if(sounds_.begin(), sounds_.end(), [](const Voice& v) {
                      return (v.position >> 32) >= v.pcm->samples.size() / v.pcm->channels; }),
                  sounds_.end());
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<std::int16_t>(std::clamp<std::int32_t>(acc[i], -32768, 32767));
}

} // namespace ra2::audio
