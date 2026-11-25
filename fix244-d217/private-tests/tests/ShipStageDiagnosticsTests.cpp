#include "NativeShipFixture.h"
#include <Windows.h>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <thread>
#include <atomic>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
namespace {std::vector<std::string> logs;uint64_t testNow=1000;unsigned checks=0,failures=0;
void Check(bool ok,const char*name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
uint64_t StageClock(){return testNow;}void Tick(){testNow+=5001;psm::shiptrace::Heartbeat();}
size_t Count(const char*part){size_t n=0;for(const auto&s:logs)n+=s.find(part)!=std::string::npos;return n;}
bool Has(const char*part){return Count(part)!=0;}
}
void Log(const char*f,...){char b[4096]{};va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}
#include "../../PSM/src/runtime/ship_proof.cpp"
int main(){using namespace shiptest;using namespace psm::shiptrace;Fixture f;Enable(true,StageClock);logs.clear();Tick();
for(unsigned i=0;i<2000;++i){testNow+=1000000;ObserveActor(P(f.hud),P(f.hudPlayer.a),0,ActorOrigin::Unridden,P(f.frame));Tick();}
Check(Count("ship-proof:")<=9,"on-foot waiting has a separate bounded output reserve");logs.clear();
Put(f.ship.status,0x293,uint8_t{1});const auto before=logs.size();SetLastError(0x12345678);const auto csr=_mm_getcsr();f.ActorCall();
Check(logs.size()==before&&GetLastError()==0x12345678&&_mm_getcsr()==csr,"rejected actual Actor callback does not log or alter thread status");Tick();
Check(Has("vessel-dead-or-unreadable"),"first occupied-context rejection remains visible after long on-foot session");
int64_t out=777;Check(!TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out)&&out==777,"diagnostic capture never authorizes a dead vessel");
Put(f.ship.status,0x293,uint8_t{0});f.ActorCall();Put(f.gimmickRow,0x2E8,uint16_t{4});f.InfoCall();Tick();
Check(Has("source-reference-mismatch"),"native source-table reference failure is distinguished from missing Actor");
Put(f.gimmickRow,0x2E8,uint16_t{3});f.ActorCall();f.InfoCall();Tick();
Check(Has("state=waiting-for-health-query"),"accepted Actor and Info without a query is visible as waiting for exact health query");
const auto prior=logs.size();ObserveQuery(P(f.enemy.owner),0,P(f.enemy.entries));Check(logs.size()==prior,"unrelated native query does not format callback logs");Tick();
Check(Has("query-owner-mismatch"),"wrong query owner is distinguished without granting hull identity");
f.Query();Tick();Check(Has("state=current-vessel-health-bound"),"exact current hull query publication is reported independently of unrelated R15");
out=0;Check(TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out)&&out==200,"added diagnostics preserve exact native positive healing math");Tick();Check(Has("nativePositive=1")&&Has("scaledPositive=1"),"real functional healing counters remain available");
Put(f.player.ride,0x10,uint32_t{201});Check(!TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out),"UID invalidation still rejects the old proof");
Put(f.player.ride,0x10,uint32_t{200});Check(!TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out),"raw A B A restoration cannot revive a diagnostic-observed invalidated epoch");
f.ActorCall();f.InfoCall(P(f.otherFrame));Tick();Check(Has("info-frame-or-thread-mismatch"),"same-thread but foreign RBP Info is distinguished");
f.Bind();Check(TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out)&&out==200,"fresh full native sequence still rebinds");
for(unsigned i=0;i<2000;++i){testNow+=1000000;Put(f.ship.status,0x293,uint8_t(i%2));f.Bind();Tick();}
Check(Count("ship-proof:")<=50,"all functional-stage diagnostic summaries share a hard fifty-line bound");
size_t max=0;for(const auto&s:logs)if(s.rfind("ship-proof:",0)==0)max=(std::max)(max,s.size());Check(max>0&&max<1024,"complete functional-stage summary fits production logger payload");
Put(f.ship.status,0x293,uint8_t{0});Enable(true,StageClock);logs.clear();f.ActorCall(1);Tick();Check(Has("native-frame-invalid"),"invalid actual native frame has its own Actor failure reason");
f.ActorCall();f.vehicleCache[3 * 3 + 1]=P(f.otherInfo);f.InfoCall();Tick();Check(Has("vehicle-cache-row-mismatch"),"returned native VehicleInfo must equal the current cache row");f.vehicleCache[3 * 3 + 1]=P(f.info);
f.ActorCall();const auto savedGlobal=f.gimmickGlobal;f.gimmickGlobal=0;f.InfoCall();Tick();Check(Has("source-cache-unavailable"),"missing effective native source manager is distinguished");f.gimmickGlobal=savedGlobal;
{std::atomic<bool>ready{false},release{false};std::thread t([&]{std::lock_guard lock(psm::shipproof::proof_mutex);ready=true;while(!release.load())std::this_thread::yield();});while(!ready.load())std::this_thread::yield();const auto n=logs.size();f.ActorCall();f.InfoCall();f.Query();Check(logs.size()==n,"contended callbacks return without worker logging or waiting");release=true;t.join();Tick();}
Check(Has("actorBusy=1")&&Has("infoBusy=1")&&Has("queryBusy=1"),"each proof-lock busy callback is counted separately");
{std::atomic<bool>ready{false},release{false};std::thread t([&]{std::lock_guard lock(g_state_mutex);const auto words=psm::authority::detail::published.LoadForWriter();psm::authority::detail::published.BeginWrite();ready=true;while(!release.load())std::this_thread::yield();psm::authority::detail::published.EndWrite(words);});while(!ready.load())std::this_thread::yield();f.ActorCall();Tick();release=true;t.join();}
Check(Has("authority-unavailable"),"odd current-player publication while state lock is busy is reported unavailable");f.Bind();
const auto savedAuthority=g_player_resolve;{std::lock_guard lock(g_state_mutex);auto a=savedAuthority;a.actor=0;AssignTrackedPlayerResolveLocked(a);}f.ActorCall();Tick();Check(Has("authority-invalid"),"definitively invalid authority remains distinct from temporary unavailability");{std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(savedAuthority);}f.Bind();
const auto nativeShip=f.ship;ObserveQuery(P(f.enemy.owner),0,P(f.enemy.entries));out=0;Check(TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out)&&out==200,"unrelated query counters cannot replace or revoke a coherent selected ship");Check(std::memcmp(&f.ship,&nativeShip,sizeof nativeShip)==0,"diagnostic branches never commit native health or bounds");
Enable(true,StageClock);logs.clear();Put(f.hud,0x370,uint32_t{201});f.ActorCall();Tick();Check(Has("actorField=hud-ride-uid-mismatch")&&Has("playerUID=10 rideUID=200 hudUID=201"),"typed functional rejection records actual authoritative and HUD UID values");Put(f.hud,0x370,uint32_t{200});
const auto originalMeta=P(f.player.meta);Put(f.player.a,0x88,uintptr_t{1});f.ActorCall();Tick();Check(Has("actorField=player-category-read")&&Has("actorOffset=1"),"unreadable identity field reports exact first failing offset without weakening the strict reader");Put(f.player.a,0x88,originalMeta);
Enable(true,StageClock);logs.clear();f.ActorCall();f.InfoCall();ObserveQuery(P(f.ship.owner),0,P(f.ship.entries)+0x90);Tick();Check(Has("healthField=health-entry-not-array-base"),"unsupported native query layout remains rejected with actual query and array pointers visible");
Enable(true,StageClock);f.ActorCall();f.InfoCall();Put(f.ship.entries,0,uint16_t{19});f.Query();Tick();Check(Has("healthField=health-selector-mismatch")&&Has("healthTag=19"),"queried nonhealth tag remains rejected and diagnostic reports its actual value");Put(f.ship.entries,0,uint16_t{0});
Enable(true,StageClock);logs.clear();f.ActorCall();Put(f.gimmickRow,0x2E8,uint16_t{4});f.InfoCall();Tick();Check(Has("sourceID=123 nativeVehicleID=3 sourceReference=4"),"metadata failure reports actual source key native output and conflicting table reference");Put(f.gimmickRow,0x2E8,uint16_t{3});
size_t detailMax=0;for(const auto&line:logs)if(line.rfind("ship-proof",0)==0)detailMax=(std::max)(detailMax,line.size());Check(detailMax>0&&detailMax<1024,"all three complete worker diagnostic lines fit the production logger payload");
std::printf("maximum diagnostic line bytes=%zu\n",detailMax);
Enable(false);const auto disabled=logs.size();f.Bind();Tick();Check(logs.size()==disabled,"disabled observer produces no diagnostics");
std::printf("functional ship-stage diagnostics: %u checks %u failures\n",checks,failures);return failures?1:0;}
