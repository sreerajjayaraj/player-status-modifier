#include <Windows.h>
#include "NativeShipFixture.h"
#include <cstdio>
#include <thread>
#include <vector>
#include <cstdarg>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
void Log(const char*,...){}unsigned checks=0,failures=0;void Check(bool x,const char*n){++checks;if(!x){++failures;std::printf("FAIL %s\n",n);}}
#ifdef PSM_SHIP_PROOF_V2
#include "../../PSM/src/runtime/ship_proof.cpp"
#endif
int main(){using namespace shiptest;using namespace psm::shiptrace;Fixture f;Enable(true);f.Bind();int64_t out=0;Check(f.Write(101,&out)&&out==200,"fresh native frame/source-manager/VehicleInfo-cache/query proof boosts native healing");
 Put(f.player.ride,0x10,uint32_t{201});Put(f.hud,0x370,uint32_t{201});Put(f.ship.owner,0x58,P(f.enemy.entries));Put(f.enemy.owner,0x58,P(f.ship.entries));Check(!f.Write(90,&out),"immediate old-entry ABA loses ship exclusion before any next HUD callback");Put(f.player.ride,0x10,uint32_t{200});Put(f.hud,0x370,uint32_t{200});Put(f.ship.owner,0x58,P(f.ship.entries));Put(f.enemy.owner,0x58,P(f.enemy.entries));Check(!f.Write(101,&out),"restoring raw UID and entry after observed invalidation cannot resurrect old proof");f.Bind();Check(f.Write(101,&out)&&out==200,"fresh complete Actor Info query chain rebinds after UID ABA");
 Put(f.ship.gimmick,0x48,uint16_t{456});Check(!f.Write(101,&out),"native source lookup key change invalidates healing and exclusion");Put(f.ship.gimmick,0x48,uint16_t{123});Check(!f.Write(101,&out),"source-key A B A does not revive revoked proof");f.Bind();
 auto savedMeta=f.ship.meta;Put(f.ship.a,0x88,P(f.otherInfo));f.otherInfo[1]=7;Check(!f.Write(101,&out),"same-category metadata pointer switch invalidates binding");Put(f.ship.a,0x88,P(f.ship.meta));f.ship.meta=savedMeta;Check(!f.Write(101,&out),"metadata pointer restoration requires fresh native selection chain");f.Bind();
 Put(f.info,0x0A,uint8_t{1});Check(!f.Write(90,&out),"blocked metadata cannot retain native-preserve exemption");Put(f.info,0x0A,uint8_t{0});Check(!f.Write(101,&out),"unblock does not revive an old row proof");f.Bind();
 f.gimmickCache[123 * 3 + 1]=P(f.otherRow);Put(f.otherRow,0x2E8,uint16_t{3});Check(!f.Write(101,&out),"same vehicle reference in replacement source cache row still changes table generation");f.gimmickCache[123 * 3 + 1]=P(f.gimmickRow);f.Bind();Put(f.gimmickRow,0x2E8,uint16_t{4});Check(!f.Write(101,&out),"source record VehicleInfo internal reference remains current");Put(f.gimmickRow,0x2E8,uint16_t{3});f.Bind();f.otherInfo=f.info;f.vehicleCache[3 * 3 + 1]=P(f.otherInfo);Check(!f.Write(101,&out),"replaced current VehicleInfo cache row rejects stale native RAX row");f.vehicleCache[3 * 3 + 1]=P(f.info);f.Bind();
 Enable(true);f.ActorCall();f.InfoCall(P(f.otherFrame));f.Query();Check(!f.Write(101,&out),"wrong native frame cannot complete a pending getter selection");f.Bind();f.ActorCall();Put(f.ship.gimmick,0x48,uint16_t{456});f.gimmickCache[456 * 3 + 1]=P(f.gimmickRow);f.InfoCall();f.Query();Check(!f.Write(101,&out),"input switch between pre-getter and getter return rejects the returned static row");Put(f.ship.gimmick,0x48,uint16_t{123});f.Bind();
 {std::lock_guard lock(g_state_mutex);Check(f.Write(101,&out)&&out==101,"coherent owned hull under state-lock contention keeps positive native request without healing");Check(f.Write(90,&out)&&out==90,"coherent owned hull under state-lock contention keeps native loss");Check(f.Write(100,&out)&&out==100,"coherent owned hull under state-lock contention keeps equal native request");AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::resource_owner,P(f.ship.owner));Check(!f.Write(90,&out),"current source owner alias change defeats stale proof even while state lock unavailable");AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::resource_owner,P(f.player.owner));}
 Check(!f.Write(101,&out),"source authority alias A B A remains revoked after lock release");f.Bind();
#ifdef PSM_SHIP_PROOF_V2
 {std::lock_guard lock(psm::shipproof::proof_mutex);Check(f.Write(101,&out)&&out==101,"proof-mutex contention validates atomic snapshot and preserves only native healing");Check(f.Write(90,&out)&&out==90,"proof-mutex contention preserves current own hull native loss");Put(f.player.ride,0x10,uint32_t{201});Check(!f.Write(90,&out),"proof-mutex contention cannot hide a definitive native UID mismatch");Put(f.player.ride,0x10,uint32_t{200});Check(!f.Write(101,&out),"revocation during proof contention latches across raw UID restoration");}
 f.Bind();
#endif
 f.ship.meta[1]=1;Put(f.ship.status,0x30,uint16_t{77});f.Bind();Check(f.Write(101,&out)&&out==200,"native CharacterInfo branch uses status component internal key and source row reference");Put(f.ship.status,0x30,uint16_t{78});Check(!f.Write(101,&out),"CharacterInfo key switch invalidates cached vessel class");Put(f.ship.status,0x30,uint16_t{77});f.Bind();
 for(const auto category:std::array<uint8_t,7>{1,3,4,5,6,8,9}){f.ship.meta[1]=category;f.Bind();Check(f.Write(101,&out)&&out==200,"all native CharacterInfo dispatcher categories retain exact vessel support");}
 f.ship.meta[1]=2;f.Bind();Check(!f.Write(101,&out),"unsupported native dispatcher category cannot use a fabricated vessel record");f.ship.meta[1]=7;f.Bind();
 {std::lock_guard lock(g_state_mutex);AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::resource_owner,P(f.enemy.owner));AssignTrackedPlayerPointerFieldLocked(&ActorResolveSnapshot::resource_owner,P(f.player.owner));}
 Check(!f.Write(101,&out),"authority publication A B A without intervening ship attempt still changes identity generation");f.Bind();
#ifdef PSM_SHIP_PROOF_V2
 psm::shipproof::Words old_words{};psm::shipproof::published.Load(&old_words);const auto old_proof=psm::shipproof::Unpack(old_words);
 Put(f.ship.owner,0x58,P(f.enemy.entries));ObserveQuery(P(f.ship.owner),0,P(f.enemy.entries));Put(f.ship.owner,0x58,P(f.ship.entries));f.Query();Check(!f.Write(101,&out),"changed published owner array cannot rebind via query alone after raw restoration");f.Bind();psm::shipproof::Words new_words{};psm::shipproof::published.Load(&new_words);const auto new_proof=psm::shipproof::Unpack(new_words);psm::shipproof::Revoke(old_proof.epoch);Check(new_proof.epoch!=old_proof.epoch&&f.Write(101,&out)&&out==200,"retiring an old publication epoch cannot revoke the newer freshly bound proof");
#endif
 Enable(false);std::printf("native ship proof lifetime: %u checks %u failures\n",checks,failures);return failures?1:0;}
