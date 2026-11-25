#include <Windows.h>
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include <array>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
static std::vector<std::string> logs;static uint64_t clockNow=1000;static unsigned checks=0,failures=0;
void Log(const char*f,...){char b[2048]{};va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}
void Check(bool x,const char*n){++checks;if(!x){++failures;std::printf("FAIL %s\n",n);}}
uint64_t FakeClock(){return clockNow;}template<class T>uintptr_t Ptr(T&x){return reinterpret_cast<uintptr_t>(x.data());}template<size_t N,class T>void Put(std::array<unsigned char,N>&a,size_t o,T v){std::memcpy(a.data()+o,&v,sizeof v);}
struct FakeActor{std::array<unsigned char,0x400>a{},table{},vehicle{},ride{},dock{},status{},owner{},entries{},meta{};FakeActor(uint32_t uid,uint8_t category){Put(a,0x60,uid);Put(a,0x68,Ptr(table));Put(a,0x88,Ptr(meta));meta[1]=category;Put(table,0x48,Ptr(vehicle));Put(vehicle,0x18,Ptr(ride));Put(table,0x170,Ptr(dock));Put(table,0x20,Ptr(status));Put(status,0x18,Ptr(owner));Put(owner,0x58,Ptr(entries));Put(entries,0,uint16_t{0});Put(entries,8,int64_t{300000});Put(entries,0x18,int64_t{500000});}};
void Tick(){
#ifdef PSM_SHIP_OBSERVER_DIAGNOSTICS
 psm::shiptrace::Heartbeat();
#endif
}
void Player(uintptr_t a,uintptr_t m){
#ifdef PSM_SHIP_OBSERVER_DIAGNOSTICS
 psm::shiptrace::NoteAuthoritativePlayerCapture();psm::shiptrace::ObserveAuthoritativePlayer(a,m);
#else
 (void)a;(void)m;
#endif
}
size_t Count(const char*part){size_t n=0;for(const auto&s:logs)if(s.find(part)!=std::string::npos)++n;return n;}
bool Contains(const char*part){return Count(part)>0;}

#define shiptrace legacy_shiptrace
#include "../legacy/runtime/ship_trace.h"
#undef shiptrace

int main(){FakeActor player(10,1),ride(200,7);std::array<unsigned char,0x400>hud{};Put(player.ride,0x10,uint32_t{200});Put(player.dock,0x18,uint32_t{200});Put(hud,0x370,uint32_t{200});const auto p=player;const auto h=hud;
 auto compare=[&](uintptr_t c,uintptr_t a,uintptr_t b,uintptr_t expected,bool nullOut){psm::shiptrace::Identity current{};psm::legacy_shiptrace::Identity old{};const auto x=psm::shiptrace::ReadIdentity(c,a,b,expected,nullOut?nullptr:&current),y=psm::legacy_shiptrace::ReadIdentity(c,a,b,expected,nullOut?nullptr:&old);Check(x==y,"actual old/new identity bool contract identical");if(x)Check(current.controller==old.controller&&current.player==old.player&&current.ride_actor==old.ride_actor&&current.ride_record==old.ride_record&&current.player_owner==old.player_owner&&current.ride_owner==old.ride_owner&&current.player_uid==old.player_uid&&current.ride_uid==old.ride_uid&&current.hud_uid==old.hud_uid&&current.player_dock_uid==old.player_dock_uid&&current.ride_dock_uid==old.ride_dock_uid&&current.player_category==old.player_category&&current.ride_category==old.ride_category,"actual old/new identity output fields identical");};
 compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(player.a),false);compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(player.a),true);compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),0,false);compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(ride.a),false);compare(1,Ptr(player.a),Ptr(ride.a),Ptr(player.a),false);compare(Ptr(hud),0,Ptr(ride.a),Ptr(player.a),false);compare(Ptr(hud),Ptr(player.a),1,Ptr(player.a),false);compare(Ptr(hud),Ptr(player.a),Ptr(player.a),Ptr(player.a),false);
 Put(hud,0x370,uint32_t{0});compare(Ptr(hud),Ptr(player.a),0,Ptr(player.a),false);compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(player.a),false);hud=h;Put(player.a,0x88,uintptr_t{1});compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(player.a),false);player=p;Put(player.table,0x170,uintptr_t{1});compare(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(player.a),false);player=p;Put(player.ride,0x10,uint32_t{0});Put(hud,0x370,uint32_t{0});compare(Ptr(hud),Ptr(player.a),0,Ptr(player.a),false);
 std::printf("old/new actual identity differential: %u checks %u failures; no gamecode\n",checks,failures);return failures?1:0;}

