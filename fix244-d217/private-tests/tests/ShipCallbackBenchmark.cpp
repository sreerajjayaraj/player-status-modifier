#include "runtime/runtime_state.h"
#include "runtime/ship_trace.h"
#include "config.h"
#include "NativeShipFixture.h"
#include <Windows.h>
#include <safetyhook.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
void TestShipActorCallback(SafetyHookContext&,bool);void TestShipInfoCallback(SafetyHookContext&);void TestShipQueryCallback(SafetyHookContext&);void TestStatWriteCallback(SafetyHookContext&,bool);
using namespace shiptest;
template<class F> void Bench(const char*name,unsigned n,F&&fn){std::array<double,7>ns{};for(unsigned warm=0;warm<1000;++warm)fn();for(auto&value:ns){auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<n;++i)fn();auto stop=std::chrono::steady_clock::now();value=std::chrono::duration<double,std::nano>(stop-start).count()/n;}std::sort(ns.begin(),ns.end());std::printf("%s calls=%u median_ns=%.2f min_ns=%.2f max_ns=%.2f\n",name,n,ns[3],ns[0],ns[6]);}
int main(int argc,char**argv){const bool legacy=argc>1&&!std::strcmp(argv[1],"legacy-baseline");Fixture f;ModConfig c{};c.general.enabled=true;c.health.enabled=false;c.stamina.enabled=false;c.spirit.enabled=false;c.mount.enabled=false;c.ship_healing.enabled=true;c.damage.incoming.enabled=false;c.damage.outgoing.enabled=false;SetConfigSnapshot(L"",c);g_runtime_enabled=true;psm::shiptrace::Enable(true);
SafetyHookContext a{};a.rsi=P(f.hud);a.r15=P(f.hudPlayer.a);a.r13=P(f.ship.a);a.rbp=P(f.frame);a.rax=P(f.info);SafetyHookContext q{};q.rsi=P(f.ship.owner);q.rbx=0;q.rax=P(f.ship.entries);auto nonhealth=q;nonhealth.rbx=19;std::array<unsigned char,0x100>stack{};SafetyHookContext w{};w.rdi=P(f.ship.entries);w.rsi=P(f.ship.entries)+0x18;w.r14=w.rsi;w.rbx=101;w.rsp=P(stack);
{std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked({});}
Bench("init-query0-no-proof",200000,[&]{TestShipQueryCallback(q);});Bench("init-query19-no-proof",200000,[&]{TestShipQueryCallback(nonhealth);});Bench("init-writer-no-proof",200000,[&]{w.rbx=101;TestStatWriteCallback(w,true);});
ActorResolveSnapshot authority{};authority.actor=P(f.player.table);authority.marker=P(f.player.status);authority.root=authority.resource_owner=authority.damage_target=P(f.player.owner);authority.damage_source=authority.actor;authority.health_entry=P(f.player.entries);{std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(authority);}Put(f.player.ride,0x10,uint32_t{0});Put(f.hud,0x370,uint32_t{0});
Bench("onfoot-unridden",100000,[&]{TestShipActorCallback(a,true);});Bench("onfoot-query0-no-proof",200000,[&]{TestShipQueryCallback(q);});Bench("onfoot-writer-no-proof",200000,[&]{w.rbx=101;TestStatWriteCallback(w,true);});
if(legacy){std::lock_guard lock(g_state_mutex);authority.actor=P(f.player.a);authority.damage_source=authority.actor;AssignTrackedPlayerResolveLocked(authority);}Put(f.player.ride,0x10,uint32_t{200});Put(f.hud,0x370,uint32_t{200});TestShipActorCallback(a,false);TestShipInfoCallback(a);TestShipQueryCallback(q);
int64_t out=0;const bool bound=psm::shiptrace::TryAdjustOccupiedShipHealthWrite(P(f.ship.entries),101,100,&out)&&out==200;std::printf("bound_fixture=%u representation=%s\n",bound?1:0,legacy?"legacy-synthetic-native-actor":"production-component-table");if(!bound)return 2;
Bench("occupied-actor",50000,[&]{TestShipActorCallback(a,false);});TestShipInfoCallback(a);TestShipQueryCallback(q);Bench("occupied-query0",50000,[&]{TestShipQueryCallback(q);});Bench("occupied-writer",50000,[&]{w.rbx=101;TestStatWriteCallback(w,true);});psm::shiptrace::Enable(false);return 0;}
