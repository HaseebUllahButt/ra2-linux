#include "ra_bink.h"
#include <SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Movie {
    ra_bink api;
    unsigned char *bytes;
    size_t size,position;
    AVFormatContext *format;
    AVIOContext *io;
    AVCodecContext *video,*audio;
    AVFrame *frame;
    AVPacket *packet;
    struct SwsContext *scale;
    struct SwrContext *resample;
    int video_stream,audio_stream,decoded,paused,volume;
    uint64_t start,pause_start;
    SDL_AudioDeviceID device;
    FILE *capture;
} Movie;
static char error_text[256];
static int sound_enabled=1;
void ra_bink_sound(int enabled) { sound_enabled=enabled!=0; }
const char *ra_bink_error(void) { return error_text; }
static int error(const char *message) { snprintf(error_text,sizeof(error_text),"%s",message); return -1; }
static int read_bytes(void *context,uint8_t *out,int count)
{
    Movie *m=context;
    if (m->position>=m->size) return AVERROR_EOF;
    size_t take=(size_t)count;
    if (take>m->size-m->position) take=m->size-m->position;
    memcpy(out,m->bytes+m->position,take); m->position+=take; return (int)take;
}
static int64_t seek_bytes(void *context,int64_t offset,int whence)
{
    Movie *m=context;
    if (whence&AVSEEK_SIZE) return (int64_t)m->size;
    whence&=~AVSEEK_FORCE;
    if (whence==SEEK_CUR) offset+=(int64_t)m->position;
    else if (whence==SEEK_END) offset+=(int64_t)m->size;
    else if (whence!=SEEK_SET) return AVERROR(EINVAL);
    if (offset<0 || (uint64_t)offset>m->size) return AVERROR(EINVAL);
    m->position=(size_t)offset; return offset;
}
static AVCodecContext *decoder(AVStream *stream)
{
    const AVCodec *codec=avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return NULL;
    AVCodecContext *context=avcodec_alloc_context3(codec);
    if (!context) return NULL;
    if (avcodec_parameters_to_context(context,stream->codecpar)<0 || avcodec_open2(context,codec,NULL)<0) { avcodec_free_context(&context); return NULL; }
    return context;
}
void ra_bink_close(ra_bink *api)
{
    if (!api) return;
    Movie *m=(Movie *)api;
    if (m->device) { SDL_CloseAudioDevice(m->device); SDL_QuitSubSystem(SDL_INIT_AUDIO); }
    if (m->capture) fclose(m->capture);
    avcodec_free_context(&m->video); avcodec_free_context(&m->audio);
    av_frame_free(&m->frame); av_packet_free(&m->packet);
    if (m->format) avformat_close_input(&m->format);
    if (m->io) { av_freep(&m->io->buffer); avio_context_free(&m->io); }
    sws_freeContext(m->scale); swr_free(&m->resample);
    free(m->bytes); free(m);
}
ra_bink *ra_bink_open(unsigned char *bytes,size_t size)
{
    if (!bytes || size<44 || memcmp(bytes,"BIK",3)) { free(bytes); error("Not a complete Bink header"); return NULL; }
    Movie *m=calloc(1,sizeof(*m)); if (!m) { free(bytes); error("Out of memory"); return NULL; }
    m->bytes=bytes; m->size=size; m->video_stream=m->audio_stream=-1; m->volume=32768;
    m->format=avformat_alloc_context();
    unsigned char *buffer=av_malloc(32768);
    if (!m->format || !buffer) { av_free(buffer); goto fail; }
    m->io=avio_alloc_context(buffer,32768,0,m,read_bytes,NULL,seek_bytes);
    if (!m->io) { av_free(buffer); goto fail; }
    m->format->pb=m->io; m->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&m->format,NULL,av_find_input_format("bink"),NULL)<0) goto fail;
    for (unsigned i=0;i<m->format->nb_streams;i++) {
        AVStream *s=m->format->streams[i];
        if (s->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && m->video_stream<0) { m->video_stream=(int)i; m->video=decoder(s); }
        if (s->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && m->audio_stream<0) { m->audio_stream=(int)i; m->audio=decoder(s); }
    }
    if (!m->video) goto fail;
    m->api.Width=m->video->width; m->api.Height=m->video->height;
    /* The Bink container stores these fields as little-endian integers. */
    m->api.Frames=(uint32_t)bytes[8]|((uint32_t)bytes[9]<<8)|((uint32_t)bytes[10]<<16)|((uint32_t)bytes[11]<<24);
    m->api.FrameRate=(uint32_t)bytes[28]|((uint32_t)bytes[29]<<8)|((uint32_t)bytes[30]<<16)|((uint32_t)bytes[31]<<24);
    m->api.FrameRateDiv=(uint32_t)bytes[32]|((uint32_t)bytes[33]<<8)|((uint32_t)bytes[34]<<16)|((uint32_t)bytes[35]<<24);
    if (!m->api.FrameRate || !m->api.FrameRateDiv || !m->api.Frames || m->api.Width>16384 || m->api.Height>16384) goto fail;
    m->api.FrameNum=1;
    m->frame=av_frame_alloc(); m->packet=av_packet_alloc(); if (!m->frame || !m->packet) goto fail;
    if (m->audio) {
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&m->resample,&stereo,AV_SAMPLE_FMT_S16,m->audio->sample_rate,
            &m->audio->ch_layout,m->audio->sample_fmt,m->audio->sample_rate,0,NULL)<0 || swr_init(m->resample)<0) goto fail;
        if (sound_enabled) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) goto fail;
        SDL_AudioSpec spec={0}; spec.freq=m->audio->sample_rate; spec.format=AUDIO_S16LSB; spec.channels=2; spec.samples=512;
        m->device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        if (!m->device) { SDL_QuitSubSystem(SDL_INIT_AUDIO); goto fail; }
        SDL_PauseAudioDevice(m->device,0);
        }
        const char *capture=SDL_getenv("RA2_BINK_AUDIO_CAPTURE");
        if (capture && *capture) { m->capture=fopen(capture,"ab"); if (!m->capture) goto fail; }
    }
    m->start=SDL_GetTicks64();
    fprintf(stderr,"RA2 Bink: open width=%u height=%u frames=%u fps=%u/%u audio=%d\n",m->api.Width,m->api.Height,m->api.Frames,m->api.FrameRate,m->api.FrameRateDiv,m->audio!=NULL);
    error_text[0]=0; return &m->api;
