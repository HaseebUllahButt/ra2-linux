#include "lan_session.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <algorithm>
namespace ra2::net {
namespace {
using Bytes=std::vector<std::uint8_t>;
struct Writer {
 Bytes b{'R','2','L',1};
 void u(std::uint32_t x){for(int i=0;i<4;++i)b.push_back(x>>(8*i));}
 void s(const std::string& x){if(x.size()>128)throw std::invalid_argument("LAN string too long");u(x.size());b.insert(b.end(),x.begin(),x.end());}
 void player(const Player& p){s(p.name);u(p.side);u(p.color);u(p.start_position);}
};
struct Reader {
 std::span<const std::uint8_t> b;std::size_t at=4;
 std::uint32_t u(){if(at+4>b.size())throw std::runtime_error("short packet");std::uint32_t x=0;for(int i=0;i<4;++i)x|=std::uint32_t(b[at++])<<(8*i);return x;}
 std::string s(){auto n=u();if(n>128 || at+n>b.size())throw std::runtime_error("bad string");std::string x(b.begin()+at,b.begin()+at+n);at+=n;return x;}
 Player player(){Player p;p.name=s();auto side=u(),color=u(),pos=u();if(side>255||color>255||pos>255)throw std::runtime_error("bad player");p.side=side;p.color=color;p.start_position=pos;return p;}
};
bool same(sockaddr_in a,sockaddr_in b){return a.sin_addr.s_addr==b.sin_addr.s_addr&&a.sin_port==b.sin_port;}
sockaddr_in endpoint(const std::string& ip,std::uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);if(inet_pton(AF_INET,ip.c_str(),&a.sin_addr)!=1)throw std::invalid_argument("IPv4 address required");return a;}
int socket_at(std::uint16_t port){int fd=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);if(fd<0)throw std::runtime_error("UDP socket");auto a=endpoint("example.invalid",port);if(bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)<0){close(fd);throw std::runtime_error("UDP bind");}return fd;}
void send(int fd,const sockaddr_in& a,const Bytes& b){sendto(fd,b.data(),b.size(),0,reinterpret_cast<const sockaddr*>(&a),sizeof a);}
Writer lobby_packet(const Lobby& l,int id){Writer w;w.u(3);w.u(id);w.s(l.map);w.u(l.started);w.u(l.players.size());for(auto& p:l.players)w.player(p);return w;}
Lobby read_lobby(Reader& r){Lobby l;l.map=r.s();l.started=r.u()!=0;auto n=r.u();if(!n||n>8)throw std::runtime_error("bad lobby");while(n--)l.players.push_back(r.player());return l;}
}
struct LanSession::Impl {
 int fd=-1,id=-1;bool host=false;Player me;Lobby lobby;sockaddr_in server{};
 std::vector<sockaddr_in> peers;
 std::unique_ptr<Lockstep> step;
 struct Saved {std::uint32_t frame;Bytes data;};std::vector<Saved> saved;
 std::chrono::steady_clock::time_point last{};
 ~Impl(){if(fd>=0)close(fd);}
 void broadcast(const Bytes& b){if(host){for(auto a:peers)send(fd,a,b);}else send(fd,server,b);}
 void remember(std::uint32_t f,Bytes b){saved.push_back({f,std::move(b)});}
 void trim(){if(step)std::erase_if(saved,[&](auto& x){return step->next_frame()>x.frame && step->next_frame()-x.frame>32;});}
};
LanSession::LanSession():impl_(std::make_unique<Impl>()){}
LanSession::~LanSession()=default;
std::unique_ptr<LanSession> LanSession::host(std::uint16_t port,Player p,std::string map){
 auto s=std::unique_ptr<LanSession>(new LanSession);auto& i=*s->impl_;i.fd=socket_at(port);i.host=true;i.id=0;i.me=p;i.lobby={std::move(map),{std::move(p)},false};return s;
}
std::unique_ptr<LanSession> LanSession::join(const std::string& ip,std::uint16_t port,Player p){
 auto s=std::unique_ptr<LanSession>(new LanSession);auto& i=*s->impl_;i.fd=socket_at(0);i.server=endpoint(ip,port);i.me=std::move(p);s->poll();return s;
}
const Lobby& LanSession::lobby()const{return impl_->lobby;}
int LanSession::player_id()const{return impl_->id;}
std::uint16_t LanSession::port()const{sockaddr_in a{};socklen_t n=sizeof a;getsockname(impl_->fd,reinterpret_cast<sockaddr*>(&a),&n);return ntohs(a.sin_port);}
Lockstep& LanSession::lockstep(){if(!impl_->step)throw std::logic_error("lobby not started");return *impl_->step;}
void LanSession::start(){auto& i=*impl_;if(!i.host||i.lobby.started)throw std::logic_error("host must start once");i.lobby.started=true;i.step=std::make_unique<Lockstep>(i.lobby.players.size());i.last={};poll();}
bool LanSession::send_orders(std::uint32_t frame,const std::vector<Event>& events){
 auto& i=*impl_;if(!i.step||!i.step->submit(i.id,frame,events))return false;
 Writer w;w.u(4);w.u(i.id);w.u(frame);w.u(events.size());for(auto& e:events){w.u(unsigned(e.type));w.b.insert(w.b.end(),e.data.begin(),e.data.end());}
 // A complete batch is immutable. Do not grow history on caller retransmissions.
 if(std::none_of(i.saved.begin(),i.saved.end(),[&](auto& x){return x.data==w.b;}))i.remember(frame,w.b);
 i.broadcast(w.b);return true;
}
void LanSession::send_crc(std::uint32_t f,std::uint32_t c){auto& i=*impl_;lockstep().checksum(i.id,f,c);Writer w;w.u(5);w.u(i.id);w.u(f);w.u(c);if(std::none_of(i.saved.begin(),i.saved.end(),[&](auto& x){return x.data==w.b;}))i.remember(f,w.b);i.broadcast(w.b);}
void LanSession::poll(){
 auto& i=*impl_;std::array<std::uint8_t,16000> buffer{};
 for(int count=0;count<256;++count){sockaddr_in from{};socklen_t n=sizeof from;auto size=recvfrom(i.fd,buffer.data(),buffer.size(),0,reinterpret_cast<sockaddr*>(&from),&n);if(size<0)break;
 if(size<8||buffer[0]!='R'||buffer[1]!='2'||buffer[2]!='L'||buffer[3]!=1)continue;
 try{Reader r{std::span(buffer.data(),size)};auto kind=r.u();
 if(kind==1&&i.host){send(i.fd,from,lobby_packet(i.lobby,-1).b);continue;}
 if(kind==2&&i.host){auto p=r.player();auto it=std::find_if(i.peers.begin(),i.peers.end(),[&](auto a){return same(a,from);});
 if(it==i.peers.end()){if(i.lobby.started||i.peers.size()>=7)continue;i.peers.push_back(from);i.lobby.players.push_back(p);i.last={};}continue;}
 if(!i.host&&!same(from,i.server))continue;
 if(kind==3&&!i.host){int id=r.u();auto l=read_lobby(r);if(id<1||id>=int(l.players.size()))continue;if(i.lobby.started)continue;i.id=id;i.lobby=std::move(l);if(i.lobby.started)i.step=std::make_unique<Lockstep>(i.lobby.players.size());continue;}
 if((kind==4||kind==5)&&i.step){auto p=r.u(),f=r.u();if(p>=i.lobby.players.size())continue;
 auto next=i.step->next_frame();if((f>next&&f-next>256)||(next>f&&next-f>256))continue;
 if(i.host && (p==0||!same(from,i.peers.at(p-1))))continue;
 bool accepted=true;
 if(kind==4){auto count_events=r.u();if(count_events>128)continue;std::vector<Event> events;
 while(count_events--){Event e;e.player=p;e.frame=f;auto type=r.u();if(type>46)throw std::runtime_error("bad event type");e.type=EventType(type);if(r.at+e.data.size()>r.b.size())throw std::runtime_error("short event");std::copy_n(r.b.begin()+r.at,e.data.size(),e.data.begin());r.at+=e.data.size();events.push_back(e);}if(r.at!=r.b.size())continue;accepted=i.step->submit(p,f,events);
 }else{auto crc=r.u();if(r.at!=r.b.size())continue;i.step->checksum(p,f,crc);}
 if(i.host&&accepted){Bytes b(buffer.begin(),buffer.begin()+size);if(std::none_of(i.saved.begin(),i.saved.end(),[&](auto& x){return x.data==b;}))i.remember(f,b);i.broadcast(b);}
 }
 }catch(const std::exception&){/* Ignore malformed UDP datagrams. */}
 }
 auto now=std::chrono::steady_clock::now();if(now-i.last<std::chrono::milliseconds(40))return;i.last=now;
 if(i.host){for(std::size_t p=0;p<i.peers.size();++p)send(i.fd,i.peers[p],lobby_packet(i.lobby,p+1).b);}
 else if(!i.lobby.started){Writer w;w.u(2);w.player(i.me);send(i.fd,i.server,w.b);}
 i.trim();for(auto& x:i.saved)i.broadcast(x.data);
 // TODO-ORIGINAL LAN transport: original packet format, timeout/leave, reconnect, lobby edits and host migration.
 // Retain 32 completed frames; a disconnected peer stalls progress instead of being silently removed.
}
std::vector<LanGame> LanSession::discover(std::uint16_t port,int timeout,const std::string& ip){
 if(timeout<0||timeout>10000)throw std::invalid_argument("discovery timeout");
 Impl socket;socket.fd=socket_at(0);int yes=1;setsockopt(socket.fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof yes);Writer w;w.u(1);send(socket.fd,endpoint(ip,port),w.b);
 std::vector<LanGame> out;auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
 do{std::array<std::uint8_t,2048>b{};sockaddr_in from{};socklen_t n=sizeof from;auto size=recvfrom(socket.fd,b.data(),b.size(),0,reinterpret_cast<sockaddr*>(&from),&n);
 if(size>=8&&b[0]=='R'&&b[1]=='2'&&b[2]=='L'&&b[3]==1)try{Reader r{std::span(b.data(),size)};if(r.u()==3){r.u();auto l=read_lobby(r);char ipbuf[INET_ADDRSTRLEN];inet_ntop(AF_INET,&from.sin_addr,ipbuf,sizeof ipbuf);if(std::none_of(out.begin(),out.end(),[&](auto& g){return g.address==ipbuf&&g.port==ntohs(from.sin_port);}))out.push_back({ipbuf,ntohs(from.sin_port),std::move(l)});}}catch(const std::exception&){}
 std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }while(std::chrono::steady_clock::now()<end);return out;
}
}
