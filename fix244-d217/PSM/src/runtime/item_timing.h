#pragma once
#include <cstddef>
#include <cstdint>
namespace psm::item_timing {
enum class Kind:unsigned {Plan=0,Push=1,Receipt=2};
enum class Phase:unsigned {Prepared=1,NativeBegin=2,NativeEnd=3};
using Clock=uint64_t(*)() noexcept;
struct Sample {
 uint64_t epoch=0,id=0,parent=0,frequency=0,ticks[5]{};
 uintptr_t object_raw=0,caller_raw=0,result_raw=0;
 uint32_t thread=0,scaled_items=0;Kind kind=Kind::Plan;bool prepared=false,completed=false,abnormal=false;
};
struct Span {Sample sample{};bool active=false,linkable=false;};
struct Statistics {uint64_t dropped=0,contended=0,rejected=0,clock_rejected=0;unsigned queued=0;bool sequence_exhausted=false;};
void Enable(bool on,Clock clock=nullptr,uint64_t frequency=0) noexcept;
Span Begin(Kind kind,uintptr_t object,uintptr_t caller=0,uint64_t parent=0,bool eligible=true) noexcept;
void Mark(Span& span,Phase phase) noexcept;
void End(Span& span,bool prepared,bool completed,bool abnormal,uintptr_t result=0,uint32_t scaled=0) noexcept;
std::size_t Drain(Sample* out,std::size_t capacity) noexcept;
Statistics GetStatistics() noexcept;
void Heartbeat() noexcept; // existing worker only; never call from a native callback
#ifdef PSM_ITEM_TIMING_TESTING
void TestLock() noexcept;void TestUnlock() noexcept;void TestSequence(uint64_t) noexcept;
#endif
}
