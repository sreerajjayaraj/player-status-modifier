#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include "config.h"
#include "runtime/runtime_state.h"
#include "runtime/oxygen_resource.h"
#include "oxygen_catalog_fixture.h"

std::mutex g_state_mutex;
ActorResolveSnapshot g_player_resolve;
std::atomic<bool> g_runtime_enabled{true};
ModConfig fixture_config;
ModConfig GetConfig() { return fixture_config; }
void Log(const char*, ...) {}
namespace {
int checks=0, failures=0;
void Check(bool pass,const char* message) { ++checks; if(!pass){++failures;std::printf("FAIL %s\n",message);} }
template<class T> void Put(uintptr_t at,T value){std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
struct Fixture {
 std::array<unsigned char,0x100> actor{}, table{};
 std::array<unsigned char,0x400> status{}, owner{}, gauge{};
 std::array<unsigned char,0x90*16> entries{};
 uintptr_t a=0,t=0,s=0,o=0,g=0,e=0;
 Fixture(){
  a=reinterpret_cast<uintptr_t>(actor.data());t=reinterpret_cast<uintptr_t>(table.data());
  s=reinterpret_cast<uintptr_t>(status.data());o=reinterpret_cast<uintptr_t>(owner.data());
  g=reinterpret_cast<uintptr_t>(gauge.data());e=reinterpret_cast<uintptr_t>(entries.data())+0x90*3;
  Put(a+0x68,t);Put(t+0x20,s);Put(s+8,a);Put(s+0x18,o);Put(o,s);Put(o+0x58,reinterpret_cast<uintptr_t>(entries.data()));
  Put(g+0xE2,uint16_t{42});Put(e,uint16_t{42});
  Put(e+8,int64_t{800000});Put(e+0x18,int64_t{1000000});Put(e+0x28,int64_t{0});
  g_player_resolve={};g_player_resolve.actor=a;g_player_resolve.marker=s;g_player_resolve.root=o;
  g_player_resolve.resource_owner=o;g_player_resolve.health_entry=e-0x90*3;
  g_player_resolve.stamina_entry=e-0x90*2;g_player_resolve.spirit_entry=e-0x90;
  g_player_resolve.stamina_type=19;g_player_resolve.spirit_type=20;
  fixture_config={};fixture_config.general.enabled=true;fixture_config.oxygen_unlimited=true;
  g_runtime_enabled=true;SetPlayerOxygenEnabled(true);
 }
 void Bind(){ObservePlayerOxygenGauge(g,a);ObservePlayerOxygenEntry(o,e);}
 bool Lock(int64_t value=-123,bool rate=true){int64_t out=777;bool ok=TryLockPlayerOxygen(e,value,&out,rate);return ok&&out==0;}
};
}
int main(){
 Check(!ModConfig{}.oxygen_unlimited,"oxygen defaults off");
 {Fixture f;Check(!f.Lock(),"unbound entry passes through");f.Bind();
  Check(f.Lock(),"bound player's negative oxygen rate suppressed");
  Check(f.Lock(-123,false),"bound player's oxygen cost suppressed");
  Check(f.Lock(INT64_MIN),"minimum signed delta suppressed without overflow");
  Check(!f.Lock(123),"positive oxygen recovery unchanged");Check(!f.Lock(0),"zero unchanged");
  int64_t out=777;Check(!TryLockPlayerOxygen(f.e+0x90,-5,&out,true)&&out==777,"other entry untouched");
  Check(*reinterpret_cast<int64_t*>(f.e+8)==800000,"stored oxygen is never rewritten");
  Check(!TryLockPlayerOxygen(f.e,-5,nullptr,true),"null output rejected");
  fixture_config.oxygen_unlimited=false;Check(!f.Lock(),"live option disable stops lock");
  fixture_config.oxygen_unlimited=true;Check(f.Lock(),"live option enable resumes valid binding");
  fixture_config.general.enabled=false;Check(!f.Lock(),"global disable stops lock");}
 {Fixture f;f.Bind();g_runtime_enabled=false;Check(!f.Lock(),"runtime disable stops lock");}
 {Fixture f;f.Bind();SetPlayerOxygenEnabled(false);Check(!f.Lock(),"contract removal clears binding");}
 {Fixture f;f.Bind();Put(f.s+0x293,uint8_t{1});Check(!f.Lock(),"dead actor rejected");}
 {Fixture f;f.Bind();g_player_resolve.actor+=8;Check(!f.Lock(),"character switch invalidates binding");}
 {Fixture f;f.Bind();g_player_resolve.resource_owner+=8;Check(!f.Lock(),"resource owner switch invalidates binding");}
 {Fixture f;f.Bind();Put(f.s+0x18,uintptr_t{0});Check(!f.Lock(),"broken actor chain rejected");}
 {Fixture f;f.Bind();Put(f.g+0xE2,uint16_t{43});Check(!f.Lock(),"gauge selector change rejected");}
 {Fixture f;f.Bind();Put(f.e,uint16_t{43});Check(!f.Lock(),"entry selector change rejected");}
 {Fixture f;ObservePlayerOxygenGauge(f.g,f.a+8);ObservePlayerOxygenEntry(f.o,f.e);Check(!f.Lock(),"non-player actor never binds");}
 {Fixture f;ObservePlayerOxygenGauge(f.g,f.a);ObservePlayerOxygenEntry(f.o+8,f.e);Check(!f.Lock(),"foreign owner never binds");}
 {Fixture f;Put(f.g+0xE2,uint16_t{0});Put(f.e,uint16_t{0});f.Bind();Check(!f.Lock(),"health selector never binds");}
 {Fixture f;Put(f.g+0xE2,uint16_t{19});Put(f.e,uint16_t{19});f.Bind();Check(!f.Lock(),"stamina selector never binds");}
 {Fixture f;Put(f.g+0xE2,uint16_t{20});Put(f.e,uint16_t{20});f.Bind();Check(!f.Lock(),"spirit selector never binds");}
 {Fixture f;g_player_resolve.health_entry=f.e;f.Bind();Check(!f.Lock(),"health entry alias rejected");}
 {Fixture f;g_player_resolve.stamina_entry=f.e;f.Bind();Check(!f.Lock(),"stamina entry alias rejected");}
 {Fixture f;g_player_resolve.spirit_entry=f.e;f.Bind();Check(!f.Lock(),"spirit entry alias rejected");}
 {Fixture f;Put(f.e+0x18,int64_t{0});f.Bind();Check(!f.Lock(),"invalid maximum rejected");}
 {Fixture f;Put(f.e+0x20,INT64_MAX);f.Bind();Check(!f.Lock(),"overflowed maximum rejected");}
 {Fixture f;f.Bind();Put(f.o+0x58,uintptr_t{0});Check(!f.Lock(),"resource array replacement invalidates binding");}
 {Fixture f;ObservePlayerOxygenGauge(0,f.a);ObservePlayerOxygenEntry(f.o,f.e);Check(!f.Lock(),"null gauge rejected");}
 // This session's Breath entry is slot14, relative to the current player's
 // health at the start of the owner array. No HUD or slot observer ran.
 {Fixture f;
  f.e=reinterpret_cast<uintptr_t>(f.entries.data())+0x7e0;
  Put(f.e,uint16_t{24});Put(f.e+8,int64_t{120000});Put(f.e+0x18,int64_t{120000});
  OxygenCatalogFixture native;native.SetCatalog(f.o,f.s);
  Check(f.Lock(),"authoritative Breath drain can bind without a HUD lookup");
  Check(f.Lock(-300,false),"catalog-bound oxygen also handles native negative cost");
  Check(!f.Lock(600)&&*reinterpret_cast<const int64_t*>(f.e+8)==120000,
        "catalog-bound oxygen leaves recovery and stored oxygen unchanged");
  Put(f.e+0x90,uint16_t{24});Put(f.e+0xa8,int64_t{120000});
  int64_t out=777;
  Check(!TryLockPlayerOxygen(f.e+0x90,-123,&out,true)&&out==777,"another type24 entry is not authorized");
  Put(native.Address(native.group)+8,uint32_t{1000002});
  Check(!f.Lock(),"foreign native group key rejected");
  Put(native.Address(native.group)+8,uint32_t{1000001});native.slots[24]=13;
  Check(!f.Lock(),"changed native slot mapping rejected");
  native.slots[24]=14;native.selectors[14]=23;
  Check(!f.Lock(),"changed group slot selector rejected");
  native.selectors[14]=24;Put(native.Address(native.status)+8,uint32_t{1000079});
  Check(!f.Lock(),"foreign native status key rejected");
  Put(native.Address(native.status)+8,uint32_t{1000078});
  Put(native.status_global+0x28,uint32_t{24});
  Check(!f.Lock(),"catalog count must contain the selector before indexing");
  Put(native.status_global+0x28,uint32_t{25});
  native.status_rows[24 * 3 + 1]=0;
  Check(!f.Lock(),"absent native status cache remains inert");
  native.status_rows[24 * 3 + 1]=native.Address(native.status);Put(native.Address(native.group)+0x58,uint32_t{24});
  Check(!f.Lock(),"out of range map index remains inert");
  Put(native.Address(native.group)+0x58,uint32_t{25});
  fixture_config.oxygen_unlimited=false;Check(!f.Lock(),"catalog fallback honors live option disable");
  fixture_config.oxygen_unlimited=true;Check(f.Lock(),"catalog fallback honors live option enable");
  Put(f.s+0x293,uint8_t{1});Check(!f.Lock(),"catalog fallback refuses a dead player");
  Put(f.s+0x293,uint8_t{0});g_player_resolve.resource_owner=f.o+8;
  Check(!f.Lock(),"stale atomic health hint cannot authorize a changed owner");}
 // Installed 3732's StatusInfo selectors are Stamina 25, Mp 26 and Breath 27.
 {Fixture f;
  f.e=reinterpret_cast<uintptr_t>(f.entries.data())+0x90*14;
  Put(f.e,uint16_t{27});Put(f.e+8,int64_t{100000});Put(f.e+0x18,int64_t{120000});
  g_player_resolve.stamina_type=25;g_player_resolve.spirit_type=26;
  OxygenCatalogFixture native;native.SetCatalog(f.o,f.s,27,27);
  Check(f.Lock(),"3732 Breath selector 27 suppresses negative drain without a gauge");
  int64_t out=777;
  Check(TryPreservePlayerOxygenWrite(f.e,80000,&out)&&out==100000,
        "3732 Breath selector 27 preserves an absolute drain");
  Put(f.e,uint16_t{24});Check(!f.Lock(),"3732 legacy selector 24 cannot substitute for Breath");
  Put(f.e,uint16_t{27});
  native.SetCatalog(f.o,f.s,27,33);
  Check(f.Lock(),"native group index comes from the Breath descriptor");
  f.e+=0x90;
  Put(f.e,uint16_t{27});Put(f.e+8,int64_t{100000});Put(f.e+0x18,int64_t{120000});
  Check(!f.Lock(),"an unrelated entry with Breath's selector cannot bypass the mapped slot");
  native.SetCatalog(f.o,f.s,27,33,15);
  Check(f.Lock(),"verified group mapping can move the Breath slot");
  native.selectors[15]=26;
  Check(!f.Lock(),"reverse selector map mismatch still fails closed");
 }
 // The active client and its native server actor have independent banks.
 {Fixture f;
  constexpr uintptr_t server_vtable=0x140001000;
  constexpr uint32_t uid=0xB0100002;
  constexpr std::size_t breath_slot=14;
  std::array<unsigned char,0x100> server_actor{}, server_table{}, metadata{}, server_metadata{};
  std::array<unsigned char,0x400> server_marker{}, server_owner{};
  std::array<unsigned char,0x90*16> server_entries{};
  using OF=OxygenCatalogFixture;
  const auto a=OF::Address(server_actor),t=OF::Address(server_table),m=OF::Address(server_marker);
  const auto o=OF::Address(server_owner),entries=OF::Address(server_entries);
  const auto breath=entries+0x90*breath_slot;
  f.e=reinterpret_cast<uintptr_t>(f.entries.data())+0x90*breath_slot;
  g_player_resolve.stamina_type=25;g_player_resolve.spirit_type=26;
  Put(f.a+0x60,uid);Put(f.a+0x88,OF::Address(metadata));metadata[1]=4;
  Put(a,server_vtable);Put(a+0x60,uid);Put(a+0x68,t);Put(a+0x88,OF::Address(server_metadata));
  server_metadata[1]=4;
  Put(t+0x20,m);Put(m+8,a);Put(m+0x18,o);Put(o,m);Put(o+0x58,entries);
  for (const auto e : {f.e,breath}) {
   Put(e,uint16_t{27});Put(e+8,int64_t{110000});Put(e+0x18,int64_t{120000});
  }
  OF native;native.SetCatalog(f.o,f.s,27,27,breath_slot,server_vtable);
  int64_t out=777;
  const auto hold=[&] { return TryPreservePlayerOxygenWrite(breath,80000,&out)&&out==110000; };
  const auto bind=[&] { ObservePlayerOxygenEntry(o,breath); };
  Check(f.Lock(), "local Breath remains effective when server support is enabled");
  Check(!hold(), "no unobserved server entry is modified");
  bind();
  Check(hold(), "matching native server Breath commit preserves its own current oxygen");
  Check(TryLockPlayerOxygen(breath,-100,&out,true)&&out==0, "server natural drain uses the exact native rate authority");
  Check(TryLockPlayerOxygen(breath,-200,&out,false)&&out==0, "server negative cost is suppressed");
  Check(!TryPreservePlayerOxygenWrite(breath,120000,&out), "server refill proceeds through the original writer");
  Check(*reinterpret_cast<const int64_t*>(breath+8)==110000, "server oxygen is never stored by the hook");
  Put(a+0x60,uid+1);Check(!hold(), "other playable character server UID is refused");
  Put(a+0x60,uid);Check(!hold(), "revoked server lease needs a fresh exact slot observation");
  Put(a,server_vtable+8);bind();Check(!hold(), "matching UID on another class is refused");
  Put(a,server_vtable);server_metadata[1]=1;bind();Check(!hold(), "server category must match the controlled character");
  server_metadata[1]=4;Put(m+0x18,f.o);bind();Check(!hold(), "broken reciprocal owner is refused");
  Put(m+0x18,o);Put(m+0x293,uint8_t{1});bind();Check(!hold(), "dead server actor is refused");
  Put(m+0x293,uint8_t{0});native.selectors[breath_slot]=26;bind();Check(!hold(), "server exact Breath mapping is required");
  native.selectors[breath_slot]=27;bind();Check(hold(), "qualified server actor can bind after stale conditions clear");
  fixture_config.oxygen_unlimited=false;Check(!hold(), "server protection honors live INI disable");
  fixture_config.oxygen_unlimited=true;Check(hold(), "server protection honors live INI enable");
  g_player_resolve.actor+=8;Check(!hold(), "character switch revokes the server Breath lease");
 }
 {Fixture f;
  std::array<unsigned char,0x1B8> widget{};
  using OF=OxygenCatalogFixture;
  const auto view=OF::Address(widget);
  f.e=OF::Address(f.entries)+0x90*14;
  Put(view+0x1B0,f.g);
  Put(f.g+0xE2,uint16_t{27});Put(f.g+0xE8,int64_t{45000});Put(f.g+0xF0,int64_t{120000});
  Put(f.e,uint16_t{27});Put(f.e+8,int64_t{110000});Put(f.e+0x10,int64_t{-100});
  Put(f.e+0x18,int64_t{120000});
  OF native;native.SetCatalog(f.o,f.s,27,27);
  OxygenHudSample sample{};
  Check(ReadPlayerOxygenHud(view,f.a,&sample) && sample.raw_available &&
        sample.same_player && sample.entry_proved && sample.shown==45000 &&
        sample.shown_max==120000 && sample.stored==110000 && sample.rate==-100,
        "HUD trace separates the displayed number from the active native Breath entry");
  Check(*reinterpret_cast<int64_t*>(f.e+8)==110000 &&
        *reinterpret_cast<int64_t*>(f.g+0xE8)==45000,"HUD trace does not write native or displayed Breath");
  g_player_resolve.resource_owner+=8;
  Check(ReadPlayerOxygenHud(view,f.a,&sample) && sample.raw_available &&
        !sample.same_player && !sample.entry_proved,"trace exposes stale tracked owner without granting write authority");
  Put(f.g+0xE2,uint16_t{24});
  Check(ReadPlayerOxygenHud(view,f.a,&sample) && !sample.raw_available &&
        sample.selector==24 && !sample.entry_proved,"trace reports an unknown selector without reading it as Breath");
  Check(ReadPlayerOxygenHud(view,f.a+8,&sample) && !sample.raw_available &&
        !sample.same_player && !sample.entry_proved,"broken actor chain reports the displayed gauge only");
  fixture_config.oxygen_unlimited=false;
  Check(!ReadPlayerOxygenHud(view,f.a,&sample),"trace honors the live Oxygen setting");
  fixture_config.oxygen_unlimited=true;
  Put(f.g+0xF0,int64_t{0});
  Check(!ReadPlayerOxygenHud(view,f.a,&sample),"inactive gauge is not sampled");
 }
 {Fixture f;
  std::array<unsigned char,0x1B8> widget{};
  std::array<unsigned char,0x100> world{}, context{};
  using OF=OxygenCatalogFixture;
  const auto view=OF::Address(widget), ctx=OF::Address(context);
  const uintptr_t root=OF::Address(world);
  constexpr uintptr_t token=0x140021000;
  const auto root_slot=reinterpret_cast<uintptr_t>(&root);
  Put(root+0x30,ctx);Put(ctx+0x58,token);
  Put(f.a+0xA0,token);Put(f.a+0x94,uint8_t{1});
  Put(f.a+0x5E,uint8_t{0x10});Put(f.a+0x60,uint32_t{0xA0100001});
  f.e=OF::Address(f.entries)+0x90*14;
  Put(view+0x1B0,f.g);Put(f.g+0xE2,uint16_t{27});
  Put(f.g+0xE8,int64_t{600000});Put(f.g+0xF0,int64_t{630000});
  Put(f.e,uint16_t{27});Put(f.e+8,int64_t{630000});Put(f.e+0x10,int64_t{-1000});
  Put(f.e+0x18,int64_t{630000});
  OF native;native.SetCatalog(f.o,f.s,27,27,14,0,root_slot);
  Put(OF::Address(native.actor_info)+8,uint32_t{1});
  {
   std::lock_guard lock(g_state_mutex);
   auto rebound=g_player_resolve;
   rebound.resource_owner+=8;
   rebound.stamina_entry=0;
   AssignTrackedPlayerResolveLocked(rebound);
  }
  OxygenHudSample sample{};
  Check(!f.Lock(-1000),"stale tracked damage owner cannot independently authorize Breath");
  Check(ReadPlayerOxygenHud(view,f.a,&sample) && sample.raw_available && sample.control_bound &&
        !sample.same_player && !sample.entry_proved,
        "current native controller binds despite the rejected shared combat owner");
  Check(f.Lock(-1000) && f.Lock(-1000,false),"verified own native Breath rate and cost are held");
  int64_t out=0;
  Check(TryPreservePlayerOxygenWrite(f.e,600000,&out) && out==630000,
        "native absolute decrease preserves current controlled Breath");
  Check(!f.Lock(30) && !TryPreservePlayerOxygenWrite(f.e,631000,&out),
        "local-control path preserves positive refill");
  Check(!TryLockPlayerOxygen(f.e+0x90,-1000,&out,true),
        "nearby entries cannot borrow the verified controller");
  fixture_config.oxygen_unlimited=false;Check(!f.Lock(-1000),"live disable honors the former option");
  fixture_config.oxygen_unlimited=true;
  Put(ctx+0x58,token+8);Check(!f.Lock(-1000),"controller change revokes the exact Breath binding");
  Put(ctx+0x58,token);
  Put(f.a+0x94,uint8_t{0});
  Check(ReadPlayerOxygenHud(view,f.a,&sample) && !sample.control_bound && !f.Lock(-1000),
        "inactive character cannot bind even through a visible gauge");
  Put(f.a+0x94,uint8_t{1});Put(OF::Address(native.actor_info)+8,uint32_t{4});
  Check(ReadPlayerOxygenHud(view,f.a,&sample) && sample.control_bound && f.Lock(-1000),
        "another verified playable actor can bind after a fresh HUD observation");
  Put(f.s+0x293,uint8_t{1});Check(!f.Lock(-1000),"dead actor clears the previously accepted binding");
 }
 // The published Health bank is the server bank in the current game. Its
 // Breath must stay above the minimum when its periodic projection is committed.
 for (const auto hero : {1U,4U,6U}) {
  Fixture local, server, previous;
  std::array<unsigned char,0x1B8> widget{};
  std::array<unsigned char,0x100> world{}, context{}, metadata{};
  using OF=OxygenCatalogFixture;
  const auto view=OF::Address(widget), ctx=OF::Address(context);
  const uintptr_t root=OF::Address(world);
  constexpr uintptr_t control_token=0x140021000, server_vtable=0x140001000;
  constexpr uintptr_t child_actor_vtable=0x140002000, server_status_vtable=0x140003000;
  constexpr uintptr_t client_status_vtable=0x140004000;
  const auto control_slot=reinterpret_cast<uintptr_t>(&root);
  const uint32_t uid=hero==1 ? 0xA0100001 : hero==4 ? 0xB0100002 : 0xB0100003;
  const uint8_t category=hero==1 ? 1 : 4;
  Put(root+0x30,ctx);
  Put(ctx+0x58,control_token);
  Put(local.a+0xA0,control_token);
  Put(local.a+0x94,uint8_t{1});
  Put(local.a+0x5E,uint8_t{0x10});
  Put(local.a+0x60,uid);
  Put(server.a,child_actor_vtable);
  Put(server.s,server_status_vtable);
  Put(server.a+0x60,uid);
  Put(server.a+0x88,OF::Address(metadata));
  metadata[1]=category;
  auto make_breath=[](Fixture& bank) {
   bank.e=OF::Address(bank.entries)+14*0x90;
   Put(bank.e,uint16_t{27});
   Put(bank.e+8,int64_t{630000});
   Put(bank.e+0x10,int64_t{-1000});
   Put(bank.e+0x18,int64_t{630000});
  };
  make_breath(local);
  make_breath(server);
  Put(view+0x1B0,local.g);
  Put(local.g+0xE2,uint16_t{27});
  Put(local.g+0xF0,int64_t{630000});
  g_player_resolve.actor=server.a;
  g_player_resolve.marker=server.s;
  g_player_resolve.root=server.o;
  g_player_resolve.resource_owner=server.o;
  g_player_resolve.health_entry=OF::Address(server.entries);
  OF native;
  native.SetCatalog(local.o,local.s,27,27,14,server_vtable,control_slot,server_status_vtable);
  Put(OF::Address(native.actor_info)+8,hero);
  int64_t resolved=999;
  auto clamp=[&] { return TryPreservePlayerOxygenWrite(server.e,0,&resolved)&&resolved==630000; };
  Check(!server.Lock(-1000), "the Health hint alone cannot authorize a server bank");
  OxygenHudSample sample{};
  Check(ReadPlayerOxygenHud(view,local.a,&sample) && sample.control_bound,
        "active hero binds the displayed Breath bank");
  Check(local.Lock(-1000) && server.Lock(-1000),
        "both exact banks suppress depletion before it reaches the passive threshold");
  Check(clamp(), "a downward native commit to zero keeps the server's own current oxygen");
  Check(server.Lock(-1000,false), "the server bank suppresses a native Breath cost");
  Put(server.e+8,int64_t{600000});
  Check(!server.Lock(100000) && !TryPreservePlayerOxygenWrite(server.e,630000,&resolved),
        "the positive refill rate and commit both pass through");
  Check(*reinterpret_cast<int64_t*>(server.e+8)==600000 &&
        *reinterpret_cast<int64_t*>(server.e+0x10)==-1000,
        "native Breath fields are left to the game");
  fixture_config.oxygen_unlimited=false;
  Check(!server.Lock(-1000) && !TryPreservePlayerOxygenWrite(server.e,0,&resolved),
        "live INI disable restores native depletion on the server bank");
  fixture_config.oxygen_unlimited=true;
  Put(server.a+0x60,uid ^ 0x01000000U);
  ObservePlayerOxygenEntry(server.o,server.e);
  Check(!server.Lock(-1000), "a partial UID match cannot transfer protection");
  Put(server.a+0x60,uid);
  Put(server.s,client_status_vtable);
  Check(!server.Lock(-1000), "a matching UID on the client status component is rejected");
  Put(server.s,server_status_vtable);
  Put(server.s+0x18,previous.o);
  Check(!server.Lock(-1000), "server marker and owner must remain reciprocal");
  Put(server.s+0x18,server.o);
  {
   std::lock_guard lock(g_state_mutex);
   auto other=g_player_resolve;
   other.actor=previous.a;
   other.marker=previous.s;
   other.root=previous.o;
   other.resource_owner=previous.o;
   other.health_entry=OF::Address(previous.entries);
   AssignTrackedPlayerResolveLocked(other);
  }
  ObservePlayerOxygenEntry(server.o,server.e);
  Check(server.Lock(-1000), "native server lookup binds independently of the combat tracker");
  Check(!TryLockPlayerOxygen(OF::Address(server.entries),-1000,&resolved,true),
        "Health cannot inherit the Breath guard from the same bank");
  Put(local.a+0x94,uint8_t{0});
  Check(!local.Lock(-1000) && !server.Lock(-1000),
        "losing local control immediately revokes both banks");
 }
 std::printf("oxygen runtime checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
