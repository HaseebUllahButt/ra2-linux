// Tests for the Bink player against real game movies. The movies are read
// out of MOVIES01.MIX with the MIX loader and decoded to RGBA frames plus
// audio samples. Usage: test_bink_player [game_dir].
#include "bink_player.h"

extern "C" {
#include <libavutil/log.h>
}

#include "../fs/mix_archive.h"
#include "../fs/mix_bootstrap.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=ra2::fs;
using ra2::video::BinkAudio;
using ra2::video::BinkFrame;
using ra2::video::BinkPlayer;

namespace {
void expect(bool condition,const std::string& what) {
    if(!condition) throw std::runtime_error(what);
}

// Real campaign briefing movies, read straight out of the MIX archive.
// A01_F00E.BIK is the first Allied briefing; S13_F01E.BIK is a short Soviet
// briefing used for the full-play check.
BinkPlayer open_movie(fs::MixFileSystem& fs,const std::string& name,
    std::uint32_t width,std::uint32_t height,std::uint32_t frames) {
    auto location=fs.locate(name);
    if(!location) throw std::runtime_error(std::string(name)+": not in any MIX");
    auto bytes=location->archive->read(location->entry);
    expect(bytes.size()>1000,name+": empty MIX entry");
    BinkPlayer player;
    expect(player.open(std::move(bytes),name),name+": open failed: "+player.error());
    expect(player.width()==width,
        name+": width "+std::to_string(player.width()));
    expect(player.height()==height,
        name+": height "+std::to_string(player.height()));
    expect(player.frame_count()==frames,
        name+": frame count "+std::to_string(player.frame_count()));
    return player;
}

void test_briefing_frames(fs::MixFileSystem& fs) {
    // First Allied campaign briefing: decode the first 30 frames.
    auto player=open_movie(fs,"A01_F00E.BIK",640,480,1554);
    BinkFrame frame;
    double last_seconds=-1;
    for(int i=0;i<30;++i) {
        expect(player.next_frame(frame),"A01_F00E.BIK: frame "+std::to_string(i)+" missing");
        expect(frame.width==640&&frame.height==480,"A01_F00E.BIK: frame size");
        expect(frame.rgba.size()==static_cast<std::size_t>(640)*480*4,
            "A01_F00E.BIK: RGBA byte count");
        expect(frame.seconds>=last_seconds,"A01_F00E.BIK: timestamps go back");
        last_seconds=frame.seconds;
        // Alpha is fully opaque; the movies carry no transparency.
        for(std::size_t p=3;p<frame.rgba.size();p+=4)
            if(frame.rgba[p]!=255) throw std::runtime_error("A01_F00E.BIK: alpha not 255");
    }
    expect(last_seconds>0.5,"A01_F00E.BIK: 30 frames cover too little time");
}

void test_full_play(fs::MixFileSystem& fs) {
    // Short Soviet briefing: play everything through play_bytes.
    auto location=fs.locate("S13_F01E.BIK");
    if(!location) throw std::runtime_error("S13_F01E.BIK: not in any MIX");
    auto bytes=location->archive->read(location->entry);
    std::vector<BinkFrame> frames;
    BinkAudio audio;
    expect(ra2::video::play_bytes(std::move(bytes),frames,audio,"S13_F01E.BIK"),
        "S13_F01E.BIK: play_bytes failed");
    expect(frames.size()==378,
        "S13_F01E.BIK: frame count "+std::to_string(frames.size()));
    expect(frames.back().seconds>25.0,"S13_F01E.BIK: movie ends too early");
    expect(audio.rate==44100,"S13_F01E.BIK: audio rate "+std::to_string(audio.rate));
    expect(audio.channels==2,"S13_F01E.BIK: audio channels");
    // 378 frames at 15 fps of stereo 44100 Hz audio. The final Bink audio
    // block is padded, so allow one block of slack.
    const std::size_t expected=static_cast<std::size_t>(44100*25.2)*2;
    const std::size_t block=2*(44100/15);
    expect(audio.samples.size()>=expected-block&&
        audio.samples.size()<=expected+block,
        "S13_F01E.BIK: audio sample count "+std::to_string(audio.samples.size()));
    double energy=0;
    for(auto sample:audio.samples) {
        expect(std::isfinite(sample)&&sample>=-1.0f&&sample<=1.0f,
            "S13_F01E.BIK: audio sample out of range");
        energy+=double(sample)*sample;
    }
    auto rms=std::sqrt(energy/std::max<std::size_t>(1,audio.samples.size()));
    expect(rms>0.05,"S13_F01E.BIK: audio is silence (rms "+std::to_string(rms)+")");
}

void test_errors() {
    BinkPlayer player;
    expect(!player.open({}),"empty bytes must not open");
    expect(!player.error().empty(),"empty bytes must set an error");
    expect(!player.open({'N','O','P','E'}),"bad magic must not open");
    expect(!player.open({'B','I','K',0x69,1,2,3}),"truncated header must not open");
    // A valid header with no payload: open may pass, decode must stop.
    std::vector<std::uint8_t> truncated={'B','I','K',0x69,0,0,0,0,1,0,0,0,0,0,0,0};
    (void)player.open(std::move(truncated),"truncated");
    BinkFrame frame;
    expect(!player.next_frame(frame),"truncated movie must not decode frames");
}
} // namespace

int main(int argc,char** argv) {
    // The truncated-header test writes an expected libavformat error line;
    // keep the run quiet.
    av_log_set_level(AV_LOG_FATAL);
    const std::string game_dir=argc>1?argv[1]:"/path/to/user/ra2/game";
    fs::MixFileSystem fs(game_dir);
    fs.mount_language(fs::GameVariant::RedAlert2);
    fs.bootstrap(fs::GameVariant::RedAlert2);
    fs.mount_overlays();
    fs.mount_resources({});
    fs.mount("MOVIES01.MIX");
    test_briefing_frames(fs);
    test_full_play(fs);
    test_errors();
    std::cout<<"test_bink_player: PASS ("<<game_dir<<")\n";
    return 0;
}
