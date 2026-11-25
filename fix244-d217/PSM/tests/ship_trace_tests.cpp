#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include "logger.h"
#include <filesystem>
#include <fstream>
#include <Windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <vector>
using namespace psm::shiptrace;
int checks=0; void Check(bool b,const char*n){++checks;if(!b){std::cerr<<"FAIL "<<n<<'\n';std::exit(1);}}
template<size_t N,class T>void Put(std::array<unsigned char,N>&b,size_t a,T v){std::memcpy(b.data()+a,&v,sizeof v);}
template<class T>uintptr_t P(T&b){return reinterpret_cast<uintptr_t>(b.data());}
struct Actor {std::array<unsigned char,0x300>a{},t{},v{},r{},d{},s{},o{},entries{},meta{};Actor(uint32_t uid,uint8_t category){Put(a,0x60,uid);Put(a,0x68,P(t));Put(a,0x88,P(meta));meta[1]=category;Put(t,0x48,P(v));Put(v,0x18,P(r));Put(t,0x170,P(d));Put(t,0x20,P(s));Put(s,0x18,P(o));Put(o,0x58,P(entries));Put(entries,0,uint16_t{0});Put(entries,8,int64_t{300000});Put(entries,0x18,int64_t{500000});}};
int main(){Actor player(10,1),ride(200,7),other(300,1);std::array<unsigned char,0x400>hud{};std::array<unsigned char,0x100>info{};Put(player.r,0x10,uint32_t{200});Put(player.d,0x18,uint32_t{200});Put(ride.d,0x18,uint32_t{400});Put(hud,0x370,uint32_t{200});const auto p0=player, r0=ride;const auto h0=hud;Identity id{};
 Check(ReadIdentity(P(hud),P(player.a),P(ride.a),P(player.a),&id),"production typed identity captures coherent current ride");
 Check(id.player_uid==10&&id.ride_uid==200&&id.hud_uid==200&&id.player_dock_uid==200&&id.ride_dock_uid==400,"typed UID and docking fields retained without operation claim");
 Check(id.player_category==1&&id.ride_category==7&&id.ride_owner==P(ride.o),"categories and native status-owner relation captured");
 Check(std::memcmp(&player,&p0,sizeof player)==0&&std::memcmp(&ride,&r0,sizeof ride)==0&&hud==h0,"identity capture never mutates native objects");
 Identity junk{};Check(!ReadIdentity(P(hud),P(player.a),P(ride.a),P(other.a),&junk),"foreign current player fails closed");Check(!ReadIdentity(1,P(player.a),P(ride.a),P(player.a),&junk),"unreadable HUD fails closed");
 Put(hud,0x370,uint32_t{201});Check(!ReadIdentity(P(hud),P(player.a),P(ride.a),P(player.a),&junk),"stale HUD UID rejected");hud=h0;
 Put(ride.a,0x60,uint32_t{201});Check(!ReadIdentity(P(hud),P(player.a),P(ride.a),P(player.a),&junk),"reused ride actor identity rejected");ride=r0;
 Health h{};Check(ReadHealth(id,P(ride.o),0,P(ride.entries),&h)&&h.value==300000&&h.maximum==500000,"native health query binds exact ride owner and entry");
 Check(!ReadHealth(id,P(other.o),0,P(other.entries),&h),"unrelated NPC owner not classified as ride health");Check(!ReadHealth(id,P(ride.o),19,P(ride.entries),&h),"nonhealth selector not labelled hull health");Check(!ReadHealth(id,P(ride.o),0,P(player.entries),&h),"foreign resource entry rejected");
 Put(ride.o,0x58,P(other.entries));Check(!ReadHealth(id,P(ride.o),0,P(ride.entries),&h),"reallocated owner entries rejected");ride=r0;
 Put(player.r,0x10,uint32_t{0});Put(hud,0x370,uint32_t{0});Identity left{};Check(ReadIdentity(P(hud),P(player.a),0,P(player.a),&left)&&left.ride_uid==0&&!left.ride_actor,"seat leave captured without retaining old ride");Check(!ReadHealth(id,P(ride.o),0,P(ride.entries),&h),"retained health invalidated after leave");player=p0;hud=h0;
 Put(player.d,0x18,uint32_t{999});Check(!ReadHealth(id,P(ride.o),0,P(ride.entries),&h),"changed docking relation invalidates old packet");player=p0;
 Put(info,8,uint16_t{16970});Put(info,0x40,uint32_t{1234});VehicleInfo vi{};Check(ReadVehicleInfo(id,P(info),&vi)&&vi.external_key==16970&&vi.type_hash==1234,"already-resolved VehicleInfo uses verified native fields");Check(!ReadVehicleInfo(id,1,&vi),"invalid native metadata pointer fails closed");
 Put(player.a,0x68,uintptr_t{1});Check(!ReadIdentity(P(hud),P(player.a),P(ride.a),P(player.a),&junk),"unreadable chain fails closed");player=p0;
 Budget budget;Check(budget.Take(1000)&&!budget.Take(1001),"production budget rate limits unchanged packet");Check(budget.Take(1001,true),"identity transition may bypass interval within cap");for(unsigned i=0;i<Budget::limit;++i)budget.Take(2000+i*1000,true);Check(!budget.Take(999999,true),"production budget hard cap");
 std::vector<std::thread>threads;std::atomic<unsigned>success{0};for(unsigned i=0;i<8;++i)threads.emplace_back([&]{for(unsigned k=0;k<100;++k){Identity x{};if(ReadIdentity(P(hud),P(player.a),P(ride.a),P(player.a),&x))++success;}});for(auto&t:threads)t.join();Check(success==800,"parallel immutable native observations are consistent");
 {std::lock_guard lock(g_state_mutex);g_player_resolve.actor=P(player.a);}
 wchar_t hostPath[32768]{};Check(GetModuleFileNameW(nullptr,hostPath,32768)>0,"private test path");const auto tracePath=std::filesystem::path(hostPath).parent_path()/L"ship-trace-private-test.log";Check(InitializeLogger(tracePath.wstring(),true,false,2000),"real private diagnostic logger");
 Enable(true);ObserveActor(P(hud),P(player.a),P(ride.a));ObserveVehicleInfo(P(hud),P(player.a),P(ride.a),P(info));ObserveQuery(P(ride.o),0,P(ride.entries));
 void*raw=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);Check(raw!=nullptr,"private source guard allocation");DWORD prior=0;Check(VirtualProtect(raw,4096,PAGE_READWRITE|PAGE_GUARD,&prior)!=0,"private raw source is guarded");
 ObserveDamage(P(ride.o),0,-100,reinterpret_cast<uintptr_t>(raw),0x123456);MEMORY_BASIC_INFORMATION mbi{};Check(VirtualQuery(raw,&mbi,sizeof mbi)!=0&&(mbi.Protect&PAGE_GUARD),"actual receipt observer never dereferences uninterpreted source context");VirtualFree(raw,0,MEM_RELEASE);
 ObserveWriter(P(ride.entries),290000,0x123456);
 Log("test-mark: stale-phase-begin");Put(player.r,0x10,uint32_t{0});Put(hud,0x370,uint32_t{0});ObserveQuery(P(ride.o),0,P(ride.entries));ObserveDamage(P(ride.o),0,-5,1,1);ObserveWriter(P(ride.entries),299995,1);Log("test-mark: stale-phase-end");ShutdownLogger();
 std::ifstream traceFile(tracePath);std::string text((std::istreambuf_iterator<char>(traceFile)),{});const auto at=text.find("test-mark: stale-phase-begin"),end=text.find("test-mark: stale-phase-end");const auto stale=text.substr(at,end-at);const auto invalid=stale.find("identity invalidated");Check(invalid!=std::string::npos&&stale.find("identity invalidated",invalid+1)==std::string::npos,"real query-only leave emits one lifecycle invalidation");Check(stale.find("native-health-query")==std::string::npos&&stale.find("native-status-receipt")==std::string::npos&&stale.find("native-pre-notification-writer")==std::string::npos,"query damage writer emit no stale packet after invalidation");player=p0;hud=h0;
 const auto deckPath=tracePath.wstring()+L".deck";Check(InitializeLogger(deckPath,true,false,2000),"private occupied-deck log");Put(player.r,0x10,uint32_t{0});Put(hud,0x370,uint32_t{0});Enable(true);ObserveActor(P(hud),P(player.a),0);ObserveDamage(P(other.o),0,-7,1,1);Log("test-mark: character-switch-begin");{std::lock_guard lock(g_state_mutex);g_player_resolve.actor=P(other.a);}ObserveDamage(P(other.o),0,-8,1,1);ObserveWriter(P(ride.entries),1,1);Enable(false);ShutdownLogger();std::ifstream deckFile(deckPath);std::string deck((std::istreambuf_iterator<char>(deckFile)),{});Check(deck.find("native-status-receipt generation=1 rideUID=0 playerDockParentUID=200")!=std::string::npos,"actual observer records occupied deck context without helm restriction");const auto switched=deck.substr(deck.find("test-mark: character-switch-begin"));Check(switched.find("reason=current-player-changed")!=std::string::npos&&switched.find("native-status-receipt")==std::string::npos&&switched.find("native-pre-notification-writer")==std::string::npos,"actual character switch clears stale receipt and writer state");{std::lock_guard lock(g_state_mutex);g_player_resolve.actor=P(player.a);}player=p0;hud=h0;
 Enable(true);threads.clear();for(unsigned i=0;i<8;++i)threads.emplace_back([&]{for(unsigned j=0;j<100;++j){ObserveActor(P(hud),P(player.a),P(ride.a));ObserveQuery(P(ride.o),0,P(ride.entries));}});for(auto&t:threads)t.join();Enable(false);ObserveActor(1,1,1);ObserveDamage(1,0,-1,1,1);ObserveWriter(1,1,1);
 Check(std::memcmp(&player,&p0,sizeof player)==0&&std::memcmp(&ride,&r0,sizeof ride)==0&&hud==h0,"all production observations remain passive");
 std::cout<<"PASS "<<checks<<" ship diagnostic capture checks; no modifiers\n";
}
