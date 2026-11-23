#include "homing_speed.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ra2::weapons;
// Stream driver for bit-exact x86 comparisons, including repeated ticks.
int main(int argc,char** argv) {
    if (argc<2 || argc>3) return 2;
    try {
        const auto stage=BaseHomingSpeed::from_executable(argv[1]);
        if (argc==3) {
            if (std::string(argv[2])!="--self-test") return 2;
            // Exact results also checked against game.exe at 0045F8FF.
            HomingSpeedState s{{1,0,0},40,1,0};
            stage.tick(s,{3,0,0});
            std::uint64_t bits; std::memcpy(&bits,&s.velocity[0],8);
            if (bits!=4616189618054758400ULL || s.flag!=0 || s.counter!=0)
                throw std::runtime_error("homing speed check failed");
            s={{0,0,0},40,1,0};
            stage.tick(s,{3,0,0});
            std::memcpy(&bits,&s.velocity[0],8);
            if (bits!=4613937818241073151ULL || s.flag!=0 || s.counter!=0)
                throw std::runtime_error("zero-speed check failed");
            std::cout<<"Homing speed checks passed\n";
            return 0;
        }
        HomingSpeedState state;
        HomingSpeedInput input;
        unsigned flag; int ticks;
        while (std::cin>>state.velocity[0]>>state.velocity[1]>>state.velocity[2]
               >>state.limit>>flag>>state.counter>>input.acceleration>>input.threshold>>input.frame>>ticks) {
            if (flag>255 || ticks<1 || ticks>1000) return 2;
            state.flag=static_cast<std::uint8_t>(flag);
            for (int i=0;i<ticks;++i) {
                stage.tick(state,input);
                for (double v:state.velocity) {
                    std::uint64_t bits; std::memcpy(&bits,&v,8);
                    std::cout<<bits<<' ';
                }
                std::cout<<unsigned(state.flag)<<' '<<state.counter<<' '<<state.limit<<'\n';
                // Wrap exactly like the original global frame counter.
                input.frame=input.frame==INT32_MAX ? INT32_MIN : input.frame+1;
            }
        }
        return std::cin.eof() ? 0 : 2;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
