#include "mix_bootstrap.h"
#include <iostream>
#include <fstream>
#include <map>
#include <iomanip>
using namespace ra2::fs;
unsigned u16(const std::vector<uint8_t>& b,size_t n){return b.at(n)|(b.at(n+1)<<8);}
unsigned u32(const std::vector<uint8_t>& b,size_t n){return u16(b,n)|(u16(b,n+2)<<16);}
int main(){
 MixFileSystem fs("/path/to/user/ra2/game"); fs.bootstrap();
 auto a=fs.mount("CONQUER.MIX");
 std::cout<<a->name()<<" entries "<<a->entries().size()<<"\n";
 std::ifstream names("scripts/binary_names.txt");std::string n;
 std::map<unsigned,unsigned> flags;
 while(std::getline(names,n)){
  if(auto e=a->find(n)){
   auto b=a->read(*e);
   std::cout<<n<<" size "<<b.size();
   if(b.size()>=8 && n.find(".SHP")!=std::string::npos){
    auto count=u16(b,6); std::cout<<" hdr "<<u16(b,0)<<","<<u16(b,2)<<","<<u16(b,4)<<","<<count;
    if(8+24*count<=b.size())for(unsigned i=0;i<count;++i)++flags[u32(b,8+24*i+8)];
    for(unsigned i=0;i<std::min(count,2u);++i){auto p=8+24*i;std::cout<<" frame["<<u16(b,p)<<","<<u16(b,p+2)<<","<<u16(b,p+4)<<","<<u16(b,p+6)<<" flags="<<u32(b,p+8)<<" ofs="<<u32(b,p+20)<<"]";}
   }
   std::cout<<"\n";
  }
 }
 for(auto x:flags)std::cout<<"flag "<<x.first<<" count "<<x.second<<"\n";
 for(const char* pal:{"UNITTEM.PAL","TEMPERAT.PAL","ANIM.PAL","CAMEO.PAL"}){
  if(auto b=fs.read_file(pal))std::cout<<pal<<" "<<b->size()<<"\n";
 }
}
