#include "config.h"
#include "mod_logic.h"
#include "runtime/damage_logic.h"
#include "runtime/actor_resolve.h"
#include "runtime/stat_logic.h"
#include <cstring>
#include "runtime/runtime_state.h"
#include "runtime/native_resource_scaling.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main() {
    ModConfig config{};
    config.general.enabled = true;
    config.health.consumption_multiplier = 0.1;
    config.health.heal_multiplier = 100.0;
    config.damage.incoming.enabled = true;
    config.damage.incoming.multiplier = 0.1;
    config.mount.enabled = false;
    SetConfigSnapshot(L"", config);
    g_runtime_enabled.store(true);

    // Exercise the real production handler against isolated actor storage.
    // No game process, hooks, worker threads, or game functions are started.
    alignas(16) std::array<std::array<std::byte, 0x2000>, 3> actors{};
    const char* names[] = {"Kliff", "Damiane", "Oongka"};
    int failures = 0;
    for (size_t actor = 0; actor < actors.size(); ++actor) {
        const auto owner = reinterpret_cast<uintptr_t>(actors[actor].data());
        g_player_resolve = {};
        g_player_resolve.actor = owner + 0x400;
        g_player_resolve.marker = owner + 0x800;
        g_player_resolve.root = owner;
        g_player_resolve.damage_target = owner;
        g_mount_resolve = {};

        for (const int selector : {17, 19, 20, 21, 22, 23, 24, 36, 55}) {
            for (const int64_t original : {-40000LL, -5000LL, 400LL, 300LL, 100000LL}) {
                int64_t delta = original;
                const bool adjusted = TryScalePlayerDamage(owner, selector, 0, 0, &delta);
                if (adjusted || delta != original) {
                    std::printf("FAIL: %s non-health selector=%d delta=%lld became=%lld adjusted=%d\n",
                        names[actor], selector, original, delta, adjusted);
                    ++failures;
                }
            }
        }

        int64_t spirit_cost = -5000;
        TryScalePlayerDamage(owner, 23, 0, 0, &spirit_cost);
        spirit_cost = psm::native_resource::ScaleSigned(spirit_cost, 10.0, 20.0);
        if (spirit_cost != -50000) {
            std::printf("FAIL: %s Spirit cost should scale once to -50000, got %lld\n", names[actor], spirit_cost);
            ++failures;
        }
        int64_t spirit_heal = 400;
        TryScalePlayerDamage(owner, 23, 0, 0, &spirit_heal);
        spirit_heal = psm::native_resource::ScaleSigned(spirit_heal, 10.0, 20.0);
        if (spirit_heal != 8000) {
            std::printf("FAIL: %s Spirit heal should scale once to 8000, got %lld\n", names[actor], spirit_heal);
            ++failures;
        }
        int64_t health_cost = -100000;
        const bool health_adjusted = TryScalePlayerDamage(owner, 0, 0, 0, &health_cost);
        if (!health_adjusted || health_cost < -1001 || health_cost > -1000) {
            std::printf("FAIL: %s health damage no longer uses health/incoming config\n", names[actor]);
            ++failures;
        }
    }
    // Same-shape enemy health must never inherit the player's enabled heal or
    // consumption config. Exercise the production generic write handler with
    // a valid authoritative player binding and terminal native writes.
    alignas(16) std::array<std::byte,0x300> player{}, enemy{};
    auto put=[](auto&b,size_t at,auto v){std::memcpy(b.data()+at,&v,sizeof(v));};
    const auto player_entry=reinterpret_cast<uintptr_t>(player.data());
    const auto enemy_entry=reinterpret_cast<uintptr_t>(enemy.data());
    put(player,0,int32_t{0});put(player,8,int64_t{500});put(player,0x18,int64_t{1000});
    put(enemy,0,int32_t{0});put(enemy,8,int64_t{50});put(enemy,0x18,int64_t{1000});
    g_player_resolve={};g_player_resolve.actor=player_entry+0x100;
    g_player_resolve.marker=player_entry+0x180;g_player_resolve.root=player_entry;
    g_player_resolve.resource_owner=player_entry;g_player_resolve.damage_target=player_entry;
    g_player_resolve.health_entry=player_entry;g_mount_resolve={};
    config.health.enabled=true;config.damage.outgoing.enabled=true;
    config.damage.outgoing.stat_write_fallback=true;config.damage.outgoing.multiplier=4;
    SetConfigSnapshot(L"",config);
    int64_t lethal=25;
    if(!TryAdjustStatWrite(enemy_entry,&lethal,0)||lethal!=1||
       g_player_resolve.health_entry!=player_entry){
        std::puts("FAIL: fallback created a death from a native nonlethal hit");++failures;
    }
    config.damage.outgoing.multiplier=0.25;
    SetConfigSnapshot(L"",config);
    lethal=0;TryAdjustStatWrite(enemy_entry,&lethal,0);
    if(lethal!=0){std::puts("FAIL: native terminal enemy write resurrected by Health config");++failures;}
    int64_t enemy_heal=60;
    if(TryAdjustStatWrite(enemy_entry,&enemy_heal,0)||enemy_heal!=60){
        std::puts("FAIL: enemy heal inherited player HealMultiplier");++failures;
    }

    // Model the current native actor's 0x90-strided Health bank and shifted
    // selectors observed at encounter entry. No incoming hit primes the bank.
    alignas(16) std::array<std::byte,0x1000> native_actor{}, table{}, marker{}, owner{}, bank{}, metadata{};
    auto addr=[](auto& b){return reinterpret_cast<uintptr_t>(b.data());};
    auto init_actor=[&](uint32_t uid){
        put(native_actor,0x60,uid); put(native_actor,0x68,addr(table)); put(native_actor,0x88,addr(metadata));
        put(metadata,1,uint8_t{1}); put(table,0x20,addr(marker)); put(marker,8,addr(native_actor));
        put(marker,0x18,addr(owner)); put(owner,0,addr(marker)); put(owner,0x58,addr(bank));
        auto stat=[&](size_t off,int32_t selector,int64_t max){
            put(bank,off,selector);put(bank,off+8,max);put(bank,off+0x18,max);
        };
        stat(0,0,1650000);stat(0x6C0,22,440000);stat(0x750,23,170000);
    };
    {
        std::lock_guard lock(g_state_mutex);
        ResetTrackedEntriesLocked();
    }
    init_actor(0xA0100001);
    UpdateTrackedPlayerStatusComponent(addr(native_actor),addr(marker),true);
    ObserveStatEntry(addr(bank),addr(owner));
    if(GetTrackedPlayerHealthEntry()!=addr(bank)){
        std::puts("FAIL: active native Health lookup needs a first damage write");++failures;
    }


    // The native controller callback may report a separate combat actor
    // with the current playable UID. Authenticate both banks, keep the
    // primary resource snapshot, and retire the server binding on a change.
    alignas(16) std::array<std::byte,0x1000> server_actor{}, server_table{}, server_marker{}, server_owner{}, server_bank{}, next_bank{}, boss_bank{};
    const auto server_vtable=addr(metadata);
    auto stat_in=[](auto& storage,size_t off,int32_t selector,int64_t max){
        std::memcpy(storage.data()+off,&selector,sizeof(selector));
        std::memcpy(storage.data()+off+8,&max,sizeof(max));
        std::memcpy(storage.data()+off+0x18,&max,sizeof(max));
    };
    put(server_actor,0,server_vtable);put(server_actor,0x68,addr(server_table));put(server_actor,0x88,addr(metadata));
    put(server_table,0x20,addr(server_marker));put(server_marker,8,addr(server_actor));
    put(server_marker,0x18,addr(server_owner));put(server_owner,0,addr(server_marker));
    stat_in(server_bank,0,0,1650000);stat_in(next_bank,0,0,1650000);
    stat_in(boss_bank,0,0,5350000);

    config.damage.outgoing.multiplier=1.2;
    SetConfigSnapshot(L"",config);
    psm::combat_health::SetServerActorVtable(server_vtable);
    auto check=[&](bool ok,const char* what){
        if(!ok){std::printf("FAIL: %s\n",what);++failures;}
    };
    auto exercise_encounter=[&](uintptr_t server_health,uintptr_t primary_owner,uintptr_t primary_health){
        check(psm::combat_health::HealthEntry(addr(server_owner))==server_health,"server lookup before the first hit");
        check(psm::combat_health::HealthEntry(primary_owner)==primary_health,"primary bank survives the server binding");
        int64_t incoming=-100000;
        check(TryScalePlayerDamage(addr(server_owner),0,0,0,&incoming,true) && incoming==-10000,
              "first boss hit takes the configured incoming multiplier on the native server bank");
        int64_t health_request=1650000+incoming;
        check(TryAdjustStatWrite(server_health,&health_request,0) && health_request==1649000,
              "server write applies the player Health factor once and does not inherit outgoing damage");
        int64_t outgoing=5350000-138000;
        check(TryAdjustStatWrite(addr(boss_bank),&outgoing,0) && outgoing==5350000-165600,
              "first outgoing boss hit receives 1.2x with a bound player bank");
        int64_t lethal_write=0;
        check(!TryAdjustStatWrite(addr(boss_bank),&lethal_write,0) && lethal_write==0,
              "native finishing write retains its terminal decision");
        put(boss_bank,0x28,int64_t{5000000});
        int64_t gated=5020000;
        check(TryAdjustStatWrite(addr(boss_bank),&gated,0) && gated==5000000,
              "native boss phase floor remains in effect when scaled damage crosses it");
        put(boss_bank,0x28,int64_t{0});
    };
    put(server_actor,0x60,uint32_t{0xA0100009});
    put(server_owner,0x58,addr(server_bank));
    psm::combat_health::ObserveServerActor(addr(server_actor));
    check(!psm::combat_health::HealthEntry(addr(server_owner)),"foreign playable actor cannot become the server player");

    // Live 3732 damage events repeatedly offered plausible but unrelated
    // Health/Stamina/Spirit triples while the controlled actor was stable.
    stat_in(server_bank,0x5A0,25,100000);
    stat_in(server_bank,0x1B0,18,100000);
    const auto before_foreign_event = g_player_resolve;
    IsRelevantDamageEvent(addr(server_bank),0,0,10000);
    check(g_player_resolve.health_entry == before_foreign_event.health_entry &&
          g_player_resolve.stamina_entry == before_foreign_event.stamina_entry &&
          g_player_resolve.spirit_entry == before_foreign_event.spirit_entry,
          "foreign Health activity cannot replace the active player's resource bank");
    put(server_actor,0x60,uint32_t{0xA0100001});
    put(server_actor,0,server_vtable+8);
    psm::combat_health::ObserveServerActor(addr(server_actor));
    check(!psm::combat_health::HealthEntry(addr(server_owner)),"wrong server vtable refused");
    put(server_actor,0,server_vtable);
    put(server_owner,0,addr(marker));
    psm::combat_health::ObserveServerActor(addr(server_actor));
    check(!psm::combat_health::HealthEntry(addr(server_owner)),"foreign marker and unlinked banks refused");
    put(server_owner,0,addr(server_marker));
    psm::combat_health::ObserveServerActor(addr(server_actor));
    exercise_encounter(addr(server_bank),addr(owner),addr(bank));

    // Same-address actor across a reload, then the matching new server bank.
    {
        std::lock_guard lock(g_state_mutex);
        ResetTrackedEntriesLocked();
    }
    init_actor(0xA0100006);
    UpdateTrackedPlayerStatusComponent(addr(native_actor),addr(marker),true);
    check(!psm::combat_health::HealthEntry(addr(server_owner)),"stale server authority expires across a rematch");
    put(server_actor,0x60,uint32_t{0xA0100006});
    put(server_owner,0x58,addr(next_bank));
    psm::combat_health::ObserveServerActor(addr(server_actor));
    exercise_encounter(addr(next_bank),addr(owner),addr(bank));
    check(!psm::combat_health::HealthEntry(0,addr(server_bank)),"old health address cannot be used by the new encounter");

    // A live bank move also invalidates the previous proof before a new observation.
    put(server_owner,0x58,addr(server_bank));
    check(!psm::combat_health::HealthEntry(addr(server_owner)),"in-place bank replacement invalidates live proof");
    psm::combat_health::ObserveServerActor(addr(server_actor));
    check(psm::combat_health::HealthEntry(addr(server_owner))==addr(server_bank),"updated native bank can be observed again");
    put(owner,0x58,uintptr_t{0});
    put(server_owner,0x58,addr(next_bank));
    psm::combat_health::ObserveServerActor(addr(server_actor));
    check(psm::combat_health::HealthEntry(addr(server_owner))==addr(next_bank),
          "authenticated server Health works before the display health array is ready");

    // The same native capture produced category 4 for both switched heroes.
    // Keep the old actor/bank reachable while the authoritative capture moves.
    struct HeroBank {
        std::array<std::byte,0x1000> actor{}, parts{}, status{}, owner{}, stats{}, metadata{};
    };
    std::array<HeroBank,2> switched{};
    for (size_t i=0;i<switched.size();++i) {
        auto& h=switched[i];
        const uint32_t uid=0xB0100002U+static_cast<uint32_t>(i);
        put(h.actor,0x60,uid); put(h.actor,0x68,addr(h.parts)); put(h.actor,0x88,addr(h.metadata));
        put(h.metadata,1,uint8_t{4}); put(h.parts,0x20,addr(h.status));
        put(h.status,8,addr(h.actor)); put(h.status,0x18,addr(h.owner));
        put(h.owner,0,addr(h.status)); put(h.owner,0x58,addr(h.stats));
        stat_in(h.stats,0,0,1650000);
        UpdateTrackedPlayerStatusComponent(addr(h.actor),addr(h.status),true);
        check(GetTrackedPlayerHealthEntry()==addr(h.stats) &&
              psm::combat_health::HealthEntry(addr(h.owner))==addr(h.stats),
              i==0?"Damiane binds fresh Health without a first damage write":
                   "Oongka binds fresh Health without a first damage write");
        check(!psm::combat_health::HealthEntry(i?addr(switched[i-1].owner):addr(owner)) &&
              !psm::combat_health::HealthEntry(addr(server_owner)),
              "switched character does not inherit old player or server Health");
        put(metadata,1,uint8_t{4});
        put(server_actor,0x60,uid+1);
        psm::combat_health::ObserveServerActor(addr(server_actor));
        check(!psm::combat_health::HealthEntry(addr(server_owner)),
              "another category-4 actor cannot borrow the controlled player's Health");
        put(server_actor,0x60,uid);
        psm::combat_health::ObserveServerActor(addr(server_actor));
        check(psm::combat_health::HealthEntry(addr(server_owner))==addr(next_bank),
              "matching category-4 server mirror follows the switch");
        exercise_encounter(addr(next_bank),addr(h.owner),addr(h.stats));
    }
    put(metadata,1,uint8_t{1});
    put(owner,0x58,addr(bank));
    UpdateTrackedPlayerStatusComponent(addr(native_actor),addr(marker),true);
    check(psm::combat_health::HealthEntry(addr(owner))==addr(bank) &&
          !psm::combat_health::HealthEntry(addr(server_owner)) &&
          !psm::combat_health::HealthEntry(addr(switched[1].owner)),
          "return to Kliff retires the other hero's Health and mirror");
    psm::combat_health::SetServerActorVtable(0);
    {
        std::lock_guard lock(g_state_mutex);
        ResetTrackedEntriesLocked();
    }

    // Non-combat health decrement (the generic fall/environmental route).
    // Mirror the captured runtime INI, including the ABSENT incoming fallback
    // option. Parsing it proves the generic route uses Health0.1 once; the
    // combat-only IncomingDamage0.1 must not compound it to0.01 here.
    const auto fall_dir=std::filesystem::temp_directory_path()/
        (L"PSM-Fall-Regression-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(fall_dir);
    const auto fall_ini=fall_dir/L"player-status-modifier.ini";
    {
        std::ofstream fixture(fall_ini);
        fixture << "[General]\nEnabled=1\n[OutgoingDamage]\nUseStatWriteFallback=1\nEnabled=1\nMultiplier=4\n"
                << "[IncomingDamage]\nEnabled=1\nMultiplier=0.1\n"
                << "[Mount]\nEnabled=1\nLockHealth=0\nLockStamina=1\nLockValue=9999999\n"
                << "[SpecialMount]\nEnabled=1\nLockHealth=0\nLockStamina=1\nLockValue=9999999\n"
                << "[Health]\nEnabled=1\nConsumptionMultiplier=0.1\nHealMultiplier=100\n"
                << "[Stamina]\nEnabled=1\nConsumptionMultiplier=1.1\nHealMultiplier=100\n"
                << "[Spirit]\nEnabled=1\nConsumptionMultiplier=1.5\nHealMultiplier=20\n";
    }
    ModConfig fall_config{};
    if(!ReadConfigSnapshot(fall_ini.wstring(),&fall_config)||!fall_config.health.enabled||
       fall_config.health.consumption_multiplier!=0.1||fall_config.health.heal_multiplier!=100||
       !fall_config.damage.incoming.enabled||fall_config.damage.incoming.multiplier!=0.1||
       fall_config.damage.incoming.stat_write_fallback){
        std::puts("FAIL: captured fall-damage INI semantics changed");++failures;
    }
    SetConfigSnapshot(fall_ini.wstring(),fall_config);
    for(size_t actor=0;actor<actors.size();++actor){
        const auto owner=reinterpret_cast<uintptr_t>(actors[actor].data());
        const auto health=owner+0x100;
        const int32_t selector=0;const int64_t current=500,maximum=1000;
        std::memcpy(reinterpret_cast<void*>(health),&selector,sizeof(selector));
        std::memcpy(reinterpret_cast<void*>(health+8),&current,sizeof(current));
        std::memcpy(reinterpret_cast<void*>(health+0x18),&maximum,sizeof(maximum));
        g_player_resolve={};g_mount_resolve={};
        g_player_resolve.actor=owner+0x400;g_player_resolve.marker=owner+0x800;
        g_player_resolve.root=g_player_resolve.resource_owner=owner;
        g_player_resolve.health_entry=health;g_player_resolve.stamina_entry=health+0x90;
        g_player_resolve.spirit_entry=health+0x120;
        // No attacker, combat context, damage owner or preceding damage hook.
        int64_t requested=400;
        if(!TryAdjustStatWrite(health,&requested,0)||requested!=490||
           g_player_resolve.damage_source||g_player_resolve.damage_target){
            std::printf("FAIL: %s generic fall loss100 must become10 without combat context; got%lld\n",
                        names[actor],static_cast<long long>(requested));++failures;
        }
        requested=0;
        if(!TryAdjustStatWrite(health,&requested,0)||requested!=450){
            std::printf("FAIL: %s generic terminal fall request must retain Health0.1 scaling; got%lld\n",
                        names[actor],static_cast<long long>(requested));++failures;
        }
        int64_t stored=0;std::memcpy(&stored,reinterpret_cast<const void*>(health+8),sizeof(stored));
        if(stored!=500){std::puts("FAIL: health handler changed native storage before native commit");++failures;}
    }
    std::filesystem::remove(fall_ini);std::filesystem::remove(fall_dir);

    // Native 3446 API18E2500 forwards a signed health delta. Positive values
    // add health; OutgoingDamage must not multiply another actor's health gain.
    alignas(16) std::array<std::byte,0x2000> source_context{}, recipient{};
    const auto source=reinterpret_cast<uintptr_t>(source_context.data());
    const auto other_owner=reinterpret_cast<uintptr_t>(recipient.data());
    g_player_resolve={};g_mount_resolve={};
    g_player_resolve.actor=player_entry+0x100;g_player_resolve.marker=player_entry+0x180;
    g_player_resolve.root=g_player_resolve.resource_owner=player_entry;
    g_player_resolve.health_entry=player_entry;g_player_resolve.damage_target=player_entry;
    g_player_resolve.damage_source=player_entry+0x100;
    put(source_context,0x68,g_player_resolve.damage_source);
    config.general.enabled=true;config.health.enabled=true;config.health.heal_multiplier=100;
    config.damage.outgoing.enabled=true;config.damage.outgoing.multiplier=4;
    SetConfigSnapshot(L"",config);
    for(const int64_t original_gain:{1LL,10LL,100LL}){
        auto gain=original_gain;
        if(TryScalePlayerDamage(other_owner,0,0,source,&gain)||gain!=original_gain){
            std::puts("FAIL: outgoing damage multiplier altered native positive health gain");++failures;
        }
    }
    int64_t attack=-10;
    if(!TryScalePlayerDamage(other_owner,0,0,source,&attack)||attack!=-40){
        std::puts("FAIL: signed negative outgoing health damage lost x4 scaling");++failures;
    }
    int64_t own_heal=10;
    if(!TryScalePlayerDamage(player_entry,0,0,source,&own_heal)||own_heal!=1000){
        std::puts("FAIL: player HealMultiplier changed while fixing other actor health gains");++failures;
    }

    std::printf("damage/resource isolation: %d failures\n", failures);
    return failures ? 1 : 0;
}