fail:
    error("Cannot initialize the Bink decoder"); ra_bink_close(&m->api); return NULL;
}
static int decode_audio(Movie *m)
{
    if (!m->audio) return 0;
    if (avcodec_send_packet(m->audio,m->packet)<0) return error("Cannot send Bink audio packet");
    AVFrame *frame=av_frame_alloc(); if (!frame) return error("Out of memory");
    while (avcodec_receive_frame(m->audio,frame)==0) {
        int count=swr_get_out_samples(m->resample,frame->nb_samples);
        if (count<0 || count>INT_MAX/4) { av_frame_free(&frame); return error("Invalid audio frame size"); }
        uint8_t *pcm=av_malloc((size_t)count*4); if (!pcm) { av_frame_free(&frame); return error("Out of memory"); }
        int written=swr_convert(m->resample,&pcm,count,(const uint8_t **)frame->extended_data,frame->nb_samples);
        if (written<0) { av_free(pcm); av_frame_free(&frame); return error("Cannot convert Bink audio"); }
        int16_t *samples=(int16_t *)pcm;
        for (int i=0;i<written*2;i++) samples[i]=(int16_t)((int32_t)samples[i]*m->volume/32768);
        if (m->capture && fwrite(pcm,4,(size_t)written,m->capture)!=(size_t)written) { av_free(pcm); av_frame_free(&frame); return error("Cannot write Bink audio capture"); }
        if (m->device && SDL_QueueAudio(m->device,pcm,(Uint32)written*4)) { av_free(pcm); av_frame_free(&frame); return error("Cannot queue Bink audio"); }
        av_free(pcm); av_frame_unref(frame);
    }
    av_frame_free(&frame); return 0;
}
int ra_bink_do_frame(ra_bink *api)
{
    Movie *m=(Movie *)api;
    if (!m) return error("Null Bink handle");
    if (m->decoded) return 0;
    while (av_read_frame(m->format,m->packet)>=0) {
        int stream=m->packet->stream_index;
        int status=0;
        if (stream==m->audio_stream) status=decode_audio(m);
        if (stream==m->video_stream) {
            status=avcodec_send_packet(m->video,m->packet);
            av_packet_unref(m->packet);
            if (status<0 || avcodec_receive_frame(m->video,m->frame)<0) return error("Cannot decode Bink video frame");
            m->decoded=1; return 0;
        }
        av_packet_unref(m->packet); if (status<0) return status;
    }
    return error("End of Bink stream");
}
int ra_bink_copy(ra_bink *api,void *destination,int pitch,uint32_t height,uint32_t x,uint32_t y,uint32_t flags)
{
    Movie *m=(Movie *)api; enum AVPixelFormat format; unsigned bytes;
    if (!m || !destination || !m->decoded || pitch<=0) return error("Invalid Bink destination");
    switch (flags&15) {
    case 1: format=AV_PIX_FMT_BGR24; bytes=3; break;
    case 3: format=AV_PIX_FMT_BGRA; bytes=4; break;
    case 9: format=AV_PIX_FMT_RGB555LE; bytes=2; break;
    case 10: format=AV_PIX_FMT_RGB565LE; bytes=2; break;
    default: return error("Unsupported Bink pixel format");
    }
    if ((uint64_t)x+api->Width>(unsigned)pitch/bytes || (uint64_t)y+api->Height>height) return error("Bink copy exceeds destination");
    m->scale=sws_getCachedContext(m->scale,m->frame->width,m->frame->height,m->frame->format,
        m->frame->width,m->frame->height,format,SWS_POINT,NULL,NULL,NULL);
    if (!m->scale) return error("Cannot initialize Bink color conversion");
    uint8_t *output[4]={(uint8_t *)destination+(size_t)y*(unsigned)pitch+(size_t)x*bytes,NULL,NULL,NULL};
    int strides[4]={pitch,0,0,0};
    return sws_scale(m->scale,(const uint8_t *const *)m->frame->data,m->frame->linesize,0,m->frame->height,output,strides)<0 ? error("Cannot copy Bink frame") : 0;
}
void ra_bink_next(ra_bink *api)
{
    if (!api) return;
    Movie *m=(Movie *)api;
    api->LastFrameNum=api->FrameNum;
    if (api->FrameNum<api->Frames) api->FrameNum++;
    m->decoded=0; av_frame_unref(m->frame);
}
int ra_bink_goto(ra_bink *api,uint32_t frame)
{
    if (!api || !frame || frame>api->Frames) return error("Invalid Bink seek frame");
    Movie *m=(Movie *)api;
    int volume=m->volume,paused=m->paused;
    /* FFmpeg's Bink demuxer rewinds to frame one. Keep the audio device. */
    if (av_seek_frame(m->format,m->video_stream,0,AVSEEK_FLAG_BACKWARD)<0) return error("Cannot rewind Bink stream");
    avcodec_flush_buffers(m->video);
    if (m->audio) avcodec_flush_buffers(m->audio);
    av_frame_unref(m->frame); av_packet_unref(m->packet);
    if (m->resample) { swr_close(m->resample); if (swr_init(m->resample)<0) return error("Cannot reset Bink audio converter"); }
    api->FrameNum=1; api->LastFrameNum=0; m->decoded=0;
    FILE *capture=m->capture; m->capture=NULL;
    if (m->device) SDL_PauseAudioDevice(m->device,1);
    int status=0;
    for (uint32_t i=1;i<frame;i++) { if (ra_bink_do_frame(api)) { status=-1; break; } ra_bink_next(api); }
    m->capture=capture; m->volume=volume; m->paused=paused;
    if (m->device) { SDL_ClearQueuedAudio(m->device); SDL_PauseAudioDevice(m->device,paused); }
    m->pause_start=SDL_GetTicks64();
    m->start=SDL_GetTicks64()-(uint64_t)(frame-1)*1000*api->FrameRateDiv/api->FrameRate;
    return status;
}
int ra_bink_pause(ra_bink *api,int paused)
{
    Movie *m=(Movie *)api; if (!m) return 0;
    int old=m->paused;
    if (paused && !old) m->pause_start=SDL_GetTicks64();
    if (!paused && old) m->start+=SDL_GetTicks64()-m->pause_start;
    m->paused=paused!=0; if (m->device) SDL_PauseAudioDevice(m->device,m->paused); return old;
}
int ra_bink_wait(ra_bink *api)
{
    Movie *m=(Movie *)api; if (!m) return 0;
    uint64_t due=(uint64_t)(api->FrameNum-1)*1000*api->FrameRateDiv/api->FrameRate;
    int wait=m->paused || SDL_GetTicks64()-m->start<due;
    if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_PumpEvents();
    if (wait) SDL_Delay(1); return wait;
}
void ra_bink_volume(ra_bink *api,int volume)
{
    if (api) ((Movie *)api)->volume=volume<0 ? 0 : volume>32768 ? 32768 : volume;
}
