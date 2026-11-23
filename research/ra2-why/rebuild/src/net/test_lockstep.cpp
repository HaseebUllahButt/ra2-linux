#include "lan_session.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
using namespace ra2::net;
using namespace std::chrono_literals;
static void play(LanSession& s) {
 auto deadline=std::chrono::steady_clock::now()+10s;
 while(!s.lobby().started){s.poll();assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(2ms);}
 assert(s.lobby().map=="test-map" && s.lobby().players.size()==2);
 assert(s.lobby().players[1]==(Player{"Guest",1,3,2}));
 ra2::session::world::Snapshot world;
 ra2::session::world::House h;h.id=1;h.cash=100;world.houses.push_back(h);
 auto& step=s.lockstep();
 for(unsigned f=0;f<step.delayed_frame();++f)assert(s.send_orders(f,{}));
 unsigned queued=UINT32_MAX;
 while(step.next_frame()<60){
  auto f=step.delayed_frame();
  if(f!=queued){Event e;e.player=s.player_id();e.frame=f;e.type=EventType::Produce;e.data[0]=1+e.player;assert(s.send_orders(f,{e}));queued=f;}
  s.poll();
  if(step.ready()){auto frame=step.next_frame();for(auto& e:step.advance())*world.houses[0].cash+=e.data[0];s.send_crc(frame,world_crc(world));}
  assert(!step.desync());assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(2ms);
 }
 assert(*world.houses[0].cash==271);
 // Keep polling to exchange the final CRC, then inject a known mismatch.
 auto until=std::chrono::steady_clock::now()+300ms;
 while(std::chrono::steady_clock::now()<until){s.poll();assert(!step.desync());std::this_thread::sleep_for(2ms);}
 s.send_crc(60,s.player_id()==0?123:124);
 until=std::chrono::steady_clock::now()+300ms;
 while(std::chrono::steady_clock::now()<until){s.poll();std::this_thread::sleep_for(2ms);}
 assert(step.desync() && step.desync()->frame==60);
 assert(step.desync()->report().find("Desync frame=60")!=std::string::npos);
}
int main(){
 assert(frame_crc(std::array<std::uint8_t,4>{1,2,3,4})==0xb63cfbcdu);
 assert(frame_crc(std::array<std::uint8_t,1>{1})==frame_crc(std::array<std::uint8_t,4>{1,1,1,1}));
 Lockstep q(2);assert(q.delayed_frame()==3);assert(q.submit(1,0,{}));assert(!q.ready());assert(q.submit(0,0,{}));assert(q.ready());assert(q.advance().empty());assert(!q.submit(0,0,{}));
 Event e;e.player=0;e.frame=1;e.type=EventType::Deploy;assert(q.submit(0,1,{e}));assert(q.submit(0,1,{e}));e.data[0]=1;assert(!q.submit(0,1,{e}));assert(!q.submit(2,1,{}));assert(!q.submit(0,300,{}));
 bool threw=false;try{q.advance();}catch(const std::logic_error&){threw=true;}assert(threw);
 q.checksum(1,0,2);q.checksum(0,0,1);assert(q.desync());
 auto host=LanSession::host(0,{"Host",0,1,1},"test-map");auto port=host->port();
 pid_t child=fork();assert(child>=0);
 if(child==0){host.reset();auto games=LanSession::discover(port,150,"example.invalid");assert(games.size()==1&&games[0].lobby.map=="test-map");auto guest=LanSession::join("example.invalid",port,{"Guest",1,3,2});play(*guest);_exit(0);}
 auto deadline=std::chrono::steady_clock::now()+5s;
 while(host->lobby().players.size()!=2){host->poll();assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(2ms);}
 host->start();play(*host);int status=0;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 std::cout<<"Lockstep tests passed: two UDP processes, 60 frames, matching world CRCs, desync detected\n";
}
