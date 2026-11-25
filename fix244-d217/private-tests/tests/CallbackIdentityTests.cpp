// The old observer counted expected-player-mismatch but discarded all callback
// arguments. These behavior regressions require the worker report to retain the
// actual native operands while refusing to bind or mutate either actor.
#include <Windows.h>
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include <array>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
static std::vector<std::string> logs;static uint64_t now=1000;static unsigned checks=0,failures=0;
void Log(const char*f,...){char b[2048]{};va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}
void Check(bool x,const char*n){++checks;if(!x){++failures;std::printf("FAIL %s\n",n);}}
uint64_t FakeClock(){return now;}template<class T>uintptr_t P(T&x){return reinterpret_cast<uintptr_t>(x.data());}template<size_t N,class T>void Put(std::array<unsigned char,N>&a,size_t o,T v){std::memcpy(a.data()+o,&v,sizeof v);}
struct Actor{std::array<unsigned char,0x400>a{},table{},v{},ride{},dock{},status{},owner{},entries{},meta{};Actor(uint32_t uid,uint8_t cat){Put(a,0x60,uid);Put(a,0x68,P(table));Put(a,0x88,P(meta));meta[1]=cat;Put(table,0x48,P(v));Put(v,0x18,P(ride));Put(table,0x170,P(dock));Put(table,0x20,P(status));Put(status,0x18,P(owner));Put(owner,0x58,P(entries));Put(entries,0,uint16_t{0});Put(entries,8,int64_t{300000});Put(entries,0x18,int64_t{500000});}};
bool Has(const char*x){for(const auto&s:logs)if(s.find(x)!=std::string::npos)return true;return false;}
size_t Count(const char*x){size_t n=0;for(const auto&s:logs)if(s.find(x)!=std::string::npos)++n;return n;}
std::string Addr(const char*key,uintptr_t p){char b[100]{};std::snprintf(b,sizeof b,"%s=0x%p",key,reinterpret_cast<void*>(p));return b;}
void Tick(){psm::shiptrace::Heartbeat();}
int main(){using namespace psm::shiptrace;Actor authority(10,1),callback(11,1),ride(200,7),enemy(201,7);std::array<unsigned char,0x400>hud{},info{};
 Put(hud,0x370,uint32_t{200});Put(authority.ride,0x10,uint32_t{200});Put(callback.ride,0x10,uint32_t{200});Put(info,8,uint16_t{16970});Put(info,0x40,uint32_t{1234});Put(callback.a,8,P(enemy.a));
 g_player_resolve.actor=P(authority.a);g_player_resolve.marker=P(authority.status);g_player_resolve.resource_owner=P(authority.owner);const auto a0=authority,c0=callback,r0=ride,e0=enemy;const auto h0=hud,i0=info;
 Enable(true,FakeClock);logs.clear();SetLastError(0xABCD4321);const auto csr=_mm_getcsr();ObserveActor(P(hud),P(callback.a),P(ride.a));Check(!Has("callback-identity"),"native callback queues identity evidence without formatting or logger calls");Check(GetLastError()==0xABCD4321&&_mm_getcsr()==csr,"new callback identity sampling preserves LastError and MXCSR");Tick();
 Check(Has("callback-identity")&&Has("origin=mounted"),"worker exposes actual mounted callback identity despite equality rejection");Check(Has(Addr("callbackPlayer",P(callback.a)).c_str())&&Has(Addr("expectedPlayer",P(authority.a)).c_str()),"report retains callback and authoritative actor operands independently");Check(Has("callbackUID=11")&&Has("expectedUID=10")&&Has("rideUID=200")&&Has("hudUID=200"),"direct native UID operands survive failed identity comparison");Check(Has("playerEqual=0")&&Has("callbackReferenceRaw="),"native plus8 reference is labelled raw and never normalized into player authority");Check(!Has("capture armed")&&!Has("identity generation=")&&!Has("native-health-query"),"diagnostic identity mismatch cannot seed hull binding");
 now+=1001;ObserveVehicleInfo(P(hud),P(callback.a),P(ride.a),P(info));Tick();Check(Has("origin=vehicle-info")&&Has("externalVehicleKey=16970")&&Has("typeHash=1234"),"unbound already-returned native VehicleInfo is visible with actual callback args");Check(Has("ship-binding-unproved=1")&&Has("diagnostic-no-mutation=1"),"raw metadata and ride owner are explicitly unproved for modifier routing");
 now+=1001;Put(callback.a,0x68,uintptr_t{1});ObserveActor(P(hud),P(callback.a),P(ride.a));Tick();Check(Has("callbackUID=11")&&Has("callbackFields="),"partially unreadable actor chain retains pointer and readable UID with field mask");callback=c0;
 now+=1001;ObserveActor(P(hud),1,1,ActorOrigin::Unridden);Tick();Check(Has("origin=unridden")&&Has("callbackPlayer=0x0000000000000001"),"invalid low operands are reported without dereference");
 now+=1001;Put(callback.a,0x60,uint32_t{10});ObserveActor(P(hud),P(callback.a),P(ride.a));Tick();Check(!Has("capture armed"),"same UID on foreign actor never bypasses exact expected-player guard");callback=c0;
 now+=1001;{std::lock_guard lock(g_state_mutex);g_player_resolve.actor=P(enemy.a);}ObserveActor(P(hud),P(callback.a),P(ride.a));Tick();Check(Has(Addr("expectedPlayer",P(enemy.a)).c_str())&&Has("expectedUID=201"),"authoritative actor switch is reflected without retained authority");g_player_resolve.actor=P(authority.a);
 const auto before=Count("callback-identity");for(unsigned j=0;j<500;++j){now+=1001;ObserveActor(P(hud),P(callback.a),P(ride.a));Tick();}Check(Count("callback-identity")<=32&&Count("callback-identity")>=before,"worker callback identity evidence has independent hard line cap");Check(!Has("capture armed"),"repeated mismatches never arm capture");
 Enable(true,FakeClock);logs.clear();ObserveActor(P(hud),P(authority.a),P(ride.a));Tick();Check(Has("capture armed")&&Has("playerEqual=1"),"exact direct native player retains existing coherent identity behavior");ObserveQuery(P(ride.owner),0,P(ride.entries));Check(Has("native-health-query"),"existing typed health query remains available after exact identity proof");Put(authority.ride,0x10,uint32_t{0});Put(hud,0x370,uint32_t{0});now+=1001;ObserveActor(P(hud),P(authority.a),0,ActorOrigin::Unridden);Tick();ObserveDamage(P(ride.owner),0,-1,1,1);Check(!Has("native-status-receipt"),"seat leave clears prior ride packet before damage observation");authority=a0;hud=h0;
 Enable(false);const auto n=logs.size();ObserveActor(1,1,1);ObserveVehicleInfo(1,1,1,1);Tick();Check(logs.size()==n,"disabled callback diagnostics remain inert");Check(std::memcmp(&authority,&a0,sizeof a0)==0&&std::memcmp(&callback,&c0,sizeof c0)==0&&std::memcmp(&ride,&r0,sizeof r0)==0&&std::memcmp(&enemy,&e0,sizeof e0)==0&&hud==h0&&info==i0,"full actor health maximum and reference fixtures remain byte-identical");
 std::printf("callback identity diagnostics: %u checks %u failures; no hull binding or native writes\n",checks,failures);return failures?1:0;}


