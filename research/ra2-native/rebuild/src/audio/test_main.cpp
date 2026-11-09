#include "audio.h"
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace ra2::audio;
using B = std::vector<std::uint8_t>;
static void put(B& b, int p, unsigned v) { for (int i=0;i<4;i++) b[p+i] = (v >> (8*i)) & 255; }
template<class F> static void fails(F f) { bool caught=false; try { f(); } catch (const std::runtime_error&) { caught=true; } assert(caught); }
int main() {
    B idx(48); put(idx,0,0x41424147); put(idx,4,2); put(idx,8,1); idx[12]='T'; put(idx,32,2); put(idx,36,22050);
    auto bag=Bag::open(idx,{0,255}); auto p=bag.decode("t");
    assert(p.sample_rate==22050 && p.channels==1 && p.samples[0]==-32768 && p.samples[1]==32512);
    fails([&]{bag.decode("missing");});
    B bad=idx; bad[0]=0; fails([&]{Bag::open(bad,{0,255});});
    bad=idx; bad.pop_back(); fails([&]{Bag::open(bad,{0,255});});
    fails([&]{Bag::open(idx,{0});});
    bad=idx; put(bad,40,8); auto compressed=Bag::open(bad,{0,255}); fails([&]{compressed.decode("T");});
    B adidx=idx; put(adidx,32,8); put(adidx,40,8); put(adidx,44,8);
    B block(8); block[4]=0x11;
    auto adpcm=Bag::open(adidx,block).decode("T");
    assert((adpcm.samples == std::vector<std::int16_t>{0,1,2,2,2,2,2,2,2}));
    block[2]=89; auto invalid=Bag::open(adidx,block); fails([&]{invalid.decode("T");});
    B stereo_index=idx; put(stereo_index,32,20); put(stereo_index,40,9); put(stereo_index,44,1024);
    B stereo(20); stereo[2]=0; stereo[6]=0; stereo[8]=0x11; stereo[12]=0x22;
    auto partial=Bag::open(stereo_index,stereo).decode("T");
    assert(partial.channels==2 && partial.samples.size()==18);
    assert(partial.samples[0]==0 && partial.samples[1]==0 && partial.samples[2]==1 && partial.samples[3]==3);
    B badblock=stereo; badblock[6]=89;
    auto invalid_stereo=Bag::open(stereo_index,badblock);
    fails([&]{invalid_stereo.decode("T");});
    B aud(14); aud[0]=0x22; aud[1]=0x56; put(aud,2,2); put(aud,6,2); aud[12]=0; aud[13]=255;
    assert(decode_aud(aud).samples == p.samples); aud.pop_back(); fails([&]{decode_aud(aud);});
}
