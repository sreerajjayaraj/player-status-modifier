#include "config.h"
#include "runtime/runtime_state.h"
#include "runtime/oxygen_resource.h"
#include "oxygen_catalog_fixture.h"
#include <safetyhook.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
void TestNativeResourceRateCallback(SafetyHookContext&);
void TestStatWriteCallback(SafetyHookContext&,bool);
void TestNativeResourceCostCallback(SafetyHookContext&);
namespace {
int count=0,fail=0;
void Check(bool ok,const char* label){++count;if(!ok){++fail;std::printf("FAIL %s\n",label);}}
template<class T> void Put(uintptr_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof v);}
}
int main(){
 const auto dir=std::filesystem::temp_directory_path()/(L"PSM-Oxygen-"+std::to_wstring(GetCurrentProcessId()));
 std::filesystem::create_directories(dir);
 const auto ini=dir/L"player-status-modifier.ini";
 {std::ofstream f(ini);f<<"[General]\nEnabled=1\n[ShipHealing]\nEnabled=0\n[Oxygen]\nUnlimited=1\n[Health]\nEnabled=0\n[Stamina]\nEnabled=0\n[Spirit]\nEnabled=0\n";}
 ModConfig c{};Check(ReadConfigSnapshot(ini.wstring(),&c)&&c.oxygen_unlimited,"actual INI loader reads oxygen");
 Check(ShouldInstallSharedStatHooks(c),"oxygen alone requires shared hooks");
 Check(ShouldInstallLegacyStatWriteHook(c),"oxygen alone requests the exact pre-notify writer");
 SetConfigSnapshot(ini.wstring(),c);
 std::array<unsigned char,0x100> actor{},table{};
 std::array<unsigned char,0x400> status{},owner{},gauge{};
 std::array<unsigned char,0x90*16> entries{};
 auto a=reinterpret_cast<uintptr_t>(actor.data()),t=reinterpret_cast<uintptr_t>(table.data());
 auto s=reinterpret_cast<uintptr_t>(status.data()),o=reinterpret_cast<uintptr_t>(owner.data());
 auto g=reinterpret_cast<uintptr_t>(gauge.data()),e=reinterpret_cast<uintptr_t>(entries.data())+0x90*3;
 Put(a+0x68,t);Put(t+0x20,s);Put(s+8,a);Put(s+0x18,o);Put(o,s);Put(o+0x58,reinterpret_cast<uintptr_t>(entries.data()));
 Put(g+0xE2,uint16_t{42});Put(e,uint16_t{42});Put(e+8,int64_t{500000});Put(e+0x18,int64_t{1000000});
 g_player_resolve={};g_player_resolve.actor=a;g_player_resolve.marker=s;g_player_resolve.root=o;g_player_resolve.resource_owner=o;
 g_player_resolve.health_entry=e-0x90*3;g_player_resolve.stamina_entry=e-0x90*2;g_player_resolve.spirit_entry=e-0x90;
 g_player_resolve.stamina_type=19;g_player_resolve.spirit_type=20;g_runtime_enabled=true;
 SetPlayerOxygenEnabled(true);ObservePlayerOxygenGauge(g,a);ObservePlayerOxygenEntry(o,e);
 SafetyHookContext ctx{};ctx.rbx=e;ctx.r11=static_cast<uintptr_t>(-1000LL);ctx.r10=123;
 TestNativeResourceRateCallback(ctx);Check(ctx.r11==0&&ctx.rbx==e&&ctx.r10==123,"actual rate callback changes only R11");
 ctx={};ctx.rsi=e;ctx.rbx=static_cast<uintptr_t>(-500LL);ctx.r11=987;
 TestNativeResourceCostCallback(ctx);Check(ctx.rbx==0&&ctx.rsi==e&&ctx.r11==987,"actual cost callback changes only RBX");
 ctx={};ctx.rsi=e;ctx.rbx=500;TestNativeResourceCostCallback(ctx);Check(ctx.rbx==500,"actual positive recovery unchanged");
 OxygenCatalogFixture native;native.SetCatalog(o,s); // clears prior gauge state
 const auto breath=reinterpret_cast<uintptr_t>(entries.data())+0x7e0;
 Put(breath,uint16_t{24});Put(breath+8,int64_t{120000});Put(breath+0x18,int64_t{120000});
 ctx={};ctx.rbx=breath;ctx.r11=static_cast<uintptr_t>(-114225LL);ctx.r10=123;
 TestNativeResourceRateCallback(ctx);Check(ctx.r11==0&&ctx.rbx==breath&&ctx.r10==123,
        "actual rate callback protects the proven Breath entry without HUD observation");
 ctx={};ctx.rsi=breath;ctx.rbx=static_cast<uintptr_t>(-500LL);ctx.r11=987;
 TestNativeResourceCostCallback(ctx);Check(ctx.rbx==0&&ctx.rsi==breath&&ctx.r11==987,
        "actual cost callback protects the proven Breath entry without HUD observation");
 ctx={};ctx.rdi=breath;ctx.rbx=104116;ctx.r11=987;
 TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==120000&&ctx.rdi==breath&&ctx.r11==987,
       "actual pre-notify writer preserves confirmed player breath on absolute drain");
 Put(breath+8,int64_t{95000});
 ctx.rbx=120000;TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==120000,"positive native refill remains unchanged");
 ctx.rbx=95000;TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==95000,"equal native write remains unchanged");
 ctx.rbx=80000;TestStatWriteCallback(ctx,false);
 Check(ctx.rbx==80000,"unverified writer cannot adjust oxygen");
 ctx.rbx=80000;Put(breath+8,int64_t{120001});TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==80000,"out-of-bounds current value cannot replace the native result");
 Put(breath+8,int64_t{95000});
 {
  std::lock_guard lock(g_state_mutex);
  AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::actor,t);
 }
 ctx.rbx=80000;TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==95000,"reciprocal component-table authority protects the same exact entry");
 Put(t+0x20,uintptr_t{0});
 ctx.rbx=80000;TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==80000,"foreign status marker cannot protect the candidate");
 Put(t+0x20,s);
 c.oxygen_unlimited=false;SetConfigSnapshot(ini.wstring(),c);
 ctx.rbx=80000;TestStatWriteCallback(ctx,true);
 Check(ctx.rbx==80000,"live config disables absolute write protection");
 ctx.rsi=breath;ctx.rbx=static_cast<uintptr_t>(-500LL);TestNativeResourceCostCallback(ctx);Check(static_cast<int64_t>(ctx.rbx)==-500,"live config disables callback adjustment");
 {std::ofstream f(ini);f<<"[General]\nEnabled=1\n";}
 WritePrivateProfileStringW(nullptr,nullptr,nullptr,ini.c_str());
 Check(ReadConfigSnapshot(ini.wstring(),&c)&&!c.oxygen_unlimited,"missing option preserves off default");
 SetPlayerOxygenEnabled(false);
 std::filesystem::remove(ini);std::filesystem::remove(dir);
 std::printf("oxygen integration checks=%d failures=%d\n",count,fail);return fail?1:0;
}
