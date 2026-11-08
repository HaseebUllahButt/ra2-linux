#include "voxel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace ra2::voxel {
namespace {
void check(const Bytes& b,std::size_t p,std::size_t n){if(p>b.size()||n>b.size()-p)throw std::runtime_error("truncated voxel file");}
std::uint32_t u32(const Bytes& b,std::size_t p){check(b,p,4);return std::uint32_t(b[p])|std::uint32_t(b[p+1])<<8|std::uint32_t(b[p+2])<<16|std::uint32_t(b[p+3])<<24;}
float f32(const Bytes& b,std::size_t p){auto v=u32(b,p);float f;std::memcpy(&f,&v,4);if(!std::isfinite(f))throw std::runtime_error("invalid float");return f;}
std::string name(const Bytes& b,std::size_t p){check(b,p,16);std::size_t n=0;while(n<16&&b[p+n])++n;return std::string(reinterpret_cast<const char*>(b.data()+p),n);}
}
Model read_vxl(const Bytes& b){
 check(b,0,32);if(std::memcmp(b.data(),"Voxel Animation",15))throw std::runtime_error("invalid VXL signature");
 auto palettes=u32(b,16), heads=u32(b,20), tails=u32(b,24), body_size=u32(b,28);
 if(!heads||heads!=tails||heads>1024||palettes>256)throw std::runtime_error("invalid VXL counts");
 std::size_t header=32+std::size_t(palettes)*770;check(b,header,std::size_t(heads)*28);std::size_t body=header+std::size_t(heads)*28;check(b,body,body_size);std::size_t tail=body+body_size;check(b,tail,std::size_t(tails)*92);
 Model m;
 for(std::size_t s=0;s<heads;++s){auto h=header+28*s, t=tail+92*s; Section section{};section.name=name(b,h); section.scale=f32(b,t+12);for(int i=0;i<3;++i){section.min[i]=f32(b,t+64+4*i);section.max[i]=f32(b,t+76+4*i);} section.x=b[t+88];section.y=b[t+89];section.z=b[t+90];section.normal_type=b[t+91];
  if(!section.x||!section.y||!section.z)throw std::runtime_error("invalid VXL dimensions");
  std::size_t count=std::size_t(section.x)*section.y; auto starts=u32(b,t), ends=u32(b,t+4), data=u32(b,t+8);
  if(starts>body_size||ends>body_size||data>body_size||count*4>body_size-starts||count*4>body_size-ends)throw std::runtime_error("invalid VXL tables");
  for(std::size_t i=0;i<count;++i){auto a=u32(b,body+starts+i*4),z=u32(b,body+ends+i*4);if(a==0xffffffff && z==0xffffffff)continue;if(a>z||z>=body_size-data)throw std::runtime_error("invalid VXL span");std::size_t p=body+data+a, end=body+data+z+1; unsigned height=0;
   while(p<end){if(end-p<2)throw std::runtime_error("truncated VXL span");height+=b[p++];unsigned n=b[p++];if(height+n>section.z||end-p<std::size_t(n)*2+1)throw std::runtime_error("invalid VXL run");for(unsigned j=0;j<n;++j){section.points.push_back({std::uint8_t(i%section.x),std::uint8_t(i/section.x),std::uint8_t(height++),b[p],b[p+1]});p+=2;}if(b[p++]!=n)throw std::runtime_error("invalid VXL run end");}
  }m.sections.push_back(std::move(section));
 }return m;
}
Animation read_hva(const Bytes& b){check(b,0,24);Animation a{};a.frames=u32(b,16);auto sections=u32(b,20);if(!a.frames||!sections||sections>1024||a.frames>100000||std::uint64_t(a.frames)*sections>1000000)throw std::runtime_error("invalid HVA counts");std::size_t p=24;check(b,p,std::size_t(sections)*16);for(unsigned i=0;i<sections;++i)a.names.push_back(name(b,p+i*16));p+=std::size_t(sections)*16;check(b,p,std::size_t(a.frames)*sections*48);a.matrices.resize(std::size_t(a.frames)*sections);for(auto& mat:a.matrices)for(auto& x:mat){x=f32(b,p);p+=4;}return a;}
Lighting read_vpl(const Bytes& b){check(b,0,784);auto levels=u32(b,8);if(!levels||levels>65536)throw std::runtime_error("invalid VPL count");check(b,784,std::size_t(levels)*256);Lighting l{ra2::gfx::Palette::from_bytes(b.data()+16,768),{}};l.tables.resize(levels);for(unsigned i=0;i<levels;++i)std::copy_n(b.begin()+784+std::size_t(i)*256,256,l.tables[i].begin());return l;}
Image draw(const Model& m,const Animation& a,const Lighting& l,unsigned frame,unsigned level,unsigned width,unsigned height,float angle,float zoom){
 if(std::uint64_t(a.frames)*a.names.size()!=a.matrices.size()||frame>=a.frames||level>=l.tables.size()||std::uint64_t(width)*height>100000000||!std::isfinite(angle)||!std::isfinite(zoom)||zoom<=0)throw std::runtime_error("invalid view");
 Image image{width,height,std::vector<ra2::gfx::Rgba>(std::size_t(width)*height,{0,0,0,0})};std::vector<float> depth(image.pixels.size(),-std::numeric_limits<float>::infinity());float c=std::cos(angle),s=std::sin(angle);
 for(const auto& sec:m.sections){auto it=std::find(a.names.begin(),a.names.end(),sec.name);if(it==a.names.end())continue;auto idx=std::size_t(it-a.names.begin());const auto& mat=a.matrices[std::size_t(frame)*a.names.size()+idx];for(auto v:sec.points){float x=sec.min[0]+(sec.max[0]-sec.min[0])*(v.x+0.5f)/sec.x,y=sec.min[1]+(sec.max[1]-sec.min[1])*(v.y+0.5f)/sec.y,z=sec.min[2]+(sec.max[2]-sec.min[2])*(v.z+0.5f)/sec.z;float X=mat[0]*x+mat[1]*y+mat[2]*z+mat[3],Y=mat[4]*x+mat[5]*y+mat[6]*z+mat[7],Z=mat[8]*x+mat[9]*y+mat[10]*z+mat[11];float rx=c*X-s*Y,ry=s*X+c*Y;float px=width/2.f+rx*zoom,py=height/2.f+(ry*0.5f-Z)*zoom;if(!std::isfinite(px)||!std::isfinite(py)||px<0||py<0||px>=width||py>=height)continue;auto index=std::size_t(py)*width+std::size_t(px);float d=ry+Z*0.5f;if(d>depth[index]){depth[index]=d;image.pixels[index]=l.palette[l.tables[level][v.color]];}}}return image;
}
}
