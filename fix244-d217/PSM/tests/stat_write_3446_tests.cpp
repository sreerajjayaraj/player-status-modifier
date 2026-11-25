#include "config.h"
#include "runtime/runtime_state.h"
#include "runtime/stat_logic.h"
#include "runtime/ship_trace.h"
#include "runtime/actor_resolve.h"
#include "../../private-tests/tests/NativeCatalogFixture.h"
#include "runtime/mounted_resource.h"
#include <immintrin.h>
#include <safetyhook.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <limits>

void TestStatWriteCallback(SafetyHookContext&, bool current_3446);
void TestShipActorCallback(SafetyHookContext&,bool);
void TestShipInfoCallback(SafetyHookContext&);
void TestShipQueryCallback(SafetyHookContext&);
bool TestConfigNeedsHookReinstall(const ModConfig&,const ModConfig&);
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",message);}}
template<class T> void Put(uintptr_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
struct Entry {
 alignas(16) std::array<unsigned char,0x90> bytes{};
 uintptr_t p(){return reinterpret_cast<uintptr_t>(bytes.data());}
 void Set(int32_t selector,int64_t current,int64_t base,int64_t bonus,int64_t minimum){
  bytes={};Put(p(),selector);Put(p()+8,current);Put(p()+0x18,base);Put(p()+0x20,bonus);Put(p()+0x28,minimum);
 }
};
alignas(16) std::array<unsigned char,0x2000> player_owner{}, mount_owner{};
Entry player,stamina,spirit,mount,other;
ModConfig Config(){
 ModConfig c{};c.general.enabled=true;c.health.enabled=true;
 c.health.consumption_multiplier=0.1;c.health.heal_multiplier=100;
 c.damage.incoming.enabled=true;c.damage.incoming.multiplier=0.1;
 c.damage.incoming.stat_write_fallback=false;
 c.damage.outgoing.enabled=true;c.damage.outgoing.stat_write_fallback=true;c.damage.outgoing.multiplier=4;
 return c;
}
void Reset(const ModConfig& c){
 SetConfigSnapshot(L"",c);g_runtime_enabled=true;SetAuthoritativePlayerResourceDeltaActive(false);
 player.Set(0,500,1000,0,0);stamina.Set(19,100,1000,0,0);spirit.Set(20,100,1000,0,0);
 const auto owner=reinterpret_cast<uintptr_t>(player_owner.data());
 g_player_resolve={};g_mount_resolve={};g_mount_track_confidence=0;
 g_player_resolve.actor=owner+0x400;g_player_resolve.marker=owner+0x800;
 g_player_resolve.root=g_player_resolve.resource_owner=owner;
 g_player_resolve.health_entry=player.p();g_player_resolve.stamina_entry=stamina.p();g_player_resolve.spirit_entry=spirit.p();
 g_player_resolve.stamina_type=19;g_player_resolve.spirit_type=20;
}
alignas(16) std::array<unsigned char,0x100> stack{};
SafetyHookContext Context(uintptr_t entry,int64_t requested){
 stack={};Put(reinterpret_cast<uintptr_t>(stack.data())+0x68,uintptr_t{0x14B764013});
 SafetyHookContext c{};c.rdi=entry;c.rsi=entry+0x18;c.rbx=static_cast<uintptr_t>(requested);
 c.r8=static_cast<uintptr_t>(requested);c.rcx=0x123456;c.rdx=123456789;c.r9=17;c.r10=18;c.r11=19;
 c.r12=20;c.r13=21;c.r14=entry+0x18;c.r15=22;c.rbp=23;c.rax=24;c.rflags=0x202;
 c.rsp=reinterpret_cast<uintptr_t>(stack.data());c.rip=0x14B76494E;return c;
}
void Run(uintptr_t entry,int64_t requested,int64_t expected,const char* label,bool current=true){
 auto ctx=Context(entry,requested);auto want=ctx;want.rbx=static_cast<uintptr_t>(expected);
 TestStatWriteCallback(ctx,current);
 if(static_cast<int64_t>(ctx.rbx)!=expected) std::printf("VALUE: requested=%lld expected=%lld actual=%lld\n",static_cast<long long>(requested),static_cast<long long>(expected),static_cast<long long>(static_cast<int64_t>(ctx.rbx)));
 Check(static_cast<int64_t>(ctx.rbx)==expected,label);
 auto registers=ctx;registers.rbx=want.rbx;
 Check(!std::memcmp(&registers,&want,sizeof(ctx)),"actual callback preserves every other register and flags");
 Check(g_runtime_enabled.load(),"native-bound rejection does not disable runtime");
}
}

