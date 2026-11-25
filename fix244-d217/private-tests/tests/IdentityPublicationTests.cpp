#include "runtime/runtime_state.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
unsigned checks=0,failures=0;void Check(bool x,const char*n){++checks;if(!x){++failures;std::printf("FAIL %s\n",n);}}
#ifdef PSM_PLAYER_IDENTITY_PUBLICATION
using Token=psm::authority::Token;
bool ReadToken(Token*t){return psm::authority::Read(t);}
void Assign(const ActorResolveSnapshot&v){AssignTrackedPlayerResolveLocked(v);}
#else
struct Token{uintptr_t actor=0,marker=0,root=0,resource_owner=0,health_entry=0,damage_source=0,damage_target=0;uint64_t generation=0;};
bool ReadToken(Token*){return false;}void Assign(const ActorResolveSnapshot&v){g_player_resolve=v;}
#endif
int main(){ActorResolveSnapshot a{};a.actor=0x10000200;a.marker=0x10000300;a.root=0x10000400;a.resource_owner=0x10000500;a.health_entry=0x10000600;a.damage_source=0x10000700;a.damage_target=0x10000800;a.stamina_type=19;a.spirit_type=20;Token t{},again{};{std::lock_guard lock(g_state_mutex);Assign(a);}Check(ReadToken(&t)&&t.actor==a.actor&&t.marker==a.marker&&t.resource_owner==a.resource_owner&&t.health_entry==a.health_entry&&t.damage_source==a.damage_source&&t.damage_target==a.damage_target,"whole player assignment publishes a coherent current ship-authority tuple");Check(std::memcmp(&a,&g_player_resolve,sizeof a)==0,"authority publication preserves exact existing player snapshot assignment");{std::lock_guard lock(g_state_mutex);Assign(a);}Check(ReadToken(&again)&&again.generation==t.generation,"identical routine player refresh does not change authority generation");
#ifdef PSM_PLAYER_IDENTITY_PUBLICATION
 {std::lock_guard lock(g_state_mutex);AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::resource_owner,0x10000900);}
#endif
 Check(ReadToken(&again)&&again.actor==a.actor&&again.marker==a.marker&&again.resource_owner==0x10000900&&again.generation!=t.generation,"same-hero resource owner change advances generation");t=again;
#ifdef PSM_PLAYER_IDENTITY_PUBLICATION
 {std::lock_guard lock(g_state_mutex);AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::health_entry,0x10000A00);Check(ReadToken(&again)&&again.health_entry==0x10000A00,"busy reader sees current health identity without acquiring state mutex");}
#endif
 Check(ReadToken(&again)&&again.generation!=t.generation,"partial health identity write invalidates old authority generation");
#ifdef PSM_PLAYER_IDENTITY_PUBLICATION
 auto words=psm::authority::detail::published.LoadForWriter();psm::authority::detail::published.BeginWrite();Check(!ReadToken(&t),"in-progress odd publication never exposes old authority as current");psm::authority::detail::published.EndWrite(words);Check(ReadToken(&t),"complete even publication becomes readable again");
 std::atomic<bool> done{false};std::atomic<unsigned> torn{0},samples{0};std::thread writer([&]{for(uintptr_t n=1;n<20000;++n){auto v=a;v.actor=0x20000000+n*0x100;v.marker=v.actor+8;v.root=v.actor+16;v.resource_owner=v.actor+24;v.health_entry=v.actor+32;v.damage_source=v.actor+40;v.damage_target=v.actor+48;std::lock_guard lock(g_state_mutex);Assign(v);}done=true;});while(!done.load()){Token x{};if(ReadToken(&x)&&x.actor>=0x20000000){++samples;if(x.marker!=x.actor+8||x.root!=x.actor+16||x.resource_owner!=x.actor+24||x.health_entry!=x.actor+32||x.damage_source!=x.actor+40||x.damage_target!=x.actor+48)++torn;}}writer.join();Check(samples.load()>0&&torn.load()==0,"bounded atomic-word reads never produce torn authority during concurrent assignments");
#endif
 {std::lock_guard lock(g_state_mutex);Assign({});}Check(ReadToken(&t)&&!t.actor&&!t.marker,"player reset publishes explicit invalid identity rather than retaining old owner");std::printf("player identity publication: %u checks %u failures\n",checks,failures);return failures?1:0;}
