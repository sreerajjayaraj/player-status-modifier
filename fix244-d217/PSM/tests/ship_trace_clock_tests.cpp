#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include "logger.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace psm::shiptrace;
unsigned checks=0,failures=0;void Check(bool b,const char*n){++checks;if(!b){++failures;std::cerr<<"FAIL "<<n<<'\n';}}
template<size_t N,class T>void Put(std::array<unsigned char,N>&b,size_t a,T v){std::memcpy(b.data()+a,&v,sizeof v);}
template<class T>uintptr_t P(T&b){return reinterpret_cast<uintptr_t>(b.data());}
struct Actor {std::array<unsigned char,0x300>a{},t{},v{},r{},d{},s{},o{},entries{},meta{};Actor(uint32_t uid,uint8_t category){Put(a,0x60,uid);Put(a,0x68,P(t));Put(a,0x88,P(meta));meta[1]=category;Put(t,0x48,P(v));Put(v,0x18,P(r));Put(t,0x170,P(d));Put(t,0x20,P(s));Put(s,0x18,P(o));Put(o,0x58,P(entries));Put(entries,0,uint16_t{0});Put(entries,8,int64_t{300000});Put(entries,0x18,int64_t{500000});}};
std::atomic<uint64_t> now{1000};uint64_t ClockNow(){return now.load();}
size_t Count(const std::string&s,const std::string&needle){size_t n=0,at=0;while((at=s.find(needle,at))!=std::string::npos){++n;at+=needle.size();}return n;}
int main(){Actor player(10,1),ride(200,7);std::array<unsigned char,0x400>hud{};Put(player.r,0x10,uint32_t{200});Put(player.d,0x18,uint32_t{200});Put(hud,0x370,uint32_t{200});const auto player0=player,ride0=ride;const auto hud0=hud;
 {std::lock_guard lock(g_state_mutex);g_player_resolve.actor=P(player.a);}
 wchar_t host[32768]{};Check(GetModuleFileNameW(nullptr,host,32768)>0,"owned test image path");const auto path=std::filesystem::path(host).parent_path()/L"ship-clock-private-test.log";
 Check(InitializeLogger(path.wstring(),true,false,2000),"owned real trace logger");Enable(true,ClockNow);
 Put(player.r,0x10,uint32_t{0});Put(player.d,0x18,uint32_t{0});Put(hud,0x370,uint32_t{0});
 for(unsigned i=0;i<160;++i){now=1000+i*5001;ObserveActor(P(hud),P(player.a),0);ObserveQuery(P(ride.o),0,P(ride.entries));}
 Log("test-mark: first-coherent-context");now=60*60*1000;player=player0;hud=hud0;ObserveActor(P(hud),P(player.a),P(ride.a));ObserveQuery(P(ride.o),0,P(ride.entries));ObserveDamage(P(ride.o),0,-3,1,1);
 for(unsigned i=1;i<140;++i){now=60*60*1000+i*1001;ObserveQuery(P(ride.o),0,P(ride.entries));}
 Log("test-mark: hard-expiry");now=60*60*1000+900001;ObserveDamage(P(ride.o),0,-4,1,1);ObserveActor(P(hud),P(player.a),P(ride.a));ObserveQuery(P(ride.o),0,P(ride.entries));Enable(false);ShutdownLogger();
 std::ifstream in(path);std::string text((std::istreambuf_iterator<char>(in)),{});auto first=text.find("test-mark: first-coherent-context"),end=text.find("test-mark: hard-expiry");Check(first!=std::string::npos&&end!=std::string::npos,"test boundary markers recorded");
 Check(text.substr(0,first).find("ship-trace: identity generation=")==std::string::npos,"nonvehicle menu/loading samples consume no identity budget before arming");
 const auto active=text.substr(first,end-first);Check(Count(active,"ship-trace: capture armed")==1,"late coherent vehicle context arms capture exactly once after one hour idle");
 Check(active.find("ship-trace: identity generation=")!=std::string::npos&&active.find("native-status-receipt")!=std::string::npos,"actual late actor and damage observers remain available after long startup");
 Check(Count(active,"native-health-query")==128,"cold queries consume no per-kind budget; active query hard cap remains128");
 const auto expired=text.substr(end);Check(expired.find("native-status-receipt")==std::string::npos&&expired.find("native-health-query")==std::string::npos,"hard900second duration does not rearm on later context");
 const auto backwardPath=path.wstring()+L".backward";Check(InitializeLogger(backwardPath,true,false,2000),"owned backward-clock logger");now=9000000;Enable(true,ClockNow);ObserveActor(P(hud),P(player.a),P(ride.a));Log("test-mark: backward");now=8999999;ObserveQuery(P(ride.o),0,P(ride.entries));now=9000010;ObserveActor(P(hud),P(player.a),P(ride.a));ObserveDamage(P(ride.o),0,-9,1,1);Enable(false);ShutdownLogger();std::ifstream bi(backwardPath);std::string backward((std::istreambuf_iterator<char>(bi)),{});const auto phase=backward.substr(backward.find("test-mark: backward"));Check(phase.find("native-health-query")==std::string::npos&&phase.find("native-status-receipt")==std::string::npos,"backward clock terminates session without late rearming");
 Check(std::memcmp(&player,&player0,sizeof player)==0&&std::memcmp(&ride,&ride0,sizeof ride)==0&&hud==hud0,"clock and capture never mutate native fixtures");
 std::cout<<"ship diagnostic clock: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
