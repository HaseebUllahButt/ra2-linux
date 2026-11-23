// Bink playback over the MIX entry bytes with FFmpeg. See bink_player.h.
#include "bink_player.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <utility>

namespace ra2::video {

// Custom read callback over the MIX entry bytes. libavformat pulls the movie
// through this; nothing reads the file a second time.
struct ReadSource {
    const std::uint8_t* bytes=nullptr;
    std::size_t size=0;
    std::size_t pos=0;
};
static int read_packet(void* opaque, std::uint8_t* buffer, int buffer_size) {
    auto* source=static_cast<ReadSource*>(opaque);
    if(source->pos>=source->size) return AVERROR_EOF;
    if(buffer_size<=0) return AVERROR(EINVAL);
    auto left=source->size-source->pos;
    auto take=std::min<std::size_t>(left,static_cast<std::size_t>(buffer_size));
    std::memcpy(buffer,source->bytes+source->pos,take);
    source->pos+=take;
    return static_cast<int>(take);
}
static std::int64_t seek_packet(void* opaque, std::int64_t offset, int whence) {
    auto* source=static_cast<ReadSource*>(opaque);
    if(whence&AVSEEK_SIZE) return static_cast<std::int64_t>(source->size);
    switch(whence) {
        case SEEK_SET: break;
        case SEEK_CUR: offset+=static_cast<std::int64_t>(source->pos); break;
        case SEEK_END: offset+=static_cast<std::int64_t>(source->size); break;
        default: return AVERROR(EINVAL);
    }
    if(offset<0) return AVERROR(EINVAL);
    source->pos=static_cast<std::size_t>(
        std::min<std::uint64_t>(static_cast<std::uint64_t>(offset),source->size));
    return static_cast<std::int64_t>(source->pos);
}

// Owns the FFmpeg contexts and the movie bytes for one open movie.
struct BinkPlayer::State {
    std::vector<std::uint8_t> bytes;   // The MIX entry bytes; the IO reads here.
    ReadSource source;
    AVFormatContext* format=nullptr;   // Owns the custom IO context.
    AVCodecContext* video=nullptr;     // Bink video decoder.
    AVCodecContext* audio=nullptr;     // Bink audio decoder.
    SwsContext* scale=nullptr;         // YUV to RGBA.
    SwrContext* resample=nullptr;      // Bink audio to stereo float.
    int video_stream=-1, audio_stream=-1;
    std::uint32_t width=0, height=0;
    std::uint32_t decoded_frames=0;
    std::vector<float> audio_samples;  // Interleaved stereo, whole track.
    std::uint32_t audio_rate=0;
    ~State() { close(); }
    void close() noexcept {
        if(video) avcodec_free_context(&video);
        if(audio) avcodec_free_context(&audio);
        if(format) {
            AVIOContext* io=format->pb;
            avformat_close_input(&format);
            // AVFMT_FLAG_CUSTOM_IO: the caller owns the context and buffer.
            if(io) {
                av_freep(&io->buffer);
                av_freep(&io);
            }
        }
        if(scale) sws_freeContext(scale);
        if(resample) swr_free(&resample);
        video=nullptr; audio=nullptr; format=nullptr;
        scale=nullptr; resample=nullptr;
        video_stream=audio_stream=-1;
    }
};

BinkPlayer::BinkPlayer() noexcept=default;
BinkPlayer::~BinkPlayer()=default;
BinkPlayer::BinkPlayer(BinkPlayer&&) noexcept=default;
BinkPlayer& BinkPlayer::operator=(BinkPlayer&&) noexcept=default;

bool BinkPlayer::open(std::vector<std::uint8_t> bytes, std::string name) {
    error_.clear();
    name_=std::move(name);
    if(bytes.size()<4 || bytes[0]!='B' || bytes[1]!='I' || bytes[2]!='K') {
        error_=name_+": not a Bink movie (bad magic)";
        return false;
    }
    // The Bink demuxer ships inside libavformat; ask for it by name.
    const AVInputFormat* bink=av_find_input_format("bink");
    if(!bink) {
        error_=name_+": no Bink demuxer in this libavformat build";
        return false;
    }
    // Custom read callback on the MIX entry bytes: libavformat reads the
    // movie through the buffer, never from the file a second time.
    auto state=std::make_unique<State>();
    state->bytes=std::move(bytes);
    state->source.bytes=state->bytes.data();
    state->source.size=state->bytes.size();
    state->format=avformat_alloc_context();
    auto* io=avio_alloc_context(
        static_cast<std::uint8_t*>(av_malloc(4096)),4096,0,
        &state->source,read_packet,nullptr,seek_packet);
    if(!io) {
        error_=name_+": out of memory for the custom read callback";
        return false;
    }
    state->format->pb=io;
    state->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if(avformat_open_input(&state->format,"",bink,nullptr)!=0) {
        error_=name_+": avformat_open_input failed";
        return false;
    }
    if(avformat_find_stream_info(state->format,nullptr)<0) {
        error_=name_+": avformat_find_stream_info failed";
        return false;
    }
    for(unsigned i=0;i<state->format->nb_streams;++i) {
        auto* stream=state->format->streams[i];
        if(stream->codecpar->codec_type==AVMEDIA_TYPE_VIDEO &&
           state->video_stream<0) {
            state->video_stream=static_cast<int>(i);
            state->width=static_cast<std::uint32_t>(stream->codecpar->width);
            state->height=static_cast<std::uint32_t>(stream->codecpar->height);
            auto* decoder=avcodec_find_decoder(stream->codecpar->codec_id);
            if(!decoder) {
                error_=name_+": no Bink video decoder in this libavcodec build";
                return false;
            }
            state->video=avcodec_alloc_context3(decoder);
            if(avcodec_parameters_to_context(state->video,stream->codecpar)<0 ||
               avcodec_open2(state->video,decoder,nullptr)<0) {
                error_=name_+": cannot open the Bink video decoder";
                return false;
            }
        }
        if(stream->codecpar->codec_type==AVMEDIA_TYPE_AUDIO &&
           state->audio_stream<0) {
            state->audio_stream=static_cast<int>(i);
            auto* decoder=avcodec_find_decoder(stream->codecpar->codec_id);
            if(!decoder) {
                error_=name_+": no Bink audio decoder in this libavcodec build";
                return false;
            }
            state->audio=avcodec_alloc_context3(decoder);
            if(avcodec_parameters_to_context(state->audio,stream->codecpar)<0 ||
               avcodec_open2(state->audio,decoder,nullptr)<0) {
                error_=name_+": cannot open the Bink audio decoder";
                return false;
            }
            state->audio_rate=static_cast<std::uint32_t>(state->audio->sample_rate);
        }
    }
    if(state->video_stream<0) {
        error_=name_+": no video stream in the Bink movie";
        return false;
    }
    state->scale=sws_getContext(
        static_cast<int>(state->width),static_cast<int>(state->height),
        state->video->pix_fmt,
        static_cast<int>(state->width),static_cast<int>(state->height),
        AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
    if(!state->scale) {
        error_=name_+": cannot scale Bink frames to RGBA";
        return false;
    }
    if(state->audio) {
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo,2);
        AVChannelLayout source=state->audio->ch_layout;
        if(source.order==AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&source,source.nb_channels);
        if(swr_alloc_set_opts2(&state->resample,
               &stereo,AV_SAMPLE_FMT_FLT,state->audio_rate,
               &source,static_cast<AVSampleFormat>(state->audio->sample_fmt),
               state->audio_rate,0,nullptr)<0 || swr_init(state->resample)<0) {
            error_=name_+": cannot resample Bink audio to stereo float";
            return false;
        }
    }
    state_=std::move(state);
    return true;
}

bool BinkPlayer::next_frame(BinkFrame& frame) {
    if(!state_) return false;
    auto* format=state_->format;
    AVPacket* packet=av_packet_alloc();
    AVFrame* decoded=av_frame_alloc();
    bool got_any=false;
    while(av_read_frame(format,packet)>=0) {
        int stream=packet->stream_index;
        if(stream==state_->video_stream) {
            if(avcodec_send_packet(state_->video,packet)==0) {
                while(avcodec_receive_frame(state_->video,decoded)==0) {
                    frame.width=state_->width;
                    frame.height=state_->height;
                    frame.seconds=decoded->pts*
                        av_q2d(format->streams[stream]->time_base);
                    frame.rgba.assign(
                        static_cast<std::size_t>(frame.width)*frame.height*4,0);
                    const std::uint8_t* src[4]={decoded->data[0],decoded->data[1],
                        decoded->data[2],nullptr};
                    const int src_stride[4]={decoded->linesize[0],
                        decoded->linesize[1],decoded->linesize[2],0};
                    int dst_stride[1]={static_cast<int>(frame.width*4)};
                    std::uint8_t* dst[1]={frame.rgba.data()};
                    sws_scale(state_->scale,src,src_stride,0,decoded->height,
                        dst,dst_stride);
                    ++state_->decoded_frames;
                    got_any=true;
                }
            }
        } else if(stream==state_->audio_stream) {
            if(avcodec_send_packet(state_->audio,packet)==0) {
                // Bink audio comes in fixed blocks; keep the whole track in
                // order, one block after another.
                while(avcodec_receive_frame(state_->audio,decoded)==0) {
                    auto in_frame=static_cast<std::size_t>(decoded->nb_samples);
                    auto before=state_->audio_samples.size();
                    state_->audio_samples.resize(before+in_frame*2);
                    const std::uint8_t* in[4]={decoded->data[0],decoded->data[1],
                        decoded->data[2],nullptr};
                    std::uint8_t* out[1]={reinterpret_cast<std::uint8_t*>(
                        state_->audio_samples.data()+before)};
                    swr_convert(state_->resample,out,static_cast<int>(in_frame),
                        in,in_frame);
                    // Bink RDFT audio can overshoot [-1,1] a little; clamp
                    // like the shipped sound path does at playback.
                    for(auto i=before;i<state_->audio_samples.size();++i)
                        state_->audio_samples[i]=
                            std::clamp(state_->audio_samples[i],-1.0f,1.0f);
                    av_frame_unref(decoded);
                }
            }
        }
        av_packet_unref(packet);
        if(got_any) break;
    }
    av_packet_free(&packet);
    av_frame_free(&decoded);
    return got_any;
}

bool BinkPlayer::audio(BinkAudio& out) {
    if(!state_ || !state_->audio) return false;
    out.rate=state_->audio_rate;
    out.channels=2; // Output is always interleaved stereo.
    out.samples=std::move(state_->audio_samples);
    state_->audio_samples.clear();
    return out.rate>0 && !out.samples.empty();
}

std::uint32_t BinkPlayer::width() const noexcept {
    return state_?state_->width:0;
}
std::uint32_t BinkPlayer::height() const noexcept {
    return state_?state_->height:0;
}
std::uint32_t BinkPlayer::frame_count() const noexcept {
    if(!state_) return 0;
    auto* stream=state_->format->streams[state_->video_stream];
    // The Bink demuxer stores the frame count as the stream duration in frame
    // ticks (one tick per frame); pts are frame indices.
    if(stream->duration>0) return static_cast<std::uint32_t>(stream->duration);
    if(stream->nb_frames>0) return static_cast<std::uint32_t>(stream->nb_frames);
    return state_->decoded_frames;
}
std::int64_t BinkPlayer::audio_samples() const noexcept {
    if(!state_) return 0;
    return static_cast<std::int64_t>(state_->audio_samples.size()/2);
}

bool play_bytes(std::vector<std::uint8_t> bytes, std::vector<BinkFrame>& frames,
    BinkAudio& audio_out, std::string name) {
    BinkPlayer player;
    if(!player.open(std::move(bytes),std::move(name))) return false;
    BinkFrame frame;
    while(player.next_frame(frame)) frames.push_back(std::move(frame));
    return player.audio(audio_out);
}

} // namespace ra2::video
