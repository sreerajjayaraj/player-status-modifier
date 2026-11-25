#pragma once
#define PSM_PLAYER_IDENTITY_PUBLICATION 1
#include "runtime/atomic_words.h"
#include "runtime/runtime_state.h"
#include <limits>
namespace psm::authority {
struct Token {
 uintptr_t actor=0,marker=0,root=0,resource_owner=0,health_entry=0,damage_source=0,damage_target=0;
 uint64_t generation=0;
 bool operator==(const Token&)const=default;
};
namespace detail {
inline AtomicWords<8> published{};
inline std::atomic<bool> exhausted{false};
inline AtomicWords<8>::Words WordsFor(const ActorResolveSnapshot&x){
 auto old=published.LoadForWriter();AtomicWords<8>::Words next={x.actor,x.marker,x.root,x.resource_owner,x.health_entry,x.damage_source,x.damage_target,old[7]};
 bool changed=false;for(unsigned i=0;i<7;++i)changed|=old[i]!=next[i];
 if(changed){if(old[7]==UINT64_MAX)exhausted.store(true,std::memory_order_release);else ++next[7];}
 return next;
}
}
inline uintptr_t HealthEntryHint(){return detail::published.LoadHint<4>();}
inline bool Read(Token*out){
 if(!out||detail::exhausted.load(std::memory_order_acquire))return false;
 AtomicWords<8>::Words x{};if(!detail::published.Load(&x))return false;
 *out={x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7]};return true;
}
// State mutex is already held. This also synchronizes authored fixtures that
// initialize the global directly; production identity writes use Assign below.
inline void SynchronizeLocked(){
 const auto next=detail::WordsFor(g_player_resolve),old=detail::published.LoadForWriter();if(next!=old)detail::published.Store(next);
}
}
// These helpers preserve the exact existing assignment and its existing lock.
// Odd publication covers the write itself; busy readers cannot use an old tuple
// as current during an actor/owner/health/source identity transition.
inline void AssignTrackedPlayerResolveLocked(const ActorResolveSnapshot&next){
 const auto words=psm::authority::detail::WordsFor(next);
 psm::authority::detail::published.BeginWrite();g_player_resolve=next;psm::authority::detail::published.EndWrite(words);
}
inline void AssignTrackedPlayerPointerFieldLocked(uintptr_t ActorResolveSnapshot::*field,uintptr_t value){
 auto next=g_player_resolve;next.*field=value;AssignTrackedPlayerResolveLocked(next);
}
