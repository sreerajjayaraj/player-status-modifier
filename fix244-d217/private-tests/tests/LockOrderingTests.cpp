#include <Windows.h>
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <immintrin.h>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
void Log(const char*,...){}uint64_t FakeClock(){return 1000;}
template<class T>uintptr_t P(T&a){return reinterpret_cast<uintptr_t>(a.data());}template<size_t N,class T>void Put(std::array<unsigned char,N>&a,size_t o,T v){std::memcpy(a.data()+o,&v,sizeof v);}
struct Actor{std::array<unsigned char,0x400>a{},t{},v{},r{},d{},s{},o{},e{},m{};Actor(uint32_t uid){Put(a,0x60,uid);Put(a,0x68,P(t));Put(a,0x88,P(m));m[1]=1;Put(t,0x48,P(v));Put(v,0x18,P(r));Put(t,0x170,P(d));Put(t,0x20,P(s));Put(s,0x18,P(o));Put(o,0x58,P(e));Put(e,0,uint16_t{0});Put(e,8,int64_t{100});Put(e,0x18,int64_t{1000});}};
int main(){using namespace psm::shiptrace;Actor player(10),ship(200);std::array<unsigned char,0x400>hud{},info{};Put(player.r,0x10,uint32_t{200});Put(hud,0x370,uint32_t{200});Put(info,8,uint16_t{16970});Put(info,0x40,uint32_t{0x4798223F});Put(info,0x0D,uint8_t{1});g_player_resolve.actor=P(player.a);g_player_resolve.marker=P(player.s);Enable(true,FakeClock);ObserveActor(P(hud),P(player.a),P(ship.a));ObserveVehicleInfo(P(hud),P(player.a),P(ship.a),P(info));ObserveQuery(P(ship.o),0,P(ship.e));
 const auto error=0x4123ABCul;const auto csr=_mm_getcsr();SetLastError(error);{std::lock_guard lock(g_state_mutex);ObserveQuery(P(ship.o),0,P(ship.e));ObserveActor(P(hud),P(player.a),P(ship.a));ObserveVehicleInfo(P(hud),P(player.a),P(ship.a),P(info));ObserveAuthoritativePlayer(P(player.a),P(player.s));int64_t out=777;
#ifdef PSM_OCCUPIED_SHIP_HEALING
 if(TryAdjustOccupiedShipHealthWrite(P(ship.e),101,100,&out)||out!=777)return 2;
#endif
 }if(GetLastError()!=error||_mm_getcsr()!=csr)return 3;Enable(false);std::printf("PASS actual observer/query/ship-writer paths under held state mutex return fail-closed; LastError/MXCSR preserved\n");return 0;}
