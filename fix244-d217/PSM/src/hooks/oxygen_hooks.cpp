#include "hooks/game_hooks.h"
#include "hooks/oxygen_hooks.h"
#include "runtime/oxygen_resource.h"
#include "native_3446.h"
#include "logger.h"
#include <safetyhook.hpp>
#include <atomic>
#include <utility>
namespace {
SafetyHookMid breath_hook, gauge_sample_hook;
std::atomic<ULONGLONG> next_sample{0};
std::atomic<unsigned> sample_count{0};
std::atomic<uintptr_t> trace_actor{0};

void ObserveDisplayedBreath(SafetyHookContext& ctx) {
 const auto now=GetTickCount64();
 auto deadline=next_sample.load(std::memory_order_relaxed);
 if (now<deadline || !next_sample.compare_exchange_strong(deadline,now+500)) return;
 OxygenHudSample sample{};
 if (!ReadPlayerOxygenHud(ctx.rsi,ctx.rbx,&sample) ||
     (sample.shown>=sample.shown_max && (!sample.raw_available || sample.rate>=0))) return;
 if (trace_actor.exchange(ctx.rbx,std::memory_order_relaxed)!=ctx.rbx)
  sample_count.store(0,std::memory_order_relaxed);
 if (sample_count.fetch_add(1,std::memory_order_relaxed)>=96) return;
 Log("oxygen: hud selector=%u shown=%lld shown-max=%lld stored=%lld rate=%lld max=%lld min=%lld raw=%d same-player=%d entry-proof=%d control-bound=%d",
     static_cast<unsigned>(sample.selector),static_cast<long long>(sample.shown),
     static_cast<long long>(sample.shown_max),static_cast<long long>(sample.stored),
     static_cast<long long>(sample.rate),static_cast<long long>(sample.maximum),
     static_cast<long long>(sample.minimum),sample.raw_available?1:0,sample.same_player?1:0,sample.entry_proved?1:0,sample.control_bound?1:0);
}
void ObserveBreath(SafetyHookContext& ctx) {
 // At the breath-specific native call: RCX gauge, RDX actor.
 ObservePlayerOxygenGauge(ctx.rcx,ctx.rdx);
}
}
psm::oxygen::Sites PreparePlayerOxygenHooks(){
 __try{
  const auto module=GetModuleHandleW(L"CrimsonDesert.exe");
  return psm::oxygen::Scan(module,psm::v3446::ModuleSize(module));
 }__except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
bool InstallPlayerOxygenHooks(const psm::oxygen::Sites& sites, bool stats_observer_ready){
 bool gauge_ready=false;
 if(sites.breath_before_call&&stats_observer_ready){
  auto hook=psm::game::CreateMid(reinterpret_cast<void*>(sites.breath_before_call),ObserveBreath);
  if(hook){breath_hook=std::move(*hook);gauge_ready=true;}
  else Log("oxygen: optional breath gauge observer installation failed");
 }
 const bool catalog_ready=bool(sites.catalog);
 if(!gauge_ready&&!catalog_ready){Log("oxygen: native authority unavailable; option stays inert");return false;}
 const auto server_actor_vtable = stats_observer_ready ? sites.server_actor_vtable : 0;
 bool trace_ready=false;
 const auto& witness=psm::oxygen::kGaugeSampleWitness;
 const auto* sample=reinterpret_cast<const void*>(sites.gauge_sample);
 if (catalog_ready && psm::game::ExecutableSpan(psm::game::MainModule(),sites.gauge_sample,witness.size()) &&
     !std::memcmp(sample,witness.data(),witness.size())) {
  auto trace=psm::game::CreateMid(const_cast<void*>(sample),ObserveDisplayedBreath,SafetyHookMid::StartDisabled);
  if (trace && trace->original_bytes().size()<=witness.size() &&
      !std::memcmp(trace->original_bytes().data(),witness.data(),trace->original_bytes().size()) &&
      psm::game::EnablePreparedHook(*trace)) {
   gauge_sample_hook=std::move(*trace);
   trace_ready=true;
  }
 }
 const auto control_slot=trace_ready ? sites.client_control_slot : 0;
 const auto server_status_vtable = control_slot && stats_observer_ready ? sites.server_status_vtable : 0;
 SetPlayerOxygenEnabled(true,sites.catalog,server_actor_vtable,control_slot,server_status_vtable);
 Log("oxygen: HUD trace=%d local-control=%d",trace_ready?1:0,control_slot?1:0);
 Log("oxygen: authority ready native-catalog=%d gauge-observer=%d server-mirror=%d server-minimum-rule=%d player-only=1 stored-value-writes=0",
     catalog_ready?1:0,gauge_ready?1:0,(control_slot?server_status_vtable:server_actor_vtable)?1:0,
     server_status_vtable?1:0);
 return true;
}
void RemovePlayerOxygenHooks(){
 SetPlayerOxygenEnabled(false);
 gauge_sample_hook.reset();
 breath_hook.reset();
}