void OccupiedShipActualHookTests(){
 struct A {std::array<unsigned char,0x400>a{},t{},v{},r{},d{},s{},o{},e{},m{},gm{};};
 A p{},callback{},ship{},enemy{};std::array<unsigned char,0x400>hud{},info{};
 auto ptr=[](auto&b){return reinterpret_cast<uintptr_t>(b.data());};
 auto init=[&](A&x,uint32_t uid,uint8_t cat){Put(ptr(x.a)+0x60,uid);Put(ptr(x.a)+0x68,ptr(x.t));Put(ptr(x.a)+0x88,ptr(x.m));x.m[1]=cat;Put(ptr(x.t)+0x20,ptr(x.s));Put(ptr(x.s)+8,ptr(x.a));Put(ptr(x.s)+0x18,ptr(x.o));Put(ptr(x.o),ptr(x.s));Put(ptr(x.s)+0x30,uint16_t{77});Put(ptr(x.t)+0x30,ptr(x.gm));Put(ptr(x.gm)+0x48,uint16_t{123});Put(ptr(x.t)+0x48,ptr(x.v));Put(ptr(x.v)+0x18,ptr(x.r));Put(ptr(x.t)+0x170,ptr(x.d));Put(ptr(x.o)+0x58,ptr(x.e));Put(ptr(x.e),uint16_t{0});Put(ptr(x.e)+8,int64_t{100});Put(ptr(x.e)+0x18,int64_t{1000});};
 init(p,10,1);init(callback,99,1);init(ship,200,7);init(enemy,201,7);Put(ptr(p.r)+0x10,uint32_t{200});Put(ptr(hud)+0x370,uint32_t{200});Put(ptr(info)+8,uint16_t{16970});Put(ptr(info)+0x40,uint32_t{0x4798223F});Put(ptr(info)+0x0D,uint8_t{1});
 ModConfig reloadBefore{};reloadBefore.mount.enabled=false;reloadBefore.ship_healing.enabled=false;reloadBefore.oxygen_unlimited=true;auto reloadAfter=reloadBefore;reloadAfter.ship_healing.enabled=true;
 Check(!TestConfigNeedsHookReinstall(reloadBefore,reloadAfter),"ship healing can reload when Oxygen already owns the native writer");
 reloadBefore.oxygen_unlimited=false;reloadBefore.stamina.enabled=true;
 reloadAfter=reloadBefore;reloadAfter.ship_healing.enabled=true;
 Check(ShouldInstallSharedStatHooks(reloadBefore)&&!ShouldInstallLegacyStatWriteHook(reloadBefore)&&
       TestConfigNeedsHookReinstall(reloadBefore,reloadAfter),
       "ship healing requires writer installation when only shared stats are active");
 shiptest::NativeCatalogFixture native(ptr(info));
 auto c=Config();c.health.enabled=false;c.health.heal_multiplier=3;c.ship_healing.enabled=true;c.ship_healing.multiplier=100;Reset(c);
 g_player_resolve.actor=ptr(p.t);g_player_resolve.marker=ptr(p.s);g_player_resolve.root=g_player_resolve.resource_owner=ptr(p.o);g_player_resolve.health_entry=ptr(p.e);
 psm::shiptrace::Enable(true);SafetyHookContext frame{};frame.rbp=native.Frame();frame.rsi=ptr(hud);frame.r15=ptr(callback.a);frame.r13=ptr(ship.a);frame.rax=ptr(info);frame.rflags=0x202;auto saved=frame;
 const auto actorError=0x18273645ul;const auto actorCsr=_mm_getcsr();SetLastError(actorError);
 TestShipActorCallback(frame,false);Check(GetLastError()==actorError&&_mm_getcsr()==actorCsr,"actual Actor callback preserves LastError and MXCSR");Check(!std::memcmp(&frame,&saved,sizeof frame),"actual mounted callback forwards RSI/R15/R13 without register mutation");
 SetLastError(actorError);TestShipInfoCallback(frame);Check(GetLastError()==actorError&&_mm_getcsr()==actorCsr,"actual Info callback preserves LastError and MXCSR");Check(!std::memcmp(&frame,&saved,sizeof frame),"actual typed metadata callback forwards native RAX and preserves registers");
 frame.rsi=ptr(ship.o);frame.rbx=0;frame.rax=ptr(ship.e);saved=frame;SetLastError(actorError);TestShipQueryCallback(frame);Check(GetLastError()==actorError&&_mm_getcsr()==actorCsr,"actual Query callback preserves LastError and MXCSR");Check(!std::memcmp(&frame,&saved,sizeof frame),"actual native selector0 query callback binds exact RSI owner RAX entry");
 auto*unreadable=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);Check(unreadable!=nullptr,"unreadable native frame ABI fixture allocated");
 if(unreadable){auto invalidFrame=frame;invalidFrame.rbp=reinterpret_cast<uintptr_t>(unreadable);const auto beforeFrame=invalidFrame;SetLastError(actorError);TestShipInfoCallback(invalidFrame);Check(GetLastError()==actorError&&_mm_getcsr()==actorCsr&&!std::memcmp(&invalidFrame,&beforeFrame,sizeof beforeFrame),"actual Info rejects unreadable RBP output while preserving status and all Context fields");VirtualFree(unreadable,0,MEM_RELEASE);}
 const auto nativeShip=ship;Run(ptr(ship.e),101,200,"actual writer boosts tiny periodic ship heal with player Health disabled");Run(ptr(ship.e),1000,1000,"actual complete anchorage repair remains native full HP");Run(ptr(ship.e),100,100,"actual zero native heal does not create health");Run(ptr(ship.e),90,90,"exact occupied hull loss bypasses unrelated outgoing multiplier4");
 Put(ptr(ship.e)+8,int64_t{1000});Run(ptr(ship.e),1000,1000,"already-full ship receives no artificial periodic increases");ship=nativeShip;
 c.ship_healing.enabled=false;SetConfigSnapshot(L"",c);Run(ptr(ship.e),101,101,"separate ship switch disables healing independently of Health");c.ship_healing.enabled=true;c.ship_healing.multiplier=2;SetConfigSnapshot(L"",c);Run(ptr(ship.e),101,102,"live independent ship multiplier2 changes only ship healing");
 auto ctx=Context(ptr(ship.e),101);const auto error=0x1324ABCDul;const auto csr=_mm_getcsr();SetLastError(error);TestStatWriteCallback(ctx,true);Check(GetLastError()==error&&_mm_getcsr()==csr,"actual ship writer preserves LastError and MXCSR");
 SetNativeMountedResourceEnabled(true,true);
 frame={};frame.rbp=native.Frame();frame.rsi=ptr(hud);frame.r15=ptr(p.a);frame.r13=ptr(ship.a);TestShipActorCallback(frame,false);
 frame={};frame.rsi=ptr(ship.o);frame.rbx=0;frame.rax=ptr(ship.e);saved=frame;
 {std::lock_guard lock(g_state_mutex);TestShipQueryCallback(frame);}
 Check(!std::memcmp(&frame,&saved,sizeof frame),"actual health query with an enabled bound mounted owner returns under state mutex without entering stamina lock");
 SetNativeMountedResourceEnabled(false);
 {std::lock_guard lock(g_state_mutex);Run(ptr(ship.e),101,101,"held state mutex makes actual ship writer fail closed without self-lock");}
 Run(ptr(enemy.e),101,101,"enemy vessel positive health stays native on ordinary fallback");
 Put(ptr(p.r)+0x10,uint32_t{0});Put(ptr(hud)+0x370,uint32_t{0});frame={};frame.rbp=native.Frame();frame.rsi=ptr(hud);frame.r15=ptr(callback.a);TestShipActorCallback(frame,true);Run(ptr(ship.e),101,101,"actual native dismount callback invalidates old hull before later writer");
 Check(!std::memcmp(&ship,&nativeShip,sizeof ship),"actual writer adjusts only RBX and never commits fixture health/max/charges");psm::shiptrace::Enable(false);
}

