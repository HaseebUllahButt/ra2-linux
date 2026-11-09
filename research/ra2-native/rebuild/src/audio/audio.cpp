#include "audio.h"
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <utility>

namespace ra2::audio {
namespace {
using Bytes = std::vector<std::uint8_t>;
std::uint32_t u32(const Bytes& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 4) throw std::runtime_error("truncated audio data");
    return std::uint32_t(b[p]) | (std::uint32_t(b[p+1]) << 8) | (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
}
std::uint16_t u16(const Bytes& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 2) throw std::runtime_error("truncated audio data");
    return std::uint16_t(b[p] | (unsigned(b[p+1]) << 8));
}
std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return r;
}
Pcm pcm(const Bytes& b, std::size_t p, std::size_t n, unsigned rate, unsigned channels, unsigned bits) {
    if (!rate || !channels || (bits != 8 && bits != 16) || n % (channels * (bits / 8))) throw std::runtime_error("invalid PCM format");
    if (p > b.size() || n > b.size() - p) throw std::runtime_error("truncated PCM data");
    if (channels > 2 || n / (bits / 8) > 128000000) throw std::runtime_error("PCM output too large");
    Pcm out{rate, static_cast<std::uint16_t>(channels), {}};
    out.samples.reserve(n / (bits / 8));
    for (std::size_t i = 0; i < n; i += bits / 8) {
        int sample = bits == 8 ? (int(b[p+i]) - 128) * 256 : int(u16(b, p+i));
        if (bits == 16 && sample >= 32768) sample -= 65536;
        out.samples.push_back(static_cast<std::int16_t>(sample));
    }
    return out;
}
// 0x00408610 recognizes WAVE format 0x11 and its block alignment.
Pcm ima(const Bytes& b, std::size_t p, std::size_t n, unsigned rate, unsigned channels, unsigned block) {
    static const int step[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    static const int adjust[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
    if (!rate || (channels != 1 && channels != 2) || block < 4*channels || p > b.size() || n > b.size()-p) throw std::runtime_error("invalid ADPCM format");
    // A short final block is valid. Only complete stereo channel groups
    // contain matched frames. 0x00401c00 reads the exact stored byte count.
    if (n > 32000000) throw std::runtime_error("ADPCM output too large");
    Pcm out{rate, static_cast<std::uint16_t>(channels), {}};
    while (n) {
        const std::size_t len = std::min<std::size_t>(n, block);
        if (len < 4 * channels) throw std::runtime_error("truncated ADPCM block header");
        int predictor[2] = {};
        int index[2] = {};
        std::vector<std::int16_t> channel[2];
        for (unsigned c = 0; c < channels; ++c) {
            predictor[c] = u16(b, p + 4 * c);
            if (predictor[c] >= 32768) predictor[c] -= 65536;
            index[c] = b[p + 4 * c + 2];
            if (index[c] > 88 || b[p + 4 * c + 3])
                throw std::runtime_error("invalid ADPCM block header");
            channel[c].push_back(static_cast<std::int16_t>(predictor[c]));
        }
        const std::size_t payload = len - 4 * channels;
        const std::size_t complete = channels == 1 ? payload : payload / 8 * 8;
        for (std::size_t k = 0; k < complete;) {
            for (unsigned c = 0; c < channels; ++c) {
                const std::size_t group = std::min<std::size_t>(4, complete - k);
                for (std::size_t j = 0; j < group; ++j) {
                    const auto byte = b[p + 4 * channels + k++];
                    for (unsigned h = 0; h < 2; ++h) {
                        const unsigned code = (byte >> (h * 4)) & 15;
                        const int st = step[index[c]];
                        int diff = st >> 3;
                        if (code & 1) diff += st >> 2;
                        if (code & 2) diff += st >> 1;
                        if (code & 4) diff += st;
                        predictor[c] = std::clamp(predictor[c] + ((code & 8) ? -diff : diff), -32768, 32767);
                        index[c] = std::clamp(index[c] + adjust[code], 0, 88);
                        channel[c].push_back(static_cast<std::int16_t>(predictor[c]));
                    }
                }
            }
        }
        for (std::size_t i = 0; i < channel[0].size(); ++i)
            for (unsigned c = 0; c < channels; ++c) out.samples.push_back(channel[c][i]);
        p += len;
        n -= len;
    }
    return out;
}

}
Bag Bag::open(Bytes index, Bytes bag) {
    // 0x004011c0 reads a 12-byte header and version 1 or 2 records.
    if (index.size() < 12 || u32(index, 0) != 0x41424147) throw std::runtime_error("invalid audio index header");
    auto version = u32(index, 4), count = u32(index, 8);
    if (version != 1 && version != 2) throw std::runtime_error("unsupported audio index version");
    std::size_t stride = version == 1 ? 32 : 36;
    if (count > (index.size() - 12) / stride) throw std::runtime_error("truncated audio index");
    Bag out; out.bag_ = std::move(bag);
    for (std::size_t i = 0; i < count; ++i) {
        auto p = 12 + i * stride;
        std::size_t len = 0; while (len < 16 && index[p+len]) ++len;
        if (!len) throw std::runtime_error("empty audio name");
        Entry e{upper(std::string_view(reinterpret_cast<const char*>(index.data()+p),len)), u32(index,p+16), u32(index,p+20), u32(index,p+24), u32(index,p+28), version == 2 ? u32(index,p+32) : 0};
        if (e.offset > out.bag_.size() || e.size > out.bag_.size() - e.offset) throw std::runtime_error("audio entry exceeds bag");
        out.entries_.push_back(std::move(e));
    }
    return out;
}
Pcm Bag::decode(std::string_view name) const {
    auto key = upper(name);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e){ return e.name == key; });
    if (it == entries_.end()) throw std::runtime_error("audio sample not found");
    // 0x00401640 maps flags 1, 4, and 8 to channels, sample width, and compression.
    if (it->flags & ~std::uint32_t(15)) throw std::runtime_error("unsupported bag audio flags");
    if (it->flags & 8) return ima(bag_, it->offset, it->size, it->rate, (it->flags & 1) ? 2 : 1, it->extra);
    if (it->flags & ~std::uint32_t(7)) throw std::runtime_error("unsupported bag audio flags");
    return pcm(bag_, it->offset, it->size, it->rate, (it->flags & 1) ? 2 : 1, (it->flags & 4) ? 16 : 8);
}
Pcm decode_wav(const Bytes& b) {
    if (b.size()<12 || u32(b,0)!=0x46464952 || u32(b,8)!=0x45564157 || u32(b,4)<4 || u32(b,4)>b.size()-8) throw std::runtime_error("invalid WAVE header");
    std::size_t end=8+u32(b,4), p=12, data=0, size=0;
    unsigned format=0, channels=0, rate=0, bits=0, block=0;
    bool have_fmt=false, have_data=false, have_fact=false;
    std::uint32_t frames=0;
    while(p<end) {
        if(end-p<8) throw std::runtime_error("truncated WAVE chunk header");
        auto id=u32(b,p), len=u32(b,p+4); p+=8;
        if(len>end-p) throw std::runtime_error("truncated WAVE chunk");
        if(id==0x20746d66 && !have_fmt) {
            if(len<16) throw std::runtime_error("truncated WAVE format");
            format=u16(b,p); channels=u16(b,p+2); rate=u32(b,p+4); block=u16(b,p+12); bits=u16(b,p+14); have_fmt=true;
        } else if(id==0x74636166 && !have_fact) {
            if(len<4) throw std::runtime_error("truncated WAVE fact");
            frames=u32(b,p); have_fact=true;
        } else if(id==0x61746164 && !have_data) {data=p;size=len;have_data=true;}
        p+=len; if(len&1) {if(p==end) throw std::runtime_error("missing WAVE padding"); ++p;}
    }
    if(!have_fmt || !have_data) throw std::runtime_error("missing WAVE chunk");
    if(format==1) return pcm(b,data,size,rate,channels,bits);
    if(format==0x11 && bits==4) {
        auto result=ima(b,data,size,rate,channels,block);
        if(have_fact) {
            if(frames > result.samples.size()/channels) throw std::runtime_error("invalid WAVE sample count");
            result.samples.resize(std::size_t(frames)*channels);
        }
        return result;
    }
    throw std::runtime_error("unsupported WAVE compression");
}
Pcm decode_aud(const Bytes& b) {
    if (b.size() < 12) throw std::runtime_error("truncated AUD header");
    unsigned rate = u16(b,0), flags = b[10], codec = b[11];
    auto packed = u32(b,2), unpacked = u32(b,6);
    if (packed > b.size()-12) throw std::runtime_error("truncated AUD data");
    if (flags & ~3u) throw std::runtime_error("unsupported AUD flags");
    if (codec != 0) throw std::runtime_error("unsupported AUD compression");
    if (packed != unpacked) throw std::runtime_error("invalid uncompressed AUD length");
    return pcm(b, 12, packed, rate, (flags & 1) ? 2 : 1, (flags & 2) ? 16 : 8);
}
}
