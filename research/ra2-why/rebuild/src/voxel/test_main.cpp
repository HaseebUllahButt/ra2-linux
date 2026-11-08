#include "voxel.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ra2::voxel;
int main(){try{ra2::fs::MixFileSystem fs("/path/to/user/ra2/game");fs.bootstrap();fs.mount("conquer.mix",false);fs.mount("conqmd.mix",false);auto get=[&](const char* n){auto b=fs.read_file(n);if(!b)throw std::runtime_error(std::string("missing ")+n);return *b;};auto vb=get("htnk.vxl"),hb=get("htnk.hva"),pb=get("voxels.vpl");auto v=read_vxl(vb);auto h=read_hva(hb);auto p=read_vpl(pb);if(v.sections.empty()||v.sections[0].points.empty()||h.frames!=1||p.tables.size()!=32)throw std::runtime_error("asset mismatch");auto image=draw(v,h,p,0,16,128,128,0,2);if(std::none_of(image.pixels.begin(),image.pixels.end(),[](auto x){return x.a!=0;}))throw std::runtime_error("empty image");for(auto* b:{&vb,&hb,&pb}){auto copy=*b;copy.resize(8);bool rejected=false;try{if(b==&vb)(void)read_vxl(copy);else if(b==&hb)(void)read_hva(copy);else (void)read_vpl(copy);}catch(const std::runtime_error&){rejected=true;}if(!rejected)throw std::runtime_error("accepted truncated file");}std::cout<<"HTNK VXL/HVA, VOXELS VPL, drawing and truncation passed; points="<<v.sections[0].points.size()<<'\n';}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
