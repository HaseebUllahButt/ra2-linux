#pragma once
// Plays one Bink movie from its bytes into RGBA frames plus audio samples.
// FFmpeg's Bink demuxer and decoders do the Bink work; this file only moves
// bytes, pixels and samples. The bytes are one MIX entry read, and libavformat
// pulls the movie through a custom read callback over them.
// TODO-ORIGINAL: the shipped game calls BinkPlay from the Options.cpp movie
// list; the play loop is FUN_00430600 @ 0x00430600 (BinkWait, BinkDoFrame,
// BinkCopyToBuffer, BinkNextFrame) and the open path is FUN_005d8b00
// @ 0x005d8b00. This replacement decodes the same .bik files with FFmpeg.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ra2::video {

// One decoded RGBA frame plus its presentation time.
struct BinkFrame {
    std::uint32_t width=0, height=0;
    double seconds=0;               // Since the movie start.
    std::vector<std::uint8_t> rgba; // width*height*4, top-down.
};

// Whole audio track as interleaved float samples.
struct BinkAudio {
    std::uint32_t rate=0;       // Sample rate, e.g. 22050.
    int channels=0;             // Always 2; the player downmixes to stereo.
    std::vector<float> samples; // Interleaved frames in [-1,1].
};

// Decodes one Bink movie from bytes read out of a MIX archive.
class BinkPlayer {
public:
    BinkPlayer() noexcept;
    ~BinkPlayer();
    BinkPlayer(BinkPlayer&&) noexcept;
    BinkPlayer& operator=(BinkPlayer&&) noexcept;

    // Bytes of one .bik file, e.g. a MIX entry read. Returns false and sets
    // error() when the bytes do not parse as Bink.
    bool open(std::vector<std::uint8_t> bytes, std::string name="<memory>");
    // Next RGBA frame. Returns false once the movie ends or fails.
    bool next_frame(BinkFrame& frame);
    // Whole audio track, interleaved float samples in [-1,1]. Call after the
    // last frame: next_frame collects audio while it scans for video.
    bool audio(BinkAudio& out);

    const std::string& name() const noexcept { return name_; }
    const std::string& error() const noexcept { return error_; }
    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;
    std::uint32_t frame_count() const noexcept;
    // Audio samples per channel, 0 when the movie has no audio stream.
    std::int64_t audio_samples() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::string name_, error_;

    // Defined out of line in bink_player.cpp; State is complete there.
    BinkPlayer(const BinkPlayer&)=delete;
    BinkPlayer& operator=(const BinkPlayer&)=delete;
};

// Convenience: every frame and the whole audio track of one movie.
bool play_bytes(std::vector<std::uint8_t> bytes, std::vector<BinkFrame>& frames,
    BinkAudio& audio_out, std::string name="<memory>");

} // namespace ra2::video