void ShipProductionRepresentationTests(){
 struct A {std::array<unsigned char,0x1000>a{},t{},v{},r{},d{},s{},o{},e{},m{},gm{};};
 A p{},callback{},ship{},server{},foreign{};std::array<unsigned char,0x400>hud{},info{};auto ptr=[](auto&v){return reinterpret_cast<uintptr_t>(v.data());};
 auto init=[&](A&x,uint32_t uid,uint8_t cat){Put(ptr(x.a)+0x60,uid);Put(ptr(x.a)+0x68,ptr(x.t));Put(ptr(x.a)+0x88,ptr(x.m));x.m[1]=cat;Put(ptr(x.t)+0x20,ptr(x.s));Put(ptr(x.s)+8,ptr(x.a));Put(ptr(x.s)+8,ptr(x.a));Put(ptr(x.s)+0x18,ptr(x.o));Put(ptr(x.o),ptr(x.s));Put(ptr(x.o),ptr(x.s));Put(ptr(x.s)+0x30,uint16_t{77});Put(ptr(x.t)+0x30,ptr(x.gm));Put(ptr(x.gm)+0x48,uint16_t{123});Put(ptr(x.t)+0x48,ptr(x.v));Put(ptr(x.v)+0x18,ptr(x.r));Put(ptr(x.t)+0x170,ptr(x.d));Put(ptr(x.o)+0x58,ptr(x.e));Put(ptr(x.e),int32_t{0});Put(ptr(x.e)+8,int64_t{100});Put(ptr(x.e)+0x18,int64_t{1000});};
 init(p,10,1);init(callback,10,1);init(ship,200,7);Put(ptr(p.e)+0x5A0,int32_t{19});Put(ptr(p.e)+0x5A8,int64_t{100});Put(ptr(p.e)+0x5B8,int64_t{1000});Put(ptr(p.e)+0x630,int32_t{20});Put(ptr(p.e)+0x638,int64_t{100});Put(ptr(p.e)+0x648,int64_t{1000});
 Put(ptr(p.r)+0x10,uint32_t{200});Put(ptr(hud)+0x370,uint32_t{200});Put(ptr(info)+8,uint16_t{16970});Put(ptr(info)+0x0D,uint8_t{1});Put(ptr(info)+0x40,uint32_t{0x4798223F});shiptest::NativeCatalogFixture native(ptr(info));
 auto c=Config();c.health.enabled=false;Reset(c);ActorResolveSnapshot resolved{};Check(TryResolveActorResolveFromMarker(ptr(p.s),&resolved),"actual production resolver accepts coherent status reverse links and exact resource layout");Check(resolved.actor==ptr(p.t)&&resolved.actor!=ptr(p.a)&&resolved.marker==ptr(p.s)&&resolved.root==ptr(p.o),"actual production resolver publishes component table rather than native Actor");{std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(resolved);}
 psm::shiptrace::Enable(true);
 auto select=[&]{SafetyHookContext frame{};frame.rsi=ptr(hud);frame.r15=ptr(callback.a);frame.r13=ptr(ship.a);frame.rbp=native.Frame();frame.rax=ptr(info);TestShipActorCallback(frame,false);TestShipInfoCallback(frame);};
 auto query=[](uintptr_t owner,uintptr_t entry){SafetyHookContext frame{};frame.rsi=owner;frame.rbx=0;frame.rax=entry;TestShipQueryCallback(frame);};
 auto bind=[&]{select();query(ptr(ship.o),ptr(ship.e));};bind();
 Run(ptr(ship.e),101,200,"production component-table authority bridges to actual Actor and scales native current ship heal");Run(ptr(ship.e),90,90,"production component-table authority preserves current hull native loss");
 Put(ptr(p.s)+8,ptr(callback.a));Run(ptr(ship.e),101,101,"same-UID foreign native Actor cannot bridge authoritative component table");Run(ptr(ship.e),90,60,"bridge failure immediately restores ordinary outgoing route for stale hull entry");Put(ptr(p.s)+8,ptr(p.a));Run(ptr(ship.e),101,101,"marker native-Actor A B A restoration cannot revive retired proof");bind();Run(ptr(ship.e),101,200,"fresh complete native chain rebinds after representation bridge ABA");
 Put(ptr(p.a)+0x68,ptr(callback.t));Run(ptr(ship.e),101,101,"native Actor must refer to exact published component table");Put(ptr(p.a)+0x68,ptr(p.t));bind();
 Put(ptr(p.t)+0x20,ptr(callback.s));Run(ptr(ship.e),101,101,"component table must refer to exact published status marker");Put(ptr(p.t)+0x20,ptr(p.s));bind();
 Put(ptr(p.s)+0x18,ptr(callback.o));Run(ptr(ship.e),101,101,"status marker must refer to authoritative root and resource owner");Put(ptr(p.s)+0x18,ptr(p.o));bind();
 Put(ptr(p.o),ptr(callback.s));Run(ptr(ship.e),101,101,"authoritative root must refer back to the same status marker");Put(ptr(p.o),ptr(p.s));bind();
 auto direct=resolved;
 direct.actor=ptr(p.a);
 {std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(direct);}
 Run(ptr(ship.e),101,101,"changing the published representation retires the old proof");
 bind();
 Run(ptr(ship.e),101,200,"authoritative current-player capture can publish the native actor directly");
 Put(ptr(p.a)+0x68,ptr(callback.t));
 bind();
 Run(ptr(ship.e),101,101,"direct actor authority still requires the reciprocal status marker");
 Put(ptr(p.a)+0x68,ptr(p.t));
 Put(ptr(p.o),ptr(callback.s));
 bind();
 Run(ptr(ship.e),101,101,"direct actor authority still requires the reciprocal root");
 Put(ptr(p.o),ptr(p.s));
 {std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(resolved);}
 bind();
 auto savedShip=ship;Run(ptr(ship.e),1000,1000,"complete native anchorage repair remains exactly capped after bridge repair");Check(!std::memcmp(&ship,&savedShip,sizeof ship),"representation bridge only adjusts native writer RBX and never stored health");

 constexpr uintptr_t server_vtable=0x20000, second_server_vtable=0x30000;
 init(server,200,7);
 init(foreign,201,7);
 Put(ptr(server.a),server_vtable);
 Put(ptr(foreign.a),second_server_vtable);
 psm::shiptrace::ConfigureNativeMetadataCatalogs(reinterpret_cast<uintptr_t>(&native.character_global),
     reinterpret_cast<uintptr_t>(&native.gimmick_global),reinterpret_cast<uintptr_t>(&native.vehicle_global),
     {server_vtable,second_server_vtable});
 {std::lock_guard lock(g_state_mutex);AssignTrackedPlayerResolveLocked(direct);}
 bind();
 Run(ptr(ship.e),101,101,"a native health query for the HUD mirror does not grant server hull ownership");
 Run(ptr(server.e),101,101,"same-UID server hull still requires an exact native query");
 query(ptr(foreign.o),ptr(foreign.e));
 Run(ptr(foreign.e),101,101,"other server vessel UID stays native");
 Put(ptr(foreign.a)+0x60,uint32_t{200});
 Put(ptr(foreign.gm)+0x48,uint16_t{124});
 query(ptr(foreign.o),ptr(foreign.e));
 Run(ptr(foreign.e),101,101,"same UID with mismatched vessel catalog identity stays native");
 Put(ptr(server.a),uintptr_t{0x40000});
 query(ptr(server.o),ptr(server.e));
 Run(ptr(server.e),101,101,"same UID and catalog with non-server RTTI stays native");
 Put(ptr(server.a),server_vtable);
 query(ptr(server.o),ptr(ship.e));
 Run(ptr(server.e),101,101,"query cannot pair a server status owner with the HUD entry");
 select();query(ptr(server.o),ptr(server.e));
 const auto savedServer=server;
 Run(ptr(server.e),101,200,"matching server hull scales the requested positive native change");
 Run(ptr(server.e),950,1000,"matching server hull repair is capped at native maximum");
 Run(ptr(server.e),90,90,"matching server hull preserves native damage");
 Run(ptr(ship.e),101,101,"server binding never scales the HUD mirror");
 Check(!std::memcmp(&server,&savedServer,sizeof server),"native pre-commit path does not write hull storage");
 Put(ptr(server.s)+0x18,ptr(foreign.o));
 Run(ptr(server.e),101,101,"a server status owner change revokes a published proof");
 Put(ptr(server.s)+0x18,ptr(server.o));
 Run(ptr(server.e),101,101,"restoring the stale server owner does not revive the proof");
 select();query(ptr(server.o),ptr(server.e));
 Put(ptr(p.r)+0x10,uint32_t{0});Put(ptr(hud)+0x370,uint32_t{0});
 Run(ptr(server.e),101,101,"live dismount revokes server hull scaling before a new HUD callback");
 psm::shiptrace::Enable(false);
}
int main(){
 auto config=Config();Reset(config);
 other.Set(0,438000,5350000,0,1);
 const auto original=other.bytes;
 Run(other.p(),274000,1,"Ogre lethal x4 adjustment respects native floor1");
 Check(other.bytes==original,"callback does not commit native resource memory");
 auto ctx=Context(other.p(),274000);TestStatWriteCallback(ctx,true);
 Check(static_cast<int64_t>(ctx.rbx)==1,"native result descriptor reports floor1");
 Check(static_cast<int64_t>(ctx.rbx)==*reinterpret_cast<int64_t*>(other.p()+0x28),"native now-at-floor comparison is true after bound correction");
 other.Set(0,438000,5350000,0,0);
 Run(other.p(),274000,1,"native floor0 still keeps a nonlethal request positive");
 Run(other.p(),0,0,"native lethal request stays at zero");
 other.Set(0,438000,5350000,0,1);
 Run(other.p(),274000,0,"legacy profile keeps historical behavior",false);

 Reset(config);Run(player.p(),400,490,"non-combat player fall damage keeps Health0.1");
 Run(player.p(),0,450,"native terminal fall request keeps clipped Health scaling");
 Run(player.p(),501,600,"player recovery keeps Heal100");
 Run(player.p(),500,500,"native no-op stays unchanged");
 config.damage.incoming.stat_write_fallback=true;Reset(config);
 Run(player.p(),400,499,"explicit incoming fallback still compounds configured incoming0.1");
 config=Config();Reset(config);
 player.Set(0,100,1000,-800,0);
 Run(player.p(),150,200,"upper bound uses base plus negative bonus");
 player.Set(0,900,1000,500,0);
 Run(player.p(),1200,1000,"positive bonus remains valid while preserving existing heal policy");
 player.Set(0,500,1000,0,0);
 config.health.heal_multiplier=1;Reset(config);
 Run(player.p(),501,501,"unchanged recovery candidate is not rewritten");

 config=Config();config.mount.enabled=true;config.mount.lock_health=true;config.mount.lock_value=9999999;
 Reset(config);mount.Set(0,3000000,5000000,-1000000,1);
 const auto m=reinterpret_cast<uintptr_t>(mount_owner.data());
 g_mount_resolve.actor=m+0x400;g_mount_resolve.marker=m+0x800;g_mount_resolve.root=m;
 g_mount_resolve.resource_owner=m;g_mount_resolve.health_entry=mount.p();g_mount_resolve.stamina_entry=m+0x100;
 g_mount_track_confidence=3;
 Run(mount.p(),2900000,5000000,"legacy mount fixture reaches configured lock ceiling",false);
 Run(mount.p(),2900000,4000000,"mount lock cannot exceed native upper bound");

 config=Config();Reset(config);
 other.Set(55,600,1000,0,0);Run(other.p(),500,500,"untracked non-health entry unchanged");
 other.Set(0,0,1000,0,0);Run(other.p(),0,0,"already-zero native health remains unchanged");
 player.Set(0,500,1000,0,2000);Run(player.p(),400,400,"invalid minimum/upper ordering reverts original native value");
 player.Set(0,500,(std::numeric_limits<int64_t>::max)(),1,0);
 Run(player.p(),400,400,"overflowing native upper reverts original native value");
 player.Set(0,500,1000,0,500);Run(player.p(),400,400,"inconsistent native input/bounds reverts original value");

 SYSTEM_INFO info{};GetSystemInfo(&info);const auto page=static_cast<size_t>(info.dwPageSize);
 auto* region=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 Check(region!=nullptr,"guard-page fixture allocated");
 if(region){
  const auto edge=reinterpret_cast<uintptr_t>(region+page-0x20);
  Put(edge,int32_t{0});Put(edge+8,int64_t{500});Put(edge+0x18,int64_t{1000});
  DWORD prior=0;Check(VirtualProtect(region+page,page,PAGE_NOACCESS,&prior)!=0,"guard-page fixture protected");
  g_player_resolve.health_entry=edge;
  Run(edge,400,400,"unreadable native bounds revert original value without SEH escape");
  VirtualFree(region,0,MEM_RELEASE);
 }
 OccupiedShipActualHookTests();
 ShipProductionRepresentationTests();
 std::printf("stat-write3446 checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
