#include "runtime/item_timing.h"
#include "logger.h"
#include <Windows.h>
#include <atomic>
#include <array>
#include <limits>
namespace psm::item_timing {
namespace {
struct ErrorGuard {DWORD saved=GetLastError();~ErrorGuard(){SetLastError(saved);}};
SRWLOCK lock=SRWLOCK_INIT;
std::atomic<bool> enabled{false},exhausted{false};
std::atomic<uint64_t> epoch{0},sequence{0},frequency{0},dropped{0},contended{0},rejected{0},clockRejected{0},reportedDrops{0},reportedEpoch{0};
std::atomic<unsigned> summaryCount{0};
std::atomic<Clock> clockFunction{nullptr};
std::array<Sample,128> packets{};std::array<unsigned,3> budgets{};unsigned head=0,count=0;
uint64_t RealClock()noexcept{LARGE_INTEGER q{};return QueryPerformanceCounter(&q)?static_cast<uint64_t>(q.QuadPart):0;}
uint64_t Now()noexcept{const auto fn=clockFunction.load(std::memory_order_acquire);return fn?fn():RealClock();}
bool TryLock()noexcept{if(TryAcquireSRWLockExclusive(&lock))return true;contended.fetch_add(1,std::memory_order_relaxed);return false;}
double Microseconds(const Sample&s,unsigned from,unsigned to)noexcept{
 return s.frequency&&s.ticks[from]&&s.ticks[to]>=s.ticks[from]?double(s.ticks[to]-s.ticks[from])*1000000.0/double(s.frequency):-1.0;
}
}
void Enable(bool on,Clock clock,uint64_t hz)noexcept{
 ErrorGuard preserve;enabled.store(false,std::memory_order_release);AcquireSRWLockExclusive(&lock);
 epoch.fetch_add(1,std::memory_order_acq_rel); // odd initialization epoch cannot be observed as ready
 if(!clock&&on){LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)on=false;else hz=static_cast<uint64_t>(f.QuadPart);}
 if(clock&&!hz)on=false;clockFunction.store(clock,std::memory_order_release);frequency.store(hz,std::memory_order_release);
 sequence.store(0);exhausted.store(false);head=count=0;budgets={};
 dropped.store(0);contended.store(0);rejected.store(0);clockRejected.store(0);reportedDrops.store(0);reportedEpoch.store(0);summaryCount.store(0);
 epoch.fetch_add(1,std::memory_order_release); // publish complete even epoch before ready
 enabled.store(on,std::memory_order_release);ReleaseSRWLockExclusive(&lock);
}
Span Begin(Kind kind,uintptr_t object,uintptr_t caller,uint64_t parent,bool eligible)noexcept{
 ErrorGuard preserve;Span s{};
 if(!eligible||static_cast<unsigned>(kind)>2||!enabled.load(std::memory_order_acquire)||exhausted.load(std::memory_order_acquire))return s;
 const auto capturedEpoch=epoch.load(std::memory_order_acquire);
 if(capturedEpoch&1)return s;
 const auto before=sequence.fetch_add(1,std::memory_order_relaxed);
 if(before==(std::numeric_limits<uint64_t>::max)()){exhausted.store(true,std::memory_order_release);dropped.fetch_add(1);return s;}
 s.sample.epoch=capturedEpoch;s.sample.id=before+1;s.sample.kind=kind;
 s.sample.object_raw=object;s.sample.caller_raw=caller;s.sample.thread=GetCurrentThreadId();
 // Only Receipt gets a parent from the still-live existing TLS Push scope.
 // Plan and Push never infer identity across a returned native function.
 s.sample.parent=kind==Kind::Receipt?parent:0;
 s.sample.frequency=frequency.load(std::memory_order_acquire);s.sample.ticks[0]=Now();
 if(capturedEpoch!=epoch.load(std::memory_order_acquire)||!enabled.load(std::memory_order_acquire))return {};
 s.active=true;return s;
}
void Mark(Span&s,Phase p)noexcept{
 ErrorGuard preserve;const auto at=static_cast<unsigned>(p);
 if(s.active&&at>0&&at<4&&s.sample.epoch==epoch.load(std::memory_order_acquire))s.sample.ticks[at]=Now();
}
void End(Span&s,bool prepared,bool completed,bool abnormal,uintptr_t result,uint32_t scaled)noexcept{
 ErrorGuard preserve;if(!s.active)return;s.active=false;
 if(!enabled.load(std::memory_order_acquire)||s.sample.epoch!=epoch.load(std::memory_order_acquire))return;
 if((s.sample.kind==Kind::Plan&&!prepared)||(s.sample.kind==Kind::Receipt&&!s.sample.parent)){rejected.fetch_add(1,std::memory_order_relaxed);return;}
 s.sample.ticks[4]=Now();s.sample.prepared=prepared;s.sample.completed=completed;s.sample.abnormal=abnormal;
 s.sample.result_raw=result;s.sample.scaled_items=scaled;
 uint64_t previous=s.sample.ticks[0];if(!previous||!s.sample.frequency){clockRejected.fetch_add(1);return;}
 for(unsigned i=1;i<5;++i)if(s.sample.ticks[i]){if(s.sample.ticks[i]<previous){clockRejected.fetch_add(1);return;}previous=s.sample.ticks[i];}
 if(!TryLock()){dropped.fetch_add(1);return;}
 if(s.sample.epoch!=epoch.load(std::memory_order_acquire)||!enabled.load(std::memory_order_acquire)){ReleaseSRWLockExclusive(&lock);return;}
 auto& budget=budgets[static_cast<unsigned>(s.sample.kind)];
 if(budget>=64||count==packets.size()){dropped.fetch_add(1);ReleaseSRWLockExclusive(&lock);return;}
 ++budget;packets[(head+count)%packets.size()]=s.sample;++count;ReleaseSRWLockExclusive(&lock);
}
std::size_t Drain(Sample*out,std::size_t capacity)noexcept{
 ErrorGuard preserve;if(!out||!capacity||!TryLock())return 0;
 std::size_t taken=0;while(taken<capacity&&count){out[taken++]=packets[head];head=(head+1)%packets.size();--count;}
 ReleaseSRWLockExclusive(&lock);return taken;
}
Statistics GetStatistics()noexcept{
 ErrorGuard preserve;Statistics s{dropped.load(),contended.load(),rejected.load(),clockRejected.load(),0,exhausted.load()};
 if(TryLock()){s.queued=count;ReleaseSRWLockExclusive(&lock);}return s;
}
void Heartbeat()noexcept{
 ErrorGuard preserve;
 const auto readyEpoch=epoch.load(std::memory_order_acquire);
 if(enabled.load(std::memory_order_acquire)&&!(readyEpoch&1)&&frequency.load()&&reportedEpoch.load()!=readyEpoch){
  reportedEpoch.store(readyEpoch);
  if(enabled.load(std::memory_order_acquire)&&epoch.load()==readyEpoch)
   Log("items-timing: READY_WAITING epoch=%llu frequency=%llu queueCapacity=128 perKindBudget=64 workerBatch=16 dropSummaryBudget=4 transferOnly=1 noCrossReturnPlanLink=1 SQL_UI_UNOBSERVED=1",readyEpoch,frequency.load());
 }
 Sample batch[16]{};const auto n=Drain(batch,16);
 for(std::size_t i=0;i<n;++i){const auto&s=batch[i];const char*kind=s.kind==Kind::Plan?"plan":s.kind==Kind::Push?"push":"receipt";
  Log("items-timing: kind=%s epoch=%llu id=%llu parentPush=%llu tid=%u objectRaw=0x%p callerRaw=0x%p startQpc=%llu prepUs=%.3f setupUs=%.3f publicationAndExistingLogUs=%.3f nativeUs=%.3f postNativeAndExistingLogUs=%.3f totalUs=%.3f scaledItems=%u completed=%d abnormal=%d resultRaw=0x%p SQL_UI_UNOBSERVED=1",
   kind,s.epoch,s.id,s.parent,s.thread,reinterpret_cast<void*>(s.object_raw),reinterpret_cast<void*>(s.caller_raw),s.ticks[0],
   Microseconds(s,0,1),Microseconds(s,0,2),Microseconds(s,1,2),Microseconds(s,2,3),Microseconds(s,3,4),Microseconds(s,0,4),
   s.scaled_items,s.completed?1:0,s.abnormal?1:0,reinterpret_cast<void*>(s.result_raw));
 }
 const auto stats=GetStatistics();const auto old=reportedDrops.exchange(stats.dropped);
 unsigned summaries=summaryCount.load();bool report=false;
 if(old!=stats.dropped){while(summaries<4){if(summaryCount.compare_exchange_strong(summaries,summaries+1)){report=true;break;}}}
 if(report)Log("items-timing: dropped=%llu contention=%llu rejectedUnqualified=%llu clockRejected=%llu sequenceExhausted=%d queueCapacity=128 perKindBudget=64 noCrossReturnPlanLink=1",
  stats.dropped,stats.contended,stats.rejected,stats.clock_rejected,stats.sequence_exhausted?1:0);
}
#ifdef PSM_ITEM_TIMING_TESTING
void TestLock()noexcept{AcquireSRWLockExclusive(&lock);}void TestUnlock()noexcept{ReleaseSRWLockExclusive(&lock);}void TestSequence(uint64_t value)noexcept{sequence.store(value);}
#endif
}
