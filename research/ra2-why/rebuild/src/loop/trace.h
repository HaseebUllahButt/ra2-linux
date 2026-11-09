#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>

namespace ra2::loop::trace {
// One private log for SDL events and game-loop phases.
inline FILE* debug_log() {
    static FILE* log=[]()->FILE* {
        const char* value=std::getenv("RA2_DEBUG_LOOP");
        if(!value || !*value || *value=='0')return nullptr;
        char path[]="/tmp/ra2-debug-loop.XXXXXX";
        const int fd=mkstemp(path);if(fd<0)return nullptr;
        FILE* out=fdopen(fd,"w");
        if(!out){close(fd);return nullptr;}
        setvbuf(out,nullptr,_IOLBF,0);
        std::fprintf(stderr,"Debug loop log: %s\n",path);
        return out;
    }();
    return log;
}
inline void phase(const char* name,std::uint32_t frame,bool active,bool paused) {
    if(auto* log=debug_log()) {
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::fprintf(log,"Loop %lld frame=%u phase=%s active=%d paused=%d\n",
            static_cast<long long>(now),frame,name,active,paused);
    }
}
}
