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
int main(){using namespace psm::shiptrace;FakeActor player(10,1),ride(200,7);std::array<unsigned char,0x400>hud{};g_player_resolve.actor=Ptr(player.a);g_player_resolve.marker=Ptr(player.status);Put(player.ride,0x10,uint32_t{200});Put(player.dock,0x18,uint32_t{200});
 Enable(true,FakeClock);logs.clear();Tick();Check(Count("observer-waiting")==1,"worker emits initial waiting marker without HUD callback");for(unsigned i=0;i<100;++i)Tick();Check(Count("observer-waiting")==1,"initial marker is once-only");
 Identity id{};Check(!ReadIdentity(Ptr(hud),Ptr(player.a),0,Ptr(player.a),&id),"raw ride200 vsHUD0 rejects zero-ride callback despite docking200");Check(!ReadIdentity(Ptr(hud),Ptr(player.a),Ptr(ride.a),Ptr(player.a),&id),"raw ride200 vsHUD0 also rejects actor callback");
 ObserveActor(Ptr(hud),Ptr(player.a),0);ObserveQuery(Ptr(ride.owner),0,Ptr(ride.entries));ObserveDamage(Ptr(ride.owner),0,-7,1,1);ObserveWriter(Ptr(ride.entries),299993,1);Check(!Contains("capture armed")&&!Contains("native-health-query")&&!Contains("native-status-receipt"),"actual production callbacks cannot seed identity afterHUD rejection");
 Player(Ptr(player.a),Ptr(player.status));clockNow+=5000;Tick();Check(Contains("rawRideUID=200")&&Contains("rawDockUID=200"),"authoritative raw ride/dock sample is reported independently ofHUD cache");Check(Contains("hud-ride-uid-mismatch"),"silent typed rejection becomes explicit bounded reason");Check(!Contains("capture armed"),"raw UID report does not arm existing capture window");
 for(unsigned i=0;i<5000;++i){clockNow+=1000000;ObserveActor(Ptr(hud),Ptr(player.a),0);Player(Ptr(player.a),Ptr(player.status));Tick();}Check(Count("observer-")<=41,"added waiting/heartbeat summaries have hard41line bound");
 const auto after=logs.size();g_player_resolve.actor=0;Player(Ptr(player.a),Ptr(player.status));Tick();Check(logs.size()<=after+1,"mismatched authoritative player never dereferences an untrusted raw candidate");g_player_resolve.actor=Ptr(player.a);
 Enable(true,FakeClock);logs.clear();Tick();for(unsigned i=0;i<100;++i){clockNow+=10000000;Tick();}Check(Count("observer-")<=9,"idle summaries bounded separately from laterrawcontext reserve");Put(player.ride,0x10,uint32_t{0});Put(player.dock,0x18,uint32_t{0});Player(Ptr(player.a),Ptr(player.status));Tick();Check(Contains("rawPlayerUID=10")&&Contains("rawRideUID=0")&&Contains("rawDockUID=0"),"first coherent player gets fresh summary even withzero ride/dock after1h idle"); for(unsigned i=0;i<40;++i){clockNow+=5001;Tick();}clockNow+=1001;ObserveActor(Ptr(hud),Ptr(player.a),0);Tick();Check(Contains("no-vehicle-or-dock-context"),"delayed first HUD rejection survives periodic heartbeat exhaustion");clockNow+=1001;Put(player.ride,0x10,uint32_t{200});Put(player.dock,0x18,uint32_t{200});Player(Ptr(player.a),Ptr(player.status));Tick();Check(Contains("rawRideUID=200"),"raw field change retains reserved summary after old idle and periodic budgets");Check(Count("observer-")<=41,"firstplayer and transition reserves share explicit hard41line cap");
 Put(hud,0x370,uint32_t{200});ObserveActor(Ptr(hud),Ptr(player.a),Ptr(ride.a));Check(Count("capture armed")==1,"unchanged capture arms only on original coherent HUDidentity");ObserveQuery(Ptr(ride.owner),0,Ptr(ride.entries));Check(Count("native-health-query")==1,"original typed health packet remains available");clockNow+=900001;ObserveDamage(Ptr(ride.owner),0,-8,1,1);Check(Count("identity invalidated")==1&&!Contains("native-status-receipt"),"original900s capture expiry unchanged");Enable(false);const auto n=logs.size();Tick();Player(1,1);ObserveActor(1,1,1);Check(logs.size()==n,"disabled diagnostic is inert");
 std::printf("ship observer visibility: %u checks %u failures; authored source only/no gamecode\n",checks,failures);return failures?1:0;}



